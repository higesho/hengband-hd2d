#pragma once
#include "bridge/native_targeting_api.h"
#include "floor/geometry.h"
#include "target/target.h"
#include <tl/optional.hpp>
class PlayerType;
bool hb_heng_mapped_direction();
Direction hb_heng_choose_direction(PlayerType *,int mode);
Target hb_heng_choose_target(PlayerType *);
tl::optional<Pos2D> hb_heng_choose_point(PlayerType *);
void hb_heng_menu_book_row(PlayerType *,int key,int row,int column,const char *rendered,const char *name,int realm,int spell,bool eligible,int selected);
