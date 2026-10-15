/*!
 * @file attack_fx_scenes_elemental.cpp
 * @brief その他のダメージの属性の検査の場面（`attack_fx_scenes.h`）。見た目を作る担当は、ここに場面を足す。
 *
 * 属性ごとに、ボール（半径 2）・ボルト・ブレス（半径 7）を置く。ビームで撃つ属性（光の剣・マジック・ミサイル・轟音）はビームも、
 * 対象の上で効く属性（遅鈍・時間逆転・重力・因果混乱・深淵）は飛ばない魔法（`hidden`）も置く。
 */
#include "app/checks/attack_fx_scenes.h"

namespace hd2d {

const std::vector<FxCheckScene> &fx_scenes_elemental()
{
    static const std::vector<FxCheckScene> scenes{
        { "fire_ball", "fire", ProjectionShape::Ball, 2, 0 },
        { "fire_bolt", "fire", ProjectionShape::Bolt, 0, 0 },
        { "fire_breath", "fire", ProjectionShape::Breath, 7, 0 },
        { "cold_ball", "cold", ProjectionShape::Ball, 2, 0 },
        { "cold_bolt", "cold", ProjectionShape::Bolt, 0, 0 },
        { "cold_breath", "cold", ProjectionShape::Breath, 7, 0 },
        { "ice_ball", "ice", ProjectionShape::Ball, 2, 0 },
        { "ice_bolt", "ice", ProjectionShape::Bolt, 0, 0 },
        { "ice_breath", "ice", ProjectionShape::Breath, 7, 0 },
        { "shards_ball", "shards", ProjectionShape::Ball, 2, 0 },
        { "shards_bolt", "shards", ProjectionShape::Bolt, 0, 0 },
        { "shards_breath", "shards", ProjectionShape::Breath, 7, 0 },
        { "sound_ball", "sound", ProjectionShape::Ball, 2, 0 },
        { "sound_bolt", "sound", ProjectionShape::Bolt, 0, 0 },
        { "sound_breath", "sound", ProjectionShape::Breath, 7, 0 },
        { "force_ball", "force", ProjectionShape::Ball, 2, 0 },
        { "force_bolt", "force", ProjectionShape::Bolt, 0, 0 },
        { "force_breath", "force", ProjectionShape::Breath, 7, 0 },
        { "meteor_ball", "meteor", ProjectionShape::Ball, 2, 0 },
        { "meteor_bolt", "meteor", ProjectionShape::Bolt, 0, 0 },
        { "meteor_breath", "meteor", ProjectionShape::Breath, 7, 0 },
        { "gravity_ball", "gravity", ProjectionShape::Ball, 2, 0 },
        { "gravity_bolt", "gravity", ProjectionShape::Bolt, 0, 0 },
        { "gravity_breath", "gravity", ProjectionShape::Breath, 7, 0 },
        { "inertial_ball", "inertial", ProjectionShape::Ball, 2, 0 },
        { "inertial_bolt", "inertial", ProjectionShape::Bolt, 0, 0 },
        { "inertial_breath", "inertial", ProjectionShape::Breath, 7, 0 },
        { "missile_ball", "missile", ProjectionShape::Ball, 2, 0 },
        { "missile_bolt", "missile", ProjectionShape::Bolt, 0, 0 },
        { "missile_breath", "missile", ProjectionShape::Breath, 7, 0 },
        { "psy_spear_ball", "psy_spear", ProjectionShape::Ball, 2, 0 },
        { "psy_spear_bolt", "psy_spear", ProjectionShape::Bolt, 0, 0 },
        { "psy_spear_breath", "psy_spear", ProjectionShape::Breath, 7, 0 },
        { "chaos_ball", "chaos", ProjectionShape::Ball, 2, 0 },
        { "chaos_bolt", "chaos", ProjectionShape::Bolt, 0, 0 },
        { "chaos_breath", "chaos", ProjectionShape::Breath, 7, 0 },
        { "nether_ball", "nether", ProjectionShape::Ball, 2, 0 },
        { "nether_bolt", "nether", ProjectionShape::Bolt, 0, 0 },
        { "nether_breath", "nether", ProjectionShape::Breath, 7, 0 },
        { "disenchant_ball", "disenchant", ProjectionShape::Ball, 2, 0 },
        { "disenchant_bolt", "disenchant", ProjectionShape::Bolt, 0, 0 },
        { "disenchant_breath", "disenchant", ProjectionShape::Breath, 7, 0 },
        { "nexus_ball", "nexus", ProjectionShape::Ball, 2, 0 },
        { "nexus_bolt", "nexus", ProjectionShape::Bolt, 0, 0 },
        { "nexus_breath", "nexus", ProjectionShape::Breath, 7, 0 },
        { "time_ball", "time", ProjectionShape::Ball, 2, 0 },
        { "time_bolt", "time", ProjectionShape::Bolt, 0, 0 },
        { "time_breath", "time", ProjectionShape::Breath, 7, 0 },
        { "void_magic_ball", "void_magic", ProjectionShape::Ball, 2, 0 },
        { "void_magic_bolt", "void_magic", ProjectionShape::Bolt, 0, 0 },
        { "void_magic_breath", "void_magic", ProjectionShape::Breath, 7, 0 },
        { "abyss_ball", "abyss", ProjectionShape::Ball, 2, 0 },
        { "abyss_bolt", "abyss", ProjectionShape::Bolt, 0, 0 },
        { "abyss_breath", "abyss", ProjectionShape::Breath, 7, 0 },
        { "holy_fire_ball", "holy_fire", ProjectionShape::Ball, 2, 0 },
        { "holy_fire_bolt", "holy_fire", ProjectionShape::Bolt, 0, 0 },
        { "holy_fire_breath", "holy_fire", ProjectionShape::Breath, 7, 0 },
        { "hell_fire_ball", "hell_fire", ProjectionShape::Ball, 2, 0 },
        { "hell_fire_bolt", "hell_fire", ProjectionShape::Bolt, 0, 0 },
        { "hell_fire_breath", "hell_fire", ProjectionShape::Breath, 7, 0 },
        { "rocket_ball", "rocket", ProjectionShape::Ball, 2, 0 },
        { "rocket_bolt", "rocket", ProjectionShape::Bolt, 0, 0 },
        { "rocket_breath", "rocket", ProjectionShape::Breath, 7, 0 },
        { "nuke_ball", "nuke", ProjectionShape::Ball, 2, 0 },
        { "nuke_bolt", "nuke", ProjectionShape::Bolt, 0, 0 },
        { "nuke_breath", "nuke", ProjectionShape::Breath, 7, 0 },
        { "psy_spear_beam", "psy_spear", ProjectionShape::Beam, 0, 0 },
        { "missile_beam", "missile", ProjectionShape::Beam, 0, 0 },
        { "sound_beam", "sound", ProjectionShape::Beam, 0, 0 },
        { "inertial_hidden", "inertial", ProjectionShape::Bolt, 0, kProjHidden },
        { "time_hidden", "time", ProjectionShape::Bolt, 0, kProjHidden },
        { "gravity_hidden", "gravity", ProjectionShape::Bolt, 0, kProjHidden },
        { "nexus_hidden", "nexus", ProjectionShape::Bolt, 0, kProjHidden },
        { "abyss_hidden", "abyss", ProjectionShape::Bolt, 0, kProjHidden },
    };
    return scenes;
}

} // namespace hd2d
