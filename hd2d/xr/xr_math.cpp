/*!
 * @file xr_math.cpp
 * @brief `xr_math.h` の実装。**OpenXR にも GL にも触らない純関数だけ。**
 */
#include "xr/xr_math.h"

#include "render/camera.h" //!< `rotate_screen_delta`（回る向きをフラットと突き合わせる検査で使う）

#include <algorithm>
#include <cmath>
#include <string>

namespace hd2d::xr {

namespace {

//! 世界 (x, y, z) → ステージ (x, z, y)。**この 1 枚が左手系 → 右手系の全部。**
Mat4 axis_swap()
{
    Mat4 out;
    out.m[0] = 1.f; // 列 0・行 0: 東 → +x（右）
    out.m[6] = 1.f; // 列 1・行 2: 南 → +z（手前）
    out.m[9] = 1.f; // 列 2・行 1: 上 → +y
    out.m[15] = 1.f;
    return out;
}

//! ステージの y 軸まわりの回転。
Mat4 rotation_y(float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = Mat4::identity();
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

//! 頭の向きの水平成分（正規化。真上／真下を向いていたら既定の -z）。
/*!
 * @details **盤・文字の板・一人称の 3 ところが同じこれを使う。**見上げたまま置き直しても
 * 盤が空へ飛ばないという規則を 3 か所で別々に書くと、必ず片方だけ直された状態になる。
 */
Vec3 flat_forward(const float orientation[4]);

} // namespace

Vec3 quat_rotate(const float q[4], const Vec3 &v)
{
    // v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    const Vec3 u{ q[0], q[1], q[2] };
    const float w = q[3];
    const Vec3 t = cross(u, v) + (v * w);
    return v + (cross(u, t) * 2.f);
}

namespace {

Vec3 flat_forward(const float orientation[4])
{
    const Vec3 forward = quat_rotate(orientation, Vec3{ 0.f, 0.f, -1.f });
    const float len = std::sqrt((forward.x * forward.x) + (forward.z * forward.z));
    if (len < 1e-4f) {
        return Vec3{ 0.f, 0.f, -1.f };
    }
    return Vec3{ forward.x / len, 0.f, forward.z / len };
}

} // namespace

Mat4 world_to_stage(const Vec3 &board_center_world, const BoardPlacement &board)
{
    const Mat4 center = translation(Vec3{ -board_center_world.x, -board_center_world.y, -board_center_world.z });
    return translation(board.origin) * rotation_y(board.yaw) * scaling(board.tile_m) * axis_swap() * center;
}

Vec3 stage_to_world(const Vec3 &stage, const Vec3 &board_center_world, const BoardPlacement &board)
{
    // 置き場所を外す → 盤の向きを外す → 縮尺を戻す → 軸を戻す → 盤の中心を足す。
    const Vec3 local = stage - board.origin;
    const float c = std::cos(-board.yaw);
    const float s = std::sin(-board.yaw);
    const Vec3 unrotated{ (local.x * c) + (local.z * s), local.y, (-local.x * s) + (local.z * c) };
    const float inv = (board.tile_m > 1e-9f) ? (1.f / board.tile_m) : 0.f;
    // ステージ (X, Y, Z) → 世界 (X, Z, Y)。入れ替えは自分自身が逆写像。
    return Vec3{ (unrotated.x * inv) + board_center_world.x, (unrotated.z * inv) + board_center_world.y,
        (unrotated.y * inv) + board_center_world.z };
}

Mat4 eye_view_from_pose(const float position[3], const float orientation[4])
{
    /*
     * 視点行列は「目のいる場所と向き」の逆である。回転は転置で逆になるので、
     * 基底 3 本を四元数で回してから**行として**並べる。
     */
    const Vec3 right = quat_rotate(orientation, Vec3{ 1.f, 0.f, 0.f });
    const Vec3 up = quat_rotate(orientation, Vec3{ 0.f, 1.f, 0.f });
    const Vec3 back = quat_rotate(orientation, Vec3{ 0.f, 0.f, 1.f }); //!< 前は -z なので、これは後ろ
    const Vec3 eye{ position[0], position[1], position[2] };

    Mat4 out = Mat4::identity();
    out.m[0] = right.x;
    out.m[4] = right.y;
    out.m[8] = right.z;
    out.m[1] = up.x;
    out.m[5] = up.y;
    out.m[9] = up.z;
    out.m[2] = back.x;
    out.m[6] = back.y;
    out.m[10] = back.z;
    out.m[12] = -dot(right, eye);
    out.m[13] = -dot(up, eye);
    out.m[14] = -dot(back, eye);
    return out;
}

Mat4 projection_from_fov(float angle_left, float angle_right, float angle_up, float angle_down,
    float z_near, float z_far)
{
    const float tan_left = std::tan(angle_left);
    const float tan_right = std::tan(angle_right);
    const float tan_up = std::tan(angle_up);
    const float tan_down = std::tan(angle_down);
    const float width = tan_right - tan_left;
    const float height = tan_up - tan_down;

    Mat4 out;
    out.m[0] = 2.f / width;
    out.m[5] = 2.f / height;
    out.m[8] = (tan_right + tan_left) / width; //!< 中心のずれ。ここが 0 でないのが「非対称」
    out.m[9] = (tan_up + tan_down) / height;
    out.m[10] = -(z_far + z_near) / (z_far - z_near);
    out.m[11] = -1.f;
    out.m[14] = -(2.f * z_far * z_near) / (z_far - z_near);
    return out;
}

float billboard_azimuth(const BoardPlacement &board, const float mid_orientation[4])
{
    // 頭の「右」をステージで採り、盤の向きを外し、軸を戻して世界の水平角にする。
    const Vec3 stage_right = quat_rotate(mid_orientation, Vec3{ 1.f, 0.f, 0.f });
    const float c = std::cos(-board.yaw);
    const float s = std::sin(-board.yaw);
    const Vec3 unrotated{ (stage_right.x * c) + (stage_right.z * s), stage_right.y,
        (-stage_right.x * s) + (stage_right.z * c) };
    // ステージ (X, Y, Z) → 世界 (X, Z, Y)。入れ替えは自分自身が逆写像。
    const Vec3 world_dir{ unrotated.x, unrotated.z, unrotated.y };
    if ((std::fabs(world_dir.x) < 1e-6f) && (std::fabs(world_dir.y) < 1e-6f)) {
        return 0.f; //!< 真上／真下を向いている。板は倒れているので角に意味が無い
    }
    return std::atan2(world_dir.y, world_dir.x);
}

RoomPlacement place_room(const float head_position[3], const float head_orientation[4],
    float forward_m, float height_m)
{
    RoomPlacement room;
    room.visible = true;

    //! 前は「頭の向きの水平成分」。見上げたまま再中心しても卓が空へ飛ばないように。
    const Vec3 flat = flat_forward(head_orientation);
    /*
     * `Ry(yaw) · (0,0,-1) == flat` を満たす角。`Ry` は (0,0,-1) を (-sin, 0, -cos) へ
     * 送るので、`yaw = atan2(-flat.x, -flat.z)` になる。**符号を 1 つ間違えると
     * 卓が 90 度ずれて据わる**ので、素の姿勢（-z 向き）で 0 になることを検査で押さえる。
     */
    room.yaw = std::atan2(-flat.x, -flat.z);
    //! 部屋は頭の**真下**を中心に置く（卓の側へ寄せない。歩ける余地を四方に残す）。
    room.center = Vec3{ head_position[0], 0.f, head_position[2] };

    /*
     * 天板の高さ。**頭の高さから導く**（2026-08-14 に決めた）。実物の机に着いた
     * ときの「目から天板まで」がおよそ 0.52m で、座れば約 0.70m・立てば約 1.15m に
     * 落ちる。頭の高さが取れない（LOCAL 空間で 0 に近い）ときは 0.72m へ倒す。
     */
    //! 手で入れた高さは**床（0.05m）まで**下げられる（2026-08-14 に決めた）。
    const float top = (height_m > 0.f)
        ? std::clamp(height_m, 0.05f, 1.40f)
        : ((head_position[1] > 0.8f) ? std::clamp(head_position[1] - 0.52f, 0.40f, 1.15f) : 0.72f);
    const float forward = std::max(0.3f, forward_m);
    room.table_center = Vec3{ head_position[0] + (flat.x * forward), top, head_position[2] + (flat.z * forward) };
    return room;
}

BoardPlacement recenter(const RoomPlacement &room, float tile_m)
{
    BoardPlacement board;
    board.tile_m = tile_m;
    board.yaw = room.yaw;
    //! 天板のちょうど上。同じ高さに置くと床の板と z 争いを起こす。
    board.origin = Vec3{ room.table_center.x, room.table_center.y + kBoardLiftM, room.table_center.z };
    return board;
}

float first_person_cell_m(float head_y, float eye_height_cells)
{
    //! 既定は 3m（目の高さ 0.55 マス × 3m ＝ 1.65m。成人の目の高さ）。
    constexpr float kFallback = 3.f;
    if ((head_y < 0.8f) || (eye_height_cells < 0.05f)) {
        return kFallback; //!< 座っている・床が取れていない。身長からは決められない
    }
    //! 極端な値で世界が伸び縮みしないように締める。
    return std::clamp(head_y / eye_height_cells, 1.5f, 6.f);
}

BoardPlacement first_person_recenter(const float head_position[3], const float head_orientation[4], float cell_m)
{
    BoardPlacement board;
    board.tile_m = std::max(0.1f, cell_m);
    const Vec3 flat = flat_forward(head_orientation);
    board.yaw = std::atan2(-flat.x, -flat.z);
    /*
     * **足元に置く。**y は 0（ステージの床）——世界の床を実際の床に合わせると、
     * 利用者の身長がそのまま目の高さになる。`kFpsEyeHeight` は足さない。
     */
    board.origin = Vec3{ head_position[0], 0.f, head_position[2] };
    return board;
}

float head_azimuth(const BoardPlacement &board, const float head_orientation[4])
{
    const Vec3 flat = flat_forward(head_orientation);
    // 盤の向きを外して、軸を世界へ戻す。
    const float c = std::cos(-board.yaw);
    const float s = std::sin(-board.yaw);
    const Vec3 unrotated{ (flat.x * c) + (flat.z * s), 0.f, (-flat.x * s) + (flat.z * c) };
    const Vec3 world{ unrotated.x, unrotated.z, unrotated.y }; //!< ステージ (X,Y,Z) → 世界 (X,Z,Y)
    //! `FpsMode` の約束（0 = 北 = -y・時計回り）。北を向いていれば world = (0,-1,0) → 0。
    return std::atan2(world.x, -world.y);
}

void depth_range_for_first_person(float cell_m, float &z_near, float &z_far)
{
    const float cell = std::max(0.1f, cell_m);
    /*
     * **平らな一人称と同じ 0.05〜160 マスをメートルへ直す**（`Camera::depth_range` の
     * 一人称の枝）。ここを視程（20 マス）で切っていたのが、一人称 VR で空が丸ごと
     * 消えていた正体である——天球は半径 50 マス・雲は高さ 40 マスにあるので、
     * 24 マスで切ると**どちらも視錐台の外**になる（2026-08-14 に決めた
     * 「fps は完全にゲーム空間（全天球）に入り込み」。設計書 §20・罠 26）。
     *
     * 近は 0.05 マス。1 マス 3m なら 0.15m で、壁（0.5 マス ＝ 1.5m）よりずっと手前。
     * 比は平らと同じ 3200:1 なので、深度の精度も同じである。
     */
    z_near = 0.05f * cell;
    z_far = std::max(z_near + 0.5f, 160.f * cell);
}

const char *panel_slot_key(PanelSlot slot)
{
    switch (slot) {
    case PanelSlot::Status:
        return "status";
    case PanelSlot::Sub1:
        return "sub1";
    case PanelSlot::Sub2:
        return "sub2";
    case PanelSlot::Sub3:
        return "sub3";
    case PanelSlot::Sub4:
        return "sub4";
    case PanelSlot::Sub5:
        return "sub5";
    case PanelSlot::Minimap:
        return "minimap";
    case PanelSlot::Message:
        return "message";
    case PanelSlot::Prompt:
        return "prompt";
    case PanelSlot::Bottom:
        return "bottom";
    case PanelSlot::Count:
    default:
        return "?";
    }
}

const char *panel_slot_label(PanelSlot slot)
{
    switch (slot) {
    case PanelSlot::Status:
        return "状態列";
    case PanelSlot::Sub1:
        return "サブ 1（メッセージ）";
    case PanelSlot::Sub2:
        return "サブ 2";
    case PanelSlot::Sub3:
        return "サブ 3";
    case PanelSlot::Sub4:
        return "サブ 4";
    case PanelSlot::Sub5:
        return "サブ 5";
    case PanelSlot::Minimap:
        return "ミニマップ";
    case PanelSlot::Message:
        return "メッセージ帯";
    case PanelSlot::Prompt:
        return "プロンプト";
    case PanelSlot::Bottom:
        return "最下段";
    case PanelSlot::Count:
    default:
        return "?";
    }
}

PanelPose place_hud_panel(const float head_position[3], float panel_yaw, int px_w, int px_h,
    float deg_per_px, float lift_deg, float dist_m)
{
    PanelPose pose;
    const float to_rad = 3.14159265f / 180.f;
    const float dist = std::max(0.2f, dist_m);

    //! 見込み角から寸法を出す。**画素数で決まる**（メートルで持たない。§16 の勘定）。
    const float width_deg = std::clamp(static_cast<float>(std::max(1, px_w)) * deg_per_px, 1.f, 160.f);
    pose.width_m = 2.f * dist * std::tan(width_deg * 0.5f * to_rad);
    pose.height_m = pose.width_m * (static_cast<float>(std::max(1, px_h)) / static_cast<float>(std::max(1, px_w)));

    /*
     * 置き場所。**向いている方角の、目線より `lift_deg` だけ上。**
     * 上下は頭に追わせない（見上げれば読め、正面を向けば視界が空く）。
     */
    const float lift = lift_deg * to_rad;
    const Vec3 forward{ -std::sin(panel_yaw), 0.f, -std::cos(panel_yaw) }; //!< `Ry(yaw) · (0,0,-1)`
    const float horizontal = dist * std::cos(lift);
    pose.center = Vec3{ head_position[0] + (forward.x * horizontal), head_position[1] + (dist * std::sin(lift)),
        head_position[2] + (forward.z * horizontal) };

    /*
     * 向きは y 軸まわりだけ。**傾けない**（2026-08-14 に決めた
     * 「無理に HMD の方に向けなくていい」）。首を傾げても字が水平に読める。
     */
    pose.orientation[0] = 0.f;
    pose.orientation[1] = std::sin(panel_yaw * 0.5f);
    pose.orientation[2] = 0.f;
    pose.orientation[3] = std::cos(panel_yaw * 0.5f);
    return pose;
}

void follow_panel_yaw(float &panel_yaw, const float head_orientation[4], float dead_deg, float dt)
{
    constexpr float kPi = 3.14159265f;
    const Vec3 flat = flat_forward(head_orientation);
    const float head_yaw = std::atan2(-flat.x, -flat.z);

    //! 角の差を −π〜+π へ畳む（そのまま引くと 359 度の差が「ほぼ 1 周」になる）。
    float diff = head_yaw - panel_yaw;
    while (diff > kPi) {
        diff -= 2.f * kPi;
    }
    while (diff < -kPi) {
        diff += 2.f * kPi;
    }

    const float dead = std::max(0.f, dead_deg) * (kPi / 180.f);
    const float over = std::fabs(diff) - dead;
    if (over <= 0.f) {
        return; //!< 遊びの中。**板は動かない**（首を振っても付いてこない）
    }
    /*
     * はみ出したぶんだけ寄せる。一気に合わせると板が飛ぶので、時定数 ~0.15 秒で追う。
     * 追いつく先は「遊びの縁」なので、大きく振り向いたときだけ連れてくる形になる。
     */
    const float t = std::clamp(dt * 6.f, 0.f, 1.f);
    panel_yaw += ((diff > 0.f) ? over : -over) * t;
}

void rotate_anchor(HeadAnchor &anchor, const Vec3 &pivot, float delta_yaw)
{
    const float c = std::cos(delta_yaw);
    const float s = std::sin(delta_yaw);
    const Vec3 rel = Vec3{ anchor.position[0], anchor.position[1], anchor.position[2] } - pivot;
    //! `Ry(delta)` を点に当てる（`rotation_y` と同じ式）。
    anchor.position[0] = pivot.x + ((rel.x * c) + (rel.z * s));
    anchor.position[2] = pivot.z + ((-rel.x * s) + (rel.z * c));
    //! 向きは y 軸まわりの四元数を**左から**掛ける（世界のほうが回るので）。
    const float hc = std::cos(delta_yaw * 0.5f);
    const float hs = std::sin(delta_yaw * 0.5f);
    const float x = anchor.orientation[0];
    const float y = anchor.orientation[1];
    const float z = anchor.orientation[2];
    const float w = anchor.orientation[3];
    anchor.orientation[0] = (hc * x) + (hs * z);
    anchor.orientation[1] = (hc * y) + (hs * w);
    anchor.orientation[2] = (hc * z) - (hs * x);
    anchor.orientation[3] = (hc * w) - (hs * y);
}

void depth_range_for_board(const Vec3 &eye_stage, const BoardPlacement &board, const RoomPlacement &room,
    int cols, int rows, float max_height, float &z_near, float &z_far)
{
    //! 盤の包絡球。**手で当てず、広さと高さから出す**（`Camera::depth_range` と同じ思想）。
    const float half_x = static_cast<float>(std::max(1, cols)) * 0.5f;
    const float half_y = static_cast<float>(std::max(1, rows)) * 0.5f;
    const float height = std::max(1.f, max_height);
    const float radius = board.tile_m * std::sqrt((half_x * half_x) + (half_y * half_y) + (height * height));
    const Vec3 to_center = board.origin - eye_stage;
    const float distance = std::sqrt(dot(to_center, to_center));

    //! 2cm より手前は切る。人は盤に顔を寄せられるので、これ以上は詰めない。
    z_near = std::max(0.02f, distance - radius);
    z_far = std::max(z_near + 0.05f, distance + radius);

    /*
     * **部屋も入れる。**盤の包絡だけで採ると far が 1m 少々になり、卓の向こうの壁と
     * 天井が丸ごと切れて「暗い部屋のはずが黒い虚空」になる（盤より部屋のほうが広い）。
     */
    if (room.visible) {
        const Vec3 to_room = room.center - eye_stage;
        const float half_diag = 0.5f * std::sqrt((room.room_w * room.room_w) + (room.room_d * room.room_d)
            + (room.room_h * room.room_h));
        z_far = std::max(z_far, std::sqrt(dot(to_room, to_room)) + half_diag);
    }
}

bool orientation_check(std::string &report)
{
    int failures = 0;
    report.clear();
    const auto expect = [&failures, &report](bool ok, const char *what, float got) {
        report += std::string("    ") + (ok ? "OK   " : "FAIL ") + what + " = " + std::to_string(got) + "\n";
        if (!ok) {
            ++failures;
        }
    };

    /*
     * **頭の向きを変えても同じ結論が出ること**が要点である。再中心すると盤は頭に
     * ついて回るので、「東は右・南は手前」は向きに依らず成り立たなければならない。
     * ここを 1 姿勢だけで見ていると、`recenter` の角の符号違い（盤が 90 度ずれて
     * 据わる）を素通りさせる。
     */
    struct HeadPose {
        const char *name;
        float position[3];
        float orientation[4]; //!< y 軸まわりだけ（x, y, z, w）
    };
    const float kSin45 = 0.70710678f;
    const HeadPose poses[] = {
        { "素の姿勢（-z を向いて原点）", { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f, 1.f } },
        { "左へ 90 度・少し歩いた", { 0.4f, 1.6f, -0.3f }, { 0.f, kSin45, 0.f, kSin45 } },
        { "右へ 90 度", { 0.f, 1.6f, 0.f }, { 0.f, -kSin45, 0.f, kSin45 } },
        { "真後ろ", { -1.f, 1.2f, 2.f }, { 0.f, 1.f, 0.f, 0.f } },
    };

    for (const HeadPose &pose : poses) {
        report += std::string("  ") + pose.name + "\n";
        const RoomPlacement room = place_room(pose.position, pose.orientation, 0.70f, 0.f);
        const BoardPlacement board = recenter(room, 0.04f);
        const Vec3 center{ 100.f, 50.f, 0.f }; //!< 注視点。原点から離した値でも成り立つこと
        const Mat4 whole = eye_view_from_pose(pose.position, pose.orientation) * world_to_stage(center, board);

        const auto apply = [&whole](const Vec3 &world) {
            const float x = (whole.m[0] * world.x) + (whole.m[4] * world.y) + (whole.m[8] * world.z) + whole.m[12];
            const float y = (whole.m[1] * world.x) + (whole.m[5] * world.y) + (whole.m[9] * world.z) + whole.m[13];
            const float z = (whole.m[2] * world.x) + (whole.m[6] * world.y) + (whole.m[10] * world.z) + whole.m[14];
            return Vec3{ x, y, z };
        };
        const Vec3 at_center = apply(center);
        const Vec3 at_east = apply(center + Vec3{ 1.f, 0.f, 0.f });
        const Vec3 at_south = apply(center + Vec3{ 0.f, 1.f, 0.f });
        const Vec3 at_up = apply(center + Vec3{ 0.f, 0.f, 1.f });

        //! **東は右**（視点空間の +x）。
        expect((at_east.x - at_center.x) > 0.03f, "東へ 1 マス → 視点 x が増える", at_east.x - at_center.x);
        //! **南は手前**（視点空間の z が増える＝目に近づく。前は -z なので）。
        expect((at_south.z - at_center.z) > 0.03f, "南へ 1 マス → 視点 z が増える（手前）",
            at_south.z - at_center.z);
        //! **上は上**（視点空間の +y）。
        expect((at_up.y - at_center.y) > 0.03f, "上へ 1 マス → 視点 y が増える", at_up.y - at_center.y);
        //! 東西と南北が入れ替わっていないこと（東で z が動かない・南で x が動かない）。
        expect(std::fabs(at_east.z - at_center.z) < 1e-4f, "東へ 1 マス → 視点 z は動かない",
            at_east.z - at_center.z);
        expect(std::fabs(at_south.x - at_center.x) < 1e-4f, "南へ 1 マス → 視点 x は動かない",
            at_south.x - at_center.x);
        //! 縮尺（1 マス = `tile_m`）。入れ替えで長さが変わっていないこと。
        expect(std::fabs((at_east.x - at_center.x) - 0.04f) < 1e-5f, "1 マス = 0.04m",
            at_east.x - at_center.x);
        /*
         * **盤は卓の天板の上に載っている**（§20）。前後は卓の中心までの距離そのもの、
         * 上下は「天板の高さ − 頭の高さ」。頭を動かしても向きを変えても同じでなければ
         * ならない——ここが姿勢ごとに変わるなら、卓が頭について回っている。
         */
        expect(std::fabs(at_center.z + 0.70f) < 1e-4f, "盤の中心は卓の中心（0.70m 先）の真上", -at_center.z);
        const float lifted = (room.table_center.y + kBoardLiftM) - pose.position[1];
        expect(std::fabs(at_center.y - lifted) < 1e-4f, "盤は天板の上に載っている", at_center.y - lifted);
        /*
         * かきわりの方位角。再中心の直後は頭の右＝世界の東なので、必ず 0 になる。
         * **フラットの `Camera::azimuth()` と同じ約束**（東が 0・南へ回るのが正）。
         */
        expect(std::fabs(billboard_azimuth(board, pose.orientation)) < 1e-4f,
            "再中心の直後の方位角は 0（東）", billboard_azimuth(board, pose.orientation));
    }

    /*
     * **盤は空間に固定されている**（頭に貼り付いていない）ことも見る。再中心したあと
     * 頭を右へずらしたら、盤は視点空間で左へ動かなければならない。ここが 0 のままなら
     * 盤が頭についてきており、覗き込めない＝酔いの元になっている。
     */
    {
        const float head[3] = { 0.f, 0.f, 0.f };
        const float rot[4] = { 0.f, 0.f, 0.f, 1.f };
        const BoardPlacement board = recenter(place_room(head, rot, 0.70f, 0.f), 0.04f);
        const Vec3 center{ 100.f, 50.f, 0.f };
        const float moved[3] = { 0.10f, 0.f, 0.f }; //!< 頭を右へ 10cm
        const Mat4 before = eye_view_from_pose(head, rot) * world_to_stage(center, board);
        const Mat4 after = eye_view_from_pose(moved, rot) * world_to_stage(center, board);
        report += "  頭を動かしても盤は空間に残るか\n";
        expect(std::fabs((after.m[12] - before.m[12]) + 0.10f) < 1e-5f,
            "頭を右へ 10cm → 盤は視点空間で 10cm 左", after.m[12] - before.m[12]);
    }

    /*
     * `stage_to_world` が `world_to_stage` の逆になっているか。**目の位置を世界へ戻す**のに
     * 使っており、ここがずれると光の減衰だけが静かに狂う（絵の形は正しいまま暗くなる）ので、
     * 目で気づけない。往復で押さえる。
     */
    {
        const float head[3] = { 0.3f, 1.5f, -0.2f };
        const float rot[4] = { 0.f, kSin45, 0.f, kSin45 };
        const BoardPlacement board = recenter(place_room(head, rot, 0.70f, 0.f), 0.04f);
        const Vec3 center{ 100.f, 50.f, 0.f };
        const Mat4 to_stage = world_to_stage(center, board);
        report += "  ステージ → 世界 の往復\n";
        const Vec3 probes[] = { center, center + Vec3{ 7.f, -3.f, 2.f }, center + Vec3{ -11.f, 5.f, 0.5f } };
        for (const Vec3 &world : probes) {
            const Vec3 stage{
                (to_stage.m[0] * world.x) + (to_stage.m[4] * world.y) + (to_stage.m[8] * world.z) + to_stage.m[12],
                (to_stage.m[1] * world.x) + (to_stage.m[5] * world.y) + (to_stage.m[9] * world.z) + to_stage.m[13],
                (to_stage.m[2] * world.x) + (to_stage.m[6] * world.y) + (to_stage.m[10] * world.z) + to_stage.m[14],
            };
            const Vec3 back = stage_to_world(stage, center, board);
            const Vec3 gap = back - world;
            expect(std::sqrt(dot(gap, gap)) < 1e-3f, "世界 → ステージ → 世界 が戻る",
                std::sqrt(dot(gap, gap)));
        }
    }

    /*
     * 卓の高さ（§20。2026-08-14 に決めた「頭の高さから導出」）。座って被っても
     * 立って被っても腹〜胸の高さに来ること、頭の高さが取れないときに倒れる先があること。
     */
    {
        report += "  卓の高さ\n";
        const float rot[4] = { 0.f, 0.f, 0.f, 1.f };
        const struct {
            const char *what;
            float head_y;
            float expected;
        } cases[] = {
            { "立って被る（頭 1.65m）→ 天板 1.13m", 1.65f, 1.13f },
            { "座って被る（頭 1.20m）→ 天板 0.68m", 1.20f, 0.68f },
            { "床が取れない（頭 0m）→ 天板 0.72m", 0.f, 0.72f },
            { "背の高い人（頭 2.00m）→ 天板 1.15m で頭打ち", 2.f, 1.15f },
        };
        for (const auto &c : cases) {
            const float head[3] = { 0.f, c.head_y, 0.f };
            const RoomPlacement room = place_room(head, rot, 0.70f, 0.f);
            expect(std::fabs(room.table_center.y - c.expected) < 1e-3f, c.what, room.table_center.y);
        }
        //! cfg で書いたらその値がそのまま出ること（撮り比べる手段が死んでいないか）。
        const float head[3] = { 0.f, 1.65f, 0.f };
        expect(std::fabs(place_room(head, rot, 0.70f, 0.85f).table_center.y - 0.85f) < 1e-4f,
            "cfg に 0.85 と書いたら 0.85m", place_room(head, rot, 0.70f, 0.85f).table_center.y);
    }

    /*
     * 空中の板（§22）。**盤と同じ落とし穴（角の符号）がここにもある**ので、
     * 頭を回した姿勢で見る。見るのは 5 つ:
     * 1. 向いている方角の**正面**に出るか（左右がずれていないか）
     * 2. 目線より**上**に出るか（見上げる位置に置いたか）
     * 3. 板の面が頭のほうを向いているか／傾いていないか
     * 4. 幅が「画素数 × 見込み角」になっているか（読み味の勘定が生きているか）
     * 5. **遊びの角の中では動かない／外へ出たら付いてくる**（両方の指示を満たすか）
     */
    {
        report += "  空中の板\n";
        const float head[3] = { 0.4f, 1.60f, -0.3f };
        const float rot[4] = { 0.f, kSin45, 0.f, kSin45 }; //!< 左へ 90 度
        const Mat4 view = eye_view_from_pose(head, rot);
        const auto to_view = [&view](const Vec3 &p) {
            return Vec3{ (view.m[0] * p.x) + (view.m[4] * p.y) + (view.m[8] * p.z) + view.m[12],
                (view.m[1] * p.x) + (view.m[5] * p.y) + (view.m[9] * p.z) + view.m[13],
                (view.m[2] * p.x) + (view.m[6] * p.y) + (view.m[10] * p.z) + view.m[14] };
        };
        constexpr float kDegPerPx = 0.04f; //!< 1 桁（8 画素）0.32°
        constexpr float kLift = 22.f;
        //! 板の向きは頭の向きに合わせてある（再中心の直後の状態）。
        const Vec3 flat_head = flat_forward(rot);
        const float head_yaw = std::atan2(-flat_head.x, -flat_head.z);
        const PanelPose pose = place_hud_panel(head, head_yaw, 1920, 1080, kDegPerPx, kLift, 1.2f);
        const Vec3 v = to_view(pose.center);
        expect(std::fabs(v.x) < 1e-4f, "板は向いている方角の正面に出る", v.x);
        expect(std::fabs(v.y - (1.2f * std::sin(kLift * 3.14159265f / 180.f))) < 1e-4f,
            "板は目線より上（見上げる位置）", v.y);
        expect(v.z < -1.f, "板は前にある（後ろへ回っていない）", v.z);
        //! 面が頭を向いているか。板の法線は自分の +z。**裏を向いていたら負になる。**
        const Vec3 normal = quat_rotate(pose.orientation, Vec3{ 0.f, 0.f, 1.f });
        const Vec3 to_head = Vec3{ head[0], head[1], head[2] } - pose.center;
        expect(dot(normal, normalize(to_head)) > 0.90f, "板の面は頭のほうを向く",
            dot(normal, normalize(to_head)));
        //! 傾けない（横の軸が水平のまま）。首を傾げても字が水平に読めること。
        const Vec3 right_axis = quat_rotate(pose.orientation, Vec3{ 1.f, 0.f, 0.f });
        expect(std::fabs(right_axis.y) < 1e-5f, "板は傾いていない（横軸が水平）", right_axis.y);
        //! 幅は「画素数 × 見込み角」。1920 画素 × 0.04° = 76.8° → 1.2m 先で 1.90m。
        const float want = 2.f * 1.2f * std::tan(1920.f * kDegPerPx * 0.5f * 3.14159265f / 180.f);
        expect(std::fabs(pose.width_m - want) < 1e-3f, "幅は画素数と見込み角で決まる", pose.width_m);
        expect(std::fabs(pose.height_m - (pose.width_m * (1080.f / 1920.f))) < 1e-3f, "縦横比は画面のまま",
            pose.height_m / pose.width_m);

        /*
         * 追従。**遊びの角の中では動かない**（頭に貼り付くと疲れる）、
         * **外へ出たら付いてくる**（北に固定だと旋回して見えなくなる）。
         */
        float yaw = head_yaw;
        const float small_yaw = head_yaw + 0.3f; //!< 17 度ほど首を振る
        const float small[4] = { 0.f, std::sin(small_yaw * 0.5f), 0.f, std::cos(small_yaw * 0.5f) };
        for (int i = 0; i < 60; ++i) {
            follow_panel_yaw(yaw, small, 35.f, 1.f / 60.f);
        }
        expect(std::fabs(yaw - head_yaw) < 1e-5f, "遊びの中（17 度）では板は動かない", yaw - head_yaw);
        const float big_yaw = head_yaw + 1.6f; //!< 92 度ほど回した
        const float big[4] = { 0.f, std::sin(big_yaw * 0.5f), 0.f, std::cos(big_yaw * 0.5f) };
        for (int i = 0; i < 240; ++i) {
            follow_panel_yaw(yaw, big, 35.f, 1.f / 60.f);
        }
        const float dead = 35.f * 3.14159265f / 180.f;
        expect(std::fabs(std::fabs(big_yaw - yaw) - dead) < 2e-3f, "外へ出たら遊びの縁まで付いてくる",
            std::fabs(big_yaw - yaw) * 180.f / 3.14159265f);
    }

    /*
     * 一人称 VR（§17）。見るのは 4 つ。
     * 1. 縮尺が身長から出ているか（平らな画面の目の高さと同じ比になるか）
     * 2. プレイヤのマスが**利用者の足元**に来るか（目の高さが身長ぶん上にあるか）
     * 3. 目の世界座標が `kFpsEyeHeight` マスの高さになるか（＝平らな画面と同じ目線）
     * 4. **頭が照準か**——再中心の直後は北（0）、頭を右へ 90 度回したら東（+90 度）
     */
    {
        report += "  一人称 VR\n";
        constexpr float kEyeCells = 0.55f; //!< `kFpsEyeHeight`。ui/ を引かないので値で持つ
        expect(std::fabs(first_person_cell_m(1.65f, kEyeCells) - 3.f) < 1e-4f,
            "身長 1.65m なら 1 マス 3m", first_person_cell_m(1.65f, kEyeCells));
        expect(std::fabs(first_person_cell_m(0.f, kEyeCells) - 3.f) < 1e-4f,
            "床が取れないときは 3m へ落とす", first_person_cell_m(0.f, kEyeCells));

        const float head[3] = { 0.3f, 1.65f, -0.2f };
        const float rot[4] = { 0.f, kSin45, 0.f, kSin45 }; //!< 左へ 90 度
        const float cell_m = first_person_cell_m(head[1], kEyeCells);
        const BoardPlacement board = first_person_recenter(head, rot, cell_m);
        const Vec3 center{ 100.f, 50.f, 0.f }; //!< プレイヤのマス（床。z = 0）
        const Mat4 to_stage = world_to_stage(center, board);
        const Mat4 whole = eye_view_from_pose(head, rot) * to_stage;
        const auto apply = [&whole](const Vec3 &world) {
            return Vec3{
                (whole.m[0] * world.x) + (whole.m[4] * world.y) + (whole.m[8] * world.z) + whole.m[12],
                (whole.m[1] * world.x) + (whole.m[5] * world.y) + (whole.m[9] * world.z) + whole.m[13],
                (whole.m[2] * world.x) + (whole.m[6] * world.y) + (whole.m[10] * world.z) + whole.m[14],
            };
        };
        const Vec3 at_feet = apply(center);
        //! 足元は真下（横にも前後にもずれない）。
        expect(std::sqrt((at_feet.x * at_feet.x) + (at_feet.z * at_feet.z)) < 1e-4f,
            "プレイヤのマスは足の真下", std::sqrt((at_feet.x * at_feet.x) + (at_feet.z * at_feet.z)));
        expect(std::fabs(at_feet.y + 1.65f) < 1e-4f, "床は目の 1.65m 下", -at_feet.y);
        const Vec3 at_east = apply(center + Vec3{ 1.f, 0.f, 0.f });
        expect(std::fabs((at_east.x - at_feet.x) - cell_m) < 1e-4f, "東へ 1 マス = 3m",
            at_east.x - at_feet.x);
        //! 目の世界座標。**平らな画面と同じ目線の高さ**（`kFpsEyeHeight`）になるか。
        const Vec3 eye_world = stage_to_world(Vec3{ head[0], head[1], head[2] }, center, board);
        expect(std::fabs(eye_world.z - kEyeCells) < 1e-4f, "目の高さは 0.55 マス", eye_world.z);
        //! 頭が照準。再中心の直後は北（0）。
        expect(std::fabs(head_azimuth(board, rot)) < 1e-4f, "再中心の直後は北を向いている",
            head_azimuth(board, rot));
        /*
         * 頭を**右へ 90 度**回す（再中心のときより時計回り）。方位は東（+90 度）になるはず。
         * ここが逆符号だと「右を見て左へ歩く」になる。
         */
        const float turned[4] = { 0.f, 0.f, 0.f, 1.f }; //!< 左 90 度から素の姿勢へ＝右へ 90 度
        expect(std::fabs(head_azimuth(board, turned) - 1.5707963f) < 1e-4f,
            "頭を右へ 90 度 → 東（+90 度）", head_azimuth(board, turned) * 57.29578f);
    }

    report = std::string("[vr-math] 鏡像検査（東が右・南が手前）と盤の据わり\n") + report;
    return failures == 0;
}

BoardPlacement board_with_turn(const BoardPlacement &board, int turn)
{
    /*
     * **`board.yaw` は書き換えない。**あれは再中心したときの頭の向きで、据え直しの基準である。
     * ここは毎フレーム重ねる「追加の回転」だけを足した写しを返す。
     *
     * 足す符号は**フラットの `camera_turn` と一致する側**である
     * （`board_turn_check()` の 1 番目が突き合わせる）。手で決めていない。
     */
    BoardPlacement out = board;
    out.yaw += board_turn_slab_yaw(turn);
    return out;
}

float board_turn_slab_yaw(int turn)
{
    constexpr float kHalfPi = 1.57079633f;
    const int steps = ((turn % kBoardTurnCount) + kBoardTurnCount) % kBoardTurnCount;
    /*
     * 盤へ足す角と、板の固定ヨーは**同じ値**である（`xr_math.h` の導出）。
     * 1 つの関数から両方を引くことで、片方だけ直された状態になりようがない。
     */
    return static_cast<float>(steps) * kHalfPi;
}

bool board_turn_check(std::string &report)
{
    int failures = 0;
    report.clear();
    const auto expect = [&failures, &report](bool ok, const std::string &what) {
        report += std::string("    ") + (ok ? "OK   " : "FAIL ") + what + "\n";
        if (!ok) {
            ++failures;
        }
    };

    /*
     * 頭の姿勢は 2 通り見る。**素の姿勢だけだと `board.yaw` が 0 で、
     * 「回転を足す先が 0 だから合っている」だけの検査になる**（再中心の角と
     * 混ざったときに崩れないことが見たい）。
     */
    const float kSin45 = 0.70710678f;
    struct HeadPose {
        const char *name;
        float position[3];
        float orientation[4];
    };
    const HeadPose poses[] = {
        { "素の姿勢", { 0.f, 1.6f, 0.f }, { 0.f, 0.f, 0.f, 1.f } },
        { "左へ 90 度で再中心", { 0.4f, 1.6f, -0.3f }, { 0.f, kSin45, 0.f, kSin45 } },
    };

    for (const HeadPose &pose : poses) {
        report += std::string("  ") + pose.name + "\n";
        const RoomPlacement room = place_room(pose.position, pose.orientation, 0.70f, 0.f);
        const BoardPlacement base = recenter(room, 0.04f);
        const Vec3 center{ 100.f, 50.f, 0.f };

        //! `turn = 0` のときの板の法線（ステージ空間）。これが変わらないのが 2 番目の合格条件。
        Vec3 reference{};
        for (int turn = 0; turn < kBoardTurnCount; ++turn) {
            const BoardPlacement turned = board_with_turn(base, turn);

            /*
             * ① **フラットと向きが一致するか。**「ステージの右」を世界へ戻し、
             * 平らな画面の `rotate_screen_delta(turn, 1, 0)`（＝画面の右が指す世界の方向）と
             * 突き合わせる。写像の逆は「盤の向きを外して軸を戻す」だけ（`billboard_azimuth` と同じ道）。
             */
            {
                const float c = std::cos(-turned.yaw);
                const float s = std::sin(-turned.yaw);
                //! 頭を基準にした「右」。再中心の角ぶんは基準の盤で外しておく（回転ぶんだけを見る）。
                const Vec3 stage_right = quat_rotate(pose.orientation, Vec3{ 1.f, 0.f, 0.f });
                const Vec3 unrotated{ (stage_right.x * c) + (stage_right.z * s), stage_right.y,
                    (-stage_right.x * s) + (stage_right.z * c) };
                const Vec3 world_dir{ unrotated.x, unrotated.z, unrotated.y };
                int flat_dx = 1;
                int flat_dy = 0;
                /*
                 * **盤の 1 段は 90°、平らな画面の 1 段は 45°**（2026-09-06 に刻みを変えた）。
                 * 突き合わせるときは盤の段を 2 倍する。
                 */
                rotate_screen_delta(turn * 2, flat_dx, flat_dy);
                //! 符号だけ見る（長さは縮尺の話で、向きの検査には関係が無い）。
                const bool x_ok = (std::fabs(world_dir.x) < 0.2f)
                    ? (flat_dx == 0)
                    : ((world_dir.x > 0.f) == (flat_dx > 0)) && (flat_dx != 0);
                const bool y_ok = (std::fabs(world_dir.y) < 0.2f)
                    ? (flat_dy == 0)
                    : ((world_dir.y > 0.f) == (flat_dy > 0)) && (flat_dy != 0);
                expect(x_ok && y_ok,
                    "turn=" + std::to_string(turn) + " ステージの右 → 世界 (" + std::to_string(world_dir.x) + ", "
                        + std::to_string(world_dir.y) + ")  フラットは (" + std::to_string(flat_dx) + ", "
                        + std::to_string(flat_dy) + ")");
            }

            /*
             * ② **実体の板が正面を向き続けるか。**固定ヨー θ の板の法線は世界で
             * (−sinθ, cosθ, 0)。それを盤の写像に通した向きが `turn` に依らないこと。
             */
            {
                const float theta = board_turn_slab_yaw(turn);
                const Vec3 normal_world{ -std::sin(theta), std::cos(theta), 0.f };
                const Vec3 swapped{ normal_world.x, normal_world.z, normal_world.y };
                const float c = std::cos(turned.yaw);
                const float s = std::sin(turned.yaw);
                const Vec3 stage_normal{ (swapped.x * c) + (swapped.z * s), swapped.y,
                    (-swapped.x * s) + (swapped.z * c) };
                if (turn == 0) {
                    reference = stage_normal;
                    expect(true, "turn=0 板の法線（ステージ）を基準にする");
                } else {
                    const float dx = stage_normal.x - reference.x;
                    const float dy = stage_normal.y - reference.y;
                    const float dz = stage_normal.z - reference.z;
                    const float drift = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
                    expect(drift < 1e-4f,
                        "turn=" + std::to_string(turn) + " 板の法線が基準と同じ（ずれ " + std::to_string(drift) + "）");
                }
            }
        }

        //! ③ 4 段回すと元に戻る（丸めが効いていること）。
        const BoardPlacement wrapped = board_with_turn(base, kBoardTurnCount);
        expect(std::fabs(wrapped.yaw - base.yaw) < 1e-6f,
            "turn=4 は turn=0 と同じ据え方（差 " + std::to_string(wrapped.yaw - base.yaw) + "）");
    }

    report = std::string("[vr-math] 盤の 90 度回転（フラットの回転との一致・板の正対・巻き戻り）\n") + report;
    return failures == 0;
}

} // namespace hd2d::xr
