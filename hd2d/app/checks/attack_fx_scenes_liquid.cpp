/*!
 * @file attack_fx_scenes_liquid.cpp
 * @brief 液体の系統の検査の場面（`attack_fx_scenes.h`）。
 *
 * 6 属性すべてに、§5.1 で決まっている代表の形（ボルト・ボール・ブレス）とビームを置く。
 * 毒だけは、ほかの形（ジャンプ・飛ばない魔法・足元だけ・視界内の全員）も置く（形の見分けは属性によらず同じ作りなので）。
 * 血の呪いは「飛ばない魔法」（原作が描かない投射）でも使われるので、その形も置く。
 */
#include "app/checks/attack_fx_scenes.h"

namespace hd2d {

const std::vector<FxCheckScene> &fx_scenes_liquid()
{
    static const std::vector<FxCheckScene> scenes{
        { "pois_bolt", "pois", ProjectionShape::Bolt, 0, 0 },
        { "pois_ball", "pois", ProjectionShape::Ball, 2, 0 },
        { "pois_breath", "pois", ProjectionShape::Breath, 7, 0 },
        { "pois_beam", "pois", ProjectionShape::Beam, 0, 0 },
        { "pois_jump", "pois", ProjectionShape::Jump, 1, 0 },
        { "pois_hidden", "pois", ProjectionShape::Bolt, 0, kProjHidden },
        { "pois_touch", "pois", ProjectionShape::Touch, 0, 0 },
        { "pois_allsight", "pois", ProjectionShape::Bolt, 0, kProjAllInSight },
        { "acid_bolt", "acid", ProjectionShape::Bolt, 0, 0 },
        { "acid_ball", "acid", ProjectionShape::Ball, 2, 0 },
        { "acid_breath", "acid", ProjectionShape::Breath, 7, 0 },
        { "acid_beam", "acid", ProjectionShape::Beam, 0, 0 },
        { "lava_flow_bolt", "lava_flow", ProjectionShape::Bolt, 0, 0 },
        { "lava_flow_ball", "lava_flow", ProjectionShape::Ball, 2, 0 },
        { "lava_flow_breath", "lava_flow", ProjectionShape::Breath, 7, 0 },
        { "lava_flow_beam", "lava_flow", ProjectionShape::Beam, 0, 0 },
        { "water_bolt", "water", ProjectionShape::Bolt, 0, 0 },
        { "water_ball", "water", ProjectionShape::Ball, 2, 0 },
        { "water_breath", "water", ProjectionShape::Breath, 7, 0 },
        { "water_beam", "water", ProjectionShape::Beam, 0, 0 },
        { "water_flow_bolt", "water_flow", ProjectionShape::Bolt, 0, 0 },
        { "water_flow_ball", "water_flow", ProjectionShape::Ball, 2, 0 },
        { "water_flow_breath", "water_flow", ProjectionShape::Breath, 7, 0 },
        { "water_flow_beam", "water_flow", ProjectionShape::Beam, 0, 0 },
        { "blood_curse_bolt", "blood_curse", ProjectionShape::Bolt, 0, 0 },
        { "blood_curse_ball", "blood_curse", ProjectionShape::Ball, 2, 0 },
        { "blood_curse_breath", "blood_curse", ProjectionShape::Breath, 7, 0 },
        { "blood_curse_beam", "blood_curse", ProjectionShape::Beam, 0, 0 },
        { "blood_curse_hidden", "blood_curse", ProjectionShape::Bolt, 0, kProjHidden },
    };
    return scenes;
}

} // namespace hd2d
