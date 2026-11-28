// The log's writer queue (runtime/src/log_queue.h): lines come out complete and in order, a slow writer does not
// slow the threads that log, flush() waits for what was queued, back-pressure keeps every line, and a crash
// handler's try_take() does not take lines while a batch is being written.
#include "log_queue.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono;

static std::mutex g_m;
static std::vector<std::string> g_out;        // what the writer wrote, in order
static std::atomic<int> g_delay_ms{0};        // a slow sink (a console window on Windows)
static std::atomic<bool> g_gate{false};       // the writer waits while true
static std::atomic<bool> g_in_write{false};
static std::atomic<std::thread::id> g_writer_thread;

static void writer(const std::string& rec) {
    g_writer_thread = std::this_thread::get_id();
    g_in_write = true;
    while (g_gate) std::this_thread::sleep_for(milliseconds(1));
    if (g_delay_ms) std::this_thread::sleep_for(milliseconds(g_delay_ms.load()));
    std::lock_guard<std::mutex> lk(g_m);
    logq::Queue::each(rec, [](const char* s, size_t n) {
        assert(s[n] == '\0');
        g_out.emplace_back(s, n);
    });
    g_in_write = false;
}
static void reset() {
    std::lock_guard<std::mutex> lk(g_m);
    g_out.clear();
    g_delay_ms = 0;
    g_gate = false;
}
static void push(logq::Queue& q, const std::string& s, std::vector<std::string>* order = nullptr) {
    q.push(s.data(), s.size(), [&] { if (order) order->push_back(s); });
}

// records: lengths, empty lines, a NUL after each text
static void records() {
    std::string rec;
    logq::Queue::append(rec, "abc", 3);
    logq::Queue::append(rec, "", 0);
    logq::Queue::append(rec, "a line that is a bit longer", 27);
    std::vector<std::string> got;
    logq::Queue::each(rec, [&](const char* s, size_t n) { got.emplace_back(s, n); });
    assert(got.size() == 3 && got[0] == "abc" && got[1].empty() && got[2] == "a line that is a bit longer");
    rec.resize(rec.size() - 3);  // a cut record is left out
    got.clear();
    logq::Queue::each(rec, [&](const char* s, size_t n) { got.emplace_back(s, n); });
    assert(got.size() == 2);
}

// many threads: every line once, each thread's lines in its order, the whole in the order the lock was taken
static void order() {
    reset();
    auto& q = *new logq::Queue(writer);  // (never destroyed: its writer thread stays, as in core.cpp)
    std::vector<std::string> taken;  // filled under the queue's lock (as core.cpp's crash ring)
    constexpr int kThreads = 8, kLines = 5000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([&, t] {
            for (int i = 0; i < kLines; ++i) push(q, "[t" + std::to_string(t) + "] " + std::to_string(i), &taken);
        });
    for (auto& t : ts) t.join();
    assert(q.flush());
    std::lock_guard<std::mutex> lk(g_m);
    assert(g_out.size() == size_t(kThreads * kLines));
    assert(g_out == taken);
    std::vector<int> next(kThreads, 0);
    for (const auto& l : g_out) {
        const int t = l[2] - '0', i = atoi(l.c_str() + l.find(' ') + 1);
        assert(next[t] == i);
        next[t]++;
    }
    assert(q.queued() == uint64_t(kThreads * kLines) && q.written() == q.queued());
    assert(g_writer_thread.load() != std::this_thread::get_id());
}

// a sink that takes 30 ms a batch: pushing 20 report lines stays far below that (formatting and a copy)
static void slow_writer_does_not_block() {
    reset();
    auto& q = *new logq::Queue(writer);  // (never destroyed: its writer thread stays, as in core.cpp)
    push(q, "[boot] first line");  // the writer thread is running
    assert(q.flush());
    g_delay_ms = 30;
    const std::string line(600, 'x');
    double worst_us = 0, total_us = 0;
    for (int block = 0; block < 5; ++block) {
        const auto t0 = steady_clock::now();
        for (int i = 0; i < 20; ++i) push(q, "[native-draw] " + std::to_string(block) + " " + line);
        const double us = duration<double, std::micro>(steady_clock::now() - t0).count();
        worst_us = std::max(worst_us, us);
        total_us += us;
        std::this_thread::sleep_for(milliseconds(5));
    }
    assert(q.flush());
    printf("log_queue: 20 lines of 600 bytes with a 30 ms writer: worst block %.0f us, average %.0f us\n", worst_us,
           total_us / 5);
    assert(worst_us < 10000);  // (the old synchronous write took the writer's 30 ms, or more, per line)
    std::lock_guard<std::mutex> lk(g_m);
    assert(g_out.size() == 101);
}

// flush() waits for lines a slow writer has not written yet; a timeout says false
static void flush_waits() {
    reset();
    auto& q = *new logq::Queue(writer);  // (never destroyed: its writer thread stays, as in core.cpp)
    g_delay_ms = 50;
    for (int i = 0; i < 10; ++i) push(q, "line " + std::to_string(i));
    assert(q.flush());
    {
        std::lock_guard<std::mutex> lk(g_m);
        assert(g_out.size() == 10 && g_out.back() == "line 9");
    }
    g_gate = true;
    push(q, "held");
    assert(!q.flush(milliseconds(20)));
    g_gate = false;
    assert(q.flush());
    std::lock_guard<std::mutex> lk(g_m);
    assert(g_out.back() == "held");
}

// past max_bytes queued, push waits for the writer: nothing is dropped, the order stays
static void back_pressure() {
    reset();
    auto& q = *new logq::Queue(writer, 1024);
    g_delay_ms = 2;
    std::vector<std::string> taken;
    for (int i = 0; i < 400; ++i) push(q, "pressure " + std::to_string(i) + std::string(100, '.'), &taken);
    assert(q.flush());
    std::lock_guard<std::mutex> lk(g_m);
    assert(g_out == taken && g_out.size() == 400);
}

// try_take (crash handlers): refused while a batch is being written (the lines would come out before it); after
// everything is written: nothing to take
static void crash_take() {
    reset();
    auto& q = *new logq::Queue(writer);  // (never destroyed: its writer thread stays, as in core.cpp)
    g_gate = true;
    push(q, "in the batch being written");
    while (!g_in_write) std::this_thread::sleep_for(milliseconds(1));
    push(q, "queued after it");
    std::string rec;
    assert(!q.try_take(rec, 5) && rec.empty());
    g_gate = false;
    assert(q.flush());
    assert(q.try_take(rec, 5) && rec.empty());
    std::lock_guard<std::mutex> lk(g_m);
    assert(g_out.size() == 2 && g_out[1] == "queued after it");
}

// the writer may log itself (a sink that logs): no wait on its own queue
static logq::Queue* g_self = nullptr;
static std::atomic<int> g_self_writes{0};
static void self_writer(const std::string& rec) {
    logq::Queue::each(rec, [](const char* s, size_t n) {
        if (g_self_writes++ == 0) g_self->push("from the writer", 15, [] {});
        (void)s, (void)n;
    });
}
static void writer_logs() {
    auto& q = *new logq::Queue(self_writer, 16);
    g_self = &q;
    q.push("first", 5, [] {});
    assert(q.flush());  // (the writer queued its line while writing the first: the second flush waits for it)
    assert(q.flush());
    assert(g_self_writes == 2);
}

int main() {
    records();
    order();
    slow_writer_does_not_block();
    flush_waits();
    back_pressure();
    crash_take();
    writer_logs();
    puts("log_queue: ok");
    return 0;
}
