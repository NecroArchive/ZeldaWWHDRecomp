// SPDX-License-Identifier: MPL-2.0
// Link's demo animations from the right one of his two demo archives (issue #707). Why: link_demo_archive.h.
//
// hook_026067F4 wraps dRes_control_c::getIDRes (archive name, id -> resource). For Link's three cutscene lookups,
// told apart by their return address,
//   024209F0  daPy_lk_c::dProcTool: his body animation (bck)
//   023DC3F8  daPy_lk_c::loadTextureAnimeResource, cutscene branch: eyes / mouth (btp)
//   023DC548  daPy_lk_c::loadTextureScrollResource, cutscene branch: texture scroll (btk)
// it checks the selected archive (LkD00 / LkD01) against the game's resource id table: when the id has a different
// type there (or is past its end) and the other, equally resident archive has the expected type at that id, the other
// archive's resource is returned. A body animation that neither archive has keeps Link in the animation he has (the
// cutscene goes on; without this, setSeAnime crashes reading guest 0x4). Every lookup that already fits is left as it
// is, so normal play order never sees a change: the guard acts only where the game would use a resource of the wrong
// kind. Ids that are the expected type in both archives stay with the selected one (the table cannot tell those apart).
//
// Not tied to "Fix original game bugs": the alternative is a crash. WWHD_LINK_DEMO_ARCHIVE=0 turns it off (the
// game's own lookup, crash included), for comparisons.
#include "mods/link_demo_archive.h"

#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <tuple>

#include "game_hooks.h"
#include "guest_addr.h"
#include "runtime.h"

extern "C" void f_026067F4_orig(Cpu* c);  // dRes_control_c::getIDRes(control, archive SafeString*, id)

namespace mods::link_demo_archive {
namespace {
const bool g_on = [] {
    const char* e = std::getenv("WWHD_LINK_DEMO_ARCHIVE");
    return !e || std::atoi(e) != 0;
}();
const uint32_t kIdTable = GD(0x100DE8F4), kIdTableCount = GD(0x100C45C0);  // archive CRC32 -> {names, count}
const uint32_t kLkD00 = GD(0x10052828), kLkD01 = GD(0x10052830);         // the game's "LkD00" / "LkD01" strings
constexpr uint32_t kAnmUnder0 = 0x57FC;  // daPy_lk_c::mAnmRatioUnder[0].mpAnmTransform (dProcTool: setSeAnime's anm)
constexpr uint32_t kAnmHeader = 0x08;    // the animation's data header (setSeAnime reads its name through it)

std::string cstr(uint32_t p, size_t max = 16) {
    std::string s;
    for (size_t i = 0; p && i < max; ++i) {
        const char ch = char(ld8(p + uint32_t(i)));
        if (!ch) break;
        s += ch;
    }
    return s;
}
uint32_t crc32(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : s) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}
// The table's type of `id` in `archive` and its file name (kMissing / "" past the end or for an unknown archive)
std::pair<int32_t, std::string> table_entry(const std::string& archive, uint32_t id) {
    const uint32_t hash = crc32(archive), rows = ld32(kIdTableCount);
    for (uint32_t i = 0; i < rows && i < 4096; ++i) {
        const uint32_t row = kIdTable + i * 12;
        if (ld32(row) != hash) continue;
        const uint32_t names = ld32(row + 4), count = ld32(row + 8);
        if (id >= count) return {kMissing, ""};
        return {int32_t(ld32(names + id * 8 + 4)), cstr(ld32(names + id * 8), 48)};
    }
    return {kMissing, ""};
}
bool usable(int32_t expected, uint32_t res) { return res && (expected != kBck || ld32(res + kAnmHeader)); }
int32_t expected_type(uint32_t lr) {
    if (lr == GC(0x024209F0)) return kBck;
    if (lr == GC(0x023DC3F8)) return kBtp;
    if (lr == GC(0x023DC548)) return kBtk;
    return kMissing;
}
// getIDRes(control, {other}, id), with a key of our own below the caller's stack (the game's key keeps its name)
uint32_t lookup(Cpu* c, uint32_t control, uint32_t key, uint32_t name, uint32_t id) {
    const uint32_t sp = c->r[1];
    c->r[1] = sp - 0x20;
    st32(c->r[1], sp);
    const uint32_t own = c->r[1] + 0x10;
    st32(own, name);                  // sead::SafeString: +0 text
    st32(own + 4, ld32(key + 4));     // +4 the game key's vtable
    const uint32_t res = guest_call(c, GC(0x026067F4), {control, own, id});  // lr 0: this hook passes it through
    c->r[1] = sp;
    return res;
}
void note(const char* what, const std::string& archive, uint32_t id, const std::string& here,
          const std::string& other_name, const std::string& used) {
    static std::mutex m;
    static std::set<std::tuple<std::string, uint32_t, std::string>> seen;
    std::lock_guard<std::mutex> lock(m);
    if (seen.size() > 256 || !seen.insert({archive, id, what}).second) return;
    const std::string outcome = used.empty() ? "kept, " + other_name + " has no better one" : used;
    LOG("[link] cutscene %s 0x%X: %s has %s there; %s (#707)", what, id, archive.c_str(),
        here.empty() ? "nothing" : here.c_str(), outcome.c_str());
}
}  // namespace
}  // namespace mods::link_demo_archive

extern "C" void hook_026067F4(Cpu* c) {
    using namespace mods::link_demo_archive;
    const uint32_t lr = c->lr, control = c->r[3], key = c->r[4], id = c->r[5];
    f_026067F4_orig(c);
    if (!g_on) return;
    const int32_t expected = expected_type(lr);
    if (expected == kMissing || !key) return;
    const std::string archive = cstr(ld32(key));
    const std::string_view other = other_archive(archive);
    if (other.empty()) return;
    const uint32_t res = c->r[3];
    const auto [type_here, name_here] = table_entry(archive, id);
    const Answer here{type_here, usable(expected, res)};
    if (here.type == expected && here.usable) return;  // fits: the game's own answer (all normal play)
    const auto [type_other, name_other] = table_entry(std::string(other), id);
    uint32_t alt = 0;
    if (type_other == expected) alt = lookup(c, control, key, other == "LkD00" ? kLkD00 : kLkD01, id);
    const uint32_t link = expected == kBck ? game_hooks::link() : 0;
    const uint32_t current = link ? ld32(link + kAnmUnder0) : 0;
    const char* what = expected == kBck ? "animation" : expected == kBtp ? "texture pattern" : "texture scroll";
    switch (pick(expected, here, Answer{type_other, usable(expected, alt)}, usable(kBck, current))) {
        case Pick::kOther:
            c->r[3] = alt;
            note(what, archive, id, name_here, std::string(other), "using " + std::string(other) + "'s " + name_other);
            break;
        case Pick::kCurrent:
            c->r[3] = current;
            note(what, archive, id, name_here, std::string(other), "Link keeps his current animation");
            break;
        case Pick::kKeep:
            c->r[3] = res;
            note(what, archive, id, name_here, std::string(other), "");
            break;
    }
}
