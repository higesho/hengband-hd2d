#pragma once

/*
 * NetHack のコアの、C と C++ の境目。
 *
 * - 原作のヘッダ（hack.h）を読めるのは C の nh_winport.c だけ。ここへは原作の型を出さず、
 *   素の値（座標・字・色・番号・文字列）だけで話す。
 * - C++ の側（nh_main.cpp・nh_frame.cpp）がプロトコル v1 の frame を組んで送る。
 * - 画面の関数（C）から呼ばれる関数は、キーが要るところで frame を送ってから待つ。
 *   待っている間に UI が居なくなった・終了を求められたら、原作の hangup（保存して終わる道）へ入る。
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 原作の地図の大きさ（COLNO・ROWNO。nh_winport.c が照合する）。列 0 は使われない */
enum {
    NH_MAP_COLS = 80,
    NH_MAP_ROWS = 21
};

/* nh_host_getkey の種類（原作の program_state.input_state による） */
enum {
    NH_KEY_OTHER = 0,   /* 問い・位置の選択・方向などの途中 */
    NH_KEY_COMMAND = 1, /* 次の命令を待っている（commandInp） */
    NH_KEY_POS = 2,     /* 位置の選択（nh_poskey）。nethack-ui では wait reason=pos。クリックも返る */
    NH_KEY_MORE = 3     /* --More-- の応答（nethack-ui の内部用。C の側は使わない） */
};

/* ゲームの段階。名前の問いとキャラクタ作成の間だけ PREGAME */
enum {
    NH_PHASE_PREGAME = 0,
    NH_PHASE_GAME = 1
};

/* メニューの選び方。原作の PICK_NONE / PICK_ONE / PICK_ANY と同じ値 */
enum {
    NH_PICK_NONE = 0,
    NH_PICK_ONE = 1,
    NH_PICK_ANY = 2
};

/* 地図のマスの glyph の分類（原作の glyph_is_* による。番号は分類の中の番号） */
enum {
    NH_GLYPH_NOTHING = 0,    /* 何も無い（glyph_is_nothing） */
    NH_GLYPH_UNEXPLORED = 1, /* 未探索 */
    NH_GLYPH_CMAP = 2,       /* 地形・設備。番号は S_* （glyph_to_cmap） */
    NH_GLYPH_MONSTER = 3,    /* 怪物。番号は怪物の番号（glyph_to_mon） */
    NH_GLYPH_PET = 4,        /* ペット。同上 */
    NH_GLYPH_OBJECT = 5,     /* アイテム。番号はアイテムの番号（glyph_to_obj） */
    NH_GLYPH_TRAP = 6,       /* 罠。番号は罠の種類（glyph_to_trap） */
    NH_GLYPH_WARNING = 7,    /* 警告の数字 */
    NH_GLYPH_INVISIBLE = 8,  /* 見えない怪物の記憶 */
    NH_GLYPH_SWALLOW = 9,    /* 飲み込まれている */
    NH_GLYPH_EXPLOSION = 10, /* 爆発 */
    NH_GLYPH_ZAP = 11,       /* 魔法の筋 */
    NH_GLYPH_OTHER = 12
};

/* ステータスの項目の番号（原作 include/botl.h の enum statusfields と同じ。nh_winport.c が照合する） */
enum {
    NH_BL_TITLE = 0,
    NH_BL_STR = 1,
    NH_BL_DX = 2,
    NH_BL_CO = 3,
    NH_BL_IN = 4,
    NH_BL_WI = 5,
    NH_BL_CH = 6,
    NH_BL_ALIGN = 7,
    NH_BL_SCORE = 8,
    NH_BL_CAP = 9,
    NH_BL_GOLD = 10,
    NH_BL_ENE = 11,
    NH_BL_ENEMAX = 12,
    NH_BL_XP = 13,
    NH_BL_AC = 14,
    NH_BL_HD = 15,
    NH_BL_TIME = 16,
    NH_BL_HUNGER = 17,
    NH_BL_HP = 18,
    NH_BL_HPMAX = 19,
    NH_BL_LEVELDESC = 20,
    NH_BL_EXP = 21,
    NH_BL_CONDITION = 22,
    NH_BL_MAX = 27
};

/* メニュー 1 行 */
typedef struct nh_menu_item {
    int selectable;  /* 0: 見出し・説明の行（選べない） */
    char selector;   /* 選ぶ字（0 なら、ホストが a〜z A〜Z を振る） */
    char group;      /* グループの字（0 なら無し） */
    int attr;        /* 原作の ATR_* */
    int color;       /* 原作の色。無ければ -1 */
    int preselected; /* はじめから選ばれている */
    const char *text;
    int glyph;       /* add_menu の glyph_info の glyph（絵が無ければ -1）。nethack-ui の menu の glyph 欄 */
} nh_menu_item;

/* ---- C++ の側が実装する（C から呼ぶ） ---- */

/* 地図 */
void nh_host_map_clear(void);
/*
 * 地図の 1 マス（print_glyph）。ch・color はそのマスの一番上に見える物の字と色（原作の ttychar と色の番号）。
 * glyph_class・glyph_index は NH_GLYPH_*。terrain_cmap は、そのマスで描く地形の表示記号の番号
 * （原作の S_*。glyph_to_cmap の値）で、一番上が地形ならその番号、怪物・アイテムなどが載っているときは
 * 原作の地図の記憶（levl[x][y].glyph）から引いた下の地形、分からなければ -1。
 * terrain_variant は、その地形の glyph が持つ変種の番号。壁（S_vwall〜S_trwall）は迷宮の系統
 * （0 通常・1 鉱山・2 ゲヘナ・3 ノックス・4 ソコバン。GLYPH_CMAP_MAIN/MINES/GEH/KNOX/SOKO_OFF の範囲）、祭壇（S_altar）は
 * 属性（altar_types: 0 無属性・1 混沌・2 中立・3 秩序・4 その他。GLYPH_ALTAR_OFF からの差）、それ以外は 0。
 */
void nh_host_map_cell(int x, int y, int ch, int color, int glyph, int glyph_class, int glyph_index,
                      unsigned glyph_flags, int terrain_cmap, int terrain_variant);
void nh_host_map_cursor(int x, int y);

/* メッセージ 1 行（putstr(WIN_MESSAGE)・raw_print） */
void nh_host_message(const char *text);

/* ステータス（status_update）。text は項目の値の字（BL_GOLD は「<記号>:<金額>」のまま） */
void nh_host_status_field(int field, const char *text);
void nh_host_status_conditions(unsigned long mask, const char *names);
void nh_host_status_flush(const char *player_name);

/*
 * メニューを出して選ばせる。how は NH_PICK_*。
 * 選ばれた項目の番号（items の添字）を chosen に、個数の指定（無ければ -1）を counts に入れ、選ばれた数を返す。
 * 取り消し（ESC）は -1、何も選ばずに終えたら 0。chosen と counts は count 個ぶんの領域を持つこと。
 * how が NH_PICK_NONE なら表示だけ（キーを 1 つ待つ）。
 */
int nh_host_menu(int how, const char *prompt, const nh_menu_item *items, int count, int *chosen,
                 long *counts);

/* テキストの窓（行の並びを出して、キーを 1 つ待つ） */
void nh_host_text(const char *const *lines, int count);

/*
 * yn_function。choices は選べる字（NULL なら何でも）、def は既定の字。
 * 返す字は原作の tty と同じ規則（ESC は q か n か既定、空白・改行は既定）で決める。
 */
char nh_host_yn(const char *query, const char *choices, char def);

/* getlin。1: 入力した（buf に入る）、0: 取り消し（ESC） */
int nh_host_getlin(const char *query, char *buf, int bufsize);

/* キーを 1 つ待つ（0〜255）。UI が居なくなった・終了を求められたら原作の hangup へ入り ESC(27) を返す */
int nh_host_getkey(int mode);

/* ゲームの段階（NH_PHASE_*）。frame の pre_game_menu に出る */
void nh_host_set_phase(int phase);

/* 終了の知らせ（exit_nhwindows の字）。最後の frame を送る */
void nh_host_exit_notice(const char *text);

/* 過去のメッセージを出す（^P） */
void nh_host_show_message_history(void);

/* UI の言語が日本語か（ui_state が届くまでは、組んだ言語） */
int nh_host_lang_is_ja(void);

/*
 * ---- nethack-ui（握手で画面と申告し合ったときだけ働く） ----
 *
 * 申告が無いとき nh_host_ui_enabled() は 0 を返し、下の nh_host_ui_* は何もしない（C の側は今までどおりの関数を呼ぶ）。
 * 有効なとき、窓の字（メッセージ・メニュー・問い）は frame ではなく nethack_ui の知らせで画面へ送り、
 * 答えは nethack_reply で受ける。
 */

/* 窓の種類（原作の NHW_*）と、nh_host_ui_win の操作 */
enum {
    NH_UI_WIN_CREATE = 0,
    NH_UI_WIN_CLEAR = 1,
    NH_UI_WIN_DISPLAY = 2,
    NH_UI_WIN_DESTROY = 3
};

/* 文字入力の種類（nh_host_ask_text） */
enum {
    NH_ASK_GETLIN = 0,
    NH_ASK_NAME = 1,
    NH_ASK_EXT_CMD = 2
};

/* キャラクタ作成の表（player_selection の表。nh_winport.c が roles[]・races[]・ok_* から作る） */
enum {
    NH_SEL_MAX_ROLES = 16,
    NH_SEL_MAX_RACES = 8,
    NH_SEL_MAX_GENDERS = 3,
    NH_SEL_MAX_ALIGNS = 4,
    NH_SEL_MAX_COMBOS = 1024
};
typedef struct nh_select_table {
    int nroles;
    const char *role_name[NH_SEL_MAX_ROLES];   /* 男の名前（または共通の名前） */
    const char *role_name_f[NH_SEL_MAX_ROLES]; /* 女の名前。無ければ NULL */
    int nraces;
    const char *race_name[NH_SEL_MAX_RACES];   /* noun（human・elf…） */
    const char *race_adj[NH_SEL_MAX_RACES];    /* adj（human・elven…） */
    int ngenders;
    const char *gender_name[NH_SEL_MAX_GENDERS];
    int naligns;
    const char *align_name[NH_SEL_MAX_ALIGNS];
    /* 有効な組み合わせ（ok_role・ok_race・ok_gend・ok_align がすべて真）。{役割, 種族, 性別, 属性} の番号 */
    int ncombos;
    unsigned char combos[NH_SEL_MAX_COMBOS][4];
    int pick[4];       /* 初期の選び（役割, 種族, 性別, 属性）。決まっていなければ -1 */
    const char *name;  /* 今の名前 */
} nh_select_table;

int nh_host_ui_enabled(void);

/* create_nhwindow・clear_nhwindow・display_nhwindow・destroy_nhwindow。type は原作の NHW_*（rip は 100） */
void nh_host_ui_win(int op, int id, int type, int blocking);
/* 地図の窓やメッセージの窓の blocking な display_nhwindow が待つキー（任意の 1 キー。wait reason=key） */
void nh_host_ui_wait_any_key(void);
/* putstr(WIN_MESSAGE)。--More-- の規則はここ（msg_lines は原作の iflags.msg_history）。raw_print は id が -1 */
void nh_host_ui_putstr(int id, int attr, const char *text, int msg_lines);
void nh_host_ui_curs(int id, int x, int y);
void nh_host_ui_cliparound(int x, int y);
/* status_update の 1 項目（pct は割合。無いとき -1）。text は項目の値の字 */
void nh_host_ui_status(int field, const char *text, int color, int attr, int pct);
/* 状態異常。names・colors・attrs は n 個 */
void nh_host_ui_status_cond(unsigned long mask, int n, const char *const *names, const int *colors, const int *attrs);
void nh_host_ui_status_flush(int is_reset);
void nh_host_ui_bell(void);
void nh_host_ui_pref(const char *name, const char *value);

/* 窓の番号つきのメニュー（nh_host_menu と同じ。ui が有効なら menu の知らせで問う） */
int nh_host_menu_win(int win_id, int win_type, int how, const char *prompt, const nh_menu_item *items, int count,
                     int *chosen, long *counts);
/* テキストの窓（NHW_TEXT の display_nhwindow・display_file）。閉じられるまで戻らない */
void nh_host_text_win(int win_id, const nh_menu_item *lines, int count);
/* 墓標。lines は墓標の後に putstr された点数の行など */
void nh_host_rip(int win_id, const char *name, long gold, const char *killer, int year, const nh_menu_item *lines,
                 int count);
/*
 * 文字入力（kind は NH_ASK_*）。1: 入力した（buf に入る）、0: 取り消し。
 * cmds は拡張コマンドの名前の一覧（kind が NH_ASK_EXT_CMD のとき。それ以外は NULL）。
 */
int nh_host_ask_text(int kind, const char *query, char *buf, int bufsize, const char *const *cmds, int ncmds);
/* キャラクタ作成。1: 選んだ（result に役割・種族・性別・属性の番号。おまかせは -1）、0: やめる */
int nh_host_select(const nh_select_table *table, int result[4]);
/*
 * nh_poskey（命令・方向・位置の選択がみな通る）。mode は NH_KEY_*（nh_host_getkey と同じ）。
 * キーなら 1〜255 を返し、地図のクリック（nethack-ui の pos の返事。mode が NH_KEY_POS のときだけ）なら 0 を返して
 * *x *y *mod（1 左・2 右）を入れる。
 */
int nh_host_getkey_pos(int mode, int *x, int *y, int *mod);

/* ---- C の側（nh_winport.c）が実装する（C++ から呼ぶ） ---- */

/* 原作の hangup(0) を呼ぶ（保存して終わる道へ入る） */
void nh_request_hangup(void);

/*
 * いまの地図の見え方を写す。ゲームが始まっていれば 1、まだなら 0 を返す。
 * visible は NH_MAP_COLS * NH_MAP_ROWS 個（添字は y * NH_MAP_COLS + x）。いま見えているマス（原作の cansee）が 1、
 * 見えていないマスが 0。hero_x / hero_y は主人公の位置（原作の u.ux / u.uy）。
 */
int nh_snapshot_view(unsigned char *visible, int *hero_x, int *hero_y);

/*
 * 地図のマス (x, y) の説明（原作の autodescribe〔getpos の自動説明〕と同じ文。do_screen_description の firstmatch）を buf に入れる。
 * 入れられたら 1、マスが範囲外・ゲームがまだ始まっていないなら 0。状態を変えない。ゲームのスレッドの入力待ちの中で呼ぶこと。
 */
int nh_describe_cell(int x, int y, char *buf, int bufsize);

/*
 * コマンドの欄（nethack-ui の cmdbar。タッチで打つためのボタンの並び）の絵の glyph。id は nh_main.cpp の欄の表の番号。
 * 物の絵（objnum_to_glyph）・地図の記号（cmap_to_glyph）・祭壇（altar_to_glyph）。絵の無い番号は -1（画面はキーの字を描く）。
 */
int nh_cmdbar_glyph(int id);

/* ゲームを始めて、終わるまで戻らない。datadir は読むデータ、savedir はセーブ・ロック・スコアの置き場 */
int nh_game_main(const char *datadir, const char *savedir);

#ifdef __cplusplus
}
#endif
