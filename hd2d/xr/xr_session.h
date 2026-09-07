/*!
 * @file xr_session.h
 * @brief OpenXR の instance / session / swapchain とフレーム同期。
 *
 * 基準は （§2 構成・§4 フレームループ・§6 両眼描画）。
 *
 * ## この層が引き受けるもの
 * - **プラットフォーム依存はここに閉じ込める**（設計書 §2）。`XR_USE_PLATFORM_WIN32` /
 *   `XR_USE_PLATFORM_ANDROID` も、WGL / EGL の APIも、このヘッダの外へ出さない。
 *   **この約束のおかげで Quest ネイティブは `.cpp` の中の島 8 つで済んだ**
 * - 状態機械（IDLE〜FOCUSED〜STOPPING）と `xrPollEvent` の吸い出し（罠 7）
 * - 目ごとの swapchain と、そこへ結んだ FBO
 *
 * ## この層が引き受けないもの
 * - 世界 → ステージの写像・射影行列（`xr_math`。M1）
 * - 入力（`xr_input`。M3）
 * - 何を描くか。`begin_frame` が返した `Frame` を見て呼ぶ側が描く
 *
 * ## 使い方
 * @code
 * Session xr;
 * if (!xr.init(err)) { フラットで続ける }
 * ...
 * Frame frame;
 * if (xr.begin_frame(frame)) {          // 描く回だけ true
 *     for (int eye = 0; eye < frame.view_count; ++eye) {
 *         xr.bind_eye(eye);             // その目の swapchain 画像が描き先になる
 *         ... 描く ...
 *     }
 *     xr.end_frame();                   // release + xrEndFrame
 * }
 * @endcode
 * `begin_frame` が false を返しても**呼び続けること**。セッションが起きるのを待つのも
 * ランタイムのイベントを吸うのも、この関数の中でやっている。
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace hd2d::xr {

//! 目 1 つぶんの、この 1 フレームの描き先と姿勢。
struct EyeView {
    //! 描き先の大きさ（`xrEnumerateViewConfigurationViews` の推奨値。自分で決めない）。
    int width{ 0 };
    int height{ 0 };
    /*!
     * @name 姿勢と画角（M1 で使う。M0 では埋めるだけ）
     * @details 位置はメートル・**右手系 y 上**のステージ座標。向きは四元数 (x,y,z,w)。
     * 画角は**非対称**（左右上下がそれぞれ独立した角。単位はラジアン）なので、
     * 対称前提の `perspective_horizontal` は使えない（設計書 §5）。
     * @{
     */
    float position[3]{ 0.f, 0.f, 0.f };
    float orientation[4]{ 0.f, 0.f, 0.f, 1.f };
    float fov_left{ 0.f };
    float fov_right{ 0.f };
    float fov_up{ 0.f };
    float fov_down{ 0.f };
    /*! @} */
};

/*!
 * @brief 空中の板 1 枚（`Session::set_ui_panels`）。
 * @details 中身は「UI テクスチャのどこを」「空間のどこへ」の 2 つだけ。
 * 角度や大きさの決め方は `xr_math` の領分で、ここは受け取った値を積むだけである。
 */
struct UiPanel {
    /*!
     * @name テクスチャの中の矩形（画素・**左上原点**。`ui_layout` の矩形そのまま）
     * @{
     */
    int rect_x{ 0 };
    int rect_y{ 0 };
    int rect_w{ 0 };
    int rect_h{ 0 };
    /*! @} */
    //! ステージ座標（メートル）。
    float center[3]{ 0.f, 0.f, 0.f };
    //! 向き（四元数 x,y,z,w）。板の面は自分の +z。
    float orientation[4]{ 0.f, 0.f, 0.f, 1.f };
    float width_m{ 0.4f };
    float height_m{ 0.2f };
};

//! `begin_frame` が返す、この 1 フレームのぶん。
struct Frame {
    //! 目の数（プライマリステレオなので 2）。
    int view_count{ 0 };
    EyeView views[2]{};
    /*!
     * @brief ランタイムが「この時刻に表示する」と予測した時刻（ナノ秒）。
     * @details **姿勢はこの時刻で引く。** `SDL_GetTicks64` の時刻で引くと首振りで
     * 絵が遅れて張り付く（設計書 罠 8）。
     */
    std::int64_t predicted_display_time{ 0 };
    //! 姿勢が取れたか。取れていない回は絵を出さない（レイヤを積まない）。
    bool views_valid{ false };
};

/*!
 * @brief OpenXR のセッション 1 つ。
 * @details 作れなければ `init` が false を返すだけで、呼ぶ側はフラットで続けてよい
 * （`--vr` は「VR で起動を試みる」旗であって、失敗を致命にはしない。設計書 §10）。
 */
class Session {
public:
    Session() = default;
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    /*!
     * @brief instance / system / session / swapchain までを作る。
     * @param err 失敗の理由（人に見せる文）。
     * @return 作れたか。
     * @note **GL コンテキストが current な状態で呼ぶこと。** ランタイムへ渡す
     * `HDC` / `HGLRC` を「いま current なもの」から取るため。
     */
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 目の描き先をランタイムの推奨値の何倍で作るか。
     * @param scale 0.5〜1.0。**既定の 1.0 では推奨値をそのまま使う**（1 画素も動かさない）。
     * @details 掛かるのは `init` が swapchain を作るときだけなので、**`init` より前に呼ぶこと。**
     * 後から変えても次に立て直すまで効かない（swapchain は作り直す手段を持たない）。
     * cfg の `vr_render_scale` が唯一の口で、Quest で 72Hz が出ないときに落とすためにある。
     */
    void set_render_scale(float scale) { this->render_scale_ = std::clamp(scale, 0.5f, 1.f); }

    //! instance と session を持っているか（`init` が通ったか）。
    bool valid() const { return this->valid_; }
    //! ランタイムの名前と版（起動行ログに出す。設計書 罠 1）。
    const std::string &runtime_name() const { return this->runtime_name_; }
    //! セッションが**いま** FOCUSED（入力も届く状態）か。
    bool focused() const { return this->focused_; }
    /*!
     * @brief **一度でも** FOCUSED に達したか。
     * @details 判定にはこちらを使う。セッションは
     * `FOCUSED → VISIBLE → SYNCHRONIZED → STOPPING → IDLE → EXITING` と自然に降りてくる
     * （ランタイムを畳めばそうなる）ので、終わり際の値を見ると「達しなかった」に化ける。
     */
    bool focus_reached() const { return this->focus_reached_; }
    //! ここまでに見たセッション状態の名前を古い順に（検証用。`--vr-check`）。
    const std::vector<std::string> &state_log() const { return this->state_log_; }
    /*!
     * @name 生のハンドル（`xr_input` へ渡すためだけ）
     * @details `openxr.h` をヘッダの外へ出さないための逃げ道。XrInstance / XrSession は
     * どちらもポインタ大の不透明ハンドルなので `void *` を往復できる。
     * **これ以外の用途で使わないこと**（使い始めると §2 の「閉じ込める」が崩れる）。
     * @{
     */
    void *native_instance() const;
    void *native_session() const;
    /*! @} */

    /*!
     * @brief 1 フレームの頭。イベントを吸い、描く回なら `xrBeginFrame` まで進める。
     * @param out 描く回だけ埋まる。
     * @return この回に絵を描くか。false でも**必ず呼び続けること**。
     * @details false の中身は 2 通りある。(a) セッションがまだ起きていない
     * （IDLE / SYNCHRONIZED）、(b) 起きているがランタイムが「今回は描かなくてよい」と
     * 言った（`shouldRender == false`）。(b) では `xrBeginFrame`/`xrEndFrame` の対を
     * 内部で閉じてある。
     */
    bool begin_frame(Frame &out);
    /*!
     * @brief その目の swapchain 画像を描き先にする（FBO を結び viewport を張る）。
     * @return 結べたか。
     */
    bool bind_eye(int eye);
    /*!
     * @brief 直前に `bind_eye` で結んだ FBO の名前。
     * @details ポスト処理の出し先に渡すために要る（`PostChain::resolve` の `target_fbo`）。
     * 目ごとに結び直して使い回す 1 枚なので、**`bind_eye` の直後に取ること**。
     */
    unsigned int eye_framebuffer() const;
    /*!
     * @name 文字 UI のクワッドレイヤ（§7）
     * @details **UI はシーンに混ぜない。**クワッドレイヤはコンポジタが表示解像度で
     * 直接標本化するので、シーン経由の再投影ぼけが乗らない。文字の可読性はここで確保する。
     * @{
     */
    /*!
     * @brief UI の板を作る（1 回だけ呼ぶ）。
     * @param w,h 板の中身の解像度（画素）。
     * @return 作れたか。作れなくても VR は続く（板が出ないだけ）。
     * @details 板が何枚になっても**テクスチャは 1 枚**である（`set_ui_panels` を見よ）。
     */
    bool create_ui_layer(int w, int h, std::string &err);
    /*!
     * @name 板のテクスチャの大きさ（**窓と一致していること**）
     * @details 呼ぶ側は `ui_layout` の矩形（窓の座標）で板を指すので、窓の大きさが
     * 変わったのにここが古いままだと、**板が別の場所を写す**。全画面への切り替えや
     * 窓の拡大で実際にそうなった（設計書 §20 罠 30）。毎フレーム見比べて、
     * 違っていたら `create_ui_layer` を呼び直すこと（中で作り直す）。
     * @{
     */
    int ui_width() const { return this->ui_width_; }
    int ui_height() const { return this->ui_height_; }
    /*! @} */
    /*!
     * @brief 空中の板をまとめて置く（2026-08-14 に決めた。設計書 §20）。
     * @param panels 板の配列。
     * @param count 枚数（多すぎるぶんは黙って落とす。ランタイムの上限まで）。
     * @details **1 枚のテクスチャの部分矩形**をそれぞれの板が指す
     * （`XrSwapchainSubImage::imageRect`）。だから画面は今までどおり 1 回描くだけでよく、
     * 3D の穴は「板にしない」だけで消える。毎フレーム呼んでよい。
     */
    void set_ui_panels(const struct UiPanel *panels, int count);
    //! ランタイムが受けるレイヤの上限（`XrSystemProperties`。仕様の下限は 16）。
    int max_layers() const { return this->max_layers_; }
    /*!
     * @brief この回の板を描き先にする。
     * @return FBO の名前。0 なら板が無い（呼ぶ側は今までどおり窓へ描く）。
     * @note **透明で消してある。**描かなかった所はシーンが透ける。
     */
    unsigned int bind_ui();
    /*! @} */

    //! `begin_frame` が true を返した回の締め。swapchain を返してレイヤを積む。
    void end_frame();

    /*!
     * @brief その目の絵を既定の枠バッファ（窓）の矩形へ写す（鏡窓）。
     * @details VR 中もフラットと同じ検証路（`--shot=`）が生きるように、窓には絵を出しておく。
     * 矩形を渡せるのは、左右を並べて 1 枚に収めたいから（`--vr-check`）。
     */
    void blit_mirror(int eye, int dst_x0, int dst_y0, int dst_x1, int dst_y1);

private:
    //! 溜まったイベントを空になるまで吸う。false ならもう畳んでよい（罠 7）。
    bool poll_events();
    void destroy_swapchains();
    /*!
     * @brief `xrWaitFrame` が返った所で 1 回呼ぶ（vr-perf の集計と、5 秒ごとの 1 行）。
     * @param eye_w,eye_h 目の描き先の大きさ（行に出す。`vr_render_scale` が効いているかの証拠）。
     * @details 数えるのは**間隔**なので、区間の最初の 1 本（起きるまでの待ちが混ざる）は
     * 捨てる。`--vr-check` の勘定と同じ考え方である。目の寸法を引数で受けるのは、
     * この関数を**プラットフォームの分岐の外**に 1 本だけ置くため（`Impl` に触らない）。
     */
    void perf_tick(int eye_w, int eye_h);

    /*!
     * 中身は `xr_session.cpp` の中だけで持つ。**`openxr.h` と `windows.h` を
     * このヘッダへ持ち込まないため**（設計書 §2「プラットフォーム依存を閉じ込める」）。
     */
    struct Impl;
    Impl *impl_{ nullptr };

    /*!
     * @name vr-perf の 1 行ログ
     *
     * @details VR 中、**約 5 秒ごとに `xrWaitFrame` の間隔**を stderr へ 1 行出す。
     * @code
     * [hd2d] vr-perf: wait avg=13.9 min=13.2 max=41.0 ms frames=358 eye=1080x1188
     * @endcode
     * `xrWaitFrame` はランタイムの表示周期まで待つので、**間隔がそのまま歩調**である
     * （72Hz なら 13.9ms。落ちれば 27.8ms へ跳ぶ）。どの場面が何 ms かは実機の
     * logcat からしか読めないので、集計は**ここで完結させる**——呼ぶ側に撒くと
     * 実行体ごとに違う数え方になる（`--vr-check` は同じ勘定を自前で持っているが、
     * あちらは 1 回きりの検査で、こちらは遊んでいる最中の記録である）。
     *
     * コストは 1 フレームに `steady_clock::now()` 1 回と足し算だけ。
     * @{
     */
    //! 直前の `xrWaitFrame` が返った時刻（ナノ秒。`steady_clock`）。0 = まだ 1 回も。
    std::int64_t perf_last_ns_{ 0 };
    //! この区間の頭の時刻（ナノ秒）。ここから 5 秒でまとめて 1 行出す。
    std::int64_t perf_window_ns_{ 0 };
    double perf_sum_ms_{ 0.0 };
    double perf_min_ms_{ 0.0 };
    double perf_max_ms_{ 0.0 };
    int perf_frames_{ 0 };
    /*! @} */

    std::string runtime_name_;
    std::vector<std::string> state_log_;
    int max_layers_{ 16 };
    int ui_width_{ 0 };
    int ui_height_{ 0 };
    //! 目の描き先の倍率（`set_render_scale`）。**1.0 は「推奨値のまま」の意味。**
    float render_scale_{ 1.f };
    bool valid_{ false };
    bool focused_{ false };
    bool focus_reached_{ false };
};

} // namespace hd2d::xr
