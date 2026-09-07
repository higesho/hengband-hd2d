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
    void shutdown();

    //! 半角 1 文字ぶんのマス目（全角はこの 2 倍）。
    int cell_w() const { return this->cell_w_; }
    int cell_h() const { return this->cell_h_; }

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
    struct Glyph {
        float u0{};
        float v0{};
        float u1{};
        float v1{};
        int w{};
        int h{};
        bool drawable{ false }; //!< 空白・焼けなかった字は false（マスだけ進める）
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
    static constexpr int kAtlasSide = 1024;

    const Glyph *glyph_for(std::uint32_t codepoint);
    //! UTF-8 を 1 符号位置ずつ取り出す。壊れたバイトは U+FFFD 扱いで 1 バイト進める。
    static std::uint32_t next_codepoint(std::string_view text, std::size_t &pos);

    ::TTF_Font *font_{ nullptr };
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
