/*!
 * @file slab_library.cpp
 * @brief `slab_library.h` の実装。
 */
#include "world/slab_library.h"

#include <SDL2/SDL_image.h>

#include <cstdio>

namespace hd2d {

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

} // namespace hd2d
