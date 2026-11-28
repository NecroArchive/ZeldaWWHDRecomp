// SPDX-License-Identifier: MPL-2.0
// Settings > Randomizer's settings for a new game (mods/randomizer_settings_model.cpp): the randomizer's command line
// from the installed release (guest-sdk.json, as the randomizer job finds setup); its answers read; the commands in
// order, each followed by settings; a set shown at once; then() after the queue; refusals kept as the error.
// A stand-in randomizer answers like tools/rando/cli/main.cpp. With an argument (a wwhd-rando build folder,
// cmake -S tools/rando -B DIR) it also runs the real randomizer: the permalink the game's choices give equals the one
// the randomizer gives for the same commands in a folder of its own, and preset / custom follow the choices.
#include "mods/code_mods.h"
#include "mods/mod_json.h"
#include "mods/randomizer_settings_model.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hostui {
bool get(const char*, std::string&) { return false; }
void set(const char*, const std::string&) {}
void post(std::function<void()> fn) { fn(); }
}  // namespace hostui
namespace mods::packages {
bool enable_after_code_rebuild(const std::string&, std::string&) { return true; }
}  // namespace mods::packages

namespace fs = std::filesystem;
using namespace rando_model;

static void env(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}

static State settled() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (state().busy) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return state();
}

// the stand-in randomizer: two settings, presets, seed, permalink "P-<mail>-<camera>-<seed>"
struct Fake {
    std::mutex m;
    std::vector<std::vector<std::string>> calls;
    int mail = 0, camera = 0;
    std::string seed = "Seed0";
    bool slow = false;
    host::ProcessResult operator()(const std::vector<std::string>& a) {
        if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::lock_guard lock(m);
        calls.push_back(a);
        auto answer = [](std::string body) {
            host::ProcessResult r;
            r.code = 0;
            r.output = "[rando] a log line {not json}\n{\"ok\":true" + body + "}\n";
            return r;
        };
        auto refuse = [](const std::string& why) {
            host::ProcessResult r;
            r.code = 1;
            r.output = "{\"ok\":false,\"error\":\"" + why + "\"}\n";
            return r;
        };
        const std::string link = "P-" + std::to_string(mail) + "-" + seed;
        if (a[0] == "settings")
            return answer(",\"settings\":{\"Progress Mail\":{\"value\":" + std::to_string(mail) +
                          ",\"kind\":\"bool\",\"min\":0,\"max\":1,\"choices\":[],\"in_permalink\":true},"
                          "\"Camera\":{\"value\":" + std::to_string(camera) +
                          ",\"kind\":\"choice\",\"min\":0,\"max\":1,\"choices\":[\"Standard\",\"Reverse left/right\"],"
                          "\"in_permalink\":false}},\"seed\":\"" + seed + "\",\"permalink\":\"" + link +
                          "\",\"preset\":\"" + (mail ? "everything" : "standard") + "\"");
        if (a[0] == "preset") return mail = a[1] == "everything", answer("");
        if (a[0] == "set" && a[1] == "Progress Mail") return mail = std::stoi(a[2]), answer("");
        if (a[0] == "set" && a[1] == "Camera") return camera = std::stoi(a[2]), answer("");
        if (a[0] == "set") return refuse("no plain setting named " + a[1]);
        if (a[0] == "set-seed") return seed = a[1], answer("");
        if (a[0] == "new-seed") return seed = "Random", answer("");
        if (a[0] == "load-permalink") {
            if (a[1].rfind("P-", 0) != 0) return refuse("permalink: INVALID");
            mail = a[1][2] - '0', seed = a[1].substr(4);
            return answer("");
        }
        return refuse("unknown");
    }
};

static void real(const fs::path& build) {
    // the real randomizer: the game's own folder and one of the test's, the same commands, the same permalink
    const fs::path exe = build / "wwhd-rando", data = build / "data";
    env("WWHD_RANDO_EXE", exe.string());
    env("WWHD_RANDO_DATA", data.string());
    set_runner(nullptr);
    preset("standard");
    set_seed("ModelTest");
    State s = settled();
    assert(s.loaded && s.error.empty() && s.preset == "standard" && s.seed == "ModelTest");
    const std::vector<std::pair<std::string, int>> chosen = {
        {"Randomize Dungeon Entrances", 1}, {"Randomize Cave Entrances", 2}, {"Mix Dungeons", 1},
        {"Decouple Entrances", 1},          {"Dungeon Small Keys", 4},       {"Path Hints", 5},
        {"Required Boss Items", 1},         {"Pig Color", 1}};
    for (const auto& [n, v] : chosen) set(n, v);
    s = settled();
    assert(s.error.empty() && s.preset.empty());
    for (const auto& [n, v] : chosen) assert(s.find(n) && s.find(n)->value == v);
    assert(!s.find("Pig Color")->in_permalink && s.find("Mix Dungeons")->in_permalink);
    // the randomizer itself, in a folder of its own
    const fs::path other = fs::temp_directory_path() / ("wwhd-rando-model-" + std::to_string(std::rand()));
    auto cli = [&](std::vector<std::string> a) {
        std::vector<std::string> c = {exe.string(), "--data", data.string(), "--work", other.string()};
        c.insert(c.end(), a.begin(), a.end());
        std::string line, error;
        const bool ok = parse_result(host::run_process(c).output, line, error);
        assert(ok);
        return line;
    };
    cli({"preset", "standard"});
    cli({"set-seed", "ModelTest"});
    for (const auto& [n, v] : chosen) cli({"set", n, std::to_string(v)});
    State theirs;
    std::string error;
    assert(parse_settings(cli({"settings"}), theirs, error));
    assert(theirs.permalink == s.permalink);
    // and a pasted permalink fills the settings
    preset("everything");
    assert(settled().preset == "everything");
    load_permalink(theirs.permalink);
    s = settled();
    assert(s.permalink == theirs.permalink && s.find("Randomize Cave Entrances")->value == 2);
    fs::remove_all(other);
}

int main(int argc, char** argv) {
    const auto root = fs::temp_directory_path() /
                      ("wwhd-rando-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path release = root / "release", data = root / "data";
    fs::create_directories(release / "tools" / "installer");
    fs::create_directories(release / "tools" / "bin");
    fs::create_directories(data);
    std::ofstream(release / "tools" / "installer" / "setup.py") << "";
    mods::json::Value config;
    config["format_version"] = 2;
    config["python"].type = mods::json::Value::Array;
    config["python"].array.emplace_back("python3");
    config["setup"] = (release / "tools" / "installer" / "setup.py").string();
    config["data_dir"] = data.string();
    std::ofstream(root / "guest-sdk.json") << mods::json::dump(config);
    env("WWHD_GUEST_BUILD_CONFIG", (root / "guest-sdk.json").string());
    env("WWHD_RANDO_EXE", "");
    env("WWHD_RANDO_DATA", "");

    // the command line: the release's randomizer, its data, the game's own work folder
    std::string why;
    assert(command({"settings"}, why).empty() && why.find("no randomizer") != std::string::npos);
#ifdef _WIN32
    const fs::path exe = release / "tools" / "bin" / "wwhd-rando.exe";
#else
    const fs::path exe = release / "tools" / "bin" / "wwhd-rando";
#endif
    std::ofstream(exe) << "";
    const auto cmd = command({"set", "Progress Mail", "1"}, why);
    assert(cmd.size() == 8 && cmd[0] == exe.string() && cmd[1] == "--data" &&
           cmd[2] == (release / "tools" / "rando" / "data").string() && cmd[3] == "--work" &&
           cmd[4] == (data / "randomizer" / "game-settings").string() && cmd[5] == "set" && cmd[7] == "1");

    // answers: the last {"ok" line, after the randomizer's log lines
    std::string line, error;
    assert(parse_result("log {x}\n{\"ok\":true,\"seed\":\"A\"}\n", line, error) && line == "{\"ok\":true,\"seed\":\"A\"}");
    assert(!parse_result("{\"ok\":false,\"error\":\"no\"}", line, error) && error == "no");
    error.clear();
    assert(!parse_result("nothing\n", line, error) && error.empty());

    // the model on the stand-in
    Fake fake;
    set_runner([&](const std::vector<std::string>& a) { return fake(a); });
    load();
    State s = settled();
    assert(s.loaded && s.preset == "standard" && s.permalink == "P-0-Seed0" && s.settings.size() == 2);
    assert(s.find("Camera") && !s.find("Camera")->in_permalink && s.find("Camera")->choices.size() == 2);

    // a preset fills the settings; a change is custom (here: the stand-in's other preset) and shows at once
    preset("everything");
    s = settled();
    assert(s.preset == "everything" && s.find("Progress Mail")->value == 1 && s.permalink == "P-1-Seed0");
    fake.slow = true;
    set("Progress Mail", 0);
    assert(state().find("Progress Mail")->value == 0 && state().busy);
    s = settled();
    assert(s.preset == "standard" && s.permalink == "P-0-Seed0");
    fake.slow = false;

    // in order, each followed by settings
    fake.calls.clear();
    set_seed("Alpha");
    set("Camera", 1);
    s = settled();
    assert(fake.calls.size() == 4 && fake.calls[0][0] == "set-seed" && fake.calls[1][0] == "settings" &&
           fake.calls[2][0] == "set" && fake.calls[3][0] == "settings");
    assert(s.seed == "Alpha" && s.find("Camera")->value == 1);

    // a pasted permalink fills them; a refused one keeps them and says why
    load_permalink("P-1-Pasted");
    s = settled();
    assert(s.find("Progress Mail")->value == 1 && s.seed == "Pasted" && s.error.empty());
    load_permalink("garbage");
    s = settled();
    assert(s.error == "permalink: INVALID" && s.permalink == "P-1-Pasted");
    set("No Such Setting", 1);
    assert(settled().error.find("no plain setting") != std::string::npos);

    // then(): after what was asked before it, with its state (Make this game: a new seed, then its permalink)
    std::string made;
    std::mutex made_mutex;
    new_seed();
    then([&](const State& st) {
        std::lock_guard lock(made_mutex);
        made = st.permalink;
    });
    settled();
    {
        std::lock_guard lock(made_mutex);
        assert(made == "P-1-Random");
    }

    // no randomizer answer: the error
    set_runner([](const std::vector<std::string>&) {
        host::ProcessResult r;
        r.error = "cannot start the randomizer";
        return r;
    });
    load();
    s = settled();
    assert(!s.loaded && s.error == "cannot start the randomizer");

    if (argc > 1) real(argv[1]);
    set_runner(nullptr);
    fs::remove_all(root);
    return 0;
}
