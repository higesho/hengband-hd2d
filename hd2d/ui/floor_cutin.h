/*!
 * @file floor_cutin.h
 * @brief 階が変わったときのカットイン演出（P10。2026-08-11 に決めた）。
 *
 * ```
 * ⓪ 階段を踏んだ瞬間に黒幕を張る（行先の地図が来るまで）
 * ① 黒画面 → 地名が左から・階層が右からカットインしてくる
 *      イークの洞窟→→→→                    ←←←←2 層
 * ② 中央で止まり、**決定キーを待つ**
 * ③ 決定キーで左右へ抜けていく
 * ④ マップが浮かび上がる
 * ```
 *
 * ## ⓪ が要る理由（2026-08-28 に気づいた。**全コア共通**）
 * コアは階段を踏んだ後、**「階だけ新しく・地図は元の階のまま」の 1 枚**を送ってくる
 * （出どころと実測は `floor_cutin.cpp` の表）。**この 1 枚は `-more-` の待ちなので、
 * プレイヤがキーを押すまで画面に残り続ける**——つまり**元の階の絵を見せたまま止まる**。
 * 演出をここで始めてはいけない（幕が上がったとき元の階が出る）が、
 * **幕だけは先に張る**。`Pending` はそのための段である。
 *
 * `Pending` の間は **`blocks_input()` を立てない**。`-more-` を越えるキーは
 * コアへ届かねばならず、止めると先へ進めなくなる。
 *
 * ## 出す場所と出さない場所
 * **地上マップとダンジョンだけ**で、**全体マップ（広域マップ）には出さない**（決めたこと）。
 * 広域マップは `FloorIdentity::kind` では見分けられない（あれも `Surface` で `town_id` は
 * 荒野と同じ 0）ので、コアの `AngbandWorld::is_wild_mode()` を
 * `FloorIdentity::wild_mode` として運んでもらっている。
 *
 * ## 「文字を直に描き込まない」
 * 決めたことは「**将来は地名と階層を画像に差し替える。**いまは文字でよい（＝差し替え
 * られる作りにしておくこと）」である。そこで出す単位を**板（`CutinPlate`）**にしてある:
 *
 * | | 板 1（地名） | 板 2（階層） |
 * |---|---|---|
 * | 絵があるとき | `assets/ui/cutin/<鍵>.png` を貼る | 同左 |
 * | 絵が無いとき | 文字を描く | 同左 |
 *
 * 鍵は**数字**で作る（`place_d5` / `place_t1` / `level_37`）。地名そのものをファイル名に
 * すると、日本語と英語で別の名前になり、上流が名前を変えるたびに絵が迷子になる。
 * 絵を置くだけで差し替わり、**コードのビルドは要らない**（対応表と同じ流儀＝§9.4）。
 *
 * ## 決定待ちの間はコアへ入力を流さない
 * 誤コマンド禁止（機能メニューを開いている間と同じ扱い）。`blocks_input()` が真の間、
 * 呼び出し側は `input_event` を送らず、クリック移動も止める。
 *
 * ## 自前の `UiPaint` と `TextOverlay` を持つ理由
 * 1. **いちばん上に描かねばならない。**本体の `paint`→`text` の積みに混ぜると、
 *    黒幕（四角）が HUD の文字より下に流れて、幕の上に状態列が浮く
 * 2. **字が大きい。**高さは「マップ縦幅の 1/6」（決めたこと）で、本体の 16px とは桁が違う。
 *    `TextOverlay` は開いたときの px で焼くので、別の器が要る
 */
#pragma once

#include "frame/game_frame.h"
#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "render/text_overlay.h"

#include <cstdint>
#include <string>

namespace hd2d {

//! カットインの段。
enum class CutinPhase {
    Idle, //!< 何もしていない
    Pending, //!< **階を離れた。まだ行先の地図が来ていない**——幕だけ張って待つ
    In, //!< 左右から入ってくる
    Wait, //!< 中央で止まり、決定キーを待つ
    Out, //!< 左右へ抜けていく
    Reveal, //!< 幕が薄れてマップが浮かび上がる
};

/*!
 * @brief 出す板 1 枚。**絵があれば絵、無ければ文字。**
 * @details 「文字を直に描き込まない」（2026-08-11 に決めた）の実体。
 */
struct CutinPlate {
    std::string text; //!< 絵が無いときに描く字（UTF-8）
    std::string art_key; //!< `assets/ui/cutin/<art_key>.png`
    UiImage image; //!< 読めたときだけ `valid()`

    bool empty() const { return this->text.empty() && !this->image.valid(); }
};

class FloorCutin {
public:
    //! GL コンテキストの後に呼ぶ。**失敗しても致命ではない**（演出が出ないだけ）。
    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 1 フレーム進める。**階が変わっていたらここで始まる。**
     * @param frame いまのフレーム。
     * @param dt 秒。
     * @param layout 画面の割り付け（字の大きさをマップの縦幅から採る）。
     * @param map_shown 画面に地図（3D）が出ているか。**タイトルや店の画面では始めない。**
     */
    void update(const GameFrame &frame, float dt, const UiLayout &layout, bool map_shown);

    //! 幕を張っているか（`Pending` を含む）。**バーチャルパッドを上へ描き直す条件**。
    bool active() const { return this->phase_ != CutinPhase::Idle; }
    /*!
     * @brief **行先の地図待ち**（⓪）か。
     * @details 幕は出ているが演出はまだ始まっていない。ここでは入力を止めない。
     */
    bool pending() const { return this->phase_ == CutinPhase::Pending; }
    //! 決定キーを待っているか。
    bool waiting() const { return this->phase_ == CutinPhase::Wait; }
    /*!
     * @brief コアへ入力を流してはいけないか。
     * @details 抜けていく間（`Out`）も止める。幕がまだ地図を覆っているので、
     * ここで通したキーは「見えていない画面への入力」になる。
     */
    bool blocks_input() const
    {
        return (this->phase_ == CutinPhase::In) || (this->phase_ == CutinPhase::Wait)
            || (this->phase_ == CutinPhase::Out);
    }
    /*!
     * @brief 決定を受ける。
     * @return 受けたか（偽なら呼び出し側はそのキーを従来どおり扱ってよい）。
     * @details `In` の途中で押されたら**そこから抜けへ飛ぶ**（待たされている感じにしない）。
     */
    bool confirm();

    //! **いちばん上へ**描く。本体の `paint` / `text` を流した後に呼ぶこと。
    void draw(const UiLayout &layout, int screen_w, int screen_h, const UiImagePainter &images);

private:
    void begin(const GameFrame &frame, const UiLayout &layout);
    void load_plate(CutinPlate &plate, const std::string &text, const std::string &art_key);
    //! マップの縦幅から字の大きさを決め、変わっていたら焼き直す。
    void ensure_font(const UiLayout &layout);

    CutinPhase phase_{ CutinPhase::Idle };
    float elapsed_{ 0.f };
    bool has_last_floor_{ false };
    FloorIdentity last_floor_{};
    /*!
     * @brief 覚えている階の**地図そのもの**の印（`floor_cutin.cpp` の `map_signature()`）。
     * @details 「行先へ着いたか」を `generated_turn` だけで見てはいけない
     * ——**4 コアのうち 3 本は `generated_turn` を 0 のまま送る**（`fc_frame.cpp` /
     * `sq_frame.cpp` / `gb_frame.cpp` の同名の関数）。地図が入れ替わったかで見る。
     */
    std::uint64_t last_map_sig_{ 0 };
    //! `Pending` が続いた秒数（保険の頭打ち。`floor_cutin.cpp` の `kPendingMaxSeconds`）。
    float pending_seconds_{ 0.f };

    CutinPlate place_; //!< 地名
    CutinPlate level_; //!< 階層（地上は空）

    UiPaint paint_;
    TextOverlay text_;
    int font_px_{ 0 };
    bool ready_{ false };
};

} // namespace hd2d
