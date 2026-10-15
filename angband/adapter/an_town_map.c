/*!
 * @file an_town_map.c
 * @brief `an_town_map.h` の実体。
 *
 * ## 地図のファイル（`angband/lib/hd2d/town.txt`）
 *
 * **`angband/lib/gamedata/` の外に置く。** `tools/angband/prepare_sources.py` の
 * `copy_runtime_data()` は `gamedata`/`customize`/`screens`/`help` を実行のたびに
 * `shutil.rmtree` してから原作の中身で埋め直すので、そこへ置くと準備をやり直す
 * たびに消える（2026-09-24 の見直しで `lib/hd2d/` へ移した）。置き場は
 * `an_lib_root()`（`angband/adapter/an_bootstrap.c`。`an_bootstrap()` に渡された
 * `<exe_dir>/angband/lib/` そのもの）から組む——`ANGBAND_DIR_GAMEDATA` のような
 * 原作側の変数には頼らない。
 *
 * 1 文字 1 マス。行数は `z_info->town_hgt`（22）、各行の桁数は `z_info->town_wid`
 * （66）——**行末の改行（CR/LF どちらでもよい）を除いて、ちょうどこの大きさ**であること。
 * 地図の外周（世界の枠）も含めて、66×22 の全マスをこのファイルが決める
 * （原作のように「まず枠を描いてから内側だけ乱数で埋める」下ごしらえは無い）。
 *
 * | 字 | 地形 |
 * | --- | --- |
 * | `.` | 床（`FEAT_FLOOR`） |
 * | `~` | 溶岩（`FEAT_LAVA`） |
 * | `,` | 通れる瓦礫（`FEAT_PASS_RUBBLE`） |
 * | `:` | 通れない瓦礫（`FEAT_RUBBLE`） |
 * | `#` | 花崗岩の壁（`FEAT_GRANITE`。掘れる） |
 * | `X` | 永久壁（`FEAT_PERM`。世界の枠など、店の建物ではないもの） |
 * | `B` | 店の建物の壁（`FEAT_PERM`。ゲームでは `X` と同じ地形——**店の入口に添える壁だと
 * 　　　人が読み分けるための字**であって、コアの動きは変えない） |
 * | `>` | 下り階段（`FEAT_MORE`） |
 * | `@` | 最初に立つ位置（地形は床。**下り階段と重ねなくてよい**——原作は重ねている
 * 　　　が、この地図では別々のマスにできる） |
 * | `1`〜`8` | 店の入口。`angband/lib/gamedata/store.txt` の `store:` の並び順
 * 　　　　　　（1 General Store・2 Armoury・3 Weapon Smiths・4 Bookseller・
 * 　　　　　　5 Alchemy Shop・6 Magic Shop・7 Black Market・8 Home）。**8 種とも
 * 　　　　　　ちょうど 1 個ずつ**なければならない |
 *
 * 読み込みが失敗したときは `an_town_build_from_file()` が `false` を返す
 * （`stderr` へ理由を書く）。呼び出し元（`gen-cave.c` の A5）はそのまま原作の
 * `town_gen_layout()` へ落ちる。
 */
#include "an_town_map.h"

#include "an_shim.h" /* an_lib_root() */
#include "angband.h"
#include "cave.h"
#include "init.h"
#include "player-util.h"

#include <stdio.h>
#include <string.h>

/*! @brief 店の入口の字（`1`〜`8`）と `store.txt` の並び順の対応。 */
#define AN_TOWN_STORE_COUNT 8

static bool an_town_map_path(char *out, size_t out_size)
{
    const char *root = an_lib_root();
    if ((root == NULL) || (root[0] == '\0')) {
        return false; /* an_bootstrap() がまだ走っていない（呼ばれるはずのない順序）。 */
    }
    /* an_lib_root() は末尾に区切りつき（"<exe_dir>/angband/lib/"）。 */
    if (snprintf(out, out_size, "%shd2d%stown.txt", root, PATH_SEP) >= (int)out_size) {
        return false;
    }
    return true;
}

/*! @brief `n`（0 始まり。`store.txt` の並び順）の店の入口の地形番号を引く。
 * `build_store()`（`gen-cave.c`）と同じ探し方——`f_info` を線形に走査する。 */
static bool an_town_store_feat(int n, int *out_feat)
{
    int feat;
    for (feat = 0; feat < FEAT_MAX; feat++) {
        if (feat_is_shop(feat) && (f_info[feat].shopnum == n + 1)) {
            *out_feat = feat;
            return true;
        }
    }
    return false;
}

/*! @brief 1 文字を地形番号へ。店の入口（`1`-`8`）と `@`（開始位置。床）は
 * 呼び出し元が別に扱うので、ここには来ない。 */
static bool an_town_glyph_feat(char ch, int *out_feat)
{
    switch (ch) {
    case '.': *out_feat = FEAT_FLOOR; return true;
    case '~': *out_feat = FEAT_LAVA; return true;
    case ',': *out_feat = FEAT_PASS_RUBBLE; return true;
    case ':': *out_feat = FEAT_RUBBLE; return true;
    case '#': *out_feat = FEAT_GRANITE; return true;
    case 'X': *out_feat = FEAT_PERM; return true;
    case 'B': *out_feat = FEAT_PERM; return true;
    case '>': *out_feat = FEAT_MORE; return true;
    default: return false;
    }
}

bool an_town_build_from_file(struct chunk *c, struct player *p)
{
    char path[4096];
    FILE *f;
    char line[512];
    int row;
    int width, height;
    /* 1 行ぶんの地形（`.`/`@` は FEAT_FLOOR、店の入口はダミー 0 のまま――後で埋める）。 */
    int store_x[AN_TOWN_STORE_COUNT];
    int store_y[AN_TOWN_STORE_COUNT];
    bool store_seen[AN_TOWN_STORE_COUNT];
    bool have_start = false;
    bool have_stairs = false;
    int start_x = -1, start_y = -1;
    int i;

    if (!an_town_map_path(path, sizeof(path))) {
        fprintf(stderr, "[angband] town.txt: lib root unknown (an_bootstrap() not run yet?; not fatal -- falling back)\n");
        return false;
    }

    f = fopen(path, "rb");
    if (f == NULL) {
        /* まだ地図を用意していないだけ――致命ではない。 */
        fprintf(stderr, "[angband] town.txt not found at %s (falling back to town_gen_layout())\n", path);
        return false;
    }

    width = (int)z_info->town_wid;
    height = (int)z_info->town_hgt;
    for (i = 0; i < AN_TOWN_STORE_COUNT; i++) {
        store_seen[i] = false;
    }

    for (row = 0; row < height; row++) {
        int col;
        size_t len;
        if (fgets(line, (int)sizeof(line), f) == NULL) {
            fprintf(stderr,
                "[angband] town.txt: expected %d rows but only found %d (falling back)\n",
                height, row);
            fclose(f);
            return false;
        }
        len = strlen(line);
        while ((len > 0) && ((line[len - 1] == '\n') || (line[len - 1] == '\r'))) {
            line[len - 1] = '\0';
            len--;
        }
        if ((int)len != width) {
            fprintf(stderr,
                "[angband] town.txt: row %d has %d column(s), expected %d (falling back)\n",
                row, (int)len, width);
            fclose(f);
            return false;
        }

        for (col = 0; col < width; col++) {
            const char ch = line[col];
            int feat;

            if (ch == '@') {
                if (have_start) {
                    fprintf(stderr,
                        "[angband] town.txt: more than one '@' (start position). "
                        "first at (%d,%d), second at (%d,%d) (falling back)\n",
                        start_x, start_y, col, row);
                    fclose(f);
                    return false;
                }
                have_start = true;
                start_x = col;
                start_y = row;
                square_set_feat(c, loc(col, row), FEAT_FLOOR);
                continue;
            }
            if ((ch >= '1') && (ch <= '8')) {
                const int n = ch - '1';
                if (store_seen[n]) {
                    fprintf(stderr,
                        "[angband] town.txt: store entrance '%c' appears more than once "
                        "(falling back)\n", ch);
                    fclose(f);
                    return false;
                }
                store_seen[n] = true;
                store_x[n] = col;
                store_y[n] = row;
                continue; /* 実際の地形は、8 種すべてそろったことを確かめてから敷く。 */
            }
            if (ch == '>') {
                have_stairs = true;
            }
            if (!an_town_glyph_feat(ch, &feat)) {
                fprintf(stderr,
                    "[angband] town.txt: unknown character '%c' (0x%02x) at row %d, "
                    "column %d (falling back)\n", (ch >= 0x20 && ch < 0x7f) ? ch : '?',
                    (unsigned char)ch, row, col);
                fclose(f);
                return false;
            }
            square_set_feat(c, loc(col, row), feat);
        }
    }
    fclose(f);

    if (!have_start) {
        fprintf(stderr, "[angband] town.txt: no '@' (start position) found (falling back)\n");
        return false;
    }
    if (!have_stairs) {
        fprintf(stderr, "[angband] town.txt: no '>' (down staircase) found (falling back)\n");
        return false;
    }
    for (i = 0; i < AN_TOWN_STORE_COUNT; i++) {
        if (!store_seen[i]) {
            fprintf(stderr,
                "[angband] town.txt: store entrance '%d' is missing (need exactly one of "
                "each of 1-8; falling back)\n", i + 1);
            return false;
        }
    }

    /* 店の入口を敷く（`build_store()` と同じ探し方）。 */
    for (i = 0; i < AN_TOWN_STORE_COUNT; i++) {
        int feat;
        if (!an_town_store_feat(i, &feat)) {
            fprintf(stderr,
                "[angband] town.txt: no terrain in f_info has shopnum %d (game data "
                "problem, not the map file; falling back)\n", i + 1);
            return false;
        }
        square_set_feat(c, loc(store_x[i], store_y[i]), feat);
    }

    player_place(c, p, loc(start_x, start_y));
    fprintf(stderr, "[angband] town built from %s (start=(%d,%d))\n", path, start_x, start_y);
    return true;
}
