/*!
 * @file sdl_sub_window_terms.cpp
 * @brief サブパネル用の画素なし Term 群と、選べる種類の一覧（K-26）
 *
 * コアの `window_stuff()` は `angband_terms[i]` のうち `g_window_flags[i]` に
 * 立っている種類だけを `fix_*` で描く（`src/core/window-redrawer.cpp:246`）。
 * ここでは画素を出さない Term を `kSubPanelCount` 枚置き、そのフラグを利用者の選択で
 * 入れ替え、
 * コアが描いた文字グリッドを読み取って `GameFrame::sub_panels` に積む。
 * **コアは 1 行も変えない**（必守制約 1）。
 */
#include "term/sdl_sub_window_terms.h"

#include "frame/sdl_ui_options.h"
#include "frame/sub_panel_kinds.h"

#include "game-option/option-flags.h"
#include "io/files-util.h"
#include "locale/character-encoding.h"
#include "locale/language-switcher.h"
#include "system/redrawing-flags-updater.h"
#include "term/gameterm.h"
#include "term/term-color-types.h"
#include "term/z-term.h"
#include "util/enum-converter.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace presentation {

namespace {

// z-term.cpp の AF_KANJI / AF_TILE フラグ（scr->a）。presentation_bridge.cpp と同じ値。
constexpr uint8_t kAfKanji1 = 0x10;
constexpr uint8_t kAfKanji2 = 0x20;
constexpr uint8_t kAfKanjic = 0x0F;
constexpr uint8_t kAfTile1 = 0x80;
constexpr uint8_t kAfBigtile2 = 0xf0;

//! Term を置く先。`angband_terms[0]` は本線（`SdlNullTerm`）なので 1 から使う。
constexpr int kFirstSubTermIndex = 1;
static_assert(kFirstSubTermIndex + kSubPanelCount <= MAX_WINDOW_ENTITIES,
    "サブパネル用 Term が angband_terms に収まらない");

/*!
 * @name Term サイズの安全域
 * @details パネルの実寸が届く前（起動直後）と、極端な値が届いたときの丸め。
 * 下限をあまり小さくするとコアの表示関数が何も出せない（`display_inventory` は
 * 桁数から表示幅を決める）ので、実用に足る最小を置く。
 * @{
 */
constexpr int kMinCols = 20;
constexpr int kMaxCols = 255;
constexpr int kMinRows = 3;
constexpr int kMaxRows = 66;
constexpr int kDefaultCols = 40;
constexpr int kDefaultRows = 10;
/*! @} */

//! グラフィックタイル属性のセル（コア地図の流し込み）は文字として読まない。
bool is_graphic_tile_attr(uint8_t attr)
{
    if ((attr & kAfTile1) != 0) {
        return true;
    }
    return (attr & kAfBigtile2) == kAfBigtile2;
}

char sanitize_term_char(char c)
{
    if (c == '\0') {
        return ' ';
    }
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
        return ' ';
    }
    return c;
}

/*!
 * @brief セル 1 個ぶんの文字を行へ足す。
 * @details セルは**文字を丸ごと**持つ（`term/term-char.h`）。全角の右半分は空なので何も足さない。
 */
void append_term_char(std::string &line, const TermChar &cell)
{
    const auto sv = cell.view();
    if (sv.empty()) {
        return;
    }

    if (sv.size() == 1) {
        line.push_back(sanitize_term_char(sv[0]));
        return;
    }

    line.append(sv);
}

//! セルが空白（または空）か。
bool term_char_is_blank(const TermChar &cell)
{
    const auto sv = cell.view();
    return sv.empty() || ((sv.size() == 1) && (sanitize_term_char(sv[0]) == ' '));
}

struct SubTerm {
    term_type term{};
    bool installed{ false };
    int cols{ kDefaultCols };
    int rows{ kDefaultRows };
    //! ui から届いた希望サイズ。次の `fill_sub_panels` で `term_resize` する。
    int want_cols{ kDefaultCols };
    int want_rows{ kDefaultRows };
};

SubTerm g_sub_terms[kSubPanelCount];

void hook_init(term_type *) {}
void hook_nuke(term_type *) {}

errr hook_text(TERM_LEN, TERM_LEN, int, TERM_COLOR, std::string_view)
{
    return 0; // 画素を出さない（バッファはコアが保持する。読むのは Bridge）
}

errr hook_wipe(TERM_LEN, TERM_LEN, int)
{
    return 0;
}

errr hook_curs(TERM_LEN, TERM_LEN)
{
    return 0;
}

errr hook_pict(TERM_LEN, TERM_LEN, int, const TERM_COLOR *, const TermChar *, const TERM_COLOR *, const TermChar *)
{
    return 0;
}

/*!
 * @brief サブパネル Term の xtra フック。**seam は一切叩かない**。
 * @details `display_sub_windows()` は描いた後に `term_fresh()` を呼ぶ。ここで本線と同じ
 * `on_fresh`（capture→present）へ落ちると、capture の中から capture を呼ぶ再入になる。
 * サブパネル Term は「コアの描画結果を溜めておく箱」でしかないので全て no-op で返す。
 */
errr hook_xtra(int n, int)
{
    switch (n) {
    case TERM_XTRA_FRESH:
    case TERM_XTRA_CLEAR:
    case TERM_XTRA_FLUSH:
    case TERM_XTRA_SHAPE:
    case TERM_XTRA_BORED:
    case TERM_XTRA_REACT:
    case TERM_XTRA_ALIVE:
    case TERM_XTRA_LEVEL:
    case TERM_XTRA_DELAY:
        return 0;
    case TERM_XTRA_EVENT:
        return 1; // 入力は本線 Term だけが持つ（ここには来ないが、来ても何も返さない）
    default:
        return 1;
    }
}

int clamp_int(int v, int lo, int hi)
{
    return (std::max)(lo, (std::min)(v, hi));
}

/*!
 * @name キャラクターごとのパネル構成（セーブデータの隣に持つ）
 * @details 人間の指示「セーブデータにパネル構成を保存するように」。
 * **コアのセーブ形式は触れない**（必守制約 1。読み書きの版まで変わる）ので、
 * セーブファイルと同じ場所に `<セーブ名>.sdl2panels` を置く。
 * - セーブのパスが決まった時点で読み込み、**あればそれが構成の持ち主**になる。
 * - 無いとき（このセーブを初めて開いた／古いセーブ）は `sdl2_ui_options.cfg` の値
 *   （＝新規キャラクターの初期値・最後に選んだ構成）をそのまま使う。
 * - 値が変わったら書き出す。機能メニューで変えた分もここを通って保存される。
 * @note `list_player_savefiles()`（bootstrap）は名前に `.` を含むファイルを除くので、
 * この横置きファイルはロード一覧に出てこない。
 * @{
 */
constexpr const char *kSidecarSuffix = ".sdl2panels";

//! 直近に読んだ／書いたセーブのパス。空ならまだセーブが決まっていない。
std::string g_sidecar_path;
//! そのファイルに入っている値。これと**実効値**が食い違ったら書き出す。
int g_sidecar_kinds[kSubPanelCount] = { -1, -1, -1, -1, -1, -1, -1 };
bool g_sidecar_valid = false;
//! いま読んだばかりのファイルの中身を、画面へ渡すまで抱えるための旗（`fill_sub_panels` が下ろす）。
bool g_sidecar_just_loaded = false;

std::string current_sidecar_path()
{
    if (savefile.empty()) {
        return {};
    }
    auto path = savefile;
    path += kSidecarSuffix;
    return path.string();
}

//! 横置きファイルを読む。読めたら opts へ写して true。
bool load_sidecar(const std::string &path, SdlUiOptions &opts)
{
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    int read[kSubPanelCount];
    for (int i = 0; i < kSubPanelCount; ++i) {
        read[i] = -1;
    }
    bool seen[kSubPanelCount] = {};
    bool any = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        // sub_panel1_kind 〜 sub_panel7_kind。cfg と同じキー名にしてある。
        if (key.size() != 15 || key.compare(0, 9, "sub_panel") != 0 ||
            key.compare(10, 5, "_kind") != 0) {
            continue;
        }
        const int panel = key[9] - '1';
        if (panel < 0 || panel >= kSubPanelCount) {
            continue;
        }
        read[panel] = (value < -1 || value > 15) ? -1 : value;
        seen[panel] = true;
        any = true;
    }
    if (!any) {
        return false;
    }

    /*
     * **5 個しか無い古いファイルは並べ替える**（2026-08-19 に枠が 5 → 7 になった）。
     * 昔の 4・5 個目は右列で、いまの右列は添字 4 から始まる（`ui_layout.h` の `kSubRightSlot`）。
     * そのまま入れると右上のステータスが下段の 4 枚目へ移り、キャラクターを読んだ瞬間に
     * 「中身が入れ替わっていた」になる。**画面側の `hd2d.cfg` と同じ直しである。**
     */
    bool legacy = true;
    for (int i = 0; i < 5; ++i) {
        legacy = legacy && seen[i];
    }
    for (int i = 5; i < kSubPanelCount; ++i) {
        legacy = legacy && !seen[i];
    }
    if (legacy) {
        static constexpr int kLegacySlot[5] = { 0, 1, 2, 4, 5 };
        int moved[kSubPanelCount];
        for (int i = 0; i < kSubPanelCount; ++i) {
            moved[i] = -1;
        }
        for (int i = 0; i < 5; ++i) {
            moved[kLegacySlot[i]] = read[i];
        }
        for (int i = 0; i < kSubPanelCount; ++i) {
            read[i] = moved[i];
        }
    }
    for (int i = 0; i < kSubPanelCount; ++i) {
        opts.sub_panel_kind[i] = read[i];
    }
    return true;
}

void save_sidecar(const std::string &path, const SdlUiOptions &opts)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return; // 書けないなら黙って諦める（ゲームは続けられる）
    }
    out << "# Hengband SDL2 UI: サブパネル構成（このセーブ専用）\n";
    for (int i = 0; i < kSubPanelCount; ++i) {
        out << "sub_panel" << (i + 1) << "_kind=" << opts.sub_panel_kind[i] << '\n';
    }
}

/*!
 * @brief セーブが決まった／変わったら、そのキャラクターの構成を読む（**フレームの先頭で**）。
 *
 * @details 直したのは 2026-08-19（「キャラクター別構成も直して」と決めた）。
 * 前はここで `opts` へ写すだけだったが、`apply_ui_state()` が capture のたびに
 * 画面の値で `opts` を塗り直すので、**ファイルの値が効くのは 1 フレームだけ**だった。
 * 次のフレームには画面の値で上書きされ、そのうえ `save_sidecar()` が画面の値で
 * ファイルまで書き換えていた——つまりキャラクター別構成は**画面設定の写しに退化していた**。
 *
 * いまは「読んだ」ことを旗で立て、`fill_sub_panels()` が
 * **画面が同じ値を送り返すまで抱える**（コアの `=` で変えられたときと同じ道）。
 * 画面はそれを `frame.sub_panels[].kind` から取り込み、cfg にも残す。
 */
void load_savefile_config(SdlUiOptions &opts)
{
    const std::string path = current_sidecar_path();
    if (path.empty() || (path == g_sidecar_path)) {
        return; // セーブ未確定（オープニング）か、同じセーブのまま
    }
    g_sidecar_path = path;
    g_sidecar_valid = load_sidecar(path, opts);
    for (int i = 0; i < kSubPanelCount; ++i) {
        g_sidecar_kinds[i] = opts.sub_panel_kind[i];
    }
    if (g_sidecar_valid) {
        g_sidecar_just_loaded = true; //!< 画面へ渡すまで抱える
        return;
    }
    //! 初回。**いまの構成をこのキャラクターのものとして残す**（画面の既定を引き継ぐ）。
    save_sidecar(path, opts);
    g_sidecar_valid = true;
}

/*!
 * @brief この回の**実効値**をキャラクター別構成へ書き戻す（**フレームの終わりで**）。
 * @details 頭で書くと、`apply_ui_state()` が塗った画面の値（抱え込み中は古い値）を
 * 保存してしまう。実効値が決まるのは枚ごとの輪を回した後である。
 */
void save_savefile_config(const SdlUiOptions &opts)
{
    if (!g_sidecar_valid || g_sidecar_path.empty()) {
        return;
    }
    bool changed = false;
    for (int i = 0; i < kSubPanelCount; ++i) {
        if (g_sidecar_kinds[i] != opts.sub_panel_kind[i]) {
            changed = true;
            g_sidecar_kinds[i] = opts.sub_panel_kind[i];
        }
    }
    if (changed) {
        save_sidecar(g_sidecar_path, opts);
    }
}
/*! @} */

//! 一覧に載っていない番号（記念撮影・予約領域・壊れた cfg）は「UI 既定」に落とす。
int sanitize_kind(int kind)
{
    if (kind < 0) {
        return -1;
    }
    for (const auto &entry : sub_panel_kind_entries()) {
        if (entry.flag == kind) {
            return kind;
        }
    }
    return -1;
}

/*!
 * @name コアが変えたフラグを画面へ返す（2026-08-19 に決めた）
 *
 * @details ここが**双方向**になる前は、コアの `=`→`w`（ウィンドウフラグ）で立てたフラグが
 * **次のフレームで黙って戻っていた**（`apply_kind()` の注記）。操作はできるのに結果が
 * 消えるので、遊ぶ人からは壊れて見える。
 *
 * ## 見分けにフラグの差分を使ってはいけない
 * 最初はそう書いて**振動した**（歩くだけで所持品と装備品が入れ替わり続けた）。
 * `g_window_flags` を書くのは利用者だけではない:
 *
 * | 誰が | いつ |
 * | --- | --- |
 * | `toggle_inventory_equipment()` | 床の品物を選ぶたび・店・Ctrl-I。**所持品↔装備品を全窓で入れ替える** |
 * | `option-loader.cpp` | セーブの読み込み。配列を丸ごと書く |
 * | `do_cmd_options_win()` | **`=`→`w`。これだけが利用者の意図** |
 *
 * だから見るのは `g_window_flags_generation`（`src/game-option/option-flags.h`）である。
 * **`=` の画面で実際に変わったときだけ**増える数字で、上の 2 つでは動かない。
 *
 * ## 拾ったあとは画面が追いつくまで抱えておく
 * `apply_ui_state()` は capture のたびに画面の（まだ古い）値で `opts` を塗り直すので、
 * 一度きりで渡すと**次のフレームで元に戻る**（画面まで往復するのに数フレーム掛かる）。
 * @{
 */
constexpr int kNoOverride = -2; //!< -1 は「UI 既定」という**正しい値**なので使えない

//! コア側で変えられた種類。画面が同じ値を送り返してくるまで保つ。
int g_core_override[kSubPanelCount] = { kNoOverride, kNoOverride, kNoOverride, kNoOverride, kNoOverride,
    kNoOverride, kNoOverride };
//! 前に見た `g_window_flags_generation`。**増えていたら `=` の画面で変えられた。**
unsigned int g_seen_flags_generation = 0;

/*!
 * @brief いま `g_window_flags[1+panel]` に立っている種類（1 つも無ければ -1）。
 * @details **2 つ以上立っていたら小さい番号を採る。**コアの `=` 画面は `t` も `s` も
 * `clear()` してから 1 つ立てるので、普通は 1 つしか立たない
 * （`src/cmd-io/cmd-gameoption.cpp`）。それでも決めておく——
 * 決めずに「不定」にすると、同じ操作で違う板が出る形が残る。
 */
int current_kind(int panel)
{
    const auto &flags = g_window_flags[static_cast<size_t>(kFirstSubTermIndex + panel)];
    for (int i = 0; i < static_cast<int>(SubWindowRedrawingFlag::MAX); ++i) {
        if (flags.has(i2enum<SubWindowRedrawingFlag>(i))) {
            return i;
        }
    }
    return -1;
}

/*!
 * @brief コア側で決まった中身を抱え込む。**フレームの頭で 1 回**呼ぶ。
 * @param opts キャラクター別構成を読んだ直後なら、その値が入っている。
 * @details 抱え込む理由は 2 つあり、どちらも「画面まで往復するのに数フレーム掛かる」ため:
 *   - `=`→`w` で利用者が変えた（`g_window_flags_generation` が動いた）
 *   - **キャラクター別構成を読んだ**（`<セーブ名>.sdl2panels`。2026-08-19）
 */
void take_core_side_changes(const SdlUiOptions &opts)
{
    if (g_sidecar_just_loaded) {
        g_sidecar_just_loaded = false;
        for (int i = 0; i < kSubPanelCount; ++i) {
            g_core_override[i] = sanitize_kind(opts.sub_panel_kind[i]);
        }
        //! 読んだ直後のフラグは古いので、`=` の変化として拾い直さない。
        g_seen_flags_generation = g_window_flags_generation;
        return;
    }
    if (g_window_flags_generation == g_seen_flags_generation) {
        return;
    }
    g_seen_flags_generation = g_window_flags_generation;
    for (int i = 0; i < kSubPanelCount; ++i) {
        g_core_override[i] = sanitize_kind(current_kind(i));
    }
}
/*! @} */

/*!
 * @brief `g_window_flags[1+panel]` を望みの種類**だけ**にする。変えたら true。
 * @details キャッシュと比べるのではなく**いまのフラグそのもの**と比べる。コアも
 * 同じ配列を書く経路を持っている（`init_windows`（既定値）・セーブデータの読み込み
 * `src/load/option-loader.cpp:109`・コアのオプション画面 `=`・`toggle_inventory_equipment()`）
 * ため、キャッシュだけを見ると「UI の設定と実際のフラグ」が黙って食い違う。
 * @note 裏を返せば、**この設定がフラグの持ち主**になる。コアの `=` 画面や
 * 持ち物／装備の入れ替えコマンドでフラグを変えても次のフレームで戻る。
 */
bool apply_kind(int panel, int kind)
{
    auto &flags = g_window_flags[static_cast<size_t>(kFirstSubTermIndex + panel)];
    EnumClassFlagGroup<SubWindowRedrawingFlag> want{};
    if (kind >= 0) {
        want.set(i2enum<SubWindowRedrawingFlag>(kind));
    }
    if (flags == want) {
        return false;
    }
    flags = want;
    return true;
}

//! 希望サイズが届いていれば `term_resize` する（本線 Term を奪ったままにしない）。
bool resize_if_needed(int panel)
{
    SubTerm &st = g_sub_terms[panel];
    if (!st.installed) {
        return false;
    }
    if (st.want_cols == st.cols && st.want_rows == st.rows) {
        return false;
    }

    auto *prev = game_term;
    term_activate(&st.term);
    term_resize(st.want_cols, st.want_rows);
    st.cols = st.term.wid;
    st.rows = st.term.hgt;
    term_activate(prev);
    return true;
}

/*!
 * @brief Term の文字グリッドを 1 行ずつ UTF-8 へ写す。
 * @details 行の色は「行内で最初に見つかった文字の色」（Term ミラーの `pick_line_color` と同じ）。
 * コアのサブウインドウは 1 行に複数色を使うが、行単位で描く限りは 1 行 1 色にしかできない。
 */
void capture_term_lines(const SubTerm &st, std::vector<SubPanelLine> &out)
{
    out.clear();
    if (!st.installed || st.term.scr == nullptr) {
        return;
    }

    const auto &scr = *st.term.scr;
    const int height = (std::min)(st.rows, static_cast<int>(scr.c.size()));
    for (int y = 0; y < height; ++y) {
        const auto &row_c = scr.c[static_cast<size_t>(y)];
        const auto &row_a = scr.a[static_cast<size_t>(y)];
        const int width = (std::min)(st.cols, static_cast<int>(row_c.size()));

        std::string sys_line;
        sys_line.reserve(static_cast<size_t>(width) + 1U);
        uint8_t color = 1;
        bool color_found = false;

        for (int x = 0; x < width; ++x) {
            const uint8_t attr = static_cast<uint8_t>(row_a[static_cast<size_t>(x)]);
            if (is_graphic_tile_attr(attr)) {
                sys_line.push_back(' ');
                continue;
            }
            if ((attr & kAfKanji2) != 0) {
                continue; // 全角の右半分（左半分のセルが文字を丸ごと持っている）
            }

            const auto &cell = row_c[static_cast<size_t>(x)];
            append_term_char(sys_line, cell);
            if (!color_found && !term_char_is_blank(cell)) {
                color = static_cast<uint8_t>(attr & kAfKanjic);
                color_found = true;
            }
        }

        while (!sys_line.empty() && sys_line.back() == ' ') {
            sys_line.pop_back();
        }

        SubPanelLine line{};
        line.color = color;
        if (const auto utf8 = sys_to_utf8(sys_line)) {
            line.text_utf8 = *utf8;
        } else {
            line.text_utf8 = sys_line;
        }
        out.push_back(std::move(line));
    }

    // 末尾の空行は落とす（枠の下に空きが並ぶだけなので）。途中の空行は残す。
    while (!out.empty() && out.back().text_utf8.empty()) {
        out.pop_back();
    }
}

} // namespace

} // namespace presentation

/*
 * 種類の一覧（`presentation/frame/sub_panel_kinds.h`）はグローバル名前空間の関数として
 * 宣言してある（ui からも呼ぶため）。コアの表を読めるのはこの TU だけなので実装はここ。
 */
const std::vector<SubPanelKindEntry> &sub_panel_kind_entries()
{
    static std::vector<SubPanelKindEntry> entries;
    //! @details 見出しは言語で変わる。**遊んでいる途中で切り替わる**ので、
    //! 1 度組んで終わりにはできない（`i18n::language_generation()` で古さを見る）。
    static int built_generation = -1;
    if (built_generation == i18n::language_generation()) {
        return entries;
    }

    built_generation = i18n::language_generation();
    entries.clear();
    entries.push_back(SubPanelKindEntry{ -1, _("UI 既定", "UI default") });
    for (int i = 0; i < enum2i(SubWindowRedrawingFlag::MAX); ++i) {
        if (window_flag_desc[i] == nullptr) {
            continue; // 予約領域
        }
        if (i == enum2i(SubWindowRedrawingFlag::SNAPSHOT)) {
            // 記念撮影には `window_stuff()` から呼ばれる `fix_*` が無い。
            // 選ばせると永久に空のままなので一覧に載せない。
            continue;
        }
        SubPanelKindEntry entry{};
        entry.flag = i;
        if (const auto utf8 = sys_to_utf8(window_flag_desc[i])) {
            entry.label_utf8 = *utf8;
        } else {
            entry.label_utf8 = window_flag_desc[i];
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

const std::string &sub_panel_kind_label(int flag)
{
    const auto &entries = sub_panel_kind_entries();
    for (const auto &entry : entries) {
        if (entry.flag == flag) {
            return entry.label_utf8;
        }
    }
    return entries.front().label_utf8;
}

int sub_panel_kind_index_of(int flag)
{
    const auto &entries = sub_panel_kind_entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].flag == flag) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

int sub_panel_kind_flag_at(int index)
{
    const auto &entries = sub_panel_kind_entries();
    if (index < 0 || static_cast<size_t>(index) >= entries.size()) {
        return -1;
    }
    return entries[static_cast<size_t>(index)].flag;
}

namespace presentation {

void install_sub_window_terms()
{
    auto *prev = game_term;
    for (int i = 0; i < kSubPanelCount; ++i) {
        SubTerm &st = g_sub_terms[i];
        if (st.installed) {
            continue;
        }

        // キュー長 0 で作る（このTermから入力は取らない。取る経路も無い）。
        term_init(&st.term, st.cols, st.rows, 0);
        st.term.attr_blank = TERM_WHITE;
        st.term.char_blank = ' ';
        st.term.soft_cursor = true;
        st.term.always_pict = false;
        st.term.always_text = false;
        // never_fresh を立てると `window_stuff()` がこの Term のフラグを集めない
        // （`window-redrawer.cpp:255`）。**立ててはいけない。**
        st.term.never_fresh = false;
        st.term.init_hook = hook_init;
        st.term.nuke_hook = hook_nuke;
        st.term.text_hook = hook_text;
        st.term.wipe_hook = hook_wipe;
        st.term.curs_hook = hook_curs;
        st.term.pict_hook = hook_pict;
        st.term.xtra_hook = hook_xtra;
        st.term.data = &st;

        angband_terms[static_cast<size_t>(kFirstSubTermIndex + i)] = &st.term;
        st.installed = true;
    }
    // 本線 Term を奪ったままにしない（install は activate しない）。
    term_activate(prev);
}

void set_sub_panel_term_cells(int panel, int cols, int rows)
{
    if (panel < 0 || panel >= kSubPanelCount) {
        return;
    }
    SubTerm &st = g_sub_terms[panel];
    st.want_cols = clamp_int(cols, kMinCols, kMaxCols);
    st.want_rows = clamp_int(rows, kMinRows, kMaxRows);
}

void fill_sub_panels(GameFrame &frame)
{
    bool need_redraw = false;
    auto &opts = sdl_ui_options();
    /*
     * **画面の希望をここで写し取る。**`apply_ui_state()` が capture の直前に書いた値が
     * まだ入っている——この後 `load_savefile_config()` がキャラクター別構成で塗り替えるので、
     * 先に控えておかないと「画面が送り返してきた値」と区別が付かなくなる
     * （区別できないと抱え込みがその場で解け、読んだ構成が 1 フレームで消える）。
     */
    int ui_wants[kSubPanelCount];
    for (int i = 0; i < kSubPanelCount; ++i) {
        ui_wants[i] = opts.sub_panel_kind[i];
    }
    //! キャラクターごとの構成（セーブの横に置く `<セーブ名>.sdl2panels`）を読む。
    load_savefile_config(opts);
    //! コア側で決まった中身（`=`→`w`／いま読んだ構成）を抱え込む。**下の輪より先に見る。**
    take_core_side_changes(opts);

    for (int i = 0; i < kSubPanelCount; ++i) {
        SubTerm &st = g_sub_terms[i];
        SubPanelContent &content = frame.sub_panels[static_cast<size_t>(i)];
        int kind = st.installed ? sanitize_kind(opts.sub_panel_kind[i]) : -1;

        if (st.installed && (g_core_override[i] != kNoOverride)) {
            //! コア側で決まった中身。**画面が同じ値を送り返すまで抱える。**
            if (ui_wants[i] == g_core_override[i]) {
                g_core_override[i] = kNoOverride; //!< 画面が追いついた。手綱を返す
            } else {
                kind = g_core_override[i]; //!< まだ。**画面の古い値で戻さない**
            }
        }

        if (apply_kind(i, kind)) {
            need_redraw = true;
        }
        //! `opts` にも返す（`.sdl2panels` と、次に画面から来る値の突き合わせに要る）。
        opts.sub_panel_kind[i] = kind;
        if (kind >= 0 && resize_if_needed(i)) {
            need_redraw = true; // 桁数が変わったので中身を作り直させる
        }

        content.kind = kind;
        if (kind < 0) {
            content.title_utf8.clear();
            content.lines.clear();
            continue;
        }
        content.title_utf8 = sub_panel_kind_label(kind);
        capture_term_lines(st, content.lines);
    }

    //! **実効値**をキャラクター別構成へ残す（輪を回した後。画面の古い値では書かない）。
    save_savefile_config(opts);

    if (need_redraw) {
        // 種類・桁数が変わった＝いま Term に入っている絵は古い。次の handle_stuff で
        // コアに描き直させる（どの種類がどのパネルに移ったか追うより確実）。
        RedrawingFlagsUpdater::get_instance().fill_up_sub_flags();
    }
}

} // namespace presentation
