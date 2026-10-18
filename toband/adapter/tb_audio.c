/*!
 * @file tb_audio.c
 * @brief 効果音（の音）。
 *
 * ## どこまでがコアの仕事か
 * **何を鳴らすかはコアが決めている。** `sound()`（`util.c`）が `Term_xtra(TERM_XTRA_SOUND, v)`
 * を投げてくる。ここがやるのは**「番号 → `sound.cfg` の行 → 鳴らす」だけ**である。
 *
 * ## BGM は無い
 * TOband（変愚蛮怒 1.5.1 系）には BGM の仕組み（`TERM_XTRA_MUSIC_*`・`use_music`・
 * `music.cfg`）が無い。幻想蛮怒の `gb_audio.c` から MCI（曲）の部分をすべて外した。
 * 画面側へ渡す口（`tb_audio_play_music` ほか）は形だけ残し、何もしない・数は 0 を返す。
 *
 * ## 鳴らす口
 * 効果音は `PlaySound()`（winmm）。wav だけ。音量は PCM を書き換えて当てる（`tb_scale_wav`）。
 * 素材（`lib/xtra/sound/*.wav`）は原作の配布物に無い場合があり、その場合は鳴らない。
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。ただし**コアへ渡す文字列はすべて SJIS**。
 */

#include "angband.h"

#include "tb_shim.h"

#if !defined(_WIN32)

/*
 * ## Windows 以外（Android / Quest。2026-08-21）
 *
 * この TU の中身は **winmm そのもの**である（効果音は `PlaySound`。BGM は無い）。
 * Android に winmm は無く、素材（`lib/xtra/sound`）も TOband には
 * 付いてこないので、**まず黙って動くこと**を採って空実装にする。
 *
 * **効果音は画面側が鳴らす**（`ui_state.sound_events` が真のとき）。
 * コアがするのは、原作の `sound()` が出した音の名前を `frame.sounds` へ書き留めることだけで、
 * それは winmm に依らない（`tb_null_term.c` の `TERM_XTRA_SOUND`）。だから Windows 以外でも
 * `tb_audio_enable_core()` で原作の `use_sound` を立て、`tb_main.cpp` は `audio` を申告する
 * （決めたこと 2026-10-18「他の OS でも鳴らして」）。コア自身が鳴らす口（画面側の音の装置が
 * 開けなかったとき）だけが空のまま。
 *
 * 鳴らすなら SDL2_mixer（`presentation/audio/sdl_mixer_audio.cpp` が変愚で使っている
 * 実体）へ載せ替えるのが素直な道である。 の未決 3 と同じ棚。
 */

void tb_audio_init(void) {}
void tb_audio_enable_core(void)
{
    /* 原作の `sound()` は `use_sound` が偽だと `Term_xtra` すら呼ばず、書き留める機会ごと消える */
    arg_sound = TRUE;
    use_sound = TRUE;
}
void tb_audio_set_enabled(int sound_on, int music_on) { (void)sound_on; (void)music_on; }
void tb_audio_set_volume(int sound_index, int music_index) { (void)sound_index; (void)music_index; }
void tb_audio_stop_music(void) {}
int tb_audio_play_sound(int v) { (void)v; return 0; }
int tb_audio_play_music(int n, int v) { (void)n; (void)v; return 0; }
void tb_audio_pump(void) {}
int tb_audio_mon_music_priority(int r_idx) { (void)r_idx; return 0; }

int tb_audio_entry_counts(int *sound, int *music)
{
    if (sound) {
        *sound = 0;
    }
    if (music) {
        *music = 0;
    }
    return 0; /* 用意できていない＝`tb_audio_ready` が偽のときと同じ返し */
}

#else /* _WIN32 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
/* `defines.h` の `GF_INERTIA`（属性）と `winuser.h` の `GF_INERTIA`（ジェスチャ）が衝突する（C4005）。
 * この TU は属性を使わないので、`windows.h` の前に外す。 */
#ifdef GF_INERTIA
#undef GF_INERTIA
#endif
#include <windows.h>
#include <mmsystem.h>

#include <string.h>

/*!
 * @name `main-win.c` の中に閉じている小道具の作り直し
 * @details 音のパス（`tb_dir_sound`）も、設定行を割る `tb_tokenize()` も、実在を見る
 * `tb_file_exists()` も、`main-win.c` の `static` である。あの TU は組まないので、
 * ここで同じものを持つ。パスの作り方（`ANGBAND_DIR_XTRA` の下の `sound`）は `main-win.c` と同じ。
 * @{
 */

static char tb_dir_sound[1024];

//! 実在するファイルか。
static int tb_file_exists(cptr path)
{
    FILE *fp = my_fopen(path, "rb");

    if (!fp) {
        return 0;
    }
    my_fclose(fp);
    return 1;
}

/*!
 * @brief 設定行を空白で割る。**`<>` で囲めば空白入りの名前も 1 つ**。
 * @return 割った数
 */
static int tb_tokenize(char *buf, int num, char **tokens)
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
 * 効果音の候補選び用の乱数。ゲーム本体の乱数列（`Rand_value` など）を進めないための別系統。
 * TOband には変愚の `Rand_external()` が無いので、小さな線形合同法で持つ。
 */
static unsigned long tb_audio_rand_state = 2463534242UL;

static int tb_audio_pick(int n)
{
    tb_audio_rand_state = tb_audio_rand_state * 1664525UL + 1013904223UL;
    return (n <= 1) ? 0 : (int)((tb_audio_rand_state >> 8) % (unsigned long)n);
}

/*! 1 項目につき候補は最大 8 つ（`main-win.c` の `SAMPLE_MAX` と同じ値）。 */
#define TB_SAMPLE_MAX 8

/*! 効果音。`[Sound]` の 1 項目につき候補が最大 8 つ。 */
static cptr tb_sound_file[SOUND_MAX][TB_SAMPLE_MAX];

static int tb_audio_ready = 0;
/*! 画面側の設定（`ui_state.audio`）。**既定は切**——鳴らすと決めるのは画面側。 */
static int tb_sound_on = 0;
/*!
 * 効果音の音量（0〜1000）。**既定は最大。**
 * @details `PlaySound` に音量の引数は無いが、**渡す前に PCM を書き換えれば効く**
 * （`tb_scale_wav`）。振幅を `volume/1000` 倍にしてから `SND_MEMORY` で鳴らす。
 * 縮め方は変愚の `modulate_amplitude`（`main-win-sound.cpp`）と同じ。
 * @note 最大のときは 1 バイトも触らず、従来どおり `SND_FILENAME` で鳴らす。
 */
static int tb_sound_volume = 1000;

/*! 書き換えた PCM の器（`SND_MEMORY` で鳴らしている間は生かしておく）。 */
static unsigned char *tb_sfx_buf = NULL;
static long tb_sfx_buf_size = 0;

/*! これより大きい wav は器へ載せない（素のまま鳴らす）。効果音は普通 100KB 未満。 */
#define TB_SFX_MAX_BYTES (8L * 1024L * 1024L)

/*! 音量の添字（0 ＝ 100% … 9 ＝ 10%）→ 0〜1000。**変愚の `VOLUME_TABLE` と同じ表**。 */
static const int tb_volume_table[10] = { 1000, 800, 600, 450, 350, 250, 170, 100, 50, 20 };

/*! 読み込めた項目の数（起動時に stderr へ出す。設定が空なのかパスが違うのかを分ける）。 */
static int tb_sound_entries = 0;

/*! `TB_AUDIO_LOG=1` のとき、鳴らすたびに stderr へ 1 行出す（検査用）。 */
static int tb_audio_log = 0;
/*!
 * `TB_AUDIO_SILENT=1`（または `HENGBAND_MUTE=1`）のとき、**ファイルの解決までは通して、
 * 鳴らす直前で止める**。検査のたびに実際に音が出ないようにするため。
 */
static int tb_audio_silent = 0;

/*!
 * @brief PCM の振幅を `volume/1000` 倍にする（**その場で書き換える**）。
 * @param bits 1 標本のビット数。**8（符号なし・中心 128）と 16（符号付き）だけ**扱う
 * @return 縮めたら 1。触れない形式なら 0（呼び側は素のファイルで鳴らす）
 * @details 変愚の `modulate_amplitude` と同じ計算にしてある——片方だけ変えると、
 * 同じ段なのにコアによって大きさが違う、という直しにくい違いになる。
 */
static int tb_modulate_pcm(unsigned char *pcm, long size, int bits, int volume)
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
static int tb_scale_wav(unsigned char *buf, long size, int volume)
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
            return tb_modulate_pcm(buf + body, (long)len, bits, volume);
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
 * @note `TB_AUDIO_SILENT=1` でも**ここまでは通す**（読み込みと縮めまで）。
 * 省くのは `PlaySound` の 1 行だけ——検査が「実物の wav を縮められるか」を
 * 見られるようにするため（記憶 `hengband-check-must-not-copy-the-draw` と同じ考え）。
 */
static int tb_play_sound_scaled(cptr path)
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
    if ((size <= 0) || (size > TB_SFX_MAX_BYTES) || (fseek(fp, 0, SEEK_SET) != 0)) {
        fclose(fp);
        return 0;
    }
    PlaySound(NULL, NULL, 0); /* **先に止める。**この後で器を書き換えるから */
    if (tb_sfx_buf_size < size) {
        unsigned char *grown = (unsigned char *)realloc(tb_sfx_buf, (size_t)size);
        if (!grown) {
            fclose(fp);
            return 0;
        }
        tb_sfx_buf = grown;
        tb_sfx_buf_size = size;
    }
    got = fread(tb_sfx_buf, 1, (size_t)size, fp);
    fclose(fp);
    if ((long)got != size) {
        return 0;
    }
    if (!tb_scale_wav(tb_sfx_buf, size, tb_sound_volume)) {
        return 0;
    }
    if (tb_audio_silent) {
        if (tb_audio_log) {
            fprintf(stderr, "[toband] audio: scaled %ld bytes to %d/1000 (not played)\n",
                size, tb_sound_volume);
        }
        return 1; /* 縮めるところまでは通した。鳴らすところだけ省く */
    }
    {
        /*
         * **鳴らせたかどうかを出す。**書き換えた器を `SND_MEMORY` で渡しているので、
         * 頭（RIFF）を壊していれば偽が返る。偽なら呼び側が素のファイルへ逃がす
         * ——大きさは元のままになるが、**黙るよりまし**。
         */
        const int played = PlaySound((cptr)tb_sfx_buf, NULL, SND_MEMORY | SND_ASYNC) ? 1 : 0;
        if (tb_audio_log) {
            fprintf(stderr, "[toband] audio: scaled %ld bytes to %d/1000 (played=%d)\n",
                size, tb_sound_volume, played);
        }
        return played;
    }
}

/*! @brief `sound.cfg` を読む。 */
static void tb_load_sound_prefs(void)
{
    char ini_path[1024];
    char tmp[1024];
    char full[1024];
    char *zz[TB_SAMPLE_MAX];
    int i;
    int j;
    int num;

    path_build(ini_path, sizeof(ini_path), tb_dir_sound, "sound.cfg");
    for (i = 0; i < SOUND_MAX; i++) {
        if (!angband_sound_name[i][0]) {
            continue;
        }
        GetPrivateProfileString("Sound", angband_sound_name[i], "", tmp, sizeof(tmp), ini_path);
        num = tb_tokenize(tmp, TB_SAMPLE_MAX, zz);
        for (j = 0; j < num; j++) {
            path_build(full, sizeof(full), tb_dir_sound, zz[j]);
            if (!tb_file_exists(full)) {
                continue;
            }
            tb_sound_file[i][j] = string_make(zz[j]);
            tb_sound_entries++;
        }
    }
}

void tb_audio_init(void)
{
    const char *log;

    if (tb_audio_ready) {
        return;
    }
    tb_audio_ready = 1;
    log = getenv("TB_AUDIO_LOG");
    tb_audio_log = (log && (log[0] != '0')) ? 1 : 0;
    log = getenv("TB_AUDIO_SILENT");
    tb_audio_silent = (log && (log[0] != '0')) ? 1 : 0;
    /* `HENGBAND_MUTE=1` でも黙る（検査用。決めたこと 2026-09-26） */
    log = getenv("HENGBAND_MUTE");
    if (log && (log[0] != '0')) {
        tb_audio_silent = 1;
    }

    /* パスは `lib/xtra` の下（`main-win.c` と同じ作り）。 */
    path_build(tb_dir_sound, sizeof(tb_dir_sound), ANGBAND_DIR_XTRA, "sound");

    tb_load_sound_prefs();

    /* **読めた数を必ず出す。**「鳴らない」ときに、設定が空なのかパスが違うのかを分けるため。 */
    fprintf(stderr, "[toband] audio: sound %d / music 0 entries (no BGM in TOband)%s\n",
        tb_sound_entries, tb_audio_silent ? " [silent]" : "");
}

/*!
 * @brief コアが効果音の縁を投げてくるようにする（`use_sound`）。**起動時に 1 回。**
 * @details 偽だと `sound()` は入口で戻る。立てるのは本来 `main-win.c` の仕事だが、
 * あの TU は組まない。実際に鳴らすかどうかは画面側の設定が決める（`tb_audio_set_enabled`）。
 */
void tb_audio_enable_core(void)
{
    arg_sound = TRUE;
    use_sound = TRUE;
}

void tb_audio_set_enabled(int sound_on, int music_on)
{
    (void)music_on; /* BGM は無い */
    tb_sound_on = sound_on ? 1 : 0;
}

void tb_audio_set_volume(int sound_index, int music_index)
{
    (void)music_index; /* BGM は無い */
    if ((sound_index < 0) || (sound_index >= 10)) {
        sound_index = 0;
    }
    tb_sound_volume = tb_volume_table[sound_index];
}

void tb_audio_stop_music(void) {}

int tb_audio_play_sound(int v)
{
    char full[1024];
    int i;

    if (!tb_audio_ready || !tb_sound_on) {
        return 1;
    }
    if ((v < 0) || (v >= SOUND_MAX)) {
        return 1;
    }
    for (i = 0; i < TB_SAMPLE_MAX; i++) {
        if (!tb_sound_file[v][i]) {
            break;
        }
    }
    if (i == 0) {
        return 1;
    }
    path_build(full, sizeof(full), tb_dir_sound, tb_sound_file[v][tb_audio_pick(i)]);
    if (tb_audio_log) {
        fprintf(stderr, "[toband] audio: sound %d (%s) -> %s\n", v,
            angband_sound_name[v][0] ? angband_sound_name[v] : "?", full);
    }
    /*
     * **音量は PCM を書き換えて当てる**（`PlaySound` に音量の引数が無い）。
     * 最大のときは 1 バイトも触らない道を通す。縮められなかった wav（壊れている・24bit・圧縮）も
     * 素の道へ逃がす。
     */
    if ((tb_sound_volume < 1000) && tb_play_sound_scaled(full)) {
        return 0;
    }
    if (tb_audio_silent) {
        return 0; /* 解決までは通した。鳴らすところだけ省く */
    }
    return PlaySound(full, 0, SND_FILENAME | SND_ASYNC) ? 0 : 1;
}

/*! BGM は無い。戻り値 1 ＝ 鳴らせなかった。 */
int tb_audio_play_music(int n, int v)
{
    (void)n;
    (void)v;
    return 1;
}

/*! 曲が無いので汲み上げる物も無い。 */
void tb_audio_pump(void) {}

int tb_audio_mon_music_priority(int r_idx)
{
    (void)r_idx;
    return 0;
}

int tb_audio_entry_counts(int *sound, int *music)
{
    if (sound) {
        *sound = tb_sound_entries;
    }
    if (music) {
        *music = 0;
    }
    return tb_audio_ready;
}

#endif /* _WIN32 */
