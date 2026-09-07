/*!
 * @file camera.cpp
 * @brief `camera.h` の実装。
 */
#include "render/camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hd2d {

namespace {

//! 世界の上（z 上向き）。
const Vec3 kWorldUp{ 0.f, 0.f, 1.f };

} // namespace

Vec3 Camera::eye() const
{
    if (this->first_person) {
        //! 一人称（`ui/fps_mode.h`）。注視点＝プレイヤのマスの真上、目の高さ。
        return Vec3{ this->target.x, this->target.y, this->target.z + this->eye_height };
    }
    /*
     * 注視点の**視線と逆の向き**へ水平に引き、見下ろし角ぶん持ち上げる。
     * `yaw == 0`（既定）では `sin 0 == 0` / `cos 0 == 1` が厳密なので、
     * 「注視点の南（+y）へ引く」という当初の式と**寸分違わない**値になる。
     */
    const float s = std::sin(this->yaw);
    const float c = std::cos(this->yaw);
    return Vec3{ this->target.x - (s * this->distance),
        this->target.y + (c * this->distance),
        this->target.z + (this->distance * std::tan(this->pitch)) };
}

Vec3 Camera::forward() const
{
    if (this->first_person) {
        // 注視点と目の位置が同じなので差では引けない。方位と伏角から直に組む。
        const float cp = std::cos(this->pitch);
        return Vec3{ std::sin(this->yaw) * cp, -std::cos(this->yaw) * cp, -std::sin(this->pitch) };
    }
    return normalize(this->target - this->eye());
}

float Camera::azimuth() const
{
    const Vec3 right = normalize(cross(kWorldUp, this->forward()));
    return std::atan2(right.y, right.x);
}

float Camera::tan_half_x() const
{
    return std::tan(this->fov_x * 0.5f);
}

float Camera::tan_half_y() const
{
    return this->tan_half_x() * (static_cast<float>(this->viewport_h) / static_cast<float>(std::max(1, this->viewport_w)));
}

void Camera::depth_range(float &z_near, float &z_far) const
{
    if (this->first_person) {
        /*
         * 一人称は**マスの中に立っている**。壁の面はわずか 0.5 マス先なので、
         * 見下ろしの式（距離の 5%）をそのまま使うと自分の目の前が切れる。
         *
         * 遠クリップ 160 は**雲のため**（`render/cloud_layer.h`。高さ 40 の層を
         * 仰角 15° 付近まで見せるには水平 150 マス先の板が要る）。96 だと
         * 正面を向いたときの空の帯に雲が 1 枚も入らなかった（実機の絵で確かめた）。
         * 深度は 0.05〜160 の 3200:1 で、24 ビットの深度には十分に収まる。
         */
        z_near = 0.05f;
        z_far = 160.f;
        return;
    }
    // カメラ高と距離から機械的に決める。手で当てると画角を変えたとき静かに壊れる。
    const float height = this->distance * std::tan(this->pitch);
    const float span = std::sqrt((this->distance * this->distance) + (height * height));
    z_near = std::max(0.05f, span * 0.05f);
    z_far = (span * 8.f) + 64.f;
}

Mat4 Camera::view() const
{
    if (this->first_person) {
        // 注視点をそのまま渡すと「真下を見る」行列になる（目と注視点が高さしか違わない）。
        const Vec3 origin = this->eye();
        return look_at(origin, origin + this->forward(), kWorldUp);
    }
    return look_at(this->eye(), this->target, kWorldUp);
}

Mat4 Camera::projection() const
{
    float z_near = 0.f;
    float z_far = 0.f;
    this->depth_range(z_near, z_far);
    return perspective_horizontal(this->fov_x,
        static_cast<float>(this->viewport_w) / static_cast<float>(std::max(1, this->viewport_h)), z_near, z_far);
}

Mat4 Camera::view_projection() const
{
    return this->projection() * this->view();
}

bool Camera::unproject_to_plane(float sx, float sy, float plane_z, Vec3 &out) const
{
    const float ndc_x = ((2.f * sx) / static_cast<float>(std::max(1, this->viewport_w))) - 1.f;
    const float ndc_y = 1.f - ((2.f * sy) / static_cast<float>(std::max(1, this->viewport_h)));

    const Vec3 f = this->forward();
    // **左手系なので `上 × 前`**（`math3d.h` の `look_at` と同じ理由。逆にすると鏡像になる）。
    const Vec3 right = normalize(cross(kWorldUp, f));
    const Vec3 up = cross(f, right);
    const Vec3 dir = normalize(f + (right * (ndc_x * this->tan_half_x())) + (up * (ndc_y * this->tan_half_y())));

    const Vec3 origin = this->eye();
    if (std::abs(dir.z) < 1e-6f) {
        return false; // 視線が水平＝面と交わらない
    }
    const float t = (plane_z - origin.z) / dir.z;
    if (t <= 0.f) {
        return false; // 後ろ側／地平線より上を指している
    }
    out = origin + (dir * t);
    return true;
}

bool Camera::project(const Vec3 &world, float &sx, float &sy) const
{
    const Mat4 vp = this->view_projection();
    const float x = (vp.m[0] * world.x) + (vp.m[4] * world.y) + (vp.m[8] * world.z) + vp.m[12];
    const float y = (vp.m[1] * world.x) + (vp.m[5] * world.y) + (vp.m[9] * world.z) + vp.m[13];
    const float w = (vp.m[3] * world.x) + (vp.m[7] * world.y) + (vp.m[11] * world.z) + vp.m[15];
    if (w <= 1e-6f) {
        return false; // カメラの後ろ
    }
    sx = ((x / w) * 0.5f + 0.5f) * static_cast<float>(this->viewport_w);
    sy = (0.5f - ((y / w) * 0.5f)) * static_cast<float>(this->viewport_h);
    return true;
}

ViewWindow derive_view_window(const Camera &camera, float max_height, float max_distance)
{
    const Vec3 origin = camera.eye();
    const Vec3 f = camera.forward();
    // **左手系なので `上 × 前`**（`math3d.h` の `look_at` と同じ理由。逆にすると鏡像になる）。
    const Vec3 right = normalize(cross(kWorldUp, f));
    const Vec3 up = cross(f, right);
    const float tx = camera.tan_half_x();
    const float ty = camera.tan_half_y();

    float min_x = camera.target.x;
    float max_x = camera.target.x;
    float min_y = camera.target.y;
    float max_y = camera.target.y;

    /*
     * 視錐台の四隅の光線を、地面（z=0）と「立ち上がるものの天面」（z=max_height）の
     * **両方**と交わらせて外接矩形を採る。
     *
     * - 地面だけで測ると、奥の高い壁の上端が先に画面へ入ってくるぶんが漏れる
     * - 見下ろし角が浅いと上側の光線が地平線を越えて交点が無限へ飛ぶので、
     *   そのときは `max_distance` で打ち切る（**打ち切ったことは黙らせない**＝
     *   呼び出し側が窓の上限で気づく）
     */
    for (const float plane_z : { 0.f, max_height }) {
        for (const int sx : { -1, 1 }) {
            for (const int sy : { -1, 1 }) {
                const Vec3 dir = normalize(f + (right * (static_cast<float>(sx) * tx)) + (up * (static_cast<float>(sy) * ty)));
                float t = max_distance;
                if (std::abs(dir.z) > 1e-6f) {
                    const float hit = (plane_z - origin.z) / dir.z;
                    if ((hit > 0.f) && (hit < max_distance)) {
                        t = hit;
                    }
                }
                const Vec3 point = origin + (dir * t);
                min_x = std::min(min_x, point.x);
                max_x = std::max(max_x, point.x);
                min_y = std::min(min_y, point.y);
                max_y = std::max(max_y, point.y);
            }
        }
    }

    ViewWindow window;
    window.west = camera.target.x - min_x;
    window.east = max_x - camera.target.x;
    window.north = camera.target.y - min_y;
    window.south = max_y - camera.target.y;

    /*
     * **ここが §14-2 の罠。**プロトコルの窓は `ox = px - view_w/2` で注視点を中心にした
     * 対称な矩形なので（`presentation_bridge.cpp` の `capture`）、非対称な可視帯を覆うには
     * **長い方の 2 倍**が要る。北（奥）だけ 8 マス要るのに 2×6 しか頼まなければ、
     * 奥の 2 行が「未探知」でも「壁」でもなく**そもそも届かない**まま欠ける。
     */
    const float half_w = std::max(window.west, window.east);
    const float half_h = std::max(window.north, window.south);
    const float margin = 1.5f; //!< 端のマスが半分だけ見えている場合と、丸めのぶん
    window.cols = (2 * static_cast<int>(std::ceil(half_w + margin))) + 1;
    window.rows = (2 * static_cast<int>(std::ceil(half_h + margin))) + 1;
    return window;
}

float view_turn_yaw(int turn)
{
    return static_cast<float>(((turn % kViewTurnCount) + kViewTurnCount) % kViewTurnCount) * kViewTurnStep;
}

void rotate_screen_delta(int turn, int &dx, int &dy)
{
    const int steps = ((turn % kViewTurnCount) + kViewTurnCount) % kViewTurnCount;
    if ((steps == 0) || ((dx == 0) && (dy == 0))) {
        return;
    }
    /*
     * **偶数の段は 90° の倍数**なので、従来どおり整数のまま回す（長い矢でも厳密）。
     * (dx,dy) → (-dy,dx) を 1 回で 90°。
     */
    if ((steps % 2) == 0) {
        for (int i = 0; i < steps / 2; ++i) {
            const int nx = -dy;
            const int ny = dx;
            dx = nx;
            dy = ny;
        }
        return;
    }
    /*
     * **奇数の段は 45°。** 8 近傍の単位方向は輪を送るだけで厳密に写る
     * （移動の入力はここしか通らない）。それ以外は丸める（`camera.h` の注記）。
     */
    static const int kRing[8][2] = { { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
        { -1, 0 }, { -1, -1 } };
    for (int i = 0; i < 8; ++i) {
        if ((dx == kRing[i][0]) && (dy == kRing[i][1])) {
            const int j = (i + steps) % 8;
            dx = kRing[j][0];
            dy = kRing[j][1];
            return;
        }
    }
    const float angle = static_cast<float>(steps) * kViewTurnStep;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float fx = static_cast<float>(dx);
    const float fy = static_cast<float>(dy);
    dx = static_cast<int>(std::lround((fx * c) - (fy * s)));
    dy = static_cast<int>(std::lround((fx * s) + (fy * c)));
}

bool camera_orientation_check(const Camera &camera, std::string &report)
{
    const float centre_x = static_cast<float>(camera.viewport_w) * 0.5f;
    const float centre_y = static_cast<float>(camera.viewport_h) * 0.5f;

    /*
     * **回転しても成り立つ形で見る**。
     * 「東が右・南が下」は `turn = 0` のときだけの言い方で、そのまま 4 方位へ持ち込むと
     * 正しい絵で落ちる検査になる。代わりに **画面基準の「右」と「下」を
     * `rotate_screen_delta()` で世界へ写し、それが本当に右・下へ出るか**を見る
     * ——鏡像を捕まえる力（この検査の存在理由）はそのまま残る。
     */
    const int turn = static_cast<int>(std::lround(camera.yaw / kViewTurnStep));
    int right_dx = 1;
    int right_dy = 0;
    rotate_screen_delta(turn, right_dx, right_dy);
    int down_dx = 0;
    int down_dy = 1;
    rotate_screen_delta(turn, down_dx, down_dy);

    float right_x = 0.f;
    float right_y = 0.f;
    float down_x = 0.f;
    float down_y = 0.f;
    const bool right_ok = camera.project(
        Vec3{ camera.target.x + (static_cast<float>(right_dx) * 3.f), camera.target.y + (static_cast<float>(right_dy) * 3.f), 0.f },
        right_x, right_y);
    const bool down_ok = camera.project(
        Vec3{ camera.target.x + (static_cast<float>(down_dx) * 3.f), camera.target.y + (static_cast<float>(down_dy) * 3.f), 0.f },
        down_x, down_y);

    const bool right_is_right = right_ok && (right_x > centre_x);
    const bool down_is_down = down_ok && (down_y > centre_y);

    char buf[288]{};
    std::snprintf(buf, sizeof(buf),
        "turn=%d screen-right=world(%+d,%+d)->x=%.0f (centre %.0f) %s / screen-down=world(%+d,%+d)->y=%.0f (centre %.0f) %s",
        ((turn % kViewTurnCount) + kViewTurnCount) % kViewTurnCount, right_dx, right_dy, right_x, centre_x,
        right_is_right ? "OK" : "NG", down_dx, down_dy, down_y, centre_y, down_is_down ? "OK" : "NG");
    report = buf;
    return right_is_right && down_is_down;
}

bool camera_round_trip_check(const Camera &camera, const ViewWindow &window, std::string &report)
{
    int checked = 0;
    int failed = 0;
    int off_screen = 0;
    int worst_gx = 0;
    int worst_gy = 0;
    float worst_dx = 0.f;
    float worst_dy = 0.f;

    const int half_cols = window.cols / 2;
    const int half_rows = window.rows / 2;
    const int base_x = static_cast<int>(std::floor(camera.target.x)) - half_cols;
    const int base_y = static_cast<int>(std::floor(camera.target.y)) - half_rows;

    for (int row = 0; row < window.rows; ++row) {
        for (int col = 0; col < window.cols; ++col) {
            const int gx = base_x + col;
            const int gy = base_y + row;
            const Vec3 center{ static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, 0.f };

            float sx = 0.f;
            float sy = 0.f;
            if (!camera.project(center, sx, sy)) {
                ++off_screen;
                continue;
            }
            if ((sx < 0.f) || (sy < 0.f) || (sx >= static_cast<float>(camera.viewport_w))
                || (sy >= static_cast<float>(camera.viewport_h))) {
                ++off_screen;
                continue; // 画面の外は対象外（「写っているマスを指し直せるか」の検査）
            }
            Vec3 back{};
            ++checked;
            if (!camera.unproject_to_plane(sx, sy, 0.f, back)) {
                ++failed;
                continue;
            }
            const int back_gx = static_cast<int>(std::floor(back.x));
            const int back_gy = static_cast<int>(std::floor(back.y));
            if ((back_gx != gx) || (back_gy != gy)) {
                ++failed;
                const float dx = back.x - center.x;
                const float dy = back.y - center.y;
                if ((std::abs(dx) + std::abs(dy)) > (std::abs(worst_dx) + std::abs(worst_dy))) {
                    worst_dx = dx;
                    worst_dy = dy;
                    worst_gx = gx;
                    worst_gy = gy;
                }
            }
        }
    }

    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "checked=%d failed=%d off_screen=%d", checked, failed, off_screen);
    report = buf;
    if (failed > 0) {
        std::snprintf(buf, sizeof(buf), "  worst=(%d,%d) drift=(%.3f,%.3f)", worst_gx, worst_gy, worst_dx, worst_dy);
        report += buf;
    }
    return failed == 0;
}

} // namespace hd2d
