/*!
 * @file nh_model.h
 * @brief コアから届いた `nethack_ui` の知らせから作る、NetHack の専用の画面の状態。
 *
 * ## 何のためのものか
 * 窓の表（`win`）・メッセージの窓（`putstr` → `NhMsgWindow`）・ステータスの項目と色と割合（`status`）・待ちの理由と要求の番号
 * （`wait`・`menu`・`ask` ほか）・地図の窓のカーソル（`curs`・`cliparound`）・設定（`pref`）を持つ。描画にも通信にも触れない。
 * 単体検査 `tools/hd2d_verify/test_nethack_ui_model.cpp` が、知らせの列から正しい状態が作られることを固定する。
 *
 * ## 知らせの扱い（届いた順に全部渡される）
 * - 知らない `k` ・壊れた JSON・欄の型違いは黙って捨てる（`ingest` が偽を返す）。画面を止めない。
 * - **`wait` 以外の知らせが来たら、それまでの待ちは終わった**とする（コアは `wait` の直前に止まるので、あとの知らせは再開を意味する）。
 * - `menu`・`text`・`ask`・`select`・`rip`（N2 以降の窓）は、種類と要求の番号 `r` だけを覚える。中身の取り込みはその段でここへ足す。
 */
#pragma once

#include "nethack/nh_msgwin.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hd2d::nethack {

//! 原作の `BL_*`（`include/botl.h`）。
enum NhField : int {
    kBlTitle = 0,
    kBlStr = 1,
    kBlDx = 2,
    kBlCo = 3,
    kBlIn = 4,
    kBlWi = 5,
    kBlCh = 6,
    kBlAlign = 7,
    kBlScore = 8,
    kBlCap = 9,
    kBlGold = 10,
    kBlEne = 11,
    kBlEneMax = 12,
    kBlXp = 13,
    kBlAc = 14,
    kBlHd = 15,
    kBlTime = 16,
    kBlHunger = 17,
    kBlHp = 18,
    kBlHpMax = 19,
    kBlLevelDesc = 20,
    kBlExp = 21,
    kBlCondition = 22,
    kBlMax = 27,
};

//! 原作の `NO_COLOR`（色の指定なし）。
inline constexpr int kNoColor = 8;

//! ステータスの項目の属性の位（`HL_*`。`include/botl.h`）。
inline constexpr int kHlBold = 0x02;
inline constexpr int kHlDim = 0x04;
inline constexpr int kHlUline = 0x10;
inline constexpr int kHlBlink = 0x20;
inline constexpr int kHlInverse = 0x40;

struct NhWindowInfo {
    int id{ -1 };
    std::string type; //!< `message`・`status`・`map`・`menu`・`text`・`rip`・`invent`
    bool blocking{ false };
    bool displayed{ false };
    //! `putstr` で届いた行（メッセージ以外の窓。メニュー・テキストの窓が N2 で使う）。`win clear` で空にする。
    std::vector<NhMsgLine> lines;
};

struct NhStatusField {
    bool present{ false };
    std::string text; //!< コアが送った表示用の字
    std::string fmt;  //!< 書式（`%s` を 1 つ含む。任意。無ければ `text` のまま。下の `nh_status_display_text`）
    int color{ kNoColor };
    int attr{ 0 }; //!< `HL_*` の位
    int pct{ -1 }; //!< 割合（0〜100。無ければ -1）
};

struct NhStatusCond {
    std::string name;
    int color{ kNoColor };
    int attr{ 0 };
};

//! コアが止まった理由と要求の番号。`reason` が空なら待っていない。
struct NhWait {
    std::string reason; //!< `command`・`more`・`menu`・`ask`・`pos`・`key`
    int r{ 0 };
};

/*!
 * @brief コマンドの欄（`cmdbar`）の 1 つ。タッチだけの端末でもコマンドを打てるよう、画面が絵のボタンとして並べる。
 * 押すと `keys`（キーの列）を送るか、`ext` があれば拡張コマンドの名前を送る。
 */
struct NhCmdItem {
    int id{ 0 };
    std::vector<int> keys;
    std::string ext;
    int glyph{ -1 }; //!< 絵（無ければ -1。`text` を描く）
    std::string text; //!< 絵が無いときに描く字
    std::string en;
    std::string ja;
};

class NhModel {
public:
    /*!
     * @brief 知らせ 1 通（`{"t":"nethack_ui","k":…}`）を取り込む。
     * @return 取り込めたら真。`nethack_ui` でない・壊れている・知らない `k` は偽（何も変えない）。
     */
    bool ingest(const std::string &payload);

    // ---- 窓 ----
    const NhWindowInfo *window(int id) const;
    //! その種類の窓の番号（無ければ -1）。
    int window_of_type(const std::string &type) const;
    //! 地図の窓を作ったか。ゲームに入った印（キャラクタ作成のあいだは作られない）。
    bool map_created() const { return this->window_of_type("map") >= 0; }
    bool exited() const { return this->exited_; }
    const std::string &exit_text() const { return this->exit_text_; }

    // ---- メッセージの窓 ----
    NhMsgWindow &msgwin() { return this->msgwin_; }
    const NhMsgWindow &msgwin() const { return this->msgwin_; }

    // ---- ステータス ----
    const NhStatusField &status(int field) const;
    const std::vector<NhStatusCond> &conditions() const { return this->conditions_; }
    //! 項目か状態が変わるたびに増える。
    std::uint64_t status_version() const { return this->status_version_; }
    bool status_seen() const { return this->status_seen_; }

    // ---- 待ち ----
    const NhWait &wait() const { return this->wait_; }
    bool more_pending() const { return this->wait_.reason == "more"; }
    //! 画面が返事（キー）を送ったので、待ちを終えたことにする。次の知らせで正しい状態に戻る。
    void clear_wait() { this->wait_ = NhWait{}; }
    //! `wait` の知らせが届くたびに増える（同じ待ちの種類が続いても、新しい待ちだと分かる。地図の上の説明の取り直しに使う）。
    std::uint64_t wait_serial() const { return this->wait_serial_; }
    //! 地図のクリックを受ける待ちか（`command` は次の命令、`pos` は位置の選択。原作の `nh_poskey`）。
    bool map_click_wait() const { return (this->wait_.reason == "command") || (this->wait_.reason == "pos"); }
    bool pos_wait() const { return this->wait_.reason == "pos"; }
    /*!
     * 位置の選択の途中か。`wait pos` で立ち、ほかの待ち・窓・問い・終了で下りる。選んだ直後〜次の待ちまでは立ったまま
     * （`curs` や説明の行で一瞬待ちが途切れても、地図のカーソルの印を消さないため）。
     */
    bool pos_active() const { return this->pos_active_; }
    //! 画面が位置を返した（クリックで選んだ）ので、位置の選択は終わったことにする。
    void clear_pos_active() { this->pos_active_ = false; }
    //! 最後に届いたメニュー・問いなどの要求（`menu`・`text`・`ask`・`select`・`rip`）の種類と番号。N2 以降が使う。
    const std::string &last_request_kind() const { return this->last_request_kind_; }
    int last_request_r() const { return this->last_request_r_; }

    //! 返事を待っている要求（`menu`・`text`・`ask`・`select`・`rip`）。窓の部品が取り込む。返事を送るまで残る。
    struct Request {
        std::string kind;    //!< `menu`・`text`・`ask`・`select`・`rip`
        int r{ 0 };
        std::string payload; //!< 届いた JSON の字そのもの
        std::uint64_t serial{ 0 }; //!< 届くたびに増える（窓の部品が新しい要求かを見分ける）
    };
    const Request *request() const { return this->request_open_ ? &this->request_ : nullptr; }
    //! 窓の部品が返事を送った（要求を閉じる）。
    void clear_request() { this->request_open_ = false; }

    // ---- 持ち物の常設の窓（`win type=invent` と、その窓の `menu`。`r` は無い）----
    int invent_window() const { return this->invent_window_; }
    //! 常設の窓を出すか。コアは `perm_invent` が On のときだけ `win create type=invent` を送る（プロトコル v1 §8.7）ので、窓があれば真。
    //! 窓が無いときは `pref perm_invent`（1・`true`・`on`）を見る。
    bool perm_invent() const { return (this->invent_window_ >= 0) || this->pref_bool("perm_invent"); }
    //! 常設の窓の中身（`menu` の知らせそのもの）。まだ届いていなければ空。
    const std::string &invent_payload() const { return this->invent_payload_; }
    //! 常設の窓の中身が届くたびに増える。
    std::uint64_t invent_version() const { return this->invent_version_; }

    // ---- 地図の窓 ----
    bool cursor_known() const { return this->cursor_known_; }
    int cursor_x() const { return this->cursor_x_; }
    int cursor_y() const { return this->cursor_y_; }
    //! 地図のマスの説明（`hover` の知らせ。`nethack_query` に答えてコアが送る）。`hover_serial` は届くたびに増える。
    int hover_x() const { return this->hover_x_; }
    int hover_y() const { return this->hover_y_; }
    const std::string &hover_text() const { return this->hover_text_; }
    std::uint64_t hover_serial() const { return this->hover_serial_; }
    bool clip_known() const { return this->clip_known_; }
    int clip_x() const { return this->clip_x_; }
    int clip_y() const { return this->clip_y_; }

    //! コマンドの欄（コアが `cmdbar` で 1 回送る）。届く前は空。
    const std::vector<NhCmdItem> &cmdbar() const { return this->cmdbar_; }
    //! コマンドを待っているか（コマンドの欄を押してよいか）。
    bool command_wait() const { return this->wait_.reason == "command"; }

    // ---- 設定 ----
    /*! `pref` で届いた設定。`perm_invent` は 1・`true`・`on` を真とする。 */
    bool pref_bool(const std::string &name, bool fallback = false) const;
    std::string pref_string(const std::string &name) const;

    //! 取り込むたびに増える（再描画の判定に使う）。
    std::uint64_t version() const { return this->version_; }

private:
    NhWindowInfo *find_window(int id);
    std::map<int, NhWindowInfo> windows_;
    NhMsgWindow msgwin_;
    NhStatusField fields_[kBlMax]{};
    std::vector<NhStatusCond> conditions_;
    std::uint64_t status_version_{ 0 };
    bool status_seen_{ false };
    NhWait wait_;
    std::string last_request_kind_;
    int last_request_r_{ 0 };
    Request request_;
    bool request_open_{ false };
    std::uint64_t request_serial_{ 0 };
    int invent_window_{ -1 };
    std::string invent_payload_;
    std::uint64_t invent_version_{ 0 };
    bool cursor_known_{ false };
    int cursor_x_{ 0 };
    int cursor_y_{ 0 };
    bool clip_known_{ false };
    int clip_x_{ 0 };
    int clip_y_{ 0 };
    std::map<std::string, std::string> prefs_;
    std::vector<NhCmdItem> cmdbar_;
    bool exited_{ false };
    std::string exit_text_;
    std::uint64_t version_{ 0 };
    std::uint64_t wait_serial_{ 0 };
    bool pos_active_{ false };
    int hover_x_{ 0 };
    int hover_y_{ 0 };
    std::string hover_text_;
    std::uint64_t hover_serial_{ 0 };
};

/*!
 * @brief ステータスの項目の表示用の字（書式を当てたもの）。先頭の空白は付けない。
 * @details 優先: (1) コアが `fmt` を送ったときはそれに当てる、(2) 字が値だけに見えるとき（`:` を含まず、`(`・`/` で始まらない数字などの値）は
 * 原作の既定の書式（`St:`・`HP:`・`(…)` ほか。`botl.c` の `initblstats`）を当てる、(3) それ以外は `text` のまま。
 * コアが整形済みの字を送るなら (3) になる。
 */
std::string nh_status_display_text(int field, const NhStatusField &value);

} // namespace hd2d::nethack
