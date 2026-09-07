/*!
 * @file xr_input.h
 * @brief XR の actions → 既存の `PadInput`。
 *
 * ## 狙い
 * **既存の抽象へ流し込むだけにする。** `game_pad.h` はヒステリシス・連射・再武装・
 * 同時押しの割り当て・cfg 保存を全部持っている。ここで同じ仕掛けを書き直すと
 * 必ず片方だけ直った状態になるので、こちらは「いま押されているか」を作って
 * `GamePad::feed_external()` へ渡すだけにする。
 *
 * 割り当て UI（`ui/key_binds.h`）も cfg も無改修で効く。**Touch のボタンは
 * 机の実パッドと同じ枠**を使うということである。
 *
 * ## SDL のパッドとは並列に生かす
 * 机のパッドを挿したまま被れる。`GamePad` は右スティックだけ実パッドを優先し、
 * 残りは先に来たほうが効く。
 */
#pragma once

#include "ui/game_pad.h"

#include <string>

namespace hd2d::xr {

/*!
 * @brief Touch コントローラ 1 組ぶんの actions。
 * @details ハンドルは `void *` で受け取る（`openxr.h` をこのヘッダへ持ち込まないため。
 * XrInstance / XrSession はどちらもポインタ大の不透明ハンドル）。
 */
class Input {
public:
    Input() = default;
    ~Input();
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;

    /*!
     * @brief action set を作り、Touch へ結び、セッションへ付ける。
     * @param instance,session `Session::native_instance()` / `native_session()`。
     * @return 立ったか。立たなくても VR は続く（キーボードと机のパッドで遊べる）。
     */
    bool init(void *instance, void *session, std::string &err);
    void shutdown();
    bool valid() const { return this->impl_ != nullptr; }

    /*!
     * @brief 1 フレームぶん同期して、いまの状態を採る。
     * @param out 押されているボタンとスティックの傾き。
     * @return 採れたか（セッションが FOCUSED でなければ偽）。
     */
    bool sync(ExternalPadState &out);

    //! いま繋がっている interaction profile の名前（起動行ログと切り分け用）。
    const std::string &profile_name() const { return this->profile_name_; }

private:
    struct Impl;
    Impl *impl_{ nullptr };
    std::string profile_name_;
};

/*!
 * @brief Touch のどの操作がどの `PadInput` になるかの表（§8）。
 * @details **実装の外から読める形で持つ**——`--vr-input-check` がこれを出し、
 * 実機が無くても「何がどこへ行くか」を確かめられるようにするため。
 */
struct TouchBinding {
    const char *touch; //!< Touch 側の呼び名（人が読む）
    const char *path; //!< OpenXR の入力パス
    PadInput input; //!< 流し込み先
};

//! 表の中身。`count` に個数が入る。
const TouchBinding *touch_bindings(int &count);

} // namespace hd2d::xr
