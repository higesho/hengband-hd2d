/*!
 * @file native_core_ui.h
 * @brief 原作の画面を持つコア（NetHack など）の、画面の側の専用の部品の基底クラス。
 *
 * ## 何のためのものか
 * T1（案 C）。握手でコアが専用の画面を持つと申告したとき（NetHack は `nethack-ui`）だけ、
 * `hd2d_app.cpp` がこのクラスの派生を作り、(1) 受信した知らせを渡す、(2) 配置の結果（立体の地図の矩形・下の帯）を差し替える、
 * (3) HD2D の部品の代わりに区画を描く、(4) 窓の入力を受ける、の 4 つだけを呼ぶ。申告の無いコアは `native_ui` が空のままで、
 * ここを 1 行も通らない。
 *
 * DCSS の `DcssUi`（`hd2d/dcss/dcss_ui.h`）は、このクラスの派生ではない（DCSS の経路を書き換えないため。設計書 T1）。
 * NetHack が動いたあと、別のコミットで `DcssUi` を同じ基底の実装にして分岐の重複を消す。そのときのために、
 * 口は `DcssUi` が `hd2d_app.cpp` から呼ばれている物に揃えてある（名前と引数の意味が同じ）。
 *
 * ## 派生が守る約束
 * - `prepare` は毎フレーム呼ばれてよい（配置の入力が変わらなければ何もしない）。
 * - `scene_rect` は `layout_ok()` かつ `in_game()` のときだけ使われる。空の矩形を返してはならない。
 * - コアへ返す物は `take_outgoing` に積む（`type` は `payload` の `t` と同じ字。`CoreLink::send_dcss_message` が運ぶ）。
 */
#pragma once

#include "ui/ui_image.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

//! コアへ送る 1 通。`type` は `payload`（JSON 1 通）の `t` と同じ字。
struct NativeUiOutgoing {
    std::string type;
    std::string payload;
};

//! パッド（実物・バーチャルパッド）の操作。窓が入力を取っている間だけ、画面が `NativeCoreUi::on_pad` へ渡す。
enum class NativePad { Up, Down, Left, Right, A, B };

class NativeCoreUi {
public:
    virtual ~NativeCoreUi() = default;
    NativeCoreUi() = default;
    NativeCoreUi(const NativeCoreUi &) = delete;
    NativeCoreUi &operator=(const NativeCoreUi &) = delete;

    //! 握手で申告した名前（`nethack-ui`）。記録に出す。
    virtual const char *feature_name() const = 0;
    //! 表示の言語（`ja` / `en`）。
    virtual void set_language(const std::string &lang) = 0;

    //! コアから届いた 1 通（`{"t":"nethack_ui",…}`）を取り込む。届いた順に全部渡される。
    virtual void ingest_payload(const std::string &payload) = 0;

    /*!
     * @brief 窓の大きさから配置を決める（毎フレーム呼んでよい。変わらなければ何もしない）。
     * @param origin_x,origin_y 使える範囲の左上（窓の座標）
     * @param usable_w,usable_h 使える範囲の大きさ
     * @param reserved_bottom 下の案内の帯の高さ。配置は残りの高さで計算する
     * @details 字の大きさが変わるときは字を開き直す（GL が要る）。失敗したら `err` に理由を入れて偽。
     */
    virtual bool prepare(int origin_x, int origin_y, int usable_w, int usable_h, int reserved_bottom, std::string &err) = 0;
    virtual bool layout_ok() const = 0;
    //! 配置を組み直すたびに増える（記録を出すかの判定に使う）。
    virtual std::uint64_t layout_serial() const = 0;
    //! ゲームに入っているか。入っていない間は区画を描かず、HD2D の配置のまま（タイトル・キャラクター作成は従来どおり）。
    virtual bool in_game() const = 0;
    //! 立体の地図を描く矩形（窓の座標）。
    virtual RectPx scene_rect() const = 0;
    //! 配置の 1 行の説明（記録用）。
    virtual std::string describe() const = 0;

    //! 区画（メッセージ・ステータスほか）を描く。字は派生が自分の字で積む（`flush_text` で流す）。
    virtual void draw(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h) = 0;
    //! 自分の字を流す（`paint.flush()` の後に呼ぶ）。
    virtual void flush_text() = 0;
    //! 窓（メニュー・問いほか）を描く。HD2D の部品より上に重ねる。
    virtual void draw_windows(UiPaint &paint, const UiImagePainter &images, int screen_w, int screen_h) = 0;

    //! 窓が開いていて、キーを窓が受けるか。
    virtual bool windows_capture_keys() const = 0;
    //! SDL の出来事 1 つを窓へ見せる。食ったら真。`reserved_key` は HD2D が自分で使うキー（機能メニュー・Alt+Enter）。
    virtual bool on_window_event(const SDL_Event &event, bool reserved_key) = 0;
    //! パッドの操作（窓が入力を取っている間だけ呼ばれる。`windows_capture_keys()`）。食ったら真。既定は何もしない。
    virtual bool on_pad(NativePad) { return false; }
    //! 区画（メッセージ・ステータス・持ち物の窓）へのマウス。食ったら真。
    virtual bool on_region_mouse(const SDL_Event &) { return false; }
    /*!
     * @brief 地図のマウス（動き・押下）。食ったら真。
     * @param on_map 立体の地図のマスの上か
     * @param cell_x,cell_y そのマスの地図の座標（フレームの `cells` の `gx`・`gy`。NetHack は原作の `x`・`y` と同じ）
     */
    virtual bool on_map_event(const SDL_Event &, bool /*on_map*/, int /*cell_x*/, int /*cell_y*/) { return false; }
    //! 地図の上に印を付けるマス（位置の選択のカーソルなど）。無ければ偽。
    virtual bool map_cursor_cell(int & /*cell_x*/, int & /*cell_y*/) const { return false; }
    /*!
     * @brief `map_cursor_cell` のマスの印を描く。
     * @param cell 窓の座標でのそのマスの矩形（呼ぶ側が立体のカメラで投影して求める）
     */
    virtual void draw_map_cursor(UiPaint & /*paint*/, const RectPx & /*cell*/) {}
    //! コアへ送る物を持っていく。
    virtual std::vector<NativeUiOutgoing> take_outgoing() = 0;

    //! 下の案内の帯の右側に足す、このコア専用の案内（空なら足さない）。
    virtual std::string key_hint(bool /*ja*/) const { return {}; }
    //! ソフトキーボードを出したいか（文字入力の窓が出ている間）。
    virtual bool wants_soft_keyboard() const { return false; }

    virtual void shutdown() = 0;
};

} // namespace hd2d
