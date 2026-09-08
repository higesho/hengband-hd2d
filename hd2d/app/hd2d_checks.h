/*!
 * @file hd2d_checks.h
 * @brief 検査モード（`--*-check`）の入口。
 *
 * @details `run()` は頭で `AppOptions` を見て、検査のモードならここの関数を呼んで
 * そのまま返る。**遊ぶ経路とは 1 行も交わらない。**
 *
 * もとは `hd2d/app/hd2d_app.cpp` に全部入っていた（17,981 行のうち 8,834 行、
 * 49% が検査だった）。読めないので `hd2d/app/checks/` へ分けている（2026-09-06）。
 *
 * ## 足すときは
 *
 * 1. `hd2d/app/checks/` のどれかに関数を書く（`namespace hd2d` の直下。無名名前空間ではない）
 * 2. ここに宣言を足す
 * 3. `platform/windows/hd2d_main.cpp` に引数を足し、`run()` の分岐から呼ぶ
 * 4. `tools/hd2d_verify/golden.py` の `CHECKS` に 1 行足して `--record` し直す
 *
 * ## 振る舞いを変えていないことの確かめ方
 *
 *     python tools/hd2d_verify/golden.py --check
 *
 * 検査の出力を丸ごと突き合わせる。`run()`（ゲームループ）は通らないので、
 * そちらを触ったときは実機で遊んで見ること（道具の説明の「この網が見ていない所」）。
 */
#pragma once

#include "app/hd2d_app.h"      //!< `AppOptions`（検査は全部これ 1 つだけを受け取る）
#include "voxel/part_motion.h" //!< `MotionKind`（`motion_kind_name()` が採る）

namespace hd2d {

/*! @brief `--material-check`: ライブラリぜんぶの材質の推定を数える。**GL 不要。** */
int run_material_check(const AppOptions &options);

/*! @brief `--prefab-check=`: プレハブ 1 個の形と材質の内訳を出す。**GL 不要。** */
int run_prefab_check(const AppOptions &options);

/*! @brief `--pad-check`: 同時押しとマクロのトリガーの検査。**窓も GL もコアも要らない。** */
int run_pad_check(const AppOptions &options);

/*! @brief `--motion-check`: 動き（P6）の検査。**窓も GL も要らない。** */
int run_motion_check(const AppOptions &options);

/*! @brief `--world-check`: フロアの生成・光・視線・地形の対応表の検査。**GL 不要。** */
int run_world_check(const AppOptions &options);

/*! @brief `--town-check`: 実データの町を読んで組み立てを見る。**GL 不要。** */
int run_town_check(const AppOptions &options);

/*!
 * @brief `--slab-ladder-check`: 一辺 64〜512 の板が書けるかの段階検査。**GL 不要。**
 * @details **エディタ側（`HD2D_EDITOR`）にしか無い。**書き出しに
 * `hd2d/edit/prefab_writer.cpp` が要り、Android と配布用ビルドはあの木を丸ごと
 * 外している。呼び出し側（`hd2d_app.cpp`）も同じ条件で分けてある。
 */
#if defined(HD2D_EDITOR)
int run_slab_ladder_check(const AppOptions &options);
#endif

/*!
 * @brief `--vr-check`: VR（OpenXR）の検査。**窓と GL は要り、実機も要る。**
 * @details ヘッドセットが繋がっていないと `XR_ERROR_FORM_FACTOR_UNAVAILABLE` で
 * 必ず失敗する（環境の話であってコードの不具合ではない）。そのため
 * `tools/hd2d_verify/golden.py` の網には入れていない。
 */
int run_vr_check(const AppOptions &options);

/*! @brief `--terrain-check`: 地形の描画・画調・面の汚しの検査。**窓と GL が要る。** */
int run_terrain_check(const AppOptions &options);

/*! @brief `--post-check`: 後処理（ブルーム・被写界深度・LUT）の検査。**窓と GL が要る。** */
int run_post_check(const AppOptions &options);

/*! @brief `--cutaway-check`: 手前を刈る切り欠きの検査。**窓と GL が要る。** */
int run_cutaway_check(const AppOptions &options);

/*! @brief `--ui-check`: 画面まわり（パネル・持ち物・メニュー）の検査。**窓と GL が要る。** */
int run_ui_check(const AppOptions &options);

/*!
 * @brief `--prefab-view=`: プレハブを 1 個だけ描いて見せる。**検査ではない**（人が見るもの）。
 * @note 合否を出さないので `tools/hd2d_verify/golden.py` の網には入れていない。
 */
int run_prefab_view(const AppOptions &options);

/*! @brief `--town-view`: 実データの町を描いて見せる。**検査ではない**（人が見るもの）。 */
int run_town_view(const AppOptions &options);

/*!
 * @brief `MotionKind` を人が読む名前へ（画面と記録に出す）。
 * @details 定義は `checks/input_checks.cpp`。`--motion-check` と
 * `checks/view_modes.cpp` の `run_prefab_view()` の両方が使う。
 */
const char *motion_kind_name(MotionKind kind);

//! SDL キューから入力の振り分けを検査する（コアを起動しない）。
int run_sdl_input_check();
int run_core_link_check();

} // namespace hd2d
