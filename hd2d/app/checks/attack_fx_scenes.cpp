/*!
 * @file attack_fx_scenes.cpp
 * @brief 検査の場面の表の共通部分（4 つの系統をつなぐ・名前から引く）。見た目を作る担当は触らない。
 */
#include "app/checks/attack_fx_scenes.h"

namespace hd2d {

std::vector<FxCheckScene> fx_scenes_all()
{
    std::vector<FxCheckScene> all;
    for (const std::vector<FxCheckScene> *family : { &fx_scenes_liquid(), &fx_scenes_lightning(), &fx_scenes_elemental(), &fx_scenes_spirit() }) {
        all.insert(all.end(), family->begin(), family->end());
    }
    return all;
}

std::vector<FxCheckScene> fx_scenes_of_family(const std::string &family)
{
    if (family == "liquid") {
        return fx_scenes_liquid();
    }
    if (family == "lightning") {
        return fx_scenes_lightning();
    }
    if (family == "elemental") {
        return fx_scenes_elemental();
    }
    if (family == "spirit") {
        return fx_scenes_spirit();
    }
    return {};
}

bool fx_parse_shape(const std::string &name, ProjectionShape &shape, std::uint16_t &flags)
{
    flags = 0;
    if (name == "bolt") {
        shape = ProjectionShape::Bolt;
    } else if (name == "beam") {
        shape = ProjectionShape::Beam;
    } else if (name == "ball") {
        shape = ProjectionShape::Ball;
    } else if (name == "breath") {
        shape = ProjectionShape::Breath;
    } else if (name == "jump") {
        shape = ProjectionShape::Jump;
    } else if (name == "touch") {
        shape = ProjectionShape::Touch;
    } else if (name == "hidden") {
        shape = ProjectionShape::Bolt;
        flags = kProjHidden;
    } else if (name == "allsight") {
        shape = ProjectionShape::Bolt;
        flags = kProjAllInSight;
    } else {
        return false;
    }
    return true;
}

} // namespace hd2d
