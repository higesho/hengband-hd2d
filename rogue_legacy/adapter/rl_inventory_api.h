#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rl_inventory_item_state {
    int key;
    int type;
    int quantity;
    unsigned int equipped_flags; /* 1 weapon, 2 armor, 4 left ring, 8 right ring */
    int cursed;
    int identified;
    int callable;
    char description[512];
} rl_inventory_item_state;

int rl_inventory_count(void);
/* 表示中の記号 A-Z に対応する原作のモンスター名（英語。訳は画面側で当てる）。 */
const char *rl_monster_name(int letter);
int rl_inventory_item(int index, rl_inventory_item_state *out);

/*
 * 光の届く範囲。y は画面の行（先頭行がメッセージ行）、x は桁。
 * どちらも読むだけでゲームの状態は変えない。ゲームが始まっていない間（地図が出ていない画）は呼ばないこと。
 *
 * rl_can_see: 原作の cansee()。いま見えているマス（同じ部屋で部屋が明るい、または近い）。盲目なら偽。
 * rl_light_reaches: 松明風の光（半径 1）が届くマス＝自分の周り 3x3 のうち見えているマス。盲目なら偽。
 */
int rl_can_see(int y, int x);
int rl_light_reaches(int y, int x);
/* 盲目なら真（攻撃の効果の記録が使う）。 */
int rl_player_blind(void);

/*
 * 状態のアイコン（`GameFrame::status_effects`。・CORE_HUD_ROLLOUT.md §2.3）。
 * 空腹・盲目・混乱・麻痺・捕まっている・加速・透明視・混乱させる手（5.4 は幻覚・浮遊・怪物感知も）・鑑定済みの指輪の効果を、
 * 並べて返す（書いた件数。cap を超えたぶんは捨てる）。kind は StatusEffectKind の値（0 良い・1 悪い・4 常時・5 呪い）。
 * 名前は画面の言語に合わせた UTF-8（日本語・英語）。どれも読むだけでゲームの状態は変えない。
 */
typedef struct rl_status_effect {
    char key[24];
    char label[48];
    char amount[12];
    int kind;
} rl_status_effect;

int rl_status_effects(rl_status_effect *out, int cap);
/* 状態のアイコンの集め方の自己検査（`--selftest-status` から）。0 なら全部合った。 */
int rl_status_effects_selftest(void);
/* テスト用: 環境変数 HENGBAND_TEST_STATUS があれば、ゲームが始まった最初の 1 回だけ本物の状態の旗を付ける。無ければ何もしない。 */
void rl_test_status_apply(void);
/* テスト用: 環境変数 HENGBAND_TEST_FX_ITEMS があれば、ゲームが始まった最初の 1 回だけ、指定した杖・巻物・武器を持ち物へ足す。無ければ何もしない。 */
void rl_test_fx_items_apply(void);

#ifdef __cplusplus
}
#endif
