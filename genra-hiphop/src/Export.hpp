// SPDX-License-Identifier: GPL-3.0-only
// GENRA HIP-HOP: MIDI export and saving the chops, straight into a folder (no save dialog: on Windows with ASIO a
// dialog can stall Rack's engine). The MIDI folder is the one every GENRA module uses (Genra/plugin-settings.json,
// default <Documents>/Midi Files from Genra); the chops go to <Documents>/Chops from Genra unless another is picked.
#pragma once
#include "plugin.hpp"
#include "hh/Core.hpp"
#include <vector>

namespace hhx {

std::string midiFolder();
std::string chopFolder();
void pickMidiFolder();
void pickChopFolder();
/** A file name that does not exist yet in dir: <YYYY-MM-DD>_<name><ext> (then -2, -3 …). */
std::string freeName(const std::string& dir, const std::string& name, const std::string& ext);
std::string cleanName(const std::string& s);

/** A type-1 MIDI file of the four bars as rendered: drums (channel 10, GM notes), bass, melody, pads, chop (pad 1 =
 *  C1; a chop's pitch as pitch bend over ±12 semitones, set by RPN; reverse as CC 20). */
std::vector<uint8_t> midiFile(const hh::Render& r, float bpm, float swing, const hh::PadSet* pads, int globalPitch);

} // namespace hhx
