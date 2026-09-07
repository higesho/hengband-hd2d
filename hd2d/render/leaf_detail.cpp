/*!
 * @file leaf_detail.cpp
 * @brief 木の葉（`leaf_detail.h`）の中身。
 */
#include "render/leaf_detail.h"

#include "render/gl_core.h"
#include "render/surface_wear.h"

#include <string>

using namespace hd2d::gl;

namespace hd2d {

/*
 * 葉の並びの種は**面アトラスの座標から**採る（世界座標ではない）。
 *
 * 葉は風で曲がる（`u_part_wind_k`。設計書 §8.2）ので、世界座標を種にすると
 * **面の上を模様が泳ぐ**——葉が揺れているのではなく、葉の絵が滑って見える。
 * アトラスの座標は模型に貼り付いているので、曲げても模様は葉と一緒に動く。
 *
 * 引き換えに、同じプレハブを何本置いても葉の並びは同じになる。1 枚が 1/64 マスなので
 * 見下ろしでは判別できず、木は 10 変種あるので繰り返しには見えない。
 */
const char *const kLeafDetailGlsl = R"(
// ---- 木の葉（leaf_detail.h） ----
uniform float u_leaf_amount;   //!< 0 = 描かない（ここで早く抜ける）
uniform float u_leaf_cells;    //!< 1 マスの面の一辺に葉を何枚並べるか
uniform float u_leaf_gap;      //!< 葉と葉の間の暗さ
uniform float u_leaf_vein;     //!< 中肋の明るさ
uniform float u_leaf_scatter;  //!< ボクセル 1 個ごとのばらつき

float hd2d_leaf_hash(vec2 cell)
{
    vec3 p = fract(vec3(cell.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

/*!
 * 葉の面に葉を描く。**材質が `Leaf`（9）の面だけ。**
 *
 * @param base 汚しまで済んだ色
 * @param uv   面アトラスのテクセル座標（`fract` が 1 マスの面の中の位置）
 * @param mat  材質（`MaterialClass`）
 */
vec3 hd2d_leaf_apply(vec3 base, vec2 uv, int mat)
{
    if ((u_leaf_amount <= 0.0) || (mat != 9)) {
        return base;
    }
    vec2 face = floor(uv);

    // --- 1. 粒。**遠くで効くのはここだけ**（1 ボクセルが 2 画素しかない） ---
    float grain = hd2d_leaf_hash(face);
    float tone = hd2d_leaf_hash(face + 31.7);
    float scatter = u_leaf_scatter * u_leaf_amount;
    base *= mix(1.0 - scatter, 1.0 + scatter, grain);
    //! 黄緑と濃緑へ振る。**明暗だけだと日陰にしか見えない**（葉は色そのものが揃わない）。
    base *= mix(vec3(1.0) - (vec3(0.06, 0.00, 0.10) * scatter * 4.0),
        vec3(1.0) + (vec3(0.06, 0.02, -0.08) * scatter * 4.0), tone);

    /*
     * **1 画素がボクセルより粗くなったら形を消す。**`fwidth` は「隣の画素との差」
     * ＝ 1 画素が何ボクセルぶんかで、見下ろしの町では 0.5 ほどになる。
     * 消さないと 1 画素を割った模様がカメラの動きで沸き立つ（ドット絵の敵）。
     */
    float px = max(fwidth(uv.x), fwidth(uv.y));
    float detail = 1.0 - smoothstep(0.34, 0.72, px * u_leaf_cells);
    if (detail <= 0.0) {
        return base;
    }

    /*
     * --- 2. 葉。**1 マスの面に 1 枚**（`cells` = 1）が既定。
     *
     * 初版は 2×2 で並べたが、**網戸に見えた**——1 枚が小さすぎて形が読めず、
     * マスの境の暗い線だけが規則正しく残るためである。1 枚にして中心と大きさを振ると、
     * 面の境が葉の縁と重なって格子が崩れる。
     */
    vec2 f = fract(uv) * u_leaf_cells;
    vec2 id = floor(f);
    vec2 seed = (face * 7.13) + id;
    //! 中心をずらす。**マスの真ん中に揃えない**（揃えると格子が戻る）。
    vec2 jitter = (vec2(hd2d_leaf_hash(seed + 3.1), hd2d_leaf_hash(seed + 9.7)) - 0.5) * 0.30;
    vec2 q = (fract(f) - 0.5) - jitter;
    //! 葉ごとに向きを振る。
    float a = hd2d_leaf_hash(seed) * 6.2831853;
    float cs = cos(a);
    float sn = sin(a);
    vec2 r = vec2((q.x * cs) - (q.y * sn), (q.x * sn) + (q.y * cs));
    //! 大きさも振る（同じ葉が並ぶと畳に見える）。
    float size = mix(0.52, 0.74, hd2d_leaf_hash(seed + 17.3));
    //! **根元を太く先を細く。**真円だと玉に見える。
    float taper = size * (0.72 - (0.34 * clamp(r.x / size + 0.5, 0.0, 1.0)));
    float body = length(vec2(r.x / size, r.y / max(taper, 0.05)));
    float leaf = 1.0 - smoothstep(0.72, 1.06, body);
    //! 中肋。葉の中だけ。
    float vein = (1.0 - smoothstep(0.0, 0.06 * size, abs(r.y))) * leaf;

    //! 葉の外は隙間（奥の暗がり）。葉ごとに明暗を振る。
    float lit = mix(1.0 - (u_leaf_gap * u_leaf_amount), 1.0, leaf);
    lit *= mix(1.0, 1.0 + (u_leaf_vein * u_leaf_amount), vein);
    lit *= mix(1.0 - (0.12 * u_leaf_amount), 1.0 + (0.12 * u_leaf_amount), hd2d_leaf_hash(seed + 11.0));
    base *= mix(1.0, lit, detail);
    return base;
}
)";

void LeafUniforms::locate(GLuint program)
{
    this->amount = glGetUniformLocation(program, "u_leaf_amount");
    this->cells = glGetUniformLocation(program, "u_leaf_cells");
    this->gap = glGetUniformLocation(program, "u_leaf_gap");
    this->vein = glGetUniformLocation(program, "u_leaf_vein");
    this->scatter = glGetUniformLocation(program, "u_leaf_scatter");
}

void upload_leaf(const LeafUniforms &loc, const LeafParams &leaf)
{
    if (loc.amount >= 0) {
        glUniform1f(loc.amount, leaf.amount);
    }
    if (loc.cells >= 0) {
        glUniform1f(loc.cells, leaf.cells);
    }
    if (loc.gap >= 0) {
        glUniform1f(loc.gap, leaf.gap);
    }
    if (loc.vein >= 0) {
        glUniform1f(loc.vein, leaf.vein);
    }
    if (loc.scatter >= 0) {
        glUniform1f(loc.scatter, leaf.scatter);
    }
}

bool parse_leaf_amount(const std::string &spec, float &out)
{
    return parse_wear_amount(spec, out);
}

} // namespace hd2d
