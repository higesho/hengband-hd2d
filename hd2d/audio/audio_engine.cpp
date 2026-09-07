/*!
 * @file audio_engine.cpp
 * @brief `audio_engine.h` の実装（OpenAL Soft ＋ `SDL_LoadWAV`）。
 *
 * ## OpenAL をヘッダへ持ち込まない
 * `audio_engine.h` の `device_` / `context_` が `void *` なのはそのためである。
 * 音の装置を差し替える日（Android で OpenAL が組めない場合。設計 §6 の未決 3）に、
 * 呼び出し側を 1 行も触らずに済む。
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。`hd2d/` は `/execution-charset:utf-8` で組む。
 */
#include "audio/audio_engine.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace hd2d::audio {

namespace {

//! 切り替えにかける秒（クロスフェード）。**無音を作らない**長さとして仮に置いた値。
constexpr float kCrossfadeSeconds = 1.2f;

/*!
 * @brief 層の音量が端から端まで動くのにかける秒。
 * @details **ベッドより長い。**ベッドは「別の場所へ来た」ことを知らせる切り替えだが、
 * 層は「風が強くなってきた」ように**変わったと気づかせない**のが狙いなので、
 * ゆっくり動かす。1 歩ごとに目標が動いてもここで均される＝寸断が起きない。
 */
constexpr float kLayerFadeSeconds = 3.0f;
/*!
 * 効果音の源を置く半径（マス）。**距離の意味は無い**——減衰は道のりから自分で作るので、
 * ここは「向きが安定して出る程度に離す」だけの数である。近すぎると HRTF が暴れる。
 */
constexpr float kSfxRadius = 3.f;

//! `distance_gain` の基準（この歩数で半分）。**仮の値**（実際に遊んで詰める）。
constexpr float kGainReferenceSteps = 5.f;

/*!
 * @brief wav を読んで OpenAL のバッファにする。@return バッファ（0 なら失敗）
 * @details `SDL_LoadWAV` は PCM8/16 のほか float や ADPCM も返しうるので、
 * **S16 単耳／両耳へ揃えてから**渡す（`SDL_ConvertAudio`）。揃えないと
 * OpenAL には渡せる形が無く、鳴らないか雑音になる。
 */
ALuint load_wav_buffer(const std::string &path, std::string *why)
{
    SDL_AudioSpec spec{};
    Uint8 *data = nullptr;
    Uint32 length = 0;
    if (SDL_LoadWAV(path.c_str(), &spec, &data, &length) == nullptr) {
        if (why != nullptr) {
            *why = SDL_GetError();
        }
        return 0;
    }
    const int channels = (spec.channels >= 2) ? 2 : 1; //!< 3 本以上は両耳へ畳む
    std::vector<Uint8> converted;
    const Uint8 *pcm = data;
    Uint32 pcm_len = length;
    if ((spec.format != AUDIO_S16SYS) || (spec.channels != channels)) {
        SDL_AudioCVT cvt{};
        const int built = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
            AUDIO_S16SYS, static_cast<Uint8>(channels), spec.freq);
        if (built < 0) {
            if (why != nullptr) {
                *why = SDL_GetError();
            }
            SDL_FreeWAV(data);
            return 0;
        }
        if (built > 0) {
            converted.resize(static_cast<std::size_t>(length) * static_cast<std::size_t>(cvt.len_mult));
            std::memcpy(converted.data(), data, length);
            cvt.buf = converted.data();
            cvt.len = static_cast<int>(length);
            if (SDL_ConvertAudio(&cvt) != 0) {
                if (why != nullptr) {
                    *why = SDL_GetError();
                }
                SDL_FreeWAV(data);
                return 0;
            }
            pcm = converted.data();
            pcm_len = static_cast<Uint32>(cvt.len_cvt);
        }
    }
    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, (channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, pcm,
        static_cast<ALsizei>(pcm_len), spec.freq);
    SDL_FreeWAV(data);
    if (alGetError() != AL_NO_ERROR) {
        if (why != nullptr) {
            *why = "alBufferData failed";
        }
        alDeleteBuffers(1, &buffer);
        return 0;
    }
    return buffer;
}

} // namespace

ListenerOffset to_listener_space(float dx, float dy, float dz, float fx, float fy, float fz,
    float ux, float uy, float uz)
{
    //! 右 ＝ 前 × 上（このツリーの世界は x=列・y=行・z=高さ）。
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
    return kGainReferenceSteps / (kGainReferenceSteps + steps);
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

AudioEngine::~AudioEngine()
{
    this->close();
}

bool AudioEngine::open(std::string *log)
{
    if (this->device_ != nullptr) {
        return true;
    }
    ALCdevice *device = alcOpenDevice(nullptr);
    if (device == nullptr) {
        if (log != nullptr) {
            *log = "音の装置を開けません（環境音は鳴りません）";
        }
        return false;
    }
    /*
     * **HRTF を要求する。**効かなくても開く——効いたかは `hrtf_enabled()` で申告る。
     * P1 のベッドは頭に張り付くので HRTF は効かないが、**P2 で定位を入れる前に
     * 「この機で HRTF が立つか」を知っておきたい**（立たないと設計の前提が崩れる）。
     */
    const ALCint attrs[] = { ALC_HRTF_SOFT, ALC_TRUE, 0 };
    ALCcontext *context = alcCreateContext(device, attrs);
    if ((context == nullptr) || (alcMakeContextCurrent(context) == ALC_FALSE)) {
        if (context != nullptr) {
            alcDestroyContext(context);
        }
        alcCloseDevice(device);
        if (log != nullptr) {
            *log = "音の文脈を作れません（環境音は鳴りません）";
        }
        return false;
    }
    this->device_ = device;
    this->context_ = context;
    ALCint hrtf = ALC_FALSE;
    alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtf);
    this->hrtf_ = (hrtf == ALC_TRUE);
    if (log != nullptr) {
        std::ostringstream note;
        const ALCchar *name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
        note << "音の装置: " << ((name != nullptr) ? name : "(名前なし)")
             << " / HRTF " << (this->hrtf_ ? "有効" : "無効");
        *log = note.str();
    }
    return true;
}

void AudioEngine::close()
{
    if (this->device_ == nullptr) {
        return;
    }
    this->stop_all_voices();
    for (unsigned int &source : this->sfx_sources_) {
        if (source != 0) {
            alSourceStop(source);
            alDeleteSources(1, &source);
            source = 0;
        }
    }
    for (auto &[name, voice] : this->layers_) {
        (void)name;
        if (voice.source != 0) {
            alSourceStop(voice.source);
            alDeleteSources(1, &voice.source);
            alDeleteBuffers(1, &voice.buffer);
        }
    }
    this->layers_.clear();
    for (auto &[path, buffer] : this->sfx_cache_) {
        (void)path;
        if (buffer != 0) {
            alDeleteBuffers(1, &buffer);
        }
    }
    this->sfx_cache_.clear();
    alcMakeContextCurrent(nullptr);
    if (this->context_ != nullptr) {
        alcDestroyContext(static_cast<ALCcontext *>(this->context_));
        this->context_ = nullptr;
    }
    alcCloseDevice(static_cast<ALCdevice *>(this->device_));
    this->device_ = nullptr;
    this->hrtf_ = false;
    this->bed_name_.clear();
}

void AudioEngine::set_listener(const float pos[3], const float forward[3], const float up[3])
{
    if (this->device_ == nullptr) {
        return;
    }
    alListener3f(AL_POSITION, pos[0], pos[1], pos[2]);
    const ALfloat orient[6] = { forward[0], forward[1], forward[2], up[0], up[1], up[2] };
    alListenerfv(AL_ORIENTATION, orient);
}

void AudioEngine::set_sfx_gain(float gain)
{
    this->sfx_gain_ = std::clamp(gain, 0.f, 1.f);
}

bool AudioEngine::play_sfx(const std::string &path, const ListenerOffset &at, float gain)
{
    if ((this->device_ == nullptr) || path.empty() || (this->sfx_gain_ <= 0.f)) {
        return false;
    }
    //! 貯めから引く。無ければ読んで貯める（**同じ音を毎回読み直さない**）。
    ALuint buffer = 0;
    if (const auto it = this->sfx_cache_.find(path); it != this->sfx_cache_.end()) {
        buffer = it->second;
    } else {
        std::string why;
        buffer = load_wav_buffer(path, &why);
        if (buffer == 0) {
            std::fprintf(stderr, "[hd2d][audio] %s を読めません: %s\n", path.c_str(), why.c_str());
            //! **0 を貯める**——読めない音を毎回読みに行かないため。
            this->sfx_cache_[path] = 0;
            return false;
        }
        this->sfx_cache_[path] = buffer;
    }
    if (buffer == 0) {
        return false;
    }
    //! 空いている源を探す。全部鳴っていたら**いちばん古い枠を奪う**（音を落とさない）。
    ALuint source = 0;
    int slot = -1;
    for (int i = 0; i < kSfxVoices; ++i) {
        if (this->sfx_sources_[i] == 0) {
            alGenSources(1, &source);
            if (alGetError() != AL_NO_ERROR) {
                return false;
            }
            this->sfx_sources_[i] = source;
            slot = i;
            break;
        }
        ALint state = 0;
        alGetSourcei(this->sfx_sources_[i], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            source = this->sfx_sources_[i];
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        source = this->sfx_sources_[0];
        alSourceStop(source);
        slot = 0;
    }
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcei(source, AL_LOOPING, AL_FALSE);
    /*
     * **聞き手の空間で置く。**`AL_SOURCE_RELATIVE` なら位置は聞き手から見た座標
     * （+x 右・+y 上・**−z 前**）なので、こちらで向きを解いてから渡せば、
     * 世界の座標系と OpenAL の手系の食い違いを踏まない。
     */
    const float len = std::sqrt((at.right * at.right) + (at.up * at.up) + (at.forward * at.forward));
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    if (len < 0.001f) {
        alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f); //!< 頭で鳴る（位置の無い音）
    } else {
        const float k = kSfxRadius / len;
        alSource3f(source, AL_POSITION, at.right * k, at.up * k, -at.forward * k);
    }
    //! **距離の減衰は掛けさせない**（道のりで減衰させるのはこちらの仕事。設計 §4.1）。
    alSourcef(source, AL_ROLLOFF_FACTOR, 0.f);
    alSourcef(source, AL_GAIN, std::clamp(gain, 0.f, 1.f) * this->sfx_gain_);
    alSourcePlay(source);
    return true;
}

void AudioEngine::set_bed_gain(float gain)
{
    this->gain_ = std::clamp(gain, 0.f, 1.f);
    if (this->device_ == nullptr) {
        return;
    }
    //! 鳴っている源へ**その場で**掛け直す（次の切り替えまで待たせない）。
    for (VoiceSlot &voice : this->voices_) {
        if (voice.used) {
            alSourcef(voice.source, AL_GAIN, voice.gain * this->gain_);
        }
    }
}

bool AudioEngine::start_voice(unsigned int buffer, bool loop)
{
    //! 空いている枠を探す。**無ければいちばん静かなほうを潰す**（無音を作らない）。
    VoiceSlot *slot = nullptr;
    for (VoiceSlot &voice : this->voices_) {
        if (!voice.used) {
            slot = &voice;
            break;
        }
    }
    if (slot == nullptr) {
        slot = (this->voices_[0].gain <= this->voices_[1].gain) ? &this->voices_[0] : &this->voices_[1];
        alSourceStop(slot->source);
        alDeleteSources(1, &slot->source);
        alDeleteBuffers(1, &slot->buffer);
        *slot = VoiceSlot{};
    }
    ALuint source = 0;
    alGenSources(1, &source);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buffer);
        return false;
    }
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    /*
     * **頭に張り付ける。**環境音は方向を持たないので、聞き手の姿勢で揺らしてはいけない
     * （首を振るたびに風の音が回る）。`AL_SOURCE_RELATIVE` ＋ 原点で減衰も掛からない。
     */
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f);
    alSourcef(source, AL_GAIN, 0.f); //!< 0 から上げる（`update` がフェードする）
    alSourcePlay(source);
    slot->source = source;
    slot->buffer = buffer;
    slot->gain = 0.f;
    slot->target = 1.f;
    slot->used = true;
    //! 前からあるものは下げる（＝これがクロスフェード）。
    for (VoiceSlot &voice : this->voices_) {
        if (voice.used && (&voice != slot)) {
            voice.target = 0.f;
        }
    }
    return true;
}

void AudioEngine::stop_all_voices()
{
    for (VoiceSlot &voice : this->voices_) {
        if (!voice.used) {
            continue;
        }
        alSourceStop(voice.source);
        alDeleteSources(1, &voice.source);
        alDeleteBuffers(1, &voice.buffer);
        voice = VoiceSlot{};
    }
}

bool AudioEngine::play_bed(const std::string &name, const std::string &dir)
{
    if (this->device_ == nullptr) {
        return false;
    }
    if (name == this->bed_name_) {
        return false; //!< 同じベッド。鳴らし直すと頭へ戻る
    }
    this->bed_name_ = name;
    if (name.empty()) {
        //! 止める。**その場で切らずに**下げていく（無音への段差を作らない）。
        for (VoiceSlot &voice : this->voices_) {
            voice.target = 0.f;
        }
        return false;
    }
    const std::string sep = (dir.empty() || (dir.back() == '/') || (dir.back() == '\\')) ? "" : "/";
    const std::string path = dir + sep + name + ".wav";
    std::string why;
    const ALuint buffer = load_wav_buffer(path, &why);
    if (buffer == 0) {
        /*
         * **名前は覚えたまま**にする（上で入れてある）。覚えないと、素材が無いベッドを
         * 毎フレーム読みに行って、そのたびにディスクを叩く。
         */
        std::fprintf(stderr, "[hd2d][audio] %s を読めません: %s\n", path.c_str(), why.c_str());
        for (VoiceSlot &voice : this->voices_) {
            voice.target = 0.f;
        }
        return false;
    }
    if (!this->start_voice(buffer, true)) {
        std::fprintf(stderr, "[hd2d][audio] 源を作れません（%s）\n", path.c_str());
        return false;
    }
    return true;
}

float AudioEngine::layer_gain(const std::string &name) const
{
    const auto it = this->layers_.find(name);
    return (it == this->layers_.end()) ? 0.f : it->second.gain;
}

bool AudioEngine::set_layer(const std::string &name, const std::string &dir, float target)
{
    if ((this->device_ == nullptr) || name.empty()) {
        return false;
    }
    target = std::clamp(target, 0.f, 1.f);
    if (const auto it = this->layers_.find(name); it != this->layers_.end()) {
        it->second.target = target; //!< **鳴らし直さない。**音量だけ動かす
        return false;
    }
    if (target <= 0.f) {
        return false; //!< 鳴らす気の無い層のために wav を読みに行かない
    }
    const std::string sep = (dir.empty() || (dir.back() == '/') || (dir.back() == '\\')) ? "" : "/";
    const std::string path = dir + sep + name + ".wav";
    std::string why;
    const ALuint buffer = load_wav_buffer(path, &why);
    if (buffer == 0) {
        std::fprintf(stderr, "[hd2d][audio] 層 %s を読めません: %s\n", path.c_str(), why.c_str());
        //! **枠だけ作って覚える**——読めない層を毎フレーム読みに行かないため。
        LayerVoice dead;
        dead.missing = true;
        this->layers_[name] = dead;
        return false;
    }
    ALuint source = 0;
    alGenSources(1, &source);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buffer);
        return false;
    }
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcei(source, AL_LOOPING, AL_TRUE);
    //! 層も頭に張り付ける（ベッドと同じ理由。首を振っても回らない）。
    alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(source, AL_POSITION, 0.f, 0.f, 0.f);
    alSourcef(source, AL_GAIN, 0.f); //!< 0 から上げる
    alSourcePlay(source);
    LayerVoice voice;
    voice.source = source;
    voice.buffer = buffer;
    voice.gain = 0.f;
    voice.target = target;
    this->layers_[name] = voice;
    return true;
}

void AudioEngine::fade_out_layers_except(const std::vector<std::string> &keep)
{
    for (auto &[name, voice] : this->layers_) {
        const bool wanted = std::find(keep.begin(), keep.end(), name) != keep.end();
        if (!wanted) {
            voice.target = 0.f;
        }
    }
}

bool AudioEngine::play_test_pcm(const short *pcm, std::size_t frames, int rate)
{
    if ((this->device_ == nullptr) || (pcm == nullptr) || (frames == 0)) {
        return false;
    }
    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, pcm,
        static_cast<ALsizei>(frames * sizeof(short)), rate);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &buffer);
        return false;
    }
    return this->start_voice(buffer, false);
}

void AudioEngine::update(float dt_s)
{
    if (this->device_ == nullptr) {
        return;
    }
    const float step = (kCrossfadeSeconds > 0.f) ? (dt_s / kCrossfadeSeconds) : 1.f;
    for (VoiceSlot &voice : this->voices_) {
        if (!voice.used) {
            continue;
        }
        if (voice.gain < voice.target) {
            voice.gain = std::min(voice.target, voice.gain + step);
        } else if (voice.gain > voice.target) {
            voice.gain = std::max(voice.target, voice.gain - step);
        }
        alSourcef(voice.source, AL_GAIN, voice.gain * this->gain_);
        //! 下げ切ったもの、鳴り終わったもの（ループでない検査音）は片付ける。
        ALint state = 0;
        alGetSourcei(voice.source, AL_SOURCE_STATE, &state);
        const bool faded_out = (voice.target <= 0.f) && (voice.gain <= 0.f);
        if (faded_out || (state == AL_STOPPED)) {
            alSourceStop(voice.source);
            alDeleteSources(1, &voice.source);
            alDeleteBuffers(1, &voice.buffer);
            voice = VoiceSlot{};
        }
    }

    /*
     * ---- 重ねる層（§8.2）----
     *
     * **ベッドより遅く動かす。**ベッドは 1.2 秒で入れ替わるが、層は「風が強くなってきた」
     * ように**変わったと気づかせない**のが狙いなので、こちらのほうが長い。
     */
    const float layer_step = (kLayerFadeSeconds > 0.f) ? (dt_s / kLayerFadeSeconds) : 1.f;
    for (auto it = this->layers_.begin(); it != this->layers_.end();) {
        LayerVoice &voice = it->second;
        if (voice.missing) {
            ++it; //!< 読めなかった層。**枠だけ残して二度と読みに行かない**
            continue;
        }
        if (voice.gain < voice.target) {
            voice.gain = std::min(voice.target, voice.gain + layer_step);
        } else if (voice.gain > voice.target) {
            voice.gain = std::max(voice.target, voice.gain - layer_step);
        }
        alSourcef(voice.source, AL_GAIN, voice.gain * this->gain_);
        if ((voice.target <= 0.f) && (voice.gain <= 0.f)) {
            alSourceStop(voice.source);
            alDeleteSources(1, &voice.source);
            alDeleteBuffers(1, &voice.buffer);
            it = this->layers_.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace hd2d::audio
