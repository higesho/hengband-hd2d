/*!
 * @file shadow_map.cpp
 * @brief `shadow_map.h` の実装。
 */
#include "render/shadow_map.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hd2d {

using namespace hd2d::gl;

namespace {

const Vec3 kWorldUp{ 0.f, 0.f, 1.f };

/*!
 * @brief カメラに写る地面の外接矩形を採る。
 * @details `derive_view_window()`（`camera.cpp`）と**同じ四隅の光線**を使う。
 * あちらは「コアへ何マス頼むか」を出すために余白を足して奇数のマス数へ丸めるが、
 * こちらは world の実数のままでよいので、共通化せずに同じ考え方を書き下している。
 * 揃えるべきは値ではなく**四隅と 2 枚の平面を見るという手口**である。
 */
void visible_ground_bounds(const Camera &camera, float max_height, float max_distance,
    float &min_x, float &max_x, float &min_y, float &max_y)
{
    const Vec3 origin = camera.eye();
    const Vec3 f = camera.forward();
    // **左手系なので `上 × 前`**（`math3d.h` の `look_at` と同じ理由）。
    const Vec3 right = normalize(cross(kWorldUp, f));
    const Vec3 up = cross(f, right);
    const float tx = camera.tan_half_x();
    const float ty = camera.tan_half_y();

    min_x = max_x = camera.target.x;
    min_y = max_y = camera.target.y;

    for (const float plane_z : { 0.f, max_height }) {
        for (const int sx : { -1, 1 }) {
            for (const int sy : { -1, 1 }) {
                const Vec3 dir = normalize(f + (right * (static_cast<float>(sx) * tx))
                    + (up * (static_cast<float>(sy) * ty)));
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
}

//! 地平線側の打ち切り（マス）。`derive_view_window` の呼び出し（`hd2d_app.cpp`）と同じ値。
constexpr float kMaxDistance = 60.f;

} // namespace

ShadowFit fit_shadow(const Camera &camera, const Vec3 &light_dir, float max_height, int side, float shrink)
{
    float min_x = 0.f;
    float max_x = 0.f;
    float min_y = 0.f;
    float max_y = 0.f;
    visible_ground_bounds(camera, max_height, kMaxDistance, min_x, max_x, min_y, max_y);

    // **検査用の細工。**狭めれば画面の端の影が消えるはずで、消えなければ検査が効いていない。
    return fit_shadow_bounds(min_x + shrink, max_x - shrink, min_y + shrink, max_y - shrink,
        max_height, light_dir, side);
}

ShadowFit fit_shadow_bounds(float min_x, float max_x, float min_y, float max_y,
    float max_height, const Vec3 &light_dir, int side)
{
    ShadowFit fit;
    fit.side = side;
    fit.min_x = min_x;
    fit.max_x = max_x;
    fit.min_y = min_y;
    fit.max_y = max_y;

    const Vec3 centre{ (fit.min_x + fit.max_x) * 0.5f, (fit.min_y + fit.max_y) * 0.5f, max_height * 0.5f };

    /*
     * 直方体は**正方形にする**。長方形にすると、カメラが回ったとき（P6 以降）に
     * 縦横の比が変わって影の解像度が跳ねる。いまカメラは回らないが、
     * 「回したら壊れる作り」を残さないほうが安い。
     *
     * 対角線ぶん（√2）まで見ておくのは、光の向きに対して斜めに置かれるため。
     */
    const float width = fit.max_x - fit.min_x;
    const float depth = fit.max_y - fit.min_y;
    const float diagonal = std::sqrt((width * width) + (depth * depth));
    fit.extent = std::max(diagonal, 1.f);
    const float half = fit.extent * 0.5f;

    /*
     * 光の視点。`light_dir` は**面から光源へ**なので、光源はその向きに離れた所にある。
     * 距離は直方体をすっぽり包めるだけ取る（近すぎると手前が near で切れる）。
     */
    const Vec3 dir = normalize(light_dir);
    const float back = fit.extent + max_height + 8.f;
    const Vec3 eye = centre + (dir * back);

    // 光が真上に近いと `上 × 前` が縮退する。そのときだけ基準を替える。
    const Vec3 up = (std::abs(dir.z) > 0.995f) ? Vec3{ 0.f, 1.f, 0.f } : kWorldUp;
    Mat4 view = look_at(eye, centre, up);

    /*
     * **テクセルへの吸着**（ヘッダの「なぜテクセルに丸めるのか」）。
     * 中心を光の空間へ写し、1 テクセルぶんの刻みへ丸めて、そのずれを視点行列へ戻す。
     */
    if (side > 0) {
        const float texels_per_cell = static_cast<float>(side) / fit.extent;
        const float cx = (view.m[0] * centre.x) + (view.m[4] * centre.y) + (view.m[8] * centre.z) + view.m[12];
        const float cy = (view.m[1] * centre.x) + (view.m[5] * centre.y) + (view.m[9] * centre.z) + view.m[13];
        const float snapped_x = std::floor(cx * texels_per_cell) / texels_per_cell;
        const float snapped_y = std::floor(cy * texels_per_cell) / texels_per_cell;
        view.m[12] += (snapped_x - cx);
        view.m[13] += (snapped_y - cy);
    }

    const float z_near = 0.5f;
    const float z_far = back + fit.extent + max_height + 8.f;
    fit.view_projection = ortho(-half, half, -half, half, z_near, z_far) * view;
    return fit;
}

bool shadow_fit_covers_view(const ShadowFit &fit, const Camera &camera, float max_height, std::string &report)
{
    float min_x = 0.f;
    float max_x = 0.f;
    float min_y = 0.f;
    float max_y = 0.f;
    visible_ground_bounds(camera, max_height, kMaxDistance, min_x, max_x, min_y, max_y);

    /*
     * 可視範囲の**格子状の見本点**を光のクリップ空間へ写す。四隅だけだと、
     * 直方体が回っているときに辺の途中がはみ出す場合を見逃す。
     */
    int checked = 0;
    int outside = 0;
    float worst = 0.f;
    float worst_x = 0.f;
    float worst_y = 0.f;
    constexpr int kSteps = 8;
    for (int j = 0; j <= kSteps; ++j) {
        for (int i = 0; i <= kSteps; ++i) {
            const float wx = min_x + ((max_x - min_x) * static_cast<float>(i) / static_cast<float>(kSteps));
            const float wy = min_y + ((max_y - min_y) * static_cast<float>(j) / static_cast<float>(kSteps));
            // 地面と天面の両方（背の高いものの上端も影を落とす／受ける）。
            for (const float wz : { 0.f, max_height }) {
                const Mat4 &m = fit.view_projection;
                const float cx = (m.m[0] * wx) + (m.m[4] * wy) + (m.m[8] * wz) + m.m[12];
                const float cy = (m.m[1] * wx) + (m.m[5] * wy) + (m.m[9] * wz) + m.m[13];
                const float cz = (m.m[2] * wx) + (m.m[6] * wy) + (m.m[10] * wz) + m.m[14];
                const float cw = (m.m[3] * wx) + (m.m[7] * wy) + (m.m[11] * wz) + m.m[15];
                ++checked;
                if (cw <= 0.f) {
                    ++outside;
                    continue;
                }
                const float nx = cx / cw;
                const float ny = cy / cw;
                const float nz = cz / cw;
                const float over = std::max({ std::abs(nx) - 1.f, std::abs(ny) - 1.f, std::abs(nz) - 1.f });
                if (over > 0.f) {
                    ++outside;
                    if (over > worst) {
                        worst = over;
                        worst_x = wx;
                        worst_y = wy;
                    }
                }
            }
        }
    }

    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "checked=%d outside=%d extent=%.1f cells (%.1f texels/cell)",
        checked, outside, fit.extent,
        ((fit.extent > 0.f) && (fit.side > 0)) ? (static_cast<float>(fit.side) / fit.extent) : 0.f);
    report = buf;
    if (outside > 0) {
        std::snprintf(buf, sizeof(buf), "  worst=(%.1f,%.1f) はみ出し %.3f", worst_x, worst_y, worst);
        report += buf;
    }
    return outside == 0;
}

bool ShadowMap::init(int side, std::string &err)
{
    this->side_ = std::max(256, side);

    glGenTextures(1, &this->texture_);
    glBindTexture(GL_TEXTURE_2D, this->texture_);
    /*
     * type は ES では効く。DEPTH_COMPONENT24 に合法なのは GL_UNSIGNED_INT だけで、
     * GL_FLOAT は GL_INVALID_OPERATION（デスクトップは黙って受ける。データは nullptr
     * なので実害は無いが、ES の検証が弾き **FBO が不完全**になる——エミュレータで実測）。
     */
#if defined(HENGBAND_GLES)
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), this->side_, this->side_, 0,
        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), this->side_, this->side_, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
#endif
    // **LINEAR にする。**比較モードと組で「4 点の合否を混ぜる」になる（生の深度の補間ではない）。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    /*
     * 外は `CLAMP_TO_EDGE`。ふつうは白の縁色を付けるが、**シェーダ側で
     * [0,1] の外を明示的に「日向」と返している**ので、縁色に頼らない
     * （頼ると、当てはめが可視範囲を覆えていないときに縁が静かに嘘をつく）。
     */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &this->fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, this->fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, this->texture_, 0);
    // 色は書かない。**明示しないと「色の添付が無い」で不完全になる。**
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "シャドウマップの FBO が不完全です (0x%04X)", status);
        err = buf;
        this->shutdown();
        return false;
    }
    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "シャドウマップの用意で GL エラー: " + gl_err;
        this->shutdown();
        return false;
    }
    return true;
}

void ShadowMap::shutdown()
{
    if (this->fbo_ != 0) {
        glDeleteFramebuffers(1, &this->fbo_);
        this->fbo_ = 0;
    }
    if (this->texture_ != 0) {
        glDeleteTextures(1, &this->texture_);
        this->texture_ = 0;
    }
    this->side_ = 0;
}

void ShadowMap::begin()
{
    glBindFramebuffer(GL_FRAMEBUFFER, this->fbo_);
    glViewport(0, 0, this->side_, this->side_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
}

void ShadowMap::end(int viewport_w, int viewport_h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, viewport_w, viewport_h);
    // 影のパスで表裏を裏返している描き手が居るので、既定へ戻しておく。
    glCullFace(GL_BACK);
}

} // namespace hd2d
