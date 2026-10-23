/*!
 * @file dcss_touch.h
 * @brief DCSS（Dungeon Crawl Stone Soup 日本語版）のタッチ操作。指の出来事（`SDL_FINGER*`）を、原作の Android 版と同じ決まりでマウスの出来事に直す。
 *
 * ## 原作の決まり（「段 7 のタッチ」。原作 `android-project/.../SDLActivity.java` の `SDLSurface.onTouch`・ja4）
 * 原作の Android 版は、指をマウスとして扱う（Java 側で `onNativeMouse` を呼ぶ）。**画面の側が持つ決まりはこの 4 つだけ**で、
 * 触った先で何が起きるかは、原作のタイル版の区画（地図・タブの欄・メニュー・窓・メッセージ欄・ステータス欄）がマウスに対して持つ動きと同じ。
 *
 * - 指 1 本で動かしたら、動きのたびにマウスの動き（`SDL_MOUSEMOTION`）。押している間も同じ（ホバーの強調・狙いのカーソルが指に付いてくる）。
 * - 指を離したとき、押した所から**画面の短い辺の 1% 以内**にしか動いていなければ、離した位置でクリック。**500 ミリ秒未満なら左クリック、
 *   500 ミリ秒以上なら右クリック**（長押し）。1% を超えて動いていたらクリックにならない（動きだけ）。
 * - 指を 2 本置くと、その指が全部離れるまでクリックにならない。2 本が同じ向きに縦に動くと、10 画素ごとにマウスのホイール 1 目盛り（指が下へ = 上へ回す）。
 * - 戻るキーは Esc（`DcssUi::on_window_event` ほか。このファイルの外）。
 *
 * ## このクラスの形
 * 描画にもコアにも SDL の初期化にも触れない**純粋な状態機械**（`SDL_Event` の中身だけを読み書きする。単体で検査できる）。
 * 呼ぶ側（`hd2d_app.cpp`）が、出てきたマウスの出来事を `SDL_PushEvent` で積み直し、既存のマウスの経路（窓・タブの欄・地図）へ流す。
 * 積み直した出来事の `which` は `kTouchMouseId`（実マウスでも `SDL_TOUCH_MOUSEID` でもない値。後者は既存の部品が「タッチのマウス写し」として
 * 無視するため）。
 */
#pragma once

#include <SDL2/SDL.h>

#include <cstdint>
#include <map>
#include <vector>

namespace hd2d::dcss {

//! 指から作ったマウスの出来事の `which`（`'DCST'`）。
inline constexpr std::uint32_t kTouchMouseId = 0x44435354u;

//! 原作の長押しの時間（ミリ秒）。これ以上で右クリック。
inline constexpr std::uint32_t kLongPressMs = 500;
//! 離す出来事が届かなかった指を捨てるまでの時間（ミリ秒）。
inline constexpr std::uint32_t kStaleFingerMs = 15000;
//! 2 本指のスクロールで、ホイール 1 目盛りにする縦の動き（画素。原作 `scroll * 0.1`）。
inline constexpr float kScrollPixelsPerTick = 10.f;

class DcssTouch {
public:
    /*!
     * @brief 指の出来事を 1 つ見て、積み直すマウスの出来事を返す。
     * @param event `SDL_FINGERDOWN`・`SDL_FINGERMOTION`・`SDL_FINGERUP`（座標は 0〜1）
     * @param screen_w,screen_h 窓の実寸（画素）
     * @param now 時刻（ミリ秒）
     * @param ignore_down 真のとき、この指の押下は**無かったこと**にする（仮想パッドのボタンの近くを触った指など。離したときもクリックにしない）
     * @param out 積み直す出来事（動き・押下・離し・ホイール）。追記する。
     */
    void on_finger(const SDL_Event &event, int screen_w, int screen_h, std::uint32_t now, bool ignore_down, std::vector<SDL_Event> &out);

    //! いま触れている指の数（検査用）。
    int fingers_down() const { return static_cast<int>(this->fingers_.size()); }

private:
    struct Finger {
        float x0{ 0.f }; //!< 押した位置（画素）
        float y0{ 0.f };
        float x{ 0.f }; //!< いまの位置（画素）
        float y{ 0.f };
        float last_y{ 0.f }; //!< 前の動きの位置（スクロールの差の計算用）
        float dy_last{ 0.f }; //!< 最後の動きの縦の差
        std::uint32_t t0{ 0 };
        bool ignored{ false };
    };

    std::map<std::int64_t, Finger> fingers_;
    bool scrolling_{ false }; //!< 指が 2 本以上になった。全部離れるまでクリックにしない
    float scroll_acc_{ 0.f };
};

//! 検査用: 組み込み（窓も GL も使わない）の自己検査。失敗した項目の数を返す（標準エラーへ 1 件ずつ出す）。
//! 指の決まり（`DcssTouch`）と、名前の問い（`DcssWindowStack::name_prompt`）の検出を調べる。`HengbandHd2d.exe --dcss-touch-check` が呼ぶ（0 = 全部通った）。
int dcss_touch_selftest();

} // namespace hd2d::dcss
