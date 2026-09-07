/*!
 * @file key_binds.h
 * @brief 操作 → キーボードのキー、という向きの割り当て表。
 *
 * ## なぜ「操作ごと」なのか（2026-08-11 に決めた）
 * 「キーごとに操作を割り当てるのではなく、**操作ごとにキーを割り当てる**形に」。
 * 前の作りは `X ボタン ＝ 拾う` という並べ方で、**やりたいことから引けなかった**
 * （「拾うは何を押す？」に答えるには全ボタンを順に見るしかない）。
 * 並べ替えではなく持ち方から変える。表の鍵は操作であり、値がキーである。
 *
 * ## 操作の番号（`action`）
 * 1 つの番号空間に 2 種類を同居させる。
 *   - **正** … コアのコマンド `id`（`pad_commands` で届くもの。`ui/game_pad.h`）
 *   - **負** … この exe 自身の操作（`UiAction`。機能メニューを開く 等）
 *   - **0** … 割り当て無し
 * こうしておくと、キーボードの表もパッドの表（`PadBinds`）も**同じ番号**で引ける。
 * 2 つの番号体系を持つと、必ず片方だけ直された状態になる。
 *
 * ## システムが押さえているもの（**割り当ての対象にしない**）
 * | | キーボード | コントローラー |
 * |---|---|---|
 * | 選ぶ | ↑↓←→・テンキーの方向 | 十字キー・左スティック |
 * | 決定 | Enter | A |
 * | 取消 | ESC | B |
 *
 * こう決めた——「方向キーと決定キャンセルは未割り当てに出来ないように、
 * 十字キーと Enter と ESC はシステムで確保した選択決定キャンセルとして
 * 他の操作に割り当てはできないように」。これを外すと**割り当て画面から出られなくなる**
 * （既存 UI の K-4 と同じ穴）。判定は `key_is_reserved()` 1 か所だけに置く。
 *
 * @note パッドの `Back`（機能メニュー）も固定のままにしてある（`pad_input_is_fixed`）。
 * キーボードの割り当てを全部消しても、パッドがあれば必ずメニューへ戻れる、という逃げ道である。
 */
#pragma once

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief この exe 自身の操作。**負の番号**でコアのコマンド `id`（正）と同居させる。
 * @details 値は cfg に書くので**増やすときは末尾へ**（既存の cfg の意味が変わらないように）。
 */
enum UiAction : int {
    kActionNone = 0,
    kActionFeatureMenu = -1, //!< 機能メニューを開く／閉じる
    kActionLayoutCycle = -2, //!< 画面の作りを回す
    kActionSubsToggle = -3, //!< サブパネルの開閉
    kActionFpsToggle = -4, //!< 一人称視点の切り替え
    /*!
     * @brief 一人称の視界を水平（中立の見下ろし角）へ戻す。
     * @details 2026-08-11 に決めた。既定は**コントローラー R3**と
     * **マウスホイールの押し込み**。後者は割り当ての対象にしていない
     * （マウスのボタンはこの表に並べていないため。`hd2d_app.cpp` で直に見ている）。
     */
    kActionFpsLevelView = -5,
    /*!
     * @brief VR の盤と文字パネルを、いまの頭の位置・向きへ置き直す（再中心）。
     * @details ・§8。**固定の身振りにせず、割り当ての表へ入れた**
     * ——こうするとキーボードからも Touch からも同じ番号で出せて、
     * 誤爆する場所だったら利用者が振り直せる。VR でなければ何も起きない。
     */
    kActionVrRecenter = -6,
    /*!
     * @name 見下ろしの視点を 90° 回す
     *
     * @details 既定は **F5（左回り）／ F6（右回り）**。回すのは見下ろしのカメラだけで、
     * 一人称のときは効かない（あちらは自分で向きを持っている）。
     * **ターンを消費しない**——コアへは何も送らない。
     * @note UI がこの 2 つのキーを食うので、**コア側で F5/F6 に組んだマクロは撃てなくなる**。
     * 要るなら割り当て画面（ESC で消去）で外すこと。F7〜F10 と同じ約束である。
     * @{
     */
    kActionTurnLeft = -7,
    kActionTurnRight = -8,
    /*! @} */
    /*!
     * @brief メインパネルを **3D ／ 2D アスキー**で切り替える。
     * @details 既定は **F4**。切り替わるのはメインパネルの中身だけで、
     * 周りの UI はどちらでも同じように効く。
     */
    kActionMainPanelToggle = -9,
    /*!
     * @brief **コマンドメニュー**を開く（2026-08-22 に決めた
     * 「変愚の通常メニューを UI 側で作成してすべてのコマンドを実行できるようにしよう」）。
     *
     * @details コアが `pad_commands` で送ってくる**命令の一覧をそのまま並べて、
     * 選んだものを実行する**画面である。変愚のコアは自分の通常メニュー
     * （`src/cmd-io/cmd-menu-content-table.cpp` の `menu_info`）を平坦化して送るので、
     * **あの menu がそのまま出る**（9 分類 63 件）。Sil-Q も 46 件を送る。
     *
     * **画面はコア固有の知識を持たない**（必守制約 1）——並べるのも実行するのも
     * 届いた表が根拠で、どのコアでも同じ 1 本の道である。
     *
     * 既定のキーは無い（キーボードなら機能メニューから 1 つ潜れば届く）。
     * **触りだけの機体ではここへ直に飛べることが要る**ので、割り当ての表に並べる。
     */
    kActionCommandMenu = -10,
};

//! 画面に並べる順の UI 操作。
const std::vector<int> &ui_action_ids();
//! UI 操作の表示名。UI 操作でなければ空文字。
const char *ui_action_label(int action);

/*!
 * @name 押したキーの修飾（**この exe の中だけの綴り**）
 * @details `SDL_Keymod` をそのまま持つと左右の Shift が別の値になり、
 * 「右 Shift で割り当てたら左 Shift で反応しない」になる。左右をまとめた 3 ビットだけを持つ。
 * @{
 */
constexpr int kModShift = 1;
constexpr int kModCtrl = 2;
constexpr int kModAlt = 4;
/*! @} */

//! 操作 1 件ぶんのキーボードの割り当て。
struct KeyBindEntry {
    int action{ kActionNone };
    int keycode{ 0 }; //!< `SDL_Keycode`。0 = 割り当て無し
    int mods{ 0 }; //!< `kModShift` 等の論理和
};

/*!
 * @brief 操作 → キーボードのキー。
 * @details **持っているのは「利用者が変えたぶん」だけ**である。コアのコマンドは
 * もともとコア自身のキー（`拾う` なら `g`）で出せるので、ここに無い操作は
 * 「コアの既定のキーで出す」という意味になる。割り当てを消す（ESC）と既定へ戻る。
 */
struct KeyBinds {
    std::vector<KeyBindEntry> entries;

    //! この操作に割り当てられたキー。無ければ `keycode == 0` の値を返す。
    KeyBindEntry find(int action) const;
    //! このキー（＋修飾）が割り当てられた操作。無ければ `kActionNone`。
    int action_for(int keycode, int mods) const;
    /*!
     * @brief 割り当てる。
     * @details **同じキーを 2 つの操作に持たせない**（先客からは外す）。放っておくと
     * どちらが走るか押すまで分からない表になる。予約キー（`key_is_reserved`）は受け付けない。
     * @return 割り当てられたら true。
     */
    bool assign(int action, int keycode, int mods);
    //! 割り当てを消す（コアの既定のキーへ戻る）。
    void clear(int action);

    //! `-4:1073741953:0,12:103:0` の形へ（cfg に書く）。
    std::string to_line() const;
    //! 同じ形から読む。読めた分だけ入れる。
    void parse(const std::string &line);
};

//! 既定の割り当て（cfg も引数も無いとき）。UI 操作の 4 つだけ（F10 / F9 / F8 / F7）。
KeyBinds default_key_binds();

/*!
 * @brief システムが押さえているキーか。**他の操作へ割り当てられない。**
 * @details ↑↓←→・テンキーの方向・Enter・ESC。2026-08-11 に決めた。
 */
bool key_is_reserved(int keycode);

//! ESC か。**割り当ての消去**に使うので、予約キーの中でも別扱いになる。
bool key_is_escape(int keycode);

//! 画面に出す綴り（`G` / `Shift+G` / `F10` / `Space`）。割り当てが無ければ空。
std::string key_display_name(int keycode, int mods);

/*!
 * @brief F キーの番号（1〜12）→ `SDL_Keycode`。範囲の外は 0。
 * @details 古い cfg（`key_feature_menu=10` の形）を読み替えるためだけの手段。
 * **SDL の値を hd2d_settings.cpp へ持ち込まない**ために置いてある。
 */
int key_from_fkey(int fkey);

/*!
 * @brief コアが送ってきたキー列（`PadCommand::keys`）を画面に出す綴りへ。
 * @details これは**コアの既定のキー**であって、この表の割り当てではない。
 * 0x1B は `ESC`、0x0D は `Enter`、印字 ASCII はそのまま。空なら空。
 */
std::string core_key_sequence_name(const std::vector<int> &keys);

} // namespace hd2d
