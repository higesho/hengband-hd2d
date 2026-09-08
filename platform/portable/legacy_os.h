/*!
 * @file legacy_os.h
 * @brief 旧 C の 2 コア（幻想蛮怒・Sil-Q）のアダプタが使う OS の APIを 1 か所にまとめる。
 *
 *
 * ## なぜ要るか
 * `gensoband/adapter/` と `silq/adapter/` は `main-win.c` の写しとして書かれたので、
 * `GetFileAttributesA` / `FindFirstFileA` / `Sleep` / `GetModuleFileNameA` / `_mkdir` を
 * じかに呼んでいる。**Android にはどれも無い。**
 *
 * 各所に `#if defined(_WIN32)` を撒くと、分岐が 20 か所を超えて片方だけ直す事故が起きる。
 * だから**分岐はこのファイル 1 つに閉じ込め**、アダプタは平らな関数だけを呼ぶ。
 *
 * ## Windows 側は今までと 1 ミリも変えない
 * Windows の実装は、いま各アダプタに書かれている中身をそのまま移したものである
 * （末尾の区切りの落とし方、`INVALID_FILE_ATTRIBUTES` の見方、`"*"` の並べ方まで）。
 * 移植のために Windows の挙動を変えない、という線を引くための写しである。
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace portable {

//! この平台のパス区切り。Windows は `\`、それ以外は `/`。
#if defined(_WIN32)
inline constexpr char kPathSep = '\\';
inline constexpr const char *kPathSepStr = "\\";
#else
inline constexpr char kPathSep = '/';
inline constexpr const char *kPathSepStr = "/";
#endif

/*!
 * @brief ディレクトリとして在るか。
 * @details 末尾の区切り（`\` も `/` も）を落としてから見る——`main-win.c` の
 * `check_dir()` と同じ判定。`C:\` のような根は元から末尾に区切りが要るが、
 * lib の下しか渡さないのでここでは考えない。
 */
bool dir_exists(const std::string &path);

/*!
 * @brief ディレクトリを 1 段作る。
 * @return 作れたか（**既に在れば真**）。親が無いときは作らない（`_mkdir` と同じ）。
 */
bool make_dir(const std::string &path);

/*!
 * @brief ディレクトリ直下の**ファイル名**を並べる（ディレクトリは除く）。
 * @details `.` / `..` は返さない。並び順は保証しない（呼び手が整列する）。
 */
std::vector<std::string> list_files(const std::string &dir);

//! 眠る。
void sleep_ms(int ms);

//! 単調に増える時計（ミリ秒）。差だけを見るための値で、起点に意味は無い。
uint64_t tick_ms();

/*!
 * @brief 実行体の置き場（**末尾に区切りを付けて**返す）。引けなければ空。
 *
 * @details Windows は `GetModuleFileNameA` の親。lib を cwd に依らず引くための手段である
 * （`HengbandHd2d.exe` が子として起こすので、cwd が何になるか分からない）。
 *
 * **Android では実行体という概念が合わない。** コアは `libgensocore.so` の中の関数として
 * 走り、データは APK から内部ストレージへ展開されている。そこで**カレントディレクトリ**を
 * 返す——入口（`hd2d_entry_android.cpp`）が展開先へ `chdir` してからコアを起こすので、
 * 「lib の親」という意味はこちらでも成り立つ。
 */
std::string exe_dir();

//! ゲームデータの基準。Windows の cores/ 配置では親、それ以外は exe_dir()。
std::string data_dir();

/*!
 * @brief いまの UTC を ISO 8601（`2026-08-21T04:05:06.789Z`）で返す。
 * @details プロトコルの記録（JSON Lines）の時刻欄に使う。Windows は
 * `GetSystemTime`、それ以外は `gettimeofday` + `gmtime_r`——**どちらも UTC**で、
 * 記録を突き合わせるときに時差を考えなくてよい。
 */
std::string iso_utc_now();

} // namespace portable
