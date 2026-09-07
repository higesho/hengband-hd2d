/*!
 * @file presentation_bridge.h
 * @brief コア状態 → GameFrame 一方向変換器（設計書 §5 / PHASE2 T1 / PHASE3 T0–T5）
 *
 * presentation 層はコア依存可。ただしヘッダはコア型を前方宣言に留め、
 * ui からは include させない（ui は GameFrame を presentation/frame から得る）。
 */
#pragma once

#include "frame/game_frame.h"

class PlayerType;

namespace presentation {

/*!
 * @brief 可視マップ・HUD・ヒントを読み取り専用で GameFrame に詰める。
 *
 * - コアは読取のみ（ルール変更・乱数消費・セーブ書込・描画目的の Term 書込なし）。
 * - SDL_* を呼ばない。
 * - view_* は set_view_size で動的（PHASE3 T0）。ui は本クラスを呼ばない（H1）。
 */
class Bridge {
public:
    Bridge() = default;

    /*!
     * @brief MainMap 収容セル数を UI 側レイアウトから渡す（下限 1）。
     * @details platform / seam.before_capture が capture 直前に呼ぶ（H1）。
     */
    void set_view_size(int view_w, int view_h);

    /*!
     * @brief 可視範囲の切り出しをプレイヤ中心に固定するか（既定 false＝従来どおり端で丸める）。
     * @param follow true でフロア端でも丸めず、プレイヤを常に可視範囲の中心へ置く。
     * @details **HD2D 経路のためのもの。** HD2D は 1 点透視で行数・列数を大きく増やして
     * 要求するため（既定 42° で 47×22）、コアのフロア（高々 66 行）に対して縦のスクロール
     * 余地がほとんど残らず、丸めるとカメラがフロア端に張り付いてプレイヤが画面の上下を泳ぐ。
     * SDL2 経路は従来どおり丸める（退行させない）ので、切替の引数をここに開ける。
     * どちらの経路が有効かを知っているのは ui なので、`set_view_size` と同じく
     * platform / `seam.before_capture` が capture 直前に画面側へ聞いて渡す（H1）。
     */
    void set_camera_follow_player(bool follow);

    /*!
     * @brief プレイヤ中心の可視矩形を map_info で走査し GameFrame を生成する。
     * @param player コア公開型 PlayerType（読取のみ）
     * @return fg/bg/ascii/tile_index を満たした GameFrame（UTF-8）
     */
    GameFrame capture(PlayerType *player);

    //! 可視ビューポートのセル数（横）。
    int view_cols() const { return view_cols_; }
    //! 可視ビューポートのセル数（縦）。
    int view_rows() const { return view_rows_; }

private:
    /*!
     * @brief K-40: `GameFrame::teleport_fx` を作る（コアは読取のみ）。
     * @details 「溜め」は帰還・現実変容の残りターン、「発動」は**プレイヤ格子の跳び**と
     * **階の入れ替わり**から見る。コアには転移を知らせる手段が無く、足せない（必守制約 1）。
     */
    void fill_teleport_fx(GameFrame &frame, PlayerType *player);

    uint64_t frame_counter_{};
    int view_cols_{40}; //!< set_view_size 前の安全値（固定 66×44 は廃止）
    int view_rows_{28};
    bool camera_follow_player_{false}; //!< true でフロア端の丸めを行わない（HD2D 経路）

    /*!
     * @name K-40: 転移検知の前フレーム値
     * @details `capture` は入力待ち中も 10ms ごとに回るので、ここは「前フレーム」ではなく
     * 「前回の capture」の値である。同じ状態が続く限り差分は 0 なので誤爆しない。
     * @{
     */
    int last_px_{ 0 };
    int last_py_{ 0 };
    bool has_last_pos_{ false };
    int32_t last_floor_stamp_{ 0 }; //!< `FloorType::generated_turn`（階の実体 id 代わり）
    bool has_floor_stamp_{ false };
    int recall_total_{ 0 }; //!< 帰還カウンタの初期値（溜めを 0..1 へ均す分母）
    int last_recall_remain_{ 0 };
    //! 帰還カウンタが 0 に落ちた game_turn。階が入れ替われば発動、変わらなければ打ち消し。
    int32_t recall_fired_turn_{ -1 };
    /*! @} */
};

/*!
 * @brief K-16 の選択肢抽出（`GameFrame::menu_choices`）の自己検査（`HENGBAND_SDL2_MENUCHOICE_SMOKE`）。
 * @return 0 なら全件 PASS。1 以上は失敗件数
 * @details 建物・店の Term ミラーは**町へ行かないと出せない**ので、コアの描画コード
 * （`src/market/building-service.cpp` / `src/store/cmd-store.cpp` / `src/view/display-store.cpp`）の
 * 書式そのままの画面を合成して抽出器に食わせる。コアの文言が変わればここが落ちる。
 * @note 文字列の解析しかしないので `init_angband` の前でも回る。
 */
int run_menu_choice_smoke();

/*!
 * @brief コアの自由文字入力（`askfor`）に入っているかを知る口を開ける。
 * @details `run_sdl_game` から一度だけ呼ぶ。`text_input_state_hook`
 * （`src/core/asking-player.h`）を登録する。
 * @note UI は `GameFrame::text_input_active` で受け取るので、直接この関数を見なくてよい。
 */
void install_text_input_hook();

//! いまコアが自由文字入力の中にいるか（`install_text_input_hook` 済みのとき有効）。
bool is_core_text_input_active();

/*!
 * @brief K-47: タイトル画を敷いてよい間かを立てる／降ろす。
 * @details `GameFrame::title_screen` へそのまま流れ、Renderer が背景画の出し分けに使う。
 * コアの状態からは「タイトルか birth か」を区別できないので、呼ぶ側が自分で宣言する。
 * @note **起動時から立っている**（データ初期化の進捗表示もタイトル画の上に出すため）。
 * したがって主な用途は**降ろす**側で、`sdl_choose_new_or_load()` を抜けるときに必ず降ろす
 * （降ろし忘れると birth のロール結果の裏にタイトル画が残る）。
 */
void set_title_screen(bool active);

} // namespace presentation
