// SPDX-License-Identifier: MPL-2.0
// The randomizer's settings as the player sees them: the six pages of settings by the randomizer's own names
// (tools/rando/twwhd_rando/options.cpp nameToSetting, the names wwhd-rando's settings command gives) with the port's
// labels, shared by the setup window (tools/installer/gui/setup_gui.cpp, Randomized games > New game) and the game's
// Settings > Randomizer (overlay/randomizer_tab.cpp); and the short summary of a seed's settings that page shows,
// from the description setup keeps per seed (tools/installer/randomizer_seeds.py describe_settings: the preset the
// settings are, or the closest preset and what differs from it, and every setting's value).
#pragma once
#include <cstddef>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rando_settings {

struct Row {
    const char* name;   // the randomizer's name
    const char* label;  // ours
};

inline constexpr Row kPlaces[] = {
    {"Progress Dungeons", "Dungeons"}, {"Progress Great Fairies", "Great Fairies"}, {"Progress Puzzle Caves", "Puzzle caves"},
    {"Progress Combat Caves", "Combat caves"}, {"Progress Short Sidequests", "Short sidequests"},
    {"Progress Long Sidequests", "Long sidequests"}, {"Progress Spoils Trading", "Spoils trading"},
    {"Progress Minigames", "Minigames"}, {"Progress Free Gifts", "Free gifts"}, {"Progress Mail", "Mail"},
    {"Progress Platforms Rafts", "Platforms and rafts"}, {"Progress Submarines", "Submarines"},
    {"Progress Eye Reefs", "Eye reef chests"}, {"Progress Octos Gunboats", "Big Octos and gunboats"},
    {"Progress Triforce Charts", "Triforce charts"}, {"Progress Treasure Charts", "Treasure charts"},
    {"Progress Exp Purchases", "Expensive purchases"}, {"Progress Misc", "Miscellaneous"},
    {"Progress Tingle Chests", "Tingle chests"}, {"Progress Battlesquid", "Battlesquid"},
    {"Progress Savage Labyrinth", "Savage Labyrinth"}, {"Progress Island Puzzles", "Island puzzles"},
    {"Progress Dungeon Secrets", "Dungeon secrets"}, {"Progress Obscure", "Obscure"},
};
inline constexpr Row kDungeons[] = {
    {"Num Required Dungeons", "Required dungeons"}, {"Required Boss Items", "Required bosses' items"},
    {"Dungeon Small Keys", "Small keys"}, {"Dungeon Big Keys", "Big keys"}, {"Dungeon Maps And Compasses", "Maps and compasses"},
    {"Remove Swords", "Swordless"}, {"Random Charts", "Randomize charts"}, {"CTMC", "Chest type matches contents"},
    {"Skip Refights", "Skip boss refights"}, {"Open DRC", "Open Dragon Roost Cavern"},
    {"Progressive Magic Always Double", "Magic always double"}, {"Damage Multiplier", "Damage multiplier"},
};
inline constexpr Row kEntrances[] = {
    {"Randomize Dungeon Entrances", "Dungeon entrances"}, {"Randomize Boss Entrances", "Boss entrances"},
    {"Randomize Miniboss Entrances", "Miniboss entrances"}, {"Randomize Cave Entrances", "Cave entrances"},
    {"Randomize Door Entrances", "Door entrances"}, {"Randomize Misc Entrances", "Other entrances"},
    {"Mix Dungeons", "Mix dungeons"}, {"Mix Bosses", "Mix bosses"}, {"Mix Minibosses", "Mix minibosses"},
    {"Mix Caves", "Mix caves"}, {"Mix Doors", "Mix doors"}, {"Mix Misc", "Mix others"},
    {"Decouple Entrances", "Decouple entrances"}, {"Random Start Island", "Random starting island"},
};
inline constexpr Row kHints[] = {
    {"Ho Ho Hints", "Old Man Ho Ho hints"}, {"Ho Ho Triforce Hints", "Ho Ho Triforce hints"}, {"Korl Hints", "King of Red Lions hints"},
    {"Korl Sword Hints", "KoRL sword hints"}, {"Kreeb Bow Hints", "Kreeb bow hints"}, {"Clearer Hints", "Clearer hints"},
    {"Use Always Hints", "Always hints"}, {"Hint Importance", "Hint importance"}, {"Path Hints", "Path hints"},
    {"Barren Hints", "Barren hints"}, {"Item Hints", "Item hints"}, {"Location Hints", "Location hints"},
};
inline constexpr Row kStart[] = {
    {"Starting HC", "Heart containers"}, {"Starting HP", "Pieces of heart"}, {"Starting Joy Pendants", "Joy pendants"},
    {"Starting Skull Necklaces", "Skull necklaces"}, {"Starting Boko Baba Seeds", "Boko Baba seeds"},
    {"Starting Golden Feathers", "Golden feathers"}, {"Starting Knights Crests", "Knight's crests"},
    {"Starting Red Chu Jellys", "Red Chu jellies"}, {"Starting Green Chu Jellys", "Green Chu jellies"},
    {"Starting Blue Chu Jellys", "Blue Chu jellies"}, {"Start With Random Item", "Start with a random item"},
};
inline constexpr Row kConvenience[] = {
    {"Instant Text", "Instant text boxes"}, {"Reveal Sea Chart", "Reveal the full sea chart"},
    {"Quiet Swift Sail", "Quiet Swift Sail"}, {"Add Shortcut Warps", "Warps between dungeons"}, {"Fix RNG", "Fixed RNG"},
    {"Performance", "Performance"}, {"Invert Compass", "Invert the sea compass"}, {"Remove Music", "Remove music"},
    {"Classic Mode", "Classic mode"}, {"Pig Color", "Outset pig colour"}, {"Target Type", "Targeting"},
    {"Camera", "Camera"}, {"First Person Camera", "First-person camera"}, {"Gyroscope", "Gyroscope"}, {"UI Display", "HUD"},
};

struct Page {
    const char* title;
    const Row* rows;
    size_t count;
};
inline constexpr Page kPages[] = {
    {"Where items can be", kPlaces, std::size(kPlaces)}, {"Dungeons", kDungeons, std::size(kDungeons)},
    {"Entrances", kEntrances, std::size(kEntrances)},    {"Hints", kHints, std::size(kHints)},
    {"Starting items", kStart, std::size(kStart)},       {"Convenience", kConvenience, std::size(kConvenience)},
};

// our label of a setting (the randomizer's name when it has none)
inline std::string label(const std::string& name) {
    for (const Page& p : kPages)
        for (size_t i = 0; i < p.count; i++)
            if (name == p.rows[i].name) return p.rows[i].label;
    return name;
}

inline bool is_entrance_setting(const std::string& name) {
    for (const Row& r : kEntrances)
        if (name == r.name) return true;
    return false;
}

// "Beginner" for "beginner"
inline std::string preset_label(const std::string& preset) {
    if (preset == "beginner") return "Beginner";
    if (preset == "standard") return "Standard";
    if (preset == "everything") return "Everything";
    return preset;
}

// the entrance randomizer in a few words, from the settings' values: "not randomized", "dungeons", "full (every
// kind)", "dungeons, caves and fairy fountains; mixed, decoupled; random starting island", ...
inline std::string entrances(const std::map<std::string, int>& values) {
    auto on = [&](const char* n) {
        auto it = values.find(n);
        return it != values.end() ? it->second : 0;
    };
    std::vector<std::string> kinds;
    if (on("Randomize Dungeon Entrances")) kinds.push_back("dungeons");
    if (on("Randomize Boss Entrances")) kinds.push_back("bosses");
    if (on("Randomize Miniboss Entrances")) kinds.push_back("minibosses");
    if (const int c = on("Randomize Cave Entrances")) kinds.push_back(c >= 2 ? "caves and fairy fountains" : "caves");
    if (on("Randomize Door Entrances")) kinds.push_back("doors");
    if (on("Randomize Misc Entrances")) kinds.push_back("other entrances");
    std::string out;
    if (kinds.empty()) out = "not randomized";
    else if (kinds.size() == 6 && on("Randomize Cave Entrances") >= 2) out = "full (every kind)";
    else
        for (size_t i = 0; i < kinds.size(); i++) out += (i ? ", " : "") + kinds[i];
    std::vector<std::string> how;
    if (!kinds.empty() && (on("Mix Dungeons") || on("Mix Bosses") || on("Mix Minibosses") || on("Mix Caves") || on("Mix Doors") ||
                           on("Mix Misc")))
        how.push_back("mixed");
    if (!kinds.empty() && on("Decouple Entrances")) how.push_back("decoupled");
    for (size_t i = 0; i < how.size(); i++) out += (i ? ", " : "; ") + how[i];
    if (on("Random Start Island")) out += "; random starting island";
    return out;
}

// a seed's settings as setup describes them (seeds.json "settings")
struct Description {
    bool known = false;   // setup described them (a seed made before that, not yet described: false)
    std::string preset;   // the preset they are ("" custom)
    std::string closest;  // the preset with the fewest differences
    std::vector<std::pair<std::string, std::string>> changed;  // name, value text: where they differ from closest
    std::map<std::string, int> values;                          // every setting
};

// the summary lines the game's Settings > Randomizer shows under a seed's permalink: what its settings are, its
// entrances, and (custom settings) up to `most` other differences from the closest preset
inline std::vector<std::string> summary(const Description& d, size_t most = 6) {
    std::vector<std::string> lines;
    if (!d.known) {
        lines.push_back("Its settings are those of its permalink (setup describes them here once its Randomized games "
                        "page opens or it makes the next randomized game).");
        return lines;
    }
    if (!d.preset.empty()) lines.push_back("Settings: " + preset_label(d.preset) + " (the preset).");
    else lines.push_back("Settings: custom (a permalink or the setup window's settings), closest to " +
                         preset_label(d.closest) + ".");
    lines.push_back("Entrances: " + entrances(d.values) + ".");
    std::vector<std::string> other;
    for (const auto& [name, text] : d.changed)
        if (!is_entrance_setting(name)) other.push_back(label(name) + ": " + text);
    if (other.empty()) return lines;
    std::string s = "Changed from " + preset_label(d.closest) + ": ";
    for (size_t i = 0; i < other.size() && i < most; i++) s += (i ? ", " : "") + other[i];
    if (other.size() > most) s += " and " + std::to_string(other.size() - most) + " more (the spoiler log lists them all)";
    lines.push_back(s + ".");
    return lines;
}

}  // namespace rando_settings
