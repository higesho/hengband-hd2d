/*!
 * @file fc_shim.c
 * @brief 境界の実装のうち「コアの名札」と「ゲーム状態の読み口」を持つ TU（設計 §3）。
 *
 * ここは **Frox のヘッダを include してよい場所**である（`fc_null_term.c` /
 * `fc_bootstrap.c` も同族）。C++ からは `fc_shim.h` 越しにしか見えない。
 *
 * ## 文字コード（adapter/ 全体の約束）
 * このファイルは **UTF-8（BOM 付き）**。`frox/src/` の写しは **純 ASCII**。
 * vcxproj は `/source-charset` を足していないので、MSVC は既定で ACP（932）として読み、
 * **BOM のあるファイルだけ UTF-8 として読む**。だから 2 つが同居できる。
 * - `frox/src/` に charset 指定を足してはいけない。
 * - `frox/adapter/` から BOM を落としてはいけない（註釈が化ける）。
 *
 * **M0 が返す文字列はすべて ASCII** である。M1 でコアの中が CP932 になったとき、
 * UTF-8 化するのは C++ 側（`fc_text.h`）の仕事で、この境界では変換しない。
 */

#include "angband.h"

#include "fc_shim.h"

#include "fc_lang_c.h" /* HUD の場所名の訳（M1 J4。表示にしか行かない口） */

#include <stdio.h>
#include <string.h>

/*
 * 版は `defines.h:19-22` の VER_* から組む。
 *
 *   #define VER_MAJOR 7
 *   #define VER_MINOR 3
 *   #define VER_PATCH "pipari"
 *   #define VER_EXTRA 2
 *
 * **`VER_PATCH` は文字列である。** だから `#` で文字列化してはいけない
 * （`"\"pipari\""` になる）。並べるだけで連結される。
 * 出来上がりは `src/Makefile.src:8` の `VERSION = 7.3.pipari.2` と一致する。
 * 数字の直書きはしない──写しを取り込み直したら自動で追随させるため。
 */
#define FC_STRINGIFY_(x) #x
#define FC_STRINGIFY(x) FC_STRINGIFY_(x)
#define FC_VERSION_STRING \
    FC_STRINGIFY(VER_MAJOR) "." FC_STRINGIFY(VER_MINOR) "." VER_PATCH "." FC_STRINGIFY(VER_EXTRA)

const char *fc_core_name(void)
{
    return "frox";
}

const char *fc_core_version(void)
{
    return FC_VERSION_STRING;
}

/* ==================================================================== 小さな道具 */

static void fc_copy_str(char *dst, int cap, cptr src)
{
    int n;

    if (!dst || (cap <= 0)) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    n = (int)strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

/* ================================================================== 状態の小口 */

int fc_character_generated(void)
{
    return character_generated ? 1 : 0;
}

int fc_rogue_like_commands(void)
{
    return rogue_like_commands ? 1 : 0;
}

int fc_screen_flags(void)
{
    int bits = 0;

    if (character_generated) {
        bits |= FC_SCREEN_GENERATED;
    }
    /*
     * **`character_icky` は計数器である。** 画面の退避が `++`、復帰が `--` する。
     * `== TRUE` で比べると入れ子の 2 段目を取り逃がす。非 0 だけを見る。
     */
    if (character_icky) {
        bits |= FC_SCREEN_ICKY;
    }
    if (character_xtra) {
        bits |= FC_SCREEN_XTRA;
    }
    if (character_dungeon) {
        bits |= FC_SCREEN_DUNGEON;
    }
    if (character_generated && p_ptr->is_dead) {
        bits |= FC_SCREEN_DEAD;
    }
    if (character_generated && p_ptr->playing) {
        bits |= FC_SCREEN_PLAYING;
    }
    return bits;
}

/* -------------------------------------- メッセージ行の置き場（FH-13。`fc_shim.h`） */

/*! `msg_on_startup()`（`init_angband()` の中）を通ったか。前に読むと落ちる。 */
static int fc_msg_ready = 0;

void fc_mark_msg_ready(void)
{
    fc_msg_ready = 1;
}

void fc_msg_rect_use_shop_for_test(int on)
{
    if (!fc_msg_ready) {
        return;
    }
    msg_line_init(on ? ui_shop_msg_rect() : ui_msg_rect());
}

int fc_msg_rect(int *x, int *y, int *w, int *h)
{
    rect_t r;

    if (!fc_msg_ready) {
        return FC_ERR_STATE;
    }
    r = msg_line_rect();
    if (x) {
        *x = r.x;
    }
    if (y) {
        *y = r.y;
    }
    if (w) {
        *w = r.cx;
    }
    if (h) {
        *h = r.cy;
    }
    return FC_OK;
}

/* ------------------------------------------------ doc UI の旗（M0.5。設計 §14.2(a)） */

/*!
 * doc UI が開いているか。真偽 1 つで足りる——doc UI は入れ子でも save のたびに
 * 立て直すだけで、降ろすのは「コマンド待ちまで戻った」の 1 点だから。
 */
static int fc_doc_ui_open = 0;

/*!
 * @brief 6 TU の `Term_save()` はここへ来る（vcxproj の define。`fc_shim.h`）。
 */
errr fc_ui_term_save(void)
{
    fc_doc_ui_open = 1;
    return Term_save();
}

/*!
 * @brief 6 TU の `Term_load()` はここへ来る。**旗は降ろさない**——
 * 鍛冶（`weaponsmith.c`）は小画面から戻るたび load する（save 1 / load 10）。
 */
errr fc_ui_term_load(void)
{
    return Term_load();
}

/*!
 * @brief 品選びの窓が開いているか（フック #37）。
 * @details `fc_doc_ui_active()` の掛け金とは別に、`obj_prompt()` の入りと
 * 片づけでそのまま上げ下げする。入れ子にはならない（`obj_prompt` は再入しない）。
 */
static int fc_obj_prompt_open = 0;

void fc_obj_prompt_scope(int on)
{
    fc_obj_prompt_open = (on != 0) ? 1 : 0;
}

int fc_obj_prompt_active(void)
{
    return fc_obj_prompt_open;
}

int fc_doc_ui_active(void)
{
    /*
     * `inkey_flag` はコマンド待ち（`request_command` → `inkey()`）でだけ TRUE
     * （`util.c:3731`。doc UI の中の待ちは全部 FALSE——設計 §14.1 の事実 3）。
     * そこまで戻った＝ doc UI はもう畳まれているので、ここで降ろす。
     * 店の中で開いた doc UI は店を出るまで降りない（店の inkey は旗を立てない）が、
     * 店の間は `character_icky` でどのみちミラーが開いているので見た目は変わらない。
     */
    if (fc_doc_ui_open && inkey_flag) {
        fc_doc_ui_open = 0;
    }
    return fc_doc_ui_open;
}

int fc_read_limits(int *out, int cap)
{
    if (!out || (cap < 6)) {
        return FC_ERR_ARG;
    }
    out[0] = (int)max_r_idx;
    out[1] = (int)max_k_idx;
    out[2] = (int)max_f_idx;
    out[3] = (int)max_a_idx;
    out[4] = (int)max_e_idx;
    out[5] = (int)max_d_idx;
    return 6;
}

/* ================================================================== HUD の読み口 */

/*!
 * @brief いまの場所の名。`prt_depth()`（`xtra1.c:1620`）の分岐を写す。
 *
 * @details 幻想蛮怒版と違うのは町の引き方だけ——Frox に `town[]` の配列は無く、
 * `town_name(int)`（`shop.h:91`）が名を返す。
 */
static void fc_place_name(char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!character_generated) {
        return;
    }

    /*
     * どの枝も **カタログを通してから** 写す（M1 J4。承認済みの町名など）。
     * ここはプロトコルへ運ぶだけの表示専用の口なので、裸の 1 語でも
     * `fc_tr` の丸ごと引きで安全に訳せる（設計 §4 追記 (d) と同じ理屈）。
     * 日本語層が寝ていれば `fc_tr` は原文をそのまま返す。
     */
    if (dun_level && d_info && d_name && (dungeon_type < max_d_idx)) {
        fc_copy_str(out, cap, fc_tr(d_name + d_info[dungeon_type].name));
        return;
    }

    if (p_ptr->inside_arena) {
        fc_copy_str(out, cap, fc_tr("Gladiator Ring"));
        return;
    }
    if (p_ptr->inside_battle) {
        fc_copy_str(out, cap, fc_tr("Monster Pit"));
        return;
    }
    if (p_ptr->town_num > 0) {
        fc_copy_str(out, cap, fc_tr(town_name(p_ptr->town_num)));
        return;
    }

    fc_copy_str(out, cap, fc_tr("Wilderness"));
}

/*! @brief 深さの文言。`prt_depth()` と同じ言い回しにする（**M0 は英語**）。 */
static void fc_depth_text(char *out, int cap)
{
    char buf[32];

    if (!out || (cap <= 0)) {
        return;
    }
    out[0] = '\0';

    if (!character_generated) {
        return;
    }

    if (!dun_level) {
        strcpy(buf, "Surface");
    } else if (quests_get_current() && !dungeon_type) {
        strcpy(buf, "Quest");
    } else if (depth_in_feet) {
        sprintf(buf, "%d ft", (int)dun_level * 50);
    } else {
        sprintf(buf, "L%d", (int)dun_level);
    }

    /* 場所名と同じく、表示専用の口なのでカタログを通す（Surface / Quest）。 */
    fc_copy_str(out, cap, fc_tr(buf));
}

int fc_read_hud(fc_hud_data *out)
{
    if (!out) {
        return FC_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));

    fc_copy_str(out->name, (int)sizeof(out->name), player_name);
    out->in_game = character_generated ? 1 : 0;

    /*
     * `p_ptr` は静的な実体なので常に読める。ただし `character_generated` が偽の
     * あいだは中身が既定値のままで、誕生画面の途中経過を意味しない。
     * **数値を出すのはゲームが始まってから**にする。
     */
    if (!character_generated) {
        return FC_OK;
    }

    out->hp = (int)p_ptr->chp;
    out->hp_max = (int)p_ptr->mhp;
    out->sp = (int)p_ptr->csp;
    out->sp_max = (int)p_ptr->msp;
    out->level = (int)p_ptr->lev;
    out->depth = (int)dun_level;
    out->gold = (long)p_ptr->au;

    fc_place_name(out->place, (int)sizeof(out->place));
    fc_depth_text(out->depth_text, (int)sizeof(out->depth_text));

    return FC_OK;
}

/* ============================================================= メッセージの読み口 */

int fc_message_count(void)
{
    return msg_count();
}

int fc_message_text(int age, char *out, int cap)
{
    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }

    out[0] = '\0';
    if (age < 0) {
        return 0;
    }

    /*
     * **Frox のメッセージはドキュメント片である**（`message.c`。色つきの
     * `doc` を持つ）。素の字が欲しいので、あちらが用意している
     * `msg_get_plain_text()`（`message.h:31`）を通す。範囲外は 0 を返す。
     */
    if (msg_get_plain_text(age, out, cap) <= 0) {
        out[0] = '\0';
        return 0;
    }
    /*
     * **改行を空白へ倒す。** Frox のメッセージはドキュメント片で、中に生の
     * 改行を含むものがある（誕生直後の歓迎文など）。1 行の器（`MessageEvent`）へ
     * そのまま載せると、画面側で豆腐（描けない字の箱）になる——実機の絵で見た。
     */
    {
        int i;
        for (i = 0; out[i]; i++) {
            if ((out[i] == '\n') || (out[i] == '\r') || (out[i] == '\t')) {
                out[i] = ' ';
            }
        }
    }
    return (int)strlen(out);
}

/* =================================================================== 地図の読み口 */

int fc_floor_size(int *width, int *height)
{
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_ERR_STATE;
    }
    if (width) {
        *width = (int)cur_wid;
    }
    if (height) {
        *height = (int)cur_hgt;
    }
    return FC_OK;
}

int fc_player_kind(int *pclass, int *prace)
{
    if (!character_generated) {
        return FC_ERR_STATE;
    }
    if (pclass) {
        *pclass = (int)p_ptr->pclass;
    }
    if (prace) {
        *prace = (int)p_ptr->prace;
    }
    return FC_OK;
}

int fc_player_pos(int *x, int *y)
{
    if (!character_generated || !character_dungeon) {
        return FC_ERR_STATE;
    }
    if (x) {
        *x = px;
    }
    if (y) {
        *y = py;
    }
    return FC_OK;
}

/*!
 * @brief そのマスの地形が「見えている」か。**`map_info()` と同じ判定にする**。
 *
 * @details 判定を独自に作ると、コアが画面に描いているものと HD2D の立体が食い違う
 * （記号は伏せたまま形だけ漏れる、あるいはその逆）。`cave.c:970` 以降の分岐を写す:
 * - `FF_REMEMBER` を持つ地形（壁・扉・階段）… `CAVE_MARK` だけ。盲目でも消えない
 * - それ以外（床）… 盲目なら見えない。`CAVE_MARK|CAVE_LITE|CAVE_MNLT` か、
 *   視界内かつ（自照かつ消灯でない、または暗視）
 */
static int fc_grid_known(cave_type *c_ptr, s16b feat)
{
    feature_type *f_ptr = &f_info[feat];

    if (have_flag(f_ptr->flags, FF_REMEMBER)) {
        return (c_ptr->info & CAVE_MARK) ? 1 : 0;
    }

    if (p_ptr->blind) {
        return 0;
    }
    if (c_ptr->info & (CAVE_MARK | CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    if ((c_ptr->info & CAVE_VIEW)
        && (((c_ptr->info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) || p_ptr->see_nocto)) {
        return 1;
    }
    return 0;
}

/*! @brief いま照明が当たっているか。 */
static int fc_grid_lit_now(cave_type *c_ptr)
{
    if (c_ptr->info & CAVE_MNDK) {
        return 0;
    }
    if (c_ptr->info & (CAVE_LITE | CAVE_MNLT)) {
        return 1;
    }
    return ((c_ptr->info & (CAVE_GLOW | CAVE_VIEW)) == (CAVE_GLOW | CAVE_VIEW)) ? 1 : 0;
}

/*! @brief 地形フラグ → `FC_FEAT_*`。`KNOWN` / `PLAYER` は呼び出し側。 */
static unsigned int fc_feature_bits(s16b feat, cave_type *c_ptr)
{
    feature_type *f_ptr = &f_info[feat];
    unsigned int bits = 0;

    if (have_flag(f_ptr->flags, FF_WALL)) {
        bits |= FC_FEAT_WALL;
    }
    if (have_flag(f_ptr->flags, FF_DOOR)) {
        bits |= FC_FEAT_DOOR;
        /*
         * 開いているか。**`FF_CLOSE`（＝「閉じる」の対象＝いま開いている）で見る。**
         * `FF_OPEN` は「開ける」の対象＝まだ閉じている側なので、意味が逆になる。
         */
        if (have_flag(f_ptr->flags, FF_CLOSE)) {
            bits |= FC_FEAT_DOOR_OPEN;
        }
    }
    /*
     * 瓦礫。`FF_HURT_ROCK`（岩石溶解の対象）は壁・鉱脈・隠し扉にも付くので、
     * **壁でも扉でもない HURT_ROCK** を瓦礫と読む。
     */
    if (have_flag(f_ptr->flags, FF_HURT_ROCK) && !(bits & (FC_FEAT_WALL | FC_FEAT_DOOR))) {
        bits |= FC_FEAT_RUBBLE;
    }
    /* 階段のほか、ダンジョンの口とクエストの入口も「降りる場所」として同じ扱いにする。 */
    if (have_flag(f_ptr->flags, FF_STAIRS) || have_flag(f_ptr->flags, FF_ENTRANCE)
        || have_flag(f_ptr->flags, FF_QUEST_ENTER)) {
        bits |= FC_FEAT_STAIRS;
    }
    if (have_flag(f_ptr->flags, FF_PERMANENT)) {
        bits |= FC_FEAT_PERMANENT;
    }
    if (have_flag(f_ptr->flags, FF_TREE)) {
        bits |= FC_FEAT_TREE;
    }
    if (have_flag(f_ptr->flags, FF_WATER)) {
        bits |= FC_FEAT_WATER;
    }
    if (have_flag(f_ptr->flags, FF_LAVA)) {
        bits |= FC_FEAT_LAVA;
    }
    if (have_flag(f_ptr->flags, FF_GLOW)) {
        bits |= FC_FEAT_GLOW;
    }
    /* 通り抜けられる床（HD2D の扉の向き判定の主材料）。壁・扉・木・岩は明示的に外す。 */
    if (have_flag(f_ptr->flags, FF_MOVE)
        && !(bits & (FC_FEAT_WALL | FC_FEAT_DOOR | FC_FEAT_TREE | FC_FEAT_RUBBLE))) {
        bits |= FC_FEAT_PASSABLE;
    }
    if (c_ptr->info & CAVE_ROOM) {
        bits |= FC_FEAT_ROOM;
    }
    if ((c_ptr->info & (CAVE_GLOW | CAVE_MNDK)) == CAVE_GLOW) {
        bits |= FC_FEAT_GLOWING;
    }
    return bits;
}

int fc_read_map(int x0, int y0, int w, int h, fc_map_cell *out, int cap)
{
    int vx;
    int vy;
    int n = 0;

    if (!out || (w <= 0) || (h <= 0) || (cap < (w * h))) {
        return FC_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_ERR_STATE;
    }

    for (vy = 0; vy < h; vy++) {
        const int gy = y0 + vy;
        for (vx = 0; vx < w; vx++) {
            const int gx = x0 + vx;
            fc_map_cell *cell = &out[n++];
            cave_type *c_ptr;
            s16b feat;
            byte a = 0;
            char c = ' ';
            byte ta = 0;
            char tc = ' ';
            int known;

            memset(cell, 0, sizeof(*cell));
            cell->gx = (short)gx;
            cell->gy = (short)gy;
            cell->ascii = ' ';

            /* 階の外。**空白で返す**（画面側が黒く塗る）。 */
            if (!in_bounds2(gy, gx)) {
                continue;
            }

            c_ptr = &cave[gy][gx];
            feat = get_feat_mimic(c_ptr);
            known = fc_grid_known(c_ptr, feat);

            /* 見た目はコアの `map_info()` そのまま（記号・色を作り直さない）。 */
            map_info(gy, gx, &a, &c, &ta, &tc);
            cell->fg_color = (unsigned char)(a & 0x0F);
            cell->bg_color = (unsigned char)(ta & 0x0F);
            cell->ascii = c;

            if (known) {
                cell->terrain_id = (unsigned short)feat;
                cell->feature_flags = (unsigned short)(FC_FEAT_KNOWN | fc_feature_bits(feat, c_ptr));
            }
            /* @ の居場所は既知・未知に関わらず立てる（隠す情報ではない）。 */
            if ((gy == py) && (gx == px)) {
                cell->feature_flags = (unsigned short)(cell->feature_flags | FC_FEAT_PLAYER);
            }

            if (fc_grid_lit_now(c_ptr)) {
                cell->light_level = 2;
            } else if (c_ptr->info & CAVE_VIEW) {
                cell->light_level = 1;
            } else {
                cell->light_level = 0;
            }

            /*
             * モンスターは **`ml`（視認中）のときだけ**。種族は `ap_r_idx`
             * （見かけの種族）を使う——`map_info` が絵に使うのもこちらで、
             * 変化・変装の敵で記号とタイルが食い違わないようにする。
             */
            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = &m_list[c_ptr->m_idx];
                if (m_ptr->r_idx && m_ptr->ml) {
                    cell->monster_id = (unsigned short)(m_ptr->ap_r_idx ? m_ptr->ap_r_idx : m_ptr->r_idx);
                    cell->monster_slot = (unsigned short)c_ptr->m_idx;
                }
            }

            /*
             * 床のアイテムは `OM_FOUND`（見つけたもの）の先頭 1 つだけ。
             * **いま照らされているマスに限る**（`light_level == 2`）——記憶している
             * だけの暗いマスで立てると、画面側が「実体がいる」と見て板を探しに行き、
             * 絵の決め方（地形が勝つ）と食い違って必ず外れる（幻想蛮怒で踏んだ穴）。
             * 記憶しているだけの物は Term の記号（`ascii`）には残るので情報は落ちない。
             */
            if (cell->light_level >= 2) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = &o_list[this_o_idx];
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        cell->object_id = (unsigned short)o_ptr->k_idx;
                        break;
                    }
                }
            }

            /* 未視認のマスは地形を出さない（真っ暗）。ただし見えている敵の記号は残す。 */
            if (!known) {
                cell->bg_color = 0;
                if (!cell->monster_id) {
                    cell->ascii = ' ';
                    cell->fg_color = 0;
                }
            }
        }
    }

    return n;
}

int fc_read_minimap(unsigned char *out, int cap)
{
    int gx;
    int gy;
    int need;

    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_ERR_STATE;
    }

    need = (int)cur_hgt * (int)cur_wid;
    if (cap < need) {
        return FC_ERR_ARG;
    }
    memset(out, FC_MM_UNKNOWN, (size_t)need);

    for (gy = 0; gy < (int)cur_hgt; gy++) {
        for (gx = 0; gx < (int)cur_wid; gx++) {
            cave_type *c_ptr = &cave[gy][gx];
            const s16b feat = get_feat_mimic(c_ptr);
            feature_type *f_ptr = &f_info[feat];
            const int known = fc_grid_known(c_ptr, feat);
            int monster_here = 0;
            unsigned char kind;

            if (c_ptr->m_idx > 0 && c_ptr->m_idx < m_max) {
                monster_type *m_ptr = &m_list[c_ptr->m_idx];
                monster_here = (m_ptr->r_idx && m_ptr->ml) ? 1 : 0;
            }
            /* 未踏破は出さない。ただし見えている敵（テレパシー等）は出す。 */
            if (!known && !monster_here) {
                continue;
            }

            kind = FC_MM_FLOOR;
            if (known) {
                if (have_flag(f_ptr->flags, FF_STAIRS) || have_flag(f_ptr->flags, FF_ENTRANCE)
                    || have_flag(f_ptr->flags, FF_QUEST_ENTER)) {
                    kind = FC_MM_STAIRS;
                } else if (have_flag(f_ptr->flags, FF_DOOR)) {
                    kind = FC_MM_DOOR;
                } else if (have_flag(f_ptr->flags, FF_WALL)) {
                    /* 山は壁と分けて送る（町の読み方が「敷地か岩山か」を決めるのに使う）。 */
                    kind = have_flag(f_ptr->flags, FF_MOUNTAIN) ? FC_MM_MOUNTAIN : FC_MM_WALL;
                } else if (have_flag(f_ptr->flags, FF_TREE)) {
                    /*
                     * 歩ける地形の細別（FH-09）。Frox の町と荒野は草・木・水が大半で、
                     * 全部 FLOOR に畳むと一色塗りになる。**歩ける意味は変えない**
                     * （画面側の経路判定は FLOOR と同じに扱う）。順は
                     * 木 → 水 → 溶岩 → 雪 → 草——雪解け（SLUSH）のように旗が
                     * 重なる地形は、より「見て効く」ほうに倒す。
                     */
                    kind = FC_MM_TREE;
                } else if (have_flag(f_ptr->flags, FF_WATER)) {
                    kind = FC_MM_WATER;
                } else if (have_flag(f_ptr->flags, FF_LAVA)) {
                    kind = FC_MM_LAVA;
                } else if (have_flag(f_ptr->flags, FF_SNOW)) {
                    kind = FC_MM_SNOW;
                } else if ((feat == feat_grass) || (feat == feat_flower)) {
                    /* 草には旗が無い（`FF_GRASS` は存在しない）。番号で見る。 */
                    kind = FC_MM_GRASS;
                }
            }
            if (known && fc_grid_lit_now(c_ptr) && c_ptr->o_idx && (kind != FC_MM_STAIRS)) {
                s16b this_o_idx;
                s16b next_o_idx = 0;
                for (this_o_idx = c_ptr->o_idx; this_o_idx; this_o_idx = next_o_idx) {
                    object_type *o_ptr = &o_list[this_o_idx];
                    next_o_idx = o_ptr->next_o_idx;
                    if (o_ptr->k_idx && (o_ptr->marked & OM_FOUND)) {
                        kind = FC_MM_ITEM;
                        break;
                    }
                }
            }
            if (monster_here) {
                kind = FC_MM_MONSTER;
            }
            out[(gy * (int)cur_wid) + gx] = kind;
        }
    }

    if (in_bounds2(py, px)) {
        out[(py * (int)cur_wid) + px] = FC_MM_PLAYER;
    }

    return need;
}


/* ============================================ 周囲の地形の内訳（環境音の層） */

/*! 数える半径（マス）。**耳が届くと思える範囲**であって、視界とは別。変愚と同じ 12。 */
#define FC_SURROUNDINGS_RADIUS 12

int fc_read_surroundings(fc_surroundings *out)
{
    int dy;
    int dx;
    int counted = 0;
    int grass = 0;
    int tree = 0;
    int dirt = 0;
    int swamp = 0;
    int water = 0;
    int deep_water = 0;
    int lava = 0;
    int rock = 0;
    int glass = 0;
    int wall = 0;
    const int r = FC_SURROUNDINGS_RADIUS;

    if (!out) {
        return FC_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!character_generated || !character_dungeon || (cur_hgt <= 0) || (cur_wid <= 0)) {
        return FC_OK; /* radius 0 のまま＝「数えていない」 */
    }

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            const int gy = py + dy;
            const int gx = px + dx;
            cave_type *c_ptr;
            feature_type *f_ptr;
            s16b feat;
            int is_wall;

            if (((dx * dx) + (dy * dy)) > (r * r)) {
                continue; /* 円で数える */
            }
            if (!in_bounds2(gy, gx)) {
                continue;
            }
            c_ptr = &cave[gy][gx];
            feat = get_feat_mimic(c_ptr);
            if (!fc_grid_known(c_ptr, feat)) {
                continue;
            }
            counted++;

            f_ptr = &f_info[feat];
            is_wall = have_flag(f_ptr->flags, FF_WALL) ? 1 : 0;
            if (is_wall) {
                wall++;
            }
            if (have_flag(f_ptr->flags, FF_GLASS)) {
                glass++;
            }

            /* ---- 材料は排他。順番に意味がある（`fc_shim.h` の註記）---- */
            if (feat == feat_swamp) {
                swamp++;
            } else if (have_flag(f_ptr->flags, FF_LAVA)) {
                lava++;
            } else if (have_flag(f_ptr->flags, FF_WATER)) {
                if (have_flag(f_ptr->flags, FF_DEEP)) {
                    deep_water++;
                } else {
                    water++;
                }
            } else if (have_flag(f_ptr->flags, FF_TREE)) {
                tree++;
            } else if (feat == feat_mountain) {
                rock++;
            } else if (have_flag(f_ptr->flags, FF_HURT_ROCK) && !is_wall
                && !have_flag(f_ptr->flags, FF_DOOR)) {
                rock++; /* 瓦礫（変愚の STONE に当たる） */
            } else if ((feat == feat_grass) || (feat == feat_brake) || (feat == feat_flower)) {
                grass++;
            } else if (feat == feat_dirt) {
                dirt++;
            }
        }
    }

    if (counted <= 0) {
        return FC_OK;
    }

#define FC_SURR_RATIO(N) ((unsigned char)(((N) * 255) / counted))
    out->grass = FC_SURR_RATIO(grass);
    out->tree = FC_SURR_RATIO(tree);
    out->dirt = FC_SURR_RATIO(dirt);
    out->swamp = FC_SURR_RATIO(swamp);
    out->water = FC_SURR_RATIO(water);
    out->deep_water = FC_SURR_RATIO(deep_water);
    out->lava = FC_SURR_RATIO(lava);
    out->rock = FC_SURR_RATIO(rock);
    out->glass = FC_SURR_RATIO(glass);
    out->wall = FC_SURR_RATIO(wall);
#undef FC_SURR_RATIO
    out->radius = (unsigned char)r;
    out->counted = (unsigned short)((counted > 0xFFFF) ? 0xFFFF : counted);
    return FC_OK;
}

int fc_read_floor_info(fc_floor_info *out)
{
    int day = 0;
    int hour = 0;
    int minute = 0;

    if (!out) {
        return FC_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->day_minute = 720;
    out->daytime = 1;

    if (!character_generated) {
        return FC_OK;
    }

    out->dungeon_id = (int)dungeon_type;
    out->dun_level = (int)dun_level;
    out->town_id = (int)p_ptr->town_num;
    out->wild_mode = p_ptr->wild_mode ? 1 : 0;

    if (p_ptr->inside_arena || p_ptr->inside_battle) {
        out->kind = 4; /* 闘技場 */
    } else if (quests_get_current() && !dungeon_type) {
        out->kind = 3; /* クエスト階 */
    } else if (dun_level) {
        out->kind = 2; /* ダンジョン */
    } else {
        out->kind = 1; /* 地上（町・荒野・広域マップ） */
    }

    fc_place_name(out->place_name, (int)sizeof(out->place_name));

    extract_day_hour_min(&day, &hour, &minute);
    out->day_minute = ((hour % 24) * 60) + (minute % 60);
    out->daytime = is_daytime() ? 1 : 0;
    out->light_radius = (int)p_ptr->cur_lite;

    return FC_OK;
}

/* ============================================================ 戦闘の見せ場（FH-05） */

/*
 * 建付けは `fc_shim.h` の節註のとおり。ここに在るのは
 *   - 見せ場の環（`fc_fx_take` で汲んだら消える。幻想蛮怒の `gb_fx_ring` と同じ形）
 *   - 描画色 → element の逆引き（**色が往復でほぼ保たれる**ように選ぶ）
 *   - HP の減りの走査（被弾・命中・とどめ）
 */

#define FC_FX_MAX 128

static fc_fx_event fc_fx_ring[FC_FX_MAX];
static int fc_fx_num = 0;
static unsigned long fc_fx_pushed_total = 0;

/*! 直前の弾道セル（Bolt の src を繋ぐ）。game_turn が変わったら忘れる。 */
static int fc_fx_last_bolt_y = -1;
static int fc_fx_last_bolt_x = -1;
static s32b fc_fx_last_bolt_turn = -1;

/*!
 * `sound(SOUND_KILL)` を聞いた数と、最後に聞いた turn。
 *
 * **走査ごとに使い切ってはいけない**——音（`mon_take_hit`）とスロットの消滅
 * （`delete_monster_idx`）は同じコマンドの中でも別の capture に割れることがある
 * （fresh のたびに frame が出るため。実測 2026-08-24: とどめだけ絵が出なかった）。
 * 代わりに **turn の年齢で失効**させる——古い音を後のテレポート消滅に
 * 化けさせないための守り。
 */
static int fc_fx_kill_sounds = 0;
static s32b fc_fx_kill_heard_turn = 0;
#define FC_FX_KILL_SOUND_AGE 50 /*!< これより古い音は捨てる（1 ターン＝100 前後） */

/*!
 * @brief 描画色（TERM の 16 色）→ `CombatFxElement`。
 *
 * @details 弾道の色は `spell_color()` が `lib/pref/spell-xx.prf` の `Z:` 行
 * から選ぶ。
 * GF_* の番号はこちらへ届かないので、**色から束へ**逆に引く。element は
 * 画面側で色にしか使われない（`combat_fx_view.cpp` の `color_of`）ので、
 * 「描かれた色 → 画面で同系の色になる束」を選べば、コアの宣言した色が
 * 画面でもほぼ保たれる。番号は `CombatFxElement` と同じ（0=物理 … 9=精神）。
 */
static unsigned char fc_fx_element_of_attr(int attr)
{
    static const unsigned char table[16] = {
        6, /* 0 DARK      -> Dark（黒。いちばん暗い束へ） */
        0, /* 1 WHITE     -> Physical（白。COLD の `w` もここ——画面でも白系） */
        0, /* 2 SLATE     -> Physical（灰） */
        1, /* 3 ORANGE    -> Fire */
        1, /* 4 RED       -> Fire */
        4, /* 5 GREEN     -> Acid（緑。POIS の `g` もここ——画面でも緑） */
        2, /* 6 BLUE      -> Cold（青系） */
        0, /* 7 UMBER     -> Physical（茶。ARROW / CONFUSION の `U`） */
        6, /* 8 L_DARK    -> Dark */
        0, /* 9 L_WHITE   -> Physical */
        5, /* 10 VIOLET   -> Poison（画面の紫） */
        3, /* 11 YELLOW   -> Elec（黄） */
        1, /* 12 L_RED    -> Fire */
        4, /* 13 L_GREEN  -> Acid */
        2, /* 14 L_BLUE   -> Cold */
        0, /* 15 L_UMBER  -> Physical */
    };
    return table[attr & 0x0F];
}

static void fc_fx_push(const fc_fx_event *ev)
{
    if (fc_fx_num >= FC_FX_MAX) {
        int i;
        for (i = 1; i < FC_FX_MAX; i++) {
            fc_fx_ring[i - 1] = fc_fx_ring[i];
        }
        fc_fx_num = FC_FX_MAX - 1;
    }
    fc_fx_ring[fc_fx_num++] = *ev;
    fc_fx_pushed_total++;
}

int fc_fx_take(fc_fx_event *out, int cap)
{
    int n = fc_fx_num;
    if (!out || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, fc_fx_ring, (size_t)n * sizeof(fc_fx_event));
    /* 汲んだら消す（消さないと同じ縁を何度も拾って演出が止まらない）。 */
    fc_fx_num = 0;
    return n;
}

unsigned long fc_fx_total(void)
{
    return fc_fx_pushed_total;
}

void fc_fx_note_kill_sound(void)
{
    if (fc_fx_kill_sounds < 8) {
        fc_fx_kill_sounds++;
    }
    fc_fx_kill_heard_turn = game_turn;
}

/* ==================================================================== 効果音 */

/*
 * 見せ場の環（上）と同じ形。違いは 2 つだけ。
 *   - 画面側が欲しがっているときしか積まない（`fc_sound_wanted`）
 *   - 溢れたら**古いほうを捨てる**（見せ場は詰め直すが、音は 1 コマンドで
 *     何十も出ることがあり、詰め直しの写しが割に合わない）
 */

#define FC_SOUND_MAX 32

static fc_sound_event fc_sound_ring[FC_SOUND_MAX];
static int fc_sound_num = 0;
static unsigned long fc_sound_pushed_total = 0;
static int fc_sound_wanted = 0;

void fc_sound_set_wanted(int wanted)
{
    fc_sound_wanted = wanted ? 1 : 0;
    if (!fc_sound_wanted) {
        /* 欲しがらなくなったら溜まっているぶんも捨てる（古い音が後で鳴らないように）。 */
        fc_sound_num = 0;
    }
}

void fc_sound_push(int v)
{
    fc_sound_event *ev;
    const char *name;

    if (!fc_sound_wanted) {
        return;
    }
    if ((v <= 0) || (v >= SOUND_MAX)) {
        return;
    }
    name = angband_sound_name[v];
    if (!name || !name[0]) {
        return;
    }

    if (fc_sound_num >= FC_SOUND_MAX) {
        /* 溢れたら最も古い 1 つを捨てる。 */
        int i;
        for (i = 1; i < FC_SOUND_MAX; i++) {
            fc_sound_ring[i - 1] = fc_sound_ring[i];
        }
        fc_sound_num = FC_SOUND_MAX - 1;
    }

    ev = &fc_sound_ring[fc_sound_num++];
    my_strcpy(ev->name, name, sizeof(ev->name));
    /*
     * **鳴ったマスは @ のマス**にする。`TERM_XTRA_SOUND` は番号しか運ばないので
     * 音源の位置が分からない——画面側では頭で鳴る。マスを持つ音は後から
     * 呼び出し箇所ごとに足していく。
     */
    if (character_generated) {
        ev->y = (short)py;
        ev->x = (short)px;
    } else {
        ev->y = 0;
        ev->x = 0;
    }
    fc_sound_pushed_total++;
}

int fc_sound_take(fc_sound_event *out, int cap)
{
    int n = fc_sound_num;
    if (!out || (cap <= 0)) {
        return 0;
    }
    if (n > cap) {
        n = cap;
    }
    memcpy(out, fc_sound_ring, (size_t)n * sizeof(fc_sound_event));
    /* 汲んだら消す。音は状態ではなく一度きりの出来事である（設計 §2.3）。 */
    fc_sound_num = 0;
    return n;
}

unsigned long fc_sound_total(void)
{
    return fc_sound_pushed_total;
}

void fc_fx_push_spark(int gy, int gx, int ch, int attr)
{
    fc_fx_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.element = fc_fx_element_of_attr(attr);
    ev.y = (short)gy;
    ev.x = (short)gx;
    ev.src_y = (short)gy;
    ev.src_x = (short)gx;
    /* 強さは動きの絵なので固定（ダメージの割合は HP の走査のほうが持つ）。 */
    ev.num = 1;
    ev.den = 2;

    if (ch == '*') {
        /* 爆発・ボール（`bolt_pict` の base 0x30）。そのマスで弾ける。 */
        ev.kind = FC_FX_KIND_HIT_MONSTER;
        fc_fx_last_bolt_y = -1;
        fc_fx_last_bolt_x = -1;
    } else {
        /*
         * 弾（`|` `-` `/` `\`）。直前の弾道セルが同じ turn で隣接していれば
         * そこからの線にする（画面は src -> dst の線を引く）。
         */
        ev.kind = FC_FX_KIND_BOLT;
        if ((fc_fx_last_bolt_turn == game_turn) && (fc_fx_last_bolt_y >= 0)
            && (ABS(fc_fx_last_bolt_y - gy) <= 2) && (ABS(fc_fx_last_bolt_x - gx) <= 2)) {
            ev.src_y = (short)fc_fx_last_bolt_y;
            ev.src_x = (short)fc_fx_last_bolt_x;
        }
        fc_fx_last_bolt_y = gy;
        fc_fx_last_bolt_x = gx;
        fc_fx_last_bolt_turn = game_turn;
    }
    fc_fx_push(&ev);
}

/*!
 * @name HP の走査の前回値
 * @details スロットの使い回しに備えて `r_idx` も覚える。階が替わったら
 * （`character_dungeon` が降りたら／場所の素性が動いたら）**黙って捨てる**
 * ——階またぎの消滅にとどめの絵を出すのは嘘になる。
 * @{
 */
typedef struct fc_fx_mon_prev {
    s16b r_idx;
    s16b hp;
    s16b maxhp;
    byte fy;
    byte fx;
    byte ml;
} fc_fx_mon_prev;

#define FC_FX_MON_PREV_MAX 1024

static fc_fx_mon_prev fc_fx_mon_prev_tab[FC_FX_MON_PREV_MAX];
static int fc_fx_prev_valid = 0;
static int fc_fx_prev_chp = 0;
static int fc_fx_prev_dungeon_type = -1;
static int fc_fx_prev_dun_level = -1;
static int fc_fx_prev_wild_x = -1;
static int fc_fx_prev_wild_y = -1;
/*! @} */

/*! @brief 前回値を取り直すだけ（見せ場は積まない）。 */
static void fc_fx_rebase(void)
{
    int i;
    int n = (int)m_max;

    if (n > FC_FX_MON_PREV_MAX) {
        n = FC_FX_MON_PREV_MAX;
    }
    memset(fc_fx_mon_prev_tab, 0, sizeof(fc_fx_mon_prev_tab));
    for (i = 1; i < n; i++) {
        monster_type *m_ptr = &m_list[i];
        if (!m_ptr->r_idx) {
            continue;
        }
        fc_fx_mon_prev_tab[i].r_idx = m_ptr->r_idx;
        fc_fx_mon_prev_tab[i].hp = m_ptr->hp;
        fc_fx_mon_prev_tab[i].maxhp = m_ptr->maxhp;
        fc_fx_mon_prev_tab[i].fy = m_ptr->fy;
        fc_fx_mon_prev_tab[i].fx = m_ptr->fx;
        fc_fx_mon_prev_tab[i].ml = m_ptr->ml ? 1 : 0;
    }
    fc_fx_prev_chp = character_generated ? (int)p_ptr->chp : 0;
    fc_fx_prev_dungeon_type = (int)dungeon_type;
    fc_fx_prev_dun_level = (int)dun_level;
    fc_fx_prev_wild_x = (int)p_ptr->wilderness_x;
    fc_fx_prev_wild_y = (int)p_ptr->wilderness_y;
    fc_fx_prev_valid = 1;
}

void fc_fx_scan_deltas(void)
{
    int i;
    int n;
    int floor_moved;

    if (!character_generated || !character_dungeon || p_ptr->is_dead) {
        /* @ がまだ無い・階がまだ無い・もう死んだ。次に揃ったとき取り直す。 */
        fc_fx_prev_valid = 0;
        fc_fx_kill_sounds = 0;
        return;
    }

    floor_moved = (fc_fx_prev_dungeon_type != (int)dungeon_type)
        || (fc_fx_prev_dun_level != (int)dun_level)
        || (fc_fx_prev_wild_x != (int)p_ptr->wilderness_x)
        || (fc_fx_prev_wild_y != (int)p_ptr->wilderness_y);

    if (!fc_fx_prev_valid || floor_moved) {
        /* 初回か階またぎ。差分は取れない（取ったら嘘になる）ので基準だけ更新。 */
        fc_fx_rebase();
        fc_fx_kill_sounds = 0;
        return;
    }

    /* ---- 被弾（`p_ptr->chp` の減り） ---- */
    if (((int)p_ptr->chp < fc_fx_prev_chp) && (p_ptr->mhp > 0)) {
        fc_fx_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.kind = FC_FX_KIND_HIT_PLAYER;
        ev.element = 0; /* 何で減ったかは分からない（毒・罠・地形も混ざる）。物理の白 */
        ev.y = (short)py;
        ev.x = (short)px;
        ev.src_y = (short)py;
        ev.src_x = (short)px;
        ev.num = (short)(fc_fx_prev_chp - (int)p_ptr->chp);
        ev.den = (short)p_ptr->mhp;
        fc_fx_push(&ev);
    }

    /* ---- 命中ととどめ（`m_list[]` の HP の減りとスロット消滅） ---- */
    n = (int)m_max;
    if (n > FC_FX_MON_PREV_MAX) {
        n = FC_FX_MON_PREV_MAX;
    }
    for (i = 1; i < n; i++) {
        monster_type *m_ptr = &m_list[i];
        fc_fx_mon_prev *prev = &fc_fx_mon_prev_tab[i];

        if (prev->r_idx && m_ptr->r_idx == prev->r_idx) {
            if ((m_ptr->hp < prev->hp) && (m_ptr->ml || prev->ml) && (prev->maxhp > 0)) {
                fc_fx_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.kind = FC_FX_KIND_HIT_MONSTER;
                ev.element = 0;
                ev.y = (short)m_ptr->fy;
                ev.x = (short)m_ptr->fx;
                ev.src_y = ev.y;
                ev.src_x = ev.x;
                ev.num = (short)(prev->hp - m_ptr->hp);
                ev.den = prev->maxhp;
                fc_fx_push(&ev);
            }
        } else if (prev->r_idx && prev->ml && (fc_fx_kill_sounds > 0)) {
            /*
             * 見えていた敵のスロットが消えた（または別の敵になった）。
             * **`sound(SOUND_KILL)` を聞いた数の中でだけ**とどめと読む——
             * テレポートや解放でも消えるので、消滅だけでとどめにすると嘘になる。
             */
            fc_fx_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.kind = FC_FX_KIND_HIT_MONSTER;
            ev.element = 0;
            ev.y = (short)prev->fy;
            ev.x = (short)prev->fx;
            ev.src_y = ev.y;
            ev.src_x = ev.x;
            ev.num = 1; /* とどめは満額（overkill の量は分からない） */
            ev.den = 1;
            fc_fx_push(&ev);
            fc_fx_kill_sounds--;
        }
    }

    fc_fx_rebase();
    /* とどめの音は年齢で失効（節註）。使い切りにすると音と消滅が別 capture に
     * 割れたとき（fresh ごとに frame が出る）とどめの絵が落ちる。 */
    if ((game_turn - fc_fx_kill_heard_turn) > FC_FX_KILL_SOUND_AGE) {
        fc_fx_kill_sounds = 0;
    }
}

/* ================================================================== セーブの口 */

const char *fc_save_dir(void)
{
    return ANGBAND_DIR_SAVE ? ANGBAND_DIR_SAVE : "";
}

int fc_set_savefile(const char *slot)
{
    if (!slot || !slot[0]) {
        return FC_ERR_ARG;
    }
    if (!ANGBAND_DIR_SAVE || !ANGBAND_DIR_SAVE[0]) {
        return FC_ERR_STATE; /* init_file_paths() より前 */
    }
    if (strlen(slot) >= sizeof(savefile_base)) {
        return FC_ERR_ARG;
    }

    /* `process_player_name()` が両方を見る（片方だけだと sf が真に倒れて訊かれる）。 */
    strcpy(savefile_base, slot);
    path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE, slot);
    return FC_OK;
}

void fc_clear_savefile(void)
{
    savefile[0] = '\0';
    savefile_base[0] = '\0';
}

const char *fc_savefile_base(void)
{
    return savefile_base;
}

const char *fc_savefile_path(void)
{
    return savefile;
}

/* ========================================================== 持ち物・装備・矢筒 */

/*!
 * @brief 1 個ぶんの行を組む。
 * @details **ここが設計 §3.4（決めたこと F3）の書き直しである。**
 *
 * 幻想蛮怒は `inventory[]` という 1 本の配列を添字で切って持ち物と装備に
 * 分けていたが、Frox はその方式を捨てて 3 つの器に分けた:
 *
 * | 器 | 反復 | 枠の名 |
 * |---|---|---|
 * | 持ち物 | `pack_obj(1 .. pack_max())`（`pack.h`） | `slot_label()` の英字 |
 * | 装備 | `equip_obj(1 .. equip_max())`（`equip.h`） | `equip_describe_slot()` |
 * | 矢筒 | `quiver_obj(1 .. quiver_max())`（`quiver.h`） | `slot_label()` の英字 |
 *
 * **枠は空でもよい**（`inv_obj()` は空き枠に NULL を返す）ので、
 * 「埋まっている枠だけ数える」形は変えない——画面側の目録は詰めた並びを期待する。
 *
 * 1 個の説明文は `object_desc()`（`flavor.c`）で、署名は幻想蛮怒と同じ。
 */
int fc_read_item_line(int what, int index, char *out, int cap)
{
    int slot;
    int last;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return FC_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated) {
        return 0;
    }

    if (what == FC_ITEMS_EQUIPMENT) {
        last = equip_max();
    } else if (what == FC_ITEMS_QUIVER) {
        last = quiver_max();
    } else {
        last = pack_max();
    }

    for (slot = 1; slot <= last; slot++) {
        obj_ptr o_ptr;
        char name[MAX_NLEN];
        char line[MAX_NLEN + 32];

        if (what == FC_ITEMS_EQUIPMENT) {
            o_ptr = equip_obj(slot);
        } else if (what == FC_ITEMS_QUIVER) {
            o_ptr = quiver_obj(slot);
        } else {
            o_ptr = pack_obj(slot);
        }

        if (!o_ptr) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }

        object_desc(name, o_ptr, OD_NAME_ONLY | OD_OMIT_PREFIX);
        if (what == FC_ITEMS_EQUIPMENT) {
            /*
             * 枠の名は**コアに訊く**（`equip_describe_slot()`）。幻想蛮怒のように
             * 2 文字の表を当方で持つと、Frox の職・種族で枠の並びが変わったときに
             * 黙ってずれる（あちらの枠は種族で増減する）。
             */
            cptr desc = equip_describe_slot(slot);
            sprintf(line, "%.12s %s", (desc && desc[0]) ? desc : "??", name);
        } else {
            sprintf(line, "%c) %s", slot_label(slot), name);
        }
        fc_copy_str(out, cap, line);
        return (int)strlen(out);
    }
    return 0;
}

int fc_read_visible_monster(int index, char *out, int cap)
{
    int i;
    int seen = 0;

    if (!out || (cap <= 1)) {
        return FC_ERR_ARG;
    }
    out[0] = 0;
    if (!character_generated || !character_dungeon) {
        return 0;
    }

    for (i = 1; i < m_max; i++) {
        monster_type *m_ptr = &m_list[i];
        char name[MAX_NLEN];

        if (!m_ptr->r_idx) {
            continue;
        }
        /* **視認しているものだけ。**見えていない敵を名前で漏らさない。 */
        if (!m_ptr->ml) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }
        monster_desc(name, m_ptr, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
        fc_copy_str(out, cap, name);
        return (int)strlen(out);
    }
    return 0;
}

/* ================================================== コマンドの表（v1 §8） */

/*
 * **`menu_info` / `special_menu_info` の型は `externs.h` に出ていない**
 * （`util.c:3339` と `:3480` の中だけで typedef している）。配列そのものは
 * static ではないのでリンクはできる。そこで**同じ並びの型をここに写して** extern する。
 * 幻想蛮怒の `gb_shim.c` と同じ手である——Frox も変愚の直系なので表の形が同じ。
 *
 * 写しなので、先方が構造体を変えたら黙って別物を読む。それを防ぐために
 * `fc_menu_table_sane()` で**表の実際の値**を確かめ、合わなければ 0 件を返す
 * （呼び手の `fc_pad_commands.cpp` が手書きの最小表へ落ちる）。
 * **`frox/src/` は 1 バイトも触らない**（設計 §1 制約 1）。
 * **上流は現役**なので、取り込み直しのたびにこの sane 検査が効く。
 */
typedef struct fc_menu_naiyou {
    cptr name;
    byte cmd;
    bool fin;
} fc_menu_naiyou;

typedef struct fc_special_menu_naiyou {
    cptr name;
    byte window;
    byte number;
    byte jouken;
    byte jouken_naiyou;
} fc_special_menu_naiyou;

extern fc_menu_naiyou menu_info[10][10];
extern fc_special_menu_naiyou special_menu_info[];

#define FC_MENU_CLASS 1 /* util.c:3489 の MENU_CLASS */
#define FC_MENU_WILD 2 /* 同 MENU_WILD */

/*!
 * @brief 型の写しが合っているかを、表の**実際の値**で確かめる。
 * @details 大きさが同じでも並びが違えば読み間違える。だから値で見る。
 * ここに挙げた 5 つは `util.c:3347` の表の頭と尻で、どれも意味のある値である。
 */
static int fc_menu_table_sane(void)
{
    if (menu_info[0][0].fin) return 0; /* 分類は fin == FALSE */
    if (menu_info[0][0].cmd != 1) return 0; /* "Magic/Special" → 小メニュー 1 */
    if (!menu_info[1][0].fin) return 0; /* 小メニューの項目は fin == TRUE */
    if (menu_info[1][0].cmd != (byte)0x6D) return 0; /* "Use(m)" の m */
    if (!menu_info[0][9].name || menu_info[0][9].name[0]) return 0; /* 尻は空文字列 */
    return 1;
}

/*!
 * @brief コマンド → その配列で押すべきキー（逆引き）。
 * @details `keymap_act[mode][cmd]` の逆引き。幻想蛮怒の同名関数の写し。
 * @return 押すキー。**0 は「そのコマンドは出せない」**
 */
static unsigned char fc_key_for_command(int mode, unsigned char command)
{
    int i;

    if (!command) {
        return 0;
    }
    for (i = 0; i < 256; i++) {
        cptr act = keymap_act[mode][i];
        if (!act) continue;
        if ((act[0] == (char)command) && !act[1]) {
            return (unsigned char)i;
        }
    }
    /* 逆引き失敗。cmd 自身がキーマップ済みなら押しても別物になるので出さない。 */
    if (keymap_act[mode][command]) {
        return 0;
    }
    return command;
}

/*!
 * @brief `special_menu_info` による名札の差し替え（`util.c:3544-3558` の写し）。
 * @details 画面に出る名前とパッドの名札を食い違わせないために、判定を丸ごと写す。
 * 幻想蛮怒版との違いは MENU_WILD の条件が `py_on_surface()` になったことだけ。
 */
static cptr fc_menu_name(int window, int number, cptr fallback)
{
    int i;

    for (i = 0;; i++) {
        if (!special_menu_info[i].name || !special_menu_info[i].name[0]) break;
        if ((int)special_menu_info[i].window != window) continue;
        if ((int)special_menu_info[i].number != number) continue;
        switch (special_menu_info[i].jouken) {
        case FC_MENU_CLASS:
            if (p_ptr->pclass == special_menu_info[i].jouken_naiyou) {
                fallback = special_menu_info[i].name;
            }
            break;
        case FC_MENU_WILD:
            if (py_on_surface()) {
                if ((byte)p_ptr->wild_mode == special_menu_info[i].jouken_naiyou) {
                    fallback = special_menu_info[i].name;
                }
            }
            break;
        default:
            break;
        }
    }
    return fallback;
}

int fc_read_pad_commands(fc_pad_command *out, int cap)
{
    int n = 0;
    int g;

    if (!out || (cap <= 0)) {
        return FC_ERR_ARG;
    }
    if (!fc_menu_table_sane()) {
        return 0;
    }

    /* menu_info[0] は分類（fin == FALSE で cmd が小メニュー番号）。 */
    for (g = 0; g < 10; g++) {
        const fc_menu_naiyou *group = &menu_info[0][g];
        cptr group_name;
        int sub;
        int k;

        if (group->fin) continue;
        if (!group->name || !group->name[0]) continue;
        sub = (int)group->cmd;
        if ((sub <= 0) || (sub >= 10)) continue;
        group_name = fc_menu_name(0, g, group->name);

        for (k = 0; k < 10; k++) {
            const fc_menu_naiyou *item = &menu_info[sub][k];

            /* fin == TRUE の項目だけがコマンド（FALSE は小メニューへの分岐）。 */
            if (!item->fin) continue;
            if (!item->name || !item->name[0] || !item->cmd) continue;
            if (n >= cap) return n;

            memset(&out[n], 0, sizeof(out[n]));
            out[n].command = item->cmd;
            out[n].key_normal = fc_key_for_command(KEYMAP_MODE_ORIG, item->cmd);
            out[n].key_rogue = fc_key_for_command(KEYMAP_MODE_ROGUE, item->cmd);
            fc_copy_str(out[n].group, (int)sizeof(out[n].group), group_name);
            fc_copy_str(out[n].label, (int)sizeof(out[n].label), fc_menu_name(sub, k, item->name));
            n++;
        }
    }
    return n;
}

unsigned char fc_pad_key_for_command(int rogue, int command)
{
    if ((command <= 0) || (command > 255)) {
        return 0;
    }
    return fc_key_for_command(rogue ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG, (unsigned char)command);
}

int fc_pad_table_stamp(void)
{
    int s = (int)p_ptr->pclass;

    s = s * 31 + (p_ptr->wild_mode ? 1 : 0);
    s = s * 31 + (dun_level ? 1 : 0);
    s = s * 31 + (p_ptr->inside_arena ? 1 : 0);
    s = s * 31 + (py_on_surface() ? 1 : 0);
    s = s * 31 + (rogue_like_commands ? 1 : 0);
    s = s * 31 + (character_generated ? 1 : 0);
    return s;
}

void fc_apply_cursor_mode(int enable)
{
    if (!enable) {
        return; /* 従来どおり（文字キー主体）。use_menu はコアに任せる */
    }
    if (!character_generated) {
        return; /* タイトル・誕生画面はコアの別メニュー。触らない */
    }
    command_menu = TRUE; /* Enter ＝ コマンドメニュー */
    use_menu = TRUE; /* 以降の選択はコアのカーソルで */
}

/* ============================================================ 地形の全数照合（P7） */

int fc_terrain_defined(int feat)
{
    if ((feat < 0) || (feat >= (int)max_f_idx)) {
        return -1;
    }
    if (!f_info || !f_name) {
        return 0;
    }
    /* 定義の無い枠は名前のオフセットが 0（f_name の頭は空文字列）。 */
    return (f_info[feat].name && f_name[f_info[feat].name]) ? 1 : 0;
}

int fc_monster_defined(int r_idx)
{
    if ((r_idx < 0) || (r_idx >= (int)max_r_idx)) {
        return -1;
    }
    if (!r_info || !r_name) {
        return 0;
    }
    /* 定義の無い枠は名前のオフセットが 0（`r_name` の頭は空文字列）。地形と同じ。 */
    return (r_info[r_idx].name && r_name[r_info[r_idx].name]) ? 1 : 0;
}

int fc_object_defined(int k_idx)
{
    if ((k_idx < 0) || (k_idx >= (int)max_k_idx)) {
        return -1;
    }
    if (!k_info || !k_name) {
        return 0;
    }
    return (k_info[k_idx].name && k_name[k_info[k_idx].name]) ? 1 : 0;
}

/* ==================================================================== 色の申告 */

int fc_read_palette(unsigned char *rgb, int cap)
{
    int i;

    if (!rgb || (cap < 16 * 3)) {
        return FC_ERR_ARG;
    }

    /*
     * `angband_color_table[i]` は 4 バイト（[0] は使わず、[1..3] が RGB）。
     * **16 色だけ**を送る（設計 §4.1。Frox の色は `TERM_DARK`〜`TERM_L_UMBER`）。
     */
    for (i = 0; i < 16; i++) {
        rgb[(i * 3) + 0] = angband_color_table[i][1];
        rgb[(i * 3) + 1] = angband_color_table[i][2];
        rgb[(i * 3) + 2] = angband_color_table[i][3];
    }
    return 16 * 3;
}
