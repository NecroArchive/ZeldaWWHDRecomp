// SPDX-License-Identifier: MPL-2.0
#include "randomizer_settings_model.h"

#include "code_mods.h"
#include "mod_json.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <thread>

namespace rando_model {
namespace {
namespace fs = std::filesystem;
namespace json = mods::json;

std::mutex mutex;
std::condition_variable wake;
State current;
std::deque<std::function<void()>> queue;
bool running = false;  // the worker is running a command
Runner runner;

struct Worker {
    std::thread thread;
    bool stopping = false;
    ~Worker() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            queue.clear();
        }
        wake.notify_all();
        if (thread.joinable()) thread.join();
    }
} worker;

host::ProcessResult run(const std::vector<std::string>& args) {
    if (runner) return runner(args);
    std::string why;
    const auto cmd = command(args, why);
    if (cmd.empty()) {
        host::ProcessResult r;
        r.error = why;
        return r;
    }
    return host::run_process(cmd);
}

// one randomizer command; then its settings (the state), unless it was settings itself
void step(const std::vector<std::string>& args) {
    std::string error, line;
    if (args.front() != "settings") {
        const auto r = run(args);
        if (!parse_result(r.error.empty() ? r.output : std::string(), line, error) && error.empty())
            error = r.error.empty() ? "the randomizer did not answer" : r.error;
        if (!error.empty()) fprintf(stderr, "[randomizer settings] %s: %s\n", args.front().c_str(), error.c_str());
    }
    const auto r = run({"settings"});
    State fresh;
    std::string why;
    const bool read = parse_result(r.error.empty() ? r.output : std::string(), line, why) && parse_settings(line, fresh, why);
    if (!read && why.empty()) why = r.error.empty() ? "the randomizer did not answer" : r.error;
    std::lock_guard lock(mutex);
    if (read) {
        fresh.loaded = true;
        fresh.error = error;
        current = std::move(fresh);
    } else {
        current.error = error.empty() ? why : error;
    }
}

void worker_loop() {
    std::unique_lock lock(mutex);
    while (!worker.stopping) {
        if (queue.empty()) {
            wake.wait(lock);
            continue;
        }
        auto job = std::move(queue.front());
        queue.pop_front();
        running = true;
        lock.unlock();
        try {
            job();
        } catch (const std::exception& e) {
            std::lock_guard l(mutex);
            current.error = e.what();
        }
        lock.lock();
        running = false;
    }
}

void enqueue(std::function<void()> job) {
    {
        std::lock_guard lock(mutex);
        queue.push_back(std::move(job));
        if (!worker.thread.joinable()) worker.thread = std::thread(worker_loop);
    }
    wake.notify_all();
}

void command_job(std::vector<std::string> args) {
    enqueue([args = std::move(args)] { step(args); });
}
}  // namespace

std::vector<std::string> command(const std::vector<std::string>& args, std::string& why) {
    fs::path data, script;
    try {
        mods::code::setup_command(data, &script);
    } catch (const std::exception& e) {
        why = e.what();
        return {};
    }
    // setup.py is <release>/tools/installer/setup.py: the release's randomizer and its data beside it (setup's
    // randomizer_seeds.tool, with the same overrides)
    const fs::path release = script.parent_path().parent_path().parent_path();
#ifdef _WIN32
    fs::path exe = release / "tools" / "bin" / "wwhd-rando.exe";
#else
    fs::path exe = release / "tools" / "bin" / "wwhd-rando";
#endif
    fs::path rando_data = release / "tools" / "rando" / "data";
    if (const char* e = std::getenv("WWHD_RANDO_EXE"); e && *e) exe = e;
    if (const char* d = std::getenv("WWHD_RANDO_DATA"); d && *d) rando_data = d;
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) return why = "This release has no randomizer.", std::vector<std::string>{};
    std::vector<std::string> out = {exe.string(), "--data", rando_data.string(), "--work",
                                    (data / "randomizer" / "game-settings").string()};
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

bool parse_result(const std::string& output, std::string& line, std::string& error) {
    // (stdout and stderr together: the randomizer's log lines go to stderr; its answer is the last {"ok"... line)
    size_t end = output.size();
    while (end > 0) {
        size_t start = output.rfind('\n', end - 1);
        start = start == std::string::npos ? 0 : start + 1;
        std::string l = output.substr(start, end - start);
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (l.rfind("{\"ok\"", 0) == 0) {
            try {
                const json::Value v = json::parse(l);
                if (!v.get("ok").boolean) return error = v.get("error").string("the randomizer refused"), false;
                line = l;
                return true;
            } catch (const std::exception& e) {
                return error = e.what(), false;
            }
        }
        if (start == 0) break;
        end = start - 1;
    }
    return false;
}

bool parse_settings(const std::string& line, State& s, std::string& error) {
    try {
        const json::Value v = json::parse(line);
        if (v.get("settings").type != json::Value::Object) return error = "no settings in the randomizer's answer", false;
        s.settings.clear();
        for (const auto& [name, d] : v.get("settings").object) {
            Setting x;
            x.name = name;
            x.kind = d.get("kind").string("bool");
            x.value = int(d.get("value").number), x.lo = int(d.get("min").number), x.hi = int(d.get("max").number);
            for (const auto& c : d.get("choices").array) x.choices.push_back(c.string());
            x.in_permalink = d.get("in_permalink").type != json::Value::Bool || d.get("in_permalink").boolean;
            s.settings.push_back(std::move(x));
        }
        s.seed = v.get("seed").string(), s.permalink = v.get("permalink").string(), s.preset = v.get("preset").string();
        return true;
    } catch (const std::exception& e) {
        return error = e.what(), false;
    }
}

void load() { command_job({"settings"}); }
void preset(const std::string& name) { command_job({"preset", name}); }
void set(const std::string& name, int value) {
    {
        std::lock_guard lock(mutex);
        for (auto& s : current.settings)
            if (s.name == name) s.value = value;
    }
    command_job({"set", name, std::to_string(value)});
}
void load_permalink(const std::string& link) { command_job({"load-permalink", link}); }
void set_seed(const std::string& seed) { command_job({"set-seed", seed}); }
void new_seed() { command_job({"new-seed"}); }

void then(std::function<void(const State&)> done) {
    enqueue([done = std::move(done)] {
        State s;
        {
            std::lock_guard lock(mutex);
            s = current;
        }
        done(s);
    });
}

State state() {
    std::lock_guard lock(mutex);
    State s = current;
    s.busy = running || !queue.empty();
    return s;
}

void set_runner(Runner r) {
    std::lock_guard lock(mutex);
    runner = std::move(r);
    current = State{};
}

}  // namespace rando_model
