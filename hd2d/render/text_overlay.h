/*!
 * @file text_overlay.h
 * @brief 文字描画（GL 4.6・遅延焼きのアトラス 1 枚・1 回の draw call）。
 *
 * ## 何のためのものか
 * P0 ⑤「受信したフレームの内容を**テキストで**画面に出す
 * （描画の検証ではなく接続の検証）」の受け皿。P1 以降も、三角形数・フレーム時間・
 * ヒットテストの往復検査といった**数字を実機で見る**ための場所として残る。
 *
 * ## 作り
 * - フォントは **MS ゴシック**を第一候補にする。半角＝1 マス・全角＝2 マスで刻まれるので、
 *   コアの Term（文字グリッド）をそのまま画面へ写せる
 * - グリフは**使われたときに 1 個ずつ**焼いてアトラス（`GL_R8`）へ載せる。
 *   日本語は種類が多く、全部を先に焼くのは無駄でもある
 * - 1 フレームぶんを頂点で積み、最後に 1 回だけ流す
 *
 * @note アトラスが埋まったら**新しいグリフは描かれなくなる**（落ちはしない）。
 * 起きたら警告を 1 度だけ出す。1024×1024・16px なら数千字入るので P0〜P8 では届かない見込み。
 */
#pragma once

#include "render/gl_core.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <algorithm>

//! `SDL_ttf.h` をこのヘッダへ持ち込まないための前方宣言（実体は `struct TTF_Font`）。
struct TTF_Font;

namespace hd2d {

//! 文字色（0〜1）。
struct TextColor {
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    float a{ 1.f };
};

class TextOverlay {
public:
    /*!
     * @brief フォントを開き、空のアトラスとシェーダを用意する。**GL コンテキストの後に呼ぶこと。**
     * @param px 字の高さ（ピクセル）。
     * @param[out] err 失敗の理由。
     */
    bool init(int px, std::string &err);
    /*!
     * @brief 指定したフォントのファイルだけを開いて初期化する（DCSS の画面の部品が BIZ UDゴシックを使うための入口）。
     * @details **開けなければ失敗を返す**（候補の表へは落ちない。呼ぶ側が次の候補を試す）。
     * `init(px, err)` の動きは変えない。`ttc_index` は .ttc の中の何番目の書体か（0 = 先頭）。
     */
    bool init_with_font(int px, const std::string &font_path, int ttc_index, std::string &err);
    void shutdown();

    /*!
     * @brief 全角の字の送り幅を `px` 画素に揃える（0 で既定＝字の持つ送り幅）。
     * @details DCSS の画面の部品が使う（原作の字は全角を半角のちょうど 2 倍に揃える加工が入っている。。
     * 15 画素の BIZ UDゴシックは半角 7.5 → 8、全角 15 だが、原作は 16 で並べる）。呼ばなければ何も変わらない。
     * 半角の 1.5 倍より広い送り幅の字を全角と見なす。
     */
    void set_wide_advance(int px) { wide_advance_ = px; }

    //! 半角 1 文字ぶんのマス目（全角はこの 2 倍）。
    int cell_w() const { return scaled_font_?std::max(1,int(scaled_font_->narrow_cell_w()/layout_scale_+.5f)):cell_w_; }
    int cell_h() const { return scaled_font_?std::max(1,int(scaled_font_->cell_h_/layout_scale_+.5f)):cell_h_; }
    void set_layout_scale(float scale);
    float layout_scale() const {return layout_scale_;}
    int raster_font_px() const{return scaled_font_?scaled_font_->font_px_:font_px_;}

    //! このフレームの積み上げを空にする。画面の実寸を渡す（正射影の分母になる）。
    void begin(int screen_w, int screen_h);
    /*!
     * @brief 画素座標（左上原点）に 1 行積む。戻り値は進んだ幅（画素）。
     * @param max_width これを超える字は**積まない**（0 なら無制限）。P8 のパネルで使う。
     * @details 切るのを描画側に持たせているのは、**字送りを知っているのがここだけ**だから
     * である（全角は半角の 2 倍で、バイト数からは幅が出ない）。呼び出し側で
     * 「何文字で切るか」を決めると必ず溢れるか余る。
     */
    int draw(int x, int y, std::string_view utf8, const TextColor &color = TextColor{}, int max_width = 0);
    //! 桁・行で積む（`cell_w`/`cell_h` 刻み）。
    int draw_cell(int origin_x, int origin_y, int col, int row, std::string_view utf8, const TextColor &color = TextColor{});
    //! 描かずに幅（画素）だけ測る。
    int measure(std::string_view utf8);
    /*!
     * @name 字の大きさだけを変えて描く・測る（座標と幅は画素のまま）
     * @details `set_layout_scale()` は**座標系ごと**拡大する（メニュー用。1 未満は使えない）。
     * こちらは座標系を変えず、**字だけ**を `scale` 倍（0.3〜3）にする。名札の字を大きくする・
     * 名前をアイコンの幅へ縮める、といった部分的な調整のためのもの。
     * 字の大きさは整数の画素（`字の高さ x scale` を丸めた値）で決まり、同じ画素の字は
     * 1 つのフォントを使い回す。**`layout_scale` が 1 のときだけ**効く（それ以外は
     * 普通の大きさで描く）。縮めた字・拡大した字は、普通の字より**後に**流れる。
     * @{
     */
    int draw_sized(int x, int y, std::string_view utf8, const TextColor &color, float scale, int max_width = 0);
    /*!
     * @brief `draw_sized` の、横位置が小数・横方向を `[clip_x0, clip_x1]` で切る版。
     * @details 流れるアイコンの名前に使う（等速で流すので、画素の端数も出す。端で出入りする字は
     * 字の途中で切る）。切るのは字の板（四角）で、`u` 座標も同じ割合で詰める。
     * `layout_scale` が 1 でないとき（メニューの倍率の中）は、切らずに整数の位置で描く。
     * `x_scale` は**横だけ**の倍率（既定 1 = 縮めない）。字の高さは `scale` のまま、字の幅・送りだけを `x_scale` 倍にする
     * （状態アイコンの名前が、字の高さを保ったままアイコンの幅に収まらないときだけ使う。`hd2d/ui/orb_hud.cpp`）。
     * `layout_scale` が 1 でないときは効かない（普通の大きさで描く）。
     */
    int draw_sized_clipped(float x, int y, std::string_view utf8, const TextColor &color, float scale, int max_width,
        float clip_x0, float clip_x1, float x_scale = 1.f);
    int measure_sized(std::string_view utf8, float scale);
    int cell_h_sized(float scale);
    //! `scale` が実際に使う字の高さ（画素）。縮める限界を画素で比べたいときに使う。
    int sized_px(float scale) const;
    //! 基準の字の高さ（`layout_scale` が 1 のときの `init()` の値）。
    int base_px() const { return font_px_; }
    /*! @} */
    /*!
     * @brief `max_width` に収まる**先頭のバイト数**を返す（折り返しの切れ目）。
     * @return 収まるバイト数。0 なら 1 文字も入らない（呼ぶ側は輪を止めること）。
     * @details `draw()` の切り方と**同じ規則**（字の右端で切る）である。折り返しを
     * 呼ぶ側に持たせるために切れ目だけを返す手段が要る——桁数で切ると全角で必ず狂う
     * （字送りを知っているのはここだけ、というヘッダ冒頭の話がそのまま当てはまる）。
     */
    std::size_t fit_bytes(std::string_view utf8, int max_width);
    //! 積んだものを 1 回で流す。
    void flush();

private:
    //! 問い合わせの記録（`app/ui_probe.h`）を伴わない本体。`draw()` がこれを呼んで、描いた分だけを記録する。
    int draw_raw(int x, int y, std::string_view utf8, const TextColor &color, int max_width);
    //! `draw_raw()` が実際に描いたバイト数・幅（画素）。切れたときは `utf8.size()` より小さい。
    std::size_t last_consumed_{ 0 };
    int last_width_{ 0 };
    //! 問い合わせ用：直前の描画で数えた、字形の無い字（四角で出る）・置き換え文字の数と、幅の上限で切れたか。
    int last_missing_{ 0 };
    int last_replacement_{ 0 };
    bool last_clipped_{ false };
    int draw_clipped_impl(float x, int y, std::string_view utf8, const TextColor &color, int max_width, float clip_x0,
        float clip_x1, float x_scale);
    void flush_base();
    //! `draw_sized` 系が使うフォント。普通の大きさ（または `layout_scale` が 1 でない）なら自分自身。
    TextOverlay *sized_font(float scale);
    float layout_scale_{1.f};
    int wide_advance_{0}; //!< `set_wide_advance`。0 = 使わない
    int narrow_advance_{0}; //!< 半角の送り幅の指定（0 = 字の持つ送り幅）。`set_layout_scale` が作る拡大した字にだけ入る（下の説明）
    int font_px_{16};
    TextOverlay *scaled_font_{nullptr};
    std::unordered_map<int,std::unique_ptr<TextOverlay>> scaled_fonts_;
    struct Glyph {
        float u0{};
        float v0{};
        float u1{};
        float v1{};
        int w{};
        int h{};
        bool drawable{ false }; //!< 空白・焼けなかった字は false（マスだけ進める）
        bool missing{ false }; //!< フォントに字形が無い（四角で出る／焼けなかった）。空白・制御文字は偽
    };

    struct Vertex {
        float x{};
        float y{};
        float u{};
        float v{};
        float r{};
        float g{};
        float b{};
        float a{};
    };

    //! アトラスの一辺。R8 なので 1024×1024 で 1MB。
    int atlas_side_{1024}; // Larger fonts retain enough room for Japanese glyphs.

    const Glyph *glyph_for(std::uint32_t codepoint);
    //! 字の送り幅（`set_wide_advance` があれば全角はそれに揃える）。
    float advance_of(const Glyph &glyph) const
    {
        if ((wide_advance_ > 0) && (glyph.w * 2 > cell_w_ * 3)) {
            return static_cast<float>(wide_advance_);
        }
        return (narrow_advance_ > 0) ? static_cast<float>(narrow_advance_) : static_cast<float>(glyph.w);
    }
    //! 半角の桁の幅（`narrow_advance_` があればそれ）。
    int narrow_cell_w() const { return (narrow_advance_ > 0) ? narrow_advance_ : cell_w_; }
    //! UTF-8 を 1 符号位置ずつ取り出す。壊れたバイトは U+FFFD 扱いで 1 バイト進める。
    static std::uint32_t next_codepoint(std::string_view text, std::size_t &pos);

    ::TTF_Font *font_{ nullptr };
    //! 空でなければ `init()` の候補の表の代わりにこのファイルだけを開く（`init_with_font`）。
    std::string forced_font_path_;
    int forced_font_index_{ 0 };
    gl::GLuint program_{ 0 };
    gl::GLuint vao_{ 0 };
    gl::GLuint vbo_{ 0 };
    gl::GLuint atlas_{ 0 };
    gl::GLint loc_screen_{ -1 };
    gl::GLint loc_atlas_{ -1 };
    int cell_w_{ 0 };
    int cell_h_{ 0 };
    int screen_w_{ 0 };
    int screen_h_{ 0 };
    //! 棚詰め（shelf packing）の現在位置。
    int pen_x_{ 0 };
    int pen_y_{ 0 };
    int shelf_h_{ 0 };
    bool atlas_full_warned_{ false };
    std::size_t vbo_capacity_{ 0 };
    std::unordered_map<std::uint32_t, Glyph> glyphs_;
    std::vector<Vertex> vertices_;
};

} // namespace hd2d
