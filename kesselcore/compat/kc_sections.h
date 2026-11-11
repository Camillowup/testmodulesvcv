/* SPDX-License-Identifier: GPL-3.0-only
 * KESSEL (forced in by the Makefile on every system): the LXR firmware places its hot code and data in the STM32's
 * tightly coupled memories (sections ".dtcm", ".dtcmz", ".itcm_text"). On the host that placement means nothing (and
 * Mach-O and PE want other section names), so the attribute becomes a harmless "used". The firmware stays unchanged. */
#pragma once
#define section(x) used
