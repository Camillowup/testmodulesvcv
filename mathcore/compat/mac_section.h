// SPDX-License-Identifier: GPL-3.0-only
// macOS only (forced in by the Makefile): the FM1 firmwares (JIANT, ESTAMPIE) place some buffers in linker sections of the FM1
// (".pool", ".noinit", ".ram_text"). Mach-O wants "segment,section" there; in the host build the placement means
// nothing, so the attribute becomes a harmless "used". The firmwares stay unchanged.
#pragma once
#define section(x) used
