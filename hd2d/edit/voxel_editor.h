/*!
 * @file voxel_editor.h
 * @brief ボクセルエディタ（P9・R13）— AI が作ったものを**人が手直しし、追加できる**画面。
 *
 * 基準はボクセル HD2D の設計 §7・R13、段取りは P9。
 *
 * ## 立ち位置（2026-08-08 に決めた）
 * **本編（`HengbandHd2d.exe`）に内蔵する。ただしリリース用ビルドには含めない。**
 * `hd2d/edit/` の TU は vcxproj の `HengbandHd2dEditor` プロパティ（既定 1）で条件付きになっており、
 * `/p:HengbandHd2dEditor=0` で組むと `HD2D_EDITOR` ごと消える。内蔵にしたのは
 * P9 ⑤「その場でプレビューは**本編と同じメッシャ・同じシェーダ**を使うこと。
 * 別実装にすると乖離する」を**同じバイナリ**という構造で保証するためである。
 *
 * ## 起動
 * ```
 * .\HengbandHd2d.exe --edit=shop          # 既存を開く（無ければ断る）
 * .\HengbandHd2d.exe --edit-new=lantern   # 新しく作る（既にあれば断る）
 * ```
 * コアは起こさない。`--voxel-dir=` / `--windowed=` / `--shot=` も効く。
 *
 * ## できること（P9 の ①〜⑤）
 * | | 手段 |
 * |---|---|
 * | ① 読み書き | 起動で読み、Ctrl+S で保存（保存 = 書く → 読み戻す → 照合まで） |
 * | ② ボクセル | B 置く / X 消す / C 塗る / I スポイト（右クリックは常に消す）。パレットは画面下で選択・スライダで編集 |
 * | ③ パーツ | V 2 回で箱を選択 → メニューで分割。ピボットは P（ホバー位置）。動きの種類と係数はメニュー（Tab） |
 * | ④ footprint | 宣言（いまのファイル）と実体（いまの形）を地面の枠で常時表示。食い違いは色で出る。保存時に実体へ作り直す |
 * | ⑤ プレビュー | 空白キーで動きが本編と同じ式で動く（`part_motion` ＋ 風の頂点シェーダ） |
 */
#pragma once

#include "app/hd2d_app.h"

struct SDL_Window;

namespace hd2d {

/*!
 * @brief エディタを回す。窓と GL は呼び出し側（`hd2d_app.cpp`）が作って渡す。
 * @details 窓の作り方（GL 4.6 core の要求・デバッグ出力）を 2 か所に書かないため。
 * @return プロセスの終了コード。
 */
int run_voxel_editor(const AppOptions &options, SDL_Window *window);

} // namespace hd2d
