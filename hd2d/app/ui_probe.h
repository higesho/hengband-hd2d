/*!
 * @file ui_probe.h
 * @brief 画面が「いま何を出したか」を 1 フレーム分だけ記録する、テスト用の問い合わせの元。
 *
 * `--test-input-file=` を付けて起動したときだけ働く
 * （`enable()`。通常の遊びでは各関数が最初の 1 行で戻るので、記録も速さも変わらない）。
 *
 * ## 作り
 * 部品ごとに手当てを増やさず、描画の共通の関数で記録する。
 * - 文字：`TextOverlay::draw` 系（字を描く所は全部ここを通る）
 * - 押せる部品：当たり判定を決めている所（コア選択の行・機能メニューの行・新しい UI の窓の行や操作・
 *   タブの見出し・バーチャルパッドのボタン・Term の写しの選択肢）。**押せる矩形そのもの**を記録する
 * - 窓・HUD（玉・アイコン・名前の行）・地図・攻撃の効果・メッセージ・通知：描く関数の出口
 *
 * 1 フレームの記録は `commit()` で確定する（`TestKeyboard::poll()` が毎フレームの頭で呼ぶ）。
 * 問い合わせは**確定した直前のフレーム**を返す。座標はすべて窓の画素（SDL のマウス座標と同じ）。
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace hd2d {

//! 窓の画素の矩形（左上 x,y・幅 w・高さ h）。
struct ProbeRect {
    int x{ 0 };
    int y{ 0 };
    int w{ 0 };
    int h{ 0 };
};

//! 描いた文字の書式（色・切れ・字形の欠け）。`TextOverlay` が字を描きながら数える。
struct ProbeTextStyle {
    //! 文字色（0〜1。描いた頂点の色そのもの）。
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    float a{ 1.f };
    //! 呼び出し側の幅の上限（`max_width`）で、字の途中から後ろが切られたか。
    bool clipped{ false };
    //! フォントに字形が無く、四角（豆腐）で出た字の数。
    int missing{ 0 };
    //! 置き換え文字（U+FFFD）を描いた数。
    int replacement{ 0 };
};

//! 描いた文字 1 本（クリップで切れたときは、実際に描いた部分だけ）。
struct ProbeText {
    std::string text;
    int x{ 0 };
    int y{ 0 };
    int w{ 0 };
    int h{ 0 };
    const char *layer{ "" };
    //! 描いた色（RGB は 0〜255、`alpha` は 0〜1）。
    int r{ 255 };
    int g{ 255 };
    int b{ 255 };
    float alpha{ 1.f };
    bool clipped{ false };
    int missing{ 0 };
    int replacement{ 0 };
};

//! 押せる部品 1 つ。
struct ProbeButton {
    std::string label;
    //! 言語に依存しない名前（作れる所だけ。無ければ空）。
    std::string id;
    ProbeRect rect;
    bool enabled{ true };
    bool selected{ false };
    //! 種類（`core`・`lang`・`row`・`tab`・`footer`・`menu-row`・`vpad` など。`tap` の絞り込みに使う）。
    std::string kind;
    //! 指（タッチ）でしか押せない部品（バーチャルパッド）。`tap` の既定の入れ方がタッチになる。
    bool touch_only{ false };
    const char *layer{ "" };
};

//! 窓の一覧の項目 1 つ。`cols` は行の列（店の個数・重さ・値段、呪文の Lv・MP・失率）。
struct ProbeItem {
    std::string id;
    std::string label;
    std::vector<std::string> cols;
    bool enabled{ true };
    bool selected{ false };
};

//! 開いている窓 1 枚。
struct ProbeWindow {
    //! `store`・`inventory`・`dialog`・`feature-menu`・`term`・`number-prompt`・`prompt`・`cutin`・`core-select` など。
    std::string kind;
    std::string title;
    //! 新しい UI の窓の段階（`browse`・`choose`・`number`・`confirm`・`message`・`text`・`cursor`・`finish` など）。無ければ空。
    std::string stage;
    //! 新しい UI の窓の内容の種類（`items`・`start`・`character`・`spells` など）。無ければ空。
    std::string type;
    //! 窓が出している問いの文（下の案内の行・問いの帯）。無ければ空。
    std::string prompt;
    bool has_rect{ false };
    ProbeRect rect;
    //! コアが付ける窓の版（変わるたびに増える）。窓の版を持たない窓は 0。
    int revision{ 0 };
    std::vector<ProbeItem> items;
    int selected{ -1 };
    bool has_numeric{ false };
    int numeric_value{ 0 };
    int numeric_min{ 0 };
    int numeric_max{ 0 };
    //! 窓に出している所持金（店・持ち物の窓）。無ければ -1。
    int gold{ -1 };
};

//! 玉・アイコンのうち、出したアイコン 1 つ。
struct ProbeIcon {
    std::string key;
    std::string label;
    std::string amount;
    int icon_id{ 0 };
    bool permanent{ false };
    bool bad{ false };
};

//! 画面が描いた地形・実体の 1 マス（既知のマスだけ）。
struct ProbeCell {
    int gx{ 0 };
    int gy{ 0 };
    //! `floor`・`wall`・`permanent-wall`・`door`・`door-open`・`stairs`・`tree`・`water`・`lava`・`rubble`・`other`。
    const char *kind{ "other" };
    char glyph{ ' ' };
    //! 扉の字が英数字なら、その字（店・建物の入口の番号）。無ければ 0。
    char entrance{ 0 };
    int monster{ 0 };
    int object{ 0 };
    /*!
     * @name 詳細（`{"query":"ui","detail":true}` のときだけ出す欄）
     * @{
     */
    //! コアが送った地形の番号（`MapCellView::terrain_id`）。
    int terrain_id{ 0 };
    //! 絵の索引（`MapCellView::tile_index`。0 は無し）。名前は `UiProbe::map_tile_names` で引く。
    int tile_index{ 0 };
    //! コアが送った前景の色の番号（TERM_COLOR）と、色表で引いた RGB。
    int fg_color{ 0 };
    int fg_rgb[3]{ 0, 0, 0 };
    int light_level{ 0 };
    //! プレイヤの光源で照らされているマスか（コアが `player_lit` を送るときだけ意味がある）。
    bool lit{ false };
    //! 町の意味づけ（`TownRole` の名前。町でなければ空）と、敷地の番号（-1 は敷地でない）。
    const char *role{ "" };
    int site{ -1 };
    /*! @} */
};

//! 光の届く範囲（画面の光の計算の結果）。
struct ProbeLight {
    bool present{ false };
    //! コアが照らされたマスを送っているか（`lighting.player_lit` が空でない）。偽ならマス数は 0 とする。
    bool declared{ false };
    //! 光源の種類（0 無し・1 松明・2 ランタン・3 アーティファクト・4 それ以外・5 暗黒側）。
    int kind{ 0 };
    //! コアの言うプレイヤの光源の半径（`lighting.light_radius`）。
    int radius{ 0 };
    //! 照らされるマス（コアが送った目標）の数と、画面がいま照らして描いているマス（なめらかな値が 0.5 以上）の数。
    int lit_cells{ 0 };
    int shown_cells{ 0 };
    //! 照らされたマスの、プレイヤからの最大の距離（ユークリッド距離を切り上げ）。
    int max_distance{ 0 };
    //! 記憶のマスを暗くする申告があるか（Rogue 系）と、その暗くしているマスの数。
    bool memory_dim{ false };
    int memory_cells{ 0 };
};

//! 狙い・見るの詳細（`targeting` の追加の欄）。
struct ProbeTargetingDetail {
    bool present{ false };
    //! 見る・狙いの帯（コアの問いを画面が帯にしたもの）が出ているか。窓（`TargetingUi`）が無いコアの見る `l` はこちら。
    bool band{ false };
    std::string band_text;
    //! 窓（`TargetingUi`）が出ているか。
    bool ui_active{ false };
    //! 窓の mode（`point`・`direction8`・`creature`・`fixed`・`look`）と kind（`attack`・`support`・`neutral`）。
    std::string ui_mode;
    std::string ui_kind;
    bool has_selected{ false };
    int selected_x{ 0 };
    int selected_y{ 0 };
    //! いま指しているものの種類：`monster`（敵）・`player`（自分）・`free`（敵のいない自由な位置）・`object`・`feature`・`trap`。
    std::string selected_kind;
    int selected_id{ 0 };
    int source_x{ 0 };
    int source_y{ 0 };
    int distance{ 0 };
    bool can_confirm{ false };
    bool extras_open{ false };
    int candidates{ 0 };
    int markers{ 0 };
    //! 効果の範囲のマス（玉の半径・ビームの経路。`possible` はあるかもしれないマス）。
    struct Cell {
        int x{ 0 };
        int y{ 0 };
        bool possible{ false };
    };
    std::vector<Cell> area;
    //! 弾道の経路のマス。
    std::vector<Cell> path;
};

//! 一時的な重ね書きの字（Sil-Q の聞き耳の `*`・ダメージの数字など）。
struct ProbeOverlay {
    int gx{ 0 };
    int gy{ 0 };
    std::string text;
    int color{ 1 };
};

//! 描いている実体 1 つ（`ring` は警戒度の輪）。
struct ProbeEntity {
    int gx{ 0 };
    int gy{ 0 };
    //! `monster`・`object`・`player`。
    const char *kind{ "monster" };
    int id{ 0 };
    int slot{ 0 };
    int tile_index{ 0 };
    //! 警戒度：空（コアが言わない）・`sleep`・`unaware`・`alert`。
    const char *alert{ "" };
    //! 警戒度の輪を画面が描いたか。
    bool ring_drawn{ false };
};

//! 窓の焦点・大きさ。
struct ProbeWindowState {
    bool present{ false };
    //! SDL が言う入力の焦点（`SDL_WINDOW_INPUT_FOCUS`）と、マウスの焦点。
    bool input_focus{ false };
    bool mouse_focus{ false };
    bool shown{ true };
    bool minimized{ false };
    //! 検査用の隠した窓（`HD2D_TEST_HIDE_WINDOW`）か。隠した窓では、パッドの方向入力は焦点ありとして扱う。
    bool test_hidden{ false };
    //! パッドの方向入力が焦点ありと見なされるか（`input_focus` か、検査用の隠した窓）。
    bool pad_focus{ false };
    int w{ 0 };
    int h{ 0 };
};

//! 昼夜（画面が光の計算に使った時刻）。
struct ProbeTimeState {
    bool present{ false };
    int day_minute{ 720 };
    bool daytime{ true };
    //! 町の灯りの夜の度合い（0 昼〜1 夜。`lamp_night_factor`）。
    float night_factor{ 0.f };
};

//! マス → 窓の画素（そのフレームのカメラで写す）。写せたら true。`inside` は地図の矩形の中か。
using ProbeCellProjector = std::function<bool(int gx, int gy, int &sx, int &sy, bool &inside)>;

class UiProbe {
public:
    //! 検査用の起動（`--test-input-file=`）でだけ真にする。
    void enable(bool on) { this->enabled_ = on; }
    bool enabled() const { return this->enabled_; }

    //! @name 記録（無効のときは何もしない）
    //! @{
    void text(int x, int y, int w, int h, std::string_view utf8);
    //! 色・切れ・字形の欠けつき。
    void text(int x, int y, int w, int h, std::string_view utf8, const ProbeTextStyle &style);
    void button(std::string label, const ProbeRect &rect, bool enabled = true, bool selected = false,
        const char *kind = "", bool touch_only = false, std::string id = {});
    void window(ProbeWindow window);
    //! HP・MP の値（玉が出している数字）。`orb` は玉の絵で出したか（偽なら棒）。
    void hud_values(int hp, int hp_max, int sp, int sp_max, bool has_sp, bool orb, std::string design);
    //! 玉（棒）が実際に描いた数字の文字（`20 / 20`）。
    void hud_number_text(bool hp, std::string text);
    void hud_icon(ProbeIcon icon);
    void hud_doomsday(int days_left);
    //! 名前の行（名前・レベル・所持金）。画面が描いた値。無い項目は負／空。
    void hud_name(std::string name, int level, int gold);
    //! 階層の文字（地図の隣に描いたもの）。
    void hud_depth(std::string text);
    //! 地図：プレイヤーのマスと、描いている実体の数。`view` は `3d`・`2d`・`fps`。
    void map(int player_gx, int player_gy, int players, int monsters, int objects, bool visible, std::string view);
    /*!
     * @brief 画面が描いた地形・実体（ビューポートの既知のマス）と、フロア全域の種別の格子（ミニマップ）。
     * @param floor_key 階の同一性の鍵（変わると、覚えている入口の番号を捨てる）。
     * @param floor_rows ミニマップを 1 行 1 文字列にしたもの（` `未踏・`.`床・`#`壁・`+`扉・`>`階段・`!`アイテム・`m`敵・`@`自分・`^`山・`~`水・`T`木・`"`草・`L`溶岩・`s`雪）。
     */
    void map_cells(std::vector<ProbeCell> cells, std::string floor_key, std::vector<std::string> floor_rows);
    //! マス → 窓の画素の写し方（そのフレームのカメラのコピーを持たせる）。
    void set_cell_projector(ProbeCellProjector projector);
    //! 攻撃の効果を起こした（フレームが届いたとき。短命なので、開始からの累計と直近の記録を残す）。
    void fx_started(std::string attr, std::string shape, std::string kind);
    //! いま描いている攻撃の効果の属性の名前。
    void fx_active(std::vector<std::string> names);
    //! メッセージ行に出した字（行ごと。呼ぶたびに置き換える）。
    void messages(std::vector<std::string> lines);
    //! 画面が出している通知（「状況が変わりました…」など）。出た回数は問い合わせの `count` に累計で出る。
    void notice(std::string text);
    //! 狙い・見る：`mode` は `look`・`target`・`direction`。
    void targeting(bool active, std::string mode, std::string label);
    //! 狙い・見るの詳細（いま指しているものの種類・効果の範囲のマス・帯の字）。
    void targeting_detail(ProbeTargetingDetail detail);
    //! 光の届く範囲（画面の光の計算の結果）。
    void light(const ProbeLight &light);
    //! 一時的な重ね書きの字（コアが送ったもの）と、画面が描いた数。
    void overlays(std::vector<ProbeOverlay> overlays, int drawn);
    //! 描いている実体（敵・アイテム・自分）。警戒度の輪つき。
    void entities(std::vector<ProbeEntity> entities, int rings_drawn);
    //! 窓の焦点・大きさ。
    void window_state(const ProbeWindowState &state);
    //! 昼夜。
    void time_state(const ProbeTimeState &state);
    //! 階の案内（カットイン）の段（`pending`・`in`・`wait`・`out`・`reveal`）。幕が張られていない間は呼ばない（空のまま）。
    void cutin_stage(std::string stage);
    //! `map.cells.list[]` の詳細で使った絵の索引 → 名前（`K:123:file` の形。種類・番号・ファイル名）。
    void map_tile_names(std::map<int, std::string> names);
    //! コアの生死（描いた時点）。`exit_code` は終了を受けていなければ負。
    void liveness(bool core_alive, int exit_code);
    //! フレーム全体の情報。`input_wait` は入力待ちの種類（`command`・`prompt`・`yesno`・`more`・`number`・`text`・
    //! `cutin`・`death`・`none`＝コアが処理中）。`phase` は画面の大きな状態（`title`・`birth`・`game` など）。
    void frame_info(std::string core, std::uint64_t frame_seq, int screen_w, int screen_h, std::string phase,
        std::string input_wait);
    //! @}

    /*!
     * @brief 記録中のフレームを確定する。何も記録されていなければ、前の確定を残す。
     * @param poll_phase 呼んだ側の相（`core-select`・`game`・`floor-cutin`・`lineage` ほか）。
     */
    void commit(std::string_view poll_phase);

    /*!
     * @brief 確定したフレームを JSON 1 つにして返す（`{"query":"ui"}` の応答の `ui`）。
     * @param poll_phase いまの相。`game` のとき、画面が決めた細かい相（`title`・`birth`）があればそれを返す。
     */
    std::string snapshot_json(std::string_view poll_phase, std::string_view map_mode = "near", bool detail = false) const;
    /*!
     * @brief マス (gx,gy) の画面座標を JSON で返す（`{"query":"cell","x":..,"y":..}`）。
     * @details `{gx, gy, projectable, on_screen, screen:{x,y}|null, kind, glyph, entrance}`。
     */
    std::string cell_json(int gx, int gy) const;

    //! `tap` の探し方。
    struct TapQuery {
        std::string label;
        bool contains{ false };
        int index{ -1 }; //!< -1 は指定なし
        std::string kind; //!< 空は指定なし
        std::string id; //!< 空は指定なし（言語に依存しない名前で探すとき。`label` は無視）
    };
    /*!
     * @brief 確定したフレームの押せる部品から 1 つ探す。
     * @return 見つかれば true。見つからない・曖昧（`index` 指定なしで複数）なら false で `error` に理由。
     */
    bool find_button(const TapQuery &query, ProbeButton &out, std::string &error) const;
    int screen_w() const { return this->last_.screen_w; }
    int screen_h() const { return this->last_.screen_h; }
    //! 確定したフレームがあるか。
    bool has_frame() const { return this->last_.valid; }
    //! 検査用：確定した部品の数など。
    std::size_t button_count() const { return this->last_.buttons.size(); }
    std::size_t text_count() const { return this->last_.texts.size(); }

    //! いま記録している層（`UiProbeLayer` が入れ替える）。
    const char *layer() const { return this->layer_; }
    void set_layer(const char *layer) { this->layer_ = layer; }

    //! @name ホイールの位置（試験入力の `{"wheel":{...,"at":[x,y]}}`）
    //! 原作側のホイールの処理は実マウスの位置を読むので、試験では位置を差し替える。次の `poll()` で消える。
    //! @{
    void set_wheel_at(int x, int y)
    {
        this->wheel_at_valid_ = true;
        this->wheel_x_ = x;
        this->wheel_y_ = y;
    }
    void clear_wheel_at() { this->wheel_at_valid_ = false; }
    bool wheel_at(int &x, int &y) const
    {
        if (this->wheel_at_valid_) {
            x = this->wheel_x_;
            y = this->wheel_y_;
        }
        return this->wheel_at_valid_;
    }
    //! @}

private:
    struct Hud {
        bool present{ false };
        int hp{ 0 }, hp_max{ 0 }, sp{ 0 }, sp_max{ 0 };
        bool has_sp{ false };
        bool orb{ false };
        std::string design;
        std::string hp_text, sp_text;
        bool has_name{ false };
        std::string name;
        int level{ -1 }, gold{ -1 };
        std::string depth;
        std::vector<ProbeIcon> icons;
        int doomsday_days{ -1 };
    };
    struct Map {
        bool present{ false };
        int gx{ 0 }, gy{ 0 };
        int players{ 0 }, monsters{ 0 }, objects{ 0 };
        bool visible{ false };
        std::string view;
    };
    struct Frame {
        bool valid{ false };
        std::vector<ProbeText> texts;
        std::vector<ProbeButton> buttons;
        std::vector<ProbeWindow> windows;
        std::vector<std::string> messages;
        std::vector<std::string> notices;
        std::vector<std::string> fx_names;
        Hud hud;
        Map map;
        std::vector<ProbeCell> cells;
        std::string floor_key;
        std::vector<std::string> floor_rows;
        ProbeCellProjector projector;
        bool has_targeting{ false };
        bool targeting_active{ false };
        std::string targeting_mode, targeting_label;
        ProbeTargetingDetail targeting_detail;
        ProbeLight light;
        std::vector<ProbeOverlay> overlays;
        int overlays_drawn{ 0 };
        std::vector<ProbeEntity> entities;
        int rings_drawn{ 0 };
        ProbeWindowState window_state;
        ProbeTimeState time_state;
        std::string cutin_stage;
        std::map<int, std::string> tile_names;
        bool has_liveness{ false };
        bool core_alive{ true };
        int core_exit_code{ -1 };
        std::string core;
        std::uint64_t frame_seq{ 0 };
        int screen_w{ 0 }, screen_h{ 0 };
        std::string phase;
        std::string input_wait;
        std::string poll_phase;
        std::uint64_t draw_seq{ 0 };
    };
    struct FxStart {
        std::int64_t at_ms{ 0 };
        std::string attr, shape, kind;
    };

    bool enabled_{ false };
    const char *layer_{ "" };
    Frame cur_;
    Frame last_;
    bool touched_{ false };
    std::uint64_t draw_seq_{ 0 };
    //! 起動からの累計と直近（フレームの確定とは別に持つ。効果は 1 フレームより短く終わる）。
    std::uint64_t fx_started_total_{ 0 };
    std::vector<FxStart> fx_recent_;
    //! 通知ごとの、出た回数（消えてからまた出たら増える）。
    std::map<std::string, int> notice_counts_;
    //! コアのフレームが最後に変わった時刻・入力待ちの種類が最後に変わった時刻（ミリ秒）。
    std::uint64_t last_frame_id_{ 0 };
    std::int64_t frame_changed_ms_{ 0 };
    std::string wait_kind_;
    std::int64_t wait_since_ms_{ 0 };
    //! 字の欠け・はみ出しの累計（開始から。同じ字は 1 回と数える）と、見つけた字の見本（先頭から 40 件まで）。
    struct TextIssue {
        std::string kind;
        std::string text;
        std::string layer;
        int x{ 0 };
        int y{ 0 };
        int w{ 0 };
        int h{ 0 };
    };
    std::map<std::string, int> issue_totals_;
    std::vector<TextIssue> issue_samples_;
    std::set<std::string> issue_seen_;
    //! 最新のフレームの字の問題の数（`commit` で数える）。
    std::map<std::string, int> issue_last_;
    //! 最新のフレームの字の問題を、レイヤー（`texts[].layer`）別に数えたもの。
    std::map<std::string, std::map<std::string, int>> issue_layers_;
    //! 階ごとに覚える入口の番号（ビューポートの外へ出ても残る）。
    std::string entrance_floor_;
    std::map<std::pair<int, int>, char> entrances_;
    bool wheel_at_valid_{ false };
    int wheel_x_{ 0 }, wheel_y_{ 0 };
};

//! プロセスに 1 つ。
UiProbe &ui_probe();

//! 描く間だけ、文字・部品に付ける層の名前を入れ替える（文字列リテラルを渡すこと）。
class UiProbeLayer {
public:
    explicit UiProbeLayer(const char *name) : saved_(ui_probe().layer()) { ui_probe().set_layer(name); }
    ~UiProbeLayer() { ui_probe().set_layer(this->saved_); }
    UiProbeLayer(const UiProbeLayer &) = delete;
    UiProbeLayer &operator=(const UiProbeLayer &) = delete;

private:
    const char *saved_;
};

} // namespace hd2d
