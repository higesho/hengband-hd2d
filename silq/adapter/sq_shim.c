/*!
 * @file sq_shim.c
 * @brief Sil-Q コアの読み口（C 側）。**Sil-Q のヘッダを見てよい TU** の 1 つ。
 *
 *
 * P1（足場）で実装するのはコア名と版だけ。ゲーム状態の読み口
 * （HUD・メッセージ・マップ）は P2 / P3 でここへ足す。
 * Term まわりは `sq_null_term.c`、起動列は `sq_bootstrap.c` にある。
 */

#include "angband.h"

#include "sq_lang_c.h"
#include "sq_shim.h"

#include <stdio.h>
#include <string.h>

const char *sq_core_name(void)
{
    /*
     * 握手の `core_name`（v1 §3.1）。**小文字の短い識別子**で、
     * 変愚 = "hengband" / 短愚蛮怒 = "tangband" / 幻想蛮怒 = "gensoband" と揃える。
     * 画面側はこれで挙動を変えない（設計 §1 制約 1）が、記録には出る。
     */
    return "silq";
}

const char *sq_core_version(void)
{
    /*
     * `silq/src/defines.h` の `VERSION_NAME`（"Sil-Q"）と `VERSION_STRING`
     * （"1.5.1.0-beta2"）から組む。**先方の版が上がってもここは触らない**——
     * 写しを取り込み直せば追随する。
     *
     * なお `VERSION_STRING` は CMake が定義していればそちらが勝つ作りだが、
     * 当方は vcxproj で組むので必ずヘッダの既定値になる。
     */
    static char text[64];

    if (!text[0]) {
        sprintf(text, "%s %s", VERSION_NAME, VERSION_STRING);
    }

    return text;
}

/* ============================================================ ゲーム状態の読み出し（P2） */

int sq_character_generated(void)
{
    return character_generated ? 1 : 0;
}

void sq_prime_item_list_view(void)
{
    if (!character_generated) {
        return;
    }
    /* 理屈は `sq_shim.h` の註記。**1 度だけ**立てる（呼び手が数える）。 */
    auto_display_lists = TRUE;
}

int sq_screen_flags(void)
{
    int flags = 0;

    if (character_generated) {
        flags |= SQ_SCREEN_GENERATED;
    }
    /* **計数器**なので非 0 で見る（`sq_shim.h` の注記）。 */
    if (character_icky != 0) {
        flags |= SQ_SCREEN_ICKY;
    }
    if (character_xtra != 0) {
        flags |= SQ_SCREEN_XTRA;
    }
    if (character_dungeon) {
        flags |= SQ_SCREEN_DUNGEON;
    }
    if (character_generated && p_ptr->is_dead) {
        flags |= SQ_SCREEN_DEAD;
    }
    if (character_generated && p_ptr->playing) {
        flags |= SQ_SCREEN_PLAYING;
    }

    return flags;
}

int sq_read_hud(sq_hud_data *out)
{
    if (!out) {
        return SQ_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));

    if (!character_generated) {
        return SQ_OK;
    }

    out->in_game = 1;

    my_strcpy(out->name, op_ptr->full_name, sizeof(out->name));

    out->hp = p_ptr->chp;
    out->hp_max = p_ptr->mhp;
    out->sp = p_ptr->csp;
    out->sp_max = p_ptr->msp;
    out->depth = p_ptr->depth;
    out->exp = p_ptr->new_exp;

    /*
     * 階層の字面は `prt_depth()`（`xtra1.c`）と同じにする。**組み直さない**——
     * 画面側と食い違うと「HUD だけ違う数字」になる。
     * 1 階 = 50 ft は先方の決め（`p_ptr->depth * 50`）。
     */
    /*
     * `Surface` だけ訳す（`地上`）。**`%d ft` は訳さない**——カタログの `J:` が
     * 空＝「そのまま出す」指定だからで（`messages.ja.txt:3857`）、`ft` は
     * 残してよい英語として受け入れにも挙がっている（設計 §9-4 の `ALLOWED`）。
     * 組み上がった `"50 ft"` を `sq_tr()` に通すと、**深さの数だけ未訳一覧が
     * 埋まる**（引けなかった字を積む口なので）。だから枝を分けて当てる。
     */
    if (p_ptr->depth <= 0) {
        my_strcpy(out->depth_text, sq_tr("Surface"), sizeof(out->depth_text));
    } else {
        strnfmt(out->depth_text, sizeof(out->depth_text), "%d ft", p_ptr->depth * 50);
    }

    return SQ_OK;
}

int sq_message_count(void)
{
    return (int)message_num();
}

int sq_message_text(int age, char *out, int cap)
{
    cptr text;
    int n;

    if (!out || (cap <= 1)) {
        return SQ_ERR_ARG;
    }

    out[0] = '\0';

    if ((age < 0) || (age >= (int)message_num())) {
        return 0;
    }

    text = message_str((s16b)age);
    if (!text) {
        return 0;
    }

    /*
     * **ここで訳す。** 日本語層のフックは `vstrnfmt()` の書式（#3）と
     * `Term_addstr()` の表示文字列（#1 #2）＝「**描くところ**」で、
     * この欄は Term を通らないので素通りしていた。
     *
     * `msg_format()` の文は書式の側で既に日本語になって履歴へ入るが、
     * `msg_print("英文リテラル")` の文は**履歴には英語のまま**入り、コアのメッセージ欄
     * （Term の 0 行目）では描くときに訳される。つまり直さないと、**同じ 1 文が
     * 画面の欄によって日本語と英語に割れる**（マニュアルで実際にそう出た。
     * 「You enter the forge 'Orodruth' …」は `messages.ja.txt:531` に訳がある）。
     *
     * 二重引きにはならない——`sq_tr()` は先頭バイトが 0x80 以上なら引かずに返す
     * （`sq_lang_c.h` の説明）。日本語層が寝ているときは素通りで費用ゼロ。
     */
    text = sq_tr(text);

    my_strcpy(out, text, (size_t)cap);
    n = (int)strlen(out);
    return n;
}

/* ==================================================================== 地図（P3） */

/*!
 * @brief そのマスの `SQ_FEAT_*` を立てる（設計 §4.2）。
 *
 * @details Sil-Q に `FF_` フラグは無い。判定は **`FEAT_*` の番号の範囲**と
 * **`CAVE_*` ビット**だけで組み立てる。番号の根拠は `defines.h:940-1013`:
 *
 *   0x01 FLOOR / 0x02 CHASM / 0x03 GLYPH / 0x04 OPEN / 0x05 BROKEN /
 *   0x06-0x08 WARDED / 0x09 SUNLIGHT / 0x10-0x1C TRAP / 0x20-0x2F DOOR /
 *   0x30 SECRET / 0x31 RUBBLE / 0x33 QUARTZ / 0x38-0x3B WALL / 0x3F PERM /
 *   0x40-0x4F FORGE / 0x50-0x53 STAIRS
 *
 * **壁かどうかは番号ではなく `CAVE_WALL` で見る**（`defines.h:2897` の
 * `cave_floor_bold` の裏）。秘密の扉は「壁に見える扉」で、番号は壁の側に居る。
 */
static unsigned short sq_feature_bits(int y, int x)
{
    const u16b info = cave_info[y][x];
    const byte feat = cave_feat[y][x];
    unsigned short bits = 0;

    const int is_door = ((feat >= FEAT_DOOR_HEAD) && (feat <= FEAT_DOOR_TAIL));
    const int is_wall = ((info & (CAVE_WALL)) != 0);

    if (is_wall) {
        bits |= SQ_FEAT_WALL;
    }
    if (feat == FEAT_WALL_PERM) {
        bits |= SQ_FEAT_PERMANENT;
    }
    if (is_door) {
        bits |= SQ_FEAT_DOOR;
    }
    if ((feat == FEAT_OPEN) || (feat == FEAT_BROKEN)) {
        /* 開いた扉は `FEAT_DOOR_*` の範囲に居ない。**両方立てる**——画面側の
         * 「扉だが通れる」の絵はこの組み合わせで決まる。 */
        bits |= SQ_FEAT_DOOR | SQ_FEAT_DOOR_OPEN;
    }
    if ((feat >= FEAT_LESS) && (feat <= FEAT_MORE_SHAFT)) {
        bits |= SQ_FEAT_STAIRS;
    }
    if (feat == FEAT_RUBBLE) {
        bits |= SQ_FEAT_RUBBLE;
    }
    if (feat == FEAT_CHASM) {
        bits |= SQ_FEAT_CHASM;
    }
    if ((feat >= FEAT_FORGE_HEAD) && (feat <= FEAT_FORGE_TAIL)) {
        bits |= SQ_FEAT_FORGE;
    }
    if (info & (CAVE_ROOM)) {
        bits |= SQ_FEAT_ROOM;
    }
    if (info & (CAVE_GLOW)) {
        /*
         * **`SQ_FEAT_GLOW` は立てない。** あれは画面側で「このマスが光源である」
         * （店の灯り・溶岩の類）を意味し、点光源と自発光の板が置かれる。
         * `CAVE_GLOW` は「このマスは自分で明るい＝明るい部屋の一部」で、
         * **部屋のマスすべてに立つ**。写すと部屋じゅうが電球になり、
         * 床が白く光って点光源が上限（16）を超えて溢れる
         * （2026-08-20 に実機の絵で発覚。設計 §4.2 に追記した）。
         * 明るい部屋であることは `SQ_FEAT_GLOWING` が運ぶ——あちらは
         * 見た目（明るい部屋か通路か）に使われるだけで、光は作らない。
         */
        bits |= SQ_FEAT_GLOWING;
    }
    if (info & (CAVE_MARK | CAVE_SEEN)) {
        bits |= SQ_FEAT_KNOWN;
    }
    if (!is_wall && !is_door) {
        /* **深淵も「通れる」**（設計 §4.2。落ちるが歩ける）。M0 は床として通す。 */
        bits |= SQ_FEAT_PASSABLE;
    }
    if ((y == p_ptr->py) && (x == p_ptr->px)) {
        bits |= SQ_FEAT_PLAYER;
    }

    return bits;
}

/*! @brief 明るさ。2 = いま照らされている / 1 = 見えているが無灯 / 0 = 記憶のみ。 */
static unsigned char sq_light_level(int y, int x)
{
    const u16b info = cave_info[y][x];

    if (!(info & (CAVE_SEEN))) {
        return 0;
    }
    return (cave_light[y][x] > 0) ? 2 : 1;
}

int sq_floor_size(int *width, int *height)
{
    if (!character_dungeon) {
        return SQ_ERR_STATE;
    }
    if (width) {
        *width = p_ptr->cur_map_wid;
    }
    if (height) {
        *height = p_ptr->cur_map_hgt;
    }
    return SQ_OK;
}

int sq_player_pos(int *x, int *y)
{
    if (!character_generated) {
        return SQ_ERR_STATE;
    }
    if (x) {
        *x = p_ptr->px;
    }
    if (y) {
        *y = p_ptr->py;
    }
    return SQ_OK;
}

int sq_player_race(int *race)
{
    if (!character_generated) {
        return SQ_ERR_STATE;
    }
    if (race) {
        /* `p_ptr->prace` は `race.txt` の N 番号（ノルドール 0 〜 エダイン 3）。
         * 家（`phouse`）は絵を持たないので出さない——目録に在るのは種族の 4 枚だけ。 */
        *race = (int)p_ptr->prace;
    }
    return SQ_OK;
}

/*! `do_cmd_locate()`（`silq/src/cmd3.c`）が立てる旗。 */
extern bool sq_locate_shifted;

int sq_locate_active(void)
{
    if (!character_dungeon) {
        return 0;
    }
    return sq_locate_shifted ? 1 : 0;
}

int sq_panel_view(int *x, int *y, int *w, int *h)
{
    if (!character_dungeon) {
        return SQ_ERR_STATE;
    }
    if (!angband_term[0]) {
        return SQ_ERR_STATE;
    }
    if (x) {
        *x = (int)p_ptr->wx;
    }
    if (y) {
        *y = (int)p_ptr->wy;
    }
    /*
     * **`SCREEN_WID` / `SCREEN_HGT` を素で使わない。**
     * あれは `Term->wid` / `Term->hgt`（＝**いま選ばれている Term**）から出る。
     * ここはサブパネルを描くために Term を切り替えている最中にも呼ばれうるので、
     * 素で使うと**小さな窓の大きさが返り**、区画が実物より狭く見える。
     * 地図の Term は 0 番だと決まっている（`sq_read_overlay` も同じ）。
     */
    if (w) {
        *w = ((int)angband_term[0]->wid - COL_MAP - 1) / (use_bigtile ? 2 : 1);
    }
    if (h) {
        *h = (int)angband_term[0]->hgt - ROW_MAP - 1;
    }
    return SQ_OK;
}

int sq_read_map(int x0, int y0, int w, int h, sq_map_cell *out, int cap)
{
    int y;
    int x;
    int n = 0;

    if (!out || (w <= 0) || (h <= 0) || (cap < (w * h))) {
        return SQ_ERR_ARG;
    }
    if (!character_dungeon) {
        return SQ_ERR_STATE;
    }

    for (y = y0; y < (y0 + h); y++) {
        for (x = x0; x < (x0 + w); x++) {
            sq_map_cell *cell = &out[n++];
            byte a = 0;
            char c = ' ';
            byte ta = 0;
            char tc = ' ';
            s16b m_idx;

            memset(cell, 0, sizeof(*cell));
            cell->gx = (short)x;
            cell->gy = (short)y;

            if ((y < 0) || (x < 0) || (y >= p_ptr->cur_map_hgt) || (x >= p_ptr->cur_map_wid)) {
                continue; /* 階の外。0 詰めのまま */
            }

            /* 見た目はコアが決めたものをそのまま採る（独自の判定を作らない）。 */
            map_info(y, x, &a, &c, &ta, &tc);

            /*
             * **色は 16 に畳む**（設計 §4.1）。`special_lighting_floor()`
             * （`cave.c:894`）が暗いマスに `TERM_DARK + TERM_SHADE`（= 16）を
             * 返すので、畳まないと画面側の 16 色表からはみ出す。
             * 暗さは `light_level` が別に運ぶので情報は落ちない。
             */
            cell->fg_color = (unsigned char)(a & 0x0F);
            cell->bg_color = (unsigned char)(ta & 0x0F);
            cell->ascii = c;

            cell->terrain_id = (unsigned short)f_info[cave_feat[y][x]].mimic;
            cell->feature_flags = sq_feature_bits(y, x);
            cell->light_level = sq_light_level(y, x);

            /* 敵。**視認しているときだけ**（`ml`）。位置を漏らさない。 */
            m_idx = cave_m_idx[y][x];
            if (m_idx > 0) {
                const monster_type *m_ptr = &mon_list[m_idx];
                if (m_ptr->ml) {
                    cell->monster_id = (unsigned short)m_ptr->r_idx;
                    cell->monster_slot = (unsigned short)m_idx;
                }
            }

            /*
             * 床の物。**照らされているときだけ**立てる。
             * 記憶しているだけの暗いマスで立てると、画面側が「実体が居る」と見て
             * 板を探しに行き、地形の索引しか返らないので必ず外れる（幻想蛮怒 P4 の穴）。
             * 記憶している物はコアの記号として Term に残るので情報は落ちない。
             */
            if (cell->light_level >= 2) {
                const object_type *o_ptr;
                for (o_ptr = get_first_object(y, x); o_ptr; o_ptr = get_next_object(o_ptr)) {
                    if (o_ptr->marked) {
                        cell->object_id = (unsigned short)o_ptr->k_idx;
                        break;
                    }
                }
            }
        }
    }

    return n;
}

int sq_read_overlay(const sq_map_cell *cells, int count, sq_overlay_cell *out, int cap)
{
    int i;
    int n = 0;
    const term_win *win;

    if (!cells || !out || (count < 0) || (cap <= 0)) {
        return SQ_ERR_ARG;
    }
    if (!character_generated || !character_dungeon) {
        return 0;
    }
    /* 罠 2（`sq_shim.h`）。幻覚のあいだは全面が食い違うので出さない。 */
    if (p_ptr->image) {
        return 0;
    }
    /*
     * `use_bigtile` は Sil-Q では FALSE 固定（`variable.c:90`）だが、**立っていたら
     * 何も出さない**。立つと `print_rel()` が 1 マスを 2 桁へ書くので、桁の当て方が変わる。
     * 黙って半分ずれた重ね書きを出すより、出さないほうがよい。
     */
    if (use_bigtile) {
        return 0;
    }
    /*
     * 罠 3（2026-08-25。こう気づいた——「**階段を上り下りした際のメッセージ直後に
     * 元の画面（キャッシュ？）が表示される際に置き換えが出る**」）。
     *
     * `PR_MAP` が立っているあいだは、**Term はまだ前の階を映している**
     * （階を移ると `dungeon.c` が全面の描き直しを予約し、`redraw_stuff()` が
     * `prt_map()` を回すまで中身は古いまま）。この重ね書きは
     * **「Term の字」と「`map_info()` の字」の差**で採るので、そこを突き合わせると
     * **前の階の画面がまるごと差として出る**——画面には前の階の記号が
     * 一面に浮かぶ。まさに報告のとおりである。
     *
     * **古いと分かっているものを突き合わせない。** 描き直しが済めば旗は落ちるので、
     * 出ないのは長くて数フレームである。
     */
    if (p_ptr->redraw & (PR_MAP)) {
        return 0;
    }
    if (!angband_term[0] || !angband_term[0]->scr) {
        return SQ_ERR_STATE;
    }
    win = angband_term[0]->scr;

    for (i = 0; (i < count) && (n < cap); i++) {
        const sq_map_cell *cell = &cells[i];
        int ky;
        int kx;
        int vy;
        int vx;
        char c;
        unsigned char a;

        /* 罠 1。`print_rel()` と**同じ切り方**でパネルの外を捨てる。 */
        ky = (int)cell->gy - p_ptr->wy;
        if ((ky < 0) || (ky >= SCREEN_HGT)) {
            continue;
        }
        kx = (int)cell->gx - p_ptr->wx;
        if ((kx < 0) || (kx >= SCREEN_WID)) {
            continue;
        }
        vy = ky + ROW_MAP;
        vx = kx + COL_MAP;
        if ((vy < 0) || (vy >= (int)angband_term[0]->hgt) || (vx < 0)
            || (vx >= (int)angband_term[0]->wid)) {
            continue;
        }

        c = win->c[vy][vx];
        a = (unsigned char)(win->a[vy][vx] & 0x0F);
        if ((c == cell->ascii) && (a == cell->fg_color)) {
            continue; /* コアが描いた絵と同じ＝重ね書きは載っていない */
        }

        /*
         * **敵の記号の消し残し**（SH-34。2026-08-25 に実機で捕まえた）。
         *
         * 「狼が C の字で出る」に気づいた「絡み茨が & になる」の正体はこれである。
         * 診断を入れて出た行:
         *
         *     overlay has a monster letter: (54,15) 'o' **no monster on that grid** cell='.'
         *
         * **そのマスに敵は居ない**（`cave_m_idx` が 0）。`map_info()` は床（`.`）を返しており、
         * **Term だけが古い記号を抱えている**。つまり幽霊で、遊ぶ側には
         * 「居ない敵が居るように見える」——**位置を偽る**ので害が大きい。
         *
         * ## なぜ残るか
         * 聞き耳（`listen()`。`monster2.c`）は差が 10 を超えると `m_ptr->ml` を立てて
         * `lite_spot()` を呼ぶ。敵は**視界の外**なので、次に `update_mon()` が回ったときに
         * `ml` は落ちるが、**消す `lite_spot()` は敵の「いまの」マスへ向く**。
         * その間に敵が動いていれば、**描いたマスは誰も塗り直さない**。
         *
         * ## 直し方——**塗り直す**
         * 見つけたその場で `lite_spot()` を呼んで、`map_info()` の言うとおりに描き直す。
         * これは**いつでも正しい操作**である（コアが気づいていれば同じことをしていた）。
         * そのうえで**この 1 件は重ね書きとして出さない**——幽霊を画面へ運ばない。
         *
         * @note 見るのは**英字のときだけ**。聞き耳の `*`（SH-20）や、デバッグ地図の数字は
         * この道で出るのが正しいので触らない。
         */
        if (((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'))) {
            if (cave_m_idx[(int)cell->gy][(int)cell->gx] <= 0) {
                lite_spot((int)cell->gy, (int)cell->gx);
                continue;
            }
        }

        out[n].gx = cell->gx;
        out[n].gy = cell->gy;
        out[n].ascii = c;
        out[n].color = a;
        n++;
    }

    /*
     * **件数では見分けられない。** 最初は「多すぎたら 1 件も出さない」を足したが、
     * それだと 8 系統のうち**デバッグ地図 3 種**（騒音・匂い・明かり。`wizard2.c`）が
     * 落ちる——あれはパネル全面に数字を書くので、必ず上限を超える。
     * 「Term が地図を映していない」（一覧・鍛冶・墓碑）も全面なので、数では同じに見える。
     * だから門は**呼び手の `menu_open` 1 か所**に任せ、ここは数えない
     * （上限は呼び手が渡す `cap` ＝ 視界のマス数がそのまま効く）。
     */
    return n;
}

int sq_read_alerts(sq_alert_cell *out, int cap)
{
    int i;
    int n = 0;

    if (!out || (cap <= 0)) {
        return SQ_ERR_ARG;
    }
    if (!character_generated || !character_dungeon) {
        return 0;
    }
    /* 遊ぶ人が切っているなら出さない（`sq_shim.h` の註記）。 */
    if (!hilite_unwary) {
        return 0;
    }

    for (i = 1; (i < mon_max) && (n < cap); i++) {
        const monster_type *m_ptr = &mon_list[i];

        if (!m_ptr->r_idx || !m_ptr->ml) {
            continue; /* 空き枠か、視認していない。**位置を漏らさない** */
        }
        out[n].gx = (short)m_ptr->fx;
        out[n].gy = (short)m_ptr->fy;
        /* 境目は `get_alertness_text()`（`xtra1.c`）と**同じ 2 本**。 */
        if (m_ptr->alertness < ALERTNESS_UNWARY) {
            out[n].level = SQ_ALERT_ASLEEP;
        } else if (m_ptr->alertness < ALERTNESS_ALERT) {
            out[n].level = SQ_ALERT_UNWARY;
        } else {
            out[n].level = SQ_ALERT_ALERT;
        }
        n++;
    }

    return n;
}

int sq_read_target(int *x, int *y)
{
    if (!character_generated || !character_dungeon) {
        return SQ_ERR_STATE;
    }
    if (!hilite_target || !target_sighted()) {
        return SQ_ERR_STATE;
    }
    if (x) {
        *x = p_ptr->target_col;
    }
    if (y) {
        *y = p_ptr->target_row;
    }
    return SQ_OK;
}

int sq_read_minimap(unsigned char *out, int cap)
{
    int y;
    int x;
    int wid;
    int hgt;

    if (!out) {
        return SQ_ERR_ARG;
    }
    if (!character_dungeon) {
        return SQ_ERR_STATE;
    }

    wid = p_ptr->cur_map_wid;
    hgt = p_ptr->cur_map_hgt;
    if ((wid <= 0) || (hgt <= 0) || (cap < (wid * hgt))) {
        return SQ_ERR_ARG;
    }

    for (y = 0; y < hgt; y++) {
        for (x = 0; x < wid; x++) {
            unsigned char kind = SQ_MM_UNKNOWN;
            const u16b info = cave_info[y][x];
            const byte feat = cave_feat[y][x];

            if (info & (CAVE_MARK | CAVE_SEEN)) {
                if ((feat >= FEAT_LESS) && (feat <= FEAT_MORE_SHAFT)) {
                    kind = SQ_MM_STAIRS;
                } else if (((feat >= FEAT_DOOR_HEAD) && (feat <= FEAT_DOOR_TAIL))
                    || (feat == FEAT_OPEN) || (feat == FEAT_BROKEN)) {
                    kind = SQ_MM_DOOR;
                } else if (info & (CAVE_WALL)) {
                    kind = SQ_MM_WALL;
                } else {
                    kind = SQ_MM_FLOOR;
                }

                /* 実体は地形を上書きする（自分 > 敵 > 物 > 地形）。 */
                if ((cave_light[y][x] > 0) && (info & (CAVE_SEEN))) {
                    const object_type *o_ptr;
                    for (o_ptr = get_first_object(y, x); o_ptr; o_ptr = get_next_object(o_ptr)) {
                        if (o_ptr->marked) {
                            kind = SQ_MM_ITEM;
                            break;
                        }
                    }
                }
                if (cave_m_idx[y][x] > 0) {
                    const monster_type *m_ptr = &mon_list[cave_m_idx[y][x]];
                    if (m_ptr->ml) {
                        kind = SQ_MM_MONSTER;
                    }
                }
            }

            if ((y == p_ptr->py) && (x == p_ptr->px)) {
                kind = SQ_MM_PLAYER; /* 自分は記憶に関わらず出す */
            }

            out[(y * wid) + x] = kind;
        }
    }

    return wid * hgt;
}

/* ============================================ 周囲の地形の内訳（環境音の層） */

/*! 数える半径（マス）。**耳が届くと思える範囲**であって、視界とは別。変愚と同じ 12。 */
#define SQ_SURROUNDINGS_RADIUS 12

int sq_read_surroundings(sq_surroundings *out)
{
    int dy;
    int dx;
    int counted = 0;
    int rock = 0;
    int wall = 0;
    const int r = SQ_SURROUNDINGS_RADIUS;

    if (!out) {
        return SQ_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!character_generated || !character_dungeon) {
        return SQ_OK; /* radius 0 のまま＝「数えていない」 */
    }

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            const int gy = p_ptr->py + dy;
            const int gx = p_ptr->px + dx;

            if (((dx * dx) + (dy * dy)) > (r * r)) {
                continue; /* 円で数える */
            }
            if ((gy < 0) || (gx < 0) || (gy >= p_ptr->cur_map_hgt) || (gx >= p_ptr->cur_map_wid)) {
                continue;
            }
            if (!(cave_info[gy][gx] & (CAVE_MARK | CAVE_SEEN))) {
                continue;
            }
            counted++;

            if (!cave_floor_bold(gy, gx)) {
                wall++;
            }
            if (cave_feat[gy][gx] == FEAT_RUBBLE) {
                rock++; /* 瓦礫は壁でもある（両方に数える） */
            }
        }
    }

    if (counted <= 0) {
        return SQ_OK;
    }

    out->rock = (unsigned char)((rock * 255) / counted);
    out->wall = (unsigned char)((wall * 255) / counted);
    out->radius = (unsigned char)r;
    out->counted = (unsigned short)((counted > 0xFFFF) ? 0xFFFF : counted);
    return SQ_OK;
}

int sq_read_floor_info(sq_floor_info *out)
{
    if (!out) {
        return SQ_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));

    /*
     * Sil-Q には町も荒野もクエスト階も無い——**常にダンジョン**である
     * （`kind = 2`。`FloorIdentity` の意味は `game_frame.h`）。
     */
    out->kind = 2;

    if (!character_generated) {
        return SQ_OK;
    }

    out->dun_level = p_ptr->depth;
    out->light_radius = p_ptr->cur_light;

    /* 訳し方は `sq_hud_data()` の `depth_text` と同じ（上の註記）。字面を割らない。 */
    if (p_ptr->depth <= 0) {
        my_strcpy(out->place_name, sq_tr("Surface"), sizeof(out->place_name));
    } else {
        strnfmt(out->place_name, sizeof(out->place_name), "%d ft", p_ptr->depth * 50);
    }

    return SQ_OK;
}

/* ==================================================================== 効果音 */

/*
 * 建付けは `sq_shim.h` の同名の節のとおり。積むのは `sq_null_term.c` の
 * `TERM_XTRA_SOUND`、汲むのは `sq_frame.cpp`。**汲んだら消える**（音は状態ではなく
 * 一度きりの出来事）。
 *
 * 溢れたら**古いほうを捨てる**。ゲームスレッドしか触らないので鍵は要らない
 * （上の `sq_text_input_flag` と同じ理由）。
 */

#define SQ_SOUND_MAX 32

static sq_sound_event sq_sound_ring[SQ_SOUND_MAX];
static int sq_sound_num = 0;
static unsigned long sq_sound_pushed_total = 0;
static int sq_sound_wanted = 0;

void sq_sound_set_wanted(int wanted)
{
    sq_sound_wanted = wanted ? 1 : 0;
    if (!sq_sound_wanted) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる（古い音が後で鳴らないように）。 */
        sq_sound_num = 0;
    }
}

void sq_sound_push(int v)
{
    sq_sound_event *ev;
    cptr name;

    if (!sq_sound_wanted) {
        return;
    }
    if ((v <= 0) || (v >= SOUND_MAX)) {
        return;
    }
    name = angband_sound_name[v];
    if (!name || !name[0]) {
        return;
    }

    if (sq_sound_num >= SQ_SOUND_MAX) {
        int i;
        for (i = 1; i < SQ_SOUND_MAX; i++) {
            sq_sound_ring[i - 1] = sq_sound_ring[i];
        }
        sq_sound_num = SQ_SOUND_MAX - 1;
    }

    ev = &sq_sound_ring[sq_sound_num++];
    my_strcpy(ev->name, name, sizeof(ev->name));
    /*
     * **鳴ったマスは @ のマス**にする。`TERM_XTRA_SOUND` は番号しか運ばないので
     * 音源の位置が分からない——画面側では頭で鳴る。見えていない敵の騒音に
     * マスを付けるのは P4の仕事である。
     */
    if (character_generated) {
        ev->y = (short)p_ptr->py;
        ev->x = (short)p_ptr->px;
    } else {
        ev->y = 0;
        ev->x = 0;
    }
    sq_sound_pushed_total++;
}

int sq_sound_take(sq_sound_event *out, int cap)
{
    int n = sq_sound_num;
    if (!out || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, sq_sound_ring, (size_t)n * sizeof(sq_sound_event));
    sq_sound_num = 0;
    return n;
}

unsigned long sq_sound_total(void)
{
    return sq_sound_pushed_total;
}

/* ============================================================== 文字入力（W1） */

/*!
 * `askfor_aux()` / `askfor_name()` の待ちの間だけ 1（`sq_shim.h` の `sq_text_input_set`）。
 *
 * ゲームスレッドしか触らない（`capture_frame()` も同じスレッド）ので鍵は要らない。
 */
static int sq_text_input_flag = 0;

void sq_text_input_set(int active)
{
    sq_text_input_flag = active ? 1 : 0;
}

int sq_text_input_active(void)
{
    return sq_text_input_flag;
}

int sq_digest_flag_check(char *report, int cap)
{
    unsigned long long before;
    unsigned long long after;
    int saved_generated;
    int saved_text;
    int failed = 0;

    if (report && (cap > 0)) {
        report[0] = 0;
    }

    saved_generated = character_generated ? 1 : 0;
    saved_text = sq_text_input_active();

    /* `sq_awaiting_command()` は @ が出来る前は数えないので、そこも立てて見る。 */
    character_generated = TRUE;

    /* --- 文字入力の旗（W1） --- */
    sq_text_input_set(0);
    before = sq_change_digest();
    sq_text_input_set(1);
    after = sq_change_digest();
    if (before == after) {
        failed = 1;
        if (report && (cap > 0)) {
            my_strcpy(report, "text_input_active", (size_t)cap);
        }
    }
    sq_text_input_set(saved_text);

    /* --- 命令待ちの旗（2026-08-23） --- */
    inkey_flag = FALSE;
    before = sq_change_digest();
    inkey_flag = TRUE;
    after = sq_change_digest();
    inkey_flag = FALSE;
    if (before == after) {
        failed = 1;
        if (report && (cap > 0)) {
            my_strcpy(report, "awaiting_command", (size_t)cap);
        }
    }

    /* --- コアが見ている区画（2026-08-26。`L` ＝ 地図を動かす） --- */
    {
        const int saved_wx = (int)p_ptr->wx;
        const int saved_locate = sq_locate_shifted ? 1 : 0;
        /*
         * **`sq_locate_active()` は階に居ないと 0 を返す。**立てずに測ると
         * 「混ざっていない」と誤って言う（実際に 1 度そう言われた）。
         */
        const int saved_dungeon = character_dungeon ? 1 : 0;
        character_dungeon = TRUE;

        before = sq_change_digest();
        p_ptr->wx = (s16b)(saved_wx + 1);
        after = sq_change_digest();
        p_ptr->wx = (s16b)saved_wx;
        if (before == after) {
            failed = 1;
            if (report && (cap > 0)) {
                my_strcpy(report, "panel_wx", (size_t)cap);
            }
        }

        sq_locate_shifted = FALSE;
        before = sq_change_digest();
        sq_locate_shifted = TRUE;
        after = sq_change_digest();
        sq_locate_shifted = (bool)saved_locate;
        if (before == after) {
            failed = 1;
            if (report && (cap > 0)) {
                my_strcpy(report, "locate_active", (size_t)cap);
            }
        }
        character_dungeon = (bool)saved_dungeon;
    }

    character_generated = (bool)saved_generated;
    return failed;
}

int sq_awaiting_command(void)
{
    /*
     * **@ が出来る前は数えない。**タイトルや誕生の途中にも `inkey_flag` は立つが、
     * そこでコマンドメニューを開かれても並べる命令が無い（遊びが始まっていない）。
     */
    if (!character_generated) {
        return 0;
    }
    return inkey_flag ? 1 : 0;
}
