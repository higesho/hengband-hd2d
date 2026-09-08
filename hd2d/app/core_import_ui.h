#pragma once
#include "app/core_import.h"
struct SDL_Window;
namespace hd2d {
class TextOverlay;
class UiPaint;
class GamePad;
struct RectPx;
int core_import_font_size(int width, int height);
RectPx core_import_entry_rect(int width, int height);
std::string core_import_caption(const char *key);
CoreImportConfig core_import_config();
std::filesystem::path imported_core_path(const std::string &filename);
bool run_core_import(SDL_Window *window, TextOverlay &text, UiPaint &paint, const std::string &source_zip = {}, GamePad *pad = nullptr);
}
