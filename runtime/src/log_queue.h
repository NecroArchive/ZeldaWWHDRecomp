// The log's writer queue (core.cpp log_msg): the calling thread formats a line and queues it, one writer thread
// writes the queued lines in order (the log file sink and stderr). A slow write (Windows: unbuffered stderr into a
// console window, a virus scanner on captures/wwhd.log) then stalls the writer thread, not the render or game thread
// that logged: a block of statistics lines at the swap took a whole frame and more there (a stutter every 300 frames).
//
// Records: a 4-byte length, the text and a NUL (the text without its '\n'). Order: one queue, one writer, so the lines
// come out in the order push() took the lock. Back-pressure: past `max_bytes` queued, push() waits for the writer (the
// log stays complete; a writer that cannot keep up slows the loggers, as writing on their thread did).
// flush() waits until everything queued before it is written (exit paths, fatal(), log_set_sink()).
// try_take() is for crash handlers: without blocking, the lines not yet taken by the writer.
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace logq {

class Queue {
public:
    using WriteFn = void (*)(const std::string& records);  // the writer thread: one batch of records, in order
    explicit Queue(WriteFn write, size_t max_bytes = 8u << 20) : write_(write), max_(max_bytes) {}
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // queue one line (under_lock runs first, under the queue's lock: core.cpp's crash ring keeps the same order)
    template <class F>
    void push(const char* s, size_t n, F&& under_lock) {
        std::unique_lock<std::mutex> lk(m_);
        under_lock();
        if (!started_) start();
        if (pending_.size() > max_ && !on_writer()) room_.wait(lk, [&] { return pending_.size() <= max_; });
        append(pending_, s, n);
        ++queued_;
        if (sleeping_) wake_.notify_one();
    }
    // true when all lines queued before the call are written (false: the timeout passed first)
    bool flush(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        std::unique_lock<std::mutex> lk(m_);
        if (!started_ || on_writer()) return pending_.empty();
        const uint64_t target = queued_;
        return idle_.wait_for(lk, timeout, [&] { return written_ >= target; });
    }
    // crash handlers: the queued lines the writer has not taken, when the lock is free and no batch is being
    // written (so they follow the lines already written); retries for about `ms` milliseconds; no waiting on locks
    bool try_take(std::string& out, int ms = 100) {
        for (int i = 0; i <= ms; ++i) {
            if (m_.try_lock()) {
                if (!writing_) {
                    out.swap(pending_);
                    pending_.clear();
                    written_ = queued_;
                    m_.unlock();
                    return true;
                }
                m_.unlock();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    uint64_t queued() {
        std::lock_guard<std::mutex> lk(m_);
        return queued_;
    }
    uint64_t written() {
        std::lock_guard<std::mutex> lk(m_);
        return written_;
    }

    static void append(std::string& rec, const char* s, size_t n) {
        const uint32_t len = uint32_t(n);
        rec.append(reinterpret_cast<const char*>(&len), sizeof len);
        rec.append(s, n);
        rec.push_back('\0');
    }
    // f(text, length) per record, in order (text is NUL-terminated)
    template <class F>
    static void each(const std::string& rec, F&& f) {
        size_t i = 0;
        while (i + sizeof(uint32_t) <= rec.size()) {
            uint32_t len;
            std::memcpy(&len, rec.data() + i, sizeof len);
            i += sizeof len;
            if (i + len + 1 > rec.size()) break;
            f(rec.data() + i, size_t(len));
            i += len + 1;
        }
    }

private:
    bool on_writer() const { return started_ && std::this_thread::get_id() == writer_id_; }
    void start() {  // (under m_)
        started_ = true;
        std::thread t([this] { run(); });
        writer_id_ = t.get_id();
        t.detach();
    }
    void run() {
        std::string batch;
        std::unique_lock<std::mutex> lk(m_);
        for (;;) {
            while (pending_.empty()) {
                sleeping_ = true;
                wake_.wait(lk);
                sleeping_ = false;
            }
            batch.swap(pending_);
            const uint64_t upto = queued_;
            writing_ = true;
            room_.notify_all();
            lk.unlock();
            write_(batch);
            batch.clear();
            lk.lock();
            writing_ = false;
            if (upto > written_) written_ = upto;
            idle_.notify_all();
        }
    }

    WriteFn write_;
    size_t max_;
    std::mutex m_;
    std::condition_variable wake_, idle_, room_;
    std::string pending_;
    uint64_t queued_ = 0, written_ = 0;
    bool started_ = false, sleeping_ = false, writing_ = false;
    std::thread::id writer_id_;
};

}  // namespace logq
