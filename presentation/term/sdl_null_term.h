/*!
 * @file sdl_null_term.h
 * @brief SDL UI 用 null Term（設計書 §5.5 / PHASE2_PLAN §6.2）
 *
 * FLAG ON 時、最終画素は SDL のみ。Term は画素を出さない（no-op フック）。
 * z-term の内部バッファはコアが更新し続ける（整合維持）。
 * TERM_XTRA_FRESH で render seam、TERM_XTRA_EVENT で input seam を発火する。
 *
 * term_type / angband_terms / TERM_XTRA_ を参照するのはこの presentation TU のみ。
 * ui/ には一切現れない（§7.3 維持）。
 */
#pragma once

#include "frame/ui_seam.h"

#include "term/z-term.h"

class PlayerType;

namespace presentation {

class Bridge;

/*!
 * @brief 画素を出さない Term。描画/入力はコールバック seam 経由で ui へ委譲する。
 */
class SdlNullTerm {
public:
    SdlNullTerm() = default;

    /*!
     * @brief null term を生成し angband_terms[0] に設置・activate する。
     * @param player コア公開型（Bridge へ渡す）
     * @param bridge GameFrame 生成器（presentation）
     * @param seam ui 機能（present / pump_input / delay / quit）
     */
    void install(PlayerType *player, Bridge *bridge, const UiSeam &seam);

    //! フック内部から使う（静的ディスパッチ用）。
    void on_fresh();
    errr on_event(int wait);
    void on_flush();
    void on_delay(int ms);

    /*!
     * @brief リアルタイム: 締切までコマンド入力を待つ
     * @details キーが入れば普通に戻る。締切が来ても何も無ければ `RealtimeClock::set_holding(true)`
     * を立てて戻る（＝見送り）。**締切そのものはここで進めない。** 進めるのは `on_rt_pace` だけで、
     * 両方で進めると 1 刻みに 2 回進んで速度が半分になる。
     */
    void on_rt_input();

    //! リアルタイム: 次の刻みの境界まで、描画と入力吸い上げを回しながら待つ。
    void on_rt_pace();

    /*!
     * @brief 小窓の裏で世界を 1 刻み進める
     * @return 進めて札（またはESC）を積んだら true
     * @details 許された小窓（`RealtimeClock::is_prompt_background_allowed`）で、
     * 刻みの境界に来たときだけ働く。**再入は自分で止める。**
     */
    bool rt_step_world_in_prompt();

private:
    //! 現在時刻（ms）。seam に時計が無ければ 0 を返す（＝リアルタイム不能）。
    uint32_t rt_now_ms() const;
    //! 締切を張り直す必要があれば張り直す。リアルタイムが使えないなら false。
    bool rt_begin(int &ms_per_tick);
    //! ui から吸い上げ、**論理キー 1 個だけ**を積む（先行入力 1 枠）。
    void rt_pump_single_key();
    //! Term のキュー待ちが空でないか（＝先行入力が既に入っているか）。
    bool rt_has_pending_key() const;

    term_type term_{};
    PlayerType *player_{nullptr};
    Bridge *bridge_{nullptr};
    UiSeam seam_{};
    //! 次の刻みの境界（絶対 ms）。**締切を持つのは Term 側**でコアではない。
    uint32_t rt_next_tick_at_{0};
};

} // namespace presentation
