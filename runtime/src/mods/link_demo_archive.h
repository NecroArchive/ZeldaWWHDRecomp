// SPDX-License-Identifier: MPL-2.0
// Link's demo animations from the right one of his two demo archives (issue #707; link_demo_archive.cpp).
// Pure logic, so link_demo_archive_test checks it without the game.
//
// Link's cutscene animations are in two archives, LkD00 and LkD01. Both are resident (HD loads them with the permanent
// pack); the play scene only picks which one his cutscene lookups name: LkD01 once event bit 0x2D01 is set (Aryll's
// rescue in the Forsaken Fortress, rescue.stb), LkD00 before (d_s_play phase_0 025B1418). A cutscene asks for its
// animations by number, and the numbers are those of the archive its part of the story plays with. An early cutscene
// that still plays after 0x2D01 (a save in which it was never watched, such as the first Rito Aerie meeting on Dragon
// Roost Island, demo 10, behind switch 9 of Dragon Roost's stage memory) then gets whatever LkD01 has at that number: a
// texture animation, or nothing. daPy_lk_c::dProcTool uses it as a bone animation and setSeAnime reads its missing
// header at guest 0x4: the crash of #707. The original game has the same code, so it is not a port difference; normal
// play order never gets there.
#pragma once
#include <cstdint>
#include <string_view>

namespace mods::link_demo_archive {

// The archives' resource id table types (rpx 0x100DE8F4: archive CRC32 -> {name, type} per id)
constexpr int32_t kBck = 2;    // bone animation (J3DAnmTransform): daPy_lk_c::dProcTool
constexpr int32_t kBtp = 17;   // texture pattern (eyes, mouth): loadTextureAnimeResource 023DC38C
constexpr int32_t kBtk = 21;   // texture scroll: loadTextureScrollResource 023DC4DC
constexpr int32_t kMissing = -1;  // the id is past the archive's last one

// The other one of Link's demo archives; empty for any other archive.
inline std::string_view other_archive(std::string_view name) {
    if (name == "LkD00") return "LkD01";
    if (name == "LkD01") return "LkD00";
    return {};
}

// What one archive answers for the id: the table's type there, and whether the game's lookup returned a resource that
// can be used as that type (not null; a bone animation also needs its data header).
struct Answer {
    int32_t type = kMissing;
    bool usable = false;
};

enum class Pick {
    kKeep,     // the selected archive's answer (it fits, or nothing better is there: the game's own behaviour)
    kOther,    // the other archive's resource of the same number
    kCurrent,  // bone animation only: Link keeps the animation he has (dProcTool then holds it for this cut)
};

// The cutscene asked for `expected` from the selected archive (`here`); `other` is the other archive's answer for the
// same number. An answer that fits is never replaced, so normal play is unchanged. `current_usable`: Link's current
// bone animation can stand in (only asked for kBck). `fix_on`: the "Fix original game bugs" setting; off, the game's
// own answer always stays (the original game, crash included).
inline Pick pick(bool fix_on, int32_t expected, Answer here, Answer other, bool current_usable) {
    if (!fix_on || (here.type == expected && here.usable)) return Pick::kKeep;
    if (other.type == expected && other.usable) return Pick::kOther;
    if (expected == kBck && current_usable) return Pick::kCurrent;
    return Pick::kKeep;
}

}  // namespace mods::link_demo_archive
