// SPDX-License-Identifier: MPL-2.0
// randomizer_settings.h: the six pages' table and the summary of a seed's settings the game's Settings > Randomizer
// shows under its permalink (from setup's describe_settings in seeds.json).
#include "randomizer_settings.h"
#include <cassert>
#include <set>

using namespace rando_settings;

int main() {
    // every setting once, on six pages
    std::set<std::string> names;
    size_t rows = 0;
    for (const Page& p : kPages)
        for (size_t i = 0; i < p.count; i++, rows++) names.insert(p.rows[i].name);
    assert(std::size(kPages) == 6 && names.size() == rows);
    assert(label("Dungeon Small Keys") == "Small keys" && label("Unknown Thing") == "Unknown Thing");

    // entrances in a few words
    assert(entrances({}) == "not randomized");
    assert(entrances({{"Randomize Dungeon Entrances", 1}}) == "dungeons");
    assert(entrances({{"Randomize Dungeon Entrances", 1}, {"Randomize Cave Entrances", 1}, {"Mix Dungeons", 1}}) ==
           "dungeons, caves; mixed");
    std::map<std::string, int> full = {{"Randomize Dungeon Entrances", 1}, {"Randomize Boss Entrances", 1},
                                       {"Randomize Miniboss Entrances", 1}, {"Randomize Cave Entrances", 2},
                                       {"Randomize Door Entrances", 1},     {"Randomize Misc Entrances", 1},
                                       {"Mix Caves", 1},                    {"Decouple Entrances", 1},
                                       {"Random Start Island", 1}};
    assert(entrances(full) == "full (every kind); mixed, decoupled; random starting island");
    assert(entrances({{"Mix Dungeons", 1}, {"Decouple Entrances", 1}}) == "not randomized");  // (nothing to mix)

    // a seed setup has not described yet
    auto lines = summary(Description{});
    assert(lines.size() == 1 && lines[0].find("permalink") != std::string::npos);

    // a preset
    Description d;
    d.known = true, d.preset = d.closest = "standard";
    lines = summary(d);
    assert(lines.size() == 2 && lines[0] == "Settings: Standard (the preset)." && lines[1] == "Entrances: not randomized.");

    // a permalink with the entrance randomizer and keysanity
    d.preset.clear();
    d.values = full;
    d.changed = {{"Dungeon Small Keys", "Keysanity"}, {"Randomize Dungeon Entrances", "on"}, {"Decouple Entrances", "on"}};
    lines = summary(d);
    assert(lines.size() == 3);
    assert(lines[0].rfind("Settings: custom", 0) == 0 && lines[0].find("closest to Standard") != std::string::npos);
    assert(lines[1] == "Entrances: full (every kind); mixed, decoupled; random starting island.");
    assert(lines[2] == "Changed from Standard: Small keys: Keysanity.");

    // many differences: the first few
    d.changed.clear();
    for (const char* n : {"Progress Mail", "Progress Minigames", "Progress Free Gifts", "Progress Misc", "Progress Obscure"})
        d.changed.emplace_back(n, "on");
    lines = summary(d, 3);
    assert(lines[2] == "Changed from Standard: Mail: on, Minigames: on, Free gifts: on and 2 more (the spoiler log lists them all).");
    return 0;
}
