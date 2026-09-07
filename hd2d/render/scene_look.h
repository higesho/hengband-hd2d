/*!
 * @file scene_look.h
 * @brief **画調**（軸 E）— 標準と TRON。
 *
 * ## 何のための木か
 * が立てた 4 本の軸（画面系・視点・実体の表現・出力先）に、
 * **5 本目「画調」**を足す。画調は他の 4 軸と直交する——見下ろしでも一人称でも VR でも、
 * 板でも文字でも、同じ 1 つの実装が効く。
 *
 * ## ここに全部集める理由（`lighting.h` と同じ）
 * 画調はシェーダ 2 本（ボクセル・ビルボード）と光と後処理の**4 か所**へ同時に掛かる。
 * どれか 1 つが取り残されると「壁だけ TRON で人だけ元のまま」になり、症状から原因へ
 * 辿り着けない。したがって
 *
 * | | |
 * |---|---|
 * | 式 | `kSceneLookGlsl`（**文字列 1 本**。両方のフラグメントシェーダが連結して使う） |
 * | 値 | `LookParams`（1 フレーム 1 個。両方のプログラムへ同じものを送る） |
 * | 送り出し | `LookUniforms`（プログラムごとに場所を 1 回だけ引く） |
 * | 光・後処理・空 | `apply_look_to_*()` / `look_grade()` / `look_draws_sky()` |
 *
 * ## 標準では 1 ビットも変わらない
 * `make_look_params(Standard)` は `enabled = 0` を返し、シェーダはそこで早く抜ける。
 * `apply_look_to_lighting` / `apply_look_to_post` / `look_grade` も標準では**何も触らない**。
 * これは検査（`--look-check`）が毎回確かめる。
 */
#pragma once

#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/post_process.h"

#include <string>

namespace hd2d {

//! 画調。**値は cfg に書くので増やすときは末尾へ。**`Hd2dSettings::SceneLook` と同じ並び。
enum class SceneLookKind : int {
    Standard = 0, //!< 従来の絵（既定）
    Tron, //!< 黒い面＋ネオンの線
};

/*!
 * @brief 画調のユニフォーム 1 組。**ボクセルにもビルボードにも同じものを渡す。**
 *
 * @details 既定値は TRON の値である（標準では `enabled` が 0 なので読まれない）。
 * 実物を見て詰めるための値なので、**どれも「仮」**として扱うこと
 * （記憶 `hengband-visual-review-ask-the-metric`）。
 */
struct LookParams {
    //! 0 = 標準（シェーダはここで抜ける）／1 = TRON。
    int enabled{ 0 };
    /*!
     * @brief 面に残す明るさ（0 = 真っ黒なガラス）。
     * @details 0 にすると松明の丸い明かりが**面の上に 1 画素も出なくなる**ので、
     * どこが照らされているかが線からしか読めない。わずかに残す。
     */
    float surface{ 0.055f };
    /*!
     * @brief 線の明るさ（**線形の HDR**）。1 を超える値でブルームが掴む。
     * @details `PostParams::bloom_threshold`（既定 1.10）より上でなければ光らない。
     */
    float line_gain{ 2.60f };
    /*!
     * @brief 光の当たっていない所での線の明るさの割合。
     * @details 0 にすると**松明の外の線が消えて何も見えなくなる**（ダンジョンで詰む）。
     * 1 にすると覚えた地図が全部同じ明るさで光り、松明の意味が消える。間を取る。
     */
    float line_ambient{ 0.55f };
    //! マスの格子線の**芯**の太さ（画素）。`fwidth` で画面上の太さを一定に保つ。
    float grid_px{ 1.5f };
    //! 物の稜線の**芯**の太さ（画素）。格子より太くして、形の線を前に出す。
    float edge_px{ 2.4f };
    /*!
     * @name にじみ（2026-08-19 に決めた「ネオン光はエッジの周囲に光がにじむ」）
     *
     * @details 芯のまわりへ**指数で減衰する裾**を足す。ブルーム（後処理）だけに任せると
     * 滲むのは「1 を大きく超えた画素の周り」だけで、線が細いほど効かない
     * ——線そのものが裾を持っていないと、蛍光管ではなく針金に見える。
     *
     * 裾は面の上にしか出ない（物の外へは出ない）。物の外へ広がるぶんは
     * ブルームが受け持つので、**2 つ合わせて 1 つのネオン**になっている。
     * @{
     */
    float glow_px{ 5.0f }; //!< 裾の広さ（画素。`exp(-d/glow_px)`）
    float glow_gain{ 0.55f }; //!< 裾の強さ（芯を 1 としたとき）
    /*! @} */
    /*!
     * @brief **解像できない線は引かない**境目（画素）。線の間隔がこれを下回ると消していく。
     *
     * @details 遠くの床では 1 マスが数画素になり、線と線の間が無くなる。そこへ裾（`glow_px`）を
     * 足すと**面が丸ごと光る**——実測で画面の平均輝度が 38 → 90 まで跳ねた（地平線が
     * 白い靄になる）。線の間隔が芯の太さに近づいたら、芯ごと消すのが正しい。
     *
     * 消えた先は真っ黒になる。**それでよい**——TRON の絵は「近くの格子が闇へ吸い込まれる」
     * ものであって、地平線まで格子が埋め尽くすものではない。
     */
    float fade_px{ 12.0f };
    //! マスの格子線の明るさ（稜線を 1 としたときの割合）。地の格子は控えめに。
    float grid_gain{ 0.42f };
    /*!
     * @brief ネオン化の度合い（0 = 元の色のまま／1 = 最大彩度）。
     * @details **色相は動かさない**（そう決めた
     * 「16 色をネオン化」——毒の緑と炎の赤が読めなくなってはいけない）。
     */
    float saturate{ 1.0f };
    /*!
     * @brief 明るさの効き（元の色の最大成分に掛けて 1 で頭打ち）。
     * @details **真っ黒なものを光らせないための門**である。未探知の塊（`terrain_view` が
     * 置く色 0 の箱）と、暗い材のマスをそのまま正規化すると、**知らない所まで
     * 線で縁取られて地図が漏れる**。3.0 なら最大成分 0.33 以上で光り切り、0 は 0 のまま。
     */
    float level{ 3.0f };
    /*!
     * @brief 自発光（溶岩・松明の炎）の倍率。
     * @details **1.35 では溶岩が白い穴になった**（実機の 1 枚目。`docs/screenshots/tron/`）。
     * 自発光は標準でも HDR 3.15 まで出ている（`post_process.h` の実測表）ので、
     * 面を真っ黒にした TRON では**むしろ抑える**ほうが「光源だけが色を持つ」に近づく。
     */
    float emissive_gain{ 0.45f };
    /*!
     * @name **ブロックの面に浮かぶアスキー文字**
     *
     * @details 最初に決めたこと「ブロックの各面にはアスキーアートの文字が入っている。
     * ……各面の真ん中にアスキー文字が浮かんでいる」。字は**そのマスの記号**
     * （`MapCellView::ascii_fallback`）で、色はコアの 16 色（同 §18.4-2/-4）。
     *
     * `face_glyph` を偽にすると字だけ消える（線と黒い面はそのまま）。
     * 記号が細かすぎると感じたときの逃げ口である。
     * @{
     */
    bool face_glyph{ true };
    /*!
     * @brief マスの面に対する**文字シートのマス**の大きさ（0.05〜1）。
     * @details マスは字の 1.5 倍あるので（`GlyphAtlas::bake_sheet` の余白）、
     * **見える字はこの値の 2/3 ほど**になる。0.72 なら面の半分弱。
     * @note 2026-08-19 に決めた「文字はもう一回り小さく」で 0.62（＝字は面の 0.62）から
     * ここへ移した。数字は増えているが、余白のぶん**字は小さくなっている**。
     */
    float glyph_scale{ 0.72f };
    /*!
     * @brief 字の**芯**の明るさ（線形の HDR）。線と同じくブルームに掴ませる。
     * @details 上げすぎると色が飽和して**どの字も白**になる（トーンマップの頭打ち）。
     * 「オリジナルの文字色」と決めたを守るには、色相が残る範囲に留めること。
     */
    float glyph_gain{ 1.55f };
    /*! @} */
};

//! 画調から値を作る。**標準は `enabled = 0` だけが意味を持つ**（残りは読まれない）。
LookParams make_look_params(SceneLookKind kind);

/*!
 * @brief 画調の式（GLSL）。**両方のフラグメントシェーダがこれを連結して使う。**
 * @details `#version` 行は含まない（連結する側が先頭に置く）。
 * `kLightingGlsl` の後ろに置くこと（輝度の式を光の式と共有はしていないが、
 * 並びを一定にしておくと info log の行番号のずれが読みやすい）。
 */
extern const char *const kSceneLookGlsl;

//! 画調のユニフォームの場所。プログラムごとに 1 回だけ引く。
struct LookUniforms {
    gl::GLint enabled{ -1 };
    gl::GLint glyph{ -1 };
    gl::GLint glyph_scale{ -1 };
    gl::GLint glyph_gain{ -1 };
    gl::GLint sheet{ -1 };
    gl::GLint cells{ -1 };
    gl::GLint cell_origin{ -1 };
    gl::GLint cell_size{ -1 };
    gl::GLint surface{ -1 };
    gl::GLint line_gain{ -1 };
    gl::GLint line_ambient{ -1 };
    gl::GLint grid_px{ -1 };
    gl::GLint edge_px{ -1 };
    gl::GLint glow_px{ -1 };
    gl::GLint glow_gain{ -1 };
    gl::GLint fade_px{ -1 };
    gl::GLint grid_gain{ -1 };
    gl::GLint saturate{ -1 };
    gl::GLint level{ -1 };
    gl::GLint emissive_gain{ -1 };

    //! `program` から場所を引く。**リンク後に 1 回だけ。**
    void locate(gl::GLuint program);
};

/*!
 * @brief 面の字を出すのに要る外の材（`upload_look` へ渡す）。
 * @details テクスチャを 2 枚とも持っていないときは**字を出さない**（`sheet`／`cells` の
 * どちらかが 0 なら `u_look_glyph` を 0 にする）。用意できていないのに引くと、
 * 未完成のテクスチャを読むことになる。
 */
struct LookGlyphSource {
    gl::GLuint sheet{ 0 }; //!< 16×6 の等間隔の字（`GlyphAtlas::sheet_texture`）
    gl::GLuint cells{ 0 }; //!< マスごとの記号と色（`GL_RGBA8UI`。R = 文字コード, GBA = 前景色）
    int origin_x{ 0 };
    int origin_y{ 0 };
    int width{ 0 };
    int height{ 0 };
    //! テクスチャ単位（ボクセルは 0=面アトラス・1=パレット・2=影を使っている）。
    int sheet_unit{ 3 };
    int cells_unit{ 4 };

    bool ready() const { return (this->sheet != 0) && (this->cells != 0) && (this->width > 0) && (this->height > 0); }
};

/*!
 * @brief 画調を送る。**`glUseProgram` 済みであること。**
 * @param glyphs 面の字の材。`nullptr`（既定）なら字を出さない。
 * @note テクスチャの**結び付け（`glBindTexture`）は呼ぶ側の仕事**である。ここは
 * ユニフォームだけを送る（`upload_lighting` が影のテクスチャを結ばないのと同じ流儀）。
 */
void upload_look(const LookUniforms &loc, const LookParams &look, const LookGlyphSource *glyphs = nullptr);

/*!
 * @name 画調が光・後処理・空へ及ぼすもの
 *
 * @details **標準では 1 つも触らない。**`kind == Standard` で早く戻るのは
 * 「掛けなければ従来と同一」を構造的に保証するためで、
 * 呼ぶ側が `if` で囲む必要をなくしてある（囲み忘れが必ず起きる形にしない）。
 *
 * `keep_sky` は利用者が回せる旗（`Hd2dSettings::tron_sky`）である。
 * 偽（既定）＝**空も雲も出さず真っ黒**、真＝時刻に応じた空を暗く出す。
 * @{
 */
/*!
 * @brief 太陽と環境光を TRON へ寄せる。
 * @details 面はほぼ真っ黒にするので、方向光と環境光は「形がかすかに読める」ぶんだけ残す。
 * **点光源（松明・溶岩）は触らない**——TRON で色を持ってよいのは光源そのものだからである。
 */
void apply_look_to_lighting(SceneLookKind kind, bool keep_sky, SceneLighting &lighting);
/*!
 * @brief 後処理の強さを TRON へ寄せる（ブルーム・フォグ・ビネット）。
 * @note **`fog_color` を呼ぶ側が上書きした後**に呼ぶこと。順を逆にすると
 * 「その時刻の空の色」がフォグへ戻ってきて、真っ黒のはずの奥が青くなる。
 */
void apply_look_to_post(SceneLookKind kind, bool keep_sky, PostParams &params);
//! カラーグレーディングの元。**標準は `GradeParams{}` そのもの**（同じ LUT が焼ける）。
GradeParams look_grade(SceneLookKind kind);
//! 画面用 FBO の消去色。標準は渡した値をそのまま返す。
Vec3 look_clear_color(SceneLookKind kind, bool keep_sky, const Vec3 &standard);
/*!
 * @brief 空（天球・雲・霧・蒸気）を描くか。
 * @details TRON の黒空では**描かない**。雲も霧も湯気も「もやもやした塊」であって
 * 線画ではないので、1 つでも出ていると画調が崩れる（§3.6）。
 */
bool look_draws_sky(SceneLookKind kind, bool keep_sky);
/*! @} */

/*!
 * @brief `--look=` の綴りから読む。
 * @param spec `standard`（`normal` も可）／ `tron`。
 * @return 読めたら真。読めなければ `out` は触らない。
 */
bool parse_scene_look(const std::string &spec, SceneLookKind &out);

/*!
 * @brief 字の身代わりテクスチャ 2 枚を捨てる。**GL の文脈を消す前に必ず呼ぶ。**
 * @details 理由は `release_shadow_placeholder()` と同じ（SH-36）。
 */
void release_look_placeholders();

} // namespace hd2d
