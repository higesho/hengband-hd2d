/*!
 * @file fps_mode.h
 * @brief FPS モード（一人称視点）の状態と入力の翻訳。**おまけ機能**
 *
 * ## 位置づけ（勝手に本線へ溶かさないこと）
 * 本線の見え方は「見下ろしの HD2D」で、カメラは**注視点の真南**に居る（`render/camera.h`）。
 * FPS モードはそれを一時的に横へ置く**別の見え方**で、切り替えたときだけ効く。
 * したがってこのファイルは
 *   - FPS のときにしか使わない定数
 *   - 方位（ヨー）の状態と、その動かし方
 *   - 「前後左右」→ コアの移動コマンド（テンキー番号）の翻訳
 * だけを持ち、描画にも設定にも手を出さない。`Camera` へは `apply()` で 1 回だけ触る。
 *
 * ## 向きの約束（**ここが唯一の定義**）
 * 世界は x = 東・y = 南・z = 上（`render/camera.h` と同じ）。
 *   - **ヨー 0 は北向き**（本線のカメラが向いている方向と同じ）
 *   - ヨーは**時計回りが正**（0 = 北 → 90° = 東 → 180° = 南 → 270° = 西）
 * 移動方向の丸め（`direction_key`）も同じ約束で書いてある。片方だけ直すと
 * 「前へ進んだのに横へ歩く」になるので、必ずこのファイルの中で揃えること。
 *
 * ## ゲームはターン制のままである
 * 見え方と入力の皮でしかない。W を押したら**そのとき向いている方角へ 1 歩**の
 * 移動コマンドをコアへ送るだけで、コアは FPS モードの存在を知らない。
 * 向き替え（Q / E・右スティック）は**ターンを消費しない**（コアへ何も送らない）。
 */
#pragma once

#include "render/camera.h"

namespace hd2d {

/*!
 * @name FPS モードの寸法（人間の指示による固定値。機能メニューには出さない）
 * @{
 */
/*!
 * @brief **中立の**見下ろし角（度）。5°（人間の指示）。水平よりわずかに下を向く。
 * @details 「視界を水平に戻す」（`level_view()`）が戻す先もここである。
 * 上下の視点移動を切っているときは、この値から動かない。
 */
inline constexpr float kFpsPitchDeg = 5.f;
/*!
 * @name 上下の視点移動の限界（度。2026-08-11 に決めた「仰角、下角も見れるように」）
 *
 * @details **真上・真下までは回さない。**視線が世界の上下軸と平行になると
 * `look_at()` の「上」が決まらず、画面が転がる（`math3d.h`）。60° で止めておけば
 * 天井も足元も十分に見えて、その縮退から遠い。
 * @{
 */
inline constexpr float kFpsPitchMaxDownDeg = 60.f; //!< 俯角（正が下）
inline constexpr float kFpsPitchMaxUpDeg = 60.f; //!< 仰角（負が上）
/*! @} */
/*!
 * @name 水平画角（度）。`Camera::fov_x` は**水平**で受けるのでそのまま渡せる
 *
 * @details 本線の既定 40° は見下ろしの絵を落ち着かせるための望遠寄りの値で、
 * 一人称で使うと望遠鏡を覗いているような窮屈さになる。一人称の慣習に寄せて広げる。
 *
 * **範囲は 90〜120°**（2026-08-11 に決めた「ホイールスクロール：カメラの画角の
 * 変更 90 度〜120 度の範囲で」）。ホイールで回すので、`FpsMode::fov_deg` が実効値になる。
 * @{
 */
inline constexpr float kFpsFovMinDeg = 90.f;
inline constexpr float kFpsFovMaxDeg = 120.f;
//! 既定。**下限そのもの**（いちばん歪みが少ない所から始めて、欲しい人だけ広げる）。
inline constexpr float kFpsFovDefaultDeg = 90.f;
//! ホイール 1 刻みで動く量（度）。6 刻みで下限から上限へ届く。
inline constexpr float kFpsFovStepDeg = 5.f;
/*! @} */
/*!
 * @brief 目の高さ（マス）。
 * @details 壁の高さ（構造壁はちょうど 1.0）に対して腰より上・天面より下。
 * ここを 1.0 に近づけると壁の上が見えてしまい、迷路が迷路でなくなる。
 */
inline constexpr float kFpsEyeHeight = 0.55f;
/*!
 * @brief Q／E を**離したとき**に吸い付く刻み（度）。8 方位にちょうど乗る。
 * @details 押しっぱなしの間は刻みを無視して連続で回る（`kFpsYawRateDegPerSec`）。
 * 離した所で最寄りの刻みへ寄せるので、**格子の 8 方位からは決してずれない**。
 * 移動が 8 方位しか無い游戯なので、向きだけ半端な角で止まると
 * 「見ている方へ歩けない」状態になる。
 */
inline constexpr float kFpsYawStepDeg = 45.f;
//! 回転の速さ（度／秒）。Q／E の押しっぱなしと、右スティックを振り切ったときの両方。
inline constexpr float kFpsYawRateDegPerSec = 200.f;
/*!
 * @brief 向き替えの追随の速さ（1/秒）。大きいほど瞬時に向く。
 * @details **効くのは刻みへ吸い付く最後の数度だけ**（押している間は連続で回るので
 * `yaw` は `facing` に貼り付いている）。瞬間に飛ばすと、どちらへ回ったのか分からなくなる。
 */
inline constexpr float kFpsYawFollowRate = 18.f;
/*!
 * @brief 可視窓を求めるときの打ち切り距離（マス）。
 * @details 見下ろし 5° では視錐台の上側の光線が地面と交わらない（地平線の上へ抜ける）ので、
 * `derive_view_window()` は打ち切り距離をそのまま窓の大きさにする。本線の 60 マスを
 * そのまま使うと 120 マス角を要求することになるため、一人称では視程で切る。
 */
inline constexpr float kFpsViewDistance = 20.f;
/*!
 * @brief ブロックを痩せさせる量（マス・**片側**）。
 *
 * @details 人間の指示「斜め移動が可能になっている場合はブロックがぴったりとせず
 * 隙間が空くように」。角で接する 2 つのブロックの間に `2 ×` この値の隙間が開き、
 * **斜めに抜けられることが一人称でも見て分かる**。
 *
 * **痩せるのは「隣がブロックでない辺」だけ**である。全周を痩せさせると、
 * 一直線に続く壁が 1 マスごとに割れて柵のように見えるうえ、隣り合う岩盤の間から
 * 背景が抜ける（`terrain_view.cpp` の `CellRole::Bedrock` に、以前それで
 * 四隅の欠け検査が 0.45% を出した経緯が書いてある）。
 */
inline constexpr float kFpsWallInset = 0.07f;
/*!
 * @name マウスの視点移動（2026-08-11 に決めた・**改訂**）
 *
 * @details 最初は「右ドラッグの間だけ動かし、離した所で 8 方位へ吸着」だったが、
 * 訂正が入って**ボタンを押さずにマウスを動かすだけで視界が動く**（＝ふつうの
 * 一人称の作り）に変わった。**吸着も無くなった**ので、
 *   - 右ボタンは**取消だけ**を意味する（視点移動とは無関係）
 *   - 方位は 45° の刻みに乗らない値のまま止まる
 * 歩く向きは `direction_delta()` が最寄りの 8 方位へ丸めるので、これで困らない。
 * @{
 */
//! マウス 1 画素あたり何度回るか。画面幅 1920 でおよそ 1 回転半。
inline constexpr float kFpsMouseYawDegPerPx = 0.22f;
//! 上下は横より鈍くする（同じ感度だと、少し手が上下しただけで空と床を向く）。
inline constexpr float kFpsMousePitchDegPerPx = 0.16f;
/*! @} */
/*!
 * @name 右スティックで視点を動かす速さ（度／秒）
 * @details 横（`kFpsYawRateDegPerSec` ＝ 200）より縦を鈍くするのはマウスと同じ理由。
 * @{
 */
//! 一人称の仰角・俯角。振り切って 60°（＝限界）まで 0.6 秒ほど。
inline constexpr float kFpsPadPitchDegPerSec = 100.f;
/*!
 * @brief **見下ろしの「見下ろし角の調整」**（2026-08-11 に決めた）。
 * @details 範囲は 10〜85 度なので、振り切って端から端まで 2.5 秒ほど。
 * 絵を詰めるための調整なので、速すぎると狙った角で止められない。
 */
inline constexpr float kCameraPitchDegPerSec = 30.f;
/*! @} */
/*! @} */

/*!
 * @brief FPS モードの状態。**アプリが 1 つ持つだけ**の素朴な値の入れ物。
 */
struct FpsMode {
    //! いま一人称か。切り替えは機能メニューで割り当てた F キー（既定 F7）。
    bool active{ false };
    /*!
     * @brief 画面に効いている方位（ラジアン）。`facing` へ滑らかに追いつく。
     * @details 描画だけがこちらを見る。移動の方向は `facing` の方を使う（下の doc）。
     */
    float yaw{ 0.f };
    /*!
     * @brief 向こうとしている方位（ラジアン）。**移動の方向はこちらで決める。**
     * @details Q を押した直後に W を押したとき、画面がまだ回り切っていなくても
     * 「回した先」へ歩いてほしいため。見た目の追随と操作の意味を分けておく。
     */
    float facing{ 0.f };
    /*!
     * @name 押しっぱなしの回転（`turn_hold_*` が使う）
     * @{
     */
    int turn_dir{ 0 }; //!< −1 / 0 / +1
    float turn_from{ 0.f }; //!< 押し始めた時点の `facing`（ちょん押しの判定に要る）
    /*! @} */
    /*!
     * @brief いま効いている水平画角（度）。ホイールで `kFpsFovMinDeg`〜`kFpsFovMaxDeg` を回る。
     * @details 一人称のときだけ使う。**見下ろしの画角（`Hd2dSettings::camera_fov_deg`）とは
     * 別の値**である（望遠寄りの 40° と広角の 90° を 1 つの数で兼ねられない）。
     */
    float fov_deg{ kFpsFovDefaultDeg };
    /*!
     * @brief いま効いている見下ろし角（度）。**正が下・負が上。**
     * @details 中立は `kFpsPitchDeg`（5° 下）。上下の視点移動（`vertical_look`）が
     * 入のときだけマウスで動き、「視界を水平に戻す」（`level_view()`）で中立へ帰る。
     */
    float pitch_deg{ kFpsPitchDeg };
    /*!
     * @brief 上下の視点移動を許すか（2026-08-11 に決めた「オプションで
     * 上下角の視野移動を切り替えられるように」）。
     * @details 呼び出し側が設定（`Hd2dSettings::fps_vertical_look`）から毎フレーム入れる。
     * **切に変えた瞬間に中立へ戻す**のは `advance()` の仕事（切ったのに上を向いたままだと、
     * 戻す手が無い）。
     */
    bool vertical_look{ true };
    /*!
     * @brief **見下ろしのときの方位**（ラジアン。0 = 北）。一人称を抜けたときの戻り先。
     *
     * @details 見下ろしの 90° 視点回転を入れるまで、
     * ここは常に 0（本線のカメラは必ず北）だったので `apply()` が直に 0 を書いていた。
     * 回せるようになった以上、戻り先は**回転した先**でなければならない
     * ——北へ戻すと、一人称へ入って出ただけで地図が回って見える。
     * 呼び出し側が毎フレーム `view_turn_yaw(camera_turn)` を入れる。
     * @note 一人称へ**入る**ときの初期方位もここになる（いま見ている方角から始まる）。
     */
    float base_yaw{ 0.f };

    //! 一人称へ入る／出る。方位は `base_yaw`（＝見下ろしで向いていた方角）へ揃える。
    void set_active(bool on);

    /*!
     * @name Q／E の向き替え。**押している間ずっと滑らかに回る**
     *
     * @details 刻み（45°）で飛ばすのではなく、押している間 `kFpsYawRateDegPerSec` で
     * 回し続け、**離した所で最寄りの刻みへ吸い付く**。
     *   - ちょん押し（回った角が半刻み未満）→ **ちょうど 45° 回る**
     *   - 長押し → 見ながら好きな向きで止められて、離すと格子へ揃う
     * 「刻みで飛ばす」だけだと一人称では画面が丸ごと入れ替わってどちらへ回ったか
     * 分からず、「連続だけ」だと 8 方位からずれて見ている方へ歩けなくなる。
     * @{
     */
    //! 押し始め。dir は −1（左＝Q）／+1（右＝E）。**キーリピートでは呼ばないこと。**
    void turn_hold_begin(int dir);
    //! 離した。回った角を見て、最寄りの刻み（またはちょうど 1 刻み）へ吸い付ける。
    void turn_hold_end(int dir);
    //! いま Q か E を押しているか（画面の案内に使う）。
    bool turning() const { return this->turn_dir != 0; }
    /*! @} */

    /*!
     * @brief 右スティック。`axis` は −1〜+1（右が正）。
     * @param dt 前のフレームからの経過秒。
     */
    void turn_analog(float axis, float dt);

    /*!
     * @brief マウスを動かした。**ボタンは要らない**（2026-08-11 に決めた・改訂）。
     * @param dx_px 右が正（`SDL_MouseMotionEvent::xrel`）。
     * @param dy_px 下が正（同 `yrel`）。**`vertical_look` が切なら使わない。**
     * @details 吸着はしない。方位は動かしたぶんそのままの角で止まる。
     */
    void look(int dx_px, int dy_px);

    /*!
     * @brief 視界を水平（中立の見下ろし角）へ戻す。
     * @details 2026-08-11 に決めた「FPS モードで視界を水平に戻すボタンを。
     * デフォルトではマウスホイール押し込みとコントローラー R3 キー」。
     * **方位（左右）は触らない。**「水平に戻す」は上下の話であって、
     * 向いている方角まで北へ戻されると、歩いていた向きを見失う。
     */
    void level_view();

    //! ホイール。`steps` は上へ回すと正。画角を `kFpsFovStepDeg` ずつ動かして範囲で丸める。
    void adjust_fov(int steps);

    //! 毎フレーム呼ぶ。`yaw` を `facing` へ寄せる。
    void advance(float dt);

    /*!
     * @brief 前後・左右の入力を、いまの向きから見た**8 方位**へ丸める。
     * @param forward 前 +1 ／ 後 −1 ／ 0
     * @param strafe  右 +1 ／ 左 −1 ／ 0
     * @param[out] dx 東が正（コアの `move` イベントと同じ向き）
     * @param[out] dy 南が正
     * @return 進む向きが決まったか（中立・打ち消し合いなら false）
     *
     * @details 向きが 45° 刻みで前後左右だけを押しているなら、丸めは**厳密に**方位へ乗る。
     * 右スティックで半端な方を向いているときだけ最寄りの 8 方位へ丸まる。
     */
    bool direction_delta(int forward, int strafe, int &dx, int &dy) const;

    /*!
     * @brief カメラを一人称に仕立てる。**毎フレーム必ず呼ぶこと。**
     * @param camera 触るのは `first_person` / `yaw` / `pitch` / `fov_x` / `eye_height` だけ。
     * `target`（プレイヤ位置）と viewport は呼び出し側が入れたものをそのまま使う。
     *
     * @details **`active` が偽のときは「何もしない」ではなく「本線へ戻す」。**
     * `Camera` は毎フレーム作り直さない持ち回りの値なので、素通りさせると
     * 一人称で回した `yaw` がそのまま残り、見下ろしへ戻った瞬間に地図が斜めを向く
     * （2026-08-11 に気づいた）。**本線のカメラは必ず北**（`yaw = 0`）である。
     * `pitch` / `fov_x` は呼び出し側が設定から毎フレーム当て直すのでここでは触らない。
     */
    void apply(Camera &camera) const;
};

} // namespace hd2d
