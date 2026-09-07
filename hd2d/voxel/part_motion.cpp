/*!
 * @file part_motion.cpp
 * @brief `part_motion.h` の実装。
 */
#include "voxel/part_motion.h"

#include <cmath>

namespace hd2d {

namespace {

constexpr float kTwoPi = 6.28318530717959f;

//! 軸まわりの回転（0=x 1=y 2=z）。列優先（`math3d.h` の約束）。
Mat4 rotation(int axis, float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = Mat4::identity();
    switch (axis) {
    case 0: // x
        out.m[5] = c;
        out.m[9] = -s;
        out.m[6] = s;
        out.m[10] = c;
        break;
    case 1: // y
        out.m[0] = c;
        out.m[8] = s;
        out.m[2] = -s;
        out.m[10] = c;
        break;
    default: // z
        out.m[0] = c;
        out.m[4] = -s;
        out.m[1] = s;
        out.m[5] = c;
        break;
    }
    return out;
}

} // namespace

float MotionContext::state(const std::string &name) const
{
    for (const auto &entry : this->states) {
        if (entry.first == name) {
            return entry.second;
        }
    }
    return 0.f;
}

Mat4 part_motion_matrix(const PrefabPart &part, int voxels_per_cell, const MotionContext &ctx)
{
    const MotionKind kind = part.motion.kind;
    if ((kind == MotionKind::Static) || (kind == MotionKind::Wind)) {
        // 風は頂点シェーダの仕事（§8.1）。剛体としては動かさない。
        return Mat4::identity();
    }

    if (kind == MotionKind::Blink) {
        /*
         * 明滅。周期の中で `phase` から `duty` のあいだだけ見える。見えないときは縮尺 0
         * （三角形が潰れて描かれない。影にも出ない）。時刻は周期で割った端数で見るので、
         * ゲームを長く続けても位相はずれない。
         */
        const float period = (part.motion.period > 0.001f) ? part.motion.period : 1.f;
        float t = (ctx.time / period) + part.motion.phase;
        t -= std::floor(t);
        const float duty = (part.motion.duty < 0.f) ? 0.f : ((part.motion.duty > 1.f) ? 1.f : part.motion.duty);
        if (t < duty) {
            return Mat4::identity();
        }
        Mat4 hidden;
        hidden.m[15] = 1.f;
        return hidden;
    }
    float radians = 0.f;
    switch (kind) {
    case MotionKind::Rotate:
        // 速さは**回転/秒**。位相は 0〜1 で 1 周ぶん（インスタンスごとにずらせるように）。
        radians = kTwoPi * ((ctx.time * part.motion.speed) + part.motion.phase);
        break;
    case MotionKind::Pendulum: {
        /*
         * 振り子。振幅は度で受ける（`.jsonc` を人が書くので）。
         * 減衰は「揺れが収まっていく速さ」ではなく**振幅の目減り**として使う。
         * 常に揺れ続けてほしい看板や幟に「時間とともに止まる」を入れると、
         * ゲームを長く続けたときだけ静止して見える（原因が分かりにくい壊れ方）。
         * したがって `damping` は 0〜1 の一定の減衰率として掛けるだけにする。
         */
        const float period = (part.motion.period > 0.001f) ? part.motion.period : 1.f;
        const float angle_deg = part.motion.amplitude * (1.f - part.motion.damping);
        radians = (angle_deg * 0.0174533f)
            * std::sin(kTwoPi * (((ctx.time / period)) + part.motion.phase));
        break;
    }
    case MotionKind::StateLinked: {
        /*
         * 状態連動（扉の開閉）。0 = 閉 〜 1 = 開 を `amplitude` 度まで開く。
         * **なめらかに補間するのは呼び出し側の仕事ではない。**状態そのものが 0/1 で
         * 飛んでくると扉が瞬間移動するので、状態を持つ側が滑らかにして渡す約束にしてある
         * （`MotionContext::states` は float）。
         */
        const float open = ctx.state(part.motion.state_name);
        const float clamped = (open < 0.f) ? 0.f : ((open > 1.f) ? 1.f : open);
        radians = part.motion.amplitude * 0.0174533f * clamped;
        break;
    }
    default:
        return Mat4::identity();
    }

    if (!part.has_pivot) {
        // ピボットが無ければパーツの原点まわり。**黙って動かさないより、原点で回すほうがよい**
        // （見ればすぐ気づく＝宣言し忘れが分かる）。
        return rotation(part.motion.axis, radians);
    }

    // ピボットはボクセル単位（プレハブ原点から）。世界はマス単位なので直す。
    const float scale = 1.f / static_cast<float>((voxels_per_cell > 0) ? voxels_per_cell : 32);
    const Vec3 pivot{ part.pivot[0] * scale, part.pivot[1] * scale, part.pivot[2] * scale };
    return translation(pivot) * rotation(part.motion.axis, radians) * translation(pivot * -1.f);
}

void compose_part_motions(const Prefab &prefab, const MotionContext &ctx, std::vector<Mat4> &out)
{
    const std::size_t count = prefab.parts.size();
    out.assign(count, Mat4::identity());
    std::vector<bool> done(count, false);

    /*
     * 親を先に解決する。`parts` の並びが親→子とは限らないので、
     * 「まだ解けていないものが減らなくなるまで」回す。循環は `load_prefab` の検査で
     * 弾かれている（§7.2-3）ので、必ず全部解ける。
     */
    for (std::size_t pass = 0; pass <= count; ++pass) {
        bool progressed = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (done[i]) {
                continue;
            }
            const PrefabPart &part = prefab.parts[i];
            const int parent = part.parent;
            const bool has_parent = (parent >= 0) && (static_cast<std::size_t>(parent) < count);
            if (has_parent && !done[static_cast<std::size_t>(parent)]) {
                continue; // 親がまだ
            }
            const Mat4 self = part_motion_matrix(part, prefab.voxels_per_cell, ctx);
            out[i] = has_parent ? (out[static_cast<std::size_t>(parent)] * self) : self;
            done[i] = true;
            progressed = true;
        }
        if (!progressed) {
            break;
        }
    }
}

} // namespace hd2d
