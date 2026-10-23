/*!
 * @file hd2d_entry_android.cpp
 * @brief ボクセル HD2D の Android 入口（`SDL_main`）。
 *
 * Windows の `platform/windows/hd2d_main.cpp` と同じ役目。違いは 4 つ：
 *   1. 入口が `WinMain` ではなく `SDL_main`（SDLActivity が Java から呼ぶ）
 *   2. 起動直後に内部ストレージへ chdir し、APK の assets を展開する（旧版と同じ手順）
 *   3. コアを**子プロセスではなく同一プロセスのスレッド**として結線する。
 *      コアは APK 同梱の別 .so（libhengcore.so / libtangcore.so）で、コア選択画面で
 *      選ばれたものを dlopen して入口を引く（`AppOptions::core_thread_resolve`。§2 の多コア形）
 *   4. 起動引数が無い（`--terrain-check` 等の自己検査も起動できない。旧版 A-07 と同じ）
 *
 * ここに**ゲームの都合を書かない**のは Windows 側と同じ規律。
 */
#include <SDL.h>
#include <SDL_main.h>

#include <android/log.h>
#if defined(HENGBAND_QUEST)
#include <sys/system_properties.h>
#endif

#include <dlfcn.h>
#include <jni.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "android/android_save_migrator.h"
#include "android/android_asset_installer.h"
#include "android/android_log_redirect.h"
#include "android/android_paths.h"
#include "app/hd2d_app.h"
#include "app/core_import_ui.h"

namespace {

constexpr const char *kTag = "hengband-hd2d";

/*!
 * @brief 起こし直しの申し送りを次のプロセスへ渡すファイル（基準ディレクトリの中。2026-10-07。AH-09）。
 * @details 言語を替えて同じコアを起こし直すとき、Android では**プロセスごと**起こし直す
 * （同じプロセスで同じコアを 2 度目に起こすと、コアの大域の状態が残っていて握手の前に止まる）。
 * 中身は 3 行: コア（.so の名前）・続きから始めるか（0/1）・前の回でコア選択の画を出していたか（0/1）。
 */
constexpr const char *kRelaunchFile = "core_relaunch.txt";

bool write_relaunch_file(const hd2d::CoreRelaunch &relaunch)
{
    std::ofstream out(kRelaunchFile, std::ios::binary | std::ios::trunc);
    out << relaunch.core_path << '\n' << (relaunch.resume ? 1 : 0) << '\n' << (relaunch.core_select_shown ? 1 : 0) << '\n';
    return static_cast<bool>(out);
}

//! 前のプロセスが置いた申し送りを読む（**読むと消す**。無ければ空）。
hd2d::CoreRelaunch take_relaunch_file()
{
    hd2d::CoreRelaunch relaunch;
    std::ifstream in(kRelaunchFile, std::ios::binary);
    if (!in) {
        return relaunch;
    }
    std::string core, resume, shown;
    std::getline(in, core);
    std::getline(in, resume);
    std::getline(in, shown);
    in.close();
    (void)std::remove(kRelaunchFile);
    relaunch.core_path = core;
    relaunch.resume = (resume == "1");
    relaunch.core_select_shown = (shown == "1");
    return relaunch;
}

/*!
 * @brief Java の `HengbandHd2dActivity.restartProcess()` を呼ぶ（別のプロセスの活動がこのプロセスを終わらせる）。
 * @return 呼べたら真（呼べても、起こし直しが実際に起きるのは少し後）。
 */
bool request_process_restart()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if ((env == nullptr) || (activity == nullptr)) {
        return false;
    }
    bool ok = false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID id = (cls != nullptr) ? env->GetMethodID(cls, "restartProcess", "()V") : nullptr;
    if (id != nullptr) {
        env->CallVoidMethod(activity, id);
        ok = !env->ExceptionCheck();
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (cls != nullptr) {
        env->DeleteLocalRef(cls);
    }
    env->DeleteLocalRef(activity);
    return ok;
}

bool is_packaged_core_name(const std::string &name)
{
    static const char *const kPackaged[] = {
        "libhengcore.so", "libtangcore.so", "libgensocore.so", "libsilcore.so",
        "libfroxcore.so", "librogue2core.so", "librogue36core.so", "librogue54core.so",
        "libangbandcore.so", "libumoriacore.so", "libtobandcore.so", "libdcsscore.so",
    };
    for (const char *const packaged : kPackaged) {
        if (name == packaged) return true;
    }
    return false;
}

/*!
 * @brief コア .so からコアスレッドの入口（`hengband_core_entry`）を引く。
 * @details コア選択の「在るものだけ」判定と接続の両方から呼ばれる。handle は
 * 開きっぱなしで持つ（コアはプロセス寿命と同じ。dlclose の機会が無い）。
 * @return 入口。無い .so・引けない .so は nullptr（選択肢に並ばない）。
 */
hd2d::AppOptions::CoreThreadEntry resolve_core_entry(const std::string &requested)
{
    const auto imported = hd2d::imported_core_path(requested);
    const std::string cache_key = imported.empty() ? requested : imported.string();
    static std::map<std::string, void *> handles;
    auto it = handles.find(cache_key);
    if (it == handles.end()) {
        void *handle = nullptr;
        std::vector<std::string> candidates;
        if (!imported.empty()) {
            candidates.push_back(imported.string());
        } else if (is_packaged_core_name(requested)) {
            /*
             * `dlopen("libxxx.so")` は Android の linker namespace / packaging の組み合わせで
             * 探索結果が端末依存になりうる。ApplicationInfo.nativeLibraryDir の実ファイルが
             * ある構成（現在の useLegacyPackaging=true）では絶対パスを先に使う。
             * 将来 APK 内保持へ戻して実ファイルが無い場合は soname にフォールバックする。
             */
            const auto native_dir = hd2d::core_import_config().compiler.parent_path();
            const auto full = native_dir / requested;
            std::error_code ec;
            if (std::filesystem::is_regular_file(full, ec)) candidates.push_back(full.string());
            candidates.push_back(requested);
        } else {
            candidates.push_back(requested);
        }
        for (const auto &candidate : candidates) {
            handle = ::dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (handle != nullptr) {
                __android_log_print(ANDROID_LOG_INFO, kTag, "core loaded: %s -> %s",
                    requested.c_str(), candidate.c_str());
                break;
            }
            const char *const why = ::dlerror();
            __android_log_print(ANDROID_LOG_WARN, kTag, "core dlopen failed: %s (%s)",
                candidate.c_str(), why != nullptr ? why : "unknown");
        }
        if (handle == nullptr) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "core so not available: %s", requested.c_str());
        }
        it = handles.emplace(cache_key, handle).first;
    }
    if (it->second == nullptr) {
        return nullptr;
    }
    auto entry = reinterpret_cast<hd2d::AppOptions::CoreThreadEntry>(::dlsym(it->second, "hengband_core_entry"));
    if (entry == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "hengband_core_entry not found in %s", cache_key.c_str());
    }
    return entry;
}

/*!
 * @brief その .so が**この APK に入っているか**。**開かない。**
 *
 * @details 2026-08-23 に決めた「コアの選択画面時点ではコアの起動はしないように」。
 * 以前はコア選択の「在るものだけ」判定も `resolve_core_entry` で答えていた。あれは
 * `dlopen(RTLD_NOW)` なので、**選ぶ前に 4 本ぜんぶを読み込んで静的初期化まで走らせて**
 * いた（＝実質「全部起こす」）。
 *
 * 代わりにここでは名前だけを見る。**根拠は組み方**である——
 * `android/hd2d/src/main/cpp/CMakeLists.txt` が `hengcore` / `tangcore` / `gensocore` /
 * `silcore` / `froxcore` の 5 本を**条件なしで**組み、`:hd2d` も `:quest` も同じツリーを使う
 * コアは同じ APK に入る。ファイルの有無で見ないのは、
 * `useLegacyPackaging = false` だと .so は APK の中に置かれたままで**展開された実体が
 * 無い**ため（`access()` は必ず失敗する）。
 *
 * @note ここに載っていて実際には開けない .so があれば、選んだ後の解決で気づく
 * （`hd2d_app.cpp` が理由を出して終わる。**黙って別のコアを起こさない**）。
 * コアを増やすときは CMakeLists と**この表の両方**に足すこと。
 */
bool core_so_available(const std::string &so_name)
{
    if (!hd2d::imported_core_path(so_name).empty()) return true;
    /*
     * ここは「APK に組み込むと決めた名前か」だけを見る。ファイル存在確認をしてはいけない。
     * extractNativeLibs/useLegacyPackaging の設定次第では .so は APK 内にだけ存在し、
     * `is_regular_file()` は偽になる。実際に読み込めるかは選択後の resolve_core_entry が
     * dlopen して判定し、失敗理由を logcat とダイアログへ出す。
     */
    return is_packaged_core_name(so_name);
}

//! 起動を続けられない失敗を、ログと（可能なら）ダイアログの両方で伝える。
void fatal(const std::string &message)
{
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Hengband HD2D", message.c_str(), nullptr);
}

#if defined(HENGBAND_QUEST)
/*!
 * @brief VR で入るか。
 * @details Quest 版は**起動と同時に VR**が既定。ただし system property
 * `debug.hengband.vr` が `"0"` なら立てない——ランタイム不調と描画不調を切り分ける口で、
 * `adb shell setprop debug.hengband.vr 0` だけで 2D パネル起動へ落とせる
 * （`SDL_ENV` のメタデータと違い**入れ直しが要らない**のがここでの利点）。
 */
bool want_vr_at_start()
{
    char value[PROP_VALUE_MAX]{};
    if (__system_property_get("debug.hengband.vr", value) > 0) {
        return std::string(value) != "0";
    }
    return true;
}
#endif

} // namespace

extern "C" int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    // 0. 診断（`fprintf(stderr, ...)`）を logcat へ繋ぐ。Android の失敗は
    //    「黙って止まる」型ばかりなので、これがいちばん先（旧版 §7bis A-T3）。
    platform_android::redirect_stdio_to_logcat();

    /*
     * 1. SIGPIPE を無視する。コアとの通信路は `pipe()` で、相手が閉じた後の write は
     *    既定ではプロセスごと落とす。無視しておけば write が EPIPE を返し、
     *    輸送層（protocol_transport.cpp）が IoError として持ち帰る＝「コア消滅」の
     *    通常経路に乗る。
     */
    (void)std::signal(SIGPIPE, SIG_IGN);

    /*
     * 1'. **タッチのマウス写しを切る**（2026-08-12 に決めた「Android 版はそもそも
     *     マウス機能を殺してしまおう」）。SDL は既定で最初の指をマウスに写すが、
     *     一人称の相対マウスモード中は写しの座標が実際のタッチ位置からずれ、
     *     スティックの触り直しが「左クリック＝決定」に化けた（AH-T9）。操作は
     *     バーチャルパッドに一本化し、写し由来のタップ移動・タップ決定・下段境界の
     *     指ドラッグは Android では持たない。**USB マウスなどの実マウスは写しではない**
     *     ので、繋げば従来どおり効く。窓を作る前に立てること。
     */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    // 2. 基準ディレクトリ（アプリ専用の内部ストレージ）へ chdir。
    //    `assets/voxel` も `hd2d.cfg` も `lib/` も、以降ぜんぶここが起点（K-10 と同じ前提）。
    if (!platform_android::prepare_and_enter_base_dir()) {
        fatal("内部ストレージを準備できませんでした。ストレージの空きを確認してください。");
        return 1;
    }

    // 3. APK の assets を実ファイルへ展開する（assets.manifest の版が同じなら何もしない）。
    //    コア（stdio）・プレハブ（ifstream）・タイル（絶対パスで IMG_Load）が読むため。
    if (std::string err; !platform_android::install_assets_if_needed(err)) {
        fatal("ゲームデータの展開に失敗しました。\n" + err);
        return 1;
    }

    // 3.5. 旧 EUC 版が書いたセーブの**ファイル名**を UTF-8 へ改名する（1 回だけ実体が動く）。
    //      これをしないと、日本語の名前のセーブがロード一覧で □ の並びになる。
    (void)platform_android::migrate_legacy_save_names();

    // 4. hd2d を起動。コアの起こし方だけ Android の形（選ばれた .so のスレッド）に差し替える。
    hd2d::AppOptions options;
    //! 前のプロセスが言語を替えて起こし直しを頼んでいれば、その申し送り（下の輪の註）。
    options.relaunch = take_relaunch_file();
    if (!options.relaunch.core_path.empty()) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "relaunched process: core=%s resume=%d",
            options.relaunch.core_path.c_str(), options.relaunch.resume ? 1 : 0);
    }
    options.core_thread_resolve = &resolve_core_entry;
    //! 「在るか」は開かずに答える（選ぶ前に 4 本とも読み込まないため。上の註）。
    options.core_thread_available = &core_so_available;
#if defined(HENGBAND_QUEST)
    /*
     * Quest では**被った状態で起動する**（起動引数が無いので、ここが唯一の入口）。
     * 立たなかったら今までどおり平らで続く＝機内では 2D パネルのタイトル画面が出る。
     * 「アプリは生きているが XR が立たない」が一目で分かるので、これが退路になる（§6）。
     */
    options.vr = want_vr_at_start();
    __android_log_print(ANDROID_LOG_INFO, kTag, "quest: vr=%d", options.vr ? 1 : 0);
#endif
    /*
     * 遊び終えたらコア選択へ戻す（`hd2d::kRunRestart`。Windows と同じ形）。
     * 言語を替えたときは**同じコアを起こし直す**（`hd2d::kRunRelaunchCore`。追補 A2）。
     */
    int rc = hd2d::run(options);
    while ((rc == hd2d::kRunRestart) || (rc == hd2d::kRunRelaunchCore)) {
        if (rc == hd2d::kRunRelaunchCore) {
            options.relaunch = hd2d::take_core_relaunch();
            __android_log_print(ANDROID_LOG_INFO, kTag, "language changed; restarting the same core");
        } else {
            options.relaunch = hd2d::CoreRelaunch{};
            __android_log_print(ANDROID_LOG_INFO, kTag, "core finished; back to the core picker");
        }
        /*
         * **プロセスごと起こし直す**（2026-10-07。AH-09）。同じプロセスで同じコアを 2 度目に起こすと、
         * コアの .so に前の回の大域の状態が残っていて握手の前に止まる。申し送りをファイルに書き、
         * Java の起こし直しを頼んで待つ（別のプロセスの活動がこのプロセスを終わらせる）。
         * 15 秒たっても終わらなければ、従来どおり同じプロセスで続ける（申し送りのファイルは消す）。
         */
        if (write_relaunch_file(options.relaunch) && request_process_restart()) {
            __android_log_print(ANDROID_LOG_INFO, kTag, "waiting for the process restart");
            for (int i = 0; i < 150; ++i) {
                SDL_Delay(100);
            }
            __android_log_print(ANDROID_LOG_WARN, kTag, "process restart did not happen; continuing in this process");
        }
        (void)std::remove(kRelaunchFile);
        rc = hd2d::run(options);
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "hd2d::run returned %d", rc);
    return rc;
}
