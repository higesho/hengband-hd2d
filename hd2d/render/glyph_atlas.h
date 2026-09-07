/*!
 * @file glyph_atlas.h
 * @brief **3D 空間へ立てる文字**のアトラス（`GL_RGBA8`・遅延焼き・棚詰め）。
 *
 * 基準は （アスキー実体）。
 *
 * ## 何のためのものか
 * こう決めた——「アスキーモードの HD2D（地面、地形は HD2D のボクセル。きゃら、モンスター、
 * アイテムはアスキーを板にしたもの）」。地形はボクセルのまま、**実体だけを文字の板**にする。
 * 見下ろしでも一人称でも同じ板を使う（軸拘束ビルボードは常にカメラの方位へ正対するので、
 * 見せ方ごとの向き管理が要らない）。
 *
 * ## `render/text_overlay.h` と何が違うか（**流用しない理由**）
 * | | `TextOverlay` | ここ |
 * |---|---|---|
 * | 何に貼るか | 画面（画素座標・正射影） | **3D の板**（世界座標・透視） |
 * | 形式 | `GL_R8`（覆いだけ。色は頂点） | **`GL_RGBA8`**（白い字＋暗い縁を焼き込む） |
 * | 大きさ | UI の字（16px 級） | **64px 級**（1 マスが画面で 100px 前後になるので粗が出る） |
 * | 縁取り | 無し | **有り**（下の注記） |
 *
 * 棚詰めと遅延焼きの流儀は `TextOverlay` と同じものを踏んでいる（真似ているだけで、
 * コードは共有していない——片方が画素座標に縛られているため）。
 *
 * ## 色は焼かない
 * コアの色は 16 色（`TermPalette`）あり、印字 ASCII は 95 種類ある。色ごとに焼くと
 * 1520 通りになってアトラスが溢れる。**白い字を 1 通りだけ焼き、色は
 * `BillboardInstance::r/g/b` で掛ける**（ビルボードのシェーダが `texel.rgb * v_tint`）。
 *
 * ## 暗い縁を焼き込む（設計書 §14-2）
 * 明るい床のボクセルの上に白い字を置くと読めない。**1 画素の暗い縁**を焼き込んでおくと、
 * どの地形の上でも輪郭が立つ。縁は作った時点で暗いので、上から色を掛けても暗いまま残る
 * （白い本体だけが色に染まる）。外したくなったら `kOutline` を 0 にすれば消える。
 *
 * @note アトラスが埋まったら**新しい字は描かれなくなる**（落ちはしない）。
 * 起きたら警告を 1 度だけ出す——`text_overlay.h` と同じ作法である。
 * 印字 ASCII 95 字なら 1024×1024 に十分入るが、将来全角を足したときに黙って消えると困る。
 */
#pragma once

#include "render/gl_core.h"

#include <cstdint>
#include <string>
#include <unordered_map>

//! `SDL_ttf.h` をこのヘッダへ持ち込まないための前方宣言（実体は `struct TTF_Font`）。
struct TTF_Font;

namespace hd2d {

class GlyphAtlas {
public:
    //! 1 字ぶんのアトラス上の場所。`u*`/`v*` は**テクセル**（`BillboardRenderer` がそう受ける）。
    struct Entry {
        float u0{ 0.f };
        float v0{ 0.f };
        float u1{ 0.f };
        float v1{ 0.f };
        int w{ 0 }; //!< 作った画素の幅（縁を含む）
        int h{ 0 };
        bool valid() const { return (this->w > 0) && (this->h > 0); }
    };

    /*!
     * @brief フォントを開き、空のアトラスを用意する。**GL コンテキストの後に呼ぶこと。**
     * @param px 字の高さ（画素）。既定は `kDefaultPx`。
     * @param[out] err 失敗の理由。
     */
    bool init(int px, std::string &err);
    void shutdown();
    bool ready() const { return this->atlas_ != 0; }

    /*!
     * @brief 印字 ASCII 1 文字の場所。まだ焼いていなければ焼く。
     * @return 焼けなければ `nullptr`（**捏造しない**——呼び手はその実体を描かない）。
     */
    const Entry *entry_for(char ch);

    gl::GLuint texture() const { return this->atlas_; }
    int side() const { return kAtlasSide; }

    /*!
     * @name **等間隔の文字シート**
     *
     * @details 上の棚詰めのアトラスとは**別のテクスチャ**である。理由は 2 つ。
     *
     * | | 棚詰めのアトラス | 文字シート |
     * |---|---|---|
     * | 引く側 | CPU（`entry_for` で矩形を貰う） | **シェーダ**（文字コードから場所を計算する） |
     * | 詰め方 | 字ごとに幅が違う棚詰め | **16×6 の等間隔の格子**（`(code-32)` から桁と行が出る） |
     * | 縁取り | 焼き込む（明るい床の上で読めるように） | **焼かない**（ネオンでは縁が「光らない画素」になって字が欠ける。§18.5-5） |
     *
     * シェーダ側に矩形の表を渡す道が無いので、**場所を計算で出せる形**でなければならない
     * ——それが等間隔の格子である。字は各マスの中央へ寄せて焼く。
     *
     * 中身は `GL_R8`（覆いだけ）。色はコアの 16 色をマスごとに渡すので、作る必要が無い。
     * @{
     */
    //! 文字シートを焼く。**`init()` の後に 1 度だけ。**2 度目は何もしない。
    bool bake_sheet(std::string &err);
    gl::GLuint sheet_texture() const { return this->sheet_; }
    //! 最初の文字コード（空白）。シェーダは `code - kSheetFirst` でマスを引く。
    static constexpr int kSheetFirst = 32;
    static constexpr int kSheetCols = 16;
    static constexpr int kSheetRows = 6; //!< 32..127 の 96 字 ＝ 16×6
    /*! @} */

    //! 既定の焼き付け高さ（画素）。1 マスが画面で 100px 前後なので、これで粗が見えない。
    static constexpr int kDefaultPx = 64;

private:
    static constexpr int kAtlasSide = 1024;
    //! 縁の太さ（画素）。0 で縁なし（設計書 §14-2 の「外すのは 1 行」）。
    static constexpr int kOutline = 2;

    TTF_Font *font_{ nullptr };
    gl::GLuint atlas_{ 0 };
    gl::GLuint sheet_{ 0 };
    int sheet_cell_{ 0 }; //!< マス 1 つの辺（画素）
    int pen_x_{ 0 };
    int pen_y_{ 0 };
    int shelf_h_{ 0 };
    bool warned_full_{ false };
    std::unordered_map<char, Entry> glyphs_;
};

} // namespace hd2d
