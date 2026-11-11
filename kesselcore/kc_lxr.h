/* SPDX-License-Identifier: GPL-3.0-only
 * KESSEL for VCV Rack: the parts of the LXR firmware's sound engine (kessel/Core, a snapshot: kessel/SNAPSHOT.txt)
 * that KESSEL's core uses, in one place. */
#ifndef KC_LXR_H
#define KC_LXR_H
#include "Soundengine/config.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/BufferTools.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/transientTables.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/valueShaper.h"
#include "Soundengine/Sequencer/sequencer.h"

void kc_ccHandler(MidiMsg msg, uint8_t updateOriginalValue, uint8_t isFloatModulator, float v);
float midiParser_calcDetune(uint8_t value);

#endif
