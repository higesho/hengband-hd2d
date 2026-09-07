/*!
 * @file scene_look.cpp
 * @brief `scene_look.h` の実装。
 */
#include "render/scene_look.h"

#include <cstdint>

#include <algorithm>

namespace hd2d {

using namespace hd2d::gl;

LookParams make_look_params(SceneLookKind kind)
{
    LookParams look; //!< 既定値は TRON の値（`scene_look.h`）
    look.enabled = (kind == SceneLookKind::Tron) ? 1 : 0;
    return look;
}

/*
 * ## 式
 *
 * 面はほぼ真っ黒にして、**線だけを線形の HDR で 1 を超えさせる**。ブルームが掴むのは
 * 1 を超えたものだけなので（`post_process.h` の実測表）、この作りなら「線が発光して
 * にじみ、面は沈む」が後処理の既定値のまま出る。閾値を下げて全体をにじませる作りに
 * すると、正午の町が丸ごと白くなる（同じ罠が P7 に記録されている）。
 *
 * 線は 2 種類ある。
 *
 * | 種 | 引く場所 | 何から作るか |
 * |---|---|---|
 * | マスの格子 | 1 マスごとの境（床にも壁にも） | 世界座標の `fract` |
 * | 物の稜線 | 立体の輪郭と折れ目 | メッシュ化のときに作った四角形の縁までの距離 |
 *
 * どちらも `fwidth` で**画面上の太さを一定**にする。世界の幅で引くと、遠くの床の格子が
 * 1 画素未満になって点滅する（モアレ）。
 */
const char *const kSceneLookGlsl = R"(
uniform int   u_look_enabled;
uniform float u_look_surface;
uniform float u_look_line_gain;
uniform float u_look_line_ambient;
uniform float u_look_grid_px;
uniform float u_look_edge_px;
uniform float u_look_glow_px;
uniform float u_look_glow_gain;
uniform float u_look_fade_px;
uniform float u_look_grid_gain;
uniform float u_look_saturate;
uniform float u_look_level;
uniform float u_look_emissive_gain;

/*
 * ブロックの面に浮かぶアスキー文字。
 *
 * | | |
 * |---|---|
 * | `u_look_sheet` | 16×6 の**等間隔**の字（R = 覆い）。場所は文字コードから計算で出す |
 * | `u_look_cells` | マスごとの R = 文字コード・GBA = 前景色（`GL_RGBA8UI`） |
 * | `u_look_cell_origin` / `u_look_cell_size` | マスの原点と枚数（`texelFetch` の添字を作る） |
 */
uniform int   u_look_glyph;
uniform float u_look_glyph_scale;
uniform float u_look_glyph_gain;
uniform sampler2D  u_look_sheet;
uniform usampler2D u_look_cells;
uniform ivec2 u_look_cell_origin;
uniform ivec2 u_look_cell_size;

/*
 * **16 色をネオン化する**（2026-08-19 に決めた）。最大成分で割ると
 * 色相と彩度の比は保たれたまま最大彩度へ寄る——毒の緑は緑、炎の赤は赤のまま光る。
 *
 * **明るさは元の明るさで抑える。**割り算だけだと、色 0 の未探知の塊（`terrain_view` が
 * 置く「まだ知らない」の箱）や暗い材まで光り出し、知らない所が線で縁取られてしまう。
 * `u_look_level` を掛けて 1 で頭打ちにすれば、0 は 0 のまま・そこそこ明るい材は光り切る。
 */
vec3 hd2d_look_neon(vec3 base)
{
    float peak = max(max(base.r, base.g), base.b);
    vec3 chroma = base / max(peak, 1e-4);
    float keep = clamp(peak * u_look_level, 0.0, 1.0);
    return mix(base, chroma, u_look_saturate) * keep;
}

/*
 * **線の断面**（2026-08-19 に決めた「ネオン光はエッジの周囲に光がにじむ」）。
 *
 * ```
 *   芯   = 1 - smoothstep(0, 太さ/2, 画素距離)      ← くっきりした線そのもの
 *   裾   = exp(-画素距離 / にじみの広さ)             ← まわりへ指数で減衰
 *   返す = 芯 + 裾 × にじみの強さ                    ← 1 を超えてよい（線形の HDR）
 * ```
 *
 * **ブルームだけに任せてはいけない。**後処理のブルームが滲ませるのは「1 を大きく超えた
 * 画素の周り」だけで、線が細いほど画素が足りずに効かない——線そのものが裾を持たないと
 * 蛍光管ではなく針金に見える。物の**外**へ広がるぶんはブルームが受け持つので、
 * 2 つ合わせて 1 つのネオンになる。
 *
 * **距離は画素で測る。**世界の幅で測ると、遠くの床の線が 1 画素未満になって点滅する
 * （モアレ）。`fwidth` で 1 画素あたりの変化量を取って割る。
 */
float hd2d_look_profile(float d_px, float core_px)
{
    float core = 1.0 - smoothstep(0.0, max(core_px * 0.5, 0.25), d_px);
    float glow = exp(-d_px / max(u_look_glow_px, 0.5));
    return core + (glow * u_look_glow_gain);
}

/*
 * **解像できない線は引かない。**`spacing_px` は線と線の間隔（画面の画素）で、
 * これが芯の太さに近づくと線は線でなくなる。
 *
 * **これが無いと遠景の面が丸ごと光る。**遠くの床では 1 マスが数画素になり、
 * 裾（`u_look_glow_px`）が面いっぱいに乗る——実測で画面の平均輝度が 38 → 90 まで跳ね、
 * 地平線が白い靄になった。消えた先は真っ黒でよい（TRON の絵は「格子が闇へ吸い込まれる」
 * ものであって、地平線まで格子で埋めるものではない）。
 */
float hd2d_look_fade(float spacing_px)
{
    return smoothstep(u_look_fade_px * 0.35, u_look_fade_px, spacing_px);
}

/*
 * マスの格子（1 マス）。**面内の 2 軸を法線から選ぶ**——床は xy、南北の壁は xz、
 * 東西の壁は yz。選ばずに 3 軸ぜんぶで引くと、面に垂直な軸の `fract` が
 * 面の上で一定になり、面ごと光ったり消えたりする。
 */
float hd2d_look_grid(vec3 world, vec3 n)
{
    vec3 a = abs(n);
    vec2 p = (a.z >= max(a.x, a.y)) ? world.xy : ((a.x >= a.y) ? world.yz : world.xz);
    vec2 d = 0.5 - abs(fract(p) - 0.5);   // マスの境からの距離（マス。境で 0）
    vec2 w = max(fwidth(p), vec2(1e-6));
    vec2 px = d / w;
    //! マスの幅（画素）＝ 線と線の間隔。斜めから見た面では小さいほうが効く。
    float spacing = min(1.0 / w.x, 1.0 / w.y);
    return hd2d_look_profile(min(px.x, px.y), u_look_grid_px) * hd2d_look_fade(spacing);
}

/*
 * 物の稜線。`edge` は四角形の 4 辺までの距離（テクセル。`VoxelVertex::e0` の注記）。
 * 間隔は**その四角形の短いほうの辺**（画素）——小さすぎる四角形の縁は線にならない。
 */
float hd2d_look_edge(vec4 edge)
{
    float d = min(min(edge.x, edge.y), min(edge.z, edge.w));
    float w = max(fwidth(d), 1e-6);
    float spacing = min(edge.x + edge.y, edge.z + edge.w) / w;
    return hd2d_look_profile(d / w, u_look_edge_px) * hd2d_look_fade(spacing);
}

/*
 * 光の効き（0〜1）。**線にも字にも同じものを掛ける**（こう決めた——「光の届いていない
 * 場所は普通に薄暗くする」。-6）。
 * 0 にすると松明の外が真っ暗で詰み、1 にすると覚えた地図が全部同じ明るさで光って
 * 松明の意味が消える。`u_look_line_ambient` がその間を決める。
 */
float hd2d_look_light_gain(vec3 lit)
{
    float bright = clamp(dot(lit, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    return mix(u_look_line_ambient, 1.0, bright);
}

/*
 * **ブロックの面の真ん中に浮かぶアスキー文字。**
 *
 * マスは世界座標から出す——**面から少し内側へ入って**から `floor` する。壁の面は
 * ちょうどマスの境（x = gx+1 など）に乗っているので、そのまま `floor` すると隣のマスを引く。
 *
 * 面の中の位置も世界座標の `fract` で出す。マスの面はどれも軸に沿っているので、
 * 法線を見て面内の 2 軸を選べばよい（`hd2d_look_grid` と同じ選び方）。
 * **縦は反転する**——世界の z は上へ、シートの v は下へ増えるので、
 * 反転しないと壁の字が上下逆さまになる。
 */
vec3 hd2d_look_face_glyph(vec3 world, vec3 n, vec3 lit)
{
    if (u_look_glyph == 0) {
        return vec3(0.0);
    }
    ivec2 cell = ivec2(floor(world.xy - (n.xy * 0.02))) - u_look_cell_origin;
    if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell, u_look_cell_size))) {
        return vec3(0.0);
    }
    uvec4 info = texelFetch(u_look_cells, cell, 0);
    int code = int(info.r);
    if ((code < 32) || (code > 127)) {
        return vec3(0.0);   // 0 = まだ見ていないマス。**捏造しない**
    }

    vec3 a = abs(n);
    vec2 uv;
    if (a.z >= max(a.x, a.y)) {
        uv = vec2(fract(world.x), fract(world.y));          // 床・天井（北が上になる向き）
    } else if (a.x >= a.y) {
        uv = vec2(fract(world.y), 1.0 - fract(world.z));    // 東西を向く壁
    } else {
        uv = vec2(fract(world.x), 1.0 - fract(world.z));    // 南北を向く壁
    }
    //! 面の中央へ、`u_look_glyph_scale` の大きさで置く。外は字が無い。
    vec2 g = ((uv - 0.5) / max(u_look_glyph_scale, 0.05)) + 0.5;
    if (any(lessThan(g, vec2(0.0))) || any(greaterThan(g, vec2(1.0)))) {
        return vec3(0.0);
    }
    /*
     * **読めない大きさの字は出さない**（線の `hd2d_look_fade` と同じ理由）。
     * 遠くのマスは数画素しかなく、字はただの点になって画面を白く曇らせる。
     * マスの幅ではなく**字の幅**で測る（`u_look_glyph_scale` を掛ける）。
     */
    vec2 fw_uv = max(fwidth(uv), vec2(1e-6));
    float glyph_px = u_look_glyph_scale / max(fw_uv.x, fw_uv.y);
    float readable = hd2d_look_fade(glyph_px);
    if (readable <= 0.0) {
        return vec3(0.0);
    }

    int idx = code - 32;
    int col = idx - ((idx / 16) * 16);
    int row = idx / 16;
    vec2 grid = vec2(16.0, 6.0);
    vec2 at = (vec2(float(col), float(row)) + g) / grid;

    /*
     * **字もにじませる**（2026-08-19 に決めた「面に書かれる文字も光のにじむネオン光」）。
     * 線のような解析的な距離が無いので、まわりを 8 点舐めて平均する。
     *
     * 舐める半径は**画面の画素**で決める（`fwidth`）。枡には字の 1.5 倍の余白があるので
     * （`GlyphAtlas::bake_sheet`）、半径を枡の 1/6 で頭打ちにすれば**隣の字を舐めない**。
     */
    const vec2 kRing[8] = vec2[8](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0),
        vec2(0.7071, 0.7071), vec2(-0.7071, 0.7071), vec2(0.7071, -0.7071), vec2(-0.7071, -0.7071));
    vec2 step_uv = min(fwidth(at) * u_look_glow_px, vec2(1.0 / 6.0) / grid);
    float core = texture(u_look_sheet, at).r;
    float halo = 0.0;
    for (int i = 0; i < 8; ++i) {
        halo += texture(u_look_sheet, at + (step_uv * kRing[i])).r;
    }
    halo *= 0.125;

    /*
     * 色は**コアの文字色そのまま**（「16 色に拘らず、オリジナルの文字色に」と決めた）。
     * 線のように最大彩度へ寄せない——寄せると毒の緑と草の緑が同じ色になる。
     * 明るさだけ上げてブルームに掴ませる。
     */
    vec3 tint = vec3(info.g, info.b, info.a) / 255.0;
    float lumen = (core * u_look_glyph_gain) + (halo * u_look_glyph_gain * u_look_glow_gain);
    return tint * lumen * hd2d_look_light_gain(lit) * readable;
}

/*
 * 面と線を合わせる。`lit` は場の光（方向光＋環境光＋点光源）で、
 * **線にもわずかに効かせる**（`hd2d_look_light_gain`）。
 */
vec3 hd2d_look_apply(vec3 base, vec3 lit, vec3 emissive, float line)
{
    return (base * lit * u_look_surface)
        + (hd2d_look_neon(base) * line * u_look_line_gain * hd2d_look_light_gain(lit))
        + (emissive * u_look_emissive_gain);
}
)";

void LookUniforms::locate(GLuint program)
{
    this->enabled = glGetUniformLocation(program, "u_look_enabled");
    this->glyph = glGetUniformLocation(program, "u_look_glyph");
    this->glyph_scale = glGetUniformLocation(program, "u_look_glyph_scale");
    this->glyph_gain = glGetUniformLocation(program, "u_look_glyph_gain");
    this->sheet = glGetUniformLocation(program, "u_look_sheet");
    this->cells = glGetUniformLocation(program, "u_look_cells");
    this->cell_origin = glGetUniformLocation(program, "u_look_cell_origin");
    this->cell_size = glGetUniformLocation(program, "u_look_cell_size");
    this->surface = glGetUniformLocation(program, "u_look_surface");
    this->line_gain = glGetUniformLocation(program, "u_look_line_gain");
    this->line_ambient = glGetUniformLocation(program, "u_look_line_ambient");
    this->grid_px = glGetUniformLocation(program, "u_look_grid_px");
    this->edge_px = glGetUniformLocation(program, "u_look_edge_px");
    this->glow_px = glGetUniformLocation(program, "u_look_glow_px");
    this->glow_gain = glGetUniformLocation(program, "u_look_glow_gain");
    this->fade_px = glGetUniformLocation(program, "u_look_fade_px");
    this->grid_gain = glGetUniformLocation(program, "u_look_grid_gain");
    this->saturate = glGetUniformLocation(program, "u_look_saturate");
    this->level = glGetUniformLocation(program, "u_look_level");
    this->emissive_gain = glGetUniformLocation(program, "u_look_emissive_gain");
}

namespace {

/*!
 * @brief 面の字を使わないときに、その単位へ結んでおく 1x1 の身代わり。
 *
 * ## なぜ要るか（2026-08-21。実機 Adreno 740 で踏んだ）
 *
 * 面の字の材は 2 つあり、**型が違う**:
 *
 *     uniform sampler2D  u_look_sheet;   // 字の面
 *     uniform usampler2D u_look_cells;   // どのマスにどの字か（整数）
 *
 * 以前はこの 2 つに**「面の字を使うときだけ」単位を割り当てて**いた。使わないときは
 * 既定値のまま＝**どちらも単位 0** を指す。ところが単位 0 には `u_atlas`
 * （`usampler2D`）が結ばれている。GLES の規約はこう言う:
 *
 * > 同じテクスチャ単位を、**型の違うサンプラ**から参照するプログラムで描画したら
 * > `GL_INVALID_OPERATION`。
 *
 * Adreno は規約どおり弾き、
 *
 *     [gl] type=0x824c a shader program has invalid image information or invalid sampler information
 *
 * を出して**ボクセルの描画をすべて捨てた**——地面も壁もプレハブも出ない。
 * エミュレータ（SwiftShader）はこの規約を見逃すので、あちらでは最後まで再現しなかった。
 *
 * だから**単位は常に割り当てる**（下）。そのうえで、実物が無いときは
 * ここで作る身代わりを結ぶ——単位に何も無いと整数サンプラが不完全になり、
 * 同じ検証にまた引っかかる機体があるためである。
 *
 * @note 整数テクスチャは **NEAREST でなければ不完全**（LINEAR は使えない）。
 */
struct GlyphPlaceholders {
    gl::GLuint sheet{ 0 }; //!< RGBA8（`sampler2D` 用）
    gl::GLuint cells{ 0 }; //!< R8UI（`usampler2D` 用）
};

GlyphPlaceholders g_glyph_placeholders;
bool g_glyph_placeholders_made = false;

const GlyphPlaceholders &glyph_placeholders()
{
    if (g_glyph_placeholders_made) {
        return g_glyph_placeholders;
    }
    g_glyph_placeholders_made = true;
    g_glyph_placeholders = []() {
        GlyphPlaceholders out;
        const std::uint8_t transparent[4] = { 0, 0, 0, 0 };
        glGenTextures(1, &out.sheet);
        glBindTexture(GL_TEXTURE_2D, out.sheet);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 1, 1, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, transparent);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));

        //! 実物と**同じ形式**（`LookGlyphSource::cells` の註）。形式が違うと結び直しで戸惑う。
        const std::uint8_t blank[4] = { 0, 0, 0, 0 };
        glGenTextures(1, &out.cells);
        glBindTexture(GL_TEXTURE_2D, out.cells);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8UI), 1, 1, 0, GL_RGBA_INTEGER,
            GL_UNSIGNED_BYTE, blank);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        glBindTexture(GL_TEXTURE_2D, 0);
        return out;
    }();
    return g_glyph_placeholders;
}

} // namespace

void release_look_placeholders()
{
    if (!g_glyph_placeholders_made) {
        return;
    }
    if (g_glyph_placeholders.sheet != 0) {
        glDeleteTextures(1, &g_glyph_placeholders.sheet);
    }
    if (g_glyph_placeholders.cells != 0) {
        glDeleteTextures(1, &g_glyph_placeholders.cells);
    }
    g_glyph_placeholders = GlyphPlaceholders{};
    g_glyph_placeholders_made = false;
}

void upload_look(const LookUniforms &loc, const LookParams &look, const LookGlyphSource *glyphs)
{
    /*
     * 面の字。**材が揃っていなければ 0**（用意できていないテクスチャを引かせない）。
     * `look.face_glyph` は利用者の逃げ口で、`glyphs` は「まだ焼けていない」の側である。
     */
    const bool want_glyph = (look.enabled != 0) && look.face_glyph && (glyphs != nullptr) && glyphs->ready();
    if (loc.glyph >= 0) {
        glUniform1i(loc.glyph, want_glyph ? 1 : 0);
    }
    /*
     * **単位は「使わないとき」も必ず割り当てる。**割り当てを省くと既定の 0 のままになり、
     * `u_atlas`（`usampler2D`）と同じ単位を `u_look_sheet`（`sampler2D`）が指す
     * ——型の違うサンプラの相乗りで、描画そのものが弾かれる（身代わりの註を見よ）。
     */
    static const LookGlyphSource kDefaultUnits{};
    const int sheet_unit = (glyphs != nullptr) ? glyphs->sheet_unit : kDefaultUnits.sheet_unit;
    const int cells_unit = (glyphs != nullptr) ? glyphs->cells_unit : kDefaultUnits.cells_unit;
    if (loc.sheet >= 0) {
        glUniform1i(loc.sheet, sheet_unit);
    }
    if (loc.cells >= 0) {
        glUniform1i(loc.cells, cells_unit);
    }
    if (want_glyph) {
        if (loc.cell_origin >= 0) {
            glUniform2i(loc.cell_origin, glyphs->origin_x, glyphs->origin_y);
        }
        if (loc.cell_size >= 0) {
            glUniform2i(loc.cell_size, glyphs->width, glyphs->height);
        }
    } else if ((loc.sheet >= 0) || (loc.cells >= 0)) {
        //! 実物が無い間は身代わりを結ぶ（単位が空だと整数サンプラが不完全になる）。
        const GlyphPlaceholders &stand_in = glyph_placeholders();
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(sheet_unit));
        glBindTexture(GL_TEXTURE_2D, stand_in.sheet);
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(cells_unit));
        glBindTexture(GL_TEXTURE_2D, stand_in.cells);
        glActiveTexture(GL_TEXTURE0); //!< 呼び手は 0 が現役だと思っている
    }
    if (loc.glyph_scale >= 0) {
        glUniform1f(loc.glyph_scale, look.glyph_scale);
    }
    if (loc.glyph_gain >= 0) {
        glUniform1f(loc.glyph_gain, look.glyph_gain);
    }
    if (loc.enabled >= 0) {
        glUniform1i(loc.enabled, look.enabled);
    }
    if (loc.surface >= 0) {
        glUniform1f(loc.surface, look.surface);
    }
    if (loc.line_gain >= 0) {
        glUniform1f(loc.line_gain, look.line_gain);
    }
    if (loc.line_ambient >= 0) {
        glUniform1f(loc.line_ambient, look.line_ambient);
    }
    if (loc.grid_px >= 0) {
        glUniform1f(loc.grid_px, look.grid_px);
    }
    if (loc.edge_px >= 0) {
        glUniform1f(loc.edge_px, look.edge_px);
    }
    if (loc.glow_px >= 0) {
        glUniform1f(loc.glow_px, look.glow_px);
    }
    if (loc.glow_gain >= 0) {
        glUniform1f(loc.glow_gain, look.glow_gain);
    }
    if (loc.fade_px >= 0) {
        glUniform1f(loc.fade_px, look.fade_px);
    }
    if (loc.grid_gain >= 0) {
        glUniform1f(loc.grid_gain, look.grid_gain);
    }
    if (loc.saturate >= 0) {
        glUniform1f(loc.saturate, look.saturate);
    }
    if (loc.level >= 0) {
        glUniform1f(loc.level, look.level);
    }
    if (loc.emissive_gain >= 0) {
        glUniform1f(loc.emissive_gain, look.emissive_gain);
    }
}

void apply_look_to_lighting(SceneLookKind kind, bool keep_sky, SceneLighting &lighting)
{
    if (kind != SceneLookKind::Tron) {
        return; //!< **標準では 1 つも触らない**（`scene_look.h` の約束）
    }
    /*
     * 面はほぼ真っ黒（`LookParams::surface`）にするので、方向光と環境光は
     * 「立体の形がかすかに読める」ぶんだけ残す。**点光源は触らない**——
     * TRON で色を持ってよいのは光源そのもの（松明・溶岩）だからである。
     */
    const float key = keep_sky ? 0.35f : 0.16f;
    lighting.key_color = lighting.key_color * key;
    lighting.ambient_scale *= keep_sky ? 0.42f : 0.26f;
    if (!keep_sky) {
        //! 空を出さないので、環境光の色も「黒に近い青」で固定する（時刻に依らない）。
        lighting.sky_color = Vec3{ 0.020f, 0.035f, 0.060f };
        lighting.bounce_color = Vec3{ 0.020f, 0.030f, 0.045f };
    } else {
        //! 時刻は残すが、暖色を抜いて寒色へ寄せる（色を持つのは光源だけ、の約束は保つ）。
        lighting.sky_color = Vec3{ lighting.sky_color.x * 0.45f, lighting.sky_color.y * 0.60f,
            lighting.sky_color.z * 0.85f };
        lighting.bounce_color = Vec3{ lighting.bounce_color.x * 0.40f, lighting.bounce_color.y * 0.50f,
            lighting.bounce_color.z * 0.70f };
    }
}

void apply_look_to_post(SceneLookKind kind, bool keep_sky, PostParams &params)
{
    if (kind != SceneLookKind::Tron) {
        return;
    }
    /*
     * **閾値は下げすぎない。**線は `LookParams::line_gain`（2.6）で 1 を超えているので、
     * 既定の 1.10 でも掴める。0.95 まで下げるのは、格子（線の 0.42 倍）と
     * 光の弱い所の線も薄くにじませるため。ここを 0.5 にすると面まで光る。
     */
    params.bloom_threshold = 1.00f;
    params.bloom_knee = 0.25f;
    /*
     * **0.85 では溶岩が白い穴になった**（`docs/screenshots/tron/` の 1 枚目）。自発光は
     * 標準でも既に 1 を大きく超えている（HDR 3.15 の実測。`post_process.h`）ので、
     * TRON では「線が滲む」ぶんだけで足り、光源そのものを持ち上げる必要は無い。
     */
    params.bloom_intensity = 0.55f;
    //! 奥は闇へ落とす。**呼ぶ側が `fog_color` を入れた後に呼ぶこと**（ヘッダの注記）。
    if (keep_sky) {
        params.fog_color = Vec3{ params.fog_color.x * 0.18f, params.fog_color.y * 0.26f,
            params.fog_color.z * 0.40f };
    } else {
        params.fog_color = Vec3{ 0.004f, 0.008f, 0.014f };
    }
    params.fog_density = 0.62f;
    //! 四隅を強く落とす。ブラウン管の暗がりに線が浮く、という絵のため。
    params.vignette_strength = 0.46f;
    params.vignette_radius = 0.52f;
}

GradeParams look_grade(SceneLookKind kind)
{
    GradeParams grade; //!< 既定＝従来の色（標準ではこのまま返す）
    if (kind != SceneLookKind::Tron) {
        return grade;
    }
    /*
     * **黒を持ち上げない**（面は本当に黒であってほしい）。対比を上げ、彩度は少しだけ上げる
     * ——線はシェーダの側で既に最大彩度なので、ここで上げすぎると転ぶ。
     * 影を青緑・光を青白へ振るのが TRON の色分けの実体である
     * （`GradeParams::shadow_tint` の「セピア寄り」と同じ仕掛けの向きを変えただけ）。
     */
    grade.lift = 0.000f;
    grade.contrast = 1.18f;
    grade.saturation = 1.10f;
    grade.shadow_tint = Vec3{ 0.880f, 1.000f, 1.120f };
    /*
     * **光の側も寒色へ振る。**地下の材はほとんど灰色なので、ネオン化しても色相が無い
     * ——線が真っ白のままだと「線画」には見えても「電脳空間」には見えない。
     * 色相を持つ材（毒の緑・炎の赤）は彩度が高いので、この程度の傾けでは転ばない。
     */
    grade.highlight_tint = Vec3{ 0.860f, 1.015f, 1.160f };
    return grade;
}

Vec3 look_clear_color(SceneLookKind kind, bool keep_sky, const Vec3 &standard)
{
    if (kind != SceneLookKind::Tron) {
        return standard;
    }
    return keep_sky ? Vec3{ 0.010f, 0.018f, 0.030f } : Vec3{ 0.000f, 0.004f, 0.008f };
}

bool look_draws_sky(SceneLookKind kind, bool keep_sky)
{
    if (kind != SceneLookKind::Tron) {
        return true;
    }
    return keep_sky;
}

bool parse_scene_look(const std::string &spec, SceneLookKind &out)
{
    if ((spec == "standard") || (spec == "normal") || (spec == "hd2d")) {
        out = SceneLookKind::Standard;
        return true;
    }
    if (spec == "tron") {
        out = SceneLookKind::Tron;
        return true;
    }
    return false;
}

} // namespace hd2d
