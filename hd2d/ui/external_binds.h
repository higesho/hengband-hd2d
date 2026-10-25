/*!
 * @file external_binds.h
 * @brief 機能メニューの「操作の割り当て」の表に、コアごとの割り当て（コマンドの行・コントローラーの列）を差し込む入口。DCSS（D33）が使う。
 *
 * @details 既定（入口が空）の機能メニューは、コアが送るコマンドの一覧（`pad_commands`）と HD2D のパッドの割り当て（`settings.pad`）で表を組む。
 * DCSS は原作の割り当て（ゲームパッドの枠・原作のキーマップ）を使い、HD2D のパッドの割り当ては効かないので、表の中身と変更の先をこの入口の向こうへ移す。
 * 入口を渡さないコア（ほかの全部）は 1 行も変わらない。
 */
#pragma once

#include "ui/game_pad.h"

#include <string>
#include <vector>

namespace hd2d {

struct Hd2dSettings;

//! 表の 1 行（コアごとの割り当て）。
struct ExternalBindRow {
    int id{ 0 }; //!< 行の番号（0 = 見出し。コアのコマンドは正、画面を開く行は負）
    std::string label;
    bool no_key{ false }; //!< キーボードの列は割り当てられない
    bool no_pad{ false }; //!< コントローラーの列は割り当てられない
    bool open_row{ false }; //!< 決定で割り当てを待たずに、その場で実行する行（画面を開く）
};

//! 変えられない行（システム）。
struct ExternalFixedRow {
    std::string label;
    std::string keyboard;
    std::string pad;
};

class ExternalBinds {
public:
    virtual ~ExternalBinds() = default;
    //! 機能メニューが開いたとき（最新の割り当てを問い合わせる）。
    virtual void on_open() {}
    //! 「システムが押さえているもの」の行（HD2D の既定の行の代わり）。
    virtual std::vector<ExternalFixedRow> fixed_rows() const = 0;
    //! 割り当てられない画面まわりの操作（表に出さない）。
    virtual bool hides_ui_action(int action) const = 0;
    //! コマンドの行（見出しを含む）。
    virtual std::vector<ExternalBindRow> rows() const = 0;
    //! コマンドの行の値（`col` 0 = キーボード・1 = コントローラー）。
    virtual std::string cell(int id, int col) const = 0;
    //! 画面まわりの操作（機能メニューを開く・視点を回す）のコントローラーの欄を、コアの割り当てから出せるか。
    virtual bool ui_has_pad(int action) const = 0;
    virtual std::string ui_pad_cell(int action) const = 0;
    /*!
     * @brief 割り当てる。戻りは入らなかった理由（空 = 割り当てた）。
     * @param key コアのキーのコード（印字文字はその字、Ctrl+英字は 1〜26）
     */
    virtual std::string assign_key(int id, int key) = 0;
    virtual std::string assign_pad(int id, PadInput input, int mods) = 0;
    virtual std::string assign_ui_pad(int action, PadInput input, int mods) = 0;
    //! その行のその列の、遊ぶ人が足した割り当てを消して既定へ戻す。
    virtual void clear(int id, int col) = 0;
    virtual void clear_ui_pad(int action) = 0;
    //! 「画面を開く」行の実行。真ならメニューを閉じる。
    virtual bool open_screen(int id) = 0;
};

} // namespace hd2d
