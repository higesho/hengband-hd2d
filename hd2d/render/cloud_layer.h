/*!
 * @file cloud_layer.h
 * @brief 地上の空に浮かぶ**ブロックの雲**（2026-08-11 に決めた）
 *
 * ## 何を作るか
 * 「地上と広域マップの空に雲を浮かべよう。白いブロックの雲で南北に常にゆっくり流れる感じで。
 * イメージはマインクラフトの雲」。
 *
 * - **形はブロック**である。丸いビルボードや霧ではなく、角のある白い板の集まり
 *   （1 かたまり＝格子に沿った太い平板。Minecraft の雲そのもの）
 * - **一定の高さに 1 層だけ**浮かぶ。空の絵（`sky_dome`）ではなく**世界の中の幾何**なので、
 *   一人称と浅い俯角では頭の上を流れ、**見下ろしには写らない**（層はカメラより上。
 *   2026-08-11 に決めた「見下ろし時は雲が見えない高さに雲を置いて」）
 * - **常に北へゆっくり流れる**。時刻にも風にも連動しない（Minecraft の雲が
 *   天候と無関係に一定方向へ流れ続けるのと同じ。「常に」が指示の言葉である）
 *
 * ## 天球（`sky_dome`）と別の描き手にする理由
 * 天球は「無限遠の背景」で、深度を読まず書かず一番先に描く。雲は**有限の高さにある物体**で、
 * 地形との前後（塔の向こうの雲・雲の下の町）を深度で解く必要がある。1 つに混ぜると
 * 「背景なのに深度を読む」という中途半端な描き手になる。
 *
 * ## 描く順と深度の約束
 * **不透明なもの（地形・実体）を全部描いた後・`end_scene()` の前**に描く。
 * 深度は**読むが書かない**——雲は半透明で、これより後に世界の幾何は来ないので
 * 書く相手がいない。同じ雲の表と裏で二重に混ざらないよう、**背面カリングで 1 面だけ**残す
 * （巻き方は `voxel/greedy_mesher.cpp` の `winding_sign` と同じ式。表面 = 時計回り）。
 *
 * ## 模様は起動時に焼く（資産を増やさない）
 * 64×64 マスの巻き付く（wrap する）模様を、種を固定した格子ノイズから作る。
 * `sky_dome` の星と同じ流儀で、**起動のたびに同じ雲の形**になる。パターンは
 * タイルとして繰り返し敷くので、世界のどこまで歩いても雲は続く。
 *
 * ## 地下では描かない
 * `SkyState::visible`（= `FloorKind::Surface`）に従う。判断を 2 か所に持たない。
 */
#pragma once

#include "render/gl_core.h"
#include "render/math3d.h"
#include "render/sky_dome.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

/*!
 * @name 雲の寸法と流れ（この 4 つが絵のすべてを決める）
 * @{
 */
/*!
 * @brief 雲の底の高さ（マス）。
 * @details **見下ろしでは見えない高さに置く**（2026-08-11 に決めた・言い直し）。
 *
 * 見下ろし（俯角 10〜85°）の視線が空を向くことはほぼ無いので、**カメラより上**に
 * ありさえすれば見下ろしには写らない。カメラの高さは `距離 × tanθ` で、既定
 * （俯角 29°・1 マス 65px）なら約 10 マス、ズームを最小（24px）まで引いても約 23 マス。
 * 40 はそのどれよりも上で、かつ一人称の遠クリップ（96 マス）から見上げて届く距離にある。
 *
 * 経緯: 8.2（カメラのすぐ下）で作ったら、見下ろしで雲が盤面の上を流れて地図を覆った。
 * 「雲はカメラより下でないと見下ろしに写らない」は事実だが、**写らないのが正しい**
 * （雲は一人称と浅い俯角のための演出で、見下ろしの盤面に被ってはならない）。
 */
inline constexpr float kCloudAltitudeCells = 40.f;
//! 雲の厚み（マス）。Minecraft の雲は高さの割に薄い平板で、それが「雲の層」に見える。
inline constexpr float kCloudThicknessCells = 1.f;
/*!
 * @brief 雲 1 粒（模様の 1 マス）の大きさ（マス）。
 * @details 高さ 40 から見上げる粒なので大きめに取る（Minecraft も雲の粒は
 * 人の背の 10 倍近くある）。1.0 だと地形の格子と同じ細かさで空の砂嵐に見える。
 */
inline constexpr float kCloudTexelCells = 3.f;
/*!
 * @brief 流れの速さ（マス／秒）。**向きは北（−y）で固定**。
 * @details 指示は「南北に常にゆっくり」。0.3 マス/秒は既定の拡大率（1 マス 65px）で
 * 秒に 20px——目で追えば動いていると分かり、放っておけば気にならない速さ。
 */
inline constexpr float kCloudDriftCellsPerSec = 0.3f;
/*! @} */

/*!
 * @brief ブロックの雲の層。模様（頂点）は `init()` が焼き、`draw()` はタイルを敷くだけ。
 */
class CloudLayer {
public:
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 描く。**不透明な世界を全部描いた後・`end_scene()` の前**に呼ぶこと。
     * @param eye カメラ位置。この周りのタイルだけ敷く（視錐台でさらに間引く）。
     * @param sky 空の状態。`visible` が偽（地下）なら何もしない。色も時刻もここから引く
     *   ——雲だけ昼の白のままだと、夜の空に白い板が浮く。
     * @param time_seconds 起動からの秒（`world_seconds`）。流れの位相に使う。
     * @details 見下ろしでも呼んでよい（層はカメラより上なので視錐台が全タイルを弾き、
     * 描画は 1 枚も出ない）。呼ぶ側で見え方を場合分けしない。
     */
    void draw(const Mat4 &view_projection, const Vec3 &eye, const SkyState &sky, float time_seconds);

    /*!
     * @brief **湖の霧**（デザイン4・2026-08-18。紅魔館「霧の湖は高さ2ブロックのあたりに
     * 半透明にした雲をうかべよう」）。指定のマスの上に低い霧の層を敷く。
     *
     * @param cells 霧を浮かべるマス（`TerrainView::mist_cells`。深水のマス）。
     * @param altitude 層の底の高さ（マス。`TownStyle::lake_mist_height`）。
     * @details 空の雲（`draw()`）と同じシェーダ・同じ描き方（深度は読むが書かない・
     * 不透明な世界の後・`end_scene()` の前）。**見下ろしでも描く**——空の雲と違って
     * 高さ 2 の霧は盤面の絵の一部である。マスごとに種で 1/4 を欠いてまだらにし、
     * メッシュはマスの集合が変わったときだけ組み直す（毎フレームは組まない）。
     * 昼夜の色は空の雲と同じ式で引く。
     */
    void draw_patch(const Mat4 &view_projection, const std::vector<std::array<int, 2>> &cells,
        float altitude, const SkyState &sky);

    /*!
     * @brief **地表の霧**（デザイン8 その2・2026-08-19。決めたこと:「きりであれば
     * 半透明にして**雲と同じく動かそう**」）。空の雲の網をそのまま低い所へ敷く。
     *
     * @param altitude 層の底の高さ（マス）。厚みは雲と同じ 1 マス。
     * @param alpha 濃さ（空の雲は 0.80）。
     * @param drift 流れの速さの倍率（1.0 で空の雲と同じ 0.3 マス/秒）。
     * @details **マスごとの箱（`draw_patch`）ではない。**あれは 1 マスの立方体をマスの集合に
     * 敷くので、一人称で横から見ると箱が重なって**白い板の壁**になる（実機で見た様子）。
     * こちらは空の雲と**同じ網・同じ流れ**を高さだけ変えて敷くので、塊は 3 マスの
     * 有機的な形になり、視線が抜ける隙間が残る。視点が層より高くても描く
     * ——地表の霧は**上から見下ろすもの**である（空の雲との違いはそこだけ）。
     */
    void draw_ground(const Mat4 &view_projection, const Vec3 &eye, const SkyState &sky,
        float time_seconds, float altitude, float alpha, float drift = 1.f);

    /*!
     * @brief **配管の蒸気**（デザイン7 その2・2026-08-19。決めたこと:「あちこちのパイプの
     * 継ぎ目から蒸気（半透明のボクセルで吹きだす蒸気を表現）が噴出して揺れている感じに」）。
     *
     * @param jets 噴き出し口（世界座標 {x, y, z}。`TerrainView::steam_jets`）。
     * @param time_seconds 起動からの秒（`world_seconds`）。立ち上がりと揺れの位相に使う。
     * @details **半透明はプレハブの経路に無い**（あちらは α で抜くだけ）ので、雲と同じ
     * シェーダで描く——不透明な世界の後・`end_scene()` の前・深度は読むが書かない。
     * 1 つの口につき塊 3 つが**時間差で立ち上がり、上がるほど大きく薄くなって消える**。
     * 揺れは口ごとに位相の違う横ずれ（風の一様な流れではなく、噴き出しのゆらぎ）。
     *
     * 描くのは**視錐台に入った口だけ**、かつ全体で `kSteamPuffBudget` 塊まで
     * （1 塊 1 描画呼び出しなので、上限が無いと町じゅうの継ぎ手で呼び出しが膨らむ）。
     */
    void draw_steam(const Mat4 &view_projection, const std::vector<std::array<float, 3>> &jets,
        const SkyState &sky, float time_seconds);

private:
    //! 1 フレームに描く蒸気の塊の上限（1 塊 = 1 描画呼び出し）。
    static constexpr int kSteamPuffBudget = 128;
    void build_puff();

    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLsizei vertex_count_{ 0 };
    //! 湖の霧のパッチ（マスの集合が変わったときだけ組み直すキャッシュ）。
    gl::GLuint patch_vao_{ 0 };
    gl::GLuint patch_vbo_{ 0 };
    gl::GLsizei patch_count_{ 0 };
    std::uint64_t patch_stamp_{ 0 };
    //! 蒸気の塊 1 つ（原点まわりの小さな角ばった雲。位置と大きさは uniform で振る）。
    gl::GLuint puff_vao_{ 0 };
    gl::GLuint puff_vbo_{ 0 };
    gl::GLsizei puff_count_{ 0 };

    struct Uniforms {
        gl::GLint view_projection{ -1 };
        gl::GLint offset{ -1 };
        gl::GLint color{ -1 };
        gl::GLint alpha{ -1 };
        gl::GLint rise{ -1 };
        gl::GLint scale{ -1 };
    } loc_;
};

} // namespace hd2d
