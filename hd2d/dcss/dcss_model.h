/*!
 * @file dcss_model.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）の画面の部品が持つ状態。原作のウェブタイル版の JSON から作る。
 *
 * ## 何のためのものか
 * 設計の材料。握手で `dcss-ui` を申告したコアは、原作がウェブ版の画面へ送る JSON を
 * 加工せずに `{"t":"webtiles","line":"…"}` で送ってくる。
 * ここはその行を 1 行ずつ受けて、ステータス欄・メッセージ欄・ミニマップが要る分だけを覚える。
 * 描画にも通信にも触れない（単体で検査できる）。
 *
 * ## 覚えるもの（段 U1）
 * - `player`（差分。合わせて覚える）: 名前・称号・種族・神・体力・魔力・防御・能力値・場所・時間・状態の灯・武器・矢筒ほか
 * - `msgs`: メッセージの列（`rollback` で後ろから取り消す）・`-more-`（`more`・`more_text`）
 * - `map`: ミニマップの種別（`mf`）。座標は原作が送る**相対座標**のまま（ミニマップは「見えたマスの外接の矩形」を
 *   中央に寄せて描くので、絶対の座標は要らない）。階が変わる（`clear`）と空にする
 * - `options`: 体力・魔力・能力値の色の段、ミニマップの色
 * - `input_mode`・`ui_state`・`text_cursor`・`version`・`layout`
 */
#pragma once

#include "dcss/dcss_cells.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hd2d::dcss {

//! 原作 `map_feature`（`map-feature.h`）。`mf` の値。
enum DcssMapFeature : int {
    kMfUnseen = 0,
    kMfFloor,
    kMfWall,
    kMfMapFloor,
    kMfMapWall,
    kMfDoor,
    kMfItem,
    kMfMonsFriendly,
    kMfMonsPeaceful,
    kMfMonsNeutral,
    kMfMonsHostile,
    kMfMonsNoExp,
    kMfStairUp,
    kMfStairDown,
    kMfStairBranch,
    kMfFeature,
    kMfWater,
    kMfLava,
    kMfTrap,
    kMfExclRoot,
    kMfExcl,
    kMfPlayer,
    kMfDeepWater,
    kMfPortal,
    kMfTransporter,
    kMfTransporterLanding,
    kMfExploreHorizon,
    kMfCount,
};

//! 原作 `input_mode`（`defines.h`。`mouse_control` の種類）。
enum DcssInputMode : int {
    kInputNormal = 0,
    kInputCommand = 1,
    kInputTarget = 2,
    kInputTargetDir = 3,
    kInputTargetPath = 4,
    kInputMore = 5,
    kInputMacro = 6,
    kInputPrompt = 7,
    kInputYesNo = 8,
};

//! 状態の灯（`player.status[]`）。
struct DcssStatusLight {
    std::string light; //!< 灯の字
    std::string text;
    int colour{ kLightGrey };
};

//! 持ち物 1 枠（武器の名前だけに使う）。
struct DcssInvItem {
    int quantity{ 0 };
    int letter{ 0 };
    int colour{ -1 };
    std::string name;
};

//! `player` メッセージの内容（差分を合わせたもの）。
struct DcssPlayer {
    bool seen{ false }; //!< 名前入りの `player` を受けた（ゲームに入った）
    std::string name;
    std::string title;
    std::string species;
    std::string god;
    std::string place;
    int depth{ 0 };
    int wizard{ 0 };
    int explore{ 0 };
    int penance{ 0 };
    int piety_rank{ 0 };
    int ostracism_pips{ 0 };
    int form{ 0 };
    int hp{ 0 };
    int hp_max{ 0 };
    int real_hp_max{ 0 };
    int mp{ 0 };
    int mp_max{ 0 };
    int dd_real_mp_max{ 0 };
    int poison_survival{ 0 };
    int ac{ 0 };
    int ev{ 0 };
    int sh{ 0 };
    int ac_mod{ 0 };
    int ev_mod{ 0 };
    int sh_mod{ 0 };
    int str{ 0 };
    int intel{ 0 };
    int dex{ 0 };
    int doom{ 0 };
    int contam{ 0 };
    int xl{ 0 };
    int progress{ 0 };
    int gold{ 0 };
    int noise{ -1 };
    int adjusted_noise{ 0 };
    int time{ 0 };
    int turn{ 0 };
    int weapon_index{ -1 };
    int offhand_index{ -1 };
    std::string quiver_desc;
    std::string unarmed_attack;
    int unarmed_attack_colour{ kLightGrey };
    std::vector<DcssStatusLight> status;
    std::unordered_map<int, DcssInvItem> inv;
    //! 変わるたびに増える（描き直しの合図）。
    std::uint64_t version{ 0 };
};

//! 1 メッセージ（原作は 1 つの `msgs.messages[]` の要素）。
struct DcssMessage {
    std::string text; //!< 色のタグつき
    int turn{ 0 };
    int channel{ 0 };
};

//! `options` の中の、画面に効く分。既定は原作 `initfile.cc`。
struct DcssOptions {
    std::vector<std::pair<int, int>> hp_colour{ { 70, kYellow }, { 40, kRed } };
    std::vector<std::pair<int, int>> mp_colour{ { 50, kYellow }, { 25, kRed } };
    std::vector<std::pair<int, int>> stat_colour{ { 3, kRed } };
    //! ミニマップの色（`tile_*_col`。`DcssMapFeature` で引く）。
    std::array<DcssRgb, kMfCount> map_colour{};
    DcssRgb window_colour{ 85, 136, 85 }; //!< `tile_window_col`
    bool show_game_time{ true };
    bool always_show_doom_contam{ false };
    int status_caption_colour{ kBrown };
};

class DcssUiModel {
public:
    DcssUiModel();

    /*!
     * @brief `webtiles` の 1 行（原作の JSON の字。先頭 `*` はサーバ宛の制御なので捨てる）を取り込む。
     * @return 状態が変わったか（読めない行・使わない型は偽）
     */
    bool ingest_line(const std::string &line);

    const DcssPlayer &player() const { return this->player_; }
    const DcssOptions &options() const { return this->options_; }
    int input_mode() const { return this->input_mode_; }
    int ui_state() const { return this->ui_state_; }
    bool text_cursor() const { return this->text_cursor_; }
    //! 全体地図の画面（`X`）か。
    bool map_screen() const { return this->ui_state_ == 2; }

    //! メッセージ（古い順。最大 `kMaxMessages`）。`message_serial` はこれまでに受けた数（取り消しで減らない）。
    const std::vector<DcssMessage> &messages() const { return this->messages_; }
    std::uint64_t message_serial() const { return this->message_serial_; }
    //! 取り消し（`rollback`）の通し数。メッセージ欄が作り直すかの判定に使う。
    std::uint64_t rollback_serial() const { return this->rollback_serial_; }
    bool more() const { return this->more_; }
    const std::string &more_text() const { return this->more_text_; }
    static constexpr std::size_t kMaxMessages = 400;

    //! 階が変わった（`map.clear`）回数。
    std::uint64_t level_serial() const { return this->level_serial_; }
    //! 直前の指令待ちに入った時刻の `player.time`（`時間: x.x (y.y)` の括弧の中の計算に使う）。
    int time_at_last_input() const { return this->time_at_last_input_; }
    int time_at_prev_input() const { return this->time_at_prev_input_; }

    //! ミニマップ。外接の矩形（見えたマスだけ）。無ければ偽。
    struct MapBounds {
        int min_x{ 0 };
        int min_y{ 0 };
        int max_x{ 0 };
        int max_y{ 0 };
        bool valid{ false };
    };
    MapBounds map_bounds() const;
    //! (x, y) のミニマップの種別（原作 `update_minimap` と同じ: 自分のマスは `kMfPlayer`、床・水・溶岩のうち除外したマスは `kMfExclRoot`・`kMfExcl`）。
    int map_feature_at(int x, int y) const;
    //! 見えたマスを全部渡す（x, y, 上の種別）。
    template <typename F>
    void for_each_map_cell(F &&f) const
    {
        for (const auto &entry : this->map_) {
            const int x = static_cast<int>(entry.first >> 32);
            const int y = static_cast<int>(static_cast<std::int32_t>(entry.first & 0xFFFFFFFFu));
            f(x, y, this->effective_feature(x, y, entry.second));
        }
    }
    //! プレイヤーのマス（相対座標。`player.pos`）。
    int player_x() const { return this->player_x_; }
    int player_y() const { return this->player_y_; }
    bool player_pos_known() const { return this->player_pos_known_; }
    //! 地図の見える範囲の中心（`vgrdc`。相対座標）。
    int view_centre_x() const { return this->view_x_; }
    int view_centre_y() const { return this->view_y_; }
    //! ミニマップの内容が変わるたびに増える。
    std::uint64_t map_version() const { return this->map_version_; }

private:
    static std::int64_t key(int x, int y)
    {
        return (static_cast<std::int64_t>(x) << 32) | static_cast<std::int64_t>(static_cast<std::uint32_t>(y));
    }
    //! 内部の値（`mf` | 除外の印 << 5）から、描く種別を決める。
    int effective_feature(int x, int y, std::uint8_t stored) const;
    void on_player(const void *json);
    void on_msgs(const void *json);
    void on_map(const void *json);
    void on_options(const void *json);

    DcssPlayer player_;
    DcssOptions options_;
    int input_mode_{ 0 };
    int ui_state_{ 0 };
    bool text_cursor_{ false };
    std::vector<DcssMessage> messages_;
    std::uint64_t message_serial_{ 0 };
    std::uint64_t rollback_serial_{ 0 };
    bool more_{ false };
    std::string more_text_;
    std::uint64_t level_serial_{ 0 };
    int time_at_last_input_{ 0 };
    int time_at_prev_input_{ 0 };
    std::unordered_map<std::int64_t, std::uint8_t> map_;
    int player_x_{ 0 };
    int player_y_{ 0 };
    bool player_pos_known_{ false };
    int view_x_{ 0 };
    int view_y_{ 0 };
    std::uint64_t map_version_{ 0 };
};

/*!
 * @brief ステータス欄・メッセージ欄に使う、日本語の名前（原作の日本語版の辞書から作った表）。
 * @details 種族・神は英語の名前で来る。`jiyva` の 2 つ目の名前（`Jiyva Xxxx`）は 1 つ目だけ訳して残りをつなぐ。
 * 表に無ければ英語のまま返す。
 */
std::string dcss_species_name_ja(std::string_view english);
std::string dcss_god_name_ja(std::string_view english);
/*!
 * @brief 場所の短い名前（原作 `branches[].shortname`。1 階層だけの枝には冠詞がつく）を訳す。
 */
std::string dcss_branch_name_ja(std::string_view english);

} // namespace hd2d::dcss
