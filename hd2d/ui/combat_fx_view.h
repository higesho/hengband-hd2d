/*!
 * @file combat_fx_view.h
 * @brief 戦闘の見せ場を描く
 *
 * コアは「何が・どこで・どの属性で起きたか」しか言わない（`presentation/frame/combat_fx.h`）。
 * **色も形も間合いもここで決める。**
 *
 * ## なぜ要るか
 * リアルタイムでは目が地図に張り付くので、メッセージ行が 1 行増えても気づけない。
 * しかも小窓の裏で殴られているときは誰もメッセージを読んでいない。
 * さらに被弾時の `-more-`（＝止まって気づかせる仕掛け）はリアルタイムでは外してある
 * （`display-messages.cpp`）。**気づかせる役目はここが引き受けている。**
 *
 * ## 溜めない
 * `GameFrame::combat_fx` は毎フレーム入れ替わる（コア側が汲んで空にする）ので、
 * 受け取ったらこちらで寿命を持つ。フレームを取りこぼしても演出が伸び縮みしないよう、
 * 時間は実時計（ms）で数える。
 */
#pragma once

#include "frame/combat_fx.h"
#include "render/camera.h"
#include "ui/ui_layout.h"

#include <cstdint>
#include <vector>

namespace hd2d {

class UiPaint;

struct FxColor {
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
};

//! マスの上で弾ける演出（命中・着弾）。画面座標へ落とすのは呼び出し側。
struct FxSpark {
    float cell_y{ 0.f };
    float cell_x{ 0.f };
    FxColor color;
    //! 0（出たて）→ 1（消える）。
    float progress{ 0.f };
    //! 強さ 0〜1（大きいほど大きく描く）。
    float intensity{ 1.f };
};

//! 飛道。撃ったマスから着弾側へ走る。
struct FxBolt {
    float from_y{ 0.f };
    float from_x{ 0.f };
    float to_y{ 0.f };
    float to_x{ 0.f };
    FxColor color;
    float progress{ 0.f };
};

class CombatFxView {
public:
    //! 新しいフレームの出来事を取り込む。`now_ms` は単調時計。
    void push(const std::vector<CombatFxEvent> &events, uint32_t now_ms);
    //! 寿命の切れたものを捨てる。毎フレーム呼ぶ。
    void update(uint32_t now_ms);

    /*!
     * @brief 全面のフラッシュ（被弾）。出すものが無ければ false。
     * @details 複数まとめて受けたときは**一番強いものが勝つ**（足し合わせると白飛びする）。
     */
    bool flash(FxColor &color, float &alpha) const;

    /*!
     * @brief 画面の揺れ（マス単位のずらし）。カメラの注視点へ足す。
     * @details 酔う人がいるので切れるようにしてある（`hd2d.cfg` の `damage_shake`）。
     */
    void shake_offset(float &dx, float &dz) const;

    std::vector<FxSpark> sparks() const;
    std::vector<FxBolt> bolts() const;

    //! 属性の束 → 色。**色の定義はここだけ**（コアにも presentation にも色は無い）。
    static FxColor color_of(CombatFxElement element);

    bool empty() const { return this->actives.empty(); }

private:
    struct Active {
        CombatFxEvent event;
        uint32_t start_ms{ 0 };
    };

    //! 種類ごとの寿命（ms）。短すぎると見落とし、長すぎると次の一撃と重なって濁る。
    static constexpr uint32_t FLASH_MS = 260;
    static constexpr uint32_t SPARK_MS = 220;
    static constexpr uint32_t BOLT_MS = 180;
    static constexpr uint32_t SHAKE_MS = 220;
    //! 同時に抱える上限。溢れたら古いものから捨てる（見えないものに手間をかけない）。
    static constexpr size_t MAX_ACTIVES = 48;

    static uint32_t life_ms_of(CombatFxKind kind);
    float progress_of(const Active &active) const;

    std::vector<Active> actives;
    uint32_t now_ms{ 0 };
};

/*!
 * @brief 戦闘の見せ場を描く
 * @param scene 3D を出している矩形。**被弾の色はここにだけ敷く**
 *              （画面いっぱいに敷くとサブパネルの数字まで染まって読めなくなる）
 * @param flash_enabled 被弾のフラッシュを出すか（`hd2d.cfg` の `damage_flash`）
 * @details マスを画面へ落とすのにカメラが要る。**揺れを当てた後のカメラ**を渡すこと
 *          （揺れる前のカメラで落とすと、点だけ地面から浮いて見える）。
 */
void draw_combat_fx(UiPaint &paint, const CombatFxView &fx, const Camera &camera,
    const RectPx &scene, bool flash_enabled);

} // namespace hd2d
