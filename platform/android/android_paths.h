/*!
 * @file android_paths.h
 * @brief Android のディレクトリ解決（Windows の `resolve_lib_path` 相当）
 *
 * Windows 版は「exe のあるディレクトリ」を基準にしていた。Android には
 * exe のディレクトリという概念が無いので、アプリ専用の内部ストレージ
 * （`/data/user/0/<package>/files`）を基準にする。
 *
 * ここで決めた基準ディレクトリへ **プロセスのカレントディレクトリを移す**のが要点。
 * SDL2 UI は cfg（`sdl2_ui_options.cfg` / `sdl2_pad_binds.cfg`）とタイル索引
 * （`tilework/sfc/*.png`）を**相対パスで**扱う設計（HANDOFF §5.2 K-10）なので、
 * cwd さえ合わせれば Windows と同じコードがそのまま動く。
 */
#pragma once

#include <filesystem>

namespace platform_android {

/*!
 * @brief アプリ専用の内部ストレージ（この移植の「基準ディレクトリ」）。
 * @details `SDL_AndroidGetInternalStoragePath()` の値。アンインストールで消える領域で、
 * 権限の宣言なしに読み書きできる（Android 13 でスコープドストレージの影響を受けない）。
 */
std::filesystem::path base_dir();

//! 基準ディレクトリ配下の `lib`（`init_file_paths` に渡す。セーブもこの下）。
std::filesystem::path lib_dir();

/*!
 * @brief 基準ディレクトリを作り、そこへ chdir する。
 * @return 成功したら true
 * @details **アセット展開より前に呼ぶこと。** 失敗すると cfg もログも書けない。
 */
bool prepare_and_enter_base_dir();

} // namespace platform_android
