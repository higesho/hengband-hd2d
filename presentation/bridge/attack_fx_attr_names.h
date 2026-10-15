/*!
 * @file attack_fx_attr_names.h
 * @brief 属性（`AttributeType`）の番号を、画面へ運ぶ名前（小文字）に直す表（frame/attack_fx.h）
 * @details `effect/attribute-types.h` の列挙子の名前を小文字にしたもの。変愚系（変愚・短愚）の
 * 列挙は同じ（3.0.2.4 と 3.0.2.5 で差が無いことを確認済み）。**列挙子を足す版へ上げたら足す**。
 * 表に無い番号は空の名前を返す（画面側は汎用の見た目に落ちる）。
 */
#pragma once

#include "effect/attribute-types.h"

#include <string>

namespace attack_fx {

//! 列挙子と名前の対（並びは `effect/attribute-types.h` の宣言の順）。
struct AttributeNamePair {
    AttributeType type;
    const char *name;
};

inline constexpr AttributeNamePair kAttributeNames[] = {
    { AttributeType::NONE, "none" },
    { AttributeType::ELEC, "elec" },
    { AttributeType::POIS, "pois" },
    { AttributeType::ACID, "acid" },
    { AttributeType::COLD, "cold" },
    { AttributeType::FIRE, "fire" },
    { AttributeType::PSY_SPEAR, "psy_spear" },
    { AttributeType::MISSILE, "missile" },
    { AttributeType::DEBUG, "debug" },
    { AttributeType::PLASMA, "plasma" },
    { AttributeType::WATER, "water" },
    { AttributeType::LITE, "lite" },
    { AttributeType::DARK, "dark" },
    { AttributeType::LITE_WEAK, "lite_weak" },
    { AttributeType::DARK_WEAK, "dark_weak" },
    { AttributeType::SHARDS, "shards" },
    { AttributeType::SOUND, "sound" },
    { AttributeType::CONFUSION, "confusion" },
    { AttributeType::FORCE, "force" },
    { AttributeType::INERTIAL, "inertial" },
    { AttributeType::MANA, "mana" },
    { AttributeType::METEOR, "meteor" },
    { AttributeType::ICE, "ice" },
    { AttributeType::CHAOS, "chaos" },
    { AttributeType::NETHER, "nether" },
    { AttributeType::DISENCHANT, "disenchant" },
    { AttributeType::NEXUS, "nexus" },
    { AttributeType::TIME, "time" },
    { AttributeType::GRAVITY, "gravity" },
    { AttributeType::KILL_WALL, "kill_wall" },
    { AttributeType::KILL_DOOR, "kill_door" },
    { AttributeType::KILL_TRAP, "kill_trap" },
    { AttributeType::MAKE_WALL, "make_wall" },
    { AttributeType::MAKE_DOOR, "make_door" },
    { AttributeType::MAKE_TRAP, "make_trap" },
    { AttributeType::MAKE_TREE, "make_tree" },
    { AttributeType::OLD_CLONE, "old_clone" },
    { AttributeType::OLD_POLY, "old_poly" },
    { AttributeType::OLD_HEAL, "old_heal" },
    { AttributeType::OLD_SPEED, "old_speed" },
    { AttributeType::OLD_SLOW, "old_slow" },
    { AttributeType::OLD_CONF, "old_conf" },
    { AttributeType::OLD_SLEEP, "old_sleep" },
    { AttributeType::HYPODYNAMIA, "hypodynamia" },
    { AttributeType::AWAY_UNDEAD, "away_undead" },
    { AttributeType::AWAY_EVIL, "away_evil" },
    { AttributeType::AWAY_ALL, "away_all" },
    { AttributeType::TURN_UNDEAD, "turn_undead" },
    { AttributeType::TURN_EVIL, "turn_evil" },
    { AttributeType::TURN_ALL, "turn_all" },
    { AttributeType::DISP_UNDEAD, "disp_undead" },
    { AttributeType::DISP_EVIL, "disp_evil" },
    { AttributeType::DISP_ALL, "disp_all" },
    { AttributeType::DISP_DEMON, "disp_demon" },
    { AttributeType::DISP_LIVING, "disp_living" },
    { AttributeType::ROCKET, "rocket" },
    { AttributeType::NUKE, "nuke" },
    { AttributeType::MAKE_RUNE_PROTECTION, "make_rune_protection" },
    { AttributeType::STASIS, "stasis" },
    { AttributeType::STONE_WALL, "stone_wall" },
    { AttributeType::DEATH_RAY, "death_ray" },
    { AttributeType::STUN, "stun" },
    { AttributeType::HOLY_FIRE, "holy_fire" },
    { AttributeType::HELL_FIRE, "hell_fire" },
    { AttributeType::DISINTEGRATE, "disintegrate" },
    { AttributeType::CHARM, "charm" },
    { AttributeType::CONTROL_UNDEAD, "control_undead" },
    { AttributeType::CONTROL_ANIMAL, "control_animal" },
    { AttributeType::PSI, "psi" },
    { AttributeType::PSI_DRAIN, "psi_drain" },
    { AttributeType::TELEKINESIS, "telekinesis" },
    { AttributeType::JAM_DOOR, "jam_door" },
    { AttributeType::DOMINATION, "domination" },
    { AttributeType::DISP_GOOD, "disp_good" },
    { AttributeType::DRAIN_MANA, "drain_mana" },
    { AttributeType::MIND_BLAST, "mind_blast" },
    { AttributeType::BRAIN_SMASH, "brain_smash" },
    { AttributeType::CAUSE_1, "cause_1" },
    { AttributeType::CAUSE_2, "cause_2" },
    { AttributeType::CAUSE_3, "cause_3" },
    { AttributeType::CAUSE_4, "cause_4" },
    { AttributeType::HAND_DOOM, "hand_doom" },
    { AttributeType::CAPTURE, "capture" },
    { AttributeType::ANIM_DEAD, "anim_dead" },
    { AttributeType::CHARM_LIVING, "charm_living" },
    { AttributeType::IDENTIFY, "identify" },
    { AttributeType::ATTACK, "attack" },
    { AttributeType::ENGETSU, "engetsu" },
    { AttributeType::GENOCIDE, "genocide" },
    { AttributeType::PHOTO, "photo" },
    { AttributeType::CONTROL_DEMON, "control_demon" },
    { AttributeType::LAVA_FLOW, "lava_flow" },
    { AttributeType::BLOOD_CURSE, "blood_curse" },
    { AttributeType::SEEKER, "seeker" },
    { AttributeType::SUPER_RAY, "super_ray" },
    { AttributeType::STAR_HEAL, "star_heal" },
    { AttributeType::WATER_FLOW, "water_flow" },
    { AttributeType::CRUSADE, "crusade" },
    { AttributeType::STASIS_EVIL, "stasis_evil" },
    { AttributeType::WOUNDS, "wounds" },
    { AttributeType::E_GENOCIDE, "e_genocide" },
    { AttributeType::VOID_MAGIC, "void_magic" },
    { AttributeType::ABYSS, "abyss" },
    { AttributeType::HUNGRY, "hungry" },
    { AttributeType::PLAYER_SHOOT, "player_shoot" },
    { AttributeType::PLAYER_MELEE, "player_melee" },
    { AttributeType::MONSTER_SHOOT, "monster_shoot" },
    { AttributeType::MONSTER_MELEE, "monster_melee" },
};

inline std::string attribute_name(int id)
{
    for (const auto &pair : kAttributeNames) {
        if (static_cast<int>(pair.type) == id) {
            return pair.name;
        }
    }
    return std::string();
}

} // namespace attack_fx
