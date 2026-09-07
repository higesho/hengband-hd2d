/*!
 * @file gb_win_stubs.c
 * @brief `main-win.c` を組まないことで宙に浮く関数の受け皿。
 *
 * 設計 §7 は `main.c` / `main-win.c` / `maid-x11.c` / `readdib.c` を
 * **コンパイル除外**と決めている（除外はパッチではない）。ところが
 * `main-win.c` には、入口と描画のほかに **BGM の口が 2 つだけ**入っていて、
 * そこは別の TU から呼ばれている:
 *
 * | 関数 | 呼ぶ側 |
 * |---|---|
 * | `find_mon_music_priority()` | `util.c:1826,1942,1985,2057,2060` |
 * | `init_music_hack()` | `birth.c:8060` |
 *
 * `gensoband/src/` を 1 行も触らずに link を通すには、この 2 つを外から
 * 与えるほかない。設計 §7 が「スタブ TU を用意するか、1 行 `//GB:` 改変か、
 * 最小の方」と言っているうちの**前者**である。よって `gensoband/src/` の
 * 改変はゼロで済んでいる。
 *
 * ## 中身の方針
 * M0 では「BGM は無い」と答えるだけにしてあった（設計 §10「M0 でやらない」）。
 * **その3 / a で音を実装したので、いまはどちらも本物へ回している**（`gb_audio.c`）。
 * 素材や設定が無ければ向こうが黙って「無し」を返すので、そのときの挙動は昔のままである。
 *
 * `play_music()` / `select_floor_music()` は `util.c` にあるので写しのまま動く。
 * それらは最終的に `Term_xtra(TERM_XTRA_MUSIC_*)` を叩くだけで、
 * こちらの null term が握り潰す（`gb_null_term.c`）。
 */

#include "angband.h"

/*!
 * @brief モンスター専用 BGM の優先度。本物は `main-win.c:1754`。
 * @return 常に MON_MUSIC_PRIOR_NONE（曲の設定表を持たないので「無し」）
 */
int find_mon_music_priority(int r_idx)
{
    /*
     * **曲表を持つようになったので本物を返す**（その3 / a。実体は `gb_audio.c`）。
     * `music.cfg` の `[Monster_Low]` / `[Monster_Med]` / `[Monster_High]` に
     * その番号が書いてあるかを見るだけである。書いていなければ従来どおり「無し」。
     */
    return gb_audio_mon_music_priority(r_idx);
}

/*!
 * @brief BGM の有効化。本物は `main-win.c:2557`。
 *
 * 本物は `arg_music` と `use_music` を突き合わせて MCI を起こす。
 * こちらは鳴らす先が無いので、**両方を落としたままにする**とだけ決める。
 * ここで use_music を真にすると `util.c` の曲選択が空回りし続ける。
 */
void init_music_hack(void)
{
    /*
     * **鳴らせるようになった**（その3 / a）。ここで `use_music` を立てておかないと
     * `util.c` の選曲がそもそも走らない（`play_music()` が入口で落とす）。
     *
     * 実際に鳴らすかどうかは**画面側の設定**（`ui_state.audio`）が決める
     * ——`gb_audio_set_enabled()` を通って `gb_audio.c` の側で切られる。
     * ここを切ったままにすると、画面で入にしてもコアが選曲を始めない。
     */
    gb_audio_enable_core();
}
