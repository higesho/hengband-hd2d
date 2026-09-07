/*!
 * @file audio_engine_null.cpp
 * @brief 音の装置が無い組み向けの `AudioEngine`（**すべて何もしない**）。
 *
 * ## なぜ 2 本あるのか
 * OpenAL は Windows では vcpkg から来るが、**Android / Quest には同じ道が無い**
 * 音の設計には未決が残っている。あちらをどうするかが決まるまで、
 * 呼び出し側（`hd2d_app.cpp`）に `#if` を書かずに済ませるための空実装である。
 * `hd2d/xr/xr_session.cpp` のスタブと同じ考え方——**組む木を差し替える**。
 *
 * | 組 | 組む TU |
 * |---|---|
 * | Windows（`HengbandHd2d.vcxproj`） | `audio_engine.cpp`（OpenAL Soft） |
 * | Android / Quest（`android/hd2d/.../CMakeLists.txt`） | **こちら** |
 *
 * `open()` が偽を返すだけで、以降の呼び出しはすべて無害に落ちる。
 * 音が鳴らないこと以外は、遊びに 1 つも影響しない。
 */
#include "audio/audio_engine.h"

#include <algorithm>
#include <cmath>

namespace hd2d::audio {

AudioEngine::~AudioEngine() = default;

bool AudioEngine::open(std::string *log)
{
    if (log != nullptr) {
        *log = "この組に音の装置はありません";
    }
    return false;
}

void AudioEngine::close() {}

void AudioEngine::set_listener(const float[3], const float[3], const float[3]) {}

void AudioEngine::set_bed_gain(float gain)
{
    this->gain_ = gain;
}

void AudioEngine::set_sfx_gain(float gain)
{
    this->sfx_gain_ = gain;
}

bool AudioEngine::play_sfx(const std::string &, const ListenerOffset &, float)
{
    return false;
}

/*
 * **向きと減衰の計算は空実装の側にも要る。**あちらは純関数で、音の装置とは無関係だから
 * ——`--ui-check` は Android でも同じ数字を出せなければならない。
 */
ListenerOffset to_listener_space(float dx, float dy, float dz, float fx, float fy, float fz,
    float ux, float uy, float uz)
{
    //! **右 ＝ 上 × 前**（`Camera::azimuth()` と同じ順。逆に書くと左右が入れ替わる）。
    const float rx = (uy * fz) - (uz * fy);
    const float ry = (uz * fx) - (ux * fz);
    const float rz = (ux * fy) - (uy * fx);
    ListenerOffset out;
    out.right = (dx * rx) + (dy * ry) + (dz * rz);
    out.up = (dx * ux) + (dy * uy) + (dz * uz);
    out.forward = (dx * fx) + (dy * fy) + (dz * fz);
    return out;
}

float distance_gain(int path_steps)
{
    const float steps = (path_steps > 0) ? static_cast<float>(path_steps) : 0.f;
    return 5.f / (5.f + steps);
}

SoundPlacement place_sound(const SoundEvent &event, float player_x, float player_y)
{
    const bool has_corner = (event.heard_y != 0) || (event.heard_x != 0);
    const float sy = static_cast<float>(has_corner ? event.heard_y : event.y);
    const float sx = static_cast<float>(has_corner ? event.heard_x : event.x);
    SoundPlacement out;
    out.dx = sx - player_x;
    out.dy = sy - player_y;
    out.steps = (event.path > 0)
        ? static_cast<int>(event.path)
        : static_cast<int>(std::lround(std::max(std::fabs(out.dx), std::fabs(out.dy))));
    return out;
}

bool AudioEngine::start_voice(unsigned int, bool)
{
    return false;
}

void AudioEngine::stop_all_voices() {}

bool AudioEngine::play_bed(const std::string &name, const std::string &)
{
    this->bed_name_ = name; //!< 覚えるだけ（同じ名前で読み直しに行かない道は同じにしておく）
    return false;
}

bool AudioEngine::play_test_pcm(const short *, std::size_t, int)
{
    return false;
}

/*
 * 環境音の層（`ambience_mix`）。**目録は読むが音は出さない。**
 * `set_layer` が偽を返せば呼び手は「その層は持てなかった」と見なすだけで、
 * 毎フレームの決め直しはそのまま回る（`layers_` は空のまま＝`layer_count()` は 0）。
 */
bool AudioEngine::set_layer(const std::string &, const std::string &, float)
{
    return false;
}

void AudioEngine::fade_out_layers_except(const std::vector<std::string> &) {}

//! 層を 1 つも持たないので常に 0（検査用のオプション。宣言だけ残ると次に呼ばれた側が落ちる）。
float AudioEngine::layer_gain(const std::string &) const
{
    return 0.0f;
}

void AudioEngine::update(float) {}

} // namespace hd2d::audio
