/*!
 * @file video_recorder.cpp
 * @brief `video_recorder.h` の実装。
 */
#include "app/video_recorder.h"

#include "render/gl_core.h"

#include <SDL2/SDL.h>

#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define HD2D_POPEN _popen
#define HD2D_PCLOSE _pclose
#else
#define HD2D_POPEN popen
#define HD2D_PCLOSE pclose
#endif

namespace hd2d {

using namespace gl;

//! gl_core.h に無い定数（GL 1.0 の glPixelStorei 用）。
constexpr GLenum kGlPackAlignment = 0x0D05;

VideoRecorder::~VideoRecorder()
{
    finish();
}

void VideoRecorder::start_from_env()
{
    const char *const out = std::getenv("HD2D_RECORD");
    if ((out == nullptr) || (*out == 0)) return;
    if (const char *fps = std::getenv("HD2D_RECORD_FPS"); (fps != nullptr) && (std::atoi(fps) > 0)) {
        fps_ = std::atoi(fps);
    }
    if (const char *sec = std::getenv("HD2D_RECORD_SECONDS"); (sec != nullptr) && (std::atof(sec) > 0.0)) {
        max_seconds_ = std::atof(sec);
    }
    const char *ff = std::getenv("HD2D_RECORD_FFMPEG");
    const std::string ffmpeg = ((ff != nullptr) && (*ff != 0)) ? ff : "ffmpeg";
    // 幅・高さは最初のフレームで決まるので、起こすのは capture() の最初。ここでは出力先だけ覚える。
    out_path_ = out;
    ffmpeg_path_ = ffmpeg;
    pending_ = true;
}

void VideoRecorder::capture(int width, int height)
{
    if (!pending_ && (pipe_ == nullptr)) return;
    if (failed_ || (width <= 0) || (height <= 0)) return;
    if (pending_) {
        pending_ = false;
        width_ = width;
        height_ = height;
        // cmd.exe は先頭と末尾の引用符を剥がすので、全体をもう 1 組の引用符で包む。
        std::string cmd = "\"\"" + ffmpeg_path_ + "\" -y -loglevel error -f rawvideo -pix_fmt rgb24 -s "
            + std::to_string(width) + "x" + std::to_string(height) + " -r " + std::to_string(fps_)
            + " -i - -vf \"vflip,pad=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v libx264 -preset veryfast -crf 16"
            + " -pix_fmt yuv420p \"" + out_path_ + "\"\"";
        pipe_ = HD2D_POPEN(cmd.c_str(), "wb");
        if (pipe_ == nullptr) {
            std::fprintf(stderr, "[hd2d] record: ffmpeg を起こせません\n");
            failed_ = true;
            return;
        }
        t0_ = SDL_GetPerformanceCounter();
        std::fprintf(stderr, "[hd2d] record: %s %dx%d %dfps\n", out_path_.c_str(), width, height, fps_);
    }
    if (pipe_ == nullptr) return;
    const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - t0_)
        / static_cast<double>(SDL_GetPerformanceFrequency());
    if ((max_seconds_ > 0.0) && (elapsed >= max_seconds_) && !quit_sent_) {
        quit_sent_ = true;
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
    if ((width != width_) || (height != height_)) return; // 窓の大きさが変わったコマは捨てる
    const std::uint64_t target = static_cast<std::uint64_t>(elapsed * fps_) + 1;
    if (target <= written_) return; // 速すぎる分は書かない
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    glPixelStorei(kGlPackAlignment, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    for (; written_ < target; ++written_) {
        if (std::fwrite(pixels.data(), 1, pixels.size(), pipe_) != pixels.size()) {
            std::fprintf(stderr, "[hd2d] record: 書き込みに失敗しました\n");
            failed_ = true;
            finish();
            return;
        }
    }
}

void VideoRecorder::finish()
{
    pending_ = false;
    if (pipe_ != nullptr) {
        std::fflush(pipe_);
        HD2D_PCLOSE(pipe_);
        pipe_ = nullptr;
        std::fprintf(stderr, "[hd2d] record: 終了（%llu コマ）\n", static_cast<unsigned long long>(written_));
    }
}

} // namespace hd2d
