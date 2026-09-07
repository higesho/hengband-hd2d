/*!
 * @file android_save_migrator.h
 * @brief 旧 EUC 版が書いたセーブの**ファイル名**を UTF-8 へ改名する（起動時に 1 回）
 *
 * ## なぜ要るか
 * 内部が EUC だった頃のアプリは、日本語の名前のセーブを **EUC のバイト列のファイル名**で
 * 書いた（Linux/Android のファイル名はただのバイト列で、符号化の約束が無い）。
 * 内部 UTF-8 になった今、そのままではロード一覧で **□ の並び**になる
 * （UTF-8 として読めないバイト列だから）。
 *
 * Windows では同じ問題を `setlocale(LC_CTYPE, ".UTF-8")` で解いた（ファイル名も外との
 * 境目——の穴の記録）。Android には CRT ロケールの仕掛けが
 * 無いので、**起動時に一度だけ実ファイルを改名して移行する**のがいちばん素直である。
 *
 * ## 何をするか
 * `lib/save`（と `lib/user`）を走査し、**UTF-8 として不正な名前**のものだけを
 * EUC-JP → UTF-8（だめなら SJIS → UTF-8）で読み直して改名する。
 * 既に同名のファイルがあるときは触らない（上書きで消すよりは □ のままがよい）。
 * セーブの**中身**はここでは触らない——読み込み側（`rd_string`）が符号化を見て直す。
 */
#pragma once

namespace platform_android {

//! 旧符号化のファイル名を UTF-8 へ改名する。改名した数を返す（失敗は 0 扱いで進む）。
int migrate_legacy_save_names();

} // namespace platform_android
