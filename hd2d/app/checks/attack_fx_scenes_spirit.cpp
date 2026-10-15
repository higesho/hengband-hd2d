/*!
 * @file attack_fx_scenes_spirit.cpp
 * @brief 呪い・心・状態変化・地形・特殊の検査の場面（`attack_fx_scenes.h`）。見た目を作る担当は、ここに場面を足す。
 *
 * 属性ごとに代表の形で 1 場面（§5.4 呪い・§5.5 心・§5.6 状態変化・§5.7 地形・§5.8 特殊）、続けて形違いの場面。
 */
#include "app/checks/attack_fx_scenes.h"

namespace hd2d {

const std::vector<FxCheckScene> &fx_scenes_spirit()
{
    static const std::vector<FxCheckScene> scenes{
        // §5.4
        { "cause_1_hidden", "cause_1", ProjectionShape::Bolt, 0, kProjHidden },
        { "cause_2_hidden", "cause_2", ProjectionShape::Bolt, 0, kProjHidden },
        { "cause_3_hidden", "cause_3", ProjectionShape::Bolt, 0, kProjHidden },
        { "cause_4_hidden", "cause_4", ProjectionShape::Bolt, 0, kProjHidden },
        { "wounds_hidden", "wounds", ProjectionShape::Bolt, 0, kProjHidden },
        { "hand_doom_hidden", "hand_doom", ProjectionShape::Bolt, 0, kProjHidden },
        { "hypodynamia_bolt", "hypodynamia", ProjectionShape::Bolt, 0, 0 },
        // §5.5
        { "psi_hidden", "psi", ProjectionShape::Bolt, 0, kProjHidden },
        { "mind_blast_hidden", "mind_blast", ProjectionShape::Bolt, 0, kProjHidden },
        { "brain_smash_hidden", "brain_smash", ProjectionShape::Bolt, 0, kProjHidden },
        { "psi_drain_bolt", "psi_drain", ProjectionShape::Bolt, 0, 0 },
        { "drain_mana_bolt", "drain_mana", ProjectionShape::Bolt, 0, 0 },
        { "confusion_hidden", "confusion", ProjectionShape::Bolt, 0, kProjHidden },
        { "old_conf_bolt", "old_conf", ProjectionShape::Bolt, 0, 0 },
        { "charm_bolt", "charm", ProjectionShape::Bolt, 0, 0 },
        { "charm_living_bolt", "charm_living", ProjectionShape::Bolt, 0, 0 },
        { "control_undead_bolt", "control_undead", ProjectionShape::Bolt, 0, 0 },
        { "control_animal_bolt", "control_animal", ProjectionShape::Bolt, 0, 0 },
        { "control_demon_bolt", "control_demon", ProjectionShape::Bolt, 0, 0 },
        { "domination_bolt", "domination", ProjectionShape::Bolt, 0, 0 },
        // §5.6
        { "old_sleep_bolt", "old_sleep", ProjectionShape::Bolt, 0, 0 },
        { "old_slow_bolt", "old_slow", ProjectionShape::Bolt, 0, 0 },
        { "old_speed_bolt", "old_speed", ProjectionShape::Bolt, 0, 0 },
        { "old_heal_bolt", "old_heal", ProjectionShape::Bolt, 0, 0 },
        { "star_heal_hidden", "star_heal", ProjectionShape::Bolt, 0, kProjHidden },
        { "old_poly_bolt", "old_poly", ProjectionShape::Bolt, 0, 0 },
        { "old_clone_bolt", "old_clone", ProjectionShape::Bolt, 0, 0 },
        { "turn_undead_hidden", "turn_undead", ProjectionShape::Bolt, 0, kProjHidden },
        { "turn_evil_hidden", "turn_evil", ProjectionShape::Bolt, 0, kProjHidden },
        { "turn_all_ball", "turn_all", ProjectionShape::Ball, 2, 0 },
        { "away_undead_bolt", "away_undead", ProjectionShape::Bolt, 0, 0 },
        { "away_evil_bolt", "away_evil", ProjectionShape::Bolt, 0, 0 },
        { "away_all_bolt", "away_all", ProjectionShape::Bolt, 0, 0 },
        { "stasis_bolt", "stasis", ProjectionShape::Bolt, 0, 0 },
        { "stasis_evil_hidden", "stasis_evil", ProjectionShape::Bolt, 0, kProjHidden },
        { "disp_undead_bolt", "disp_undead", ProjectionShape::Bolt, 0, 0 },
        { "disp_evil_bolt", "disp_evil", ProjectionShape::Bolt, 0, 0 },
        { "disp_all_allsight", "disp_all", ProjectionShape::Bolt, 0, kProjAllInSight },
        { "disp_demon_bolt", "disp_demon", ProjectionShape::Bolt, 0, 0 },
        { "disp_living_bolt", "disp_living", ProjectionShape::Bolt, 0, 0 },
        { "disp_good_bolt", "disp_good", ProjectionShape::Bolt, 0, 0 },
        { "stun_hidden", "stun", ProjectionShape::Bolt, 0, kProjHidden },
        { "crusade_touch", "crusade", ProjectionShape::Touch, 0, 0 },
        // §5.7
        { "kill_wall_touch", "kill_wall", ProjectionShape::Touch, 0, 0 },
        { "make_wall_touch", "make_wall", ProjectionShape::Touch, 0, 0 },
        { "stone_wall_touch", "stone_wall", ProjectionShape::Touch, 0, 0 },
        { "kill_door_touch", "kill_door", ProjectionShape::Touch, 0, 0 },
        { "make_door_touch", "make_door", ProjectionShape::Touch, 0, 0 },
        { "jam_door_touch", "jam_door", ProjectionShape::Touch, 0, 0 },
        { "kill_trap_touch", "kill_trap", ProjectionShape::Touch, 0, 0 },
        { "make_trap_touch", "make_trap", ProjectionShape::Touch, 0, 0 },
        { "make_tree_touch", "make_tree", ProjectionShape::Touch, 0, 0 },
        { "make_rune_protection_touch", "make_rune_protection", ProjectionShape::Touch, 0, 0 },
        // §5.8
        { "capture_bolt", "capture", ProjectionShape::Bolt, 0, 0 },
        { "anim_dead_touch", "anim_dead", ProjectionShape::Touch, 0, 0 },
        { "photo_allsight", "photo", ProjectionShape::Bolt, 0, kProjAllInSight },
        { "engetsu_beam", "engetsu", ProjectionShape::Beam, 0, 0 },
        { "telekinesis_bolt", "telekinesis", ProjectionShape::Bolt, 0, 0 },
        { "identify_hidden", "identify", ProjectionShape::Bolt, 0, kProjHidden },
        { "genocide_hidden", "genocide", ProjectionShape::Bolt, 0, kProjHidden },
        { "e_genocide_hidden", "e_genocide", ProjectionShape::Bolt, 0, kProjHidden },
        { "hungry_hidden", "hungry", ProjectionShape::Bolt, 0, kProjHidden },
        // 形違い
        { "kill_wall_beam", "kill_wall", ProjectionShape::Beam, 0, 0 },
        { "make_wall_ball", "make_wall", ProjectionShape::Ball, 2, 0 },
        { "old_sleep_ball", "old_sleep", ProjectionShape::Ball, 2, 0 },
        { "charm_ball", "charm", ProjectionShape::Ball, 2, 0 },
        { "photo_bolt", "photo", ProjectionShape::Bolt, 0, 0 },
        { "engetsu_bolt", "engetsu", ProjectionShape::Bolt, 0, 0 },
        { "psi_drain_ball", "psi_drain", ProjectionShape::Ball, 2, 0 },
        { "old_heal_ball", "old_heal", ProjectionShape::Ball, 2, 0 },
        { "kill_door_breath", "kill_door", ProjectionShape::Breath, 7, 0 },
        { "old_slow_jump", "old_slow", ProjectionShape::Jump, 1, 0 },
    };
    return scenes;
}

} // namespace hd2d
