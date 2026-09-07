/*!
 * @file xr_math.h
 * @brief 世界（マス）↔ ステージ（メートル）の写像・非対称射影・かきわりの方位角。
 *
 *
 * ## ここは全部「純関数」である
 * OpenXR にも GL にも触らない。**頭が無くても単体で回せる**ようにするためで、
 * §5 が要求する鏡像検査（東が右・南が手前）をここで閉じるのが目的である。
 * 左手系 → 右手系の写像は 1 箇所間違えると世界が丸ごと裏返り、
 * **往復検査では絶対に捕まらない**（`render/camera.h:124` が同じ穴を塞いだ前例）。
 *
 * ## 座標の約束
 * | | 世界（コアと `render/camera.h`） | ステージ（OpenXR） |
 * | --- | --- | --- |
 * | 単位 | 1 マス = 1.0 | メートル |
 * | 軸 | x = 東・y = 南・z = 上 | x = 右・y = 上・z = 手前 |
 * | 手 | **左手系**（東 × 南 = 下） | 右手系 |
 *
 * 写像は**軸の入れ替え 1 回**で足りる: 世界 (x, y, z) → ステージ (x, z, y)。
 * これで東 → +x（右）・南 → +z（手前）・上 → +y になり、手も正しく入れ替わる。
 *
 * @note 入れ替えは行列式が負（鏡映）なので、**表面は時計回りのまま**でよい
 * （フラットの `look_at` も同じ理由で負。`glFrontFace(GL_CW)` を変えないこと）。
 */
#pragma once

#include "render/math3d.h"

#include <string>

namespace hd2d::xr {

/*!
 * @brief 盤の据え方。**再中心（`recenter`）で決まり、次の再中心まで動かさない。**
 * @details 空間固定なので、頭を動かせば覗き込める（頭に貼り付けない——酔いの元）。
 */
struct BoardPlacement {
    //! 注視点のマスを置くステージ座標（メートル）。
    Vec3 origin{ 0.f, 0.f, 0.f };
    //! 盤の向き（ステージの y 軸まわり・ラジアン）。再中心したときの頭の向き。
    float yaw{ 0.f };
    //! 1 マスを何メートルにするか（`vr_tile_m`）。**仮の値**（§0）。
    float tile_m{ 0.04f };
};

//! 四元数 (x, y, z, w) でベクトルを回す。
Vec3 quat_rotate(const float q[4], const Vec3 &v);

/*!
 * @brief 世界 → ステージ。
 * @param board_center_world 盤の中心に置く世界座標（＝カメラの注視点。プレイヤ追従）。
 * @param board 盤の据え方。
 * @details `ステージ = T(origin) · Ry(yaw) · S(tile_m) · 入れ替え · T(-board_center)`。
 */
Mat4 world_to_stage(const Vec3 &board_center_world, const BoardPlacement &board);

/*!
 * @brief ステージ → 世界（`world_to_stage` の逆）。
 * @details **目の位置を世界座標へ戻すのに要る。** 減衰も鏡面もフォグも「マス」で
 * 書かれているので、ステージのメートルをそのまま渡すと光が全部消える。
 */
Vec3 stage_to_world(const Vec3 &stage, const Vec3 &board_center_world, const BoardPlacement &board);

/*!
 * @brief 目の姿勢（ステージ座標）→ 視点行列（ステージ → 視点）。
 * @details OpenXR の姿勢は「-z が前・+y が上・+x が右」で GL と同じ約束なので、
 * `inverse(T(pos) · R(q))` をそのまま組む。
 */
Mat4 eye_view_from_pose(const float position[3], const float orientation[4]);

/*!
 * @brief 非対称視錐台の射影（`XrFovf` の 4 つの角から）。
 * @param angle_left,angle_right,angle_up,angle_down ラジアン。左と下は負。
 * @details ランタイムが返す画角は**左右・上下が独立**なので、対称前提の
 * `perspective_horizontal` は使えない。深度は GL 既定の [-1, 1]。
 */
Mat4 projection_from_fov(float angle_left, float angle_right, float angle_up, float angle_down,
    float z_near, float z_far);

/*!
 * @brief かきわり（ビルボード）の方位角。**左右の目で 1 本を共用する。**
 * @param board 盤の据え方。
 * @param mid_orientation 左右の目の中点の向き（四元数）。
 * @return 世界座標での水平角（`Camera::azimuth()` と同じ約束）。
 * @details 目ごとに別の角を渡すと板が目ごとにねじれて立体視が壊れる（§5・罠 10）。
 */
float billboard_azimuth(const BoardPlacement &board, const float mid_orientation[4]);

/*!
 * @name 部屋と卓（2026-08-14 に決めた。設計書 §20）
 *
 * @details 「**ジオラマは VR 空間に畳 1 畳分くらいのテーブルがあり、そこにジオラマの
 * マップがある**」。盤を宙に浮かべるのをやめ、**実体のある卓の上に置く**。
 * 卓のまわりには暗い部屋を置く（何も無い方向を埋めるため）。
 *
 * ここは**ステージ空間（メートル）そのもの**で、世界（マス）は一切混ざらない。
 * 盤の縮尺を変えても卓は動かない——**卓は固定、縮尺は設定**（決めたこと）。
 * @{
 */
//! 畳 1 畳（メートル）。長辺を左右に置く（地図は横に長いので）。
inline constexpr float kTableWidthM = 1.82f;
inline constexpr float kTableDepthM = 0.91f;

/*!
 * @brief 部屋と卓の据え方。**再中心で決まり、次の再中心まで動かさない。**
 * @details 一人称では作らない（`visible == false`）——あちらは
 * 「**完全にゲーム空間に入り込む**」ので、部屋を置くと世界が二重になる。
 */
struct RoomPlacement {
    bool visible{ false };
    //! 部屋の中心（床の高さ・ステージ座標）。再中心したときの頭の真下。
    Vec3 center{ 0.f, 0.f, 0.f };
    //! 部屋と卓の向き（ステージの y 軸まわり）。再中心したときの頭の向き。
    float yaw{ 0.f };
    //! 天板の上面の中心（ステージ座標。y が天板の高さ）。**盤はここに載る。**
    Vec3 table_center{ 0.f, 0.72f, -0.70f };
    float table_w{ kTableWidthM };
    float table_d{ kTableDepthM };
    //! 部屋の内寸。
    float room_w{ 4.2f };
    float room_d{ 4.2f };
    float room_h{ 2.6f };
};

/*!
 * @brief 部屋と卓を置く。
 * @param head_position,head_orientation 頭（左右の目の中点）のステージ姿勢。
 * @param forward_m 頭から卓の中心までの水平距離。
 * @param height_m 天板の高さ。**0 以下なら頭の高さから導く**（決めたこと）。
 * @details 高さの導き方は `頭の高さ − 0.52m`（座って被れば約 0.70m ＝ 机、立てば
 * 約 1.15m ＝ 立ち机）。実物の机に着いたときの「目から天板まで」がおよそこれで、
 * 座り／立ちの設定を人に聞かずに済む。締めは 0.40〜1.15m。
 */
RoomPlacement place_room(const float head_position[3], const float head_orientation[4],
    float forward_m, float height_m);

/*!
 * @brief 再中心（ジオラマ）。**盤を卓の天板の上に載せる。**
 * @param room `place_room` の結果。
 * @param tile_m 1 マスのメートル数（設定。卓の大きさとは独立）。
 * @details 盤の中心（注視点のマス）が天板の中心の真上に来る。天板と同じ高さに
 * 置くと床の板と z 争いを起こすので、`kBoardLiftM` だけ浮かせてある。
 */
BoardPlacement recenter(const RoomPlacement &room, float tile_m);

//! 盤を天板からどれだけ浮かせるか（z 争いを避けるためだけの値）。
inline constexpr float kBoardLiftM = 0.002f;

/*!
 * @name 盤の 90° 回転
 *
 * @details **回すのは卓の上の盤だけ**である。部屋も卓も、空中の板（アンカー）も回さない
 * ——卓のまわりを歩く代わりの操作なので、家具と文字が一緒に回ったら意味が反転する。
 * 一人称 VR のスナップ振り向き（世界とアンカーごと 45°。§17.3）とは**別物**で、
 * 取り違えると「盤を回したら文字の板が後ろへ消える」（設計書 罠 8）。
 * @{
 */
//! 盤の回転の段数。**盤は 90° 刻みのまま 4 段**（平らな画面は 2026-09-06 に 45°×8 段になった）。
inline constexpr int kBoardTurnCount = 4;

/*!
 * @brief 盤に**追加の 90° 回転**を当てた据え方を返す。純関数。
 *
 * @param board 再中心で決まった据え方。`yaw` は**再中心したときの頭の向き**であり、
 *   ここは書き換えない（据え直しの基準を失うため）。回転は毎フレームここで重ねる。
 * @param turn 0..3（範囲外は 0..3 へ丸める）。
 *
 * @note **回る向きはフラットの `camera_turn` と揃えてある**（`board_turn_check()` が
 * 両者の一致を検査する）。手で符号を決めると必ず間違うので、検査で縛っている。
 */
BoardPlacement board_with_turn(const BoardPlacement &board, int turn);

/*!
 * @brief 盤を `turn` 段回したとき、実体の板へ入れる**世界の**固定ヨー（`SlabFacing::fixed_yaw`）。
 *
 * @details 板の厚みは 4 ボクセルしか無いので、盤だけ回すと**縁しか見えなくなる**。
 * ここは「盤の回転をちょうど打ち消す世界の角」を返す。
 * 写像は `stage = Ry(ψ) · 入れ替え · world` で、固定ヨー θ の板の法線は
 * ステージで `Ry(ψ − θ) · (0,0,1)` へ写る。したがって **θ を盤に足した角と同じにすれば
 * ψ − θ が変わらない**——回す前と同じ向きに立ち続ける。
 */
float board_turn_slab_yaw(int turn);
/*! @} */
/*! @} */

/*!
 * @name 一人称 VR
 *
 * @details ジオラマとの違いは**縮尺と盤の置き場所だけ**である。写像そのものは同じ
 * `world_to_stage` を通る——1 マスを 4cm ではなく数メートルにして、盤の中心（＝プレイヤの
 * マス）を**利用者の足元**に置けば、それが一人称になる。
 *
 * **目の高さは足さない。**ステージ空間（STAGE）は床が原点なので、利用者の実際の身長が
 * そのまま目の高さになる。ここで `kFpsEyeHeight` を足すと二重になり、しかも
 * 「絵の中の身長」と「本当の身長」が食い違って酔う。
 * @{
 */
/*!
 * @brief 一人称の縮尺を頭の高さから導く。
 * @param head_y 頭（目の中点）の床からの高さ（メートル）。
 * @param eye_height_cells 平らな画面での目の高さ（マス。`kFpsEyeHeight`）。
 * @return 1 マス何メートルか。
 * @details **平らな画面の絵と同じ比になるように決める。**目の高さが 0.55 マスなら、
 * 身長 1.65m の人にとって 1 マスは 3m である。こうすると壁の上端が画面で見えるか／
 * 見えないかが平らな画面と一致する（`kFpsEyeHeight` の doc が言う「1.0 に近づけると
 * 壁の上が見えて迷路が迷路でなくなる」がそのまま保たれる）。
 *
 * 頭の高さが取れない（座っている・LOCAL 空間で 0 に近い）ときは既定へ落とす。
 */
float first_person_cell_m(float head_y, float eye_height_cells);

/*!
 * @brief 一人称の据え方。**盤の中心（プレイヤのマス）を利用者の足元に置く。**
 * @param head_position,head_orientation 頭のステージ姿勢。
 * @param cell_m 1 マスのメートル数（`first_person_cell_m`）。
 */
BoardPlacement first_person_recenter(const float head_position[3], const float head_orientation[4], float cell_m);

/*!
 * @brief 頭が向いている方位（世界の角）。
 * @return `FpsMode::facing` と同じ約束（**0 = 北・時計回りが正**）。
 * @details **一人称 VR では頭が照準である。**歩く向きも実体の向きもここから決まる。
 * 右スティックで首を回す作りにすると HMD と喧嘩する（頭が既に向きを持っている）。
 */
float head_azimuth(const BoardPlacement &board, const float head_orientation[4]);

/*!
 * @brief 一人称の near/far。
 * @param cell_m 1 マスのメートル数。
 * @details **平らな一人称と同じ 0.05〜160 マス**（`Camera::depth_range`）をメートルへ
 * 直したもの。視程（20 マス）で切ると天球（半径 50）も雲（高さ 40）も視錐台の外へ
 * 出てしまい、**一人称 VR で空が丸ごと消える**（設計書 §20・罠 26）。
 */
void depth_range_for_first_person(float cell_m, float &z_near, float &z_far);
/*! @} */

/*!
 * @name 空中の板（2026-08-14 に決めた。設計書 §7・§20・§22）
 *
 * @details **画面をそのまま 1 枚で出す。**矩形ごとにばらして頭のまわりへ散らす作りを
 * 一度やったが、実機では「位置がぐちゃぐちゃ」だった（§22）。指示は 3 つ:
 *
 * 1. **分割はやめて、そのまま並べる**
 * 2. **無理に HMD の方へ向けない**（傾けない）
 * 3. **向いている方角の頭上に固定**——頭に貼り付けると振り向きについてきて疲れる、
 *    北に固定すると旋回して見えなくなる。**遊びの角の外へ出たぶんだけ**付いてくる
 *
 * ## 大きさは「1 桁の見込み角」で決まる（§16 の勘定）
 * 板の解像度を上げても字は読みやすくならない——**桁が増えるだけ**である。効くのは
 * 1 桁あたりの見込み角で、Quest 2 は 20.6 画素/度。だから寸法は
 * 「画素数 × `deg_per_px`」から出す（メートルで持たない）。
 * @{
 */
/*!
 * @brief 画面の部位。**VR の一人称で「出す／出さない」を選ぶ単位**
 * （2026-08-14 に決めた「FPS モードの際の各パネルの ON/OFF」）。
 * @details 板そのものは 1 枚なので、これは**そこへ描くかどうか**の話である。
 */
enum class PanelSlot : int {
    Status = 0, //!< 状態列（HP/MP・能力値・階）
    Sub1, //!< サブパネル 1（既定はメッセージ）
    Sub2,
    Sub3,
    Sub4,
    Sub5,
    Minimap,
    Message, //!< メッセージ帯（`Split` では空。Sub1 が家のとき出ない）
    Prompt, //!< `[y/n]` の 1 行
    Bottom, //!< ステータス行と操作ヒント
    Count,
};

//! cfg のキーと機能メニューに出す綴り（`status` など）。**綴りはここ 1 か所**。
const char *panel_slot_key(PanelSlot slot);
//! 人が読む名前（機能メニューの行に出す）。
const char *panel_slot_label(PanelSlot slot);

//! 板 1 枚の据わり方（ステージ座標）。
struct PanelPose {
    Vec3 center{ 0.f, 0.f, 0.f };
    //! 向き（四元数 x,y,z,w）。板の面は自分の +z。**y 軸まわりだけ**（傾けない）。
    float orientation[4]{ 0.f, 0.f, 0.f, 1.f };
    float width_m{ 1.2f };
    float height_m{ 0.7f };
};

/*!
 * @brief 板を置く（**画面をそのまま 1 枚で**。2026-08-14 に決めた）。
 * @param head_position,head_orientation 頭（左右の目の中点）の**いまの**ステージ姿勢。
 * @param px_w,px_h 板に写す画面の大きさ（画素）。
 * @param deg_per_px 1 画素の見込み角（度）。**読み味はこれ 1 つで決まる。**
 * @param lift_deg 目線から**上へ**何度。
 * @param dist_m 頭から板までの距離。
 *
 * @details 指示は 3 つ。
 * 1. **分割はやめて画面をそのまま並べる**（矩形ごとにばらすと、実機では位置が
 *    ちりぢりに見えた）
 * 2. **無理に HMD の方へ向けない**（傾けない。y 軸まわりだけ）
 * 3. **キャラの向いている方角の頭上に固定**——だから毎フレーム
 *    「いまの頭」の水平向きで置き直す。上下（仰角）と首の傾げには**付いてこない**ので、
 *    見上げれば読め、正面を向けば視界が空く。一人称では頭がキャラの向きそのものである
 *
 * 空間に固定（`HeadAnchor`）ではないのが盤・卓との違いである——盤は覗き込みたいが、
 * 板は振り向いた先にも要る。
 */
PanelPose place_hud_panel(const float head_position[3], float panel_yaw, int px_w, int px_h,
    float deg_per_px, float lift_deg, float dist_m);

/*!
 * @brief 板の向きを「向いている方角」へ寄せる（**頭には貼り付けない**）。
 * @param[in,out] panel_yaw 板のいまの向き（ステージの y 軸まわり）。
 * @param head_orientation いまの頭の姿勢。
 * @param dead_deg 遊びの角。**この範囲で首を振っても板は動かない。**
 * @param dt 前のフレームからの経過秒。
 *
 * @details 2026-08-14 に決めたは 2 つあって、両方を同時に満たす必要がある:
 * 「北固定だと**旋回すると見えなくなる**」「頭に固定だと**振り向きについてきて疲れる**」。
 * だから**遊びの角の外へ出たぶんだけ**、ゆっくり付いてくる形にした。
 * 首を少し振るぶんには板は空間に残り、大きく向きを変えると連れてくる。
 *
 * `dead_deg` を 0 にすれば頭に貼り付き、180 にすれば空間固定になる（どちらも設定で選べる）。
 */
void follow_panel_yaw(float &panel_yaw, const float head_orientation[4], float dead_deg, float dt);

/*!
 * @brief 再中心したときの頭。**盤・卓・板はすべてこれを基準に据わる。**
 * @details 板の置き場所を毎フレームここから組み直すので、
 * 割り付けが変わっても（サブパネルの開閉・境界の移動）板がついてくる。
 * 「いまの頭」ではなくこれを使うのが要点——いまの頭で組むと板が顔に貼り付く。
 */
struct HeadAnchor {
    float position[3]{ 0.f, 0.f, 0.f };
    float orientation[4]{ 0.f, 0.f, 0.f, 1.f };
};

/*!
 * @brief 基準の頭を回す（スナップ振り向き。§17.3）。
 * @param pivot 回転の中心（一人称なら足元＝盤の原点）。
 * @details 世界が 45° 回るときに板だけ置いていかれないように、**基準ごと**回す。
 * 板はここから組み直されるので、1 か所回せば全部の板が一緒に回る。
 */
void rotate_anchor(HeadAnchor &anchor, const Vec3 &pivot, float delta_yaw);
/*! @} */

/*!
 * @brief 視点から盤と部屋の包絡までで near/far を機械的に決める（手で当てない）。
 * @param eye_stage 目の位置（ステージ）。
 * @param board 盤の据え方。
 * @param room 部屋の据え方（`visible` が偽なら盤だけで採る）。
 * @param cols,rows 盤の広さ（マス）。
 * @param max_height 立ち上がるものの高さ（マス）。
 * @param[out] z_near,z_far
 * @details `Camera::depth_range` と同じ思想。包絡球で採るので、覗き込んで
 * 盤に顔を寄せても near が張り付かない。**部屋を入れ忘れると壁と天井が切れる。**
 */
void depth_range_for_board(const Vec3 &eye_stage, const BoardPlacement &board, const RoomPlacement &room,
    int cols, int rows, float max_height, float &z_near, float &z_far);

/*!
 * @brief **鏡像検査** — 東が右に、南が手前に出るか。**頭が無くても回せる。**
 * @param[out] report 人が読む結果。
 * @return 正しければ true。
 * @details 世界 → ステージ → 視点 まで通して、視点空間で
 * 「東へ 1 マスは x が増える」「南へ 1 マスは手前（z が増える）に来る」
 * 「上へ 1 マスは y が増える」を見る。§5 が M1 の完了条件に入れている検査の、
 * 機械で回せるほうの半分（残り半分は実機で目で見る）。
 */
bool orientation_check(std::string &report);

/*!
 * @brief **盤の 90° 回転の検査**。**頭が無くても回せる。**
 * @param[out] report 人が読む結果。
 * @return 正しければ true。
 *
 * @details 見るのは 3 つで、**どれも符号を 1 つ間違えると落ちる**:
 *
 * 1. **フラットの回転と向きが一致すること。**盤を `turn` 段回したときに「ステージの右」が
 *    指す世界の方向が、平らな画面で `rotate_screen_delta(turn, 1, 0)` が返す方向と同じであること。
 *    ここが揃っていないと、同じ「右へ 1 段」がフラットと VR で逆に回り、
 *    利用者は 2 つの別々の約束を覚えることになる
 * 2. **実体の板が回っても正面を向き続けること**（`board_turn_slab_yaw`）。板の法線が
 *    ステージ空間で `turn` に依らず同じ向きへ写ること
 * 3. **4 段回すと元に戻ること**（`turn = 4` ≡ `turn = 0`）
 *
 * @note 1 を検査に入れてあるのが要点である。盤の回る向きを doc の言葉で決めようとすると
 * 「右手系のステージで Ry が正なら…」の議論になって取り違える。
 * **フラット側を正として突き合わせれば、符号は導出されるのであって選ばれない。**
 */
bool board_turn_check(std::string &report);

} // namespace hd2d::xr
