/*!
 * @file map_click.h
 * @brief 地図のマスのクリック・タップ（AH-58。コア側は AH-57）。
 *
 * ## 何のためのものか
 * 原作の Angband は、地図のマスをクリックして歩く・走る・見る・文脈メニューを出すことができる
 * （`ui-context.c` の `textui_process_click()`）。窓からコアへ届く形は `input_event` の
 * `map_click`（ダンジョンのマスの座標・左右のボタン・Shift/Ctrl/Alt。仕様は
 * の AH-57）。ここは**画面側の仕事**——マウスと指の動きから
 * 「1 回のクリック」を組み、送ってよい場面だけ送る。
 *
 * ## 使うかどうか
 * `hello_ack.features` に `"map_click"` があるコアだけ（コア名では決めない。`hd2d_app.cpp`）。
 * 申告しないコアではここにある物は 1 つも呼ばれず、これまでのクリック（`click_path.h` の
 * 「クリックした所まで歩く」）のまま変わらない。
 *
 * ## 操作の割り当て
 * | 入力 | 送るもの |
 * | --- | --- |
 * | マウスの左ボタン（押して離す） | 左クリック |
 * | マウスの右ボタン（押して離す） | 右クリック（文脈メニュー） |
 * | 指の短いタップ | 左クリック |
 * | 指の長押し（`kLongPressMs`。動かさずに押し続ける） | 右クリック（押し続けて時間が来た瞬間に送る） |
 * | Shift / Ctrl / Alt | 押した瞬間のキーボードの状態をそのまま `mods` に載せる |
 *
 * 押してから離すまでに `kSlopPx` を超えて動いたら**ドラッグ**とみなし、クリックにしない。
 * 指が 2 本になったら（両手の操作やピンチ）どちらもクリックにしない。中ボタンは送らない。
 *
 * ## 送ってよい場面（AH-57 の契約）
 * 最新のフレームが、メニュー・問い・`-more-`・数量・文字入力・目標選びのどれでもないときだけ。
 * 走り・休憩・歩行の最中（`awaiting_command` が偽）でも送る——原作と同じく、どんな入力でも
 * 実行中の命令を止めるので、2 回目のクリックが歩行を止める入力になる。判断は `map_click_frame_block_reason()`。
 * 送ったあとは次のフレームが届く（または `MapClickGate::kReplyTimeoutMs` が過ぎる）まで、
 * 続けて送らない（`MapClickGate`）。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

#include <cstdint>

namespace hd2d {

//! 押している修飾キー（`map_click` の `mods`）。
struct MapClickMods {
    bool shift{ false };
    bool ctrl{ false };
    bool alt{ false };
};

//! `map_click` のボタン。数値は原作の `mouseclick.button` と同じ（ワイヤの `InputEventWire::button`）。
enum class MapClickButton { Left = 1, Right = 2 };

//! 認識できた 1 回のクリック。送るのは**ダンジョンのマス**で、画面の座標は送らない。
struct MapClick {
    MapClickButton button{ MapClickButton::Left };
    int gx{ 0 };
    int gy{ 0 };
    MapClickMods mods{};
    //! 押した所の画面の点（窓の座標）。コアが開く文脈メニューを、この点の近くへ出すために覚える。
    int sx{ 0 };
    int sy{ 0 };
};

/*!
 * @brief 押す・動く・離すから「クリック」を組む。マウスと指の両方を扱う。
 *
 * @details 時刻は呼び手が渡す（ミリ秒。`SDL_GetTicks()` と同じ単位）ので、SDL が無くても検査できる
 * （`tools/hd2d_verify/test_map_click_gesture.cpp`）。マスへ直すのは押した瞬間に呼び手が済ませて
 * `MapClick` として渡す——押してから離すまでにカメラが動いても、押した所のマスを送るため。
 */
class MapClickGesture {
public:
    //! 押してから離すまでに動いてよい幅（画素）。これを超えたらドラッグ。
    static constexpr int kSlopPx = 10;
    //! 指を動かさずにこれだけ押し続けたら長押し（右クリック）。
    static constexpr std::uint32_t kLongPressMs = 400;
    //! 数えられる指の数。これを超えたら 2 本以上と同じ扱い。
    static constexpr int kMaxFingers = 8;
    //! 離す出来事を取りこぼした指を、これだけ経ったら数えるのをやめる。
    static constexpr std::uint32_t kFingerStaleMs = 15000;

    enum class Source { Mouse, Touch };

    /*!
     * @brief 押した。
     * @param id 実マウスならボタン番号（1 = 左・2 = 右）、指なら指の番号。
     * @param target 押した所のマスと押した瞬間の修飾。`button` は実マウスなら押したボタン。
     *   指のときは無視して、離した（左）か長押し（右）で決める。
     * @param accept 偽なら押しは数えるだけでクリックの候補にしない（地図の外・メニューの最中など）。
     *   指は**受けなくても本数に数える**——2 本目が触れたときに 1 本目を捨てるため。
     * @return この押しをクリックの候補として追い始めたか。
     */
    bool press(Source source, std::int64_t id, int sx, int sy, const MapClick &target, std::uint32_t now_ms,
        bool accept = true);

    //! 動いた。`kSlopPx` を超えたらドラッグ扱い（この押しはクリックにならない）。
    void move(Source source, std::int64_t id, int sx, int sy);

    /*!
     * @brief 離した。
     * @param[out] out クリックとして送るもの（真を返したときだけ書く）。
     * @return クリックとして送るなら真。ドラッグ・長押しで送信済み・追っていない押しなら偽。
     * @details 指は離した時点の押し続けた長さで決める（`kLongPressMs` 以上なら右、未満なら左）。
     * 描画の 1 周より長押しの時計が遅れて `poll()` が間に合わなかったときも、結果が同じになる。
     */
    bool release(Source source, std::int64_t id, int sx, int sy, std::uint32_t now_ms, MapClick &out);

    /*!
     * @brief 時計を進める。**毎周 1 回呼ぶ**。長押しの時間が来た瞬間に右クリックを 1 回返す。
     * @details 返したらこの押しは終わり（指は文脈メニューなど別の持ち主のもの。指の数からも外す）。
     * 指はまだ押されているが、離す出来事が別の画面に食われても、次のタップを塞がない。
     * 離したときは何も送らない（`release()` が偽を返す）。
     */
    bool poll(std::uint32_t now_ms, MapClick &out);

    //! 追っている押しを捨てる（指の本数は覚えたまま。メニューが開いたなど、状況が変わったとき）。
    void abandon() { this->active_ = false; }

    //! 全部忘れる（窓が焦点を失ったとき。離す出来事が届かなくなる）。
    void reset()
    {
        this->active_ = false;
        for (FingerSlot &slot : this->fingers_) {
            slot = FingerSlot{};
        }
    }

    bool active() const { return this->active_; }
    //! 地図に触れている指の本数（`press()` が数えた分）。
    int fingers() const;

private:
    bool active_{ false };
    Source source_{ Source::Mouse };
    std::int64_t id_{ 0 };
    int start_x_{ 0 };
    int start_y_{ 0 };
    std::uint32_t start_ms_{ 0 };
    bool moved_{ false };
    MapClick target_{};

    /*!
     * @brief 地図に触れている指。番号と触れた時刻で持つ。
     * @details 本数だけ数えると、離す出来事を取りこぼした指（別の画面へ移った・窓の外へ出た）が
     * 残り続け、以後のタップが全部「2 本目」になって効かなくなる。番号で数えれば同じ番号の
     * 押し直しで直り、`kFingerStaleMs` を過ぎた古い指は捨てる。
     */
    struct FingerSlot {
        bool used{ false };
        std::int64_t id{ 0 };
        std::uint32_t since_ms{ 0 };
    };
    FingerSlot fingers_[kMaxFingers]{};

    bool beyond_slop(int sx, int sy) const;
    void add_finger(std::int64_t id, std::uint32_t now_ms);
    void remove_finger(std::int64_t id);
};

/*!
 * @brief 送りすぎの防止。送ったあと、次のフレームが届くまで続けて送らない。
 *
 * @details コアは返事を返さず、効果は次のフレームに出る（AH-57）。古いフレームを見て 2 回目を送ると、
 * 歩き始めた歩行を止める入力になってしまう。ただし何も起きないクリック（壁・レベルの外）では
 * フレームが変わらないことがあるので、`kReplyTimeoutMs` を過ぎたら次を受ける。
 */
class MapClickGate {
public:
    static constexpr std::uint32_t kReplyTimeoutMs = 300;

    bool ready(std::uint64_t frame_id, std::uint32_t now_ms) const
    {
        return !this->waiting_ || (frame_id != this->sent_frame_id_) || ((now_ms - this->sent_ms_) >= kReplyTimeoutMs);
    }

    //! 送った。`frame_id` は送った時点で持っていた最新のフレームの番号。
    void sent(std::uint64_t frame_id, std::uint32_t now_ms)
    {
        this->waiting_ = true;
        this->sent_frame_id_ = frame_id;
        this->sent_ms_ = now_ms;
    }

    void reset() { this->waiting_ = false; }

private:
    bool waiting_{ false };
    std::uint64_t sent_frame_id_{ 0 };
    std::uint32_t sent_ms_{ 0 };
};

//! `InputState`（フレームをまたぐ入力の状態）に持たせる、地図のクリックの状態。
struct MapClickState {
    MapClickGesture gesture;
    MapClickGate gate;
    /*!
     * 最後に送った右クリック（修飾なし。長押しを含む）の画面の点。コアが開く文脈メニューは、
     * この点の近くへ小さな窓で出す（`ModernStoreUi::set_context_anchor()`。窓を出すかどうかは
     * コアの `menu_over_map` が決める）。新しい右クリックのたびに書き換わる。
     */
    bool anchor_valid{ false };
    int anchor_x{ 0 };
    int anchor_y{ 0 };
};

/*!
 * @brief 最新のフレームが、地図のクリックを受けられる姿でない理由。受けられるなら `nullptr`。
 * @details メニュー（`menu_open`）・問い・`-more-`（`prompt`）・数量・文字入力・目標選びの最中、
 * タイトルや誕生の画面、地図が無い画面では送らない（送っても捨てられるだけだが、ログを汚す）。
 * `awaiting_command` は見ない——走り・休憩・歩行の最中の 2 回目のクリックは止める入力として送る。
 * 返すのは短い英語の理由（検査用のログに出す）。
 */
const char *map_click_frame_block_reason(const GameFrame &frame);

/*!
 * @brief 送ってよいマスか。ワイヤの座標の範囲（0〜255）と、フロアの大きさの中。
 * @details ミニマップが届いていればその幅・高さの外は送らない（コアは捨てるが、無駄な往復をしない）。
 */
bool map_click_cell_valid(const GameFrame &frame, int gx, int gy);

//! `input_event` に `map_click` を 1 つ積む。
void push_map_click(presentation::InputEventsMessage &out, const MapClick &click);

} // namespace hd2d
