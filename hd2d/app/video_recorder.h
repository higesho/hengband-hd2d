/*!
 * @file video_recorder.h
 * @brief 画面内録画（`HD2D_RECORD=<出力.mp4>`）。トレイラー用。
 *
 * @details 描き上がったフレーム（swap の直前）を `glReadPixels` で読み、ffmpeg の標準入力へ
 * raw RGB24 で流して H.264（yuv420p, crf 16）の mp4 にする。
 * 環境変数:
 * - `HD2D_RECORD`: 出力ファイル。設定されていれば録画する。
 * - `HD2D_RECORD_FFMPEG`: ffmpeg の場所（無ければ PATH の `ffmpeg`）。
 * - `HD2D_RECORD_FPS`: コマ数（既定 30）。
 * - `HD2D_RECORD_SECONDS`: この秒数で窓を閉じる（検査・自動撮影用。無ければ手で閉じるまで）。
 * 動画の長さが実時間に合うよう、起動からの経過時間で「いま何コマ目か」を計算し、
 * 遅れた分は同じ画を複製して書き、速すぎる分は書かない。
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace hd2d {

class VideoRecorder {
public:
    VideoRecorder() = default;
    ~VideoRecorder();
    VideoRecorder(const VideoRecorder &) = delete;
    VideoRecorder &operator=(const VideoRecorder &) = delete;

    /*! @brief 環境変数を読み、`HD2D_RECORD` があれば ffmpeg を起こす。無ければ何もしない。 */
    void start_from_env();

    /*! @brief 録画中か。 */
    bool active() const { return pending_ || (pipe_ != nullptr); }

    /*!
     * @brief 描き上がったフレームを 1 枚渡す（swap の直前に呼ぶ）。録画中でなければ即戻る。
     * @details `HD2D_RECORD_SECONDS` を過ぎていたら `SDL_QUIT` を投げる。
     */
    void capture(int width, int height);

    /*! @brief ffmpeg を閉じて mp4 を完成させる。何度呼んでもよい。 */
    void finish();

private:
    std::FILE *pipe_{ nullptr };
    int width_{ 0 };
    int height_{ 0 };
    int fps_{ 30 };
    double max_seconds_{ 0.0 };
    std::uint64_t t0_{ 0 };
    std::uint64_t written_{ 0 };
    bool quit_sent_{ false };
    bool failed_{ false };
    bool pending_{ false };
    std::string out_path_;
    std::string ffmpeg_path_;
};

} // namespace hd2d
