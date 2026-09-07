/*!
 * @file android_asset_installer.h
 * @brief APK の assets から内部ストレージへゲームデータを取り出す
 *
 * コア（`src/`）はゲームデータを **stdio**（`angband_fopen`）で読む。APK の assets は
 * 通常のファイルではないので stdio では開けない。よって `lib/` の下は起動時に
 * 実ファイルへ展開する必要がある。
 *
 * 逆に、**SDL 経由でしか読まないもの**は展開しない：
 *   - `tilework/sfc/*.png`（`IMG_Load`）
 *   - `fonts/*.otf`（`TTF_OpenFont`）
 *   - `lib/xtra/{sound,music}/*`（本移植の SDL_mixer 音声。`SDL_RWFromFile`）
 * SDL の `SDL_RWFromFile` は相対パスなら内部ストレージ → assets の順に探すため、
 * 置いたままでも読める。これで展開量が 102MB → 約 8MB に収まり、初回起動が待たされない。
 *
 * @note assets 側は必ず AssetManager から**直接**読む。`SDL_RWFromFile` は相対パスに対して
 * 内部ストレージを先に見るので、更新時に「展開済みの古いファイルを自分自身へ上書きして
 * 新版が入らない」という、気付けない失敗を起こす。
 */
#pragma once

#include <string>

namespace platform_android {

/*!
 * @brief 必要なら assets を内部ストレージへ展開する。
 * @param out_error 失敗時に理由（UTF-8）
 * @return 成功したら true
 * @details `assets.manifest`（ビルド時に tools/build_android_assets.py が生成）の
 * 版文字列を `<base>/.assets_stamp` と突き合わせ、変わっていなければ何もしない。
 * 版が違えば列挙されたファイルをすべて上書きする。
 * @note **セーブ（`lib/save/`）は manifest に載せない**ので消えない。
 */
bool install_assets_if_needed(std::string &out_error);

} // namespace platform_android
