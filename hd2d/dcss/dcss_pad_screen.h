/*!
 * @file dcss_pad_screen.h
 * @brief DCSS のゲームパッドの割り当ての画面（原作 `gamepad-menu.cc` の `PadBindMenu`・`PadCommandMenu`・`PadCommandListMenu`）。段 5。
 *
 * ## 何のためのものか
 * 決めたこと D11・D12: 割り当ての画面も原作の日本語版に合わせ、このコアの窓の部品（`dcss_windows.h`）の見た目で作る。
 * 原作の画面は `Menu`（ゲームの中のメニュー）で、コマンドを選ぶ 2 段に分かれている。ここは同じ 3 枚の窓（枠の一覧 → コマンドの分類 → 分類の中のコマンド）を、
 * ウェブタイル版のメニューの JSON（`menu`）と同じ形で `DcssWindows` の窓の積みへ**画面の側から**積み、窓の部品の描画・強調・捲りをそのまま使う。
 * 窓のキー（Enter・Esc）はコアへ送らず、ここへ渡す（`DcssWindows::set_local_handler`）。窓の題の `tag` は `hd2d_pad_*`。
 *
 * ## 動き（原作と同じ）
 * - 1 枚目「ゲームパッドの割り当て」: 枠（ボタン × 層）を層ごとの見出しの下に並べる。パッドのボタンや同時押しを押すと、その枠の 2 枚目が開く。変えた枠は黄色。
 *   単独の A・B・L3・R3 は固定なので行にしない。末尾に「この画面での変更をすべて取り消す」（はい/いいえ）。
 * - 2 枚目: 「割り当てなし」「元に戻す」「コマンドの分類 (数)」。分類を選ぶと 3 枚目が開く。
 * - 3 枚目: その分類のコマンド（日本語の名前と今のキー）。選ぶと割り当てて 2 枚目・3 枚目が閉じる。B で戻る。
 * 割り当ては `DcssPad` が持ち、HD2D の設定 `dcss_pad_binds` に保存する（原作の `pad_binds.txt` に当たる）。
 */
#pragma once

#include "dcss/dcss_pad.h"
#include "dcss/dcss_windows.h"

#include <string>
#include <vector>

namespace hd2d::dcss {

class DcssPadScreen {
public:
    DcssPadScreen(DcssPad &pad, DcssWindows &windows);

    //! 1 枚目を開く。コアのコマンドの表が届いていなければ偽（何も開かない）。
    bool open();
    //! 割り当ての画面の窓がいちばん上にあるか（この間パッドは枠のキーを出す。`capturing()` は 1 枚目が最上位のとき）。
    bool active() const;
    bool capturing() const;
    //! パッドの枠（ボタン × 層）が押された（1 枚目が最上位のとき）。その枠の 2 枚目を開く。
    void on_slot(int slot);
    //! 窓の部品が「コアへ送るはずだった行」。ここへ渡る（キーだけ控える）。
    void on_line(const std::string &line);
    //! 控えた操作を実行する（窓の積みを変えるので、窓のキーの処理が終わったあとに呼ぶ）。
    void service();

    //! 下の案内に出す 1 行の状態（直前に変えた枠）。
    const std::string &status() const { return this->status_; }

private:
    enum class Row { None, OpenSlot, UndoAll, SetNone, SetDefault, OpenGroup, SetCommand, Yes, No };
    struct RowAction {
        Row kind{ Row::None };
        int value{ 0 };
    };
    enum class Kind { Bindings, Groups, Commands, Confirm };
    struct Level {
        Kind kind{ Kind::Bindings };
        std::vector<RowAction> rows; //!< 窓の項目の番号で引く
        int slot{ -1 }; //!< 2・3 枚目: 編集している枠
        int group{ -1 }; //!< 3 枚目: 分類の番号
    };

    void push_level(Kind kind, int slot, int group, int hover_slot);
    void refill_bindings(int hover_slot);
    void pop_level();
    void activate(int row);
    bool top_is_ours() const;
    std::string esc(const std::string &s) const;
    std::string tr(const char *ja, const char *en) const { return this->pad_.japanese() ? ja : en; }
    //! 窓の部品へ積む（`menu` の JSON）。
    void inject_menu(const std::string &tag, const std::string &title, const std::string &more, const nlohmann::json &items, int hover, bool replace);

    DcssPad &pad_;
    DcssWindows &windows_;
    std::vector<Level> levels_;
    std::vector<int> pending_keys_;
    std::string status_;
};

} // namespace hd2d::dcss
