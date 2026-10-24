/*!
 * @file dcss_layout.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）の画面の配置の計算。原作のタイル版 `TilesFramework::do_layout` の移植。
 *
 * ## 何のためのものか
 * 設計の材料 段取り §6.8 の U0。決めたこと D10
 * （画面は原作のタイル版の UI を再現し、マップだけを立体にする）に沿い、原作が窓の大きさから決める
 * 各区画（ダンジョン・メッセージ・ステータス・ミニマップ・タブの欄・固定表示のタブ・全画面の文字画面）の
 * 矩形を、描画にも通信にも触れない**純粋な関数**として計算する。
 *
 * ## 原作のどこを写したか
 * 原作 `crawl-ref/source/` の次を、順序も整数の割り算の向きも含めて写した（区画の大きさを持つ
 * `Region` の 4 つの操作 `resize`・`place`・`resize_to_fit`・`recalculate` もそのまま写してあり、
 * 同じ手順で同じ整数が出る）。
 * - `tilesdl.cc` の `do_layout`・`is_using_small_layout`・`layout_statcol`・`resize_inventory`・
 *   `place_tab`・`place_minimap`・`autosize_minimap`・`calculate_default_options`（字の大きさの表）
 * - `tilereg.cc`（`Region`）、`tilereg-text.cc`（`TextRegion` の格子の数え方）、
 *   `tilereg-tab.cc`（`TabbedRegion` の見出しの高さ・小さい配置の拡大率）
 *
 * ## 呼び出し側が決める物（決め打ちしない）
 * - **字の幅と高さ**は引数 `DcssFontMetrics`。原作は `FT_Size_Metrics.max_advance` を桁の幅にする
 *   （`fontwrapper-ft.cc:105-107`）。手元のタイル版での実測（別の担当、2026-10-21）では、格子の 1 桁は
 *   **最大送り幅 = 1 em（全角の幅）**で、字そのものは半角 0.5 em で並ぶ（ステータス 16 px なら格子 16 px、
 *   メッセージ 14 px なら 14 px）。この関数は値をそのまま使い、半角・全角の仮定を持たない。呼び出し側は
 *   実測に合わせて `char_w` に字の大きさと同じ値を渡す。1280×720 は小さい配置（窓幅 1490 未満）になる。
 * - **ゲームの状態**（呪文の数・能力の数）は固定表示のタブの置き方に効くので引数 `DcssLayoutInput`。
 *
 * ## 原作と違う所・原作のままにした所（確かめた事実）
 * - 原作では `TAB_MONSTER`・`TAB_SKILL`・`TAB_COMMAND2` に代入が無い（`tilesdl.cc:409-419`。
 *   グローバルの `tiles` の静的記憶域なので 0 のまま）。`place_tab` の判定
 *   `idx == TAB_MONSTER` などは**見出し 0 番（持ち物）のときだけ真**で、実際に呼ばれる
 *   怪物・技能・システム命令のタブは行数の既定（最小 1・最大 1）で置かれる。
 *   `DcssLayoutInput::unassigned_tab_ids_are_zero` が真（既定）なら原作どおりこの挙動を写す。
 *   偽にすると「怪物は最大 3 行・技能とシステム命令は命令の数に応じた行数」という書き手の意図の挙動になる。
 * - 見出し（タブの欄の左端）の有効・無効は原作ではレイアウトをまたいで残る状態（`enable_tab`/`disable_tab`）。
 *   ここは純粋な関数なので、**直前の配置が終わった時点の有効・無効**を `tab_enabled` で渡せる
 *   （既定は全部有効 ＝ ゲームに入って最初の配置）。結果の `tab_enabled` を次の呼び出しへ渡せば
 *   原作と同じ変遷になる。
 * - 小さい配置（窓幅が狭い・`tile_use_small_layout`）は式を写してあるが、第 1 回の対象外
 *   （設計の材料 §1.3）。1 em の字だと 1480 px 以下の窓幅（1280×720 を含む）で自動的にこの配置になるので、
 *   扱いは遊ぶ人に聞き直し中。手で確かめた件数は少ない。通常配置を先に仕上げる。
 * - 字を使わない描き方（`tile_display_mode` が glyphs・hybrid）の `config_glyph_font` は写していない
 *   （立体の地図では tiles 固定。設計の材料 §2）。
 *
 * ## 使うかどうか
 * まだどこからも呼ばれない（U0）。DCSS の画面が握手で `dcss-ui` を申告したときだけ使う予定で、
 * 他のコアの `UiLayout::compute` には触れない。
 */
#pragma once

#include <array>
#include <string>
#include <vector>

namespace hd2d::dcss {

//! 視野の直径（原作 `ENV_SHOW_DIAMETER` = `LOS_RADIUS` 8 × 2 + 1）。
inline constexpr int kLosDiameter = 17;
//! ミニマップの格子の大きさ（原作 `GXM`・`GYM`）。
inline constexpr int kMapGridCols = 80;
inline constexpr int kMapGridRows = 70;
//! タブの欄の見出しの数（持ち物・呪文・覚える呪文・能力・怪物・技能・命令・システム命令・地図の命令）。
inline constexpr int kTabCount = 9;

//! タブの欄の見出しの番号（原作 `tilesdl.cc:409-419` の登録順）。
enum DcssTab : int {
    kTabItem = 0,
    kTabSpell,
    kTabMemorise,
    kTabAbility,
    kTabMonster,
    kTabSkill,
    kTabCommand,
    kTabSystemCommand,
    kTabNavigation,
};

//! 歯車の見出しの番号（`DcssTab` の外。タブではない。押すと HD2D の機能メニューが開くだけで、選んだ見出しにはならない。D31）。
inline constexpr int kTabGear = kTabCount;

//! 1 種類の字の大きさと桁の幅・行の高さ（画素）。
struct DcssFontMetrics {
    int size_px{ 0 }; //!< 字の大きさ（`tile_font_*_size`）。ラベルは見出しの余白の計算にも使う。
    int char_w{ 0 }; //!< 1 桁の幅（原作 `FTFontWrapper::char_width()` = `max_advance.x`）。
    int char_h{ 0 }; //!< 1 行の高さ（原作 `char_height()` = ascender − descender）。
};

//! 字の種類ごとの大きさ。
struct DcssFontSet {
    DcssFontMetrics crt; //!< 窓の部品・全画面の文字画面（`tile_font_crt_*`）
    DcssFontMetrics msg; //!< メッセージ欄
    DcssFontMetrics stat; //!< ステータス欄
    DcssFontMetrics tip; //!< ツールチップ（配置には使わない）
    DcssFontMetrics lbl; //!< ラベル（タブの余白の計算に大きさだけ使う）
};

//! 窓の大きさから自動で決まる既定（原作 `_screen_sizes` と `calculate_default_options`）。
struct DcssScreenProfile {
    int map_px{ 0 }; //!< ミニマップの 1 マス
    int crt_px{ 0 };
    int stat_px{ 0 };
    int msg_px{ 0 };
    int tip_px{ 0 };
    int lbl_px{ 0 };
};

//! 窓の大きさに合う行を引く（Android 以外の表。どれにも収まらなければ最後の行）。
DcssScreenProfile dcss_screen_profile(int window_w, int window_h);

/*!
 * Android・Quest の窓の拡大と字の大きさ（原作 `windowmanager-sdl.cc:1013` の `game_scale`、`tilesdl.cc:77-88` の Android の `_screen_sizes`、
 * `calculate_default_options`）。原作の Android 版は、端末の短い辺 `ref` から
 *   game_scale = ref / 1080 + 1（整数の割り算）
 * を求め、窓を game_scale 分の 1 の大きさ（論理の窓）で配置して、描くときに game_scale 倍に拡大する。字の大きさとダンジョンの拡大率は、
 * 短い辺に合う行を表から引く（`game_scale` が `ref / 960 + 1` と同じなら、行の幅に game_scale を掛けて比べる）。
 * HD2D では論理の窓で配置を計算し、描く側が座標系ごと game_scale 倍に拡大する（`DcssUi::prepare`・`draw`）。
 */
struct DcssAndroidProfile {
    int game_scale{ 1 };
    DcssScreenProfile fonts{}; //!< map_px は常に 1
    int viewport_scale_pct{ 100 }; //!< `tile_viewport_scale`（ダンジョンの拡大率を 100 倍した整数）
    int map_scale_pct{ 50 }; //!< `tile_map_scale`
};

//! 端末の画面の大きさ（短い辺が基準）から引く。
DcssAndroidProfile dcss_android_profile(int display_w, int display_h);

//! `tile_use_small_layout`（原作の `maybe_bool`）。
enum class DcssSmallLayoutMode {
    Auto, //!< 窓幅が `stat 桁幅 × 45 + msg 桁幅 × 55` 未満なら小さい配置
    Off,
    On,
};

//! 配置の入力。数の既定は原作 `initfile.cc` の `Options`。
struct DcssLayoutInput {
    int window_w{ 0 };
    int window_h{ 0 };
    DcssFontSet fonts{};

    // --- 設定（`tile_*`・`view_*`・`msg_*`）---
    int tile_cell_pixels{ 32 }; //!< `tile_cell_pixels`
    int tile_viewport_scale_pct{ 100 }; //!< `tile_viewport_scale` を 100 倍した整数（1.0 → 100）
    int tile_map_scale_pct{ 60 }; //!< `tile_map_scale` を 100 倍した整数（0.6 → 60）
    bool map_mode{ false }; //!< 全体地図の画面（`m_map_mode_enabled`）。真なら map_scale を使う
    int tile_sidebar_pixels{ 32 }; //!< `tile_sidebar_pixels`（格子と見出しの 1 マス）
    int tile_min_stat_width_characters{ 42 };
    int tile_map_pixels{ 0 }; //!< 0 = 自動。ミニマップの 1 マスの上限（非 0 のときだけ上限になる）
    int view_max_height{ 21 };
    int msg_min_height{ 7 };
    int msg_max_height{ 10 };
    DcssSmallLayoutMode small_layout_mode{ DcssSmallLayoutMode::Auto };
    bool force_overlay{ false }; //!< `tile_force_overlay`
    int overlay_alpha_percent{ 40 }; //!< `tile_overlay_alpha_percent`
    //! `tile_layout_priority`。"minimap"・"inventory" と、タブの名前（command・spell・ability・monster …）
    std::vector<std::string> layout_priority{ "minimap", "inventory", "command", "spell", "ability", "monster" };
    bool show_tab_icons{ true }; //!< 小さい配置で見出しの絵を出すか（Android は偽）
    //! 見出しの数に足す物（HD2D の機能メニューの歯車。原作には無い。D31）。見出しの大きさを決める原作の式（`tilereg-tab.cc:447`）の `NUM_TABS` に足す。0 のときは原作のまま。
    int extra_tab_headings{ 0 };
    //! 真なら、小さい配置で見出しの絵を出さない（`show_tab_icons` が偽）ときも右端の列の幅を確保する（歯車だけを出すため。D31）。
    bool tab_column_always{ false };

    // --- 見出しの絵の大きさ（`rltiles/gui/tab_*.png`）---
    int tab_icon_w{ 20 };
    int tab_icon_h{ 20 };

    // --- ゲームの状態 ---
    int spell_count{ 0 }; //!< `you.spell_no`
    int ability_count{ 0 }; //!< `your_talents().size()`
    int skill_count{ 34 }; //!< `NUM_SKILLS`（`TAG_MAJOR_VERSION` 34 の値。`skill-type.h`）
    int action_command_count{ 12 }; //!< `ct_action_commands` の数（`tilereg-cmd.h`）
    int system_command_count{ 12 }; //!< `ct_system_commands`（Android は 13）
    int map_command_count{ 17 }; //!< `ct_map_commands`

    //! 直前の配置が終わった時点で、見出しがタブの欄の中に残っているか（既定 = 全部有効 ＝ 最初の配置）。
    std::array<bool, kTabCount> tab_enabled{ true, true, true, true, true, true, true, true, true };

    //! 真なら原作どおり `TAB_MONSTER`・`TAB_SKILL`・`TAB_COMMAND2` を 0 として扱う（冒頭の説明を参照）。
    bool unassigned_tab_ids_are_zero{ true };
};

//! 1 つの区画（原作 `Region` の寸法）。
struct DcssRegion {
    int x{ 0 }; //!< 左上（`sx`）。窓の外へはみ出す区画は負にもなる
    int y{ 0 }; //!< 左上（`sy`）
    int w{ 0 }; //!< 区画の幅（`wx` = 余白 × 2 + 格子の幅）
    int h{ 0 }; //!< 区画の高さ（`wy`）
    int end_x{ 0 }; //!< 右下（`ex`）。`resize_to_fit` の直後は要求した幅の端
    int end_y{ 0 }; //!< 右下（`ey`）
    int margin_x{ 0 }; //!< 余白（`ox`）。ミニマップは中身を中央へ寄せる前の値
    int margin_y{ 0 }; //!< 余白（`oy`）
    int cell_w{ 0 }; //!< 1 マス・1 桁の幅（`dx`）
    int cell_h{ 0 }; //!< 1 マス・1 行の高さ（`dy`）
    int cols{ 0 }; //!< 格子の列数（`mx`）
    int rows{ 0 }; //!< 格子の行数（`my`）
};

//! 左のサイドバーの下側に積む固定表示のタブ。
struct DcssFixedTab {
    int tab_index{ 0 }; //!< `DcssTab`
    DcssRegion region{};
};

struct DcssLayout {
    bool ok{ false };
    std::string error; //!< ok が偽のときの理由（原作は `Failed to find a valid window layout` で終了する）

    bool small_layout{ false };
    bool message_overlay{ false }; //!< メッセージをダンジョンの上に重ねる
    int overlay_alpha{ 0 }; //!< 重ね表示の不透明度（0〜255。`255 * percent / 100`）

    int sidebar_width{ 0 }; //!< `sidebar_pw`
    int stat_x_divider{ 0 }; //!< ダンジョンとサイドバーを分ける縦線の x
    int stat_column_x{ 0 }; //!< サイドバーの左端（`m_stat_col`）
    int message_y_divider{ 0 }; //!< ダンジョンとメッセージを分ける横線の y
    int statcol_top{ 0 }; //!< 配置が終わったときの積み位置（`m_statcol_top`）
    int statcol_bottom{ 0 }; //!< 同（`m_statcol_bottom`）

    DcssRegion dungeon{}; //!< 窓より大きく取った区画全体（自キャラが中央に来る）
    int dungeon_view_w{ 0 }; //!< 見える範囲の幅（`tile_iw`）。見える範囲は (0, 0) から
    int dungeon_view_h{ 0 }; //!< 同（`tile_ih`）
    DcssRegion message{};
    DcssRegion stat{};
    bool has_minimap{ false };
    DcssRegion minimap{}; //!< has_minimap が真のとき。サイドバーの余った空間いっぱい（中身は中央寄せ）
    DcssRegion tabs{}; //!< タブの欄（見出しは左端の `margin_x` 画素、中身は `x + margin_x` から）
    std::vector<DcssFixedTab> fixed_tabs; //!< 置いた順
    DcssRegion crt{}; //!< 全画面の文字画面

    //! 結果の見出しの有効・無効（固定表示に出した見出しは無効）。次の配置へ渡せる。
    std::array<bool, kTabCount> tab_enabled{ true, true, true, true, true, true, true, true, true };

    /*!
     * @brief 窓の大きさと設定から配置を決める。
     * @details 描画・通信・ファイルに触れず、同じ入力から必ず同じ結果を返す。
     */
    static DcssLayout compute(const DcssLayoutInput &in);
};

} // namespace hd2d::dcss
