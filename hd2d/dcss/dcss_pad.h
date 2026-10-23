/*!
 * @file dcss_pad.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）のゲームパッド。原作の日本語版のタイル版（`gamepad.cc`・`windowmanager-sdl.cc`）の割り当てと動きを、画面の側で再現する。段 5。
 *
 * ## 何のためのものか
 * 決めたこと D11・D12: パッドの操作は原作の日本語版のタイル版に合わせる。原作のパッド対応は SDL の層（`windowmanager-sdl.cc`）と `gamepad-menu.cc` にあり、
 * どちらも `USE_TILE_LOCAL` の中だけで、このコア（`USE_TILE_WEB`）には組まれない。そこで の案 A のとおり、
 * 画面の側の `GamePad`・`PadChord`・`VirtualPad` が出す「押し 1 回」を、このコア専用の割り当て表で原作の入力に変える。
 *
 * | 押し | 出すもの（原作 `_pad_output_to_key`） |
 * | --- | --- |
 * | 方向 | 矢印キー。斜めは Home・PgUp・End・PgDn。窓（メニュー）の中は縦の成分だけ |
 * | 単独の A・B | 決定（Enter）・取消（Esc）。コマンド待ちでは何も出さない（無視するキー `CK_NO_KEY` を送り、自動探索などを止めるだけ） |
 * | 単独の L3・R3 | 画面の疑似カーソルの位置での左・右クリック |
 * | 割り当てたコマンド | コマンドを待っているときだけ `-(CMD_*)` のキー（キーマップを通らない）。ほかの場面では `CK_NO_KEY` |
 * | 割り当てなし | 枠ごとのキー `CK_PAD_BASE(-5000) - 枠`（マクロ・キーマップのトリガーになる） |
 *
 * 画面の側の機能（HD2D の機能メニュー・視点の回転）は、原作が使っていない入力（同時押しの層）だけに置く。
 *
 * ## 持ち物
 * - コアから届くコマンドの表（番号・`CMD_` の名前・いまのキー。`dcss/adapter/dcss_pad_data.h`）と、名前・分類の表（`dcss_pad_cmds.inc`）
 * - 割り当て（遊ぶ人が変えた分だけ。HD2D の設定 `dcss_pad_binds` に保存する）
 * - 右スティックで動かす疑似カーソル（原作の `cursor_motion` と同じ曲線）
 * - 下の操作の案内の中身
 *
 * 描画にも通信にも触れない（単体で検査できる）。
 */
#pragma once

#include "dcss/dcss_model.h"
#include "ui/game_pad.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hd2d::dcss {

//! 割り当ての枠の番号（原作 `gamepad.h` の `pad_slot`。層ごとに 16 ずつ空ける。ボタンの並びは `PadInput` と同じ）。
constexpr int kPadSlotsPerLayer = 16;
constexpr int kPadLayerCount = 4; //!< 単独・LB+・RB+・LB+RB+
constexpr int kPadSlotCount = kPadSlotsPerLayer * kPadLayerCount;
inline int dcss_pad_slot(PadInput button, int mods) { return (mods * kPadSlotsPerLayer) + static_cast<int>(button); }
inline PadInput dcss_pad_slot_button(int slot) { return static_cast<PadInput>(slot % kPadSlotsPerLayer); }
inline int dcss_pad_slot_mods(int slot) { return slot / kPadSlotsPerLayer; }
//! その層でそのボタンが枠になるか（LB・RB の押しは層 0 だけ。押しながら押せないため）。
bool dcss_pad_slot_valid(PadInput button, int mods);
//! 単独押しで固定の役目を持ち、割り当てられない枠か（A = 決定、B = 取消、L3 = 左クリック、R3 = 右クリック）。
bool dcss_pad_slot_fixed(PadInput button, int mods);
//! 枠の綴り（`X`・`LB+X`・`RB+X`・`LB+RB+X`。原作の `pad_slot_name` と同じ）。
std::string dcss_pad_slot_name(int slot);
//! 綴りから枠の番号。読めなければ -1。
int dcss_pad_parse_slot(const std::string &name);

//! 疑似カーソルが積む SDL のマウスの出来事の `which`（本物のマウス = 0、タッチの写し = `SDL_TOUCH_MOUSEID` と区別する印）。
constexpr std::uint32_t kPadMouseId = 0x50AD0001u;

//! 原作のキー符号（`cio.h`）のうち、パッドが使う物。
constexpr int kPadKeyEnter = 13;
constexpr int kPadKeyEscape = 27;
constexpr int kPadKeyUp = -254;
constexpr int kPadKeyDown = -253;
constexpr int kPadKeyLeft = -252;
constexpr int kPadKeyRight = -251;
constexpr int kPadKeyHome = -249;
constexpr int kPadKeyEnd = -248;
constexpr int kPadKeyPgUp = -246;
constexpr int kPadKeyPgDn = -245;
constexpr int kPadKeyNoKey = -9989; //!< `CK_NO_KEY`（無視するキー。`kbhit` を真にするだけで読み捨てられる）
constexpr int kPadKeyPadBase = -5000; //!< `CK_PAD_BASE`（割り当ての無い枠のキーの始まり）

/*!
 * 画面の側の機能（原作にはない物）。割り当て表の中では負の番号で持つ。
 * 原作が使っていない入力（同時押しの層）にだけ既定で置く。
 */
constexpr int kPadCmdHd2dMenu = -1001; //!< HD2D の機能メニュー（設定・言語・終了）
constexpr int kPadCmdTurnLeft = -1002; //!< 視点を左へ回す
constexpr int kPadCmdTurnRight = -1003; //!< 視点を右へ回す
constexpr int kPadCmdBindings = -1004; //!< ゲームパッドの割り当ての画面を開く（原作の `CMD_GAMEPAD_MENU`。このコアの原作の組み立てには無いので画面の機能にした。保存の綴りは原作と同じ）
//! 遊ぶ人の割り当てが無い（既定に従う）印。保存・読み込みの内部だけで使う。
constexpr int kPadUseDefault = -9999;

//! パッドの押しが向かう場面（原作の `_pad_in_command_context`・`_pad_in_menu` に当たる）。
enum class DcssPadScene {
    Command, //!< コマンド待ち（窓が無く、`input_mode` が 1）
    Window, //!< 窓（メニュー・説明・文字画面）か文字入力が開いている。キーは画面の窓の部品が受ける
    Target, //!< 狙い・見回し・地図の画面（`input_mode` 2〜4、全体地図）。斜めのまま動かせる
    Other, //!< `-more-`・問い・はい／いいえなど。決定・取消はそのまま Enter・Esc
};

//! 押しや方向の結果として、画面が行うこと。
struct DcssPadAction {
    enum class Kind {
        None,
        CoreKey, //!< コアへ `{"msg":"key","keycode":key}` を送る
        WindowKey, //!< 窓の部品へ SDL のキー `sym` を渡す（窓がコアへの送りを決める）
        Click, //!< 疑似カーソルの位置でクリック（`button` = 1 左・3 右）
        FeatureMenu, //!< HD2D の機能メニューを開く
        TurnView, //!< 視点を回す（`turn` = -1 左・+1 右）
        OpenBindings, //!< 割り当ての画面を開く
        SlotCapture, //!< 割り当ての画面が開いている間に押された枠（`slot`）
    };
    Kind kind{ Kind::None };
    int key{ 0 };
    int sym{ 0 };
    int button{ 0 };
    int turn{ 0 };
    int slot{ -1 };
};

//! コアが送るコマンドの表の 1 件（`{"t":"dcss_ui","k":"pad"}`）。
struct DcssPadCommand {
    int id{ 0 };
    std::string name; //!< `CMD_EXPLORE`
    std::string key; //!< いまのキーの名前（`o`。無ければ空）
};

//! 割り当ての画面の分類の 1 つ。
struct DcssPadGroup {
    std::string name;
    std::vector<int> ids; //!< コマンドの番号（画面の機能は負）
};

class DcssPad {
public:
    //! 表示の言語（`ja` / `en`）。
    void set_language(const std::string &lang) { this->japanese_ = (lang != "en"); }
    bool japanese() const { return this->japanese_; }

    // ---- コアのコマンドの表 ----
    //! `{"t":"dcss_ui","k":"pad","cmds":[[番号,"CMD_…","キー"],…]}` を取り込む。読めたら真。
    bool ingest_table(const nlohmann::json &message);
    bool table_ready() const { return !this->commands_.empty(); }
    const std::vector<DcssPadCommand> &commands() const { return this->commands_; }
    //! `CMD_` の名前から番号。無ければ 0。
    int id_for_name(const std::string &name) const;
    //! 番号から名前。画面の機能や表に無い番号は空。
    std::string name_for_id(int id) const;

    // ---- 名前 ----
    //! コマンドの表示名（日本語版の辞書の名前。表に無ければ `CMD_` の名前から作る）。画面の機能も含む。
    std::string label(int id) const;
    //! 「自動探索 (o)」の形（キーが無ければ名前だけ）。
    std::string label_with_key(int id) const;
    //! 分類ごとの一覧（画面の機能の分類を最後に足す）。コアの表が届くまでは空。
    std::vector<DcssPadGroup> groups() const;
    //! 層の名前（単独押し・LB を押しながら …）。
    std::string layer_name(int mods) const;

    // ---- 割り当て ----
    //! 既定（原作の `gamepad.cc` の既定と、画面の機能の既定）。0 = 無し。コアの表が届くまでは名前が引けないので 0。
    int default_command(int slot) const;
    //! 遊ぶ人が変えた値か。
    bool overridden(int slot) const { return this->user_.count(slot) != 0; }
    //! いまの割り当て（0 = 割り当てなし。遊ぶ人の値を先に、無ければ既定）。
    int command(int slot) const;
    //! 遊ぶ人の割り当てにする（`cmd` = 0 は「割り当てなし」）。`kPadUseDefault` で既定へ戻す。固定の枠は断って偽。
    bool set_command(int slot, int cmd);
    void clear_overrides() { this->user_.clear(); this->changed_ = true; }
    bool has_overrides() const { return !this->user_.empty(); }
    //! 設定（`dcss_pad_binds`）の形へ。`X:CMD_PICKUP,LB+Y:none,LB+RB+Back:HD2D_MENU`。
    std::string save_line() const;
    //! 同じ形から読む。読めない項目は黙って飛ばす。コアの表が届くまで名前が引けない物は控えて、届いたとき当てる。
    void load_line(const std::string &line);
    //! 割り当てが変わったか（保存の合図。取り出すと偽に戻る）。
    bool take_changed()
    {
        const bool c = this->changed_;
        this->changed_ = false;
        return c;
    }

    // ---- 入力 ----
    /*!
     * @brief ボタンの押し 1 回を、画面が行うことにする（原作の `_pad_output_to_key`）。
     * @param capture 割り当ての画面が開いている間か（枠のキーを出す。単独の A・B・L3・R3 は除く）
     */
    std::vector<DcssPadAction> on_press(const PadPress &press, DcssPadScene scene, bool capture) const;
    //! 方向 1 回を、画面が行うことにする（原作の `_pad_direction_key`）。
    DcssPadAction on_direction(int dx, int dy, DcssPadScene scene) const;

    /*!
     * @brief いまの画面の場面。
     * @param windows_open 窓の部品がキーを受けているか
     */
    static DcssPadScene scene_for(const DcssUiModel &model, bool windows_open);

    // ---- 疑似カーソル ----
    static constexpr int kCursorDeadzone = 8000;
    static constexpr int kCursorMinSpeed = 60;
    static constexpr int kCursorMaxSpeed = 1400;
    static constexpr int kCursorTickMs = 16;
    /*!
     * @brief 右スティックの傾き（SDL の -32768〜32767）から、前回からの動き（画素）を出す（原作 `cursor_motion`）。端数は持ち越す。
     * @return 動かす量が 0 でなければ真
     */
    bool cursor_motion(std::uint32_t now_ms, bool focused, int raw_x, int raw_y, int &dx, int &dy);
    //! 疑似カーソルの位置（窓の座標）。まだ決まっていなければ偽。
    bool cursor_position(int &x, int &y) const;
    //! 疑似カーソルを動かす（窓の外へは出さない）。
    void move_cursor(int dx, int dy, int screen_w, int screen_h);
    //! 本物のマウスが動いた。疑似カーソルをそこへ移し、本物を優先にする（最後に動かした方を優先）。
    void note_real_mouse(int x, int y);
    //! 本物のマウスの位置を毎フレーム見る。前回と違えば `note_real_mouse`（最初の 1 回は基準にするだけ）。
    void observe_mouse(int x, int y);
    //! 疑似カーソルを出すか（右スティックが最後に動かした方か）。
    bool cursor_visible() const { return this->cursor_pad_active_; }
    //! 画面の大きさが変わったときなどに位置を収める。
    void clamp_cursor(int screen_w, int screen_h);

    // ---- 下の操作の案内 ----
    /*!
     * @brief 下の操作の案内の項目（`A:決定` の形）。
     * @param chord_mask いま押している修飾（LB・RB）。層の割り当てを見せる
     * @param bindings_screen 割り当ての画面が開いているか
     */
    std::vector<std::string> guide_items(DcssPadScene scene, int chord_mask, bool bindings_screen) const;

    // ---- 仮想パッド ----
    /*!
     * @brief 仮想パッド（`VirtualPad::draw`）がボタンに出す名前を、同じ割り当て表から作る。
     * @details 押しは同じ表（`on_press`）に乗る。ここは見せるためだけ。
     */
    void fill_vpad_view(PadBinds &binds, std::vector<PadCommand> &commands) const;

private:
    std::string tr(const char *ja, const char *en) const { return this->japanese_ ? ja : en; }
    //! 名前（`CMD_…`・`none`・`HD2D_MENU`…）から割り当ての値へ。読めなければ偽。
    bool value_from_name(const std::string &name, int &value) const;
    std::string value_name(int value) const;
    void apply_pending();

    bool japanese_{ true };
    std::vector<DcssPadCommand> commands_;
    std::map<std::string, int> ids_by_name_;
    std::map<int, int> user_; //!< 枠 -> 値（0 = 割り当てなし）
    std::vector<std::pair<int, std::string>> pending_; //!< 表が届く前に読んだ名前（枠, 名前）
    bool changed_{ false };

    // 疑似カーソル
    bool cursor_moving_{ false };
    std::uint32_t cursor_last_{ 0 };
    double cursor_frac_x_{ 0 };
    double cursor_frac_y_{ 0 };
    bool cursor_known_{ false };
    int cursor_x_{ 0 };
    int cursor_y_{ 0 };
    bool cursor_pad_active_{ false };
    bool real_known_{ false };
    int real_x_{ 0 };
    int real_y_{ 0 };
};

} // namespace hd2d::dcss
