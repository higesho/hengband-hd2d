/*!
 * @file an_profile.c
 * @brief `an_profile.h` の実体。
 */
#include "an_profile.h"

#include "angband.h"
#include "generate.h" /* struct cave_profile（name） */
#include "game-world.h" /* turn */
#include "init.h" /* ANGBAND_DIR_USER */
#include "ui-game.h" /* savefile[] */
#include "z-file.h" /* path_build */

#include <stdio.h>
#include <string.h>

/*! @brief メモリに持つ記録の上限。古いものから捨てる（1 階 1 行なので十分に大きい）。 */
#define AN_PROFILE_CAP 4096

typedef struct an_profile_entry {
    long long turn;
    int id;
} an_profile_entry;

static an_profile_entry s_entries[AN_PROFILE_CAP];
static int s_count = 0;
static int s_head = 0; /*!< 次に書く位置（環状） */

/*! @brief いまメモリの表が対応しているセーブ名（変わったら読み直す）。 */
static char s_loaded_for[256] = { 0 };

/*! @brief 名前と番号の対。 */
static const struct {
    const char *name;
    int id;
} s_names[] = {
    { "classic", 101 },
    { "modified", 102 },
    { "moria", 103 },
    { "labyrinth", 104 },
    { "cavern", 105 },
    { "lair", 106 },
    { "gauntlet", 107 },
    { "hard centre", 108 },
};

int an_profile_id_from_name(const char *name)
{
    size_t i;
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(s_names) / sizeof(s_names[0]); i++) {
        if (strcmp(s_names[i].name, name) == 0) {
            return s_names[i].id;
        }
    }
    return 0;
}

/*! @brief セーブファイルの名前の部分（ディレクトリと拡張子を除く）。無ければ "default"。 */
static void an_profile_save_base(char *out, size_t cap)
{
    const char *base = savefile;
    const char *p;
    size_t n;

    if (savefile[0] == '\0') {
        strncpy(out, "default", cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    for (p = savefile; *p; p++) {
        if ((*p == '/') || (*p == '\\')) {
            base = p + 1;
        }
    }
    n = strlen(base);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, base, n);
    out[n] = '\0';
    /* 安全な字だけにする（`savefile_set_name()` は make_safe で既に絞っているが、念のため）。 */
    for (p = out; *p; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            out[p - out] = '_';
        }
    }
}

static bool an_profile_file_path(char *out, size_t cap, const char *base)
{
    char leaf[320];
    if ((ANGBAND_DIR_USER == NULL) || (ANGBAND_DIR_USER[0] == '\0')) {
        return false;
    }
    if (snprintf(leaf, sizeof(leaf), "hd2d_profile_%s.txt", base) >= (int)sizeof(leaf)) {
        return false;
    }
    path_build(out, cap, ANGBAND_DIR_USER, leaf);
    return true;
}

static void an_profile_push(long long generated_turn, int id)
{
    s_entries[s_head].turn = generated_turn;
    s_entries[s_head].id = id;
    s_head = (s_head + 1) % AN_PROFILE_CAP;
    if (s_count < AN_PROFILE_CAP) {
        s_count++;
    }
}

/*! @brief セーブ名が変わっていたら（または初回なら）ファイルからメモリの表を作り直す。 */
static void an_profile_ensure_loaded(void)
{
    char base[256];
    char path[1024];
    FILE *fp;
    char line[128];

    an_profile_save_base(base, sizeof(base));
    if (strcmp(base, s_loaded_for) == 0) {
        return;
    }
    strncpy(s_loaded_for, base, sizeof(s_loaded_for) - 1);
    s_loaded_for[sizeof(s_loaded_for) - 1] = '\0';
    s_count = 0;
    s_head = 0;
    if (!an_profile_file_path(path, sizeof(path), base)) {
        return;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return; /* まだ 1 階も作っていない（正常）。 */
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        long long t = 0;
        int id = 0;
        if (sscanf(line, "%lld %d", &t, &id) == 2) {
            an_profile_push(t, id);
        }
    }
    fclose(fp);
}

void an_cave_profile_chosen(struct player *p, const struct cave_profile *profile)
{
    int id;
    char base[256];
    char path[1024];
    FILE *fp;

    (void)p;
    if (profile == NULL) {
        return;
    }
    id = an_profile_id_from_name(profile->name);
    if (id == 0) {
        return; /* 町（"town"）や知らない名前は記録しない＝既定の意匠。 */
    }
    an_profile_ensure_loaded();
    /*
     * 同じ turn で作り直された（builder が失敗して `cave_generate()` がやり直した）ときは
     * 後の記録が勝つ——`an_profile_id_for_turn()` が新しいほうから探す。
     */
    an_profile_push((long long)turn, id);

    an_profile_save_base(base, sizeof(base));
    if (!an_profile_file_path(path, sizeof(path), base)) {
        return;
    }
    fp = fopen(path, "ab");
    if (fp == NULL) {
        return; /* 書けなくてもゲームは止めない（この実行の間はメモリの表が効く）。 */
    }
    fprintf(fp, "%lld %d %s\n", (long long)turn, id, profile->name);
    fclose(fp);
}

int an_profile_id_for_turn(long long generated_turn)
{
    int i;
    if (generated_turn <= 0) {
        return 0;
    }
    an_profile_ensure_loaded();
    /* 新しいほうから。 */
    for (i = 0; i < s_count; i++) {
        int at = (s_head - 1 - i + AN_PROFILE_CAP) % AN_PROFILE_CAP;
        if (s_entries[at].turn == generated_turn) {
            return s_entries[at].id;
        }
    }
    return 0;
}
