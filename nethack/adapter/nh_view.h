#pragma once

/*
 * NetHack のコアの「いま画面に何を見せているか」の状態（C++ の側）。
 *
 * 画面の関数（C。nh_winport.c）から nh_host.h の関数で届いた内容をここに溜め、
 * nh_frame.cpp が frame に直す。ゲームを動かすのは 1 本のスレッドだけで、この状態を書き換えるのも
 * そのスレッドだけ（受信の糸は触らない）。
 */

#include "nh_host.h"

#include <deque>
#include <string>
#include <vector>

struct GameFrame;

namespace nethack {

//! 原作の地図の大きさ（COLNO・ROWNO）。
constexpr int kMapCols = 80;
constexpr int kMapRows = 21;
//! frame の messages に載せる直近の行数。
constexpr int kFrameMessageLines = 8;
//! ^P で見せる履歴の行数の上限。
constexpr std::size_t kHistoryMax = 200;

enum class Mode {
    Map,    //!< 地図（命令・方向・位置の選択などをキーで待つ）
    Menu,   //!< 選ぶメニュー（PICK_ONE / PICK_ANY）
    Text,   //!< 見せるだけの窓（PICK_NONE のメニューと文字の窓）
    Yn,     //!< yn_function
    Getlin, //!< getlin（文字の入力）
};

//! メニュー・文字の窓の 1 行。
struct Line {
    std::string text;
    bool selectable{ false };
    int selector{ 0 }; //!< 選ぶ字（無ければ 0）
    bool selected{ false };
};

//! yn_function の選べる字 1 つ。
struct YnChoice {
    int key{ 0 };
    std::string label;
};

//! 持ち物・使用中の装備の 1 行（字と、原作が出した名前。見るだけ）。
struct InventoryRow {
    std::string label;
    int key{ 0 };
};

//! 地図の 1 マス（print_glyph の内容。nh_host_map_cell 参照）。
struct MapCell {
    //! 一番上に見える物の字（原作の ttychar）と色（原作の色の番号 0〜15）。
    int ch{ ' ' };
    int color{ 0 };
    int glyph{ 0 };
    int glyph_class{ NH_GLYPH_NOTHING };
    int glyph_index{ 0 };
    unsigned flags{ 0 };
    //! 描く地形の表示記号の番号（原作の S_*）。-1 は分からない。
    int terrain_cmap{ -1 };
    //! その地形の変種（壁の迷宮の系統 0〜4・祭壇の属性 0〜4。それ以外は 0。nh_host_map_cell 参照）。
    int terrain_variant{ 0 };
};

struct View {
    Mode mode{ Mode::Map };
    int phase{ NH_PHASE_PREGAME };
    int key_mode{ NH_KEY_OTHER };
    //! メニューの選び方（NH_PICK_*）。
    int how{ NH_PICK_NONE };

    //! メニューの問い・yn の問い・getlin の問い。
    std::string prompt;
    std::vector<Line> lines;
    std::vector<YnChoice> yn_choices;
    //! getlin で入れた字。
    std::string typed;

    //! 過去のメッセージ（古い順）。
    std::deque<std::string> history;

    //! ステータスの項目の値の字（NH_BL_* で引く）。
    std::string status[NH_BL_MAX];
    unsigned long conditions{ 0 };
    std::string condition_names;
    std::string player_name;
    bool status_seen{ false };

    //! 地図（y * kMapCols + x）。
    std::vector<MapCell> map = std::vector<MapCell>(static_cast<std::size_t>(kMapCols) * kMapRows);
    int cursor_x{ 0 };
    int cursor_y{ 0 };
    //! frame を送る直前に nh_snapshot_view で写す、いま見えているマス（y * kMapCols + x。1 が見えている）。
    std::vector<unsigned char> visible = std::vector<unsigned char>(static_cast<std::size_t>(kMapCols) * kMapRows, 0);
    /*!
     * 持ち物と使用中の装備。原作が画面の関数へ送る持ち物の一覧（命令 i と * のメニュー）を、nh_main.cpp が
     * 次の命令待ちで流して写し取る。サブパネルの装備・持ち物と、持ち物一覧のタブに使う。
     * inventory_known が偽の間（ゲームの始めなど、まだ写していない）は空のまま。
     */
    std::vector<InventoryRow> inventory;
    std::vector<InventoryRow> equipment;
    bool inventory_known{ false };

    //! 主人公の位置。map_valid が偽（ゲームがまだ始まっていない）の間は使わない。
    int hero_x{ 0 };
    int hero_y{ 0 };
    bool map_valid{ false };

    /*!
     * 専用の画面（握手で nethack-ui を申告し合った）か。真のとき frame には地図（cells・minimap・カメラ）と最小の hud だけを載せ、
     * 窓の字（menu_*・prompt・messages・status_line）と新しい画面の窓のモデルは載せない（nethack_ui の知らせで送る）。
     */
    bool ui_mode{ false };
};

/*!
 * 原作のタイルの絵の表（板になる絵の番号の集合と、glyph → 絵の番号）を渡す。
 * 絵の番号は tiles.json の glyph2tile（原作の tilemap が作る glyphmap[].tileidx と同じ）。板にする絵は
 * `nethack/data/slab/slabs.tsv` の絵の番号（tools/nethack/tiles_to_slabs.py）。どちらかが空なら、怪物・アイテムに絵の番号を付けず
 * 字の板で出す。frame を組む前に 1 回呼ぶ。
 */
void set_tile_tables(std::vector<int> glyph_to_tile, const std::vector<int> &slab_tiles);

/*!
 * 画面へ送る絵の索引（`MapCellView::tile_index`）にする。原作の絵の番号は 0 から始まるが、索引 0 は「絵は無い」の意味なので
 * 1 を足す（asset_manifest の `i` も同じ）。
 */
constexpr int tile_index_of(int tile) { return tile + 1; }

//! 壊れた UTF-8 と制御文字を直す（JSON に載せると落ちるので、画面へ出す字は必ずこれを通す）。
std::string clean_utf8(const char *text);

/*!
 * 状態から frame を組む。ja は画面の言語（ボタンの名札を替える）。
 * modern は新しい画面（ui_state.modern_ui）か。真のとき、窓ごとの欄を新しい画面の窓のモデル（nh_main.cpp の
 * NativeModernMenu）が読みやすい形にする（見せるだけの窓に「続ける」の選択肢を置かない・複数選択の行に選び状態を付ける）。
 */
void build_frame(const View &view, bool ja, const char *core_name, bool modern, GameFrame &frame);

} // namespace nethack
