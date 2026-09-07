/*!
 * @file entity_view.h
 * @brief 実体（プレイヤ・モンスター・アイテム）をビルボードの置き場所にする（P4 ②③）。
 *
 * 段取りは P4。
 *
 * ## なめらか移動（P4 ③・旧 K-41 相当）
 * コアはマス単位でしか位置を持たないので、そのまま描くと 1 マスずつ瞬間移動する。
 * **個体ごとに画面上の位置を覚えて、目標のマスへ寄せていく。**
 *
 * 個体を追うのに使うのは `MapCellView::monster_slot`（コアの `m_idx`）である。
 * `monster_id` は**種族**の番号なので、同じ種族が 2 体並んでいると
 * 「どっちがどっちへ動いたか」が分からない（`map_cell_view.h` の注記そのもの）。
 *
 * **遠くへ跳んだら補間せず吸着する。**テレポートした敵が画面を横切って流れないように。
 * 死んだ枠は再利用されるので、番号が同じでも別の個体でありうる。吸着があるので
 * 取り違えても長い流し撮りにはならない。
 */
#pragma once

#include "assets/tile_catalog.h"
#include "render/billboard_renderer.h"
#include "render/glyph_atlas.h"
#include "render/ground_ring.h"
#include "render/voxel_renderer.h"
#include "world/slab_library.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

struct GameFrame;

namespace hd2d {

/*!
 * @brief 実体 1 体ぶんの**ボクセル板**（2026-08-15 にビルボードから置き換えた）。
 *
 * @details 描くのに要るのは `slab` と `inst` だけだが、**中心と高さも持たせてある**。
 * カットアウェイ（`append_entity_cutaways`）と VR の卓の外の切り落としが、
 * 「その実体はどこに居るか」を読む必要があるためで、`inst` は**マスの角**を指している
 * （プレハブの原点がそこなので）。中心を毎回引き算し直すと、片方だけ直った状態に必ずなる。
 */
struct SlabInstance {
    int slab{ -1 }; //!< `SlabLibrary` の番号。-1 は描かない
    InstanceData inst{};
    float cx{ 0.f }; //!< 実体の中心（マス単位）
    float cy{ 0.f };
    float height{ 1.f }; //!< 板の高さ（マス単位）
};

/*!
 * @brief 板を**キャラへ向ける**設定（2026-08-15 に決めた）。
 *
 * @details 見下ろしの本線では板は南を向いたまま動かない（同日の決定「見下ろし表示は
 * 南側固定で OK」）。一人称では横から見ると厚み 4 ボクセルの縁しか見えなくなるので、
 * **そのときだけ**キャラの座標へ正対させる。
 *
 * **向く先はカメラの視線ではなくキャラの位置**である。視線に正対させると、
 * 首を振るたびに世界じゅうの板が一斉に回る（VR で 2026-08-14 に踏んだのと同じ話）。
 */
struct SlabFacing {
    bool active{ false }; //!< 一人称のときだけ真。偽なら向きは `fixed_yaw` で固定
    float at_x{ 0.f }; //!< 向く先（＝キャラの位置。マス単位）
    float at_y{ 0.f };
    //! **8 方向へ吸着**するか（偽なら 360 度自由）。決めたことで選択式にした
    bool snap8{ false };
    /*!
     * @brief `active` が偽のときの固定の向き（ラジアン。0 = 南向き＝従来）。
     *
     * @details 見下ろしの 90° 視点回転と
     * VR ジオラマの盤の回転（同 §4.2-3）のために要る。板は厚み 4 ボクセルしか無いので、
     * 回した先で向きが南のままだと**縁しか見えなくなる**（実質消える）。
     * ここへカメラ（盤）の実効 yaw を入れておけば、90° ごとにいつも正対する。
     * @note **既定 0 で従来と 1 ビットも変わらない**（回転を入れる前と同じ値）。
     */
    float fixed_yaw{ 0.f };
};

struct EntityView {
    std::vector<SlabInstance> slabs;
    /*!
     * @brief **アスキー実体**の板。
     *
     * @details 「きゃら、モンスター、アイテムはアスキーを板にしたもの」と決めた。
     * `slabs` と**同時には埋まらない**——実体の表現は排他で、板か文字のどちらかである。
     * 見下ろしでも一人称でも VR でもこの列を使う（軸拘束ビルボードは常にカメラの方位へ
     * 正対するので、見せ方ごとの向き管理が要らない。`SlabFacing::fixed_yaw` も要らない）。
     */
    std::vector<BillboardInstance> glyphs;
    /*!
     * @brief 足元のリング（SQ-1。`render/ground_ring.h`）。
     *
     * @details 敵の**警戒度**（`GameFrame::monster_alerts`）と**照準**（`target_gx/gy`）を
     * 輪で見せる。2026-08-22 に決めた「板の足元に円形の、警戒度に応じた色を変える
     * リングを表示させる」。
     *
     * **ここで組むのは、なめらかな位置を知っているのがここだけだから**である。
     * マスの中心に置くと、動いている敵の輪だけが取り残されて別の場所に浮く。
     * `slabs` とも `glyphs` とも**排他ではない**——実体をどちらで描いていても輪は出る。
     */
    std::vector<GroundRingInstance> rings;
    int monsters{ 0 };
    int objects{ 0 };
    int players{ 0 };
    //! 目録に無い／絵を読めなかった索引の数。**捏造せず、描かずに数える。**
    int missing_tiles{ 0 };
    /*!
     * @name プレイヤの**なめらかな**位置（P10 レビュー 5）
     * @details カメラをここへ追わせると、地形もキャラも一緒に流れる（`MoveSmoothing::All`）。
     * `valid` が偽なのは、プレイヤのマスがまだ届いていない・絵が無いなどで
     * ビルボードを 1 枚も作れなかったとき。そのときカメラは従来どおりマスへ吸着する。
     * @{
     */
    bool player_valid{ false };
    float player_x{ 0.f };
    float player_y{ 0.f };
    /*! @} */
};

class EntityTracker {
public:
    /*!
     * @brief いまのフレームからビルボードを組む。
     * @param dt 前のフレームからの経過秒（なめらか移動の速さに使う）。
     * @param hide_player プレイヤの**絵だけ**を出さない（一人称。`ui/fps_mode.h`）。
     *   なめらかな位置（`player_x/y`）は真でも作る。**カメラがそこへ乗る**ので、
     *   ここを止めると一人称のカメラがマスへ吸着してがくがくする。
     * @param player_min_speed **プレイヤだけ**の最低速度（マス／秒。0 で従来どおり）。
     * @details 追随は指数で寄せるので、目標に近づくほど遅くなる——**1 歩ごとに
     * 「すっ…と止まる」尾を引く**。押しっぱなしで歩いているとそれが等間隔に並び、
     * 一人称では酔いになる（2026-08-14 に気づいた）。ここに歩調ぶんの速さ
     * （1 マス ÷ 1 歩の間隔）を入れると、尾が消えて**次の一歩が来るまでに
     * ちょうど着く**ので、押している間はなめらかに流れ、離せばマスへ収まる。
     * @param glyphs **非 `nullptr` ならアスキー実体**（`out.glyphs` を埋め、`out.slabs` は空のまま）。
     *   板の目録（`catalog` / `slabs`）は引かない
     *   ——字は目録に無い実体でも描けるので、**タイルが無くて消えていた実体も出る**。
     * @param fallback_glyphs 板の道（`glyphs == nullptr`）で**目録に無い実体を字の板へ
     * 落とす**ための字の目録。
     *   `nullptr` なら従来どおり `missing_tiles` に数えて何も描かない。
     *   アスキー実体（`glyphs != nullptr`）のときは見ない——あちらは最初から全部が字である。
     *   出す字はコアの `ascii_fallback` そのもの。**捏造ではない。**
     */
    void build(const GameFrame &frame, const TileCatalog &catalog, SlabLibrary &slabs, VoxelRenderer &renderer,
        float dt, EntityView &out, bool hide_player = false, float player_min_speed = 0.f,
        const SlabFacing &facing = SlabFacing{}, GlyphAtlas *glyphs = nullptr,
        float glyph_height_cells = 0.8f, GlyphAtlas *fallback_glyphs = nullptr);

    //! フロアが変わったときに呼ぶ（覚えている位置を捨てる）。
    void reset();

private:
    struct Track {
        float x{ 0.f };
        float y{ 0.f };
        std::uint64_t seen{ 0 };
    };

    //! 画面上の位置を目標へ寄せる。戻り値は寄せた後の位置。`min_speed` はマス／秒（0 で無効）。
    Track &advance(std::uint32_t key, float target_x, float target_y, float dt, float min_speed = 0.f);

    std::unordered_map<std::uint32_t, Track> tracks_;
    /*!
     * @brief 場面が始まってからの秒数（足元の輪の明滅に使う。`ground_ring.h`）。
     * @details **階が変わっても捨てない**（`reset()` は個体の記憶だけを捨てる）。
     * ここを戻すと、階段を降りるたびに輪の脈が跳ぶ。
     */
    float elapsed_{ 0.f };
    std::uint64_t stamp_{ 0 };
};

} // namespace hd2d
