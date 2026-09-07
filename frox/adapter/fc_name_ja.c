/*!
 * @file fc_name_ja.c
 * @brief 名前の組み立ての日本語版（設計 §6.3。フック #7 #8）。**純 C**。
 *
 * ## 設計 §6.3 からの変更（2026-08-25。J3 で決めた）
 *
 * 設計は「`object_desc()` を**写して日本語化しない**——ここに日本語の組み立てを
 * 別に書く」と言っていた。**写さないという判断はそのまま守る**が、書き方を
 * **後処理**へ改めた——`object_desc()` を**そのまま呼んで**組ませ、出てきた字だけを直す。
 *
 * | | 別に書く（設計の初案） | **後処理**（こちら） |
 * |---|---|---|
 * | 上流追随 | 先方が飾りを 1 つ足すたびに当方も足す | **ただで付いてくる** |
 * | `frox/src` への行 | 0 | **0**（同じ） |
 * | 大きさ | 1,000 行級（先方の `object_desc` は 1,500 行） | **200 行** |
 * | 取りこぼし | 銘・エゴ・発動・充填数・ダイス…を全部書き直すまで出ない | 出ない |
 *
 * 手本は `gensoband/adapter/gb_name_en.c`（英語化。**JP に組ませてから直す**）。
 * **同じソースツリーで既に通っている形**である。設計 §6.3 に追記した。
 *
 * ## 何を直しているか
 *
 * | 要素 | 英語 | 日本語 | どうやって |
 * |---|---|---|---|
 * | 冠詞 | `a` / `an` / `the` | **出さない** | `&` を落とした名前には先方も付けない（下の註）／敵は頭を削る |
 * | 複数形 | `-s` / `-es` | **出さない** | `~` を落としたので先方が付けない |
 * | 個数 | `3 Rations of Food` | `食料×3` | `OD_NAME_ONLY` で名前の長さを測り、その直後へ挟む |
 * | 所有格 | `the urchin's` | `小僧の` | 尻の `'s` を `の` へ |
 * | 属性の記号 | `(+3,+5)` | **そのまま** | 触らない（記号は訳さない） |
 *
 * > **なぜ個数を足さねばならないか（J3 で踏んだ穴）**
 * > 先方は**名前が `&` で始まるときだけ**冠詞と個数を書く（`flavor.c:1564`）。
 * > 日本語の名前は `& Ration~ of Food` の `&` と `~` を落としてあるので、
 * > 冠詞と複数形が消えるのは**狙いどおり**だが、**個数まで消える**。
 * > 3 個持っていても `食料` としか出ない。ここで足し直すまでが対である。
 *
 * ## 再入について
 * `object_desc()` を呼ぶと先頭でまたここへ来る（フック #7）。旗を立てている
 * 間は 0 を返すので、**英語の組み立てがそのまま走る**。深さは必ず 1 である。
 * ゲームのスレッド 1 本しかここへ来ないのでロックは要らない（`fc_lang.cpp` と同じ）。
 *
 * ## J3 で入れていないもの（設計 §8.2 の J4 / J5）
 * - **飾りの英語**（`{cursed}` `(charging)` `(2 charges)` `(empty)`）。これらは
 *   `object_desc_str()` が直に継ぎ足すので **`fc_tr()` を通らない**。訳すなら
 *   専用の後処理か、カタログを引く置き換えが要る（J5 の宿題）
 * - **敵の人称**（`it` / `he` / `him` / `itself`）と `your`。語彙なので J4
 * - **助数詞**（本・巻物・薬…）。設計 §6.3 の表は `tval` の表を引くと書いているが、
 *   同じ節の例は `軽傷の治療のポーション×3` である。**例のほうを採った**——
 *   `×N` はどの品でも同じ形で済み、読み違えようがない。助数詞にするかは
 * 見え方の決めごとなので自分で決める
 */

#include "angband.h"

#include "fc_lang_c.h"

#include <string.h>

/*! @brief `object_desc()` を呼び返している最中か（再入よけ）。 */
static int fc_in_object_desc = 0;

/*! @brief `monster_desc()` を呼び返している最中か。**器を分ける**（品と敵は別の道）。 */
static int fc_in_monster_desc = 0;

/* ==================================================================== 品の名前 */

/*!
 * @brief 尻に付く名前（**名のある宝**と**エゴ**）を頭へ回す。
 *
 * @param name_only `OD_NAME_ONLY` で組ませた名前（CP932）。ここを書き換える
 * @param cap       `name_only` の大きさ
 * @param o_ptr     `object_type *`
 * @return 1 = 回した（呼び手は `full` を組み直す）／0 = 何もしていない
 *
 * @details 先方は**base の後ろへ空白付きで継ぐ**（`flavor.c:1756` `:1767`）。
 * 英語は `Long Bow of Bard` / `Soft Leather Armour of Protection` でそれでよいが、
 * 日本語は**前へ付く**——`射手バルド王の弓` / `防護の軟革よろい`。
 *
 * 訳語の出どころは 2 つで、扱いが違う:
 *
 * | | 訳の在り処 | ここでどうするか |
 * |---|---|---|
 * | 名のある宝（392 件） | `lib-ja/edit/a_info.txt`（**J3 で訳済み**） | `a_name` が既に日本語。回すだけ |
 * | エゴ（160 種） | **カタログ**（`e_info` は訳さない） | `fc_tr()` で引いてから回す |
 *
 * **`e_info` の `N:` を訳さないのは、名前で指されているからである。**
 * `rooms.txt` と `q_*.txt` の `EGO(speed)` 154 件を `_lookup_ego()` が
 * **部分一致**で引き当てる（`init1.c:1506`）。しかも当たる番号は同じ行の
 * `OBJ()` の種別で変わるので、番号へ書き換える手（J3 の #11 #16 #17）が使えない。
 * **データは英語のまま置き、画へ出るときだけ訳す**——それがここである。
 *
 * **名前がまるごと置き換わるエゴ**（`& Endless Quiver~` のような `FULL_NAME`。
 * 9 種）は継がれてこない——`basenm` そのものになる。そちらは中で丸ごと差し替える。
 */
static int fc_suffix_to_front(char *name_only, size_t cap, object_type *o_ptr)
{
    const char *en = NULL;
    const char *ja = NULL;
    char        work[MAX_NLEN + 200];
    char        tail[MAX_NLEN + 200];
    size_t      n_len, s_len;
    char       *at;

    if (o_ptr->name1) {
        en = a_name + a_info[o_ptr->name1].name;
        ja = en; /* J3 で訳してある（`lib-ja/edit`）。引き直さない */
    } else if (o_ptr->name2) {
        en = e_name + e_info[o_ptr->name2].name;
        ja = fc_tr(en);
        if (ja == en) {
            return 0; /* エゴの訳が無い。**英語のまま尻に置く**（制約 4） */
        }
        /*
         * **名前がまるごと置き換わるエゴ**（`OF_FULL_NAME`。9 種）。
         * `flavor.c:1517` が `basenm` を丸ごとエゴの名前にするので、
         * **継がれてこない**——`name_only` がそのままそれである
         * （`OD_NAME_ONLY` は素の名前で切る）。だから丸ごと差し替える。
         *
         * 綴りを突き合わせないのは、`~`（複数形）の展開が
         * `Harness~` → `Harnesses` のように一様でないからである。
         * **旗を見れば綴りに依らない。**
         */
        {
            u32b flgs[OF_ARRAY_SIZE];

            obj_flags(o_ptr, flgs);
            if (have_flag(flgs, OF_FULL_NAME)) {
                if (strlen(ja) >= cap) {
                    return 0;
                }
                my_strcpy(name_only, ja, (int)cap);
                return 1;
            }
        }
    } else {
        return 0;
    }

    if (!en || !en[0] || !ja || !ja[0]) {
        return 0;
    }

    n_len = strlen(name_only);
    s_len = strlen(en);
    if ((n_len < s_len + 2) || ((n_len + strlen(ja) + 1) >= cap)) {
        return 0;
    }

    /* **尻に、空白付きで、そのまま継がれている**ときだけ回す。 */
    at = name_only + n_len - s_len;
    if ((at <= name_only) || (at[-1] != ' ') || (strcmp(at, en) != 0)) {
        return 0;
    }

    my_strcpy(tail, name_only, (int)(at - name_only)); /* base（末尾の空白は落ちる） */
    strnfmt(work, (uint)sizeof(work), "%s%s", ja, tail);
    my_strcpy(name_only, work, (int)cap);
    return 1;
}

int fc_object_desc_ja(char *buf, const void *obj, unsigned long mode)
{
    object_type *o_ptr = (object_type *)obj;
    u32b inner;
    int number;
    size_t head;
    char name_only[MAX_NLEN + 200];
    char full[MAX_NLEN + 200];
    char work[MAX_NLEN + 240];

    if (!fc_lang_enabled() || fc_in_object_desc) {
        return 0;
    }
    if (!buf || !o_ptr) {
        return 0;
    }

    /*
     * 色の包み（`<color:c>…</color>`）は**外して組み、最後に自分で巻き直す**。
     * 付けたまま長さを測ると `name_only` の尻に `</color>` が入って、
     * `full` の頭と一致しなくなる（個数を挟む場所が分からなくなる）。
     */
    inner = (u32b)mode & ~(u32b)OD_COLOR_CODED;

    fc_in_object_desc = 1;
    object_desc(name_only, o_ptr, inner | OD_OMIT_PREFIX | OD_NAME_ONLY);
    object_desc(full, o_ptr, inner | OD_OMIT_PREFIX);
    fc_in_object_desc = 0;

    /*
     * `OD_NAME_ONLY` は `object_desc_done` へ跳ぶだけ（`flavor.c:1806`）なので、
     * **`full` は必ず `name_only` で始まる**。念のため確かめ、崩れていたら
     * 個数を足さずに素の字を返す（捏造しない）。
     */
    head = strlen(name_only);
    if ((head == 0) || (strncmp(full, name_only, head) != 0)) {
        head = strlen(full);
    } else if (fc_suffix_to_front(name_only, sizeof(name_only), o_ptr)) {
        /*
         * **`full` は `name_only` ＋ 飾りである。** 名前だけ回したら、
         * 飾りを付け直して `full` も組み直す（`head` も測り直す）。
         */
        char rest[MAX_NLEN + 200];

        my_strcpy(rest, full + head, sizeof(rest));
        strnfmt(full, (uint)sizeof(full), "%s%s", name_only, rest);
        head = strlen(name_only);
    }

    number = (int)o_ptr->number;

    if (mode & OD_OMIT_PREFIX) {
        /* 呼び手が「数は要らない」と言っている（`OD_LORE` など）。そのまま返す。 */
        my_strcpy(work, full, sizeof(work));
    } else if (number > 1) {
        /* `×` は CP932 の 0x81 0x7E。飾り（`full + head`）はそのまま尻へ戻す。 */
        strnfmt(work, (uint)sizeof(work), "%s\x81\x7e%d%s", name_only, number, full + head);
    } else {
        /*
         * 1 個・0 個。**0 個でも印を出さない**——英語の `no more X` に当たる字は
         * 包んでいる文（`You have no more %s.`）が持っており、あれは J4 で訳す。
         * ここで `×0` と出すと、訳した文と二重に「無い」と言うことになる。
         */
        my_strcpy(work, full, sizeof(work));
    }

    if (mode & OD_COLOR_CODED) {
        char attr = ((mode & OD_BLACK_CURSES) && (object_is_cursed(o_ptr)))
            ? 'D'
            : tval_to_attr_char(o_ptr->tval);
        char wrapped[MAX_NLEN + 260];

        sprintf(wrapped, "<color:%c>%s</color>", attr, work);
        my_strcpy(buf, wrapped, MAX_NLEN);
    } else {
        my_strcpy(buf, work, MAX_NLEN);
    }
    return 1;
}

/* ==================================================================== 敵の名前 */

/*!
 * @brief 頭が `prefix` なら削る。削ったら 1。
 * @details 削るのは**冠詞だけ**である。`your ` は「あなたの」という意味を持つので
 * 落とさない（語彙なので J4 で訳す）。
 */
static int fc_drop_prefix(char *s, const char *prefix)
{
    const size_t n = strlen(prefix);

    if (strncmp(s, prefix, n) != 0) {
        return 0;
    }
    memmove(s, s + n, strlen(s + n) + 1);
    return 1;
}

/*! @brief 名前が日本語（CP932 の 2 バイト文字）を含むか。 */
static int fc_has_wide(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p != '\0') {
        if (fc_is_sjis_lead((int)*p) && fc_is_sjis_trail((int)p[1])) {
            return 1;
        }
        p++;
    }
    return 0;
}

int fc_monster_desc_ja(char *desc, const void *mon, int mode)
{
    monster_type *m_ptr = (monster_type *)mon;
    char work[1024];
    size_t len;

    if (!fc_lang_enabled() || fc_in_monster_desc) {
        return 0;
    }
    if (!desc || !m_ptr) {
        return 0;
    }

    fc_in_monster_desc = 1;
    monster_desc(work, m_ptr, mode);
    fc_in_monster_desc = 0;

    /*
     * **名前が英語のままなら 1 バイトも触らない**（設計 §1 制約 4）。
     * 訳の無い敵（1,395 種のうち 347 種）で `the` だけ消えると、
     * `the Novice mage` が `Novice mage` になって英語として崩れる。
     */
    if (!fc_has_wide(work)) {
        strcpy(desc, work);
        return 1;
    }

    /* 冠詞（`monster2.c:2076` `:2088`）。`your ` は意味を持つので残す。 */
    if (!fc_drop_prefix(work, "the ")) {
        if (!fc_drop_prefix(work, "The ")) {
            if (!fc_drop_prefix(work, "an ")) {
                if (!fc_drop_prefix(work, "An ")) {
                    if (!fc_drop_prefix(work, "a ")) {
                        (void)fc_drop_prefix(work, "A ");
                    }
                }
            }
        }
    }

    /* 所有格。`小僧's` → `小僧の`。**同じ 2 バイトなので長さは増えない**。 */
    len = strlen(work);
    if ((len >= 2) && (work[len - 2] == '\'') && (work[len - 1] == 's')) {
        work[len - 2] = '\x82'; /* CP932 の `の` = 0x82 0xCC */
        work[len - 1] = '\xCC';
    }

    /* 削るか同じ長さにしかしていないので、呼び手の器に必ず収まる。 */
    strcpy(desc, work);
    return 1;
}
