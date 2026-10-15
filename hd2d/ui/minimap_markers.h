#pragma once
#include "frame/minimap_snapshot.h"
#include "ui/ui_paint.h"
namespace hd2d {
inline PaintColor minimap_marker_color(MinimapMarker marker,float opacity=1.f) {
    switch(marker) {
    case MinimapMarker::Treasure:return {1.f,.35f,.72f,opacity};
    case MinimapMarker::Item:return {.47f,.82f,1.f,opacity};
    case MinimapMarker::Monster:return {1.f,.18f,.18f,opacity};
    case MinimapMarker::Trap:return {1.f,.72f,.15f,opacity};
    default:return {0,0,0,0};
    }
}
}
