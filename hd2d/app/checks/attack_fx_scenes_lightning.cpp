/*!
 * @file attack_fx_scenes_lightning.cpp
 * @brief 稲妻・光線の系統の検査の場面（`attack_fx_scenes.h`）。見た目を作る担当は、ここに場面を足す。
 */
#include "app/checks/attack_fx_scenes.h"

namespace hd2d {

const std::vector<FxCheckScene> &fx_scenes_lightning()
{
    static const std::vector<FxCheckScene> scenes{
        { "elec_bolt", "elec", ProjectionShape::Bolt, 0, 0 },
        { "elec_ball", "elec", ProjectionShape::Ball, 2, 0 },
        { "elec_beam", "elec", ProjectionShape::Beam, 0, 0 },
        { "elec_breath", "elec", ProjectionShape::Breath, 7, 0 },
        { "elec_jump", "elec", ProjectionShape::Jump, 2, 0 },
        { "plasma_ball", "plasma", ProjectionShape::Ball, 2, 0 },
        { "plasma_beam", "plasma", ProjectionShape::Beam, 0, 0 },
        { "mana_ball", "mana", ProjectionShape::Ball, 2, 0 },
        { "mana_beam", "mana", ProjectionShape::Beam, 0, 0 },
        { "lite_beam", "lite", ProjectionShape::Beam, 0, 0 },
        { "lite_ball", "lite", ProjectionShape::Ball, 2, 0 },
        { "lite_breath", "lite", ProjectionShape::Breath, 7, 0 },
        { "lite_weak_beam", "lite_weak", ProjectionShape::Beam, 0, 0 },
        { "lite_weak_ball", "lite_weak", ProjectionShape::Ball, 2, 0 },
        { "dark_beam", "dark", ProjectionShape::Beam, 0, 0 },
        { "dark_ball", "dark", ProjectionShape::Ball, 2, 0 },
        { "dark_breath", "dark", ProjectionShape::Breath, 7, 0 },
        { "dark_weak_beam", "dark_weak", ProjectionShape::Beam, 0, 0 },
        { "dark_weak_ball", "dark_weak", ProjectionShape::Ball, 2, 0 },
        { "disintegrate_beam", "disintegrate", ProjectionShape::Beam, 0, 0 },
        { "disintegrate_ball", "disintegrate", ProjectionShape::Ball, 2, 0 },
        { "disintegrate_breath", "disintegrate", ProjectionShape::Breath, 7, 0 },
        { "death_ray_beam", "death_ray", ProjectionShape::Beam, 0, 0 },
        { "death_ray_ball", "death_ray", ProjectionShape::Ball, 2, 0 },
        { "seeker_beam", "seeker", ProjectionShape::Beam, 0, 0 },
        { "super_ray_beam", "super_ray", ProjectionShape::Beam, 0, 0 },
        // 飛ばない魔法（対象の上・足元・視界内の全員）
        { "elec_hidden", "elec", ProjectionShape::Bolt, 0, kProjHidden },
        { "elec_touch", "elec", ProjectionShape::Touch, 0, 0 },
        { "elec_allsight", "elec", ProjectionShape::Bolt, 0, kProjAllInSight },
        { "disintegrate_hidden", "disintegrate", ProjectionShape::Bolt, 0, kProjHidden },
        { "death_ray_hidden", "death_ray", ProjectionShape::Bolt, 0, kProjHidden },
    };
    return scenes;
}

} // namespace hd2d
