// Shared precompiled header for HengbandCore and TangbandCore (Windows, Linux and Android).
// ASCII only on purpose. Cautions: section 2.6.
//
// Contents: the upstream stdafx.h (standard library) plus the Hengband headers that most of the
// ~867 sources of the two cores include (directly or through other headers). The list was chosen
// from data: g++ -M over every HengbandCore source (targeting overlay, 2026-10-01). Inclusion
// rate = sources that include the header / 867. All headers at 57% or more are listed; the next
// one down is 46.9%, so 57% is a natural cut (26 headers are at 70% or more).
// Every listed header is also on the list because it is included by a listed header of the same
// or higher rate, so the list equals its own include closure.
//
// Rules (checked by tools/core_sources/pch_safety_check.py, which tools/core_sources/audit.py runs):
//  - no header whose content depends on a macro that some source defines/undefines before it
//    includes the header (the precompiled header is read before the source, so the macro would
//    silently stop working);
//  - every header has an include guard or #pragma once.
// Editing this list or any listed header recompiles every source of both cores.
// Build without it to find #include lines that this header hides:
//  Windows /p:HengbandCorePch=false, Linux and Android -DHB_CORE_PCH=OFF.
#pragma once

#include "stdafx.h"

// Order matters: h-basic.h (autoconf, h-config.h, h-system.h, ...) and angband.h come first, as in the sources;
// a header that reads a macro must come after the header that defines it (the checker verifies this).
#include "system/h-basic.h"  // 96.1% (833)
#include "system/angband.h"  // 93.4% (810)
#include "system/h-type.h"  // 96.3% (835)
#include "system/gamevalue.h"  // 96.1% (833)
#include "system/h-system.h"  // 96.1% (833)
#include "system/h-config.h"  // 96.1% (833)
#include "locale/language-switcher.h"  // 95.4% (827)
#include "system/angband-exceptions.h"  // 95.3% (826)
#include "util/stack-trace.h"  // 95.3% (826)
#include "system/angband-system.h"  // 94.3% (818)
#include "system/angband-version.h"  // 94.3% (818)
#include "term/z-rand.h"  // 94.1% (816)
#include "term/z-form.h"  // 93.8% (813)
#include "term/z-util.h"  // 93.7% (812)
#include "locale/text-encoding.h"  // 93.7% (812)
#include "term/term-char.h"  // 93.4% (810)
#include "term/z-term.h"  // 93.4% (810)
#include "util/bit-flags-calculator.h"  // 84.2% (730)
#include "info-reader/info-reader-util.h"  // 84.0% (728)
#include "util/flag-group.h"  // 83.2% (721)
#include "util/dice.h"  // 78.8% (683)
#include "util/point-2d.h"  // 77.0% (668)
#include "locale/localized-string.h"  // 75.9% (658)
#include "util/enum-converter.h"  // 75.0% (650)
#include "system/system-variables.h"  // 73.7% (639)
#include "object-enchant/trc-types.h"  // 72.5% (629)
#include "util/enum-range.h"  // 68.2% (591)
#include "player-info/class-types.h"  // 68.1% (590)
#include "player-info/race-types.h"  // 68.1% (590)
#include "player-ability/player-ability-types.h"  // 67.9% (589)
#include "player/player-personality-types.h"  // 66.9% (580)
#include "player/player-sex.h"  // 66.8% (579)
#include "mutation/mutation-flag-types.h"  // 66.7% (578)
#include "system/player-type-definition.h"  // 66.4% (576)
#include "player-info/class-specific-data.h"  // 66.4% (576)
#include "object-enchant/tr-types.h"  // 65.7% (570)
#include "object-enchant/tr-flags.h"  // 65.1% (564)
#include "object/tval-types.h"  // 61.6% (534)
#include "object-enchant/trg-types.h"  // 57.6% (499)
#include "system/baseitem/baseitem-key.h"  // 57.3% (497)
