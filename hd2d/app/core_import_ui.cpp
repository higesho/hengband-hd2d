#include "app/core_import_ui.h"
#include "app/test_keyboard.h"
#include "i18n/lang.h"
#include "render/text_overlay.h"
#include "ui/ui_paint.h"
#include "ui/game_pad.h"
#include <SDL2/SDL.h>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <SDL2/SDL_syswm.h>
#elif defined(__ANDROID__)
#include <jni.h>
#endif

namespace hd2d {
using namespace gl;
namespace {
namespace fs = std::filesystem;
std::mutex picker_mutex;
std::string picked_zip, picked_label;
std::string utf8(const fs::path &p) { auto s = p.u8string(); return std::string(reinterpret_cast<const char *>(s.data()), s.size()); }

#if defined(__ANDROID__)
std::string native_library_directory()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    auto cls = env->GetObjectClass(activity);
    auto method = env->GetMethodID(cls, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;");
    auto info = env->CallObjectMethod(activity, method);
    auto info_class = env->GetObjectClass(info);
    auto field = env->GetFieldID(info_class, "nativeLibraryDir", "Ljava/lang/String;");
    auto value = static_cast<jstring>(env->GetObjectField(info, field));
    const char *bytes = env->GetStringUTFChars(value, nullptr);
    std::string result(bytes); env->ReleaseStringUTFChars(value, bytes);
    env->DeleteLocalRef(value); env->DeleteLocalRef(info_class); env->DeleteLocalRef(info);
    env->DeleteLocalRef(cls); env->DeleteLocalRef(activity); return result;
}

#endif

void choose_zip(SDL_Window *window)
{
#if defined(_WIN32)
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog);
    SDL_SysWMinfo wm{}; SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(window, &wm)) dialog.hwndOwner = wm.info.win.window;
    dialog.lpstrFilter = L"Source ZIP (*.zip)\0*.zip\0\0"; dialog.lpstrFile = path; dialog.nMaxFile = 32768;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) { std::lock_guard<std::mutex> lock(picker_mutex); picked_zip = utf8(fs::path(path)); picked_label = utf8(fs::path(path).filename()); }
#elif defined(__ANDROID__)
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    auto cls = env->GetObjectClass(activity);
    auto id = env->GetMethodID(cls, "requestCoreSourceZip", "()V");
    if (id) env->CallVoidMethod(activity, id);
    else { env->ExceptionClear(); std::lock_guard<std::mutex> lock(picker_mutex); picked_zip = "!ZIP picker is not available in this app variant"; }
    env->DeleteLocalRef(cls); env->DeleteLocalRef(activity);
#endif
}
}

#if defined(__ANDROID__)
extern "C" JNIEXPORT void JNICALL Java_org_hengband_hd2d_HengbandHd2dActivity_coreSourceZipSelected(JNIEnv *env, jclass, jstring path, jstring name)
{
    const auto *bytes = env->GetStringUTFChars(path, nullptr);
    const auto *label = env->GetStringUTFChars(name, nullptr);
    { std::lock_guard<std::mutex> lock(picker_mutex); picked_zip = bytes; picked_label = label; }
    env->ReleaseStringUTFChars(path, bytes); env->ReleaseStringUTFChars(name, label);
}
#endif

int core_import_font_size(int width, int height)
{
#if defined(__ANDROID__)
    float dpi = 0;
    const float scale = SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) == 0 && dpi > 0
        ? dpi / 160.f : static_cast<float>(std::min(width, height)) / 400.f;
    return static_cast<int>(16.f * std::clamp(scale, 1.f, 4.f));
#else
    (void)width; (void)height;
    return 16;
#endif
}

RectPx core_import_entry_rect(int width, int height)
{
    const float scale = core_import_font_size(width, height) / 16.f;
    const int margin = static_cast<int>(16 * scale);
    const int h = static_cast<int>(48 * scale);
    return {margin, height - h - margin, std::min(width - margin * 2, static_cast<int>(316 * scale)), h};
}

std::string core_import_caption(const char *key)
{
    std::string value = i18n::tr(key);
#if defined(__ANDROID__)
    const auto hint = value.find(" [");
    if (hint != std::string::npos) value.resize(hint);
#endif
    return value;
}

CoreImportConfig core_import_config()
{
    CoreImportConfig c;
    char *pref = SDL_GetPrefPath("Hengband", "HD2D");
    if (!pref) throw std::runtime_error("Cannot access application storage");
    c.storage = fs::u8path(pref) / "core-import"; SDL_free(pref);
#if defined(_WIN32)
    wchar_t custom[32768]{};
    const auto custom_size = GetEnvironmentVariableW(L"HENGBAND_IMPORT_HOME", custom, 32768);
    if (custom_size > 0 && custom_size < 32768) c.storage = fs::absolute(fs::path(custom));
#else
    if (const auto custom = SDL_getenv("HENGBAND_IMPORT_HOME"); custom && *custom) c.storage = fs::absolute(fs::u8path(custom));
#endif
#if defined(_WIN32)
    char *base = SDL_GetBasePath();
    c.kit = fs::u8path(base ? base : "") / "core-import"; SDL_free(base);
    c.compiler = c.kit / "toolchain/bin/clang++.exe";
    c.linker = c.compiler; c.platform = "windows";
#elif defined(__ANDROID__)
    c.kit = fs::current_path() / "core-import";
    const auto native = fs::u8path(native_library_directory());
    c.compiler = native / "libhbclang.so"; c.linker = native / "libhblink.so";
    c.platform = "android";
#else
    c.platform = "unsupported";
#endif
    return c;
}

fs::path imported_core_path(const std::string &filename)
{
    try {
        const auto config = core_import_config();
        std::ifstream in(config.storage / "registry.json");
        if (!in) return {};
        auto registry = nlohmann::json::parse(in);
        for (const auto &entry : registry) {
            if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string()) continue;
            try {
                auto path = fs::u8path(entry.at("path").get<std::string>());
                if (path.filename() == fs::u8path(filename) && fs::is_regular_file(path)) return path;
            } catch (const std::exception &) { /* 1 件の破損で、ほかのコアを隠さない。 */ }
        }
    } catch (const std::exception &) { }
    return {};
}

bool run_core_import(SDL_Window *window, TextOverlay &base_text, UiPaint &paint, const std::string &source_zip, GamePad *pad)
{
    struct Overlay { TextOverlay value; ~Overlay() { value.shutdown(); } } enlarged;
    int initial_w = 0, initial_h = 0; SDL_GetWindowSize(window, &initial_w, &initial_h);
    const int font_size = core_import_font_size(initial_w, initial_h);
    std::string font_error;
    const bool large = font_size > base_text.cell_h() && enlarged.value.init(font_size, font_error);
    TextOverlay &text = large ? enlarged.value : base_text;
    std::unique_ptr<CoreImport> importer;
    std::vector<ImportTarget> targets;
    std::string error, zip = source_zip, label = source_zip.empty() ? "" : utf8(fs::u8path(source_zip).filename());
    try { importer = std::make_unique<CoreImport>(core_import_config()); targets = importer->targets(); }
    catch (const std::exception &e) { error = e.what(); std::fprintf(stderr, "[core-import] %s\n", e.what()); }
    int selected = 0;
    bool closing = false, changed = false, quit = false;
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    for (;;) {
        auto status = importer ? importer->status() : CoreImportStatus{};
        changed = changed || status.succeeded;
        if (closing && !status.running) { if (quit) { SDL_Event event{}; event.type = SDL_QUIT; SDL_PushEvent(&event); } return changed; }
        { std::lock_guard<std::mutex> lock(picker_mutex); if (!picked_zip.empty()) { if (picked_zip.front() == '!') error = picked_zip.substr(1); else { zip = picked_zip; label = picked_label.empty() ? utf8(fs::u8path(zip).filename()) : picked_label; error.clear(); } picked_zip.clear(); picked_label.clear(); } }
        int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h);
        const float scale = core_import_font_size(w, h) / 16.f;
        const int margin = std::max(24, static_cast<int>(16 * scale));
        const int button_h = static_cast<int>(48 * scale);
        const int list_y = margin + text.cell_h() + margin / 2;
        const int room = (h - list_y - button_h - text.cell_h() * 5 - margin * 3) / std::max(1, static_cast<int>(targets.size()));
        const int row_h = std::max(text.cell_h() + static_cast<int>(8 * scale), std::min(button_h, room));
        const int buttons_y = list_y + row_h * static_cast<int>(targets.size()) + margin;
        const int button_width = (w - margin * 4) / 3;
        const RectPx browse{margin, buttons_y, button_width, button_h};
        const RectPx build{margin * 2 + button_width, buttons_y, button_width, button_h};
        const RectPx cancel{margin * 3 + button_width * 2, buttons_y, button_width, button_h};
        auto hit = [](const RectPx &r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
        auto begin = [&] {
            if (!closing && !status.running && !zip.empty() && importer && !targets.empty()) {
                error.clear();
                try { importer->start(fs::u8path(zip), targets[selected].id); }
                catch (const std::exception &e) { error = e.what(); }
                status = importer->status();
            }
        };
        auto close = [&] { if (status.running) { importer->cancel(); closing = true; } else closing = true; };
        test_keyboard().poll("core-import");
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (pad && pad->on_event(e)) continue;
            if (e.type == SDL_QUIT) { quit = true; close(); }
            if (e.type == SDL_DROPFILE) { if (!status.running) { zip = e.drop.file; label = utf8(fs::u8path(zip).filename()); } SDL_free(e.drop.file); }
            if (e.type == SDL_KEYDOWN) {
                const auto key = e.key.keysym.sym;
                if (key == SDLK_ESCAPE) close();
                if (!status.running && !targets.empty()) {
                    if (key == SDLK_UP) selected = (selected + static_cast<int>(targets.size()) - 1) % static_cast<int>(targets.size());
                    if (key == SDLK_DOWN) selected = (selected + 1) % static_cast<int>(targets.size());
                    if (key == SDLK_o) choose_zip(window);
                    if (key == SDLK_RETURN) { if (zip.empty()) choose_zip(window); else begin(); }
                }
            }
            int x = -1, y = -1;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) { x = e.button.x; y = e.button.y; }
            if (e.type == SDL_FINGERDOWN) { x = static_cast<int>(e.tfinger.x * w); y = static_cast<int>(e.tfinger.y * h); }
            if (x >= 0) {
                if (hit(cancel, x, y)) close();
                if (!status.running) {
                    if (hit(browse, x, y)) choose_zip(window);
                    else if (hit(build, x, y)) begin();
                    else if (y >= list_y && y < list_y + row_h * static_cast<int>(targets.size())) selected = (y - list_y) / row_h;
                }
            }
        }
        if (pad) {
            int dx = 0, dy = 0;
            if (!status.running && !targets.empty() && pad->poll_direction(SDL_GetTicks(), true, dx, dy) && dy) selected = (selected + (dy > 0 ? 1 : static_cast<int>(targets.size()) - 1)) % static_cast<int>(targets.size());
            PadPress press{};
            while (pad->take_pressed(press)) {
                if (press.input == PadInput::B) close();
                if (press.input == PadInput::A && !status.running) { if (zip.empty()) choose_zip(window); else begin(); }
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, w, h); glDisable(GL_DEPTH_TEST);
        glClearColor(.025f, .03f, .055f, 1); glClear(GL_COLOR_BUFFER_BIT);
        paint.begin(w, h);
        if (!targets.empty()) paint.rect({margin, list_y + row_h * selected, w - margin * 2, row_h}, {.15f, .24f, .38f, 1});
        for (auto r : {browse, build, cancel}) paint.rect(r, {.15f, .2f, .3f, 1});
        if (status.total > 0) paint.rect({24, buttons_y + button_h + 12, (w - 48) * status.completed / status.total, 8}, {.3f, .7f, .9f, 1});
        paint.flush(); text.begin(w, h);
        const TextColor white{1, 1, 1, 1};
        text.draw(margin, margin, i18n::tr("hd2d.app.core-import.title"), white, w - 48);
        for (std::size_t i = 0; i < targets.size(); ++i) text.draw(margin + 12, list_y + static_cast<int>(i) * row_h + (row_h - text.cell_h()) / 2, targets[i].name + "  " + targets[i].version, white, w - 72);
        text.draw(browse.x + 10, browse.y + (button_h - text.cell_h()) / 2, core_import_caption("hd2d.app.core-import.choose"), white, browse.w - 20);
        text.draw(build.x + 10, build.y + (button_h - text.cell_h()) / 2, core_import_caption("hd2d.app.core-import.build"), white, build.w - 20);
        text.draw(cancel.x + 10, cancel.y + (button_h - text.cell_h()) / 2, status.running ? (core_import_caption("hd2d.app.core-import.cancel")) : (core_import_caption("hd2d.app.core-import.back")), white, cancel.w - 20);
        int y = buttons_y + button_h + margin;
        text.draw(24, y, zip.empty() ? (i18n::tr("hd2d.app.core-import.source")) : label, white, w - 48);
        const auto state = status.succeeded ? (i18n::tr("hd2d.app.core-import.complete")) : status.running ? (i18n::tr("hd2d.app.core-import.busy")) : (status.phase == "error" || !error.empty()) ? i18n::tr("hd2d.app.core-import.error") : status.phase == "cancelled" ? i18n::tr("hd2d.app.core-import.cancelled") : "";
        text.draw(24, y + text.cell_h() + 8, state, white, w - 48);
        auto detail = !error.empty() ? error : status.message;
        // 長いパス・失敗理由も複数行で表示する。
        for (int line = 0; !detail.empty() && line < 4; ++line) {
            const auto n = text.fit_bytes(detail, w - 48);
            if (!n) break;
            text.draw(24, y + (text.cell_h() + 8) * (2 + line), detail.substr(0, n), white, w - 48); detail.erase(0, n);
        }
        text.flush(); SDL_GL_SwapWindow(window); SDL_Delay(16);
    }
}
}
