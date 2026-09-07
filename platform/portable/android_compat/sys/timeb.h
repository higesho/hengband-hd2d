/*!
 * @file sys/timeb.h
 * @brief bionic に無い `<sys/timeb.h>` の身代わり。**中身は空でよい。**
 *
 *
 * ## なぜ空でよいか
 * 旧 C の 2 コアは `h-system.h` で `SET_UID` のとき `<sys/timeb.h>` を読む
 * （`silq/src/h-system.h:29` / `gensoband/src/h-system.h:45`）。20 年前の
 * `ftime()` のための行である。**`ftime` も `struct timeb` も、この 2 つの木の
 * どこからも使われていない**（2026-08-21 に全数で確かめた。当たるのは
 * `strftime` だけで、あれは `<time.h>` の別物である）。
 *
 * POSIX.1-2008 が `ftime` を捨てて以来 bionic はこのヘッダを持たない。
 * 中身の要らない `#include` を通すためだけに置く。
 *
 * **中身を足さないこと。** ここに `ftime` を生やすと「在る」ことになり、
 * どこかが使い始めてしまう。使いたくなったら `clock_gettime` を直に呼ぶ。
 */
#pragma once
