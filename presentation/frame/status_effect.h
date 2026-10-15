/*!
 * @file status_effect.h
 * @brief バフ・デバフ（かかっている状態）を画面へ運ぶ形
 *
 * ## 範囲は原作の最下段と同じ
 * 載せるのは**原作が画面の最下段に出している状態だけ**（変愚なら `view/status-bars-table.cpp` の
 * 表と、速さ・朦朧・切り傷・空腹の欄）。原作が見せない情報（残りターンなど）は載せない
 * ——原作より情報が増えるため。
 *
 * ## 何で運ぶか
 * - `key`：アイコンを引く**名前**（`haste`・`res_fire` など）。番号で運ばない——コアごとに
 *   番号が違っても、名前なら画面側の表は 1 つで済む（`SoundEvent` と同じ考え方）。
 *   画面側が知らない名前は汎用のアイコンに落ちる。
 * - `label_utf8`：アイコンの下に出す**名前の文字**。コアが自分の言語で書く（日英の切り替えは
 *   コアの側にある）。
 * - `amount_utf8`：アイコンの上に出す**効果量**（`+10` など）。原作が値を見せているものだけ。
 *   無ければ空。
 * - `kind`：画面のどちら側に置くか（良い効果は玉の左、悪い効果は右）。
 *
 * @note ここは**追加だけ**の変更なのでプロトコルの版は上げない（v1 §3.2）。
 * 送らないコアは空のまま——画面側はアイコンを 1 つも出さないだけである。
 */
#pragma once

#include <cstdint>
#include <string>

//! 置く側。
enum class StatusEffectKind : uint8_t {
    Good = 0, //!< 良い効果（玉の左）
    Bad = 1, //!< 悪い効果（玉の右）
    Waiting = 2, //!< 待っている状態（帰還待ち・現実変容待ち。玉の左の外側）
    Special = 3, //!< どちらとも言えないもの（変愚の「つよし」。玉の左の外側）
    /*
     * ---- ここから下は**常時の効果**（装備・種族・職業による。2026-10-08 に足した）----
     * 置き場は玉の横ではなく、左のキャラクター情報の枠（状態列）の右に縦並び。
     * 範囲は原作のキャラクター情報（`C` コマンド）の耐性・能力の表と同じで、**鑑定済みの分だけ**。
     */
    Permanent = 4, //!< 常時の良い効果（耐性・能力・感知・維持・オーラ）
    PermanentBad = 5, //!< 常時の悪い効果（装備の呪い）
};

//! 線の上で受け付ける最大の値。これより大きい値は**その 1 件だけ捨てる**（知らない種類は既定へ落とす約束）。
constexpr uint8_t kStatusEffectKindMax = static_cast<uint8_t>(StatusEffectKind::PermanentBad);

struct StatusEffectView {
    std::string key;
    std::string label_utf8;
    std::string amount_utf8;
    StatusEffectKind kind{ StatusEffectKind::Good };
};
