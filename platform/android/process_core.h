/*!
 * @file process_core.h
 * @brief 別のプロセスで動かすコア（NetHack）の、コアスレッドの入口の代理。
 *
 * NetHack は画面と別のプロセスで動かす（決定 D5。 の案 A2）。コアは APK の jniLibs に
 * `lib*.so` の名前で入れた**実行ファイル**で、インストールのときに `nativeLibraryDir` へ展開される（`useLegacyPackaging = true`）。
 *
 * `CoreLink`（共通の部分）から見ると、ほかのコアと同じ「コアスレッドの入口」である。中身は次のとおり。
 *   1. `posix_spawn` で `nativeLibraryDir/<名前>` を起こす。子の標準入力と標準出力に `CoreLink` のパイプのコア側の端をつなぐ。
 *      標準エラーは継承する（入口が logcat へ流すパイプに付け替えてある）。
 *   2. 子の終了を待ち、終了コードを返す。戻ると `CoreLink` がパイプの端を閉じ、画面は「コアが終わった」の通常の道に乗る。
 * 画面が書き口を閉じると、子の読みが EOF になり、原作の hangup の道で保存して終わる（nethack/adapter/nh_main.cpp）。
 */
#pragma once

#include <filesystem>
#include <string>

#include "app/hd2d_app.h"

namespace platform_android {

//! 別のプロセスで動かすコアの名前か（`libNetHackCore.so`・`libNetHackJpCore.so`）。
bool is_process_core_name(const std::string &name);

/*!
 * @brief その実行ファイルが展開先に在って、起こせるか（`access(X_OK)`）。
 * @details ほかのコアと違い、名前の表ではなくファイルを見る。`:quest` は展開しない構成で、jniLibs にも入れないため、
 * ここが偽を返してコア選択に出ない。
 */
bool process_core_available(const std::filesystem::path &native_dir, const std::string &name);

/*!
 * @brief コアスレッドの入口（代理）。名前ごとに 1 本（入口は関数のポインタ 1 本で、名前を持てないため）。
 * @param native_dir 実行ファイルの置き場（`ApplicationInfo.nativeLibraryDir`）。最初に呼んだときの値を覚える。
 * @return 入口。別のプロセスのコアでなければ nullptr。
 */
hd2d::AppOptions::CoreThreadEntry process_core_entry(const std::filesystem::path &native_dir, const std::string &name);

} // namespace platform_android
