/*!
 * @file sdl_mixer_audio.cpp
 * @brief SDL2 UI 経路の効果音／BGM（SDL_mixer 実装。Android・非 Windows 用）
 *
 * `sdl_win_audio.h` の 4 関数をそのまま実装するので、呼び出し側
 * （`presentation/bootstrap/sdl_game_bootstrap.cpp`）は Windows と同一のまま動く。
 * ビルド時にこの TU か `sdl_win_audio.cpp` のどちらか一方だけを積む。
 *
 * Windows 版との作りの違い:
 *   - 効果音   : waveOut を自前で叩く → `Mix_Chunk`（16 チャンネル）
 *   - BGM      : MCI ＋ `MM_MCINOTIFY` でループ → `Mix_PlayMusic(music, -1)`
 *                （SDL_mixer が無限ループを持つので、通知窓＝`HWND_MESSAGE` が要らない）
 *   - 設定読み : `main-win-cfg-reader`（Win32 の ini API）→ `main-unix/unix-cfg-reader`
 *                （SimpleIni。純 C++ でどこでも動く。**選曲の規則はコア側と同じ**）
 *
 * @note 音声ファイル（`lib/xtra/{sound,music}`）は**内部ストレージへ展開してある**ことが前提。
 * SDL_mixer 自体は `SDL_RWFromFile` で APK の assets からも読めるので、当初は
 * 「94MB を二重に持たないよう assets のまま」にしていた。**それでは完全に無音になる。**
 * どの音を鳴らすかを決める `CfgReader::read_sections`
 * （`src/main-unix/unix-cfg-reader.cpp:161`）が `is_regular_file` で実ファイルの存在を
 * 確かめてからでないと項目を登録せず、assets は stdio から見えないため、
 * 全項目が「割り当て無し」に落ちていた（実機相当で踏んだ）。
 */
#include "audio/sdl_win_audio.h"

#include "frame/sdl_ui_options.h"

#include "game-option/runtime-arguments.h"
#include "game-option/special-options.h"
#include "main-unix/unix-cfg-reader.h"
#include "main/music-definitions-table.h"
#include "main/scene-table.h"
#include "main/sound-definitions-table.h"
#include "main/sound-of-music.h"
#include "system/dungeon/dungeon-list.h"
#include "system/dungeon/quest-definition.h"
#include "system/dungeon/quest-list.h"
#include "system/floor/town-info.h"
#include "system/floor/town-list.h"
#include "system/monrace/monrace-list.h"
#include "system/player-type-definition.h"
#include "term/z-form.h"
#include "term/z-term.h"
#include "util/enum-converter.h"
#include "world/world.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>

extern PlayerType *p_ptr;

namespace presentation {
namespace {

//! 音量 100%,90%,…,10%。Windows 版の SOUND_VOLUME_TABLE / VOLUME_TABLE と同じ並び。
constexpr std::array<int, SdlUiOptions::kVolumeLevels> kVolumeTable = {
    1000, 800, 600, 450, 350, 250, 170, 100, 50, 20
};
constexpr int kVolumeMax = 1000;

//! 同時に鳴らせる効果音の数。Windows 版の waveOut バッファ数に合わせた。
constexpr int kSoundChannels = 16;

bool g_inited = false;
bool g_mixer_open = false;

/*!
 * @brief 音声ファイルの置き場（`lib_path` から作る**絶対**パス）。
 * @details かつては「assets から直接読めるように」相対パスにしていたが、
 * **鳴らす音を決める `CfgReader::read_sections` が `is_regular_file` で実ファイルの
 * 存在を確かめてからでないと項目を登録しない**（`unix-cfg-reader.cpp:161`）ため、
 * assets のままでは全項目が「割り当て無し」になり完全に無音だった。
 * 音声は内部ストレージへ展開する方針に変えたので、cwd に依らない絶対パスで持つ。
 */
std::filesystem::path g_sound_dir;
std::filesystem::path g_music_dir;

tl::optional<CfgData> g_sound_cfg;
tl::optional<CfgData> g_music_cfg;

//! 読み込み済みの効果音。曲数が高々数十なので、一度読んだら持ち続ける。
std::unordered_map<std::string, Mix_Chunk *> g_chunks;

Mix_Music *g_music = nullptr;
int g_music_type = TERM_XTRA_MUSIC_MUTE;
int g_music_id = 0;
std::string g_music_path;

int mix_volume(int index)
{
    const int i = std::clamp(index, 0, SdlUiOptions::kVolumeLevels - 1);
    return kVolumeTable[static_cast<size_t>(i)] * MIX_MAX_VOLUME / kVolumeMax;
}

// ---------------------------------------------------------------- cfg の読取
// キー名の決め方はコア（main-unix/unix-music.cpp）と同一。ここを変えると
// 同じ music.cfg が Windows と Android で違う曲を選ぶことになる。

tl::optional<std::string> sound_key_at(int index)
{
    const auto sk = i2enum<SoundKind>(index);
    if (sk >= SoundKind::MAX) {
        return tl::nullopt;
    }
    return sound_names.at(sk);
}

tl::optional<std::string> basic_key_at(int index)
{
    if (index >= MUSIC_BASIC_MAX) {
        return tl::nullopt;
    }
    return angband_music_basic_name[index];
}

tl::optional<std::string> dungeon_key_at(int index)
{
    if (index >= static_cast<int>(DungeonList::get_instance().size())) {
        return tl::nullopt;
    }
    return format("dungeon%03d", index);
}

tl::optional<std::string> quest_key_at(int index)
{
    const auto &quests = QuestList::get_instance();
    // B-01 と同型の罠。空の QuestList で rbegin() すると落ちる。
    if (quests.empty()) {
        return tl::nullopt;
    }
    if (index > enum2i(quests.rbegin()->first)) {
        return tl::nullopt;
    }
    return format("quest%03d", index);
}

tl::optional<std::string> town_key_at(int index)
{
    if (index >= static_cast<int>(TownList::get_instance().size())) {
        return tl::nullopt;
    }
    return format("town%03d", index);
}

tl::optional<std::string> monster_key_at(int index)
{
    if (index >= static_cast<int>(MonraceList::get_instance().size())) {
        return tl::nullopt;
    }
    return format("monster%04d", index);
}

// ------------------------------------------------------------------ 再生本体

SDL_RWops *open_media(const std::filesystem::path &dir, const std::string &filename)
{
    const std::string path = (dir / filename).string();
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (rw == nullptr) {
        std::fprintf(stderr, "[sdl-audio] open failed: %s (%s)\n", path.c_str(), SDL_GetError());
    }
    return rw;
}

Mix_Chunk *chunk_for(const std::string &filename)
{
    if (const auto it = g_chunks.find(filename); it != g_chunks.end()) {
        return it->second;
    }
    Mix_Chunk *chunk = nullptr;
    if (SDL_RWops *rw = open_media(g_sound_dir, filename); rw != nullptr) {
        chunk = Mix_LoadWAV_RW(rw, 1); // freesrc=1: 読み終えたら SDL_mixer が閉じる
        if (chunk == nullptr) {
            std::fprintf(stderr, "[sdl-audio] Mix_LoadWAV_RW failed: %s (%s)\n",
                filename.c_str(), Mix_GetError());
        }
    }
    // 失敗も覚える（毎回開き直して固まるのを防ぐ）。
    g_chunks.emplace(filename, chunk);
    return chunk;
}

void free_music()
{
    if (g_music != nullptr) {
        Mix_HaltMusic();
        Mix_FreeMusic(g_music);
        g_music = nullptr;
    }
    g_music_type = TERM_XTRA_MUSIC_MUTE;
    g_music_id = 0;
    g_music_path.clear();
}

/*!
 * @param quiet 割り当てが無いことを記録しない（シーン表の走査から呼ぶとき）
 * @details シーン表（`TERM_XTRA_SCENE`）は候補を**先頭から順に試して鳴ったところで抜ける**
 * 仕組みなので、「割り当てが無い」は正常な通過点。ここで記録するとログが埋まる。
 */
bool play_music_impl(int type, int val, bool quiet = false)
{
    if (type == TERM_XTRA_MUSIC_MUTE) {
        free_music();
        return true;
    }
    if (!g_music_cfg) {
        return false;
    }
    if (g_music_type == type && g_music_id == val && Mix_PlayingMusic()) {
        return true; // 同じ曲を鳴らしている
    }

    const auto filename = g_music_cfg->get_rand(type, val);
    if (!filename) {
        static int unmapped = 0;
        if (!quiet && unmapped < 8) {
            ++unmapped;
            std::fprintf(stderr, "[sdl-audio] no music assigned for type=%d val=%d\n", type, val);
        }
        return false; // この状況に割り当てが無い
    }

    const std::string path = (g_music_dir / *filename).string();
    if (g_music != nullptr && g_music_path == path && Mix_PlayingMusic()) {
        // 同じファイルなら切らない（町→町の移動で曲が頭に戻らないように）
        g_music_type = type;
        g_music_id = val;
        return true;
    }

    SDL_RWops *rw = open_media(g_music_dir, *filename);
    if (rw == nullptr) {
        return false;
    }
    Mix_Music *next = Mix_LoadMUS_RW(rw, 1);
    if (next == nullptr) {
        std::fprintf(stderr, "[sdl-audio] Mix_LoadMUS_RW failed: %s (%s)\n",
            path.c_str(), Mix_GetError());
        return false;
    }

    free_music();
    g_music = next;
    g_music_type = type;
    g_music_id = val;
    g_music_path = path;

    // -1 = 無限ループ。Windows 版が MCI の完了通知で自前ループしていた部分に相当する。
    if (Mix_PlayMusic(g_music, -1) != 0) {
        std::fprintf(stderr, "[sdl-audio] Mix_PlayMusic failed: %s\n", Mix_GetError());
        free_music();
        return false;
    }
    std::fprintf(stderr, "[sdl-audio] music -> %s (type=%d val=%d)\n", path.c_str(), type, val);
    return true;
}

void play_music_scene_impl(int val)
{
    // リストの先頭から順に試し、鳴らせたら抜ける（main-win-music と同じ規則）。
    auto &list = get_scene_type_list(val);
    for (auto &item : list) {
        if (play_music_impl(item.type, item.val, /*quiet=*/true)) {
            break;
        }
    }
}

} // namespace

bool sdl_win_audio_init(const std::filesystem::path &lib_path)
{
    if (g_inited) {
        return true;
    }

    // cwd に依らない絶対パスで持つ（`lib_path` は `<基準>/lib`）。
    g_sound_dir = lib_path / "xtra" / "sound";
    g_music_dir = lib_path / "xtra" / "music";

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "[sdl-audio] SDL_INIT_AUDIO failed: %s\n", SDL_GetError());
        return false;
    }
    // 44.1kHz / ステレオ。バッファは 2048（携帯端末で途切れにくい実測値の下限あたり）。
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        std::fprintf(stderr, "[sdl-audio] Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    g_mixer_open = true;
    Mix_AllocateChannels(kSoundChannels);

    // MP3 の復号器を起こす。同梱 minimp3 は静的に組み込まれているので必須ではないが、
    // 失敗したときに**ここで分かる**ようにしておく（無音の原因を後から追うのは難しい）。
    const int want = MIX_INIT_MP3;
    const int got = Mix_Init(want);
    if ((got & want) != want) {
        std::fprintf(stderr, "[sdl-audio] Mix_Init(MP3) incomplete: got=0x%x (%s)\n",
            got, Mix_GetError());
    }
    {
        int freq = 0;
        int channels = 0;
        Uint16 format = 0;
        if (Mix_QuerySpec(&freq, &format, &channels) != 0) {
            std::fprintf(stderr, "[sdl-audio] device %dHz fmt=0x%04x ch=%d driver=%s\n",
                freq, format, channels,
                (SDL_GetCurrentAudioDriver() != nullptr) ? SDL_GetCurrentAudioDriver() : "(none)");
        } else {
            std::fprintf(stderr, "[sdl-audio] Mix_QuerySpec failed: %s\n", Mix_GetError());
        }
    }

    // 設定の読取。**必ず init_angband の後**（Quest/Dungeon/Town/Monrace 表を引く）。
    // 呼び出し元（run_sdl_game）がその順序を守っている。
    {
        CfgReader sound_reader(g_sound_dir, { "sound_debug.cfg", "sound.cfg" });
        // **cfg が読めているかを必ず残す。** 無音の原因がここか再生側かの切り分けは、
        // これが無いと画面からも音からも判らない（Android で実際に詰まった）。
        std::fprintf(stderr, "[sdl-audio] sound cfg = '%s'\n",
            sound_reader.get_cfg_path().string().c_str());
        g_sound_cfg = sound_reader.read_sections({ { "Sound", TERM_XTRA_SOUND, sound_key_at } });
    }
    {
        CfgReader music_reader(g_music_dir, { "music_debug.cfg", "music.cfg" });
        std::fprintf(stderr, "[sdl-audio] music cfg = '%s'\n",
            music_reader.get_cfg_path().string().c_str());
        g_music_cfg = music_reader.read_sections({
            { "Basic", TERM_XTRA_MUSIC_BASIC, basic_key_at },
            { "Dungeon", TERM_XTRA_MUSIC_DUNGEON, dungeon_key_at },
            { "Quest", TERM_XTRA_MUSIC_QUEST, quest_key_at },
            { "Town", TERM_XTRA_MUSIC_TOWN, town_key_at },
            { "Monster", TERM_XTRA_MUSIC_MONSTER, monster_key_at, &has_monster_music },
        });
        if (!has_monster_music && g_music_cfg) {
            for (int val = MUSIC_BASIC_UNIQUE; val <= MUSIC_BASIC_HIGHER_LEVEL_MONSTER; ++val) {
                if (g_music_cfg->has_key(TERM_XTRA_MUSIC_BASIC, val)) {
                    has_monster_music = true;
                    break;
                }
            }
        }
    }

    load_sdl_ui_options();
    sdl_win_audio_apply_options();

    g_inited = true;
    std::fprintf(stderr,
        "[sdl-audio] init (SDL_mixer) sound=%s music=%s use_sound=%d use_music=%d"
        " sound_cfg=%d music_cfg=%d\n",
        g_sound_dir.string().c_str(), g_music_dir.string().c_str(),
        use_sound ? 1 : 0, use_music ? 1 : 0,
        g_sound_cfg.has_value() ? 1 : 0, g_music_cfg.has_value() ? 1 : 0);
    return true;
}

void sdl_win_audio_shutdown()
{
    if (!g_inited) {
        return;
    }
    free_music();
    for (auto &[name, chunk] : g_chunks) {
        if (chunk != nullptr) {
            Mix_FreeChunk(chunk);
        }
    }
    g_chunks.clear();
    if (g_mixer_open) {
        Mix_CloseAudio();
        g_mixer_open = false;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_inited = false;
}

void sdl_win_audio_apply_options()
{
    const auto &opts = sdl_ui_options();
    use_sound = opts.sound_enabled;
    use_music = opts.music_enabled;
    arg_sound = opts.sound_enabled;
    arg_music = opts.music_enabled;

    const int sidx = std::clamp(opts.sound_volume_index, 0, SdlUiOptions::kVolumeLevels - 1);
    const int midx = std::clamp(opts.music_volume_index, 0, SdlUiOptions::kVolumeLevels - 1);
    arg_sound_volume_table_index = sidx;
    arg_music_volume_table_index = midx;

    if (g_mixer_open) {
        Mix_Volume(-1, mix_volume(sidx)); // -1 = 全チャンネル
        Mix_VolumeMusic(mix_volume(midx));
    }

    if (!use_music) {
        free_music();
        return;
    }
    // Quest/Dungeon 表とフロアが揃ってから選曲（B-01 と同系統の順序制約）。
    if (p_ptr != nullptr && AngbandWorld::get_instance().character_generated) {
        select_floor_music(p_ptr);
    }
}

int sdl_win_audio_term_xtra(int n, int v)
{
    switch (n) {
    case TERM_XTRA_SOUND: {
        if (!use_sound || !g_mixer_open || !g_sound_cfg) {
            // 最初の 1 回だけ理由を残す（毎回出すとログが埋まる）。
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr, "[sdl-audio] sound skipped: use_sound=%d mixer=%d cfg=%d\n",
                    use_sound ? 1 : 0, g_mixer_open ? 1 : 0, g_sound_cfg.has_value() ? 1 : 0);
            }
            return 0;
        }
        const auto filename = g_sound_cfg->get_rand(TERM_XTRA_SOUND, v);
        if (!filename) {
            static int unmapped = 0;
            if (unmapped < 5) {
                ++unmapped;
                std::fprintf(stderr, "[sdl-audio] no sound assigned for val=%d\n", v);
            }
            return 0;
        }
        Mix_Chunk *chunk = chunk_for(*filename);
        if (chunk == nullptr) {
            return 0;
        }
        // 最初に鳴った 1 つだけ記録する（効果音は頻度が高いので毎回は出さない）。
        static bool first_reported = false;
        if (!first_reported) {
            first_reported = true;
            std::fprintf(stderr, "[sdl-audio] sound -> %s (val=%d)\n", filename->c_str(), v);
        }
        // 空きチャンネルが無ければ鳴らさない（古い音を切らない）。
        Mix_PlayChannel(-1, chunk, 0);
        return 0;
    }
    case TERM_XTRA_MUSIC_BASIC:
    case TERM_XTRA_MUSIC_DUNGEON:
    case TERM_XTRA_MUSIC_QUEST:
    case TERM_XTRA_MUSIC_TOWN:
    case TERM_XTRA_MUSIC_MONSTER:
        if (!use_music || !g_mixer_open) {
            return 1;
        }
        return play_music_impl(n, v) ? 0 : 1;
    case TERM_XTRA_MUSIC_MUTE:
        free_music();
        return 0;
    case TERM_XTRA_SCENE:
        if (!use_music || !g_mixer_open) {
            return 1;
        }
        play_music_scene_impl(v);
        return 0;
    case TERM_XTRA_NOISE:
        return 0;
    default:
        return 1;
    }
}

} // namespace presentation
