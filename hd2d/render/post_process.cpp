/*!
 * @file post_process.cpp
 * @brief `post_process.h` の実装。
 */
#include "render/post_process.h"

#include "render/gl_program.h"
#include "render/lighting.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hd2d {

using namespace hd2d::gl;

namespace {

//! ブルームの段の数。1600x900 なら 800x450 から 50x28 まで。
constexpr int kBloomLevels = 5;
//! これより小さくしない（1x1 まで落とすと縮小の誤差が支配的になる）。
constexpr int kMinRtSide = 8;
//! 被写界深度のピラミッドの最大段数（半分 → 1/64。1600x900 なら 800x450 から 25x14）。
constexpr int kDofMaxLevels = 6;
/*!
 * 錯乱円を正規化する分母（画面に見えている最遠 − inner）の下限（マス）。
 * カメラを極端に寄せたときに勾配が立ちすぎるのと、検査経路（view_projection を
 * 渡さない呼び出し＝単位行列）で分母が 0 になるのを防ぐ。単位行列のときは
 * 隔たりが 1 マスに満たないので、この下限と inner に阻まれて被写界深度は事実上効かない
 * （＝従来の検査の見えを変えない）。
 */
constexpr float kDofSpanMinTiles = 6.f;

/*
 * 全画面の三角形。**四角形（2 枚の三角形）にしない。**対角線の上で
 * 2 つの三角形が接するとその線上の画素が 2 回走り、ぼかしの重みが継ぎ目でずれる。
 * 頂点属性は持たず `gl_VertexID` から作るので、VBO が要らない（VAO だけ結べばよい）。
 */
const char *const kFullscreenVertex = R"(#version 460 core
out vec2 v_uv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4((p * 2.0) - 1.0, 0.0, 1.0);
}
)";

/*
 * 閾値抽出（設計書 §13 の 1 段目）。**トーンマップ前の値**を相手にする。
 * 硬く切ると明るさが行き来する縁でブルームが点滅するので、`knee` の幅で柔らかく当てる
 * （Unreal / Unity のブルームと同じ二次曲線）。
 */
const char *const kBrightFragment = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_scene;
uniform float u_threshold;
uniform float u_knee;
out vec4 o_color;
void main()
{
    vec3 c = texture(u_scene, v_uv).rgb;
    float luma = max(c.r, max(c.g, c.b));
    float knee = max(u_knee, 1e-4);
    // 閾値のまわり ±knee を二次曲線で繋ぐ。
    float soft = clamp(luma - u_threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee);
    float contribution = max(soft, luma - u_threshold) / max(luma, 1e-4);
    o_color = vec4(c * contribution, 1.0);
}
)";

//! 縮小。4 点の双一次で拾う（テクセルの中間を狙うので実質 16 点ぶん）。
const char *const kDownFragment = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_source;
uniform vec2 u_source_texel;
out vec4 o_color;
void main()
{
    vec2 t = u_source_texel;
    vec3 sum = texture(u_source, v_uv + vec2(-t.x, -t.y)).rgb;
    sum += texture(u_source, v_uv + vec2( t.x, -t.y)).rgb;
    sum += texture(u_source, v_uv + vec2(-t.x,  t.y)).rgb;
    sum += texture(u_source, v_uv + vec2( t.x,  t.y)).rgb;
    o_color = vec4(sum * 0.25, 1.0);
}
)";

/*
 * 拡大して足す（3x3 のテント）。**加算合成で呼ぶこと**（`glBlendFunc(GL_ONE, GL_ONE)`）。
 * 段ごとに足していくので、細いにじみと広いにじみが自然に重なる。
 */
const char *const kUpFragment = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_source;
uniform vec2 u_source_texel;
uniform float u_scale;
out vec4 o_color;
void main()
{
    vec2 t = u_source_texel;
    vec3 sum = texture(u_source, v_uv + vec2(-t.x,  t.y)).rgb;
    sum += texture(u_source, v_uv + vec2( 0.0,  t.y)).rgb * 2.0;
    sum += texture(u_source, v_uv + vec2( t.x,  t.y)).rgb;
    sum += texture(u_source, v_uv + vec2(-t.x,  0.0)).rgb * 2.0;
    sum += texture(u_source, v_uv).rgb * 4.0;
    sum += texture(u_source, v_uv + vec2( t.x,  0.0)).rgb * 2.0;
    sum += texture(u_source, v_uv + vec2(-t.x, -t.y)).rgb;
    sum += texture(u_source, v_uv + vec2( 0.0, -t.y)).rgb * 2.0;
    sum += texture(u_source, v_uv + vec2( t.x, -t.y)).rgb;
    o_color = vec4((sum / 16.0) * u_scale, 1.0);
}
)";

/*
 * 被写界深度のためのぼかし。**半径を外から渡せる 9 点・α も同じ重みで畳む**。
 *
 * ピラミッド（P10 レビュー 9）の各段に軽く掛けて、段の刻み（トライリニアの
 * 双一次格子）が四角く見えるのを消す。中身は premultiplied CoC なので、
 * **α（錯乱円）を落とすと合成の割り戻しが壊れる。**rgb だけ畳んではならない。
 */
const char *const kBlurFragment = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_source;
uniform vec2 u_source_texel;
uniform float u_blur_radius;
out vec4 o_color;
void main()
{
    vec2 t = u_source_texel * u_blur_radius;
    vec4 sum = texture(u_source, v_uv) * 4.0;
    sum += texture(u_source, v_uv + vec2( t.x, 0.0)) * 2.0;
    sum += texture(u_source, v_uv + vec2(-t.x, 0.0)) * 2.0;
    sum += texture(u_source, v_uv + vec2( 0.0, t.y)) * 2.0;
    sum += texture(u_source, v_uv + vec2( 0.0,-t.y)) * 2.0;
    sum += texture(u_source, v_uv + vec2( t.x, t.y));
    sum += texture(u_source, v_uv + vec2(-t.x, t.y));
    sum += texture(u_source, v_uv + vec2( t.x,-t.y));
    sum += texture(u_source, v_uv + vec2(-t.x,-t.y));
    o_color = sum / 16.0;
}
)";

/*
 * 被写界深度の下ごしらえ（P10 レビュー 9）。深度から**キャラとの前後の隔たり**を測り、
 * premultiplied CoC（色×錯乱円, 錯乱円）にして半分の大きさへ書く。
 *
 * premultiplied にするのは 2 つの縁の事故を一度に消すためである:
 * - 焦点帯の色は錯乱円 ≒ 0 なので写しに**ほぼ入らない**（鮮明な色がぼけへ滲まない）
 * - 合成が α で割り戻すと、**ぼけたい色だけの平均**が出てくる（縁の外へ輪郭ごと溶ける）
 *
 * 錯乱円の式は合成（`kCompositeBody`）と**同じでなければならない**。
 * 違うと「写しに入っている量」と「引く量」が食い違って縁に段が出る。
 */
const char *const kDofPrepareFragment = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_depth;
uniform mat4 u_inv_view_projection;
uniform vec2 u_focus_xy;
uniform vec2 u_focus_forward;
uniform float u_dof_inner;
uniform float u_dof_span;
out vec4 o_color;
void main()
{
    vec3 c = texture(u_scene, v_uv).rgb;
    float window_z = texture(u_depth, v_uv).r;
    float a = 0.0;
    // 幾何が 1 つも無い画素は錯乱円 0（ぼけへ何も差し出さない。ヘッダの注記と同じ理由）。
    if (window_z < 0.999999) {
        vec4 clip = vec4((v_uv * 2.0) - 1.0, (window_z * 2.0) - 1.0, 1.0);
        vec4 world = u_inv_view_projection * clip;
        world /= world.w;
        float along = abs(dot(world.xy - u_focus_xy, u_focus_forward));
        // 画面に見えている最遠（u_dof_span）で正規化。**比例のまま頭打ちにしない。**
        a = clamp((along - u_dof_inner) / max(u_dof_span, 1e-3), 0.0, 1.0);
    }
    o_color = vec4(c * a, a);
}
)";

//! ピラミッドの縮小。`u_lod` で**読む段を明示する**（1 枚のミップ付きテクスチャなので）。
const char *const kDofDownFragment = R"(#version 460 core
in vec2 v_uv;
uniform sampler2D u_source;
uniform vec2 u_source_texel;
uniform float u_lod;
out vec4 o_color;
void main()
{
    vec2 t = u_source_texel;
    vec4 sum = textureLod(u_source, v_uv + vec2(-t.x, -t.y), u_lod);
    sum += textureLod(u_source, v_uv + vec2( t.x, -t.y), u_lod);
    sum += textureLod(u_source, v_uv + vec2(-t.x,  t.y), u_lod);
    sum += textureLod(u_source, v_uv + vec2( t.x,  t.y), u_lod);
    o_color = sum * 0.25;
}
)";

//! 合成。順序はヘッダの表のとおり。トーンマップは `kTonemapGlsl` の 1 本しか無い。
const char *const kCompositeBody = R"(
in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_depth;
uniform sampler2D u_bloom;
uniform sampler2D u_dof_pyr;
uniform sampler3D u_lut;

uniform float u_z_near;
uniform float u_z_far;

uniform int u_use_fog;
uniform int u_use_dof;
uniform int u_use_bloom;
uniform int u_use_grade;
uniform int u_use_vignette;

/*!
 * 被写界深度（P10 レビュー 8 で軸が確定・レビュー 9 で掛け方を作り直し）。
 *
 * | 何 | |
 * |---|---|
 * | `u_inv_view_projection` | 画素 → ワールド座標。深度から位置を戻すのに要る |
 * | `u_focus_xy`            | キャラの水平位置（マス） |
 * | `u_focus_forward`       | **カメラの水平前方向**（正規化）。奥行きを測る軸 |
 * | `u_dof_inner`           | この奥行きまでは鮮明（マス） |
 * | `u_dof_span`            | **画面に実際に見えている最遠**の隔たり − inner（マス）。正規化の分母 |
 * | `u_dof_radius_px`       | いちばん遠くでの錯乱円の半径（**フル解像度の画素**） |
 * | `u_dof_pyr`             | premultiplied CoC（色×錯乱円, 錯乱円）のミップピラミッド |
 * | `u_dof_lod_max`         | ピラミッドのいちばん粗い段 |
 *
 * **前後（奥行き）だけで決める。左右は一定。高さも見ない**
 * （2026-08-09 に決めた:「横方向はぼかしの強度を変えない。前後の距離のみで
 * ぼかし強度を変化させるのが TiltShift だ。一様に帯でもなく、前後の奥行、
 * キャラの前後の距離に比例、左右は一定」）。
 *
 * 測るのは**キャラからのベクトルを前方向へ射影した長さ**（水平距離だと同心円になり、
 * 真横に離れた木までぼける。1 つ前の版がそうだった）。焦点面が地面と平行に寝ている
 * 状態、つまりレンズを傾けた TiltShift そのものである。
 *
 * **掛け方（レビュー 9）:** ぼけの量は「混ぜる重み」ではなく**ピラミッドの段（lod）**が
 * 運ぶ。重みで運ぶと 1 に届かない画素すべてで鮮明な元絵が透け、木の輪郭のような
 * 高コントラストの縁だけが生き残る（気づいたとおり）。lod は錯乱円の半径から
 * 選ぶので、**画面に見えている範囲では奥へ行くほど比例してぼけ続ける**。
 */
uniform mat4 u_inv_view_projection;
uniform vec2 u_focus_xy;
uniform vec2 u_focus_forward;
uniform float u_dof_inner;
uniform float u_dof_span;
uniform float u_dof_radius_px;
uniform float u_dof_lod_max;

uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_range;
uniform float u_fog_density;

uniform float u_bloom_intensity;

uniform float u_vignette_strength;
uniform float u_vignette_radius;
uniform float u_exposure;   //!< トーンマップの係数（既定 1.25 ＝ 従来と同じ）
uniform float u_hdr;        //!< HDR 風の強さ（0 = 掛けない）
uniform float u_lut_size;

out vec4 o_color;

//! 窓深度 [0,1] → 視点からの距離（マス）。
float hd2d_view_distance(float window_z)
{
    float ndc = (window_z * 2.0) - 1.0;
    return (2.0 * u_z_near * u_z_far) / (u_z_far + u_z_near - (ndc * (u_z_far - u_z_near)));
}

void main()
{
    vec3 color = texture(u_scene, v_uv).rgb;
    float window_z = texture(u_depth, v_uv).r;
    /*
     * **幾何が 1 つも無い画素**（深度が奥のまま）はフォグにも被写界深度にも掛けない。
     * 掛けると「四隅に背景色が見えているか」を数える検査が何も検出できなくなる
     * （ヘッダの注記）。奥行きの手掛かりとしても、何も無い所をぼかす意味は無い。
     */
    bool has_geometry = window_z < 0.999999;
    float distance = has_geometry ? hd2d_view_distance(window_z) : u_z_far;

    if (u_use_dof != 0) {
        /*
         * (1) この画素のワールド座標を戻し、**キャラからの前後の隔たり**を測る。
         * 測るのは前方向へ射影した長さで、**左右へどれだけ離れていても効かない**。
         * ここまで 3 度外している。
         *
         * **描写外（幾何が無い画素）も素通しにしない**（レビュー 9 追記）。
         * 自分の隔たりは持たないが、隣のぼけた幾何の裾はここへ滲み出してくる。
         * 素通しにすると、手前のブロックが未踏破の闇と接する下辺・縦辺・斜め辺だけ
         * 輪郭が鮮明に残る（実機のスクリーンショットそのもの）。
         */
        float a = 0.0;
        if (has_geometry) {
            vec4 clip = vec4((v_uv * 2.0) - 1.0, (window_z * 2.0) - 1.0, 1.0);
            vec4 world = u_inv_view_projection * clip;
            world /= world.w;
            float along = abs(dot(world.xy - u_focus_xy, u_focus_forward));

            /*
             * (2) 錯乱円。**画面に見えている最遠までずっと比例**させる（レビュー 9）。
             * 固定のマス数で割ると途中で頭打ちになり「一定以上の距離で均一」に見える。
             * 式は下ごしらえ（kDofPrepareFragment）と同じでなければならない。
             */
            a = clamp((along - u_dof_inner) / max(u_dof_span, 1e-3), 0.0, 1.0);
        }

        /*
         * (3) **縁を溶かす。**自分の錯乱円だけで引くと、ぼけた物と鮮明な物の境目で
         * 輪郭が生き残る（ぼけは輪郭の外へ滲み出すものだから）。近所の錯乱円を
         * ピラミッドの粗い段の α から拾い、大きいほうを採る。近い段と遠い段の
         * 2 か所を見るのは、滲み出しの届く距離が錯乱円の大きさに比例するため。
         */
        float spread = max(textureLod(u_dof_pyr, v_uv, min(2.5, u_dof_lod_max)).a,
            textureLod(u_dof_pyr, v_uv, min(4.0, u_dof_lod_max)).a * 0.85);
        float a_eff = max(a, spread * 0.9);

        float radius = a_eff * u_dof_radius_px; //!< フル解像度の画素
        if (radius > 0.25) {
            /*
             * (4) **ぼけの量は段（lod）が運ぶ。**混ぜる重みで運ぶと、1 に届かない
             * 画素すべてで鮮明な元絵が透けて縁が残る（前の版の敗因）。重みは
             * 「半径が画素に満たない所で立ち上がる」ためだけの 0→1 である。
             * premultiplied なので α で割り戻す（ぼけたい色だけの平均が出てくる）。
             *
             * 門は**かぶりの割合**（pm.a をその場のぼけ量 a_eff で正規化した被覆率）。
             * pm.a の絶対値で門を作ると、**錯乱円が小さいほど裾が薄くなり、
             * ぼけは見えるのに輪郭だけ素通しで残る**（実機の手前側で出た。
             * 合成シーンは手前でも錯乱円が大きく、見逃した）。割合にすれば
             * 輪郭の上で ≒1・裾の先で 0 という形が、ぼけの大小によらず保たれる。
             * 描写外（幾何の無い画素）も同じ式でよい: かぶりが無ければ 0 のままである。
             */
            float lod = clamp(log2(max(radius, 1.0)) - 1.0, 0.0, u_dof_lod_max);
            vec4 pm = textureLod(u_dof_pyr, v_uv, lod);
            vec3 blurred = pm.rgb / max(pm.a, 1e-3);
            float cover = clamp(pm.a / max(a_eff * 0.5, 1e-3), 0.0, 1.0);
            float w = smoothstep(0.35, 1.5, radius) * cover;
            color = mix(color, blurred, w);
        }
    }

    if ((u_use_fog != 0) && has_geometry) {
        float f = clamp((distance - u_fog_start) / max(u_fog_range, 1e-3), 0.0, 1.0);
        // 二乗で寄せる。線形だと近くから灰色が乗って、ダンジョンが白っぽくなる。
        color = mix(color, u_fog_color, f * f * u_fog_density);
    }

    if (u_use_bloom != 0) {
        color += texture(u_bloom, v_uv).rgb * u_bloom_intensity;
    }

    // **ここで [0,1] へ。**これより前は線形の HDR、これより後は画面の値である。
    color = hd2d_tonemap_at(color, u_exposure);

    /*
     * **HDR 風**（2026-08-23。表示機への HDR 出力ではなく、写真の HDR 合成の見え）。
     *
     * 3 つを同時に掛ける。影を開く（累乗で持ち上げ）・ハイライトを戻す（上から寝かせる）・
     * 色を締める（彩度をわずかに上げる）。**トーンマップの後**でやるのは、
     * 発散した値の上で影を持ち上げると光源の周りだけが濁るためである。
     */
    if (u_hdr > 0.0) {
        vec3 opened = pow(color, vec3(1.0 / (1.0 + (0.60 * u_hdr))));
        vec3 rolled = vec3(1.0) - pow(vec3(1.0) - opened, vec3(1.0 + (0.50 * u_hdr)));
        float l = dot(rolled, vec3(0.2126, 0.7152, 0.0722));
        vec3 punchy = mix(vec3(l), rolled, 1.0 + (0.25 * u_hdr));
        color = clamp(mix(color, punchy, clamp(u_hdr, 0.0, 1.0)), 0.0, 1.0);
    }

    if (u_use_grade != 0) {
        // 格子の中心を狙う（端の 1/2 テクセルを外すと 0 と 1 が正しく引けない）。
        vec3 coord = ((color * (u_lut_size - 1.0)) + 0.5) / u_lut_size;
        color = texture(u_lut, coord).rgb;
    }

    if (u_use_vignette != 0) {
        float d = length(v_uv - vec2(0.5)) * 1.41421356;
        color *= 1.0 - (u_vignette_strength * smoothstep(u_vignette_radius, 1.0, d));
    }

    o_color = vec4(color, 1.0);
}
)";

GradeParams sepia_grade_impl(const GradeParams &base, float amount)
{
    const float a = (amount < 0.f) ? 0.f : ((amount > 1.f) ? 1.f : amount);
    GradeParams out = base;
    //! 彩度を落とす（**0 にはしない**。真の白黒に茶を掛けると印刷物に見える）。
    out.saturation = base.saturation + ((0.18f - base.saturation) * a);
    //! 影は冷たいまま・光を黄褐へ。古写真の見えはこの分離から来る。
    out.shadow_tint.x = base.shadow_tint.x + ((1.010f - base.shadow_tint.x) * a);
    out.shadow_tint.y = base.shadow_tint.y + ((0.975f - base.shadow_tint.y) * a);
    out.shadow_tint.z = base.shadow_tint.z + ((0.930f - base.shadow_tint.z) * a);
    out.highlight_tint.x = base.highlight_tint.x + ((1.135f - base.highlight_tint.x) * a);
    out.highlight_tint.y = base.highlight_tint.y + ((1.020f - base.highlight_tint.y) * a);
    out.highlight_tint.z = base.highlight_tint.z + ((0.815f - base.highlight_tint.z) * a);
    //! 黒を少し浮かせる（褪せた紙の黒）。
    out.lift = base.lift + ((0.055f - base.lift) * a);
    return out;
}

//! 綴りを 1 つ読む。見つからなければ nullptr。
bool *flag_by_name(PostFlags &flags, const std::string &name)
{
    if (name == "fog") {
        return &flags.fog;
    }
    if (name == "dof") {
        return &flags.dof;
    }
    if (name == "bloom") {
        return &flags.bloom;
    }
    if (name == "grade") {
        return &flags.grade;
    }
    if (name == "vignette") {
        return &flags.vignette;
    }
    return nullptr;
}

float srgb_luma(const Vec3 &c)
{
    return (0.2126f * c.x) + (0.7152f * c.y) + (0.0722f * c.z);
}

} // namespace

/* ============================================================== PostFlags */

bool PostFlags::any() const
{
    return this->fog || this->dof || this->bloom || this->grade || this->vignette;
}

bool PostFlags::parse(const std::string &spec, PostFlags &out, std::string &err)
{
    if (spec.empty()) {
        return true;
    }
    if (spec == "off") {
        out = PostFlags{ false, false, false, false, false };
        return true;
    }
    if (spec == "all") {
        out = PostFlags{};
        return true;
    }
    /*
     * `-dof` のように引き算だけを並べたときは**全部入りから落とす**。
     * 足し算が 1 つでも混じっていたら**何も無い所から足す**。
     * 「1 つだけ見たい」と「1 つだけ切りたい」はどちらも頻繁に要るが、
     * 綴りを 2 種類覚えたくないので、書かれたものから読み取る。
     */
    bool has_add = false;
    std::istringstream scan(spec);
    std::string token;
    while (std::getline(scan, token, ',')) {
        if (!token.empty() && (token[0] != '-')) {
            has_add = true;
        }
    }
    out = has_add ? PostFlags{ false, false, false, false, false } : PostFlags{};

    std::istringstream stream(spec);
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            continue;
        }
        const bool enable = (token[0] != '-');
        const std::string name = enable ? token : token.substr(1);
        bool *const target = flag_by_name(out, name);
        if (target == nullptr) {
            err = "ポスト処理の名前が読めません: \"" + name
                + "\"（fog / dof / bloom / grade / vignette / off / all）";
            return false;
        }
        *target = enable;
    }
    return true;
}

std::string PostFlags::to_line() const
{
    std::string line;
    const auto add = [&line](const char *name, bool on) {
        line += name;
        line += on ? "+ " : "- ";
    };
    add("fog", this->fog);
    add("dof", this->dof);
    add("bloom", this->bloom);
    add("grade", this->grade);
    add("vignette", this->vignette);
    if (!line.empty()) {
        line.pop_back();
    }
    return line;
}

/* =================================================================== LUT */

DofView measure_dof(const PostParams &params, const Mat4 &view_projection, float focus_x, float focus_y,
    float forward_x, float forward_y)
{
    DofView out;
    //! 強いほど鮮明な帯を狭める（＝模型に寄る）。
    const float tighten = std::clamp(1.4f - (params.dof_strength * 0.45f), 0.25f, 1.4f);
    out.inner = std::max(0.4f, params.dof_inner * tighten);
    //! いちばん遠くでの錯乱円の半径（フル解像度の画素）。強さは半径に効く（重みにではなく）。
    out.radius_px = (params.dof_strength > 0.01f)
        ? (params.dof_max_radius * std::clamp(params.dof_strength, 0.05f, 2.4f))
        : 0.f;
    {
        // 奥行きを測る軸（カメラの水平前方向）。長さ 0 だと全画素が「奥行き 0」になる。
        const float len = std::sqrt((forward_x * forward_x) + (forward_y * forward_y));
        if (len > 1e-4f) {
            out.forward_x = forward_x / len;
            out.forward_y = forward_y / len;
        }
    }
    out.span = kDofSpanMinTiles;
    if (out.radius_px <= 0.f) {
        return out;
    }
    /*
     * 錯乱円の分母 ＝ **画面に実際に見えている最遠**の前後の隔たり（レビュー 9:
     * 「画面表示される範囲ではどこまでも比例してぼかせないか」）。固定のマス数で割ると
     * 画面の途中で頭打ちになり、そこから奥が全部同じぼけになる。
     * 画面の縁（四隅と上下の中央）から地面 (z = 0) へレイを下ろして最大を取る。
     * 地面に届かないレイは far 面の点で代用する（見える幾何はそれより遠くに無い）。
     */
    const Mat4 inv_vp = inverse(view_projection);
    const auto unproject = [&inv_vp](float nx, float ny, float nz, float dst[3]) {
        const float v[4] = { nx, ny, nz, 1.f };
        float r[4] = { 0.f, 0.f, 0.f, 0.f };
        for (int row = 0; row < 4; ++row) {
            r[row] = (inv_vp.m[row] * v[0]) + (inv_vp.m[4 + row] * v[1]) + (inv_vp.m[8 + row] * v[2])
                + (inv_vp.m[12 + row] * v[3]);
        }
        const float w = (std::fabs(r[3]) > 1e-9f) ? r[3] : 1.f;
        dst[0] = r[0] / w;
        dst[1] = r[1] / w;
        dst[2] = r[2] / w;
    };
    float farthest = 0.f;
    for (const float nx : { -1.f, 0.f, 1.f }) {
        for (const float ny : { -1.f, 1.f }) {
            float p0[3];
            float p1[3];
            unproject(nx, ny, -1.f, p0);
            unproject(nx, ny, 1.f, p1);
            const float dz = p1[2] - p0[2];
            float t = (std::fabs(dz) > 1e-6f) ? (-p0[2] / dz) : 1.f;
            t = std::clamp(t, 0.f, 1.f);
            const float gx = p0[0] + ((p1[0] - p0[0]) * t);
            const float gy = p0[1] + ((p1[1] - p0[1]) * t);
            const float along = std::fabs(((gx - focus_x) * out.forward_x) + ((gy - focus_y) * out.forward_y));
            farthest = (along > farthest) ? along : farthest;
        }
    }
    out.span = std::max(farthest - out.inner, kDofSpanMinTiles);
    return out;
}

GradeParams sepia_grade(const GradeParams &base, float amount)
{
    return sepia_grade_impl(base, amount);
}

void bake_lut(const GradeParams &grade, int size, std::vector<unsigned char> &rgba)
{
    const int n = std::max(2, size);
    rgba.assign(static_cast<std::size_t>(n) * n * n * 4, 0);
    const float denom = static_cast<float>(n - 1);

    for (int b = 0; b < n; ++b) {
        for (int g = 0; g < n; ++g) {
            for (int r = 0; r < n; ++r) {
                Vec3 c{ static_cast<float>(r) / denom, static_cast<float>(g) / denom,
                    static_cast<float>(b) / denom };

                // (1) 黒の持ち上げ。真っ黒を残さない。
                c = Vec3{ grade.lift + (c.x * (1.f - grade.lift)), grade.lift + (c.y * (1.f - grade.lift)),
                    grade.lift + (c.z * (1.f - grade.lift)) };
                // (2) コントラスト（0.5 が軸）。
                c = Vec3{ ((c.x - 0.5f) * grade.contrast) + 0.5f, ((c.y - 0.5f) * grade.contrast) + 0.5f,
                    ((c.z - 0.5f) * grade.contrast) + 0.5f };
                // (3) 彩度。
                const float luma = srgb_luma(c);
                c = Vec3{ luma + ((c.x - luma) * grade.saturation), luma + ((c.y - luma) * grade.saturation),
                    luma + ((c.z - luma) * grade.saturation) };
                /*
                 * (4) 影と光の色分け。**輝度で滑らかに混ぜる。**
                 * `smoothstep` にするのは、しきい値で切ると中間調に帯が出るため。
                 */
                const float t0 = std::clamp(srgb_luma(c), 0.f, 1.f);
                const float t = t0 * t0 * (3.f - (2.f * t0));
                const Vec3 tint{ grade.shadow_tint.x + ((grade.highlight_tint.x - grade.shadow_tint.x) * t),
                    grade.shadow_tint.y + ((grade.highlight_tint.y - grade.shadow_tint.y) * t),
                    grade.shadow_tint.z + ((grade.highlight_tint.z - grade.shadow_tint.z) * t) };
                c = Vec3{ c.x * tint.x, c.y * tint.y, c.z * tint.z };

                const std::size_t index = (static_cast<std::size_t>((b * n * n) + (g * n) + r)) * 4;
                rgba[index + 0] = static_cast<unsigned char>(std::lround(std::clamp(c.x, 0.f, 1.f) * 255.f));
                rgba[index + 1] = static_cast<unsigned char>(std::lround(std::clamp(c.y, 0.f, 1.f) * 255.f));
                rgba[index + 2] = static_cast<unsigned char>(std::lround(std::clamp(c.z, 0.f, 1.f) * 255.f));
                rgba[index + 3] = 255;
            }
        }
    }
}

bool write_lut_cube(const std::string &path, const GradeParams &grade, int size, std::string &err)
{
    std::vector<unsigned char> rgba;
    bake_lut(grade, size, rgba);
    const int n = std::max(2, size);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        err = "LUT を書けません: " + path;
        return false;
    }
    // **見出しは ASCII だけにする。**`.cube` を読む画像ツールが UTF-8 を想定しているとは限らない。
    out << "# Hengband HD2D - baked by bake_lut() in hd2d/render/post_process.cpp\n";
    out << "TITLE \"hengband-hd2d\"\n";
    out << "LUT_3D_SIZE " << n << "\n";
    out << "DOMAIN_MIN 0.0 0.0 0.0\n";
    out << "DOMAIN_MAX 1.0 1.0 1.0\n";
    char line[96]{};
    // `.cube` は**赤がいちばん速く回る**。`bake_lut` の並びと同じ。
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        std::snprintf(line, sizeof(line), "%.6f %.6f %.6f\n", static_cast<float>(rgba[i]) / 255.f,
            static_cast<float>(rgba[i + 1]) / 255.f, static_cast<float>(rgba[i + 2]) / 255.f);
        out << line;
    }
    return out.good();
}

bool read_lut_cube(const std::string &path, int &size, std::vector<unsigned char> &rgba, std::string &err)
{
    std::ifstream in(path);
    if (!in) {
        err = "LUT を読めません: " + path;
        return false;
    }
    int n = 0;
    float domain_min = 0.f;
    float domain_max = 1.f;
    std::vector<float> values;
    std::string line;
    while (std::getline(in, line)) {
        // 行末の CR を落とす（`.cube` は Windows と Unix の両方から来る）。
        while (!line.empty() && ((line.back() == '\r') || (line.back() == '\n'))) {
            line.pop_back();
        }
        std::istringstream fields(line);
        std::string head;
        if (!(fields >> head) || head.empty() || (head[0] == '#')) {
            continue;
        }
        if (head == "LUT_3D_SIZE") {
            fields >> n;
            continue;
        }
        if (head == "DOMAIN_MIN") {
            fields >> domain_min;
            continue;
        }
        if (head == "DOMAIN_MAX") {
            fields >> domain_max;
            continue;
        }
        // 数値 3 つの行だけを拾う（`TITLE` などの見出しはここで落ちる）。
        std::istringstream numbers(line);
        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        if (numbers >> r >> g >> b) {
            values.push_back(r);
            values.push_back(g);
            values.push_back(b);
        }
    }
    if (n < 2) {
        err = "LUT に LUT_3D_SIZE がありません: " + path;
        return false;
    }
    const std::size_t want = static_cast<std::size_t>(n) * n * n * 3;
    if (values.size() != want) {
        char buf[160]{};
        std::snprintf(buf, sizeof(buf), "LUT の値の数が合いません（%zu 個、%zu 個のはず）: ",
            values.size(), want);
        err = buf + path;
        return false;
    }
    const float span = ((domain_max - domain_min) > 1e-6f) ? (domain_max - domain_min) : 1.f;
    rgba.assign(static_cast<std::size_t>(n) * n * n * 4, 255);
    for (std::size_t i = 0; i < (values.size() / 3); ++i) {
        for (int c = 0; c < 3; ++c) {
            const float v = (values[(i * 3) + static_cast<std::size_t>(c)] - domain_min) / span;
            rgba[(i * 4) + static_cast<std::size_t>(c)] =
                static_cast<unsigned char>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
        }
    }
    size = n;
    return true;
}

/* ============================================================= PostChain */

bool PostChain::init(std::string &err)
{
    this->prog_bright_ = compile_program(kFullscreenVertex, kBrightFragment, err);
    if (this->prog_bright_ == 0) {
        return false;
    }
    this->prog_down_ = compile_program(kFullscreenVertex, kDownFragment, err);
    if (this->prog_down_ == 0) {
        return false;
    }
    this->prog_up_ = compile_program(kFullscreenVertex, kUpFragment, err);
    if (this->prog_up_ == 0) {
        return false;
    }
    this->prog_blur_ = compile_program(kFullscreenVertex, kBlurFragment, err);
    if (this->prog_blur_ == 0) {
        return false;
    }
    this->prog_dof_prepare_ = compile_program(kFullscreenVertex, kDofPrepareFragment, err);
    if (this->prog_dof_prepare_ == 0) {
        return false;
    }
    this->prog_dof_down_ = compile_program(kFullscreenVertex, kDofDownFragment, err);
    if (this->prog_dof_down_ == 0) {
        return false;
    }
    // トーンマップは `lighting.h` の 1 本しか無い。ここは連結するだけ。
    const std::string composite = std::string("#version 460 core\n") + kTonemapGlsl + kCompositeBody;
    this->prog_composite_ = compile_program(kFullscreenVertex, composite.c_str(), err);
    if (this->prog_composite_ == 0) {
        return false;
    }

    glGenVertexArrays(1, &this->vao_);

    /*
     * **わざと壊すオプション**（P6 の `HD2D_BREAK_SHADOW_MOTION` と同じ考え方）。
     * `HD2D_BREAK_POST=all` あるいは `HD2D_BREAK_POST=bloom,fog` で、**入れたつもりの効果を
     * 黙って落とす。**`--post-check` は「効果を入れれば画素が変わる」ことを見る検査なので、
     * これを立てると **FAIL しなければならない。**落ちなければ、それは
     * 「効いていないことを検出できない検査」だったということになる（設計書 §14-4）。
     */
    const char *const broken = std::getenv("HD2D_BREAK_POST");
    if ((broken != nullptr) && (broken[0] != '\0')) {
        std::string spec_err;
        PostFlags asked;
        if (PostFlags::parse((std::string(broken) == "all") ? std::string("all") : std::string(broken),
                asked, spec_err)) {
            this->broken_ = asked;
            std::fprintf(stderr, "[hd2d] HD2D_BREAK_POST: **%s を掛けません**"
                                 "（検査の検査。--post-check は FAIL になるのが正しい）\n",
                asked.to_line().c_str());
        } else {
            std::fprintf(stderr, "[hd2d] HD2D_BREAK_POST: %s\n", spec_err.c_str());
        }
    }

    // 既定の LUT を焼いて載せる（`--lut=` があれば呼び出し側が上書きする）。
    if (!this->set_grade(GradeParams{}, err)) {
        return false;
    }
    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "ポスト処理の用意で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void PostChain::shutdown()
{
    this->release_targets();
    if (this->vao_ != 0) {
        glDeleteVertexArrays(1, &this->vao_);
        this->vao_ = 0;
    }
    if (this->lut_ != 0) {
        glDeleteTextures(1, &this->lut_);
        this->lut_ = 0;
    }
    for (GLuint *program : { &this->prog_bright_, &this->prog_down_, &this->prog_up_,
             &this->prog_blur_, &this->prog_dof_prepare_, &this->prog_dof_down_,
             &this->prog_composite_ }) {
        if (*program != 0) {
            glDeleteProgram(*program);
            *program = 0;
        }
    }
}

void PostChain::release_rt(Rt &rt)
{
    if (rt.fbo != 0) {
        glDeleteFramebuffers(1, &rt.fbo);
        rt.fbo = 0;
    }
    if (rt.texture != 0) {
        glDeleteTextures(1, &rt.texture);
        rt.texture = 0;
    }
    rt.w = 0;
    rt.h = 0;
}

void PostChain::release_targets()
{
    for (auto &level : this->bloom_) {
        this->release_rt(level);
    }
    this->bloom_.clear();
    for (GLuint &fbo : this->dof_pyr_fbos_) {
        if (fbo != 0) {
            glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
    }
    this->dof_pyr_fbos_.clear();
    if (this->dof_pyr_tex_ != 0) {
        glDeleteTextures(1, &this->dof_pyr_tex_);
        this->dof_pyr_tex_ = 0;
    }
    for (auto &scratch : this->dof_scratch_) {
        this->release_rt(scratch);
    }
    this->dof_scratch_.clear();
    this->dof_base_w_ = 0;
    this->dof_base_h_ = 0;
    if (this->scene_fbo_ != 0) {
        glDeleteFramebuffers(1, &this->scene_fbo_);
        this->scene_fbo_ = 0;
    }
    if (this->scene_color_ != 0) {
        glDeleteTextures(1, &this->scene_color_);
        this->scene_color_ = 0;
    }
    if (this->scene_depth_ != 0) {
        glDeleteTextures(1, &this->scene_depth_);
        this->scene_depth_ = 0;
    }
    this->width_ = 0;
    this->height_ = 0;
}

bool PostChain::make_color_rt(int w, int h, Rt &out, std::string &err)
{
    out.w = std::max(1, w);
    out.h = std::max(1, h);
    glGenTextures(1, &out.texture);
    glBindTexture(GL_TEXTURE_2D, out.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA16F), out.w, out.h, 0, GL_RGBA, GL_FLOAT, nullptr);
    // **LINEAR にする。**縮小と拡大で双一次の中間を狙うのがこの鎖の要点である。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    // 端で巻き込むと、明るい左端のにじみが右端に出る。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &out.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, out.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.texture, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "ポスト処理の FBO が不完全です (0x%04X, %dx%d)", status, out.w, out.h);
        err = buf;
        return false;
    }
    return true;
}

bool PostChain::resize(int width, int height, std::string &err)
{
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    if ((w == this->width_) && (h == this->height_) && (this->scene_fbo_ != 0)) {
        return true;
    }

    /*
     * **入口でエラー行列を空にする。**
     *
     * この関数は最後に `drain_gl_errors()` で成否を決める。ところが `glGetError` の
     * 行列は**プロセスで 1 本**なので、空にしないまま入ると、**別の場所で出た古い
     * エラーがここの失敗として計上される**。そうなると FBO はどれも
     * `GL_FRAMEBUFFER_COMPLETE` なのに `release_targets()` まで走り、
     * 「画面の FBO を作れませんでした」で遊べなくなる。
     *
     * 2026-08-21 に実機で踏んだ。3D の地面が出ない状態で画面の作りを変えると
     * `GL_INVALID_OPERATION` が出たが、**エラーの出どころは描画の側**であって、
     * ここではなかった（エミュレータでは再現しない＝機体の GL 実装の差）。
     *
     * 捨てるのではなく**必ず記録する。**黙って飲むと出どころを追えなくなる。
     */
    if (const std::string stale = drain_gl_errors(); !stale.empty()) {
        std::fprintf(stderr,
            "[hd2d] post: 画面の FBO を作る前に GL エラーが残っていた（出どころは別の所）: %s\n",
            stale.c_str());
        std::fflush(stderr);
    }

    this->release_targets();

    // --- 画面（HDR ＋ 深度）---
    glGenTextures(1, &this->scene_color_);
    glBindTexture(GL_TEXTURE_2D, this->scene_color_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA16F), w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));

    glGenTextures(1, &this->scene_depth_);
    glBindTexture(GL_TEXTURE_2D, this->scene_depth_);
    // type は ES では効く（DEPTH_COMPONENT24 は GL_UNSIGNED_INT のみ合法。
    // GL_FLOAT だと FBO が不完全になる——shadow_map.cpp と同じ病。エミュレータで実測）。
#if defined(HENGBAND_GLES)
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), w, h, 0,
        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), w, h, 0,
        GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
#endif
    /*
     * **比較モードを立てない。**シャドウマップ（`shadow_map.cpp`）と違い、ここは
     * 生の深度そのものが要る。`GL_COMPARE_REF_TO_TEXTURE` のまま `texture()` を呼ぶと
     * 0/1 しか返らず、フォグも被写界深度も「掛かっているのに何も変わらない」になる。
     */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &this->scene_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, this->scene_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->scene_color_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, this->scene_depth_, 0);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "画面の FBO が不完全です (0x%04X, %dx%d)", status, w, h);
        err = buf;
        this->release_targets();
        return false;
    }

    // --- ブルームの段 ---
    int level_w = w / 2;
    int level_h = h / 2;
    for (int i = 0; i < kBloomLevels; ++i) {
        if ((level_w < kMinRtSide) || (level_h < kMinRtSide)) {
            break;
        }
        Rt rt;
        if (!this->make_color_rt(level_w, level_h, rt, err)) {
            this->release_rt(rt);
            this->release_targets();
            return false;
        }
        this->bloom_.push_back(rt);
        level_w /= 2;
        level_h /= 2;
    }

    /*
     * --- 被写界深度のピラミッド（P10 レビュー 9）---
     * 半分の大きさから 1/64 まで、**1 枚のテクスチャのミップの段**として持つ。
     * 段が別々だと合成が錯乱円の大きさで段の間（トライリニア）を引けない。
     * 段の大きさは `>> 1` で刻む（ミップ完全性の約束 floor(w/2) と同じ）。
     * 深い段ほどぼけが広い（段 i のテクセルはフル解像度の 2^(i+1) 画素）ので、
     * **lod を距離に比例させれば、ぼけの半径そのものが距離に比例する。**
     */
    {
        int level_ws[kDofMaxLevels]{};
        int level_hs[kDofMaxLevels]{};
        int levels = 0;
        int lw = std::max(1, w / 2);
        int lh = std::max(1, h / 2);
        while (levels < kDofMaxLevels) {
            level_ws[levels] = lw;
            level_hs[levels] = lh;
            ++levels;
            if ((lw / 2 < kMinRtSide) || (lh / 2 < kMinRtSide)) {
                break;
            }
            lw = std::max(1, lw >> 1);
            lh = std::max(1, lh >> 1);
        }
        this->dof_base_w_ = level_ws[0];
        this->dof_base_h_ = level_hs[0];

        glGenTextures(1, &this->dof_pyr_tex_);
        glBindTexture(GL_TEXTURE_2D, this->dof_pyr_tex_);
        for (int i = 0; i < levels; ++i) {
            glTexImage2D(GL_TEXTURE_2D, i, static_cast<GLint>(GL_RGBA16F), level_ws[i], level_hs[i], 0,
                GL_RGBA, GL_FLOAT, nullptr);
        }
        // **段の間を滑らかに引く**のが要点（LINEAR_MIPMAP_LINEAR）。段は自分たちで描く。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        //! これを段の数に合わせないと**ミップ不完全**でサンプルが黒になる。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
        glBindTexture(GL_TEXTURE_2D, 0);

        this->dof_pyr_fbos_.assign(static_cast<std::size_t>(levels), 0);
        for (int i = 0; i < levels; ++i) {
            glGenFramebuffers(1, &this->dof_pyr_fbos_[static_cast<std::size_t>(i)]);
            glBindFramebuffer(GL_FRAMEBUFFER, this->dof_pyr_fbos_[static_cast<std::size_t>(i)]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->dof_pyr_tex_, i);
            const GLenum pyr_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (pyr_status != GL_FRAMEBUFFER_COMPLETE) {
                char buf[128]{};
                std::snprintf(buf, sizeof(buf), "被写界深度の FBO が不完全です (0x%04X, 段 %d, %dx%d)",
                    pyr_status, i, level_ws[i], level_hs[i]);
                err = buf;
                this->release_targets();
                return false;
            }
            // 作業場（縮小とぼかしの中継）。段と同じ大きさ。
            Rt scratch;
            if (!this->make_color_rt(level_ws[i], level_hs[i], scratch, err)) {
                this->release_rt(scratch);
                this->release_targets();
                return false;
            }
            this->dof_scratch_.push_back(scratch);
        }
    }

    this->width_ = w;
    this->height_ = h;
    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "画面の FBO の用意で GL エラー: " + gl_err;
        this->release_targets();
        return false;
    }
    std::fprintf(stderr, "[hd2d] post: scene %dx%d (RGBA16F) / bloom %zu 段 / dof ピラミッド %dx%d から %zu 段\n",
        w, h, this->bloom_.size(), this->dof_base_w_, this->dof_base_h_, this->dof_pyr_fbos_.size());
    return true;
}

void PostChain::begin_scene(float clear_r, float clear_g, float clear_b)
{
    glBindFramebuffer(GL_FRAMEBUFFER, this->scene_fbo_);
    glViewport(0, 0, this->width_, this->height_);
    glClearColor(clear_r, clear_g, clear_b, 1.f);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostChain::end_scene(int viewport_w, int viewport_h, int viewport_x, int viewport_y, GLuint target_fbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(viewport_x, viewport_y, viewport_w, viewport_h);
}

void PostChain::draw_fullscreen()
{
    glBindVertexArray(this->vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostChain::resolve(const PostFlags &asked, const PostParams &params,
    float z_near, float z_far, float focus_distance, int screen_w, int screen_h,
    int screen_x, int screen_y, const Mat4 &view_projection, float focus_x, float focus_y,
    float forward_x, float forward_y, GLuint target_fbo)
{
    if (!this->ready()) {
        return;
    }
    // `HD2D_BREAK_POST` で落とされたものを引く（`init()` の注記）。ふだんは何も落ちない。
    PostFlags flags = asked;
    flags.fog = flags.fog && !this->broken_.fog;
    flags.dof = flags.dof && !this->broken_.dof;
    flags.bloom = flags.bloom && !this->broken_.bloom;
    flags.grade = flags.grade && !this->broken_.grade;
    flags.vignette = flags.vignette && !this->broken_.vignette;

    /*
     * 被写界深度の下ごしらえ（P10 レビュー 9）。**下ごしらえパスと合成パスが同じ値を
     * 使う**ので、ここでまとめて決める（式が食い違うと縁に段が出る）。
     * 強さ 0.01 以下は「切った」と同じ（機能メニューの 0）。
     */
    const bool dof_on = flags.dof && (this->dof_pyr_tex_ != 0) && !this->dof_pyr_fbos_.empty()
        && (params.dof_strength > 0.01f);
    const Mat4 inv_vp = inverse(view_projection);
    /*
     * **測りは `measure_dof()` 1 か所**（2026-08-23）。埃（`render/dust_motes.h`）も
     * 同じ錯乱円で自分を広げるので、式をここに埋め込んだままにすると 2 本になる。
     */
    const DofView dof_view = measure_dof(params, view_projection, focus_x, focus_y, forward_x, forward_y);
    const float dof_inner = dof_view.inner;
    const float dof_radius_px = dof_view.radius_px;
    const float forward_nx = dof_view.forward_x;
    const float forward_ny = dof_view.forward_y;
    /*
     * 錯乱円の分母 ＝ **画面に実際に見えている最遠**の前後の隔たり（レビュー 9:
     * 「画面表示される範囲ではどこまでも比例してぼかせないか」）。固定のマス数で割ると
     * 画面の途中で頭打ちになり、そこから奥が全部同じぼけになる。
     * 画面の縁（四隅と上下の中央）から地面 (z = 0) へレイを下ろして最大を取る。
     * 地面に届かないレイは far 面の点で代用する（見える幾何はそれより遠くに無い）。
     */
    const float dof_span = dof_view.span;
    /*
     * 全画面のパスは**深度を一切触らない**。`GL_DEPTH_TEST` を切ると深度への書き込みも
     * 起きない（規格どおり）が、後から読む人が迷わないように書き込みも明示的に止める。
     */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);

    // --- (1) ブルーム: 閾値抽出 → 縮小 → 拡大しながら加算 ---
    if (flags.bloom && !this->bloom_.empty()) {
        glBindFramebuffer(GL_FRAMEBUFFER, this->bloom_[0].fbo);
        glViewport(0, 0, this->bloom_[0].w, this->bloom_[0].h);
        glUseProgram(this->prog_bright_);
        glUniform1i(glGetUniformLocation(this->prog_bright_, "u_scene"), 0);
        glUniform1f(glGetUniformLocation(this->prog_bright_, "u_threshold"), params.bloom_threshold);
        glUniform1f(glGetUniformLocation(this->prog_bright_, "u_knee"), params.bloom_knee);
        glBindTexture(GL_TEXTURE_2D, this->scene_color_);
        this->draw_fullscreen();

        glUseProgram(this->prog_down_);
        glUniform1i(glGetUniformLocation(this->prog_down_, "u_source"), 0);
        const GLint down_texel = glGetUniformLocation(this->prog_down_, "u_source_texel");
        for (std::size_t i = 1; i < this->bloom_.size(); ++i) {
            const Rt &src = this->bloom_[i - 1];
            const Rt &dst = this->bloom_[i];
            glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
            glViewport(0, 0, dst.w, dst.h);
            glUniform2f(down_texel, 1.f / static_cast<float>(src.w), 1.f / static_cast<float>(src.h));
            glBindTexture(GL_TEXTURE_2D, src.texture);
            this->draw_fullscreen();
        }

        // 拡大して足す。**加算合成**（混ぜると細いにじみが広いにじみに埋もれる）。
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glUseProgram(this->prog_up_);
        glUniform1i(glGetUniformLocation(this->prog_up_, "u_source"), 0);
        const GLint up_texel = glGetUniformLocation(this->prog_up_, "u_source_texel");
        glUniform1f(glGetUniformLocation(this->prog_up_, "u_scale"), 1.f);
        for (std::size_t i = this->bloom_.size(); i-- > 1;) {
            const Rt &src = this->bloom_[i];
            const Rt &dst = this->bloom_[i - 1];
            glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
            glViewport(0, 0, dst.w, dst.h);
            glUniform2f(up_texel, 1.f / static_cast<float>(src.w), 1.f / static_cast<float>(src.h));
            glBindTexture(GL_TEXTURE_2D, src.texture);
            this->draw_fullscreen();
        }
        glDisable(GL_BLEND);
    }

    /*
     * --- (2) 被写界深度のピラミッド（P10 レビュー 9）---
     * 段 i ＝「段 i−1 の縮小 ＋ 軽いぼかし」。深い段ほどぼけが広く、合成は錯乱円の
     * 半径に合う段を lod で引く。**強さで往復数を変える作りは廃止**（強さは半径に効く）。
     *
     * 読む相手と書く先が同じテクスチャにならないように、縮小はピラミッド → 作業場、
     * ぼかしは作業場 → ピラミッドの順で振り分ける（フィードバックは未定義動作）。
     */
    if (dof_on) {
        // (2a) 下ごしらえ: 深度から錯乱円を測り、premultiplied（色×錯乱円, 錯乱円）で書く。
        glBindFramebuffer(GL_FRAMEBUFFER, this->dof_scratch_[0].fbo);
        glViewport(0, 0, this->dof_scratch_[0].w, this->dof_scratch_[0].h);
        glUseProgram(this->prog_dof_prepare_);
        glUniform1i(glGetUniformLocation(this->prog_dof_prepare_, "u_scene"), 0);
        glUniform1i(glGetUniformLocation(this->prog_dof_prepare_, "u_depth"), 1);
        glUniformMatrix4fv(glGetUniformLocation(this->prog_dof_prepare_, "u_inv_view_projection"),
            1, GL_FALSE, inv_vp.m);
        glUniform2f(glGetUniformLocation(this->prog_dof_prepare_, "u_focus_xy"), focus_x, focus_y);
        glUniform2f(glGetUniformLocation(this->prog_dof_prepare_, "u_focus_forward"), forward_nx, forward_ny);
        glUniform1f(glGetUniformLocation(this->prog_dof_prepare_, "u_dof_inner"), dof_inner);
        glUniform1f(glGetUniformLocation(this->prog_dof_prepare_, "u_dof_span"), dof_span);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, this->scene_color_);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, this->scene_depth_);
        this->draw_fullscreen();
        glActiveTexture(GL_TEXTURE0);

        // (2b) 段を作る（縮小 → 3×3。ぼかしはトライリニアの双一次格子を消すため）。
        const int levels = static_cast<int>(this->dof_pyr_fbos_.size());
        for (int i = 0; i < levels; ++i) {
            const int lw = std::max(1, this->dof_base_w_ >> i);
            const int lh = std::max(1, this->dof_base_h_ >> i);
            if (i > 0) {
                // 縮小: ピラミッドの段 i−1 → 作業場 i。読む段は `u_lod` で明示する。
                glBindFramebuffer(GL_FRAMEBUFFER, this->dof_scratch_[static_cast<std::size_t>(i)].fbo);
                glViewport(0, 0, lw, lh);
                glUseProgram(this->prog_dof_down_);
                glUniform1i(glGetUniformLocation(this->prog_dof_down_, "u_source"), 0);
                glUniform1f(glGetUniformLocation(this->prog_dof_down_, "u_lod"), static_cast<float>(i - 1));
                glUniform2f(glGetUniformLocation(this->prog_dof_down_, "u_source_texel"),
                    1.f / static_cast<float>(std::max(1, this->dof_base_w_ >> (i - 1))),
                    1.f / static_cast<float>(std::max(1, this->dof_base_h_ >> (i - 1))));
                glBindTexture(GL_TEXTURE_2D, this->dof_pyr_tex_);
                this->draw_fullscreen();
            }
            // ぼかし: 作業場 i → ピラミッドの段 i。
            glBindFramebuffer(GL_FRAMEBUFFER, this->dof_pyr_fbos_[static_cast<std::size_t>(i)]);
            glViewport(0, 0, lw, lh);
            glUseProgram(this->prog_blur_);
            glUniform1i(glGetUniformLocation(this->prog_blur_, "u_source"), 0);
            glUniform1f(glGetUniformLocation(this->prog_blur_, "u_blur_radius"), 1.1f);
            glUniform2f(glGetUniformLocation(this->prog_blur_, "u_source_texel"),
                1.f / static_cast<float>(this->dof_scratch_[static_cast<std::size_t>(i)].w),
                1.f / static_cast<float>(this->dof_scratch_[static_cast<std::size_t>(i)].h));
            glBindTexture(GL_TEXTURE_2D, this->dof_scratch_[static_cast<std::size_t>(i)].texture);
            this->draw_fullscreen();
        }
    }

    /*
     * --- (3) 合成 → 出し先のフレームバッファ（既定は窓・VR では目の swapchain） ---
     * **ここだけユニフォームの場所を毎回引いている**（他は `locate()` で 1 回だけ引く作り）。
     * 1 フレームに 1 回・25 個で、パスそのもの（0.2ms 台）に対して桁が 2 つ小さい。
     * 25 個の `GLint` をクラスに並べるより、どの値が何に効くかが 1 か所で読めるほうを採った。
     */
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(screen_x, screen_y, screen_w, screen_h);
    glUseProgram(this->prog_composite_);
    const GLuint p = this->prog_composite_;
    glUniform1i(glGetUniformLocation(p, "u_scene"), 0);
    glUniform1i(glGetUniformLocation(p, "u_depth"), 1);
    glUniform1i(glGetUniformLocation(p, "u_bloom"), 2);
    glUniform1i(glGetUniformLocation(p, "u_dof_pyr"), 3);
    glUniform1i(glGetUniformLocation(p, "u_lut"), 4);
    glUniform1f(glGetUniformLocation(p, "u_z_near"), z_near);
    glUniform1f(glGetUniformLocation(p, "u_z_far"), z_far);
    /*
     * **ブルームと被写界深度は「切った」ではなく「用意しなかった」でも切る。**
     * 段が作れなかった（窓が極端に小さい）ときに古い中身を読むと、
     * 「たまに前のフレームの光がにじむ」という追いにくい壊れ方をする。
     * 被写界深度の `dof_on` は関数の頭で決めてある（下ごしらえパスと同じ条件）。
     */
    const bool bloom_on = flags.bloom && !this->bloom_.empty();
    glUniform1i(glGetUniformLocation(p, "u_use_fog"), flags.fog ? 1 : 0);
    glUniform1i(glGetUniformLocation(p, "u_use_dof"), dof_on ? 1 : 0);
    glUniform1i(glGetUniformLocation(p, "u_use_bloom"), bloom_on ? 1 : 0);
    glUniform1i(glGetUniformLocation(p, "u_use_grade"), (flags.grade && (this->lut_ != 0)) ? 1 : 0);
    glUniform1i(glGetUniformLocation(p, "u_use_vignette"), flags.vignette ? 1 : 0);
    glUniform1f(glGetUniformLocation(p, "u_focus"), focus_distance);
    /*
     * 被写界深度の値は**下ごしらえパスと同じもの**を渡す（関数の頭で決めた）。
     * ここで別の値を作ると、写しに入っている量と引く量が食い違って縁に段が出る。
     */
    glUniformMatrix4fv(glGetUniformLocation(p, "u_inv_view_projection"), 1, GL_FALSE, inv_vp.m);
    glUniform2f(glGetUniformLocation(p, "u_focus_xy"), focus_x, focus_y);
    glUniform2f(glGetUniformLocation(p, "u_focus_forward"), forward_nx, forward_ny);
    glUniform1f(glGetUniformLocation(p, "u_dof_inner"), dof_inner);
    glUniform1f(glGetUniformLocation(p, "u_dof_span"), dof_span);
    glUniform1f(glGetUniformLocation(p, "u_dof_radius_px"), dof_radius_px);
    glUniform1f(glGetUniformLocation(p, "u_dof_lod_max"),
        static_cast<float>(std::max<std::size_t>(this->dof_pyr_fbos_.size(), 1) - 1));
    glUniform3f(glGetUniformLocation(p, "u_fog_color"), params.fog_color.x, params.fog_color.y, params.fog_color.z);
    glUniform1f(glGetUniformLocation(p, "u_fog_start"), params.fog_start);
    glUniform1f(glGetUniformLocation(p, "u_fog_range"), params.fog_range);
    glUniform1f(glGetUniformLocation(p, "u_fog_density"), params.fog_density);
    glUniform1f(glGetUniformLocation(p, "u_bloom_intensity"), params.bloom_intensity);
    glUniform1f(glGetUniformLocation(p, "u_exposure"), params.exposure);
    glUniform1f(glGetUniformLocation(p, "u_hdr"), params.hdr);
    glUniform1f(glGetUniformLocation(p, "u_vignette_strength"), params.vignette_strength);
    glUniform1f(glGetUniformLocation(p, "u_vignette_radius"), params.vignette_radius);
    glUniform1f(glGetUniformLocation(p, "u_lut_size"), static_cast<float>(std::max(2, this->lut_size_)));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->scene_color_);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, this->scene_depth_);
    /*
     * 使わない口にも**中身のあるテクスチャを結ぶ**（読まれないので何でもよい）。
     * 0 を結ぶと「未完成のテクスチャを結んでいる」とドライバが毎フレーム警告を出し、
     * 本当に見たい警告がその中に埋もれる。
     */
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, bloom_on ? this->bloom_[0].texture : this->scene_color_);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, dof_on ? this->dof_pyr_tex_ : this->scene_color_);
    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_3D, this->lut_);
    this->draw_fullscreen();

    glActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDepthMask(GL_TRUE);
}

bool PostChain::upload_lut(int size, const std::vector<unsigned char> &rgba, std::string &err)
{
    if (this->lut_ == 0) {
        glGenTextures(1, &this->lut_);
    }
    glBindTexture(GL_TEXTURE_3D, this->lut_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, static_cast<GLint>(GL_RGBA8), size, size, size, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    // **R を忘れない。**3 つ目の軸は S/T とは別に指定しなければならない。
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_3D, 0);
    this->lut_size_ = size;

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "LUT の転送で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

bool PostChain::set_grade(const GradeParams &grade, std::string &err)
{
    std::vector<unsigned char> rgba;
    bake_lut(grade, kLutSize, rgba);
    if (!this->upload_lut(kLutSize, rgba, err)) {
        return false;
    }
    this->lut_source_ = "GradeParams（焼いたもの）";
    return true;
}

bool PostChain::load_lut_cube(const std::string &path, std::string &err)
{
    int size = 0;
    std::vector<unsigned char> rgba;
    if (!read_lut_cube(path, size, rgba, err)) {
        return false;
    }
    if (!this->upload_lut(size, rgba, err)) {
        return false;
    }
    this->lut_source_ = path;
    return true;
}

} // namespace hd2d
