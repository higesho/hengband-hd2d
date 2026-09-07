#pragma once
/*!
 * @file audio_engine.h
 * @brief 画面側で音を鳴らす装置。**P1 は環境音だけ。**
 *
 * ## なぜ画面側なのか
 * 位置で聞き分けるには「音源のマス」「**聞き手の位置と向き**」「HRTF の効くミキサ」の
 * 3 つが要る。**コア側には後ろ 2 つが無い**（カメラを知らない。`PlaySound` にも MCI にも
 * 定位が無い）。だから鳴らす担当をこちらへ移す——2026-08-21 に決めた。
 *
 * ## P1 でできること・できないこと
 * | | |
 * |---|---|
 * | できる | 環境音のベッドを 1 本、ループで鳴らす。場面が変わったら**クロスフェード** |
 * | できる | HRTF の効くデバイスを開く（**効いたかを申告する**。P2 の下ごしらえ） |
 * | まだ | 効果音（`SoundEvent` はフレームにまだ無い。P2） |
 * | まだ | 定位（ベッドは**頭に張り付く**。環境音は方向を持たないので P1 では正しい） |
 * | まだ | ogg / mp3（**WAV だけ**。`SDL_LoadWAV` で読む＝新しい依存を増やさない） |
 *
 * ## 立てなくても落ちない
 * 音の装置が無い機（サーバ・音無しの検査）でも `open()` が偽を返すだけで、
 * 以降の呼び出しは**すべて何もしない**。音は遊びの本筋を止めてよい理由にならない。
 */

#include "frame/sound_event.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace hd2d::audio {

/*!
 * @brief 聞き手から見た音源の向き（**単位はマス**）。右・上・前の 3 成分。
 * @details 世界の座標のままでは渡さない。**左右の取り違えは耳では直せない**ので、
 * 世界 → 聞き手の変換を `to_listener_space()` の 1 か所に閉じ、そこを検査で突く。
 */
struct ListenerOffset {
    float right{ 0.f };
    float up{ 0.f };
    float forward{ 0.f };
};

/*!
 * @brief 世界の差 → 聞き手の空間（純関数）。
 * @param d* 音源 − 聞き手（世界のマス）
 * @param f* 聞き手の前向き（正規化済み） / @param u* 聞き手の上向き（同）
 * @details 右は **上 × 前**で作る。順番が命である——`Camera::azimuth()` が
 * `cross(kWorldUp, forward())` を右としているので、逆に書くと**左右が入れ替わる**。
 * このツリーの世界は x=列・y=行・z=高さ・`kWorldUp = (0,0,1)`。
 * @note ここが 1 か所であることに意味がある。**各所で作ると左右が食い違い、
 * しかも耳で気づくのは実機で遊んでいるときになる。**
 */
ListenerOffset to_listener_space(float dx, float dy, float dz, float fx, float fy, float fz,
    float ux, float uy, float uz);

/*!
 * @brief 道のり（歩数）→ 音の倍率（0..1）。**直線距離ではなく道のり**（設計 §4.1）。
 * @details `kGainReferenceSteps / (kGainReferenceSteps + steps)`。0 歩で 1.0、
 * 5 歩で 0.5、20 歩で 0.2。**仮の曲線である**——実際に遊んで詰めること
 * （VR の数値と同じ扱い。設計 §0）。
 */
float distance_gain(int path_steps);

//! 音源をどこに置くか（聞き手からのマスの差と、減衰に使う歩数）。
struct SoundPlacement {
    float dx{ 0.f };
    float dy{ 0.f };
    int steps{ 0 };
};

/*!
 * @brief 出来事 1 つを「向き」と「歩数」に解く（純関数。設計 §4.1）。
 * @param player_x,player_y 聞き手のマス（中心を採るので +0.5 済みの値を渡す）
 * @details 決めているのは 2 つだけ:
 *
 * 1. **向きは曲がり角から。**`heard_*` が入っていればそちら＝壁を回り込んで
 *    聞こえてくるマスを使う。入っていなければ実際のマス
 * 2. **歩数は道のりから。**`path` が入っていればそれ（**扉の +5 も乗っている**）。
 *    入っていなければマスの差で代用する——8 近傍で歩くので**チェビシェフ距離**
 *
 * どちらも「載っていなければ落ちる」形にしてあるのは、**コア名で分岐しない**ため
 * （`has_sub_panel_kinds` と同じ流儀）。
 */
SoundPlacement place_sound(const SoundEvent &event, float player_x, float player_y);

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    /*!
     * @brief 装置を開く。**HRTF を要求する**（効かなくても開く）。
     * @param log 開けた／開けない理由と、HRTF が効いたかを入れる（stderr 用）
     * @return 開けたら true。**偽でも呼び出し側は続けてよい**
     */
    bool open(std::string *log = nullptr);
    void close();
    bool is_open() const { return this->device_ != nullptr; }
    //! HRTF が実際に効いているか（`open()` の後に見る。P2 の定位の前提）。
    bool hrtf_enabled() const { return this->hrtf_; }

    /*!
     * @brief 聞き手の姿勢。**P1 のベッドには効かない**（頭に張り付くため）が、器は同じ。
     * @param pos 位置（メートル）。`forward` / `up` は正規化済みの向き
     */
    void set_listener(const float pos[3], const float forward[3], const float up[3]);

    //! 環境音の音量（0..1）。**段からの換算は呼び出し側**（設定は段で持っている）。
    void set_bed_gain(float gain);

    /*!
     * @brief ベッドを切り替える。**同じ名前なら何もしない**（鳴らし直すと頭へ戻る）。
     * @param name ベッドの名前（`AmbienceTable` が返すもの）。**空なら止める**
     * @param dir wav の置き場（`assets/audio/bed`）
     * @details 読めなければ**黙って止める**のではなく、名前を覚えたうえで鳴らさない
     * ——同じ名前で毎フレーム読み直しに行かないため。
     * @return **その場で新しく鳴らし始めたら true**（同じベッドで何もしなかった場合と、
     * 読めなかった場合は false）。検査が「ファイルから本当に鳴らせるか」を突くための返りで、
     * 本番の呼び出し側は見なくてよい。
     */
    bool play_bed(const std::string &name, const std::string &dir);
    //! いま鳴らそうとしているベッドの名前（読めたかどうかとは別）。
    const std::string &current_bed() const { return this->bed_name_; }

    /*!
     * @brief **重ねる層**の音量の目標を決める。
     * @param name 層の名前（`layer_grass` など）。**空は無視**
     * @param dir wav の置き場（ベッドと同じ）
     * @param target 目標の音量（0..1）。**0 でも止めない**——`update` が下げ切ってから畳む
     * @return その場で新しく鳴らし始めたら true
     *
     * @details **一度鳴らしたら鳴らし直さない。**音量だけを動かす。
     * 鳴らし直すと頭へ戻り、そこで音が途切れる（「寸断で不自然になる」と決めた）。
     * ベッド（`play_bed`）はクロスフェードで**入れ替わる**が、層は**入れ替わらない**——
     * だからベッドが 1.2 秒で交代している最中も、層は自分の速さで動き続けられる。
     *
     * 読めない wav は**名前を覚えて二度と読みに行かない**（毎フレーム叩かないため）。
     */
    bool set_layer(const std::string &name, const std::string &dir, float target);
    //! 目録に無い層を下げる（毎フレーム決め直すので、消えた層を畳むのに要る）。
    void fade_out_layers_except(const std::vector<std::string> &keep);
    //! いま枠を持っている層の数（検査用）。
    std::size_t layer_count() const { return this->layers_.size(); }
    //! その層のいまの音量（検査用。無ければ 0）。
    float layer_gain(const std::string &name) const;

    //! 効果音の音量（0..1）。**ベッドとは別のつまみ**（機能メニューの「効果音の音量」）。
    void set_sfx_gain(float gain);

    /*!
     * @brief 効果音を 1 つ、**その向きから**鳴らす。
     * @param path wav へのパス（`SfxCatalog::path_for`）
     * @param at 聞き手から見た向き（`to_listener_space`）。**全部 0 なら頭で鳴る**
     * @param gain 音そのものの倍率（0..1）。**距離の減衰は呼び出し側が掛けてから渡す**
     * @return 鳴らせたら true
     * @details 距離減衰は OpenAL に任せない（`AL_ROLLOFF_FACTOR` を 0 にする）。
     * **道のりで減衰させたいのに、OpenAL は直線距離しか知らない**からである（設計 §4.1）。
     * だから源は**向きだけを持つ**——聞き手から一定の半径に置き、音量はこちらで決める。
     */
    bool play_sfx(const std::string &path, const ListenerOffset &at, float gain);

    //! 毎フレーム呼ぶ。**クロスフェードを進める**（`dt_s` は前の呼び出しからの秒）。
    void update(float dt_s);

    /*!
     * @brief 検査用: 生の PCM をベッドとして鳴らす（ファイルを置かずに道を通す）。
     * @param pcm 16bit 符号付き・単耳・44100Hz の並び
     * @return 鳴らせたら true
     * @details `--ui-check` が「装置が本当に音を出せるか」を、素材無しで確かめるための手段。
     * **本番の道（`play_bed`）と同じ関数を通す**ことはできない（あちらはファイルを読むので）
     * が、鳴らす側（バッファを源へ繋いで再生する部分）は共通の実装を呼ぶ。
     */
    bool play_test_pcm(const short *pcm, std::size_t frames, int rate);

private:
    struct Voice;
    //! 源を 1 本作って鳴らす（`play_bed` と `play_test_pcm` の共通部分）。
    bool start_voice(unsigned int buffer, bool loop);
    void stop_all_voices();

    void *device_{ nullptr }; //!< `ALCdevice *`（ヘッダに OpenAL を持ち込まない）
    void *context_{ nullptr }; //!< `ALCcontext *`
    bool hrtf_{ false };

    std::string bed_name_; //!< いま鳴らそうとしているベッド（空 = 無し）
    float gain_{ 1.f };
    float sfx_gain_{ 1.f };

    /*!
     * @brief 読んだ wav の貯め（道 → バッファ）。**同じ音を毎回読み直さない。**
     * @details 効果音は 1 つ数十 KB で、同じものが何度も鳴る。貯めないと
     * 殴り合いのたびにディスクを叩く。ベッド（数 MB）は貯めない——あちらは 1 本しか要らない。
     */
    std::map<std::string, unsigned int> sfx_cache_;
    //! 同時に鳴らせる効果音の数。**変愚の待ち行列（16）と同じ**にしてある。
    static constexpr int kSfxVoices = 16;
    unsigned int sfx_sources_[kSfxVoices]{};

    /*!
     * @brief 鳴っている源（最大 2 本）。**クロスフェードのために 2 本要る。**
     * @details 新しいベッドは 0 から上げ、古いほうは 0 へ下げて止める。
     * 3 本目が要る場面（切り替えの最中にもう一度切り替える）は、いちばん古いものを
     * 即座に止めて場所を空ける——**無音を作らない**ほうを優先する。
     */
    struct VoiceSlot {
        unsigned int source{ 0 };
        unsigned int buffer{ 0 };
        float gain{ 0.f };
        float target{ 0.f };
        bool used{ false };
    };
    VoiceSlot voices_[2]{};

    /*!
     * @brief 重ねる層。名前ごとに 1 本ずつ。
     * @details ベッドの `voices_` とは**別枠**にしてある。同じ枠に入れると、
     * ベッドが入れ替わるときの「いちばん静かなほうを潰す」に層が巻き込まれて消える。
     */
    struct LayerVoice {
        unsigned int source{ 0 };
        unsigned int buffer{ 0 };
        float gain{ 0.f };
        float target{ 0.f };
        bool missing{ false }; //!< wav が読めなかった。**二度と読みに行かない**
    };
    std::map<std::string, LayerVoice> layers_;
};

} // namespace hd2d::audio
