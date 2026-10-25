/*!
 * @file dcss_binds.h
 * @brief DCSS の「操作の割り当て」（HD2D の機能メニューの表）。原作のキーマップ（キーボード）とゲームパッドの枠（コントローラー）を、その表から変える。D33。
 *
 * ## 何のためのものか
 * 決めたこと D33（2026-10-25）「このコアのキー割り当てを操作の割り当てでできるようにして」。機能メニューの「操作の割り当て」の表に、このコアのコマンドを
 * 原作の分類ごとに並べ、キーボードのキーとゲームパッドのボタンをその場で割り当てられるようにする（`ExternalBinds` の入口。`ui/external_binds.h`）。
 *
 * ## 割り当ての持ち方（技術の判断）
 * - **コントローラー**: 段 5 の割り当て（`DcssPad`。設定 `dcss_pad_binds`）をそのまま使う。原作どおりの割り当ての画面（`DcssPadScreen`）と同じ物を読み書きするので、どちらで変えても食い違わない。
 * - **キーボード**: 原作のキーマップ（ゲームの中の `~` の画面が編集する `macro.txt` の `K:`）。画面の側に別の表を持たず、コアへ要求（`hd2d_keymap`。`dcss/adapter/dcss_keymap_data.h`）を送って書かせ、
 *   返事（`{"t":"dcss_ui","k":"keymap"}`）の表を映す。`~` の画面で足したキーマップも同じ表に出る。割り当てを変えていない人の操作（既定のキー）は 1 キーも変わらない。
 *   割り当てるのは「足すキー」で、既定のキーは外さない。元に戻す＝足したキーを割り当てる前の動きへ戻す。
 *
 * 描画にも通信にも触れない（コアへ送る行は取り出して送る）。
 */
#pragma once

#include "dcss/dcss_pad.h"
#include "ui/external_binds.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace hd2d::dcss {

class DcssBinds final : public ExternalBinds {
public:
    explicit DcssBinds(DcssPad &pad) : pad_(pad) {}

    //! 原作のゲームパッドの割り当ての画面を開く（`DcssUi` が渡す。開けたら真）。
    void set_pad_screen_opener(std::function<bool()> opener) { this->open_pad_screen_ = std::move(opener); }

    //! コアの返事 `{"t":"dcss_ui","k":"keymap","maps":[[キー,"名前",コマンド],…],"err":"…"}` を取り込む。
    void ingest_keymap(const nlohmann::json &message);
    //! コアへ送る行（`hd2d_keymap` の要求・`~` のキー）を取り出す。
    std::vector<std::string> take_outgoing();

    // ---- ExternalBinds ----
    void on_open() override;
    std::vector<ExternalFixedRow> fixed_rows() const override;
    bool hides_ui_action(int action) const override;
    std::vector<ExternalBindRow> rows() const override;
    std::string cell(int id, int col) const override;
    bool ui_has_pad(int action) const override;
    std::string ui_pad_cell(int action) const override;
    std::string assign_key(int id, int key) override;
    std::string assign_pad(int id, PadInput input, int mods) override;
    std::string assign_ui_pad(int action, PadInput input, int mods) override;
    void clear(int id, int col) override;
    void clear_ui_pad(int action) override;
    bool open_screen(int id) override;

    //! 画面を開く行の番号（負。コマンドの番号は正）。
    static constexpr int kRowOpenPadScreen = -2001;
    static constexpr int kRowOpenKeymap = -2002;

    //! 足されたキー（検査用）。
    struct KeyEntry {
        int key{ 0 };
        std::string name;
        int cmd{ 0 };
    };
    const std::vector<KeyEntry> &keymaps() const { return this->keymaps_; }

private:
    std::string tr(const char *ja, const char *en) const { return this->pad_.japanese() ? ja : en; }
    //! 画面まわりの操作 → このコアの画面の機能の番号（無ければ 0）。
    static int ui_function(int action);
    std::string slots_text(int cmd) const;
    std::string assign_slot(int cmd, PadInput input, int mods);
    void reset_pad(int cmd);

    DcssPad &pad_;
    std::function<bool()> open_pad_screen_;
    std::vector<KeyEntry> keymaps_;
    std::vector<std::string> outgoing_;
};

} // namespace hd2d::dcss
