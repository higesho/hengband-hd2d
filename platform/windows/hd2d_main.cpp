/*!
 * @file hd2d_main.cpp
 * @brief `HengbandHd2d.exe` の入口（ボクセル HD2D。第 3 の実行体）。
 *
 * 基準はボクセル HD2D の設計（契約）と、その段取り。
 *
 * ## この exe の立ち位置
 * `HengbandCore.exe` にプロトコル v1 で繋がる**フロントエンド**である。
 * 旧2D画面は廃止し、HD2D画面を起動する
 * （設計書 §2・必守制約 5）。`src/` には触らない。
 *
 * ## 自分が解釈する起動引数
 * `--protocol-log=` / `--core-protocol-log=` / `--gl-probe` / `--windowed=WxH` の 4 つだけ。
 * **それ以外は全部そのままコアへ渡す**（`--bot-json-output=` などを従来どおり通すため）。
 *
 * ## サブシステム
 * WINDOWS（`WinMain`）。コンソールから起動されたときだけ親のコンソールへ stderr を繋ぐ。
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX //!< windows.h の min/max マクロが `std::max` を壊すため
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "app/hd2d_app.h"
#include "../shared/desktop_command_line.h"
#include "render/camera.h" //!< `kViewTurnCount`（視点回転の段数。1 段 45°）

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

/*!
 * @brief 画面側の stderr を `%TEMP%\hengband-hd2d-stderr.log` へ向ける。
 * @details コアの側は `core_link.cpp` が同じことをしている
 * （`%TEMP%\hengband-hd2d-core-stderr.log`）。**画面側だけ行き先が無かった**——
 * 二重起動で読み合わないよう、名前はコアのものと分けてある。
 * @note `"w"` なので**起動のたびに消える**（溜め込まない）。コアを起こし直しても
 * 画面のプロセスは生き続けるので、**1 回の遊びが 1 本のログ**になる。
 */
void log_stderr_to_temp_file()
{
    char dir[MAX_PATH]{};
    const DWORD len = ::GetTempPathA(MAX_PATH, dir);
    std::string path = ((len > 0) && (len < MAX_PATH)) ? std::string(dir, len) : std::string();
    path += "hengband-hd2d-stderr.log";
    /*
     * **`freopen_s` で開く。共有して開こうとするのは、もうやめる。**
     *
     * `freopen` は独占で開くので、**窓を閉じるまでログを読めない**。それが不便なのは
     * 確かで、2 通り試したが**どちらも 0 バイトのログを作って失敗した**:
     *
     * | 試したもの | 何が起きたか |
     * |---|---|
     * | `_fsopen()` ＋ `_dup2()`（2026-08-25） | 窓の実行体には**標準エラーの番号が無い**（`_fileno(stderr)` が負）ので、複製の行き先が無い |
     * | `CreateFile` ＋ `_open_osfhandle` ＋ `*stderr = *fp`（2026-08-26） | いまの CRT の `FILE` は**中身を持たない器**なので、丸ごと写しても何も繋がらない |
     *
     * **2 度とも「書けなくなった」ことに気づけたのは気づいたことからである。**
     * 読みやすさのために書けることを賭けるのは割に合わない。
     * **読むときは窓を閉じてもらう**——それが確実で、失うのは手間だけである。
     */
    FILE *reopened = nullptr;
    if (::freopen_s(&reopened, path.c_str(), "w", stderr) != 0) {
        return;
    }
    //! **緩衝しない。**落ちた回のログこそ読みたいのに、最後の数百行が消えては意味が無い。
    (void)std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "[hd2d] stderr -> %s\n", path.c_str());
}

/*!
 * @brief 自分の stderr を使える状態にする。
 * @details 既に有効な標準エラー（リダイレクト先）があればそれを使う。無ければ親の
 * コンソールへ相乗りする。**どちらも無ければ一時ファイルへ落とす**
 * （2026-08-25。それまでは行き先を持たず、画面側の警告が**まるごと消えていた**——
 * SH-34「板が字に化ける」で、せっかく出した理由が誰にも届かなかった。
 * 窓を二重に叩いて起動する遊ぶ人に「コンソールから起動してください」と言うのは、
 * 起きた後では手遅れである）。
 */
void prepare_stderr()
{
    const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
    if ((handle != nullptr) && (handle != INVALID_HANDLE_VALUE)) {
        return;
    }
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        log_stderr_to_temp_file();
        return;
    }
    const HANDLE console = ::CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (console == INVALID_HANDLE_VALUE) {
        log_stderr_to_temp_file();
        return;
    }
    ::SetStdHandle(STD_ERROR_HANDLE, console);
    FILE *reopened = nullptr;
    (void)::freopen_s(&reopened, "CONOUT$", "w", stderr);
}

//! `--windowed=1600x900` を読む。読めなければ何もしない（既定のまま）。
/*!
 * @brief `--windowed=WxH` を読む。
 * @note **受け取らなかったときは必ず言う。**以前は黙って既定へ落としていたので、
 * `--windowed=600x500`（下限 640×400 未満）が無視されたことに気づけず、
 * 1600×900 で撮れた画像を見て「なぜ大きいのか」を考える羽目になった。
 * 捨てたことを黙らせないのはこの企画の方針である（可視窓の打ち切りと同じ）。
 */

/*!
 * @brief 作業フォルダを自 exe のあるフォルダへ移す（配布物の形のときだけ）。
 * @param args 解釈済みの起動引数（相対のファイルパスを絶対へ直す）
 * @details 画面側は素材（`assets/lang`・`assets/fx`・`assets/ui/title.png`…）・cfg（`hd2d-*.cfg`）・
 * ログ（`logs/`）を**作業フォルダからの相対**で読み書きする。配布物のショートカットや、別のフォルダからの
 * 起動（エクスプローラーで exe を直に開く・他のツールから呼ぶ）では作業フォルダが exe の場所と違うので、
 * 訳の表が見つからず案内に訳の名前（`hd2d.app.hd2d-app.key-hint-feature-menu` など）がそのまま出ていた。
 *
 * **exe の隣に `assets/` があるときだけ**移す。開発ツリーのリポジトリ直下から
 * `.taskwork\...\out\HengbandHd2d.exe` を起こす検査の使い方（素材はリポジトリ直下の `assets/` から読む）
 * では、exe の隣に `assets/` が無いので何も変わらない。移したあとも相対で渡された
 * ファイル引数（`--shot=` など）が元の作業フォルダを指すよう、移す前に絶対パスへ直す。
 * `HENGBAND_DATA_ROOT` は呼び手の指定が先（`hd2d_app.cpp`）。Android・Quest は入口が内部ストレージへ
 * `chdir` 済み、Linux は `prepare_user_data` が保存先へ `chdir` 済みなので、この関数は Windows だけ。
 */
void anchor_to_exe_directory(std::vector<std::string> &args)
{
    wchar_t buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if ((len == 0) || (len >= MAX_PATH)) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path exe_dir = std::filesystem::path(std::wstring(buf, len)).parent_path();
    if (exe_dir.empty() || !std::filesystem::is_directory(exe_dir / "assets", ec)) {
        return;
    }
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) {
        return;
    }
    if (std::filesystem::equivalent(cwd, exe_dir, ec) && !ec) {
        return;
    }
    //! ファイルを指す引数（値が相対のパスなら、移す前の作業フォルダから絶対へ直す）。
    static const char *const kPathArgs[] = {
        "--protocol-log=", "--core-protocol-log=", "--test-input-file=", "--shot=", "--lineage-shot=",
        "--lineage-data=", "--model-", "--voxel-dir=", "--lut=", "--lut-export=", "--town-data=",
        "--status-hud-check=", "--light-range-check=", "--attack-fx-check=", "--edit=", "--edit-new=",
    };
    for (std::string &arg : args) {
        for (const char *prefix : kPathArgs) {
            if (!arg.starts_with(prefix)) {
                continue;
            }
            const std::size_t eq = arg.find('=');
            if ((eq == std::string::npos) || ((eq + 1) >= arg.size())) {
                break;
            }
            const std::string value = arg.substr(eq + 1);
            const bool absolute = (value.size() >= 2 && value[1] == ':') || value[0] == '\\' || value[0] == '/';
            if (!absolute) {
                //! `--model-*`・`--shot=`・`--test-input-file=` は UTF-8、それ以外は ANSI で受けている（`WinMain` の注記）。
                const bool utf8 = arg.starts_with("--model-") || arg.starts_with("--shot=") || arg.starts_with("--test-input-file=");
                std::string base;
                if (utf8) {
                    const auto u8 = cwd.u8string();
                    base.assign(reinterpret_cast<const char *>(u8.data()), u8.size());
                } else {
                    base = cwd.string();
                }
                arg = arg.substr(0, eq + 1) + base + "\\" + value;
            }
            break;
        }
    }
    (void)::SetCurrentDirectoryW(exe_dir.c_str());
}

} // namespace

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    prepare_stderr();

    hd2d::AppOptions options;
    // Keep existing core argument encoding. Model paths, --shot= and --test-input-file= use Unicode from Windows (UTF-8).
    int wide_count = 0;
    LPWSTR *wide_args = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    std::vector<std::string> model_args(static_cast<std::size_t>(__argc));
    if (wide_args != nullptr) {
        for (int i = 1; i < __argc && i < wide_count; ++i) {
            const std::wstring_view wide = wide_args[i];
            if (wide.starts_with(L"--model-") || wide.starts_with(L"--shot=") || wide.starts_with(L"--test-input-file=")) {
                const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                    static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
                if (length > 0) {
                    model_args[i].resize(length);
                    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                        static_cast<int>(wide.size()), model_args[i].data(), length, nullptr, nullptr);
                }
            }
        }
        LocalFree(wide_args);
    }
    std::vector<std::string> args;
    for (int i = 1; i < __argc; ++i) {
        args.emplace_back(model_args[i].empty() ? __argv[i] : model_args[i]);
    }
    anchor_to_exe_directory(args);
    hd2d::desktop::apply_options(args, options);
    return hd2d::desktop::run(options);
}
