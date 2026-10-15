#pragma once
/*!
 * @file look_screen_cover.h
 * @brief 見る・狙うの途中で、ゲーム本体が画面を退避して別の画面（モンスターの思い出など）を重ねているかを覚える。
 * @details 見る（`l`）や狙う（`*`）の最中に `r` を押すと、原作は `screen_save()` で画面を退避してから
 * モンスターの思い出（`screen_roff()` ほか）を描く。行 0 の問い（`… [r,q,p,o,+,-,<dir>]`）は退避の前から残ったままなので、
 * 各コアのアダプタがそれを「見るの帯」へ作り直して送り続けると、画面側は帯だけを描いて本文（思い出の字）を出さず、
 * 中央が真っ暗になった（2026-10-09 の気づいたこと）。
 *
 * 以前は画面側が帯の**文言**（`LB/RB:` で始まり「カーソル移動」を含む）で重なりを見分けていた
 * （`look_prompt_covered`。2026-10-09 のコミット 9d5ceee333）。弱いので作り直した（遊ぶ人と合意 2026-10-09）:
 * **ゲーム本体が画面を退避した深さ（`screen_save()` の入れ子の数。コアごとに `character_icky` などで読める）を、
 * 各コアのアダプタが見るの帯を作るときに比べ、深くなっている間は帯を送らない**。画面側は今までの規則で本文の窓を出す。
 *
 * 基準の深さは**見るの帯を作った最初のフレーム**で覚える（見る自体が画面を退避する核があっても、
 * その深さは重なりとは数えない）。帯を作らなかったフレームで忘れる（見るをやめた）。
 */
namespace presentation {
class LookScreenCover {
public:
    //! フレームの頭と尻で呼ぶ。見るの帯を作ろうとしなかったフレームでは基準を忘れる。
    struct Frame {
        explicit Frame(LookScreenCover &cover) : cover_(cover) { cover_.seen_ = false; }
        ~Frame() { if (!cover_.seen_) cover_.base_ = -1; }
        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;
    private:
        LookScreenCover &cover_;
    };
    /*!
     * @brief 見るの帯を作ろうとしているときに呼ぶ。
     * @param depth いまの画面の退避の深さ（コアが数えている値。`screen_save()` で増え、`screen_load()` で減る）
     * @return 真なら別の画面が重なっているので、帯を作らない。
     */
    bool covered(int depth) {
        seen_ = true;
        if (base_ < 0) base_ = depth;
        return depth > base_;
    }
private:
    bool seen_{false};
    int base_{-1};
};
//! コアは 1 プロセスに 1 つなので、見るの帯の状態も 1 つ。
inline LookScreenCover &look_screen_cover() {
    static LookScreenCover cover;
    return cover;
}
}
