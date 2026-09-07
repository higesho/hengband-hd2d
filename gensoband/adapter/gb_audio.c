/*!
 * @file gb_audio.c
 * @brief 効果音と BGM。
 *
 * ## どこまでがコアの仕事か
 * **何を鳴らすかはコアが決めている。** `util.c` の `play_music()` /
 * `select_floor_music()` が場面から曲の分類と番号を出し、`Term_xtra(TERM_XTRA_MUSIC_*, v)`
 * を投げてくる。効果音も同じで `sound()`（`util.c:1730`）が `TERM_XTRA_SOUND` を投げる。
 * ここがやるのは**「番号 → 設定ファイルの行 → 鳴らす」だけ**である。
 *
 * だから変愚の音の層（`presentation/audio/sdl_mixer_audio.cpp`）とは大きさが違う。
 * あちらは場面の判断まで抱えていて、コアのヘッダを 15 本読んでいる。
 *
 * ## なぜ `main-win.c` を組まずに書き直したか
 * 幻想蛮怒の `main-win.c` には同じ仕掛けが入っているが、あれは**入口と描画も抱えている**
 * ので設計 §7 で「コンパイル除外」と決めてある。ここは音の部分だけを、
 * 同じ API（INI ＋ `PlaySound` ＋ MCI）で書いた。**曲の選び方も同じ**——
 * 候補が複数あれば `Rand_external()` で 1 つ選ぶ。
 *
 * ## 鳴らす口
 * | 何 | どう |
 * |---|---|
 * | 効果音 | `PlaySound()`（winmm）。wav だけ |
 * | BGM | MCI（`mciSendCommand`）。`music.cfg` の `[Device] type` に従う（既定 `MPEGVideo` ＝ mp3） |
 *
 * MCI は**曲が終わったことを窓へ知らせて**くる。コアは窓を持たない（`HengbandCore.exe` と
 * 同じで、画は別プロセス）ので、**メッセージ専用窓**（`HWND_MESSAGE`）を 1 つ立てて
 * そこで受ける。変愚も同じことをしている（`sdl_win_audio.cpp` の `create_mci_window`）。
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。ただし**コアへ渡す文字列はすべて SJIS**
 * （設定ファイルもファイル名もコアの流儀）。ここでは日本語リテラルを使わない。
 */

#include "angband.h"

#include "gb_shim.h"

#if !defined(_WIN32)

/*
 * ## Windows 以外（Android / Quest。2026-08-21）
 *
 * この TU の中身は **winmm そのもの**である（効果音は `PlaySound`、BGM は MCI）。
 * Android に winmm は無く、素材（`lib/xtra/{sound,music}`）も幻想蛮怒には
 * 付いてこないので、**まず黙って動くこと**を採って空実装にする。
 *
 * 申告も合わせてある——`gb_main.cpp` は Windows 以外では `features` に `audio` を
 * 載せない。だから画面側に「音」の節が出て何も鳴らない、という見え方にはならない。
 *
 * 鳴らすなら SDL2_mixer（`presentation/audio/sdl_mixer_audio.cpp` が変愚で使っている
 * 実体）へ載せ替えるのが素直な道である。 の未決 3 と同じ棚。
 */

void gb_audio_init(void) {}
void gb_audio_enable_core(void) {}
void gb_audio_set_enabled(int sound_on, int music_on) { (void)sound_on; (void)music_on; }
void gb_audio_set_volume(int sound_index, int music_index) { (void)sound_index; (void)music_index; }
void gb_audio_stop_music(void) {}
int gb_audio_play_sound(int v) { (void)v; return 0; }
int gb_audio_play_music(int n, int v) { (void)n; (void)v; return 0; }
void gb_audio_pump(void) {}
int gb_audio_mon_music_priority(int r_idx) { (void)r_idx; return MON_MUSIC_PRIOR_NONE; }

int gb_audio_entry_counts(int *sound, int *music)
{
    if (sound) {
        *sound = 0;
    }
    if (music) {
        *music = 0;
    }
    return 0; /* 用意できていない＝`gb_audio_ready` が偽のときと同じ返し */
}

#else /* _WIN32 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <digitalv.h> /* MCI_DGV_SETAUDIO_*（BGM の音量） */

#include <string.h>

/*!
 * @name `main-win.c` の中に閉じている小道具の作り直し
 * @details 音の道（`gb_dir_sound` / `_MUSIC`）も、設定行を割る
 * `gb_tokenize()` も、実在を見る `gb_file_exists()` も、**すべて `main-win.c` の
 * `static`** である（`:591` `:1439` `:925`）。あの TU は組まないので、
 * ここで同じものを持つ。道の作り方（`ANGBAND_DIR_XTRA` の下の `sound` / `music`）は
 * あちらの `:6239` と同じ。
 * @{
 */

static char gb_dir_sound[1024];
static char gb_dir_music[1024];

//! 実在するファイルか。
static int gb_file_exists(cptr path)
{
    FILE *fp = my_fopen(path, "rb");

    if (!fp) {
        return 0;
    }
    my_fclose(fp);
    return 1;
}

/*!
 * @brief 設定行を空白で割る。**`<>` で囲めば空白入りの名前も 1 つ**（`music.cfg` の注記）。
 * @return 割った数
 */
static int gb_tokenize(char *buf, int num, char **tokens)
{
    int k = 0;
    char *s = buf;

    while (k < num) {
        char *t;

        for (; *s && isspace((unsigned char)*s); ++s) {
            /* 先頭の空白を飛ばす */
        }
        if (!*s) {
            break;
        }
        if (*s == '<') {
            ++s;
            if (!*s) {
                break;
            }
            for (t = s; *t && (*t != '>'); ++t) {
                if (iskanji(*t)) {
                    ++t;
                }
            }
            if (*t) {
                *t++ = '\0';
            }
            tokens[k++] = s;
            s = t;
            continue;
        }
        for (t = s; *t && !isspace((unsigned char)*t); ++t) {
            /* 次の空白まで */
        }
        if (*t) {
            *t++ = '\0';
        }
        tokens[k++] = s;
        s = t;
    }
    return k;
}
/*! @} */

/*!
 * @name 表の大きさ
 * @details `SAMPLE_MAX` と `SAMPLE_MUSIC_MAX` は `main-win.c` の中だけで定義されていて
 * ヘッダに出ていない（`:535` と `:548`）。**同じ値をここに写す**——大きいぶんには
 * 困らないが、小さいと設定ファイルの後ろが黙って切れる。
 * @{
 */
#define GB_SAMPLE_MAX 8
#define GB_SAMPLE_MUSIC_MAX 16
/*! @} */

/*! 効果音。`[Sound]` の 1 項目につき候補が最大 8 つ。 */
static cptr gb_sound_file[SOUND_MAX][GB_SAMPLE_MAX];
/*! BGM。分類ごとに `[Basic]` `[Dungeon]` `[Town]` `[Quest]` `[Monster_*]`。 */
static cptr gb_basic_music_file[MUSIC_BASIC_MAX][GB_SAMPLE_MUSIC_MAX];
static cptr gb_dungeon_music_file[DUNGEON_MAX][GB_SAMPLE_MUSIC_MAX];
static cptr gb_town_music_file[TOWN_MAX][GB_SAMPLE_MUSIC_MAX];
static cptr gb_quest_music_file[QUEST_MAX][GB_SAMPLE_MUSIC_MAX];
static cptr gb_low_mon_music_file[MON_IDX_MAX][GB_SAMPLE_MUSIC_MAX];
static cptr gb_med_mon_music_file[MON_IDX_MAX][GB_SAMPLE_MUSIC_MAX];
static cptr gb_high_mon_music_file[MON_IDX_MAX][GB_SAMPLE_MUSIC_MAX];

/*! MCI のデバイス種別（`[Device] type`）。空なら MCI に推測させる。 */
static char gb_mci_device_type[256];

/*! いま鳴っている曲の分類と番号。**同じものが来たら鳴らし直さない**（曲が頭に戻る）。 */
static int gb_current_music_type = 0;
static int gb_current_music_id = 0;

/*! MCI の開き口。曲を替えるたびに閉じて開き直す。 */
static MCI_OPEN_PARMS gb_mop;
/*! 曲の終わりを受けるためだけの窓（`HWND_MESSAGE`）。 */
static HWND gb_mci_hwnd = NULL;

static int gb_audio_ready = 0;
/*! 画面側の設定（`ui_state.audio`）。**既定は切**——鳴らすと決めるのは画面側。 */
static int gb_sound_on = 0;
static int gb_music_on = 0;
/*! BGM の音量（0〜1000）。**既定は最大**——画面側から届くまでは従来どおり鳴らす。 */
static int gb_music_volume = 1000;

/*!
 * 効果音の音量（0〜1000）。**既定は最大。**
 * @details `PlaySound` に音量の引数は無いが、**渡す前に PCM を書き換えれば効く**
 * （`gb_scale_wav`）。振幅を `volume/1000` 倍にしてから `SND_MEMORY` で鳴らす。
 * 縮め方（線形・表も同じ）は変愚の `modulate_amplitude`（`main-win-sound.cpp`）と
 * 同じなので、**同じ段なら同じ大きさで鳴る**。
 * @note 最大のときは 1 バイトも触らず、従来どおり `SND_FILENAME` で鳴らす。
 */
static int gb_sound_volume = 1000;

/*! 書き換えた PCM の器（`SND_MEMORY` で鳴らしている間は生かしておく）。 */
static unsigned char *gb_sfx_buf = NULL;
static long gb_sfx_buf_size = 0;

/*! これより大きい wav は器へ載せない（素のまま鳴らす）。効果音は普通 100KB 未満。 */
#define GB_SFX_MAX_BYTES (8L * 1024L * 1024L)

/*! 音量の添字（0 ＝ 100% … 9 ＝ 10%）→ MCI の 0〜1000。**変愚の `VOLUME_TABLE` と同じ表**。 */
static const int gb_volume_table[10] = { 1000, 800, 600, 450, 350, 250, 170, 100, 50, 20 };

/*! @brief いまの音量を開いている MCI へ当てる。開いていなければ何もしない。 */
static void gb_apply_music_volume(void)
{
    MCI_DGV_SETAUDIO_PARMS parms;

    if (!gb_mop.wDeviceID) {
        return;
    }
    memset(&parms, 0, sizeof(parms));
    parms.dwItem = MCI_DGV_SETAUDIO_VOLUME;
    parms.dwValue = (DWORD)gb_music_volume;
    mciSendCommand(gb_mop.wDeviceID, MCI_SETAUDIO,
        MCI_DGV_SETAUDIO_ITEM | MCI_DGV_SETAUDIO_VALUE, (DWORD_PTR)&parms);
}

/*! 読み込めた項目の数（起動時に stderr へ出す。設定が空なのか道が違うのかを分ける）。 */
static int gb_sound_entries = 0;
static int gb_music_entries = 0;

/*! `GB_AUDIO_LOG=1` のとき、鳴らすたびに stderr へ 1 行出す（検査用）。 */
static int gb_audio_log = 0;
/*!
 * `GB_AUDIO_SILENT=1` のとき、**曲やファイルの解決までは通して、鳴らす直前で止める**。
 * 検査（`gb_protocol_driver.py`）が「どのファイルに解決したか」を確かめるための手段である
 * ——検査のたびに実際に音が出ると、机の前で鳴りっぱなしになる。
 * **解決の道は 1 ビットも変えない**（記憶 `hengband-check-must-not-copy-the-draw` と同じ考え）。
 */
static int gb_audio_silent = 0;

/*!
 * @brief PCM の振幅を `volume/1000` 倍にする（**その場で書き換える**）。
 * @param bits 1 標本のビット数。**8（符号なし・中心 128）と 16（符号付き）だけ**扱う
 * @return 縮めたら 1。触れない形式なら 0（呼び側は素のファイルで鳴らす）
 * @details 変愚の `modulate_amplitude` と同じ計算にしてある——片方だけ変えると、
 * 同じ段なのにコアによって大きさが違う、という直しにくい違いになる。
 */
static int gb_modulate_pcm(unsigned char *pcm, long size, int bits, int volume)
{
    long i;

    if (bits == 8) {
        for (i = 0; i < size; i++) {
            int v = 128 + (((int)pcm[i] - 128) * volume) / 1000;
            pcm[i] = (unsigned char)((v < 0) ? 0 : ((v > 255) ? 255 : v));
        }
        return 1;
    }
    if (bits == 16) {
        for (i = 0; (i + 1) < size; i += 2) {
            int v = (int)((short)((unsigned short)pcm[i] | ((unsigned short)pcm[i + 1] << 8)));
            v = (v * volume) / 1000;
            if (v < -32768) v = -32768;
            if (v > 32767) v = 32767;
            pcm[i] = (unsigned char)(v & 0xff);
            pcm[i + 1] = (unsigned char)((v >> 8) & 0xff);
        }
        return 1;
    }
    return 0; /* 24bit・float・圧縮は触らない（素のまま鳴らす） */
}

/*!
 * @brief 器に読んだ wav の `data` 塊だけを縮める。@return 縮めたら 1
 * @details RIFF の塊を頭から辿って `fmt ` と `data` を拾う。**頭は書き換えない**ので、
 * そのまま `PlaySound(..., SND_MEMORY)` へ渡せる。壊れていたら 0 を返して
 * 素のファイルへ逃がす——**鳴らないより、大きくても鳴るほうがまし**。
 */
static int gb_scale_wav(unsigned char *buf, long size, int volume)
{
    long at = 12;
    int format = 0;
    int bits = 0;

    if ((size < 44) || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) {
        return 0;
    }
    while ((at + 8) <= size) {
        const long body = at + 8;
        const unsigned long len = (unsigned long)buf[at + 4] | ((unsigned long)buf[at + 5] << 8)
            | ((unsigned long)buf[at + 6] << 16) | ((unsigned long)buf[at + 7] << 24);

        if (len > (unsigned long)(size - body)) {
            return 0; /* 塊が器からはみ出している＝読み違えている */
        }
        if (!memcmp(buf + at, "fmt ", 4) && (len >= 16)) {
            format = (int)buf[body] | ((int)buf[body + 1] << 8);
            bits = (int)buf[body + 14] | ((int)buf[body + 15] << 8);
        } else if (!memcmp(buf + at, "data", 4)) {
            if (format != WAVE_FORMAT_PCM) {
                return 0;
            }
            return gb_modulate_pcm(buf + body, (long)len, bits, volume);
        }
        at = body + (long)len + (long)(len & 1UL); /* 塊は偶数境界 */
    }
    return 0;
}

/*!
 * @brief 音量を当てた効果音を鳴らす。@return 鳴らせたら 1（0 なら呼び側が素で鳴らす）
 * @details **器は 1 つで使い回す。**`PlaySound` は同時に 1 つしか鳴らさない
 * （次を鳴らすと前は止まる）ので 2 つ要らないが、**書き換える前に必ず止める**
 * ——鳴っている最中の器を書き換えると、音の途中で別の波形に化ける。
 * @note `GB_AUDIO_SILENT=1` でも**ここまでは通す**（読み込みと縮めまで）。
 * 省くのは `PlaySound` の 1 行だけ——検査が「実物の wav を縮められるか」を
 * 見られるようにするため（記憶 `hengband-check-must-not-copy-the-draw` と同じ考え）。
 */
static int gb_play_sound_scaled(cptr path)
{
    FILE *fp;
    long size;
    size_t got;

    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if ((size <= 0) || (size > GB_SFX_MAX_BYTES) || (fseek(fp, 0, SEEK_SET) != 0)) {
        fclose(fp);
        return 0;
    }
    PlaySound(NULL, NULL, 0); /* **先に止める。**この後で器を書き換えるから */
    if (gb_sfx_buf_size < size) {
        unsigned char *grown = (unsigned char *)realloc(gb_sfx_buf, (size_t)size);
        if (!grown) {
            fclose(fp);
            return 0;
        }
        gb_sfx_buf = grown;
        gb_sfx_buf_size = size;
    }
    got = fread(gb_sfx_buf, 1, (size_t)size, fp);
    fclose(fp);
    if ((long)got != size) {
        return 0;
    }
    if (!gb_scale_wav(gb_sfx_buf, size, gb_sound_volume)) {
        return 0;
    }
    if (gb_audio_silent) {
        if (gb_audio_log) {
            fprintf(stderr, "[gensoband] audio: scaled %ld bytes to %d/1000 (not played)\n",
                size, gb_sound_volume);
        }
        return 1; /* 縮めるところまでは通した。鳴らすところだけ省く */
    }
    {
        /*
         * **鳴らせたかどうかを出す。**書き換えた器を `SND_MEMORY` で渡しているので、
         * 頭（RIFF）を壊していれば偽が返る。偽なら呼び側が素のファイルへ逃がす
         * ——大きさは元のままになるが、**黙るよりまし**。
         */
        const int played = PlaySound((cptr)gb_sfx_buf, NULL, SND_MEMORY | SND_ASYNC) ? 1 : 0;
        if (gb_audio_log) {
            fprintf(stderr, "[gensoband] audio: scaled %ld bytes to %d/1000 (played=%d)\n",
                size, gb_sound_volume, played);
        }
        return played;
    }
}

/*!
 * @brief 窓の手続き。MCI から「終わった」が来たら**頭へ戻して鳴らし直す**（＝ループ）。
 * @details 曲の長さを見て自分で回すより、MCI に終わりを教えてもらうほうが確実である。
 */
static LRESULT CALLBACK gb_mci_wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == MM_MCINOTIFY) {
        if (w == MCI_NOTIFY_SUCCESSFUL) {
            gb_apply_music_volume(); /* 頭へ戻すたびに当て直す（変愚の `on_mci_notify` と同じ） */
            mciSendCommand(gb_mop.wDeviceID, MCI_SEEK, MCI_SEEK_TO_START, 0);
            mciSendCommand(gb_mop.wDeviceID, MCI_PLAY, MCI_NOTIFY, (DWORD_PTR)&gb_mop);
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

/*! @brief メッセージ専用窓を 1 つ立てる。@return 立ったら 1 */
static int gb_make_mci_window(void)
{
    WNDCLASS wc;
    static const char *kClass = "GensobandCoreMciNotify";

    if (gb_mci_hwnd) {
        return 1;
    }
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = gb_mci_wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = kClass;
    RegisterClass(&wc);
    gb_mci_hwnd = CreateWindowEx(0, kClass, kClass, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
        wc.hInstance, NULL);
    return (gb_mci_hwnd != NULL);
}

/*!
 * @brief `music.cfg` の 1 項目を読んで表へ入れる。@return 入れた候補の数
 * @details ファイル名に `:` が含まれていたら**フルパス**とみなす（`music.cfg` の注記）。
 * 実在しないファイルは**入れない**——入れると鳴らないまま候補として選ばれる。
 */
static int gb_load_music_entry(cptr ini_path, cptr section, cptr key, cptr *out)
{
    char tmp[4096];
    char full[1024];
    char *zz[GB_SAMPLE_MUSIC_MAX];
    int num;
    int j;
    int kept = 0;

    GetPrivateProfileString(section, key, "", tmp, sizeof(tmp), ini_path);
    num = gb_tokenize(tmp, GB_SAMPLE_MUSIC_MAX, zz);
    for (j = 0; j < num; j++) {
        if (strchr(zz[j], (int)':')) {
            (void)strnfmt(full, sizeof(full), "%s", zz[j]);
        } else {
            path_build(full, sizeof(full), gb_dir_music, zz[j]);
        }
        if (!gb_file_exists(full)) {
            continue;
        }
        out[kept++] = string_make(zz[j]);
    }
    return kept;
}

/*! @brief `sound.cfg` を読む。 */
static void gb_load_sound_prefs(void)
{
    char ini_path[1024];
    char tmp[1024];
    char full[1024];
    char *zz[GB_SAMPLE_MAX];
    int i;
    int j;
    int num;

    path_build(ini_path, sizeof(ini_path), gb_dir_sound, "sound.cfg");
    for (i = 0; i < SOUND_MAX; i++) {
        if (!angband_sound_name[i]) {
            continue;
        }
        GetPrivateProfileString("Sound", angband_sound_name[i], "", tmp, sizeof(tmp), ini_path);
        num = gb_tokenize(tmp, GB_SAMPLE_MAX, zz);
        for (j = 0; j < num; j++) {
            path_build(full, sizeof(full), gb_dir_sound, zz[j]);
            if (!gb_file_exists(full)) {
                continue;
            }
            gb_sound_file[i][j] = string_make(zz[j]);
            gb_sound_entries++;
        }
    }
}

/*! @brief `music.cfg` を読む。分類ごとの節は `main-win.c` の `load_music_prefs` と同じ。 */
static void gb_load_music_prefs(void)
{
    char ini_path[1024];
    char key[80];
    int i;

    path_build(ini_path, sizeof(ini_path), gb_dir_music, "music.cfg");
    GetPrivateProfileString("Device", "type", "", gb_mci_device_type,
        sizeof(gb_mci_device_type), ini_path);

    for (i = 0; i < MUSIC_BASIC_MAX; i++) {
        if (!angband_music_basic_name[i]) {
            continue;
        }
        gb_music_entries += gb_load_music_entry(ini_path, "Basic", angband_music_basic_name[i],
            gb_basic_music_file[i]);
    }
    for (i = 0; i < DUNGEON_MAX; i++) {
        sprintf(key, "dungeon%03d", i);
        gb_music_entries += gb_load_music_entry(ini_path, "Dungeon", key, gb_dungeon_music_file[i]);
    }
    for (i = 0; i < TOWN_MAX; i++) {
        sprintf(key, "town%03d", i);
        gb_music_entries += gb_load_music_entry(ini_path, "Town", key, gb_town_music_file[i]);
    }
    for (i = 0; i < QUEST_MAX; i++) {
        sprintf(key, "quest%03d", i);
        gb_music_entries += gb_load_music_entry(ini_path, "Quest", key, gb_quest_music_file[i]);
    }
    for (i = 0; i < MON_IDX_MAX; i++) {
        sprintf(key, "mon%04d", i);
        gb_music_entries += gb_load_music_entry(ini_path, "Monster_Low", key, gb_low_mon_music_file[i]);
        gb_music_entries += gb_load_music_entry(ini_path, "Monster_Med", key, gb_med_mon_music_file[i]);
        gb_music_entries += gb_load_music_entry(ini_path, "Monster_High", key, gb_high_mon_music_file[i]);
    }
}

void gb_audio_init(void)
{
    const char *log;

    if (gb_audio_ready) {
        return;
    }
    gb_audio_ready = 1;
    log = getenv("GB_AUDIO_LOG");
    gb_audio_log = (log && (log[0] != '0')) ? 1 : 0;
    log = getenv("GB_AUDIO_SILENT");
    gb_audio_silent = (log && (log[0] != '0')) ? 1 : 0;

    /* 道は `lib/xtra` の下（`main-win.c:6239` と同じ作り）。 */
    path_build(gb_dir_sound, sizeof(gb_dir_sound), ANGBAND_DIR_XTRA, "sound");
    path_build(gb_dir_music, sizeof(gb_dir_music), ANGBAND_DIR_XTRA, "music");

    gb_load_sound_prefs();
    gb_load_music_prefs();

    /*
     * **読めた数を必ず出す。**「鳴らない」ときに、設定が空なのか道が違うのか
     * ファイルが無いのかを分けられないと詰む。
     */
    fprintf(stderr, "[gensoband] audio: sound %d / music %d entries (device \"%s\")%s\n",
        gb_sound_entries, gb_music_entries, gb_mci_device_type,
        gb_audio_silent ? " [silent]" : "");
}

/*!
 * @brief コアが音の縁を投げてくるようにする（`use_sound` / `use_music`）。**起動時に 1 回。**
 * @details どちらも偽だと入口で落ちる——`sound()` は `util.c:1727`、
 * `select_floor_music()` は同 `:1760`。立てるのは本来 `main-win.c:2479` の仕事だが、
 * あの TU は組まない。`init_music_hack()` のもう 1 か所の呼び出し（`birth.c:8065`）は
 * **キャラクタ作成のときだけ**なので、在るセーブを読んだときは誰も立てない。
 * **音楽と効果音で別々に踏んだ**（曲が 1 度も選ばれず、直したら今度は効果音が出なかった）。
 *
 * 実際に鳴らすかどうかは画面側の設定が決める（`gb_audio_set_enabled`）。
 * ここを切ったままにすると、画面で入にしてもコアが縁を投げてこない。
 */
void gb_audio_enable_core(void)
{
    arg_sound = TRUE;
    use_sound = TRUE;
    arg_music = TRUE;
    use_music = TRUE;
}

void gb_audio_set_enabled(int sound_on, int music_on)
{
    gb_sound_on = sound_on ? 1 : 0;
    if (gb_music_on && !music_on) {
        gb_audio_stop_music();
    }
    gb_music_on = music_on ? 1 : 0;
}

void gb_audio_set_volume(int sound_index, int music_index)
{
    if ((sound_index < 0) || (sound_index >= 10)) {
        sound_index = 0;
    }
    if ((music_index < 0) || (music_index >= 10)) {
        music_index = 0;
    }
    gb_sound_volume = gb_volume_table[sound_index];
    gb_music_volume = gb_volume_table[music_index];
    gb_apply_music_volume();
}

void gb_audio_stop_music(void)
{
    gb_current_music_type = 0;
    gb_current_music_id = 0;
    if (!gb_mop.wDeviceID) {
        return;
    }
    mciSendCommand(gb_mop.wDeviceID, MCI_STOP, 0, 0);
    mciSendCommand(gb_mop.wDeviceID, MCI_CLOSE, 0, 0);
    gb_mop.wDeviceID = 0;
}

int gb_audio_play_sound(int v)
{
    char full[1024];
    int i;

    if (!gb_audio_ready || !gb_sound_on) {
        return 1;
    }
    if ((v < 0) || (v >= SOUND_MAX)) {
        return 1;
    }
    for (i = 0; i < GB_SAMPLE_MAX; i++) {
        if (!gb_sound_file[v][i]) {
            break;
        }
    }
    if (i == 0) {
        return 1;
    }
    path_build(full, sizeof(full), gb_dir_sound, gb_sound_file[v][Rand_external(i)]);
    if (gb_audio_log) {
        fprintf(stderr, "[gensoband] audio: sound %d (%s) -> %s\n", v,
            angband_sound_name[v] ? angband_sound_name[v] : "?", full);
    }
    /*
     * **音量は PCM を書き換えて当てる**（`PlaySound` に音量の引数が無い）。
     * 最大のときは 1 バイトも触らない道を通す——既定の鳴り方を変えないため。
     * 縮められなかった wav（壊れている・24bit・圧縮）も素の道へ逃がす。
     */
    if ((gb_sound_volume < 1000) && gb_play_sound_scaled(full)) {
        return 0;
    }
    if (gb_audio_silent) {
        return 0; /* 解決までは通した。鳴らすところだけ省く */
    }
    return PlaySound(full, 0, SND_FILENAME | SND_ASYNC) ? 0 : 1;
}

int gb_audio_play_music(int n, int v)
{
    cptr *table = NULL;
    cptr name = NULL;
    char full[1024];
    int i;

    if (!gb_audio_ready || !gb_music_on) {
        return 1;
    }
    /* すでに同じ曲が鳴っているなら何もしない（鳴らし直すと頭へ戻る）。 */
    if ((gb_current_music_type == n) && (gb_current_music_id == v)) {
        return 0;
    }

    switch (n) {
    case TERM_XTRA_MUSIC_BASIC:
        if ((v < 0) || (v >= MUSIC_BASIC_MAX)) return 1;
        table = gb_basic_music_file[v];
        break;
    case TERM_XTRA_MUSIC_DUNGEON:
        if ((v < 0) || (v >= DUNGEON_MAX)) return 1;
        table = gb_dungeon_music_file[v];
        break;
    case TERM_XTRA_MUSIC_TOWN:
        if ((v < 0) || (v >= TOWN_MAX)) return 1;
        table = gb_town_music_file[v];
        break;
    case TERM_XTRA_MUSIC_QUEST:
        if ((v < 0) || (v >= QUEST_MAX)) return 1;
        table = gb_quest_music_file[v];
        break;
    case TERM_XTRA_MUSIC_MON_L:
        if ((v < 0) || (v >= MON_IDX_MAX)) return 1;
        table = gb_low_mon_music_file[v];
        break;
    case TERM_XTRA_MUSIC_MON_M:
        if ((v < 0) || (v >= MON_IDX_MAX)) return 1;
        table = gb_med_mon_music_file[v];
        break;
    case TERM_XTRA_MUSIC_MON_H:
        if ((v < 0) || (v >= MON_IDX_MAX)) return 1;
        table = gb_high_mon_music_file[v];
        break;
    default:
        return 1;
    }

    for (i = 0; i < GB_SAMPLE_MUSIC_MAX; i++) {
        if (!table[i]) {
            break;
        }
    }
    if (i == 0) {
        return 1; /* この場面に曲が割り当てられていない。いま鳴っているものを続ける */
    }
    name = table[Rand_external(i)];
    if (strchr(name, (int)':')) {
        (void)strnfmt(full, sizeof(full), "%s", name);
    } else {
        path_build(full, sizeof(full), gb_dir_music, name);
    }

    gb_current_music_type = n;
    gb_current_music_id = v;
    if (gb_audio_log) {
        fprintf(stderr, "[gensoband] audio: music %d/%d -> %s\n", n, v, full);
    }
    if (gb_audio_silent) {
        return 0; /* 解決までは通した。MCI を開くところだけ省く */
    }
    if (!gb_make_mci_window()) {
        return 1;
    }

    gb_mop.lpstrDeviceType = gb_mci_device_type[0] ? gb_mci_device_type : NULL;
    gb_mop.lpstrElementName = full;
    gb_mop.dwCallback = (DWORD_PTR)gb_mci_hwnd;
    mciSendCommand(gb_mop.wDeviceID, MCI_STOP, 0, 0);
    mciSendCommand(gb_mop.wDeviceID, MCI_CLOSE, 0, 0);
    {
        /*
         * **開けなかったら黙らない。**mp3 を鳴らすには `[Device] type` に合った
         * デコーダが要る（既定は `MPEGVideo`）。黙って落ちると「設定は読めているのに
         * 鳴らない」の原因が分からなくなる。
         */
        const MCIERROR err = mciSendCommand(0, MCI_OPEN,
            (gb_mop.lpstrDeviceType ? MCI_OPEN_TYPE : 0) | MCI_OPEN_ELEMENT,
            (DWORD_PTR)&gb_mop);
        if (err != 0) {
            char reason[256];

            if (!mciGetErrorString(err, reason, sizeof(reason))) {
                strcpy(reason, "(unknown)");
            }
            fprintf(stderr, "[gensoband] audio: MCI could not open \"%s\": %s\n", full, reason);
            gb_mop.wDeviceID = 0;
            return 1;
        }
    }
    gb_apply_music_volume(); /* **鳴らす前に**当てる（後だと頭が最大音量で出る） */
    mciSendCommand(gb_mop.wDeviceID, MCI_SEEK, MCI_SEEK_TO_START, 0);
    mciSendCommand(gb_mop.wDeviceID, MCI_PLAY, MCI_NOTIFY, (DWORD_PTR)&gb_mop);
    return 0;
}

/*!
 * @brief 曲の終わりを取りに行く。**ゲームスレッドから定期的に呼ぶこと。**
 * @details メッセージ専用窓は誰かが `PeekMessage` しないと `MM_MCINOTIFY` を受け取れない。
 * コアは窓を持たない作りなので、既定のメッセージ回しが無い（`HengbandCore.exe` と同じ）。
 * ここを呼ばないと**曲が 1 回鳴って止まる**。
 */
void gb_audio_pump(void)
{
    MSG msg;

    if (!gb_mci_hwnd) {
        return;
    }
    while (PeekMessage(&msg, gb_mci_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

int gb_audio_mon_music_priority(int r_idx)
{
    if (!gb_audio_ready || (r_idx < 1) || (r_idx >= MON_IDX_MAX)) {
        return MON_MUSIC_PRIOR_NONE;
    }
    if (gb_high_mon_music_file[r_idx][0]) {
        return MON_MUSIC_PRIOR_HIGH;
    }
    if (gb_med_mon_music_file[r_idx][0]) {
        return MON_MUSIC_PRIOR_MED;
    }
    if (gb_low_mon_music_file[r_idx][0]) {
        return MON_MUSIC_PRIOR_LOW;
    }
    return MON_MUSIC_PRIOR_NONE;
}

int gb_audio_entry_counts(int *sound, int *music)
{
    if (sound) {
        *sound = gb_sound_entries;
    }
    if (music) {
        *music = gb_music_entries;
    }
    return gb_audio_ready;
}

#endif /* _WIN32 */
