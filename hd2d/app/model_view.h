#pragma once

namespace hd2d {
struct AppOptions;
// Opens a core-independent GLB preview. A nonempty shot_path saves a BMP and exits.
int run_model_view(const AppOptions &options);
int run_model_terrain_check(const AppOptions &options);
}
