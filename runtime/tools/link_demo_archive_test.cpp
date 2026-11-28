// Link's cutscene animations from the right demo archive (issue #707; mods/link_demo_archive.h). No game files.
#include "mods/link_demo_archive.h"
#include <cassert>

using namespace mods::link_demo_archive;

int main() {
    // only Link's two demo archives have a counterpart
    assert(other_archive("LkD00") == "LkD01");
    assert(other_archive("LkD01") == "LkD00");
    assert(other_archive("LkAnm").empty() && other_archive("").empty() && other_archive("LkD02").empty());

    const Answer bck{kBck, true}, btk{kBtk, true}, btp{kBtp, true}, missing{};
    // normal play: an answer that fits is kept whatever the other archive has
    assert(pick(true, kBck, bck, bck, true) == Pick::kKeep);
    assert(pick(true, kBck, bck, missing, false) == Pick::kKeep);
    assert(pick(true, kBtp, btp, btp, false) == Pick::kKeep);
    assert(pick(true, kBtk, btk, missing, false) == Pick::kKeep);
    // #707: the first Rito Aerie meeting (demo 10) played after Aryll's rescue asks LkD01 for 0xDF, a btk there
    // (26_cl_cut09_shock_o.btk); LkD00 has 10_cl_cut02_wait01_l.bck at 0xDF
    assert(pick(true, kBck, btk, bck, true) == Pick::kOther);
    assert(pick(true, kBck, btk, bck, false) == Pick::kOther);
    // an id past the selected archive's end (LkD01 has 434 ids, LkD00 646)
    assert(pick(true, kBck, missing, bck, true) == Pick::kOther);
    // a bone animation whose data header is missing does not count as one, in either archive
    assert(pick(true, kBck, Answer{kBck, false}, bck, true) == Pick::kOther);
    assert(pick(true, kBck, btk, Answer{kBck, false}, true) == Pick::kCurrent);
    // neither archive has a bone animation there: Link keeps his own; without one the game's answer stays
    assert(pick(true, kBck, btk, btp, true) == Pick::kCurrent);
    assert(pick(true, kBck, missing, missing, true) == Pick::kCurrent);
    assert(pick(true, kBck, btk, btp, false) == Pick::kKeep);
    // texture lookups: the other archive's texture of the right kind, never Link's body animation
    assert(pick(true, kBtp, Answer{kBck, true}, btp, true) == Pick::kOther);
    assert(pick(true, kBtk, missing, btk, true) == Pick::kOther);
    assert(pick(true, kBtp, btk, btk, true) == Pick::kKeep);
    assert(pick(true, kBtk, missing, missing, true) == Pick::kKeep);
    // a null resource of the right type is not usable
    assert(pick(true, kBtp, Answer{kBtp, false}, btp, false) == Pick::kOther);
    // "Fix original game bugs" off: the game's own answer always stays, even where it crashes (#707)
    assert(pick(false, kBck, btk, bck, true) == Pick::kKeep);
    assert(pick(false, kBck, missing, missing, true) == Pick::kKeep);
    assert(pick(false, kBtp, Answer{kBck, true}, btp, true) == Pick::kKeep);
    assert(pick(false, kBck, bck, bck, true) == Pick::kKeep);
    return 0;
}
