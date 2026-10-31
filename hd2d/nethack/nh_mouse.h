/*!
 * @file nh_mouse.h
 * @brief NetHack の専用の画面の、地図の上のマウスの判断（クリックの返事・地図のマスの説明の取り直し・下の帯の字）。
 *
 * ## 何のためのものか
 * の段 N4。原作の Windows の GUI 版（NetHackW）は、地図の窓のクリックを座標とボタン（`CLICK_1` 左・
 * `CLICK_2` 右）にして `nh_poskey` で返すだけで、歩く・走る・調べるといった規則はコア（`cmd.c` の `click_to_cmd`・`getpos.c`）が持つ。
 * この部品も同じに、(1) どの場面でクリックをコアへ返すか、(2) 返事の JSON の形、(3) マウスを載せたマスの説明をコアに聞くか、
 * (4) 下の帯に出す字、だけを決める。SDL にも GL にも通信にも触れない純粋な部品で、単体検査 `tools/hd2d_verify/test_nethack_mouse.cpp` が固定する。
 *
 * ## 原作の GUI 版と同じ所・違う所
 * - 同じ: 左クリック = `CLICK_1`、右クリック = `CLICK_2`、座標はマス番号（原作の `x` 1〜79、`y` 0〜20）。連続の 2 回目のクリックは `CLICK_2`
 *   （原作の `WM_LBUTTONDBLCLK`）。コアが命令を待っている間と位置の選択の間だけ返す（`nh_poskey` が呼ばれている間）。
 * - 違う: 原作の GUI 版には、マウスを載せたマスの説明は無い。ここでは HD2D の作りとして、マスの説明（`autodescribe` と同じ文）をコアに聞いて
 *   マウスの横に小さく出す。
 */
#pragma once

#include <cstdint>
#include <string>

namespace hd2d::nethack {

//! 原作の地図の大きさ（`COLNO`・`ROWNO`）。
inline constexpr int kNhMapCols = 80;
inline constexpr int kNhMapRows = 21;

//! 原作の `isok(x, y)`: x は 1〜79、y は 0〜20。
bool nh_map_cell_ok(int x, int y);

//! 地図のクリックの返事 `{"t":"nethack_reply","r":0,"a":"pos","x":…,"y":…,"mod":…}`。`mod` は 1 左・2 右。
std::string nh_pos_reply_json(int x, int y, int mod);

//! 地図のマスの説明の問い合わせ `{"t":"nethack_query","q":"hover","x":…,"y":…}`。
std::string nh_hover_query_json(int x, int y);

//! いまの場面（クリックを返してよいか）。
struct NhMapMouseContext {
    bool windows_active{ false }; //!< メニュー・問いなどの窓が開いている
    bool more_pending{ false };   //!< `--More--` の応答待ち
    bool map_click_wait{ false }; //!< コアが `command` か `pos` で待っている（原作の `nh_poskey`）

    bool allowed() const { return map_click_wait && !windows_active && !more_pending; }
};

struct NhPressResult {
    bool consumed{ false }; //!< 押下を食った（HD2D の標準のクリック移動へ回さない）
    std::string reply;      //!< コアへ送る返事（空なら送らない）
};

class NhMapMouse {
public:
    /*!
     * @brief 地図の上の押下。
     * @param button 1 左・2 右（それ以外は無視して食わない）
     * @param clicks 連続のクリックの数（2 以上で 2 回目以降）
     * @details 地図の上の押下は、返せない場面でも食う（HD2D の標準のクリック移動がコアを介さず歩かせないため）。
     */
    NhPressResult on_press(const NhMapMouseContext &ctx, bool on_map, int x, int y, int button, int clicks);

    /*!
     * @brief マウスの動き。説明を聞き直すなら問い合わせの JSON を返す（空なら聞かない）。
     * @param px,py マウスの画面の位置（説明の出し先）
     */
    std::string on_motion(const NhMapMouseContext &ctx, bool on_map, int x, int y, int px, int py, std::uint64_t wait_serial);

    //! 毎フレーム。マウスが同じマスに居ても、コアの待ちが新しくなっていれば（歩いた・敵が動いた）聞き直す。
    std::string on_tick(const NhMapMouseContext &ctx, std::uint64_t wait_serial);

    //! コアの答え（`hover`）。いまのマスの物だけ取る。
    void on_answer(int x, int y, const std::string &text);

    //! 説明を出す字と場所。出さない場面なら偽。
    bool tooltip(const NhMapMouseContext &ctx, std::string &text, int &px, int &py) const;

    //! マウスが載っているマス（地図の外なら偽）。
    bool hovered_cell(int &x, int &y) const;

    void reset();

private:
    std::string maybe_ask(const NhMapMouseContext &ctx, std::uint64_t wait_serial);

    bool cell_valid_{ false };
    int cx_{ 0 };
    int cy_{ 0 };
    int px_{ 0 };
    int py_{ 0 };
    bool asked_{ false };
    int asked_x_{ 0 };
    int asked_y_{ 0 };
    std::uint64_t asked_serial_{ 0 };
    bool answered_{ false };
    int ans_x_{ 0 };
    int ans_y_{ 0 };
    std::string text_;
};

/*!
 * @brief 下の操作の案内の帯に出す、いまの場面の案内（キーボードとマウス）。
 * @param wait_reason コアの待ち（`command`・`pos` ほか。空なら待っていない）
 * @details 窓が開いている間と `--More--` は `NhWindows::key_hint`・`NethackUi::key_hint` が出すので、ここは地図の上の場面だけ。
 * 末尾に空白 2 つを付ける（呼ぶ側が機能メニューなどの案内を続けて足す）。
 */
std::string nh_map_key_hint(bool ja, const std::string &wait_reason);

} // namespace hd2d::nethack
