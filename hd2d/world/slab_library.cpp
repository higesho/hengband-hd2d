/*!
 * @file slab_library.cpp
 * @brief `slab_library.h` の実装。
 */
#include "world/slab_library.h"

#include "assets/tile_catalog.h"

#include <SDL2/SDL_image.h>

#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hd2d {

void SlabLibrary::set_declared_dir(const std::string &dir)
{
#ifdef _WIN32
    /*
     * UTF-8 → UTF-16 → ANSI（CP_ACP）。UTF-8 として読めないとき（古いコアが ANSI のまま送ってきたとき）は
     * そのまま持つ——従来どおりの動きで、ASCII だけのパスはどちらでも同じバイト列になる。
     */
    if (!dir.empty()) {
        const int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, dir.data(), static_cast<int>(dir.size()), nullptr, 0);
        if (wide_len > 0) {
            std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, dir.data(), static_cast<int>(dir.size()), wide.data(), wide_len);
            const int ansi_len = WideCharToMultiByte(CP_ACP, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
            if (ansi_len > 0) {
                std::string ansi(static_cast<std::size_t>(ansi_len), '\0');
                WideCharToMultiByte(CP_ACP, 0, wide.data(), wide_len, ansi.data(), ansi_len, nullptr, nullptr);
                this->declared_dir_ = ansi;
                return;
            }
        }
    }
#endif
    this->declared_dir_ = dir;
}

std::string slab_name_from_path(const std::string &tile_path)
{
    const std::size_t slash = tile_path.find_last_of("/\\");
    const std::string base = (slash == std::string::npos) ? tile_path : tile_path.substr(slash + 1);
    const std::size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

bool SlabLibrary::init(std::string &err)
{
    //! `ui_image.cpp` が `IMG_Load` を使う（ヘッダの注記）。ここが唯一の起こし手である。
    constexpr int wanted = IMG_INIT_PNG;
    if ((IMG_Init(wanted) & wanted) != wanted) {
        err = std::string("IMG_Init(PNG) failed: ") + IMG_GetError();
        return false;
    }
    return true;
}

void SlabLibrary::shutdown(VoxelRenderer *renderer)
{
    if (renderer != nullptr) {
        for (auto &entry : this->entries_) {
            renderer->release(entry.gpu);
        }
    }
    this->entries_.clear();
    this->index_.clear();
    this->missing_ = 0;
    this->last_error_.clear();
    IMG_Quit();
}

int SlabLibrary::acquire_item(const TileCatalog &catalog, std::uint16_t item_id, VoxelRenderer &renderer)
{
    const std::uint16_t tile = catalog.find_item_tile(item_id);
    const TileEntry *const entry = (tile != 0) ? catalog.find(tile) : nullptr;
    return (entry == nullptr) ? -1 : this->acquire(tile, entry->path, renderer);
}

int SlabLibrary::acquire(std::uint16_t tile_index, const std::string &tile_path, VoxelRenderer &renderer)
{
    const auto found = this->index_.find(tile_index);
    if (found != this->index_.end()) {
        return found->second; // **-1 も覚えている**（毎フレーム読み直さない）
    }

    const std::string name = slab_name_from_path(tile_path);
    Entry entry;
    std::string err;
    /*
     * ［申告された置き場 → 既定の置き場］の 2 段（`set_declared_dir` の説明・設計 §5.4）。
     * 申告が空（＝変愚コア）のときは 2 段目だけを試すので、従来と同じ挙動になる。
     * 覚える理由（失敗を -1 で記憶する）も従来のまま——**両方で外れて初めて**外れである。
     */
    bool loaded = false;
    if (!name.empty()) {
        if (!this->declared_dir_.empty()) {
            loaded = load_prefab(this->declared_dir_, name, entry.prefab, err);
        }
        if (!loaded) {
            loaded = load_prefab(this->dir_, name, entry.prefab, err);
        }
    }
    if (!loaded) {
        this->index_.emplace(tile_index, -1);
        ++this->missing_;
        if (this->last_error_.empty()) {
            this->last_error_ = name + ": " + err;
        }
        /*
         * **外れたら、その場で言う**（2026-08-25。こう気づいた——「再開したら狼が C になった。
         * もう一度保存して再開したら板に戻った」）。
         *
         * 外れは上の `index_` へ **-1 として覚える**ので、**その起動の間は二度と試さない**
         * ——1 度きりの取りこぼしでも、その実体はその回ずっと字の板で出続ける。
         * `last_error_` は 1 つしか覚えず、しかも**どこにも出していなかった**ので、
         * 「なぜ外れたか」が誰にも分からないまま消えていた。1 行出せば次に起きたとき読める。
         */
        std::fprintf(stderr, "[hd2d] 板が読めません: %s（索引 %u・%s）: %s ／ 探した所: \"%s\" と \"%s\"\n",
            name.c_str(), static_cast<unsigned>(tile_index), tile_path.c_str(), err.c_str(),
            this->declared_dir_.c_str(), this->dir_.c_str());
        return -1;
    }
    entry.minimum={1e9f,1e9f,1e9f};entry.maximum={-1e9f,-1e9f,-1e9f};
    for(const auto &part:entry.prefab.parts) {
        const auto &model=entry.prefab.vox.models[part.model_index];const float scale=1.f/entry.prefab.voxels_per_cell;
        entry.minimum.x=std::min(entry.minimum.x,part.offset[0]*scale);entry.maximum.x=std::max(entry.maximum.x,(part.offset[0]+model.size[0])*scale);
        entry.minimum.y=std::min(entry.minimum.y,part.offset[1]*scale);entry.maximum.y=std::max(entry.maximum.y,(part.offset[1]+model.size[1])*scale);
        entry.minimum.z=std::min(entry.minimum.z,part.offset[2]*scale);entry.maximum.z=std::max(entry.maximum.z,(part.offset[2]+model.size[2])*scale);
    }
    if (!renderer.upload(entry.prefab, entry.gpu, err)) {
        this->index_.emplace(tile_index, -1);
        ++this->missing_;
        if (this->last_error_.empty()) {
            this->last_error_ = name + " を GPU へ載せられませんでした: " + err;
        }
        //! 上と同じ理由で必ず言う。**こちらは資源が尽きた疑いが濃い**（板は 1 枚 2MB ある）。
        std::fprintf(stderr, "[hd2d] 板を GPU へ載せられません: %s（索引 %u・載っている板 %zu 枚）: %s\n",
            name.c_str(), static_cast<unsigned>(tile_index), this->entries_.size(), err.c_str());
        return -1;
    }
    /*
     * **CPU 側のボクセルはここで捨てる**（2026-08-21。128px の板を受け入れるため）。
     *
     * `VoxModel::voxels` は `x * y * z` の**密な**配列である。64px の板で 262KB、
     * **128px なら 1 枚 2MB** になり、ライブラリは読んだ板を最後まで抱えるので、
     * 300 枚も出れば 600MB を超える。載せたあとに要るのは `entry.gpu` だけで、
     * `entry.prefab` を読む所は**この関数の中にしか無い**（描くのは
     * `draw_entity_slabs()` で、渡すのは `gpu` だけ）。
     *
     * @note プレハブの器そのものは残す。`voxels_per_cell` や `footprint` は
     * 軽く、後から要る目が出たときに読めるようにしておく。
     */
    for (auto &model : entry.prefab.vox.models) {
        model.voxels.clear();
        model.voxels.shrink_to_fit();
    }

    const int id = static_cast<int>(this->entries_.size());
    this->entries_.push_back(std::move(entry));
    this->index_.emplace(tile_index, id);
    return id;
}

bool SlabLibrary::front_pixels(const std::string &tile_path, int &width, int &height, std::vector<std::uint8_t> &rgba) const
{
    const std::string name = slab_name_from_path(tile_path);
    if (name.empty()) {
        return false;
    }
    Prefab prefab;
    std::string err;
    bool loaded = false;
    if (!this->declared_dir_.empty()) {
        loaded = load_prefab(this->declared_dir_, name, prefab, err);
    }
    if (!loaded) {
        loaded = load_prefab(this->dir_, name, prefab, err);
    }
    if (!loaded) {
        return false;
    }
    const int side = std::max(1, prefab.voxels_per_cell);
    // 足元（z = 0）から板の頭まで。
    int top = 0;
    for (const auto &part : prefab.parts) {
        const auto &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        top = std::max(top, static_cast<int>(part.offset[2]) + model.size[2]);
    }
    top = std::min(top, side);
    if (top <= 0) {
        return false;
    }
    std::vector<int> front_y(static_cast<std::size_t>(side) * static_cast<std::size_t>(top), -1);
    rgba.assign(static_cast<std::size_t>(side) * static_cast<std::size_t>(top) * 4u, 0);
    bool any = false;
    int highest = -1; //!< 色の付いたボクセルがある最も高い z（モデルの寸法には空の段が含まれることがある）
    for (const auto &part : prefab.parts) {
        const auto &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        const int ox = static_cast<int>(part.offset[0]);
        const int oy = static_cast<int>(part.offset[1]);
        const int oz = static_cast<int>(part.offset[2]);
        for (int z = 0; z < model.size[2]; ++z) {
            const int gz = oz + z;
            if ((gz < 0) || (gz >= top)) {
                continue;
            }
            for (int x = 0; x < model.size[0]; ++x) {
                const int gx = ox + x;
                if ((gx < 0) || (gx >= side)) {
                    continue;
                }
                for (int y = model.size[1] - 1; y >= 0; --y) { // +y が絵の面（カメラ側）。最も手前を採る
                    const std::uint8_t index = model.at(x, y, z);
                    if (index == 0) {
                        continue;
                    }
                    const std::size_t pixel = (static_cast<std::size_t>(top - 1 - gz) * static_cast<std::size_t>(side)) + static_cast<std::size_t>(gx);
                    if (front_y[pixel] < oy + y) {
                        front_y[pixel] = oy + y;
                        rgba[(pixel * 4u) + 0u] = prefab.vox.palette[index][0];
                        rgba[(pixel * 4u) + 1u] = prefab.vox.palette[index][1];
                        rgba[(pixel * 4u) + 2u] = prefab.vox.palette[index][2];
                        rgba[(pixel * 4u) + 3u] = 255;
                        any = true;
                        highest = std::max(highest, gz);
                    }
                    break;
                }
            }
        }
    }
    if (!any) {
        return false;
    }
    // 先頭（頭の側）の空の行を落とす。
    const std::size_t drop_rows = static_cast<std::size_t>(top - 1 - highest);
    rgba.erase(rgba.begin(), rgba.begin() + static_cast<std::ptrdiff_t>(drop_rows * static_cast<std::size_t>(side) * 4u));
    width = side;
    height = highest + 1;
    return true;
}

} // namespace hd2d
