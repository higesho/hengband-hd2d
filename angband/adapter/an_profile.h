/*!
 * @file an_profile.h
 * @brief 階の作り方（原作の cave profile）を画面へ送るための記録（ A6。
 * 意匠は ）。
 *
 * @details 原作の `generate.c` の `choose_profile()` は、階を作るたびに classic / modified /
 * moria / labyrinth / cavern / lair / gauntlet / hard centre のどれで作るかを決めるが、
 * その選択は `dun->profile` に置かれるだけで**階にもセーブにも残らない**。
 * A6 の手入れは `choose_profile()` が返す直前に `an_cave_profile_chosen()` を呼ぶ
 * （弱い既定つき。実体はここ）。
 *
 * 記録の鍵は**階を作った turn**（`cave_generate()` が `chunk->turn = turn` に入れる値。
 * `choose_profile()` はその同じ `cave_generate()` の中で、turn が進む前に呼ばれる）。
 * アダプタは `(turn, 作り方の番号)` を
 *   - メモリの表（この実行の間）と
 *   - `ANGBAND_DIR_USER/hd2d_profile_<セーブ名>.txt`（次の起動でも読める）
 * の両方へ残し、`an_get_floor_info()` が `cave->turn` で引いて `profile_id` に入れる。
 * **セーブを読み込み直した階**は原作が `cave->turn` をセーブから戻すので、同じ鍵で引ける。
 * 記録に無い階（このファイルを足す前に作った階など）は 0 ＝ 既定の意匠（classic と同じ絵）。
 *
 * 番号は （101〜108）。町（"town"）は記録しない。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct player;
struct cave_profile;

/*!
 * @brief `generate.c` の A6 の強シンボル。`choose_profile()` が返す直前に呼ばれる。
 * @details 作り方の名前を番号に直し、`turn` を鍵に記録する。**選択は変えない**（観察だけ）。
 */
void an_cave_profile_chosen(struct player *p, const struct cave_profile *profile);

/*!
 * @brief 作り方の名前 → 番号（§3）。知らない名前と "town" は 0。
 */
int an_profile_id_from_name(const char *name);

/*!
 * @brief 階を作った turn（`cave->turn`）から作り方の番号を引く。
 * @return 101〜108。記録に無ければ 0。
 */
int an_profile_id_for_turn(long long generated_turn);

#ifdef __cplusplus
} /* extern "C" */
#endif
