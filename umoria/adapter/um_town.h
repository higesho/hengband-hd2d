#pragma once

namespace umoria {

//! 固定の町の地図で、コアでは壁・床として扱うマスを画面へ何として見せるか。
enum class TownVisual { None, Cliff, Water, Tree, Grass, Ruin };

//! 町（地図を読めたとき）でだけ None 以外を返す。
TownVisual town_visual(int y, int x);

} // namespace umoria
