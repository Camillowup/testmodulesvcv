// SPDX-License-Identifier: GPL-3.0-only
// GENRA CHOP ABLAUF: song parts, hip-hop song forms and transitions (no Rack in here: test/hh_test.cpp).
//
// A part (INTRO, VERSE, HOOK, BRIDGE, BREAK, OUTRO) is a variation of a pattern: the tracks asked for are rolled again
// (the chords stay), then the part shapes it. In a song a row plays a pattern for some bars; INTRO, BREAK and OUTRO
// run their course over the whole row (not over the 4-bar loop), and the last bar of a row (and every 8th bar of a long
// VERSE or HOOK) can carry a transition in the style's manner: Boom Bap a drum fill or a stop, Trap hat rolls, the 808
// stopping before the HOOK and a reversed chop as a riser.
#pragma once
#include "Core.hpp"

namespace hh {

enum Part { P_INTRO, P_VERSE, P_HOOK, P_BRIDGE, P_BREAK, P_OUTRO, NUM_PARTS };
extern const char* const PART_NAMES[NUM_PARTS];

struct SongForm {
	const char* name;
	int n;
	int part[12];
	int bars[12];
};
static const int NUM_FORMS = 7;
extern const SongForm FORMS[NUM_FORMS];
/** The form that fits a style (for AUTO). */
int autoForm(int style);
/** Does the style roll its hats and stop its 808 (Trap, Drill, Memphis …) rather than fill with the snare? */
bool trapStyle(int style);

/** A part made from `src`: the tracks in renewMask (bit per Track) rolled again, then shaped. */
void makePart(Pattern& out, const Pattern& src, int part, int renewMask, const Settings& set, const SampleInfo& si,
              const PadSet* pads, uint32_t seed, float bpm);

/** What bar `barInRow` (0..) of a song row `rowBars` long plays: the row's pattern `p` with the part's course over the
 *  row and, on the last bar (fill 1) or every 8th bar of a long VERSE / HOOK, a transition into `nextPart` (-1: the
 *  end). Only the loop bar that plays (barInRow % 4) is changed. */
void shapeBar(Pattern& p, int part, int barInRow, int rowBars, bool fill, int nextPart, int style, uint32_t seed);

} // namespace hh
