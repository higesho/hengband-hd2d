/*!
 * @file attack_fx_view.h
 * @brief フレームの攻撃の情報（`GameFrame::projections`・`missiles`）から、ボクセルの視覚効果を起こして再生する。
 *
 * `ui/combat_fx_view.h`（被弾・火花の演出）と同じ考え方で作ってある。
 *
 * ## 受け取り方
 * `GameFrame::projections`・`missiles` は**汲んだら消える**（コアが記録し、フレームを作るたびに 1 回だけ載せる）。
 * 画面側は**フレームが届いたときだけ** `push()` する（毎フレーム呼ぶと同じ効果が何度も出る。`take_new_frame()` が呼ぶ）。
 * 通し番号（`seq`）で重複を除くので、同じフレームを 2 度渡しても 1 回しか起きない。
 *
 * ## 時計
 * 実時計（ミリ秒）で数える。フレームを取りこぼしても速さが変わらない。同時にいくつも飛ぶ（最大 `kMaxActive` 個。
 * 溢れたら古いほうから捨てる）。`evaluate()` が今の時刻の絵をまとめて作り、終わったものを捨てる。
 *
 * ## ゲームの結果との時間のずれ
 * 原作は効果（ダメージ）を描画のあとに出すので、フレームが届いたときには相手が既に倒れていることがある。
 * ここでは気にしない（効果は撃った位置から着弾点へ、届いた時点から始めて再生する）。
 */
#pragma once

#include "frame/attack_fx.h"
#include "render/voxel_fx.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace hd2d {

//! 描く側の事情（検査と本番で同じ入れ物を使う）。
struct FxViewParams {
    //! 投げた物の板が引けるか。引けない物は小さな箱で描く。空なら全部「引けない」。
    std::function<bool(std::uint16_t item_id)> has_slab;
    //! 板の向き（床の実体の板と同じ。画面へ正対させるときはカメラの yaw と pitch）。
    float face_yaw{ 0.f };
    float face_pitch{ 0.f };
    //! 一人称：板ごとに、飛んでいる位置からこの点へ向ける（床の板が本人へ正対するのと同じ）。真のとき `face_yaw` は使わない。
    bool face_toward{ false };
    float toward_x{ 0.f };
    float toward_y{ 0.f };
    //! 画面の右と上（世界座標での向き。正規化）。絵や輪を画面に正対させる属性の見た目が使う（`FxSpec::cam_right/cam_up`）。
    Vec3 cam_right{ 1.f, 0.f, 0.f };
    Vec3 cam_up{ 0.f, 0.f, 1.f };
    //! マス (gx,gy) にいる実体のタイルの画素を引く（分解の「タイルの写し」用）。空なら引けない。
    std::function<bool(int gx, int gy, FxTileImage &out)> tile_at;
};

class AttackFxView {
public:
    //! 同時に再生できる数。
    static constexpr std::size_t kMaxActive = 24;
    //! 通し番号を覚えておく時間（ミリ秒）。
    static constexpr std::uint32_t kSeenMs = 10000;

    //! フレームから新しく来た効果を受け取る。`now_ms` は単調時計（`clock::ticks()`）。
    void push(const std::vector<ProjectionFx> &projections, const std::vector<MissileFx> &missiles, std::uint32_t now_ms);

    /*!
     * @brief `now_ms` の絵を `out` に**足す**（`out` は消さない）。終わった効果は捨てる。
     * @details 点光源は強い順に `kMaxExtraPointLights` 個まで残す（`collect_point_lights()` が確保する枠と同じ数）。
     */
    void evaluate(std::uint32_t now_ms, const VoxelFxParams &params, const FxViewParams &view, FxFrame &out);

    //! 全部捨てる（階が変わったとき・検査の頭）。重複除去の記憶も捨てる。
    void clear();

    bool active() const { return !this->actives_.empty(); }
    std::size_t size() const { return this->actives_.size(); }
    //! 検査が撃ち方を直に見るため。
    const FxSpec &spec_at(std::size_t index) const { return this->actives_[index].spec; }

    /*!
     * @brief フレームの 1 件を撃ち方にする（`push()` が使う。検査も直に呼ぶ）。
     * @return 描くものが無い（盲目で見えない・経路が無い射撃など）なら偽。
     */
    static bool make_spec(const ProjectionFx &projection, FxSpec &out);
    static bool make_spec(const MissileFx &missile, FxSpec &out);

private:
    struct Active {
        FxSpec spec;
        std::uint32_t start_ms{ 0 };
        bool slab_decided{ false };
    };
    std::vector<Active> actives_;
    //! 受け取った通し番号と、そのときの時刻。
    std::vector<std::pair<std::uint32_t, std::uint32_t>> seen_projections_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> seen_missiles_;
};

} // namespace hd2d
