/*!
 * @file voxel_renderer.cpp
 * @brief `voxel_renderer.h` の実装。
 */
#include "render/voxel_renderer.h"

#include "render/gl_program.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace hd2d {

using namespace hd2d::gl;

namespace {

/*
 * **本描画も影のパスも、この 1 本しか使わない。**
 *
 * 設計書 §8.2 は「落とすと必ず破綻するもの」の筆頭に
 * 「**影のパスにも同じ変形を適用する。**忘れると『揺れているのに影が揺れない』」を挙げている。
 * 写した 2 本を並べて持つと、片方だけ直す事故がいつか必ず起きる（症状は「なんとなく
 * ずれている」なので原因に辿り着くのも遅い）。**構造的に起こせなくする**のが安い。
 *
 * したがって風（P6 ②）を足した先もここ 1 か所だけである。
 */
const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec3 a_pos;      // パーツのローカル（ボクセル単位）
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;       // 面アトラスの「テクセル座標」（正規化しない）
layout(location = 3) in vec3 a_instance_offset;  // マス単位の置き場所
layout(location = 4) in vec3 a_instance_scale;   // プレハブ空間での拡大率
layout(location = 5) in vec3 a_instance_tint;    // パレット色に掛ける
layout(location = 6) in float a_flex;    // 風のしなやかさ（0 = 根元 / 1 = 先端）
layout(location = 7) in vec3 a_instance_emissive; // 自発光（線形の HDR。光を受けずに足す）
layout(location = 8) in float a_instance_yaw;    // z 軸まわりの向き（0 = 南向き）
/*
 * 四角形の縁までの距離（テクセル。`VoxelVertex::e0` の注記）。**画調が TRON のときだけ**
 * フラグメントが読む。標準では使われないので、ドライバが最適化で落とす。
 */
layout(location = 9) in vec4 a_edge;
uniform mat4 u_view_projection;
uniform mat4 u_model;          // 動き（剛体）× 静的な配置。CPU 側で合成済み
uniform vec3 u_wind_vector;    // xy = 風向 × 振幅（マス）, z = 未使用
uniform vec2 u_wind_wave;      // x = 時間の周期(rad/s), y = 世界座標の波数(rad/マス)
uniform float u_time;          // 秒
uniform float u_part_wind_k;   // パーツごとの係数（0 なら曲がらない）
uniform int u_wind_phase;      // 0=波（設計書） 1=同期 2=ばらばら
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_tint;
out vec3 v_world;
out vec3 v_emissive;
out vec4 v_edge;
void main()
{
    vec4 local = u_model * vec4(a_pos, 1.0);
    /*
     * インスタンスごとの向き（一人称のときだけ 0 でない）。**1 マスの真ん中**まわりに回す。
     * 0 のときは sin/cos が 0/1 で恒等になるので、地形は 1 命令ぶんしか払わない。
     */
    vec3 spun = local.xyz;
    if (a_instance_yaw != 0.0) {
        float cs = cos(a_instance_yaw);
        float sn = sin(a_instance_yaw);
        vec2 rel = local.xy - vec2(0.5, 0.5);
        spun.xy = vec2((rel.x * cs) - (rel.y * sn), (rel.x * sn) + (rel.y * cs)) + vec2(0.5, 0.5);
    }
    vec3 world = (spun * a_instance_scale) + a_instance_offset;

    /*
     * 風（設計書 §8.1）。**位相を世界座標から作る**ので、隣の草と揃って波として渡る。
     * インスタンスごとに何も渡さなくてよいので、木 8,409 本が 1 回の描画で
     * それぞれ違う位相で揺れる。
     *
     * 曲げるのは草・葉・布だけ（§8.2）。`u_part_wind_k` が 0 のパーツは 1 命令で抜ける。
     */
    float bend = a_flex * u_part_wind_k;
    if (bend > 0.0) {
        float offset = 0.0;
        if (u_wind_phase == 0) {
            // 波（設計書 §8.1）。**曲げる前の位置**で引く（曲げた後だと自分の揺れが位相に戻る）。
            offset = dot(world.xy, vec2(u_wind_wave.y));
        } else if (u_wind_phase == 2) {
            // ばらばら（比較用）。インスタンスの置き場所から擬似乱数を引く。
            offset = fract(sin(dot(a_instance_offset.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
        }
        // u_wind_phase == 1（同期）は offset = 0。一面が同じ拍で動く。
        float phase = (u_time * u_wind_wave.x) + offset;
        // 主の揺れに別の周期を重ねる。単純な sin だけだと機械の往復に見える。
        float sway = sin(phase) + (0.35 * sin((phase * 2.17) + 1.3));
        world.xy += u_wind_vector.xy * (sway * bend);
    }

    gl_Position = u_view_projection * vec4(world, 1.0);
    // u_model は一様な縮小しか含まないが、インスタンスの拡大率は軸ごとに違いうる。
    // 軸に沿った拡大の法線は「拡大率で割る」（逆転置行列の対角成分）。
    vec3 spun_normal = mat3(u_model) * a_normal;
    if (a_instance_yaw != 0.0) {
        //! **法線も同じだけ回す。**忘れると、回した板だけ光の当たり方が元の向きのまま残る。
        float cs = cos(a_instance_yaw);
        float sn = sin(a_instance_yaw);
        spun_normal.xy = vec2((spun_normal.x * cs) - (spun_normal.y * sn),
            (spun_normal.x * sn) + (spun_normal.y * cs));
    }
    v_normal = spun_normal / a_instance_scale;
    v_uv = a_uv;
    v_tint = a_instance_tint;
    v_world = world;
    v_emissive = a_instance_emissive;
    v_edge = a_edge;
}
)";

/*!
 * 影のパスは深度しか書かない（色の添付そのものが無い）。
 * **頂点シェーダは `kVertexSource` をそのまま使う**ので、ここには何も書かない。
 * 頂点シェーダの `out` を受け取らなくてよいのは GLSL の仕様どおり。
 */
const char *const kDepthFragmentSource = R"(#version 460 core
void main()
{
}
)";

/*
 * 光の式は `render/lighting.h` の `kLightingGlsl` に一本化してある
 * （ビルボードと違う式にすると、同じ場所に立つ壁と人物の明るさが食い違う）。
 * アトラスは整数テクスチャなので `texelFetch` で 1 面 1 テクセルを厳密に引く。
 */
const char *const kFragmentBody = R"(
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_tint;
in vec3 v_world;
in vec3 v_emissive;
in vec4 v_edge;   // 四角形の 4 辺までの距離（TRON の稜線）
uniform usampler2D u_atlas;   // R = パレット索引, G = AO
uniform sampler2D u_palette;  // 256x2。0 行目 = 色, 1 行目の R = 材質（surface_wear.h）
out vec4 o_color;

/*
 * カットアウェイ（P7・設計書 §13「深度比較＋ディザ抜き」）。**必須**である。
 * 町の入口の 35%（44 箇所）は北を向いており、通りの南から見下ろすカメラでは
 * 建物自身がその入口を必ず隠す（§10.2）。構造的なものなので、絵で解くしかない。
 *
 * | 何を渡すか | |
 * |---|---|
 * | `u_cutaway_centre` | プレイヤを投影した画素（画面座標・左上原点） |
 * | `u_cutaway_depth`  | **プレイヤの位置を投影した窓深度**。深度バッファから読むのではない |
 * | `u_cutaway_radius` | 抜く半径（画素）。0 で無効 |
 * | `u_cutaway_scale`  | この描画に掛けるか（床の板は 0） |
 *
 * **深度バッファから読んではいけない。**プレイヤは隠れているので、そこに書かれている
 * のは「隠している壁の深度」である。それを基準にすると壁は自分より手前に無く、
 * 何も抜けない（判定が常に空振りする）。
 */
/*
 * **抜く所は 1 つとは限らない**（P10 第 4 期。2026-08-10 に気づいた:
 * 「視界の範囲内にあるモンスター、アイテムが壁ブロック等の 1 マス北にある場合
 * （隠れて見えない場合）は透過して見えるように」）。
 *
 * プレイヤのぶんに加えて、**隠れている実体 1 体につき 1 つ**穴を開ける。
 * 残す量（`keep`）は**いちばん小さいものを採る**——どれか 1 つが抜けと言えば抜ける。
 */
const int HD2D_MAX_CUTAWAYS = 12;
uniform vec2 u_cutaway_centre[HD2D_MAX_CUTAWAYS];
uniform float u_cutaway_radius[HD2D_MAX_CUTAWAYS];
/*!
 * @brief **カメラ側の半平面**。`.xy` = 法線（対象 → カメラの水平向き）・`.z` = 境。
 *
 * `dot(world.xy, face.xy) <= face.z` の欠片は抜かない＝**対象より奥のものは抜かない**。
 * 初版は「南の境（world の y）」1 本だったが、見下ろしを 90° 回せるようにしたら
 * 回した先で透過が起きなくなった（`Cutaway` の注記）。法線をカメラの方位から作れば
 * どの向きでも「カメラと対象の間」を正しく指す。既定 (0, 1, −1e9) は判定を掛けない。
 */
uniform vec3 u_cutaway_face[HD2D_MAX_CUTAWAYS];
/*!
 * @brief **高さの境**（world の z。マス単位。これ以下の欠片は抜かない）。
 *
 * 屋根だけを外すための軸である（2026-08-18 に決めた:「屋根自体を透過させキャラの周囲を
 * 一定範囲高さ 1 ブロックの構造物を残し屋根（建造物）を透過させる」）。
 * 1.0 を渡すと**高さ 1 マスより上だけ**が抜けるので、床と腰までの壁が残って屋根が消える。
 * 既定の −1e9 は「高さの判定を掛けない」（従来の抜きそのもの）。`u_cutaway_face` の境と対称。
 */
uniform float u_cutaway_min_z[HD2D_MAX_CUTAWAYS];
uniform float u_cutaway_depth[HD2D_MAX_CUTAWAYS];
uniform int u_cutaway_count;
uniform float u_cutaway_scale;
/*!
 * 抜きの強さ（P10 レビュー 4。「透過率はもっと上げた方が見やすい」と決めた）。
 * `keep` を累乗する。1 で従来、大きいほど**同じ半径でも網目が粗くなる＝よく抜ける**。
 * 半径を広げるのとは別の軸で、**縁の当たりを保ったまま中身だけ薄く**できる。
 */
uniform float u_cutaway_power[HD2D_MAX_CUTAWAYS];

//! 4x4 の順序ディザ。**滑らかに消さずに網目で抜く**のがドット絵の作法（設計書 §13）。
float hd2d_bayer4(vec2 pixel)
{
    const mat4 m = mat4(
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0);
    ivec2 p = ivec2(mod(pixel, 4.0));
    return (m[p.x][p.y] + 0.5) / 16.0;
}

void main()
{
    /*
     * カットアウェイ。**手前にある・近くにある**の 2 つが揃ったときだけ抜く。
     * 縁は円錐状に薄くして、抜けた穴が円形に切り抜かれて見えないようにする。
     */
    if ((u_cutaway_scale > 0.0) && (u_cutaway_count > 0)) {
        float keep = 1.0;
        for (int i = 0; i < HD2D_MAX_CUTAWAYS; ++i) {
            if (i >= u_cutaway_count) {
                break;
            }
            //! **手前にある**ものだけ抜く（隠している側だけ薄くする）。
            if ((u_cutaway_radius[i] <= 0.0)
                || (gl_FragCoord.z >= (u_cutaway_depth[i] - 0.00002))) {
                continue;
            }
            /*
             * **カメラと対象の間にある欠片だけ**抜く（2026-08-11 に決めた「真横の
             * オブジェクトを透過しない」＋ 2026-08-19「透過はカメラとキャラの間に障害が
             * ある場合として変更しよう」）。真横の壁の面はちょうど境に乗るので、
             * 境そのものは「間」に数えない（<=）。
             */
            if (dot(v_world.xy, u_cutaway_face[i].xy) <= u_cutaway_face[i].z) {
                continue;
            }
            /*
             * **低い所は残す**（屋根外しの層。2026-08-18 に決めた）。
             * `v_world.z` は**高さをマスで数えた値**（頂点シェーダの `world`）。
             * 既定は −1e9 なので、従来の穴はここを素通りする。
             */
            if (v_world.z <= u_cutaway_min_z[i]) {
                continue;
            }
            float d = length(gl_FragCoord.xy - u_cutaway_centre[i]) / u_cutaway_radius[i];
            /*
             * 中心 = 0（全部抜く）→ 外周 = 1（残す）。**内側の立ち上がりを 0.15 から取る。**
             * 0.55 にすると、半径の半分までが完全に抜けたあと縁だけが網目になり、
             * **黒い円が空いたように見える**（P7 で 1 度そう撮った）。早くから薄く始めれば、
             * 抜けた所と残った所の境目が読めなくなる。
             */
            float k = smoothstep(0.15, 1.0, d);
            /*
             * **抜きの強さ。**累乗で押し下げる（1 で従来。大きいほどよく抜ける）。
             *
             * **穴ごとに持つ**（2026-08-18 に決めた:「抜きの強さは穴ごとに強さを
             * 持たせてよい。変愚では二重で透過を持つことはないはずだから、幻想独自の
             * ルールとして追加すればいい」）。層 1 と層 2 で強さを分けるために要る。
             * 変愚は全部の穴が同じ値（2.2）なので、**絵は 1 画素も変わらない**。
             */
            k = pow(k, max(u_cutaway_power[i], 0.05));
            keep = min(keep, k);
        }
        keep = mix(1.0, keep, u_cutaway_scale);
        if (hd2d_bayer4(gl_FragCoord.xy) >= keep) {
            discard;
        }
    }

    ivec2 texel = ivec2(floor(v_uv));
    texel = clamp(texel, ivec2(0), textureSize(u_atlas, 0) - ivec2(1));
    uvec2 data = texelFetch(u_atlas, texel, 0).rg;
    if (data.r == 0u) {
        discard;   // 索引 0 = 空。ここへ来るのは詰め方を間違えたときだけ
    }
    vec3 base = texelFetch(u_palette, ivec2(int(data.r), 0), 0).rgb * v_tint;
    float ao = float(data.g) / 255.0;

    vec3 n = normalize(v_normal);

    /*
     * **面の汚し**（`render/surface_wear.h`）。ボクセルの凹凸より細かいディテールを
     * 色で足す（2026-08-23 に決めた）。材質は**パレットの 1 行目**に詰めてある
     * （`upload_meshed`）。`u_wear_amount` が 0 なら中で早く抜けるので、
     * 切ってあるときは従来と 1 ビットも変わらない。
     *
     * **光を掛ける前に**掛ける——汚れは面の色であって明るさではない。後ろでやると
     * 日向の汚れだけが濃くなり、同じ壁が場所ごとに違う材に見える。
     */
    int material = int((texelFetch(u_palette, ivec2(int(data.r), 1), 0).r * 255.0) + 0.5);
    base = hd2d_wear_apply(base, ao, v_world, v_uv, n, material);
    /*
     * **木の葉**（`render/leaf_detail.h`。2026-08-23 に決めた）。汚しの後に描く
     * ——葉の形の上へ汚しを掛けると、描いた葉脈が均されて消える。
     * 材質が `Leaf` でなければ 1 命令で戻る。
     */
    base = hd2d_leaf_apply(base, v_uv, material);

    float ndl = max(dot(n, u_light_dir), 0.0);
    float shadow = hd2d_key_shadow(v_world, ndl);

    /*
     * **AO は環境光と点光源にだけ掛ける。**主光にも掛けると、日向の面まで
     * 隅が暗くなって「焼き込みすぎた絵」になる（焼いた AO は遮蔽の近似であって影ではない）。
     */
    vec3 direct = u_key_color * ndl * shadow;
    vec3 indirect = (hd2d_ambient(n) + hd2d_point_lights(v_world, n, 0.10)) * ao;

    /*
     * **画調**。標準では `u_look_enabled` が 0 で
     * ここを素通りする＝**従来と 1 ビットも変わらない**。
     *
     * 線は 2 本ある。マスの格子（世界座標の `fract`）と物の稜線（焼いた四角形の縁）。
     * 格子は控えめ（`u_look_grid_gain`）にして、形の線を前へ出す。
     */
    if (u_look_enabled != 0) {
        /*
         * 線は 2 本（マスの格子・物の稜線）。**それぞれ芯とにじみの裾を持つ**
         * （`hd2d_look_profile`）。格子は控えめ（`u_look_grid_gain`）にして形の線を前へ出す。
         */
        float line = max(hd2d_look_grid(v_world, n) * u_look_grid_gain, hd2d_look_edge(v_edge));
        vec3 lit = direct + indirect;
        vec3 tron = hd2d_look_apply(base, lit, v_emissive, line);
        //! 面の真ん中に浮かぶマスの記号（最初に決めたこと。§18.2-4）。**足すだけ**なので線を消さない。
        tron += hd2d_look_face_glyph(v_world, n, lit);
        o_color = vec4(tron, 1.0);
        return;
    }

    /*
     * **線形の HDR をそのまま書く。**トーンマップは合成の 1 か所（P7。`lighting.cpp` の注記）。
     * 自発光は AO も影も掛けずに足す（自分で光っているものに遮蔽は関係ない）。
     */
    o_color = vec4((base * (direct + indirect)) + v_emissive, 1.0);
}
)";

} // namespace

bool VoxelRenderer::init(std::string &err)
{
    // 光の式は 1 か所（`kLightingGlsl`）にしかない。ここは連結するだけ。
    const std::string fragment = std::string("#version 460 core\n") + kLightingGlsl + kSceneLookGlsl
        + kSurfaceWearGlsl + kLeafDetailGlsl + kFragmentBody;
    this->program_ = compile_program(kVertexSource, fragment.c_str(), err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_view_projection_ = glGetUniformLocation(this->program_, "u_view_projection");
    this->loc_model_ = glGetUniformLocation(this->program_, "u_model");
    this->loc_atlas_ = glGetUniformLocation(this->program_, "u_atlas");
    this->loc_palette_ = glGetUniformLocation(this->program_, "u_palette");
    this->loc_wind_vector_ = glGetUniformLocation(this->program_, "u_wind_vector");
    this->loc_wind_wave_ = glGetUniformLocation(this->program_, "u_wind_wave");
    this->loc_time_ = glGetUniformLocation(this->program_, "u_time");
    this->loc_part_wind_k_ = glGetUniformLocation(this->program_, "u_part_wind_k");
    this->loc_wind_phase_ = glGetUniformLocation(this->program_, "u_wind_phase");
    this->loc_cutaway_centre_ = glGetUniformLocation(this->program_, "u_cutaway_centre");
    this->loc_cutaway_radius_ = glGetUniformLocation(this->program_, "u_cutaway_radius");
    this->loc_cutaway_face_ = glGetUniformLocation(this->program_, "u_cutaway_face");
    this->loc_cutaway_min_z_ = glGetUniformLocation(this->program_, "u_cutaway_min_z");
    this->loc_cutaway_depth_ = glGetUniformLocation(this->program_, "u_cutaway_depth");
    this->loc_cutaway_count_ = glGetUniformLocation(this->program_, "u_cutaway_count");
    this->loc_cutaway_scale_ = glGetUniformLocation(this->program_, "u_cutaway_scale");
    this->loc_cutaway_power_ = glGetUniformLocation(this->program_, "u_cutaway_power");
    this->light_loc_.locate(this->program_);
    this->look_loc_.locate(this->program_);
    this->wear_loc_.locate(this->program_);
    this->leaf_loc_.locate(this->program_);

    /*
     * 影のパスのプログラム。**頂点シェーダは上とまったく同じ文字列**である（§8.2）。
     * 違うのは「色を書かないフラグメントシェーダ」だけ。
     */
    this->depth_program_ = compile_program(kVertexSource, kDepthFragmentSource, err);
    if (this->depth_program_ == 0) {
        return false;
    }
    this->depth_loc_view_projection_ = glGetUniformLocation(this->depth_program_, "u_view_projection");
    this->depth_loc_model_ = glGetUniformLocation(this->depth_program_, "u_model");
    this->depth_loc_wind_vector_ = glGetUniformLocation(this->depth_program_, "u_wind_vector");
    this->depth_loc_wind_wave_ = glGetUniformLocation(this->depth_program_, "u_wind_wave");
    this->depth_loc_time_ = glGetUniformLocation(this->depth_program_, "u_time");
    this->depth_loc_part_wind_k_ = glGetUniformLocation(this->depth_program_, "u_part_wind_k");
    this->depth_loc_wind_phase_ = glGetUniformLocation(this->depth_program_, "u_wind_phase");
    return true;
}

void VoxelRenderer::shutdown()
{
    if (this->depth_program_ != 0) {
        glDeleteProgram(this->depth_program_);
        this->depth_program_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
}

bool VoxelRenderer::build_mesh(const Prefab &prefab, PrefabMesh &out, std::string &err, bool verbose)
{
    out = PrefabMesh{};
    for (std::size_t part_index = 0; part_index < prefab.parts.size(); ++part_index) {
        const PrefabPart &part = prefab.parts[part_index];
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        VoxelMesh mesh;
        if (!mesh_model(model, mesh, err, prefab.hide_unseen_from_above)) {
            err = "パーツ \"" + part.name + "\" をメッシュにできませんでした: " + err;
            return false;
        }
        out.quad_count += mesh.quad_count;
        out.naive_face_count += mesh.naive_face_count;
        out.triangle_count += mesh.triangle_count();
        out.atlas_side = (mesh.atlas_w > out.atlas_side) ? mesh.atlas_w : out.atlas_side;
        /*
         * 1 パーツ 1 行の内訳。**ライブラリの一括では切る**（`PrefabLibrary::upload_gpu`）。
         * 2,615 行あり、Android では stderr がパイプ経由で logcat へ流れるので高い。
         * `--prefab=` / `--prefab-check` の 1 個だけの道は既定どおり出す。
         */
        if (verbose) {
            std::fprintf(stderr,
                "[hd2d] mesh \"%s/%s\": %dx%dx%d voxels -> %zu quads (%zu triangles), atlas %dx%d, naive faces %zu\n",
                prefab.name.c_str(), part.name.c_str(), model.size[0], model.size[1], model.size[2],
                mesh.quad_count, mesh.triangle_count(), mesh.atlas_w, mesh.atlas_h, mesh.naive_face_count);
        }
        if (mesh.indices.empty()) {
            continue; // 中身が空のパーツ
        }

        PrefabMesh::Part built;
        /*
         * **世界の単位はマス**（設計書 §5「1 マス = 32×32×32」）。パーツのローカルは
         * ボクセル単位なので、ここで 1/`voxels_per_cell` に縮める。プレハブ空間も
         * マス単位になるので、地形の置き場所（マス座標）とそのまま足し合わせられる。
         */
        const float scale = 1.f / static_cast<float>(prefab.voxels_per_cell);
        built.model = translation(Vec3{ part.offset[0] * scale, part.offset[1] * scale, part.offset[2] * scale })
            * scaling(scale);
        // **元の添字を持つ。**空のパーツを飛ばすので、並び順で引くと静かに 1 つずれる（P6）。
        built.prefab_part_index = static_cast<int>(part_index);
        // 風で曲げるのは `motion.kind == Wind` のパーツだけ（§8.2「曲げるのは草・葉・布だけ」）。
        built.wind_k = (part.motion.kind == MotionKind::Wind) ? part.wind_k : 0.f;
        built.mesh = std::move(mesh);
        out.parts.push_back(std::move(built));
    }
    return true;
}

bool VoxelRenderer::upload_meshed(const Prefab &prefab, const PrefabMesh &meshed, GpuPrefab &out, std::string &err)
{
    out.quad_count += meshed.quad_count;
    out.naive_face_count += meshed.naive_face_count;
    out.triangle_count += meshed.triangle_count;
    out.atlas_side = (meshed.atlas_side > out.atlas_side) ? meshed.atlas_side : out.atlas_side;

    /*
     * --- パレット（256×2。索引 0 は空なので黒のまま） ---
     *
     * **0 行目 = 色・1 行目 = 材質**（`render/surface_wear.h`）。材質は面の汚しが
     * 「木か鉄か布か」で掛け方を変えるために要る。`.vox` は色しか持っていないので、
     * **色から引く**（表は `material_table.inc`。焼くのは `tools/voxel/gen_material_table.py`）。
     *
     * 別のテクスチャにしないのは、テクスチャ単位が既に 0〜4 まで埋まっているためである
     * （面アトラス・パレット・影・TRON の字 2 枚）。1 行足すほうが安い。
     */
    std::array<std::uint8_t, 256 * 2 * 4> palette_rows{};
    std::memcpy(palette_rows.data(), prefab.vox.palette, 256 * 4);
    for (int i = 0; i < 256; ++i) {
        /*
         * **材質はプレハブが宣言している**（`.jsonc` の `palette`。2026-08-23）。
         * 色から当てるのは**宣言の無いものだけ**——手で作った `.vox` と、
         * C++ で組んだ箱（`make_box_prefab`）である。
         */
        std::uint8_t mat = prefab.palette_material[i];
        if (!prefab.palette_material_declared) {
            const std::uint8_t *rgb = prefab.vox.palette[i];
            mat = static_cast<std::uint8_t>(material_for_color(rgb[0], rgb[1], rgb[2]));
        }
        std::uint8_t *row = palette_rows.data() + ((256 + static_cast<std::size_t>(i)) * 4);
        row[0] = mat;
        row[3] = 255;
    }
    glGenTextures(1, &out.palette);
    glBindTexture(GL_TEXTURE_2D, out.palette);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 256, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
        palette_rows.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    glBindTexture(GL_TEXTURE_2D, 0);

    for (const PrefabMesh::Part &built : meshed.parts) {
        const VoxelMesh &mesh = built.mesh;
        GpuPart gpu;
        gpu.model = built.model;
        gpu.index_count = static_cast<GLsizei>(mesh.indices.size());
        gpu.prefab_part_index = built.prefab_part_index;
        gpu.wind_k = built.wind_k;

        glGenTextures(1, &gpu.atlas);
        glBindTexture(GL_TEXTURE_2D, gpu.atlas);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RG8UI), mesh.atlas_w, mesh.atlas_h, 0,
            GL_RG_INTEGER, GL_UNSIGNED_BYTE, mesh.atlas.data());
        // 整数テクスチャは NEAREST 以外を受け付けない（線形補間に意味が無いため）。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenVertexArrays(1, &gpu.vao);
        glGenBuffers(1, &gpu.vbo);
        glGenBuffers(1, &gpu.ebo);
        glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(VoxelVertex)),
            mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
            mesh.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<const void *>(offsetof(VoxelVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<const void *>(offsetof(VoxelVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<const void *>(offsetof(VoxelVertex, u)));
        // 風のしなやかさ（P6 ②）。メッシュ化のときに焼いてある（`VoxelVertex::flex`）。
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<const void *>(offsetof(VoxelVertex, flex)));
        /*
         * 四角形の縁までの距離（TRON の稜線。`VoxelVertex::e0`）。**画調が標準でも常に流す。**
         * 流すかどうかを画調で切り替えると VAO を組み直すことになり、
         * 「設定を変えた最初の 1 フレームだけ線が出ない」の類を必ず生む。
         */
        glEnableVertexAttribArray(9);
        glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, sizeof(VoxelVertex), reinterpret_cast<const void *>(offsetof(VoxelVertex, e0)));

        // インスタンスの置き場所と色（中身は描くときに詰める。ここでは口だけ作る）。
        glGenBuffers(1, &gpu.instance_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.instance_vbo);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<const void *>(offsetof(InstanceData, x)));
        glVertexAttribDivisor(3, 1);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<const void *>(offsetof(InstanceData, sx)));
        glVertexAttribDivisor(4, 1);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<const void *>(offsetof(InstanceData, r)));
        glVertexAttribDivisor(5, 1);
        // 自発光（P7）。**6 番は頂点側の `flex` が使っている**ので 7 番。
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<const void *>(offsetof(InstanceData, er)));
        glVertexAttribDivisor(7, 1);
        //! 向き（2026-08-15）。**8 番**。0〜7 は上で埋まっている。
        glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData), reinterpret_cast<const void *>(offsetof(InstanceData, yaw)));
        glVertexAttribDivisor(8, 1);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        out.parts.push_back(gpu);
    }

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "プレハブの転送で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

/*!
 * @brief 従来の口。メッシュ化して、そのまま GPU へ載せる。
 * @note 1 個だけ扱う道（`--prefab=` ほか）はこれでよい。ライブラリの一括は
 * `build_mesh` を別スレッドへ出すので `upload_meshed` を直に呼ぶ。
 */
bool VoxelRenderer::upload(const Prefab &prefab, GpuPrefab &out, std::string &err)
{
    PrefabMesh meshed;
    if (!build_mesh(prefab, meshed, err)) {
        return false;
    }
    return this->upload_meshed(prefab, meshed, out, err);
}

void VoxelRenderer::release(GpuPrefab &prefab)
{
    for (auto &part : prefab.parts) {
        if (part.ebo != 0) {
            glDeleteBuffers(1, &part.ebo);
        }
        if (part.vbo != 0) {
            glDeleteBuffers(1, &part.vbo);
        }
        if (part.vao != 0) {
            glDeleteVertexArrays(1, &part.vao);
        }
        if (part.atlas != 0) {
            glDeleteTextures(1, &part.atlas);
        }
        if (part.instance_vbo != 0) {
            glDeleteBuffers(1, &part.instance_vbo);
        }
    }
    prefab.parts.clear();
    if (prefab.palette != 0) {
        glDeleteTextures(1, &prefab.palette);
        prefab.palette = 0;
    }
}

void VoxelRenderer::begin(const Mat4 &view_projection, const SceneLighting &lighting,
    const Mat4 &light_view_projection, GLuint shadow_texture, int shadow_side)
{
    this->view_projection_ = view_projection;
    this->lighting_ = lighting;
    this->light_view_projection_ = light_view_projection;
    this->shadow_texture_ = shadow_texture;
    this->shadow_side_ = shadow_side;
}

void VoxelRenderer::begin_motion(const WindParams &wind, float time)
{
    this->wind_ = wind;
    this->time_ = time;
}

void VoxelRenderer::set_look(const LookParams &look)
{
    this->look_ = look;
}

void VoxelRenderer::set_look_glyphs(const LookGlyphSource &glyphs)
{
    this->look_glyphs_ = glyphs;
}

void VoxelRenderer::set_wear(const WearParams &wear)
{
    this->wear_ = wear;
}

void VoxelRenderer::set_leaf(const LeafParams &leaf)
{
    this->leaf_ = leaf;
}

void VoxelRenderer::begin_cutaway(const Cutaway &cutaway)
{
    this->cutaways_.clear();
    if (cutaway.radius > 0.f) {
        this->cutaways_.push_back(cutaway);
    }
}

void VoxelRenderer::begin_cutaways(const Cutaway *list, std::size_t count)
{
    this->cutaways_.clear();
    for (std::size_t i = 0; (i < count) && (this->cutaways_.size() < kMaxCutaways); ++i) {
        //! 半径 0 は無効。**詰めて入れる**ので、シェーダ側は前から `count` 個だけ見ればよい。
        if (list[i].radius > 0.f) {
            this->cutaways_.push_back(list[i]);
        }
    }
}

void VoxelRenderer::set_cutaway_scale(float scale)
{
    this->cutaway_scale_ = scale;
}

void VoxelRenderer::set_shadow_scale(float scale)
{
    /*
     * **わざと元へ戻す口**（検査の検査。P10 レビュー 10）。`HD2D_BREAK_CUTAWAY_FLOOR=1` を
     * 立てると、抜いた底面がまた壁自身の影を受ける＝**見つけた症状そのもの**に戻る。
     * `--cutaway-check` の (4) はこれで **FAIL しなければならない。**
     * 落ちなければ、それは「底面が黒く沈んでいることを検出できない検査」だったことになる。
     */
    static const bool broken = []() {
        const char *const env = std::getenv("HD2D_BREAK_CUTAWAY_FLOOR");
        const bool on = (env != nullptr) && (env[0] != '\0') && (env[0] != '0');
        if (on) {
            std::fprintf(stderr, "[hd2d] HD2D_BREAK_CUTAWAY_FLOOR: **抜いた底面にも影を落とします**"
                                 "（検査の検査。--cutaway-check は FAIL になるのが正しい）\n");
        }
        return on;
    }();
    this->shadow_scale_ = broken ? 1.f : scale;
}

/*!
 * @brief パーツの合成行列を送る。
 * @details `動き × 静的な配置`。動きはプレハブ空間（マス単位）で、静的な配置は
 * 「ボクセル → プレハブ空間」なので、**動きを左から**掛ける。
 */
void VoxelRenderer::set_part_model(GLint location, const GpuPart &part, const Mat4 *part_motions, std::size_t motion_count)
{
    const bool animated = (part_motions != nullptr) && (part.prefab_part_index >= 0)
        && (static_cast<std::size_t>(part.prefab_part_index) < motion_count);
    if (!animated) {
        glUniformMatrix4fv(location, 1, GL_FALSE, part.model.m);
        return;
    }
    const Mat4 combined = part_motions[static_cast<std::size_t>(part.prefab_part_index)] * part.model;
    glUniformMatrix4fv(location, 1, GL_FALSE, combined.m);
}

void VoxelRenderer::upload_wind(GLint loc_wind_vector, GLint loc_wind_wave, GLint loc_time, GLint loc_phase)
{
    glUniform3f(loc_wind_vector, this->wind_.dir_x * this->wind_.amplitude,
        this->wind_.dir_y * this->wind_.amplitude, 0.f);
    glUniform2f(loc_wind_wave, this->wind_.frequency, this->wind_.wave_k);
    glUniform1f(loc_time, this->time_);
    glUniform1i(loc_phase, static_cast<GLint>(this->wind_.phase));
}

//! 共通の下ごしらえ（描画状態とユニフォーム）。
void VoxelRenderer::bind_common(const GpuPrefab &prefab)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE); //!< 表裏を取り違えたら消えて見える＝巻き方の自己検査になる
    glCullFace(GL_BACK);
    /*
     * **この世界は左手系**（x 東・y 南・z 上）なので、視点行列が鏡映を含む
     * （`math3d.h` の `look_at` を読むこと）。メッシャは右手系の式で外向きを決めているため、
     * 画面上では表面が**時計回り**になる。
     */
    glFrontFace(GL_CW);
    glDisable(GL_BLEND);

    glUseProgram(this->program_);
    glUniformMatrix4fv(this->loc_view_projection_, 1, GL_FALSE, this->view_projection_.m);
    glUniform1i(this->loc_atlas_, 0);
    glUniform1i(this->loc_palette_, 1);
    // 影は 2 番。0（面アトラス）と 1（パレット）を避ける。
    upload_lighting(this->light_loc_, this->lighting_, this->light_view_projection_,
        this->shadow_texture_, this->shadow_side_, 2);
    /*
     * この描画が影を受ける割合（P10 レビュー 10）。**`upload_lighting` の後に上書きする。**
     * 影のテクスチャが無いときは強さ 0 のままでなければならない（`upload_lighting` の注記）
     * ので、あちらと同じ条件を掛け直す。抜いた底面だけが 0 で来る。
     */
    if (this->shadow_scale_ < 1.f) {
        const float strength = (this->shadow_texture_ != 0) ? this->lighting_.shadow_strength : 0.f;
        glUniform1f(this->light_loc_.shadow_strength, strength * std::max(this->shadow_scale_, 0.f));
    }
    this->upload_wind(this->loc_wind_vector_, this->loc_wind_wave_, this->loc_time_, this->loc_wind_phase_);
    /*
     * 画調（軸 E）。**影のパスへは送らない**——抜き方も深度も変えないので、影は同じ。
     * 面の字のテクスチャは**ここで結ぶ**（`upload_look` はユニフォームだけを送る）。
     */
    if ((this->look_.enabled != 0) && this->look_.face_glyph && this->look_glyphs_.ready()) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(this->look_glyphs_.sheet_unit));
        glBindTexture(GL_TEXTURE_2D, this->look_glyphs_.sheet);
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(this->look_glyphs_.cells_unit));
        glBindTexture(GL_TEXTURE_2D, this->look_glyphs_.cells);
        glActiveTexture(GL_TEXTURE0);
    }
    upload_look(this->look_loc_, this->look_, &this->look_glyphs_);
    upload_wear(this->wear_loc_, this->wear_);
    upload_leaf(this->leaf_loc_, this->leaf_);
    // カットアウェイ（P7）。**影のパス（`draw_*_depth`）には送らない**（`Cutaway` の注記）。
    /*
     * 抜く所は最大 `kMaxCutaways` 個（P10 第 4 期）。
     *
     * **強さ（`power`）は穴ごと**である（2026-08-18 に決めた）。以前は先頭のものを
     * 全部へ掛けていた——「同じ絵の中で抜き方が場所ごとに違うと網目の粗さが揃わない」
     * という理屈だったが、**屋根外しの層（層 2）は層 1 と別の強さで抜きたい**ので分けた。
     * 変愚は全部の穴が既定の 2.2 のままなので、**あちらの絵は 1 画素も変わらない。**
     */
    const int cuts = static_cast<int>(this->cutaways_.size());
    if (cuts > 0) {
        float centres[VoxelRenderer::kMaxCutaways * 2]{};
        float radii[VoxelRenderer::kMaxCutaways]{};
        float faces[VoxelRenderer::kMaxCutaways * 3]{};
        float min_zs[VoxelRenderer::kMaxCutaways]{};
        float depths[VoxelRenderer::kMaxCutaways]{};
        float powers[VoxelRenderer::kMaxCutaways]{};
        for (int i = 0; i < cuts; ++i) {
            const Cutaway &cut = this->cutaways_[static_cast<std::size_t>(i)];
            centres[(i * 2) + 0] = cut.centre_x;
            centres[(i * 2) + 1] = cut.centre_y;
            radii[i] = cut.radius;
            faces[(i * 3) + 0] = cut.face_x;
            faces[(i * 3) + 1] = cut.face_y;
            faces[(i * 3) + 2] = cut.face_min;
            min_zs[i] = cut.roof_min_z;
            depths[i] = cut.depth;
            powers[i] = cut.power;
        }
        glUniform2fv(this->loc_cutaway_centre_, cuts, centres);
        glUniform1fv(this->loc_cutaway_radius_, cuts, radii);
        glUniform3fv(this->loc_cutaway_face_, cuts, faces);
        glUniform1fv(this->loc_cutaway_min_z_, cuts, min_zs);
        glUniform1fv(this->loc_cutaway_depth_, cuts, depths);
        glUniform1fv(this->loc_cutaway_power_, cuts, powers);
    }
    glUniform1i(this->loc_cutaway_count_, cuts);
    glUniform1f(this->loc_cutaway_scale_, this->cutaway_scale_);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, prefab.palette);
}

void VoxelRenderer::draw(const GpuPrefab &prefab, const Mat4 *part_motions, std::size_t motion_count)
{
    if ((this->program_ == 0) || prefab.parts.empty()) {
        return;
    }
    this->bind_common(prefab);

    for (const auto &part : prefab.parts) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, part.atlas);
        this->set_part_model(this->loc_model_, part, part_motions, motion_count);
        glUniform1f(this->loc_part_wind_k_, part.wind_k);
        glBindVertexArray(part.vao);
        // インスタンスの属性は使わないので配列を切り、既定値（原点・等倍・白）を入れておく。
        glDisableVertexAttribArray(3);
        glDisableVertexAttribArray(4);
        glDisableVertexAttribArray(5);
        glDisableVertexAttribArray(7);
        glVertexAttrib3f(3, 0.f, 0.f, 0.f);
        glVertexAttrib3f(4, 1.f, 1.f, 1.f);
        glVertexAttrib3f(5, 1.f, 1.f, 1.f);
        glVertexAttrib3f(7, 0.f, 0.f, 0.f); //!< 自発光なし（P7）
        glDrawElements(GL_TRIANGLES, part.index_count, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
}

void VoxelRenderer::draw_instanced(GpuPrefab &prefab, const InstanceData *instances, std::size_t count,
    const Mat4 *part_motions, std::size_t motion_count)
{
    if ((this->program_ == 0) || prefab.parts.empty() || (count == 0) || (instances == nullptr)) {
        return;
    }
    this->bind_common(prefab);

    const std::size_t bytes = count * sizeof(InstanceData);
    for (auto &part : prefab.parts) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, part.atlas);
        this->set_part_model(this->loc_model_, part, part_motions, motion_count);
        glUniform1f(this->loc_part_wind_k_, part.wind_k);
        glBindVertexArray(part.vao);
        glBindBuffer(GL_ARRAY_BUFFER, part.instance_vbo);
        if (bytes > part.instance_capacity) {
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), instances, GL_STREAM_DRAW);
            part.instance_capacity = bytes;
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), instances);
        }
        glEnableVertexAttribArray(3);
        glEnableVertexAttribArray(4);
        glEnableVertexAttribArray(5);
        glEnableVertexAttribArray(7);
        glEnableVertexAttribArray(8);
        glDrawElementsInstanced(GL_TRIANGLES, part.index_count, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
}

void VoxelRenderer::draw_depth_single(const GpuPrefab &prefab, const Mat4 &light_view_projection,
    const Mat4 *part_motions, std::size_t motion_count)
{
    if ((this->depth_program_ == 0) || prefab.parts.empty()) {
        return;
    }
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW);
    glCullFace(GL_FRONT); //!< `draw_instanced_depth` と同じ理由（そちらの注記）

    glUseProgram(this->depth_program_);
    glUniformMatrix4fv(this->depth_loc_view_projection_, 1, GL_FALSE, light_view_projection.m);
    this->upload_wind(this->depth_loc_wind_vector_, this->depth_loc_wind_wave_, this->depth_loc_time_, this->depth_loc_wind_phase_);

    for (const auto &part : prefab.parts) {
        this->set_part_model(this->depth_loc_model_, part, part_motions, motion_count);
        glUniform1f(this->depth_loc_part_wind_k_, part.wind_k);
        glBindVertexArray(part.vao);
        // インスタンスの属性は使わないので既定値を入れる（`draw()` と同じ）。
        glDisableVertexAttribArray(3);
        glDisableVertexAttribArray(4);
        glDisableVertexAttribArray(5);
        glDisableVertexAttribArray(7);
        glVertexAttrib3f(3, 0.f, 0.f, 0.f);
        glVertexAttrib3f(4, 1.f, 1.f, 1.f);
        glVertexAttrib3f(5, 1.f, 1.f, 1.f);
        glVertexAttrib3f(7, 0.f, 0.f, 0.f); //!< 自発光なし（P7）
        glDrawElements(GL_TRIANGLES, part.index_count, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

void VoxelRenderer::draw_instanced_depth(GpuPrefab &prefab, const Mat4 &light_view_projection,
    const InstanceData *instances, std::size_t count, const Mat4 *part_motions, std::size_t motion_count)
{
    if ((this->depth_program_ == 0) || prefab.parts.empty() || (count == 0) || (instances == nullptr)) {
        return;
    }

    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CW); //!< 本描画と同じ（左手系。`bind_common` の注記）
    /*
     * **裏面だけを残す。**中の詰まった箱では、光から遠い側の深度を書くほうが
     * 「自分の面が自分に影を落とす」までの余裕が箱の厚みぶん稼げるので、
     * 深度の下駄を小さくできる（平らな床の縞が出にくい）。
     */
    glCullFace(GL_FRONT);

    glUseProgram(this->depth_program_);
    glUniformMatrix4fv(this->depth_loc_view_projection_, 1, GL_FALSE, light_view_projection.m);
    // **本描画とまったく同じ風**（§8.2）。頂点シェーダも同じ文字列なので、揺れ方は一致する。
    this->upload_wind(this->depth_loc_wind_vector_, this->depth_loc_wind_wave_, this->depth_loc_time_, this->depth_loc_wind_phase_);

    const std::size_t bytes = count * sizeof(InstanceData);
    for (auto &part : prefab.parts) {
        this->set_part_model(this->depth_loc_model_, part, part_motions, motion_count);
        glUniform1f(this->depth_loc_part_wind_k_, part.wind_k);
        glBindVertexArray(part.vao);
        glBindBuffer(GL_ARRAY_BUFFER, part.instance_vbo);
        if (bytes > part.instance_capacity) {
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), instances, GL_STREAM_DRAW);
            part.instance_capacity = bytes;
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), instances);
        }
        glEnableVertexAttribArray(3);
        glEnableVertexAttribArray(4);
        glEnableVertexAttribArray(5);
        glEnableVertexAttribArray(7);
        glEnableVertexAttribArray(8);
        glDrawElementsInstanced(GL_TRIANGLES, part.index_count, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
}

} // namespace hd2d
