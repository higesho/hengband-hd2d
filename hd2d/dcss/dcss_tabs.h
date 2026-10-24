/*!
 * @file dcss_tabs.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）の画面の部品: 右のタブの欄（持ち物・呪文・覚える呪文・能力・怪物・技能・命令 3 種）。段 U2。
 *
 * ## 何のためのものか
 * 原作のタイル版は右下の「タブの欄」（`TabbedRegion`）に、見出しの絵を縦に積み、選んだ見出しの中身を 32 ドットのマスの格子で出す。
 * 中身は区画ごとのクラス（`InventoryRegion`・`SpellRegion` ほか。`tilereg-*.cc`。ローカル版にしか組まれない）が持つ。
 * ここはその `update()`（並べ方）・`pack_buffers()`（絵の重ね方）・`draw_tag()`（載せたマスの説明の 1 行）・`handle_mouse()`（クリック）を
 * 描画の側に写した物。設計の材料は 〜§1.5.7・§4.1（U2〜U9）。
 *
 * ## データ
 * 中身（持ち物・足元のアイテム・呪文・能力・怪物・技能・命令）はウェブタイル版の JSON に無い。コアのアダプタ（`dcss/adapter/dcss_tabs_data.h`）が
 * ゲームの中から読んで `{"t":"dcss_ui","k":"tabs",…}` で送る。マスの数で変わる並べ方（空きの枠・足元の部分・ページ送り）は、ここが原作と同じ規則で決める。
 * 絵は原作のタイル版が使うのと同じ画像ページ（`assets/ui/dcss/atlas/`。`tools/dcss/dcss_tile_atlas.py --ui-out` が作る）から切り出す。
 *
 * ## 操作
 * 見出しを押すとタブを替える（画面の中だけ）。マスの左・右クリックは、コアへ `{"t":"dcss_action",…}` を送る。コアは原作の各区画の
 * `handle_mouse` と同じ関数をゲームの入力待ちの中で実行する（説明の窓はコアが `ui-push` で送る）。怪物の欄は原作と同じく
 * 「そのマスへのクリック」なので、ウェブタイル版の `click_cell` を送る。
 *
 * 描画にも通信にも触れない部分（データの読み込み・並べ方）は `DcssTabs` の中の関数に分け、`tools/hd2d_verify` の単体検査で確かめられる。
 */
#pragma once

#include "dcss/dcss_layout.h"
#include "render/text_overlay.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hd2d::dcss {

//! 原作 `TILEI_FLAG_*`（`tile-inventory-flags.h`）。コアが送る旗と同じ値。
enum DcssTileFlag : unsigned {
    kTfChaotic = 0x0200,
    kTfEquip = 0x0400,
    kTfFloor = 0x0800,
    kTfCurse = 0x1000,
    kTfCursor = 0x2000,
    kTfMelded = 0x4000,
    kTfInvalid = 0x8000,
};

//! 格子の 1 マス（原作 `InventoryTile`）。
struct DcssTabTile {
    int idx{ -1 }; //!< 持ち物の枠・呪文・能力・技能・命令・怪物の番号。-1 = 空き
    int tile{ 0 };
    int base{ 0 }; //!< アイテムの下地（`tileidx_known_base_item`）
    int quantity{ -1 }; //!< 数字（-1 = 出さない）
    unsigned flag{ 0 };
    int special{ 0 };
    int special2{ 0 };
    int aptitude{ 0 }; //!< 技能の適性
    std::string name; //!< 載せたときの説明の 1 行
    bool empty() const { return this->idx == -1; }
};

//! 怪物の欄の 1 体。
struct DcssTabMonster {
    std::string name;
    int rel_x{ 0 };
    int rel_y{ 0 };
    bool in_sight{ false };
    std::vector<int> under; //!< 背景から下に重ねる絵（床・地形・態度の輪・脅威の枠）
    std::vector<std::array<int, 4>> layers; //!< 前景の層 [タイル番号, x のずれ, y のずれ, y_max]
    std::vector<int> icons; //!< 状態のアイコン（icons ページ）
};

//! コアが送ったタブの欄の中身（`dcss_ui` の `tabs`）。
struct DcssTabData {
    bool received{ false };
    bool map_display{ false };
    int floor_tile{ 0 };
    int floor_count{ 1 };
    std::vector<DcssTabTile> inv;
    std::vector<DcssTabTile> floor;
    std::vector<DcssTabTile> spells;
    std::vector<DcssTabTile> memorise;
    std::vector<DcssTabTile> abilities;
    std::vector<DcssTabTile> skills;
    std::vector<DcssTabMonster> monsters;
    std::array<std::vector<DcssTabTile>, 3> commands;
};

//! 切り出しの 1 枚（原作 `tile_info`）。
struct DcssAtlasTile {
    int w{ 0 }, h{ 0 }, ox{ 0 }, oy{ 0 }, sx{ 0 }, sy{ 0 }, ex{ 0 }, ey{ 0 };
    char page{ 0 }; //!< f floor / w wall / e feat / m main / p player / g gui / i icons
};

//! 原作のタイル番号 → 画像ページの切り出し。
class DcssAtlas {
public:
    //! `tiles.tsv` を読む（画像ページは使うときに読む）。
    bool load(const std::string &dir, std::string &err);
    bool loaded() const { return this->loaded_; }
    const DcssAtlasTile *info(int tile) const;
    int constant(const std::string &name, int fallback = 0) const;
    //! ページの絵（GL が要る。無ければ nullptr）。
    const UiImage *image(char page);
    void shutdown();

private:
    bool loaded_{ false };
    std::string dir_;
    std::vector<DcssAtlasTile> tiles_;
    std::map<std::string, int> consts_;
    std::map<char, UiImage> images_;
    std::map<char, bool> failed_;
};

//! 画面から出す要求。
struct DcssTabOutgoing {
    //! 真なら原作の制御メッセージ（`webtiles_input` の `line`）。偽ならアダプタへの要求（`dcss_action`）の JSON 全体。
    bool webtiles{ false };
    std::string text;
};

//! マウスの出来事（画面の座標）。
struct DcssTabMouse {
    enum class Kind { Move, Press };
    Kind kind{ Kind::Move };
    int x{ 0 };
    int y{ 0 };
    int button{ 1 }; //!< 1 左・3 右
    int mods{ 0 }; //!< 1 Shift・2 Ctrl
};

class DcssTabs {
public:
    DcssTabs();
    ~DcssTabs();

    //! `{"t":"dcss_ui","k":"tabs",…}`（`k` を見分けた後の JSON）を取り込む。変わったら真。
    bool ingest_json(const std::string &payload);
    const DcssTabData &data() const { return this->data_; }
    //! 配置の計算に渡す数（呪文・能力の数。固定表示のタブの行数に効く）。
    int spell_count() const { return static_cast<int>(this->data_.spells.size()); }
    int ability_count() const { return static_cast<int>(this->data_.abilities.size()); }

    //! 絵の置き場（`assets/ui/dcss/atlas`）を読む。失敗したら理由を返して偽（そのとき中身は出さず、枠と見出しだけ）。
    bool load_atlas(const std::string &dir, std::string &err);

    /*!
     * @brief 配置を渡す。毎フレーム呼んでよい。
     * @param origin_x,origin_y 配置の左上の窓の座標
     */
    void set_layout(const DcssLayout &layout, int origin_x, int origin_y);
    //! 場面（原作の `input_mode`・全体地図の画面）。操作が効くかの判定に使う。
    void set_state(int input_mode, bool map_screen, bool japanese);

    //! マウス。見出しの押下・マスの押下（コアへの要求を溜める）。区画の中の押下は消費したとして真を返す。
    bool on_mouse(const DcssTabMouse &ev);
    //! 溜めた要求を持っていく。
    std::vector<DcssTabOutgoing> take_outgoing();

    /*!
     * 歯車の見出し（D31。HD2D の機能メニューの入口。原作には無い）。小さい配置の右端に縦に並ぶ見出しの列の最後（下の端）に、ほかの見出しと同じ大きさ・同じ枠で足す。
     * 押すと `take_gear_request()` が真になるだけで、選んだ見出しは替わらない（地図・タブの切り替えへも漏れない）。
     * @param enabled 真なら足す（配置に右端の列の幅があるときだけ。列の幅は `DcssLayoutInput::tab_column_always` で確保する）
     * @param others_visible 偽ならほかの見出しは出さず、歯車だけを同じ場所に出す（原作の Android は既定で見出しの絵を隠す）
     */
    void set_gear_heading(bool enabled, bool others_visible);
    //! 歯車が押されていれば真にして消す。
    bool take_gear_request()
    {
        const bool v = this->gear_requested_;
        this->gear_requested_ = false;
        return v;
    }
    //! 検査用: 歯車の見出しの位置（窓の座標）と大きさ。無ければ偽。
    bool gear_heading(int &x, int &y, int &size) const;
    //! 検査用: 主の欄の見出しの数（歯車を含む）。
    int heading_count() const { return this->regions_.empty() ? 0 : static_cast<int>(this->regions_[0].headings.size()); }

    /*!
     * Android・Quest 用の足し物（原作の `tilereg-cmd.h`: `ct_system_commands` の末尾の `CMD_TOGGLE_KEYBOARD`。`#ifdef __ANDROID__`）。
     * システム命令の欄の 13 番目に「画面上のキーボードを表示/非表示にする」を足す。押すとコアへは送らず `take_keyboard_toggle()` が真になる
     * （画面の側でキーボードを出し入れする）。
     */
    void set_android_extras(bool on) { this->android_extras_ = on; }
    bool take_keyboard_toggle() { const bool v = this->keyboard_toggle_; this->keyboard_toggle_ = false; return v; }

    /*!
     * @brief 見出しと中身を描く。
     * @param paint 色つきの四角（このクラスは使わない。呼び出し側がすでに流してある）
     * @param images 絵を貼る（呼んだ瞬間に描かれる）
     * @param label 載せたマスの説明の字（原作の `tile_font_lbl`）。`begin` 済み
     * @param label_h その字の 1 行の高さ
     */
    void draw(const UiImagePainter &images, TextOverlay &label, int label_w, int label_h);

    //! 検査用: 状態の 1 行の説明（stderr に出す）。
    std::string describe() const;
    //! 状態が変わるたびに増える。
    std::uint64_t serial() const { return this->serial_; }

    //! 読み込んだ絵を捨てる。
    void shutdown();

    // ---- 単体で検査できる部分 ----

    /*!
     * @brief 持ち物の欄の並べ方（原作 `InventoryRegion::update`）。
     * @param page ページ（0 始まり）。返す値は直したページ。
     */
    static std::vector<DcssTabTile> arrange_inventory(const DcssTabData &data, int mx, int my, bool small, int &page);

private:
    struct Geom {
        int tab{ -1 };
        bool fixed{ false };
        int sx{ 0 }; //!< 区画の左上（窓の座標）
        int sy{ 0 };
        int ox{ 0 }; //!< 余白（見出しの幅）
        int dx{ 32 }; //!< 1 マス
        int mx{ 0 };
        int my{ 0 };
        int wy{ 0 }; //!< 区画の高さ
        bool active_tab{ false };
    };
    struct Heading {
        int tab{ 0 };
        int region{ 0 }; //!< `regions_` の番号
        int x{ 0 };
        int y{ 0 };
        int size{ 20 };
        int hit_h{ 21 };
    };
    //! 1 つの `TabbedRegion`（主の欄か、固定表示の 1 枚）。
    struct Region {
        bool fixed{ false };
        int fixed_tab{ -1 }; //!< 固定表示のとき、見せる見出し
        int sx{ 0 }, sy{ 0 }, ox{ 0 }, oy{ 0 }, dx{ 32 }, mx{ 0 }, my{ 0 }, wy{ 0 }, wx{ 0 };
        int active{ -1 }; //!< 選んだ見出し。-1 = 無し
        bool deactivated{ false }; //!< 小さい配置で、中身を閉じている
        int mouse_tab{ -1 };
        std::vector<Heading> headings;
    };
    struct Cursor {
        int cx{ -1 };
        int cy{ -1 };
    };
    struct Op {
        int rank{ 0 };
        int tile{ 0 };
        int cx{ 0 };
        int cy{ 0 };
        int ofs_x{ 0 };
        int ofs_y{ 0 };
        bool centre{ true };
        int ymax{ -1 };
    };

    void rebuild_regions();
    void load_heading_images();
    const std::vector<DcssTabTile> *list_for(int tab) const;
    std::vector<DcssTabTile> items_for(int tab, int mx, int my, bool small, int &page);
    void put(std::vector<Op> &ops, int tile, int cx, int cy, int ofs_x = 0, int ofs_y = 0, bool centre = true, int ymax = -1) const;
    void put_number(std::vector<Op> &ops, int cx, int cy, int number) const;
    void pack_tab(std::vector<Op> &ops, int tab, const std::vector<DcssTabTile> &items, int mx, int my, bool small, int page, const Cursor &cur);
    void pack_monsters(std::vector<Op> &ops, int mx, int my, bool small, const Cursor &cur);
    void draw_ops(const UiImagePainter &images, std::vector<Op> &ops, int ox, int oy, int dx);
    void draw_tab_headings(const UiImagePainter &images, const Region &r);
    bool tab_enabled_in_main(int tab) const;
    void activate_tab(Region &r, int tab, std::vector<DcssTabOutgoing> *out);
    bool command_mode() const { return this->input_mode_ == 1 || this->map_screen_; }
    std::string description_for(int tab, const Cursor &cur, int mx, int my, bool small, int page);
    std::string tab_name(int tab) const;
    bool mouse_in_grid(const Region &r, int x, int y, int &cx, int &cy) const;
    int mouseover_heading(const Region &r, int x, int y) const;
    static std::string action_json(const char *a, int idx, int button, int mods);

    DcssTabData data_;
    DcssAtlas atlas_;
    bool atlas_ok_{ false };
    DcssLayout layout_;
    bool have_layout_{ false };
    int origin_x_{ 0 };
    int origin_y_{ 0 };
    int input_mode_{ 0 };
    bool map_screen_{ false };
    bool prev_map_screen_{ false };
    bool japanese_{ true };

    std::vector<Region> regions_; //!< [0] = 主の欄、以降は固定表示
    std::array<Cursor, kTabCount> cursors_{};
    std::array<int, kTabCount> last_clicked_{};
    int inv_page_{ 0 };
    bool android_extras_{ false };
    bool keyboard_toggle_{ false };
    bool gear_enabled_{ false };
    bool gear_others_visible_{ true };
    bool gear_requested_{ false };
    std::vector<DcssTabOutgoing> outgoing_;
    std::uint64_t serial_{ 0 };

    // 見出しの絵
    UiImage tab_selected_;
    UiImage tab_unselected_;
    UiImage tab_mouseover_;
    UiImage tab_labels_[kTabCount];
    UiImage tab_gear_label_; //!< 歯車の見出しの絵（`assets/ui/dcss/tab_label_gear.png`。無ければ枠だけ）
    bool tab_gear_label_ok_{ false };
    bool heading_images_loaded_{ false };
    bool heading_images_failed_{ false };
};

//! 検査用: 歯車の見出し（D31）の自己検査。窓も GL も使わない。失敗した項目の数を返す。`dcss_touch_selftest` が呼ぶ（`--dcss-touch-check`）。
int dcss_gear_heading_selftest();

} // namespace hd2d::dcss
