// SPDX-License-Identifier: MPL-2.0
// Every randomizer setting for a new randomized game, chosen in the game's Settings > Randomizer (desktop): the
// release's own randomizer (tools/bin/wwhd-rando, TWWHD Randomizer 1.2.0) keeps them, with the same commands the
// setup window's Randomized games > New game uses (setup.py rando_change: settings, preset, set, load-permalink,
// set-seed, new-seed), in a work folder of the game's own (<data>/randomizer/game-settings: config.yaml and
// preferences.yaml, kept from one start to the next). The randomizer makes the permalink of the settings, so a game
// made from it (randomizer_job, the same path as a pasted permalink) is the game those settings make.
// The commands run in order on a worker thread (a few hundredths of a second each); each is followed by `settings`,
// and state() is what the last one said, with the changes asked for since then already in it.
#pragma once
#include "../platform/process.h"

#include <functional>
#include <string>
#include <vector>

namespace rando_model {

struct Setting {
    std::string name;        // the randomizer's name (runtime/src/randomizer_settings.h has our labels)
    std::string kind;        // bool | count | choice
    int value = 0, lo = 0, hi = 1;
    std::vector<std::string> choices;  // choice: the values' names
    bool in_permalink = true;          // false: a preference (camera, HUD, ...), not part of the game made
};

struct State {
    bool loaded = false;     // the randomizer answered once
    bool busy = false;       // commands are waiting or running
    std::string error;       // the last refusal ("" none), e.g. a permalink it can't read
    std::vector<Setting> settings;
    std::string seed, permalink;
    std::string preset;      // the preset the settings are ("" custom)
    const Setting* find(const std::string& name) const {
        for (const auto& s : settings)
            if (s.name == name) return &s;
        return nullptr;
    }
};

// The commands (each queued; the state shows a set() at once)
void load();
void preset(const std::string& name);
void set(const std::string& name, int value);
void load_permalink(const std::string& link);
void set_seed(const std::string& seed);
void new_seed();
// `done` runs on the worker thread with the state once the commands queued before it have run
void then(std::function<void(const State&)> done);
State state();

// ---- parts, for tests
using Runner = std::function<host::ProcessResult(const std::vector<std::string>& args)>;
// replaces how a command runs (default: the release's wwhd-rando through host::run_process) and forgets the state
void set_runner(Runner runner);
// the command line of `args` (exe --data DIR --work DIR args...); empty and `why` when this release has none
std::vector<std::string> command(const std::vector<std::string>& args, std::string& why);
// the JSON line a command printed: {"ok": ...}; false and `error` when it refused or printed none
bool parse_result(const std::string& output, std::string& json, std::string& error);
// the settings command's answer into `s` (settings, seed, permalink, preset)
bool parse_settings(const std::string& json, State& s, std::string& error);

}  // namespace rando_model
