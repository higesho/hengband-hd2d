/*!
 * @file lighting.cpp
 * @brief `lighting.h` の実装。
 */
#include "render/lighting.h"

#include "frame/cell_feature_bits.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace hd2d {

using namespace hd2d::gl;

namespace {

constexpr float kPi = 3.14159265358979f;

/*!
 * @name 地下の環境光の 2 段（通路・暗い部屋 ⇔ 明るい部屋）
 *
 * @details **2 つで 1 組なので隣に置く。**片方だけ動かすと段差の意味が変わる。
 *
 * 明るい部屋の底上げ（2026-08-20 の指示）を入れたとき、下の段は 0.55 のまま据え置いた
 * ——絶対値としては前と同じである。ところが**上が 1.05 になったぶん段差が 0.50 でき**、
 * 部屋から通路へ出た瞬間に落ちるのが「通路が暗くなった」と読めた
 * （2026-08-21 に気づいた）。**下を上げて段差を 0.25 へ半分に縮める**のが決めたことである
 * （2026-08-21。「明るい部屋は明るいまま、通路は暗くなったと感じない所まで」）。
 *
 * 上の段は 1.05 から動かさない。**1.5 まで上げると部屋の中で松明が見えなくなる**
 * ——丸い減衰が環境光に埋まるためで、これは絵合わせで決めた上限である。
 * @{
 */
//! 通路・暗い部屋（＝地下の既定）。
constexpr float kUndergroundAmbient = 0.80f;
//! 明るい部屋（`CAVE_GLOW` のマスに立っている間）。
constexpr float kLitRoomAmbient = 1.05f;
/*! @} */

/*!
 * @brief 影が無いときに `u_shadow_map` へ結ぶ 1x1 の深度テクスチャ。
 *
 * ## なぜ要るか（2026-08-21。実機 Adreno 740 で踏んだ）
 *
 * `u_shadow_map` は **`sampler2DShadow`** である。GLES の規約では、影サンプラに
 * 結ばれたテクスチャが「深度でない」「比較モードが立っていない」「そもそも無い」の
 * どれかなら、**その描画は `GL_INVALID_OPERATION`** になる。
 *
 * 影を描かない場面（地下・夜）では `shadow_texture` に 0 が来る。0 を結ぶと
 * 既定のテクスチャ（深度でも比較モードでもない）が選ばれ、**上の条件に当たる。**
 * Adreno は規約どおり弾き、
 *
 *     [gl] type=0x824c a shader program has invalid image information or invalid sampler information
 *
 * を出して**ボクセルの描画をすべて捨てる**——地面も壁もプレハブも出ない。
 * エミュレータ（SwiftShader）は見逃すので、あちらでは最後まで再現しなかった。
 *
 * 強さ（`shadow_strength`）は下で 0 にしているので、**結ぶだけで絵は 1 画素も変わらない。**
 *
 * @note GL の名前を静的な変数で持つ。**この註は 2026-08-25 に外れた**——
 * 「文脈が作り直されると古い名前が残るが、この機体構成では文脈の消失に耐える作りを
 * どこも持っていない」と書いていたが、**追補 A2（言語の切り替え）で `run()` を
 * 出入りする道ができ、文脈は作り直されるようになった**。残った名前を結び続けて
 * 1 回につき 1 万件を超える `GL_INVALID_OPERATION` を出していた（SH-36）。
 * いまは `release_shadow_placeholder()` を畳み口で呼ぶ。
 */
gl::GLuint g_shadow_placeholder = 0;

gl::GLuint shadow_placeholder_texture()
{
    gl::GLuint &texture = g_shadow_placeholder;
    if (texture != 0) {
        return texture;
    }
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    /*
     * いちばん遠い深度で埋める。万一 `shadow_strength` が 0 でないまま読まれても
     * 「日向」（＝影を作らない）に倒れる。型の約束は `shadow_map.cpp` と同じ
     * （ES では `DEPTH_COMPONENT24` に合法なのは `GL_UNSIGNED_INT` だけ）。
     */
    const std::uint32_t farthest = 0xFFFFFFFFu;
#if defined(HENGBAND_GLES)
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), 1, 1, 0,
        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &farthest);
#else
    const float far_float = 1.f;
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), 1, 1, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, &far_float);
    (void)farthest;
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    //! **これが本体。**比較モードが立っていないと `sampler2DShadow` から読めない。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

//! 線形補間（Vec3）。
Vec3 lerp(const Vec3 &a, const Vec3 &b, float t)
{
    return Vec3{ a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t) };
}

/*!
 * @brief 太陽（あるいは月）の高度から色を引く。
 * @param altitude -1（真下）〜 +1（真上）。
 *
 * @details **ここは物理ではなく「読めること」で決めている。**
 * 素直に太陽の高度だけで作ると、正午は白く飛び、日没 20 分後には**画面がほぼ真っ黒**になる
 * （実際に一度そうなった）。地形が読めない画面は絵として成立していないので、
 *
 * | | |
 * |---|---|
 * | 正午 | 主光を抑えて**影が読める**ようにする（環境光で潰さない） |
 * | 夜 | 月明かりを**寒色ではっきり残す**。暗いが地形は読める |
 *
 * とする。夜を「見えない」にしてよいのは地下だけで、**町には昼夜がある**（§12-4）。
 */
void sun_colors(float altitude, Vec3 &key, Vec3 &sky, Vec3 &bounce, float &ambient)
{
    const Vec3 kNoonKey{ 1.15f, 1.10f, 0.98f };
    const Vec3 kDuskKey{ 1.30f, 0.70f, 0.36f }; //!< 夕焼け
    const Vec3 kNightKey{ 0.34f, 0.40f, 0.62f }; //!< 月明かり（寒色）

    const Vec3 kNoonSky{ 0.58f, 0.68f, 0.90f };
    const Vec3 kDuskSky{ 0.50f, 0.42f, 0.52f };
    const Vec3 kNightSky{ 0.30f, 0.36f, 0.56f };

    const Vec3 kNoonBounce{ 0.82f, 0.74f, 0.60f };
    const Vec3 kDuskBounce{ 0.64f, 0.44f, 0.32f };
    const Vec3 kNightBounce{ 0.22f, 0.24f, 0.34f };

    if (altitude >= 0.f) {
        // 0（地平線）→ 1（真上）。夕焼けの帯を厚めに取る（0.35 まで）。
        const float t = std::clamp(altitude / 0.35f, 0.f, 1.f);
        key = lerp(kDuskKey, kNoonKey, t);
        sky = lerp(kDuskSky, kNoonSky, t);
        bounce = lerp(kDuskBounce, kNoonBounce, t);
        // 地平線すれすれの太陽は弱い。ただし 0 にはしない（日の出が真っ暗になる）。
        const float strength = std::sqrt(std::clamp(altitude, 0.f, 1.f));
        key = key * (0.45f + (0.55f * strength));
        /*
         * **環境光は高いほど下げる。**上げると影が環境光に埋まって、正午がいちばん
         * 平板な絵になる（影が最も短い時刻でもあるので二重に効く）。
         */
        ambient = 0.85f - (0.15f * t);
        return;
    }
    // 日没後。-0.15 くらいまでは薄明が残り、そこから先は月明かりに落ち着く。
    const float t = std::clamp(-altitude / 0.15f, 0.f, 1.f);
    key = lerp(kDuskKey * 0.45f, kNightKey, t);
    sky = lerp(kDuskSky, kNightSky, t);
    bounce = lerp(kDuskBounce, kNightBounce, t);
    ambient = 0.85f - (0.20f * t);
}

} // namespace

void release_shadow_placeholder()
{
    if (g_shadow_placeholder == 0) {
        return;
    }
    glDeleteTextures(1, &g_shadow_placeholder);
    g_shadow_placeholder = 0;
}

float lit_room_target(const GameFrame &frame)
{
    for (const auto &cell : frame.cells) {
        if ((cell.feature_flags & CELL_FEAT_PLAYER) != 0u) {
            return ((cell.feature_flags & CELL_FEAT_GLOWING) != 0u) ? 1.f : 0.f;
        }
    }
    return 0.f;
}

void apply_lit_room_ambient(SceneLighting &lighting, const FloorIdentity &floor, float mix)
{
    const bool underground = (floor.kind == static_cast<int>(FloorKind::Dungeon))
        || (floor.kind == static_cast<int>(FloorKind::Quest))
        || (floor.kind == static_cast<int>(FloorKind::Arena));
    if (!underground) {
        return;
    }

    const float t = std::clamp(mix, 0.f, 1.f);
    if (t <= 0.f) {
        return;
    }

    /*
     * 明るい部屋の底。**方向光はそのまま**なので、陰影の向きと影は変わらない。
     * 値と、通路との段差の付け方は `kUndergroundAmbient` / `kLitRoomAmbient` の節にある
     * （空 0.15,0.17,0.24 / 照り返し 0.20,0.17,0.15 が下の段の色）。
     */
    const Vec3 lit_sky{ 0.34f, 0.35f, 0.40f };
    const Vec3 lit_bounce{ 0.40f, 0.34f, 0.28f };

    lighting.ambient_scale += (kLitRoomAmbient - lighting.ambient_scale) * t;
    lighting.sky_color = lerp(lighting.sky_color, lit_sky, t);
    lighting.bounce_color = lerp(lighting.bounce_color, lit_bounce, t);
}

SceneLighting make_scene_lighting(const LightingState &state, const FloorIdentity &floor)
{
    SceneLighting out;

    const bool underground = (floor.kind == static_cast<int>(FloorKind::Dungeon))
        || (floor.kind == static_cast<int>(FloorKind::Quest))
        || (floor.kind == static_cast<int>(FloorKind::Arena));

    if (underground) {
        /*
         * 地下には太陽が無い。それでも**接地影は落としたい**（P5 の完了条件）ので、
         * 「上からやや南寄り」の弱い方向光を約束として置く（ヘッダの注記）。
         * 南寄りにするのは、真上からだと影が物の真下に隠れて見えないため。
         */
        out.sun_dir = normalize(Vec3{ 0.18f, 0.42f, 0.89f });
        out.key_color = Vec3{ 0.30f, 0.30f, 0.36f };
        out.sky_color = Vec3{ 0.15f, 0.17f, 0.24f };
        out.bounce_color = Vec3{ 0.20f, 0.17f, 0.15f };
        out.ambient_scale = kUndergroundAmbient;
        out.shadow_strength = 0.75f; //!< 主光が弱いので影も薄くする（真っ黒にしない）
        return out;
    }

    /*
     * 地上。太陽は**東から昇って西へ沈む**。
     * `day_minute` は 0 = 真夜中・720 = 正午（`extract_date_time()` の時刻をそのまま分にしたもの）。
     * 6 時に東（-x 側から照らす＝面から見て東向き）、12 時に真上、18 時に西。
     */
    const float phase = (static_cast<float>(state.day_minute) / 1440.f); // 0..1
    const float hour_angle = (phase - 0.25f) * 2.f * kPi; //!< 6 時を 0 にする
    const float altitude = std::sin(hour_angle); //!< -1..1（正午で 1）

    /*
     * 方位。**この世界は x = 東・y = 南である。**朝は東（+x）から照らされるので、
     * 面から光源へ向かうベクトルの水平成分は +x を向く。夕方は -x。
     * 南（+y）へ少し倒すのは、真東西から水平に照らすと影が画面を横切って伸びすぎるため。
     */
    const float horizontal = std::cos(hour_angle); //!< 6 時 +1（東）→ 18 時 -1（西）

    /*
     * 昼の光源。**高度をそのまま使わず 0.85 で頭打ちにする。**正午に光源が真上へ来ると
     * 影が物の真下に隠れて、いちばん明るい時刻がいちばん立体感の無い絵になる。
     */
    const Vec3 sun_side = normalize(Vec3{ horizontal, 0.35f, std::max(0.f, std::min(altitude, 0.85f)) });
    /*
     * 夜の光源。**月を太陽の反対側に、高い位置で置く。**「地平線の下にある太陽」を
     * そのまま光源にすると、地面が下から照らされて法線の向きが総崩れになる。
     * 光源が 1 灯しか無い作りなので、夜はこの 1 灯が月を演じる。
     */
    const Vec3 moon_side = normalize(Vec3{ -horizontal * 0.5f, 0.30f, 0.80f });
    /*
     * 薄明の帯（高度 0 〜 -0.15）で**混ぜる**。切り替えにすると、日没の瞬間に光源が
     * 西から東へ跳んで影が一斉に向きを変える。`sin` の符号は 18 時ちょうどで
     * 浮動小数の誤差ぶんだけ負にもなるので、境目に段差を作らないこと自体が必要でもある。
     */
    const float night_t = std::clamp(-altitude / 0.15f, 0.f, 1.f);
    out.sun_dir = normalize(lerp(sun_side, moon_side, night_t));

    sun_colors(altitude, out.key_color, out.sky_color, out.bounce_color, out.ambient_scale);

    /*
     * コアの「日中」（`is_daytime()`）は一日の**前半**かどうかで、`extract_date_time()` の
     * 時刻とは基準が 1/4 日ずれている（§12-4 の注記）。町の明るさやモンスターの湧きは
     * こちらで動いているので、**食い違ったときはコアの側を信じる**（少しだけ持ち上げる）。
     */
    if (state.daytime && (altitude < 0.f)) {
        out.ambient_scale = std::max(out.ambient_scale, 0.75f);
    }

    /*
     * 影の濃さ。夜も**月明かりの弱い影**を残す（0 にすると平板になる）。
     * 日の出・日没をまたぐところは連続に繋ぐ。
     */
    out.shadow_strength = (altitude >= 0.f)
        ? std::clamp(0.45f + (altitude * 2.f), 0.f, 1.f)
        : std::clamp(0.45f + (altitude * 2.f), 0.30f, 1.f);
    return out;
}

int collect_point_lights(const GameFrame &frame, const Vec3 &camera_target, SceneLighting &out, float glow_scale,
    const Vec3 *player_pos)
{
    out.points.clear();

    std::vector<PointLight> candidates;

    // (1) プレイヤの松明（§12-5）。**半径 0 なら灯りを持っていない**ので置かない。
    if (frame.lighting.light_radius > 0) {
        PointLight torch;
        /*
         * **なめらかな位置があればそちらへ置く**（2026-08-14 に気づいた
         * 「キャラ移動に対し光源の移動がマス単位なのでちらついて酔う」）。
         * 絵は補間で流れているので、光だけマスへ吸着していると明暗が段でずれる。
         */
        torch.x = (player_pos != nullptr) ? player_pos->x : (static_cast<float>(frame.player_gx) + 0.5f);
        torch.y = (player_pos != nullptr) ? player_pos->y : (static_cast<float>(frame.player_gy) + 0.5f);
        torch.z = 0.75f; //!< 腰より少し上。地面に置くと足元だけ白く飛ぶ
        // コアの半径は「そこまで見える」なので、光としてはもう少し先まで薄く届かせる。
        torch.radius = static_cast<float>(frame.lighting.light_radius) + 1.5f;
        torch.r = 1.00f;
        torch.g = 0.78f;
        torch.b = 0.48f;
        torch.intensity = 1.25f;
        candidates.push_back(torch);
    }

    // (2)(3) 溶岩と発光地形。**可視窓のマスからしか拾えない**（意味づけ層はミニマップ由来で
    // 地形の性質を持たないため）。画面に写らないものは光らせなくてよいので、これで足りる。
    for (const auto &cell : frame.cells) {
        if ((cell.feature_flags & CELL_FEAT_KNOWN) == 0u) {
            continue;
        }
        const bool lava = (cell.feature_flags & CELL_FEAT_LAVA) != 0u;
        const bool glow = (cell.feature_flags & CELL_FEAT_GLOW) != 0u;
        if (!lava && !glow) {
            continue;
        }
        /*
         * **地上の発光地形は夜だけ**（P10 第 2 期）。店の入口は地形に `GLOW` を持つので、
         * 掛けないと真昼の店先に白く飛んだ光が出る。呼ぶ側が `lamp_night_factor()` を渡す。
         * 溶岩には掛けない（洞窟に昼夜は無い）。
         */
        if (!lava && (glow_scale <= 0.01f)) {
            continue;
        }
        PointLight light;
        light.x = static_cast<float>(cell.gx) + 0.5f;
        light.y = static_cast<float>(cell.gy) + 0.5f;
        light.z = lava ? 0.10f : 0.60f; //!< 溶岩は面そのもの、発光地形は少し浮かせる
        light.radius = lava ? 2.5f : 3.0f;
        light.r = lava ? 1.20f : 0.90f;
        light.g = lava ? 0.44f : 0.82f;
        light.b = lava ? 0.16f : 0.62f;
        light.intensity = lava ? 1.10f : (0.70f * glow_scale);
        candidates.push_back(light);
    }

    if (static_cast<int>(candidates.size()) <= kMaxPointLights) {
        out.points = std::move(candidates);
        return 0;
    }

    /*
     * 溢れた。**近い順に採る**（遠い松明が消えても気づかれないが、足元が消えると分かる）。
     * プレイヤの松明は必ず先頭に居るので、安定ソートで先頭に残る……とは限らないので
     * 距離 0 扱いにして確実に残す。
     */
    const auto distance2 = [&](const PointLight &l) {
        const float dx = l.x - camera_target.x;
        const float dy = l.y - camera_target.y;
        return (dx * dx) + (dy * dy);
    };
    std::stable_sort(candidates.begin(), candidates.end(),
        [&](const PointLight &a, const PointLight &b) { return distance2(a) < distance2(b); });
    const int dropped = static_cast<int>(candidates.size()) - kMaxPointLights;
    candidates.resize(static_cast<std::size_t>(kMaxPointLights));
    out.points = std::move(candidates);
    return dropped;
}

/*
 * ============================================================================
 * GLSL
 * ============================================================================
 * **`kMaxPointLights` と `HD2D_MAX_POINT_LIGHTS` は必ず同じ値にすること。**
 * 食い違うと配列の外を読むのではなく、GL がリンク時にユニフォームを切り詰めるので
 * 「遠くの松明だけ静かに効かない」という気づきにくい壊れ方をする。
 */
const char *const kLightingGlsl = R"(
const int HD2D_MAX_POINT_LIGHTS = 16;

uniform vec3 u_light_dir;      // 面から光源へ
uniform vec3 u_key_color;
uniform vec3 u_sky_color;
uniform vec3 u_bounce_color;
uniform float u_ambient_scale;
uniform float u_shadow_strength;
uniform int u_point_count;
uniform vec4 u_point_pos_radius[HD2D_MAX_POINT_LIGHTS];  // xyz = 位置, w = 半径
uniform vec4 u_point_color[HD2D_MAX_POINT_LIGHTS];       // rgb = 色, a = 強さ
uniform mat4 u_light_view_projection;
uniform sampler2DShadow u_shadow_map;
uniform vec2 u_shadow_texel;   // 1/幅, 1/高さ

/*
 * 影。3x3 の PCF。`sampler2DShadow` なので `texture()` は「その深度より手前か」の
 * 0/1 を返し、ハードウェアが 4 点を混ぜてくれる（＝実質 12x12 相当のなめらかさ）。
 *
 * **影が無いときはここを 1 度も呼ばないこと。**影の地図が用意できていない場合
 * （`--prefab` の素材見物・FBO を作れなかった場合）は 0 番のテクスチャが結ばれており、
 * 深度比較つきサンプラで未完成のテクスチャを読むのは規格上ふるまいが決まっていない。
 * 呼び出し側は `u_shadow_strength > 0.0` を見てから呼ぶ（`hd2d_key_shadow`）。
 */
float hd2d_shadow(vec3 world, float n_dot_l)
{
    vec4 lp = u_light_view_projection * vec4(world, 1.0);
    vec3 p = ((lp.xyz / lp.w) * 0.5) + 0.5;
    // 影の地図の外は「日向」とする。**内側だけ暗くすると外周に段差が出る**ので、
    // 地図の当てはめ（shadow_map.cpp）が可視範囲を必ず覆っていることが前提になる。
    if ((p.z > 1.0) || (p.x < 0.0) || (p.x > 1.0) || (p.y < 0.0) || (p.y > 1.0)) {
        return 1.0;
    }
    /*
     * 深度の下駄。**傾いた面ほど 1 テクセルあたりの深度差が大きい**ので、
     * 法線と光のなす角で増やす（固定値にすると平らな床か急な壁のどちらかが必ず縞になる）。
     */
    float bias = mix(0.0040, 0.0007, clamp(n_dot_l, 0.0, 1.0));
    float sum = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 offset = vec2(float(i), float(j)) * u_shadow_texel;
            sum += texture(u_shadow_map, vec3(p.xy + offset, p.z - bias));
        }
    }
    return sum / 9.0;
}

/*
 * 点光源。**逆二乗ではなく半径で 0 になる式**を使う。コアの「光源半径」は
 * マス単位の打ち切りなので、物理ではなくその規則に合わせるほうが絵とゲームが一致する。
 *
 * `facing_floor` は「光に背を向けた面をどこまで暗くするか」。ボクセルは 0 に近く、
 * ビルボード（絵はすでに陰影付きで描かれている）は大きめにする。
 */
vec3 hd2d_point_lights(vec3 world, vec3 n, float facing_floor)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < u_point_count; ++i) {
        vec3 d = u_point_pos_radius[i].xyz - world;
        float dist = length(d);
        float radius = u_point_pos_radius[i].w;
        if (dist >= radius) {
            continue;
        }
        float fall = 1.0 - (dist / radius);
        fall *= fall;   // 縁をなめらかに 0 へ
        float lambert = max(dot(n, d / max(dist, 1e-4)), 0.0);
        sum += u_point_color[i].rgb * u_point_color[i].a * fall * mix(facing_floor, 1.0, lambert);
    }
    return sum;
}

/*!
 * 影の係数（0 = 完全な影 / 1 = 日向）。**影が無いときは地図を読まない。**
 * `hd2d_shadow` の注記のとおり、未完成のテクスチャを深度比較で読まないための門である。
 */
float hd2d_key_shadow(vec3 world, float n_dot_l)
{
    if (u_shadow_strength <= 0.0) {
        return 1.0;
    }
    return mix(1.0, hd2d_shadow(world, n_dot_l), u_shadow_strength);
}

//! 環境光（空の寒色を上から・地面の照り返しの暖色を下から）。設計書 §13。
vec3 hd2d_ambient(vec3 n)
{
    float up = (n.z * 0.5) + 0.5;
    return ((u_sky_color * up * 0.55) + (u_bounce_color * (1.0 - up) * 0.40)) * u_ambient_scale;
}
)";

/*
 * ============================================================================
 * トーンマップ
 * ============================================================================
 * **P7 でここから出て行った。**P5・P6 の間は物のシェーダの末尾で掛けていたが、
 * ブルームの閾値抽出はトーンマップ**前**の値を相手にしなければならない（設計書 §13）。
 * 1.0 で頭打ちになった後では「白く飛んだ紙」と「燃えている松明」が同じ値になるからである。
 *
 * したがって物のシェーダは**線形の HDR を書き**、掛けるのは合成の 1 か所だけになった。
 * 効果を全部切れば掛かる場所が変わっただけなので、**P5・P6 の見えと同一**である
 * （`--post-check` の (1) がそれを毎回確かめる）。
 */
const char *const kTonemapGlsl = R"(
//! トーンマップ（試作 `tools/voxel_probe/mill.py` と同じ係数）。**`hd2d_tonemap_cpu` と同じ式。**
vec3 hd2d_tonemap(vec3 lit)
{
    return vec3(1.0) - exp(-lit * 1.25);
}

/*!
 * 露出を渡す版（`PostParams::exposure`。2026-08-23）。
 * **1.25 を渡せば上と同じ**なので、既定のままなら絵は 1 ビットも変わらない。
 */
vec3 hd2d_tonemap_at(vec3 lit, float exposure)
{
    return vec3(1.0) - exp(-lit * exposure);
}
)";

float hd2d_tonemap_cpu(float lit)
{
    return 1.f - std::exp(-lit * 1.25f);
}

void LightUniforms::locate(GLuint program)
{
    this->light_dir = glGetUniformLocation(program, "u_light_dir");
    this->key_color = glGetUniformLocation(program, "u_key_color");
    this->sky_color = glGetUniformLocation(program, "u_sky_color");
    this->bounce_color = glGetUniformLocation(program, "u_bounce_color");
    this->ambient_scale = glGetUniformLocation(program, "u_ambient_scale");
    this->shadow_strength = glGetUniformLocation(program, "u_shadow_strength");
    this->point_count = glGetUniformLocation(program, "u_point_count");
    this->point_pos_radius = glGetUniformLocation(program, "u_point_pos_radius");
    this->point_color = glGetUniformLocation(program, "u_point_color");
    this->light_view_projection = glGetUniformLocation(program, "u_light_view_projection");
    this->shadow_map = glGetUniformLocation(program, "u_shadow_map");
    this->shadow_texel = glGetUniformLocation(program, "u_shadow_texel");
}

void upload_lighting(const LightUniforms &loc, const SceneLighting &lighting, const Mat4 &light_view_projection,
    GLuint shadow_texture, int shadow_side, int shadow_unit)
{
    const Vec3 dir = normalize(lighting.sun_dir);
    glUniform3f(loc.light_dir, dir.x, dir.y, dir.z);
    glUniform3f(loc.key_color, lighting.key_color.x, lighting.key_color.y, lighting.key_color.z);
    glUniform3f(loc.sky_color, lighting.sky_color.x, lighting.sky_color.y, lighting.sky_color.z);
    glUniform3f(loc.bounce_color, lighting.bounce_color.x, lighting.bounce_color.y, lighting.bounce_color.z);
    glUniform1f(loc.ambient_scale, lighting.ambient_scale);

    // 影のテクスチャが無ければ影は無い。**式を分岐させず強さを 0 にする**（分岐は増やさない）。
    const float strength = (shadow_texture != 0) ? lighting.shadow_strength : 0.f;
    glUniform1f(loc.shadow_strength, strength);
    glUniformMatrix4fv(loc.light_view_projection, 1, GL_FALSE, light_view_projection.m);
    glUniform1i(loc.shadow_map, shadow_unit);
    const float texel = (shadow_side > 0) ? (1.f / static_cast<float>(shadow_side)) : 0.f;
    glUniform2f(loc.shadow_texel, texel, texel);
    /*
     * **影が無くても「影サンプラとして正しいもの」を必ず結ぶ**（身代わりの註を見よ）。
     * 0 を結ぶと Adreno はその描画ごと捨てる。
     */
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(shadow_unit));
    glBindTexture(GL_TEXTURE_2D, (shadow_texture != 0) ? shadow_texture : shadow_placeholder_texture());
    glActiveTexture(GL_TEXTURE0);

    const int count = std::min(static_cast<int>(lighting.points.size()), kMaxPointLights);
    glUniform1i(loc.point_count, count);
    if (count > 0) {
        float pos_radius[kMaxPointLights * 4]{};
        float colors[kMaxPointLights * 4]{};
        for (int i = 0; i < count; ++i) {
            const PointLight &l = lighting.points[static_cast<std::size_t>(i)];
            pos_radius[(i * 4) + 0] = l.x;
            pos_radius[(i * 4) + 1] = l.y;
            pos_radius[(i * 4) + 2] = l.z;
            pos_radius[(i * 4) + 3] = l.radius;
            colors[(i * 4) + 0] = l.r;
            colors[(i * 4) + 1] = l.g;
            colors[(i * 4) + 2] = l.b;
            colors[(i * 4) + 3] = l.intensity;
        }
        glUniform4fv(loc.point_pos_radius, count, pos_radius);
        glUniform4fv(loc.point_color, count, colors);
    }
}

} // namespace hd2d
