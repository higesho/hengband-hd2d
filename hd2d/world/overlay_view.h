/*!
 * @file overlay_view.h
 * @brief 一過性の重ね書き（SQ-2）を**字のビルボード**にする。
 *
 * ## 何が来るのか
 * 旧世代のコア（Sil-Q）は、ダメージの数字・矢や投擲の飛跡・跳躍の軌跡・弾道と爆風・
 * 聞き耳の `*`・照準カーソルを、**`print_rel()` で主 Term の地図区画へ直に書いて**
 * 少し待ってから消す。この層は `map_info()` を通らないので、マスの絵を `map_info()` から
 * 作っている `cells` には**1 つも入らない**。
 *
 * コア側のアダプタが「Term の地図区画」と「`cells` の見た目」を突き合わせ、
 * 食い違ったマスだけを `GameFrame::map_overlay` へ積む。ここはそれを受けて字を立てる。
 *
 * ## なぜ実体（`entity_view`）と分けるのか
 * - **実体の表現に関わらず出す。**板で描いていても重ね書きは字である（数字や `*` に
 *   板は無い）。`EntityView::glyphs` は板と**排他**なので、そこへ混ぜると板のときに消える。
 * - **なめらか移動をしない。**重ね書きはマスに焼き付いた 1 フレームの絵で、追う個体が無い。
 *   `EntityTracker` の追随に乗せると、消えたり現れたりするたびに前の位置から流れてくる。
 *
 * ## 出ないコアでは 1 バイトも動かない
 * `map_overlay` が空なら `build_overlay_glyphs()` は即座に空を返す。
 */
#pragma once

#include "render/billboard_renderer.h"
#include "render/glyph_atlas.h"

#include <vector>

struct GameFrame;

namespace hd2d {

/*!
 * @brief `frame.map_overlay` を字のビルボードにする。
 * @param atlas 作った字の目録。**無い字は捏造せず飛ばす**（実体の字と同じ作法）
 * @param height_cells 字の背丈（マス）。実体の字と揃えると読みやすい
 * @param out 積む先（この関数が丸ごと入れ替える）
 * @return 積んだ枚数
 *
 * @details 実体より**高く**置く（`kOverlayLift`）。重ね書きは「そのマスで起きたこと」で、
 * 足元に置くと立っている敵の板に隠れる——**ダメージの数字が敵の背中に埋まる**。
 */
int build_overlay_glyphs(const GameFrame &frame, GlyphAtlas &atlas, float height_cells,
    std::vector<BillboardInstance> &out);

} // namespace hd2d
