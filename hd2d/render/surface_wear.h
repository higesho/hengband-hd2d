/*!
 * @file surface_wear.h
 * @brief **面の汚し**——ボクセルの凹凸より細かいディテールを色で足す。
 *
 * ## 何をするものか
 * ボクセルは 1 マスの面が 1 つの色でしかない。凹凸を増やせば形は細かくなるが、
 * 面そのものは平らなままである。ここは**面の中を塗り分けて**、同じ形のまま
 * 描き込み量を増やす（2026-08-23 に決めた:「ボクセルの凹凸以上に
 * ディテールを追加したい。テクスチャに汚しを加えてディテール」）。
 *
 * 足すのは 4 層で、どれも**フラグメントシェーダの中だけ**で作る。
 *
 * | 層 | 何を引くか | 何に見えるか |
 * |---|---|---|
 * | 溜まり | 作った AO（面アトラスの G） | 隅の垢。影ではなく汚れとして色が濁る |
 * | 摩耗 | 1 マスの面の縁までの距離 | 角が白茶けて、平らな壁が積んだ壁に見える |
 * | 斑 | 世界座標の値ノイズ 2 段 | しみ・苔・錆。大面積の単調さが消える |
 * | 垂れ | 縦に引き伸ばした値ノイズ | 壁の上から流れた跡。屋外の建物に効く |
 *
 * ## 素材も頂点も増えない
 * 面アトラスの座標（`a_uv`）は**正規化していないテクセル座標**なので、貪欲法で
 * 束ねた四角形の上で「1 マスにつき 1 だけ増える値」になっている。つまり
 * `fract(v_uv)` が**1 マスの面の中の位置**として既に来ている。メッシャも `.vox` も
 * タイルも触らずに、面の中を塗り分けられるのはこのためである。
 *
 * ## ドット絵を壊さない（**ここが肝**）
 * ノイズは**必ず格子へ丸めてから**引く（`WearParams::lattice`）。連続のまま使うと
 * フィルム粒子が乗った絵になり、板（ボクセルのスラブ）やタイルのドットと画調が食い違う。
 * 丸めておけば「小さめのドットで描き込んだ」ように見える。
 *
 * ## 材質ごとに変える
 * 木と鉄と布が同じ汚れ方をすると町ぜんぶが一様に灰色くすむ。材質は
 * **パレットの色から引く**（`material_for_color`）。表は `material_table.inc` に
 * 焼いてあり、焼くのは `tools/voxel/gen_material_table.py`。
 *
 * ## 既定
 * `WearParams{}` は `amount = 0` ＝**従来と 1 ビットも同じ絵**である。強さは
 * `Hd2dSettings::wear` が運ぶ（cfg の `wear=`）。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"

#include <cstdint>
#include <string>

namespace hd2d {

/*!
 * @brief 材質。**`tools/voxel/gen_material_table.py` の `CLASSES` と同じ順・同じ番号。**
 * @details 片方だけ足すと、作った表の番号が別の材質を指す（絵は出るので気づきにくい）。
 */
enum class MaterialClass : std::uint8_t {
    Default = 0, //!< 分からないもの。**弱い汚しだけ**掛ける
    Stone, //!< 石・煉瓦・舗装・瓦
    Soil, //!< 土・砂・灰・炭
    Wood, //!< 材木・板・樹皮
    Metal, //!< 鉄・鋼・金
    Plaster, //!< 漆喰・紙・骨・大理石
    Fabric, //!< 布・幟・藁・毛
    Plant, //!< 草・葉・苔・花
    Clean, //!< 水・炎・光・硝子。**汚さない**
    /*!
     * @brief 木の葉。**草・苔（`Plant`）とは別**である。
     * @details 葉の面には葉の形を描く（`render/leaf_detail.h`）。苔に葉脈が生えると
     * 嘘になるので分けてある。**番号は末尾へ**（作った表の番号がずれる）。
     */
    Leaf,
    Count
};

//! 材質の数（シェーダの材質表と長さを合わせるために使う）。
constexpr int kMaterialClassCount = static_cast<int>(MaterialClass::Count);

//! 材質ごとの入切（全部入り）。`--wear-materials=all` と同じ。
constexpr std::uint32_t kWearAllMaterials = 0xFFFFFFFFu;

/*!
 * @brief **既定で汚しを掛ける材質**（2026-08-23 に決めた）。
 *
 * @details 「かけるもの: 石・土・木・金・漆喰・布。それ以外はいったんかけない」。
 * 掛けないのは草木・木の葉（汚しより葉の描き込みのほうが効く）・水炎光硝子・
 * **推定できなかった色**（何か分からないものを汚さない）。
 *
 * 一覧は （`tools/voxel/report_palette_uses.py` が焼く）。
 */
constexpr std::uint32_t kWearDefaultMaterials
    = (1u << static_cast<unsigned>(MaterialClass::Stone))
    | (1u << static_cast<unsigned>(MaterialClass::Soil))
    | (1u << static_cast<unsigned>(MaterialClass::Wood))
    | (1u << static_cast<unsigned>(MaterialClass::Metal))
    | (1u << static_cast<unsigned>(MaterialClass::Plaster))
    | (1u << static_cast<unsigned>(MaterialClass::Fabric));

/*!
 * @brief 材質の綴りの並び（`MaterialClass` の順）。cfg の読み書きに使う。
 * @details `material_class_name()` と同じ綴り。**並びを変えるときは両方**。
 */
const char *const *material_class_names();

/*!
 * @brief `stone,wood,leaf` の形から入切のビットを作る。
 * @param spec 綴りを `,` で並べたもの。`all` で全部・`none` で全部切る。
 * @return 読めたら真。読めない綴りが 1 つでもあれば偽（`out` は触らない）。
 */
bool parse_wear_materials(const std::string &spec, std::uint32_t &out);

//! 入切のビットを `stone,wood,…` の綴りへ戻す（cfg へ書くため）。
std::string wear_materials_text(std::uint32_t bits);

/*!
 * @brief パレットの 1 色から材質を引く。
 * @details まず作った表（`material_table.inc`。`gen_prefabs.py` の登録簿が元）を引き、
 * 無ければ**色みから当てる**。当てられなければ `Default`——
 * 勝手に石や木へ寄せるより、弱い汚しで済ませるほうが害が小さい。
 */
MaterialClass material_for_color(std::uint8_t r, std::uint8_t g, std::uint8_t b);

//! 材質の名前（検査の出力用。ASCII）。
const char *material_class_name(MaterialClass mat);

/*!
 * @brief 汚しの強さ。**1 フレーム 1 個**。
 * @details 実物を見て詰めるための値なので、**どれも「仮」**として扱うこと
 * （記憶 `hengband-visual-review-ask-the-metric`）。
 */
struct WearParams {
    /*!
     * @brief 全体の強さ（0 = 掛けない）。cfg の `wear=` がそのまま入る。
     * @details **0 でシェーダが早く抜ける**ので、切ってあるときのコストはほぼ 0。
     */
    float amount{ 0.f };
    /*!
     * @brief **材質ごとの入切**（ビット。`1 << (int)MaterialClass`）。
     *
     * @details 2026-08-23 に決めた:「材質ごとに汚しをかけるか否か決める」。
     * 重み（`hd2d_wear_profile`）とは別の軸である——重みは「どのくらい」で、
     * ここは「そもそも掛けるか」。**掛けないと決めた材質はシェーダが 1 命令で戻る。**
     *
     * 既定は全部入り（`kWearAllMaterials`）。何が何に使われているかは
     * （`tools/voxel/report_palette_uses.py` が焼く）。
     */
    std::uint32_t materials{ kWearDefaultMaterials };
    //! 溜まり汚れ（隅の垢）。
    float cavity{ 1.f };
    //! 角の摩耗（縁が白茶ける）。
    float edge{ 1.f };
    //! 斑（しみ・苔・錆）。
    float mottle{ 1.f };
    //! 垂れ（壁を流れた跡）。
    float streak{ 1.f };
    /*!
     * @brief 1 マスを何分割の格子で塗るか。**ドット絵を保つための丸め幅**である。
     * @details 既定 32 は**地形の板 1 ボクセルにつき 1 粒**（板は 1 マス 32 ボクセル）。
     * ここを上げると粒がボクセルより細かくなり、画面上で 1 画素を割って**ちらつく**
     * （1600×900 で 1 マス 65 画素 ＝ 1 ボクセル 2 画素しかない）。
     * 下げると数ボクセルがまとめて同じ色になる。
     */
    float lattice{ 32.f };
    //! 汚れ色（線形。掛け算で使う）。土気の灰。
    Vec3 grime{ 0.62f, 0.58f, 0.50f };
    /*!
     * @brief 種。世界を丸ごとずらすだけ（同じ町でも別の汚れ方にできる）。
     * @details 既定 0。**変えると全部の斑が動く**ので、比べるとき以外は触らない。
     */
    float seed{ 0.f };
};

/*!
 * @brief 汚しの式（GLSL）。**ボクセルのフラグメントシェーダが連結して使う。**
 * @details `#version` 行は含まない。`kLightingGlsl` の後ろに置くこと。
 * @note ビルボード（かきわり）には掛けない——実体の板は既に描き込んであるタイルで、
 * 上から汚すと絵師の描いた濃淡と喧嘩する。
 */
extern const char *const kSurfaceWearGlsl;

//! 汚しのユニフォームの場所。プログラムごとにリンク後 1 回だけ引く。
struct WearUniforms {
    gl::GLint amount{ -1 };
    gl::GLint materials{ -1 };
    gl::GLint mix{ -1 };
    gl::GLint grime{ -1 };
    gl::GLint lattice{ -1 };
    gl::GLint seed{ -1 };

    void locate(gl::GLuint program);
};

/*!
 * @brief 汚しを送る。**`glUseProgram` 済みであること。**
 * @details `amount` が 0 でも送る（前のフレームの値が残らないように）。
 */
void upload_wear(const WearUniforms &loc, const WearParams &wear);

/*!
 * @brief `--wear=` の綴りから読む。
 * @param spec `off` / `on` / 0.0〜2.0 の数。
 * @return 読めたら真。読めなければ `out` は触らない。
 */
bool parse_wear_amount(const std::string &spec, float &out);

} // namespace hd2d
