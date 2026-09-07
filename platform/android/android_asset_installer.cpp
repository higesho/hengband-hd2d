/*!
 * @file android_asset_installer.cpp
 * @brief APK の assets から内部ストレージへゲームデータを取り出す（実装）
 */
#include "android/android_asset_installer.h"

#include "android/android_paths.h"

#include <SDL.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace platform_android {

namespace {

constexpr const char *kTag = "hengband";
constexpr const char *kManifestName = "assets.manifest";
constexpr const char *kStampName = ".assets_stamp";

/*!
 * @brief Activity#getAssets() を JNI で引いて AAssetManager を得る。
 * @details SDL の `SDL_RWFromFile` を使わないのはヘッダのコメントのとおり
 * （相対パスで内部ストレージが先に当たると、更新時に古いファイルを読んでしまう）。
 */
AAssetManager *acquire_asset_manager()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (env == nullptr) {
        return nullptr;
    }
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) {
        return nullptr;
    }

    AAssetManager *mgr = nullptr;
    jclass activity_class = env->GetObjectClass(activity);
    if (activity_class != nullptr) {
        const jmethodID get_assets = env->GetMethodID(activity_class, "getAssets", "()Landroid/content/res/AssetManager;");
        if (get_assets != nullptr) {
            jobject asset_manager = env->CallObjectMethod(activity, get_assets);
            if (asset_manager != nullptr) {
                mgr = AAssetManager_fromJava(env, asset_manager);
                env->DeleteLocalRef(asset_manager);
            }
        }
        env->DeleteLocalRef(activity_class);
    }
    env->DeleteLocalRef(activity);
    return mgr;
}

//! assets の 1 ファイルを丸ごと読む。無い／読めないときは false。
bool read_asset(AAssetManager *mgr, const std::string &name, std::vector<char> &out)
{
    AAsset *asset = AAssetManager_open(mgr, name.c_str(), AASSET_MODE_STREAMING);
    if (asset == nullptr) {
        return false;
    }
    const off64_t len = AAsset_getLength64(asset);
    out.resize(static_cast<size_t>(len));
    bool ok = true;
    off64_t done = 0;
    while (done < len) {
        const int n = AAsset_read(asset, out.data() + done, static_cast<size_t>(len - done));
        if (n <= 0) {
            ok = false;
            break;
        }
        done += n;
    }
    AAsset_close(asset);
    return ok;
}

//! `<相対パス>\t<バイト数>` の行を切り出す（バイト数は健全性の確認にだけ使う）。
bool parse_entry(const std::string &line, std::string &path, long long &size)
{
    const auto tab = line.find('\t');
    if (tab == std::string::npos || tab == 0) {
        return false;
    }
    path = line.substr(0, tab);
    try {
        size = std::stoll(line.substr(tab + 1));
    } catch (...) {
        return false;
    }
    // `..` を含むパスは受け付けない（アセットは自分で作るものだが、
    // 展開先が基準ディレクトリの外へ出る形は原理的に許さない）。
    return path.find("..") == std::string::npos;
}

std::string read_stamp(const std::filesystem::path &stamp_path)
{
    std::ifstream in(stamp_path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string s;
    std::getline(in, s);
    return s;
}

} // namespace

bool install_assets_if_needed(std::string &out_error)
{
    const auto base = base_dir();
    if (base.empty()) {
        out_error = "内部ストレージのパスが取得できませんでした。";
        return false;
    }

    AAssetManager *mgr = acquire_asset_manager();
    if (mgr == nullptr) {
        out_error = "AssetManager を取得できませんでした。";
        return false;
    }

    std::vector<char> manifest_bytes;
    if (!read_asset(mgr, kManifestName, manifest_bytes)) {
        out_error = "assets.manifest がありません（tools/build_android_assets.py を実行してください）。";
        return false;
    }
    const std::string manifest(manifest_bytes.begin(), manifest_bytes.end());

    // 1 行目が版。2 行目以降が `<相対パス>\t<バイト数>`。
    size_t pos = manifest.find('\n');
    if (pos == std::string::npos) {
        out_error = "assets.manifest の形式が不正です。";
        return false;
    }
    std::string version = manifest.substr(0, pos);
    if (!version.empty() && version.back() == '\r') {
        version.pop_back();
    }

    const auto stamp_path = base / kStampName;
    if (read_stamp(stamp_path) == version) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "assets up to date (%s)", version.c_str());
        return true;
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "installing assets (%s)", version.c_str());

    int installed = 0;
    std::vector<char> buf;
    while (pos < manifest.size()) {
        const size_t begin = pos + 1;
        if (begin >= manifest.size()) {
            break;
        }
        size_t end = manifest.find('\n', begin);
        if (end == std::string::npos) {
            end = manifest.size();
        }
        std::string line = manifest.substr(begin, end - begin);
        pos = end;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::string rel;
        long long size = 0;
        if (!parse_entry(line, rel, size)) {
            out_error = "assets.manifest に不正な行があります: " + line;
            return false;
        }

        if (!read_asset(mgr, rel, buf)) {
            out_error = "assets から読めませんでした: " + rel;
            return false;
        }
        if (static_cast<long long>(buf.size()) != size) {
            out_error = "assets のサイズが manifest と一致しません: " + rel;
            return false;
        }

        const auto dst = base / std::filesystem::path(rel);
        std::error_code ec;
        std::filesystem::create_directories(dst.parent_path(), ec);
        if (ec) {
            out_error = "ディレクトリを作れません: " + dst.parent_path().string();
            return false;
        }
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) {
            out_error = "書き込めません: " + dst.string();
            return false;
        }
        if (!buf.empty()) {
            out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
        }
        out.close();
        if (!out) {
            out_error = "書き込みに失敗しました: " + dst.string();
            return false;
        }
        ++installed;
    }

    // セーブ置き場だけは manifest に載せず、ここで確実に用意する
    // （空ディレクトリは assets に入れられないため）。
    std::error_code ec;
    std::filesystem::create_directories(base / "lib" / "save", ec);
    std::filesystem::create_directories(base / "lib" / "apex", ec);
    std::filesystem::create_directories(base / "lib" / "bone", ec);
    std::filesystem::create_directories(base / "lib" / "data", ec);
    std::filesystem::create_directories(base / "lib" / "user", ec);
    // 短愚蛮怒コア（libtangcore.so）の記録系は tangband/lib/ に分離する
    // （sdl_game_bootstrap.cpp の TANGBAND ゲートと対。tangband/UPSTREAM.md）。
    // 多コア形では 1 つの APK に両コアが載るので、ここは無条件に用意する（無害）。
    std::filesystem::create_directories(base / "tangband" / "lib" / "save", ec);
    std::filesystem::create_directories(base / "tangband" / "lib" / "apex", ec);
    std::filesystem::create_directories(base / "tangband" / "lib" / "bone", ec);
    std::filesystem::create_directories(base / "tangband" / "lib" / "data", ec);
    std::filesystem::create_directories(base / "tangband" / "lib" / "user", ec);
    /*
     * 旧 C の 3 コア（幻想蛮怒・Sil-Q・FroxComposband）も同じ形で分離する
     * （どのコアも「変愚の `lib/` とは交差しない」と
     *   決めてある）。
     * **アダプタ側も無ければ作る**が、あちらは `<lib>` そのものが無いと `mkdir` が
     * 失敗する（1 段しか掘らない）ので、展開が済んだこの場で親ごと用意しておく。
     */
    for (const char *core : { "gensoband", "silq", "frox" }) {
        for (const char *dir : { "save", "apex", "bone", "data", "user", "info" }) {
            std::filesystem::create_directories(base / core / "lib" / dir, ec);
        }
    }

    // 版の書き込みは**すべて成功した後**。途中で落ちたら次回もう一度展開させる。
    std::ofstream stamp(stamp_path, std::ios::binary | std::ios::trunc);
    if (!stamp) {
        out_error = "版の記録に失敗しました: " + stamp_path.string();
        return false;
    }
    stamp << version << '\n';
    stamp.close();

    __android_log_print(ANDROID_LOG_INFO, kTag, "assets installed: %d files", installed);
    return true;
}

} // namespace platform_android
