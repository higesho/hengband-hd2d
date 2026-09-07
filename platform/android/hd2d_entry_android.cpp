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

#include <csignal>
#include <map>
#include <string>
#include <vector>

#include "android/android_save_migrator.h"
#include "android/android_asset_installer.h"
#include "android/android_log_redirect.h"
#include "android/android_paths.h"
#include "app/hd2d_app.h"

namespace {

constexpr const char *kTag = "hengband-hd2d";

/*!
 * @brief コア .so からコアスレッドの入口（`hengband_core_entry`）を引く。
 * @details コア選択の「在るものだけ」判定と接続の両方から呼ばれる。handle は
 * 開きっぱなしで持つ（コアはプロセス寿命と同じ。dlclose の機会が無い）。
 * @return 入口。無い .so・引けない .so は nullptr（選択肢に並ばない）。
 */
hd2d::AppOptions::CoreThreadEntry resolve_core_entry(const std::string &so_name)
{
    static std::map<std::string, void *> handles;
    auto it = handles.find(so_name);
    if (it == handles.end()) {
        void *handle = ::dlopen(so_name.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "core so not available: %s (%s)",
                so_name.c_str(), ::dlerror());
        }
        it = handles.emplace(so_name, handle).first;
    }
    if (it->second == nullptr) {
        return nullptr;
    }
    auto entry = reinterpret_cast<hd2d::AppOptions::CoreThreadEntry>(::dlsym(it->second, "hengband_core_entry"));
    if (entry == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "hengband_core_entry not found in %s", so_name.c_str());
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
    static const char *const kPackaged[] = {
        "libhengcore.so",
        "libtangcore.so",
        "libgensocore.so",
        "libsilcore.so",
        /*
         * FroxComposband。CMakeLists が
         * `froxcore` を条件なしで組むので、他の 4 本と同じ立場である。
         */
        "libfroxcore.so",
    };
    for (const char *const name : kPackaged) {
        if (so_name == name) {
            return true;
        }
    }
    return false;
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
        rc = hd2d::run(options);
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "hd2d::run returned %d", rc);
    return rc;
}
