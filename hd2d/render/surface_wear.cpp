/*!
 * @file surface_wear.cpp
 * @brief 面の汚し（`surface_wear.h`）の中身。
 */
#include "render/surface_wear.h"

#include "render/gl_core.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>

using namespace hd2d::gl;

namespace hd2d {

namespace {

//! 作った表の 1 件。**`material_table.inc` の並びに合わせてある。**
struct ColorMaterial {
    std::uint32_t rgb;
    std::uint8_t mat;
};

/*!
 * @brief 色 → 材質の表。**手で書かない**（`tools/voxel/gen_material_table.py` が焼く）。
 * @details 鍵の昇順に並んでいるので二分探索で引く。
 */
constexpr ColorMaterial kColorMaterials[] = {
#include "render/material_table.inc"
};

//! 作った表が鍵の昇順であること。崩れると二分探索が静かに外す。
static_assert(std::size(kColorMaterials) > 0, "material_table.inc が空です");

/*!
 * @brief 表に無い色を**色みから**当てる。
 * @details 意匠だけの色（`reg_local`）は名前が町ごとの綴りなので、焼く側が
 * 材質を決められないものが残る。ここは**外しても害の小さい向き**へ倒す——
 * 分からないものは `Default`（弱い汚しだけ）にする。
 */
MaterialClass guess_material(int r, int g, int b)
{
    const int hi = std::max(r, std::max(g, b));
    const int lo = std::min(r, std::min(g, b));
    const int chroma = hi - lo;
    //! 彩度がほとんど無いもの＝灰。明るければ漆喰、そうでなければ石。
    if (chroma <= 14) {
        return (hi >= 200) ? MaterialClass::Plaster : MaterialClass::Stone;
    }
    //! 緑がいちばん強く、青より赤が弱いもの＝草木。
    if ((g > r) && (g > b) && (chroma >= 20)) {
        return MaterialClass::Plant;
    }
    //! 赤 > 緑 > 青 の茶（木・土）。明るさで分ける。
    if ((r > g) && (g >= b) && (chroma >= 20)) {
        return (hi >= 150) ? MaterialClass::Soil : MaterialClass::Wood;
    }
    return MaterialClass::Default;
}

} // namespace

MaterialClass material_for_color(std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const std::uint32_t key = (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8)
        | static_cast<std::uint32_t>(b);
    const auto *first = std::begin(kColorMaterials);
    const auto *last = std::end(kColorMaterials);
    const auto *found = std::lower_bound(first, last, key,
        [](const ColorMaterial &entry, std::uint32_t k) { return entry.rgb < k; });
    if ((found != last) && (found->rgb == key) && (found->mat < static_cast<std::uint8_t>(MaterialClass::Count))) {
        return static_cast<MaterialClass>(found->mat);
    }
    return guess_material(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
}

const char *material_class_name(MaterialClass mat)
{
    switch (mat) {
    case MaterialClass::Stone:
        return "stone";
    case MaterialClass::Soil:
        return "soil";
    case MaterialClass::Wood:
        return "wood";
    case MaterialClass::Metal:
        return "metal";
    case MaterialClass::Plaster:
        return "plaster";
    case MaterialClass::Fabric:
        return "fabric";
    case MaterialClass::Plant:
        return "plant";
    case MaterialClass::Clean:
        return "clean";
    case MaterialClass::Leaf:
        return "leaf";
    case MaterialClass::Default:
    case MaterialClass::Count:
    default:
        return "default";
    }
}

/*
 * 材質ごとの重みと染みの色は**シェーダの中の表**である（ユニフォームで送らない）。
 * 9 材質 × 2 本をユニフォームにすると送り口が 18 本に増えるうえ、
 * 「実物を見て詰める値」なのに直す場所が C++ とシェーダに分かれる。
 */
const char *const kSurfaceWearGlsl = R"(
// ---- 面の汚し（surface_wear.h） ----
uniform float u_wear_amount;   //!< 0 = 掛けない（ここで早く抜ける）
uniform int u_wear_materials;  //!< 材質ごとの入切（ビット。`1 << MaterialClass`）
uniform vec4 u_wear_mix;       //!< x=溜まり y=摩耗 z=斑 w=垂れ
uniform vec3 u_wear_grime;     //!< 汚れ色（掛け算）
uniform float u_wear_lattice;  //!< 1 マスの分割数。**ドット絵を保つ丸め幅**
uniform float u_wear_seed;

float hd2d_wear_luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

/*
 * 格子 1 つにつき 1 つの値（0..1）。**補間しない**——滑らかにすると粒子になる。
 * 整数の丸めは float でやる（ES では大きな ivec の掛け算が溢れやすい）。
 */
float hd2d_wear_hash(vec3 cell)
{
    vec3 p = fract(cell * 0.1031 + u_wear_seed);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

/*
 * 材質ごとの重み（x=溜まり y=摩耗 z=斑 w=垂れ）。
 * **`MaterialClass` の番号と並びが同じ**であること（surface_wear.h）。
 */
vec4 hd2d_wear_profile(int mat)
{
    if (mat == 1) { return vec4(1.00, 1.00, 1.00, 1.00); }   // 石
    if (mat == 2) { return vec4(0.90, 0.20, 1.20, 0.10); }   // 土
    if (mat == 3) { return vec4(0.80, 0.90, 0.90, 0.60); }   // 木
    if (mat == 4) { return vec4(0.70, 1.20, 1.00, 0.80); }   // 金
    if (mat == 5) { return vec4(0.90, 0.70, 0.80, 1.30); }   // 漆喰
    if (mat == 6) { return vec4(0.80, 0.30, 0.90, 0.40); }   // 布
    if (mat == 7) { return vec4(0.30, 0.00, 0.60, 0.00); }   // 草木
    if (mat == 8) { return vec4(0.00, 0.00, 0.00, 0.00); }   // 水・炎・光
    if (mat == 9) { return vec4(0.25, 0.00, 0.45, 0.00); }   // 木の葉（形は leaf_detail.h）
    return vec4(0.50, 0.30, 0.50, 0.30);                     // 分からないもの
}

//! 斑の染み色（掛け算）。石は苔、金は錆、木は灰。
vec3 hd2d_wear_stain(int mat)
{
    if (mat == 1) { return vec3(0.74, 0.88, 0.62); }   // 石 = 苔
    if (mat == 2) { return vec3(0.92, 0.84, 0.70); }   // 土 = 乾き
    if (mat == 3) { return vec3(0.86, 0.84, 0.78); }   // 木 = 灰化
    if (mat == 4) { return vec3(1.10, 0.70, 0.40); }   // 金 = 錆
    if (mat == 5) { return vec3(0.84, 0.82, 0.74); }   // 漆喰 = 雨だれ
    if (mat == 6) { return vec3(0.88, 0.84, 0.74); }   // 布 = 煤け
    if (mat == 7) { return vec3(0.88, 0.92, 0.68); }   // 草木 = 枯れ
    if (mat == 9) { return vec3(0.92, 0.90, 0.62); }   // 木の葉 = 黄ばみ
    return vec3(1.0);
}

/*!
 * 面の色に汚しを掛ける。
 *
 * @param base   パレットから引いた色（`v_tint` を掛けた後）
 * @param ao     焼いた AO（面アトラスの G。1 = 開けている）
 * @param world  世界座標（マス単位）
 * @param uv     面アトラスのテクセル座標。**`fract` が 1 マスの面の中の位置**
 * @param n      法線
 * @param mat    材質（`MaterialClass`）
 */
vec3 hd2d_wear_apply(vec3 base, float ao, vec3 world, vec2 uv, vec3 n, int mat)
{
    if (u_wear_amount <= 0.0) {
        return base;
    }
    //! **掛けないと決めた材質はここで戻る**（2026-08-23 に決めた）。
    if ((u_wear_materials & (1 << mat)) == 0) {
        return base;
    }
    vec4 w = hd2d_wear_profile(mat) * u_wear_mix * u_wear_amount;
    if (dot(w, vec4(1.0)) <= 0.0) {
        return base;
    }
    vec3 stain = hd2d_wear_stain(mat);

    //! 格子へ丸めてから引く（**ここを消すとフィルム粒子になる**）。
    vec3 fine_cell = floor(world * u_wear_lattice);
    vec3 coarse_cell = floor(world * (u_wear_lattice * 0.125));
    float fine = hd2d_wear_hash(fine_cell);
    float coarse = hd2d_wear_hash(coarse_cell + 17.0);

    // --- 1. 溜まり汚れ。AO の谷を「影」ではなく「垢」にする ---
    // AO は 1.0（開けている）〜0.45（八方塞がり）。0..1 へ伸ばす。
    float cavity = clamp((1.0 - ao) / 0.55, 0.0, 1.0);
    cavity *= mix(0.65, 1.0, fine);
    base = mix(base, base * u_wear_grime, clamp(cavity * w.x, 0.0, 1.0));

    // --- 2. 角の摩耗。1 マスの面の縁だけ白茶けさせる ---
    vec2 cell = fract(uv);
    float rim_d = min(min(cell.x, 1.0 - cell.x), min(cell.y, 1.0 - cell.y));
    float rim = 1.0 - smoothstep(0.0, 0.22, rim_d);
    //! **全部のマスが欠けるわけではない。**欠けるマスをノイズで間引く。
    rim *= step(0.55, fine);
    //! 出っ張りだけ（隅は擦れない）。
    rim *= smoothstep(0.50, 0.95, ao);
    vec3 worn = mix(base, vec3(hd2d_wear_luma(base)), 0.45) * 1.22;
    base = mix(base, worn, clamp(rim * w.y, 0.0, 1.0));

    // --- 3. 斑。大きなむらに細かい粒を重ねる ---
    float mottle = (coarse * 0.68) + (fine * 0.32);
    //! 明るさのむら。**±で振る**（暗くするだけだと全体が沈む）。
    base *= mix(0.86, 1.10, mottle);
    //! 染みは上を向いた面ほど強い（苔も埃も水平面に溜まる）。
    float facing = mix(0.45, 1.0, clamp(n.z, 0.0, 1.0));
    //! `patch` は GLSL の予約語（テセレーション）なので使えない。
    float blotch = smoothstep(0.62, 0.95, coarse) * facing;
    base = mix(base, base * stain, clamp(blotch * w.z, 0.0, 1.0));

    // --- 4. 垂れ。縦面だけ、格子を縦に引き伸ばして筋にする ---
    float vertical = 1.0 - abs(n.z);
    if ((vertical > 0.05) && (w.w > 0.0)) {
        vec3 stretched = vec3(fine_cell.xy, floor(world.z * u_wear_lattice * 0.10));
        float run = hd2d_wear_hash(stretched + 71.0);
        float streak = smoothstep(0.70, 1.0, run) * vertical;
        base = mix(base, base * u_wear_grime, clamp(streak * w.w, 0.0, 1.0));
    }
    return base;
}
)";

void WearUniforms::locate(GLuint program)
{
    this->amount = glGetUniformLocation(program, "u_wear_amount");
    this->materials = glGetUniformLocation(program, "u_wear_materials");
    this->mix = glGetUniformLocation(program, "u_wear_mix");
    this->grime = glGetUniformLocation(program, "u_wear_grime");
    this->lattice = glGetUniformLocation(program, "u_wear_lattice");
    this->seed = glGetUniformLocation(program, "u_wear_seed");
}

void upload_wear(const WearUniforms &loc, const WearParams &wear)
{
    if (loc.amount >= 0) {
        glUniform1f(loc.amount, wear.amount);
    }
    if (loc.materials >= 0) {
        glUniform1i(loc.materials, static_cast<GLint>(wear.materials));
    }
    if (loc.mix >= 0) {
        glUniform4f(loc.mix, wear.cavity, wear.edge, wear.mottle, wear.streak);
    }
    if (loc.grime >= 0) {
        glUniform3f(loc.grime, wear.grime.x, wear.grime.y, wear.grime.z);
    }
    if (loc.lattice >= 0) {
        glUniform1f(loc.lattice, wear.lattice);
    }
    if (loc.seed >= 0) {
        glUniform1f(loc.seed, wear.seed);
    }
}

const char *const *material_class_names()
{
    //! **`MaterialClass` と同じ並び。**`material_class_name()` と綴りも揃えること。
    static const char *const kNames[] = { "default", "stone", "soil", "wood", "metal", "plaster",
        "fabric", "plant", "clean", "leaf" };
    return kNames;
}

bool parse_wear_materials(const std::string &spec, std::uint32_t &out)
{
    std::string lowered;
    lowered.reserve(spec.size());
    for (const char ch : spec) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lowered == "all") {
        out = kWearAllMaterials;
        return true;
    }
    if ((lowered == "none") || lowered.empty()) {
        out = 0u;
        return true;
    }
    const char *const *names = material_class_names();
    std::uint32_t bits = 0u;
    std::size_t at = 0;
    while (at <= lowered.size()) {
        const std::size_t comma = lowered.find(',', at);
        const std::string word = lowered.substr(at, (comma == std::string::npos) ? std::string::npos : (comma - at));
        if (!word.empty()) {
            bool found = false;
            for (int i = 0; i < kMaterialClassCount; ++i) {
                if (word == names[i]) {
                    bits |= (1u << static_cast<unsigned>(i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false; //!< 1 つでも読めない綴りがあれば**何も変えない**
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
    out = bits;
    return true;
}

std::string wear_materials_text(std::uint32_t bits)
{
    if ((bits & ((1u << kMaterialClassCount) - 1u)) == ((1u << kMaterialClassCount) - 1u)) {
        return "all";
    }
    const char *const *names = material_class_names();
    std::string out;
    for (int i = 0; i < kMaterialClassCount; ++i) {
        if ((bits & (1u << static_cast<unsigned>(i))) == 0u) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += names[i];
    }
    return out.empty() ? std::string("none") : out;
}

bool parse_wear_amount(const std::string &spec, float &out)
{
    std::string lowered;
    lowered.reserve(spec.size());
    for (const char ch : spec) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if ((lowered == "off") || (lowered == "none")) {
        out = 0.f;
        return true;
    }
    if (lowered == "on") {
        out = 1.f;
        return true;
    }
    try {
        const float value = std::stof(lowered);
        if ((value < 0.f) || (value > 2.f)) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace hd2d
