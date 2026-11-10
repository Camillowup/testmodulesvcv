// SPDX-License-Identifier: GPL-3.0-only
// GENRA's output jacks as numbers, for modules that follow GENRA from another plugin (GENRA Instruments) and so cannot
// see the Genra class. GenraModule.hpp checks them against its enum (static_assert): outputs are only ever appended.
#pragma once

namespace genraports {
enum {
	DRUM_OUTPUT = 0, // 8 of them
	BASS_VOCT_OUTPUT = 8,
	BASS_GATE_OUTPUT,
	BASS_VEL_OUTPUT,
	BASS_ACC_OUTPUT,
	MEL_VOCT_OUTPUT,
	MEL_GATE_OUTPUT,
	MEL_VEL_OUTPUT,
	MEL_ACC_OUTPUT,
	PAD_VOCT_OUTPUT,
	PAD_GATE_OUTPUT,
	PAD_VEL_OUTPUT,
	PAD_MOD_OUTPUT,
	BASS_SLIDE_OUTPUT,
	MEL_SLIDE_OUTPUT,
	CLOCK_OUTPUT,
	RESET_OUTPUT,
	NUM_DRUM_OUTPUTS = 8,
	PUB_CHANNELS = 4, // channels per output in GENRA's per-frame copy (followPub)
};
} // namespace genraports
