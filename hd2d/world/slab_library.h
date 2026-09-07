/*!
 * @file slab_library.h
 * @brief 実体（プレイヤ・モンスター・アイテム）の**ボクセル板**のライブラリ。
 *
 * 素材を焼くのは `tools/voxel/tile_to_slab.py`。
 *
 * ## なぜ板なのか（2026-08-15 に決めた）
 * フィギュア（1 体ずつ彫った駒）は**工数が重いので断念**し、代わりに
 * **タイルをそのままボクセルの板にする**ことにした。1 画素 = 1 ボクセルの 1 対 1 なので、
 * タイルをピクセルパーフェクトなものへ差し替えたら、同じ道具で焼き直すだけで追随する。
 *
 * ## ビルボードとの違い
 * | | ビルボード（旧） | ボクセル板（新） |
 * |---|---|---|
 * | 実体 | アトラス上の矩形。1 枚 4 頂点 | プレハブ 1 つ。120〜404 三角形 |
 * | 向き | 軸拘束でカメラを追う | **固定**（面は南＝ +y を向く） |
 * | 光 | 専用のシェーダで弱く効かせる | **ボクセルと同じ**式で当たる |
 * | 影 | α の輪郭 | 板の形そのもの |
 *
 * **光を弱める例外は入れない**（2026-08-15 に決めた「ボクセル板の描画は弄らない。
 * 暗さはダンジョンの空間自体の明るさで調整する」）。明るさの引数は
 * `Hd2dSettings::dungeon_light` にある。
 *
 * ## 遅延読み込み
 * 目録には 1,700 枚以上あるが、**画面に出た索引だけ**読んで GPU へ載せる
 * （`sprite_atlas` が 1 枚 4KB ずつ焼いていたのと同じ考え）。
 * 読めなかった索引は **-1 を覚えて二度と試さない**（毎フレーム失敗し続けない）。
 *
 * ## 捏造しない
 * 板が無い実体は**何も描かず、数える**。プレースホルダの箱を出したりしない
 * （`tile_catalog.h` の「目録に無い索引は何も描かない」と同じ約束）。
 */
#pragma once

#include "render/voxel_renderer.h"
#include "voxel/prefab.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hd2d {

class SlabLibrary {
public:
    /*!
     * @brief SDL_image を起こす。
     * @details 板そのものは PNG を読まないが、**`ui/ui_image.cpp` が `IMG_Load` を使う**。
     * 以前は `SpriteAtlas::init()` がここを持っていて、板へ移したときに落とすと
     * 題字も枠絵も読めなくなる（実際に 1 度そうなりかけた）。
     */
    bool init(std::string &err);
    //! 載せた板を GPU から降ろす。`renderer` が nullptr なら CPU 側だけ捨てる。
    void shutdown(VoxelRenderer *renderer);

    //! 板の置き場（既定は `<voxel_dir>/slab`）。
    void set_dir(const std::string &dir) { this->dir_ = dir; }
    const std::string &dir() const { return this->dir_; }

    /*!
     * @brief コアが申告した置き場（`hello_ack.asset_roots.slab`）。空なら申告なし。
     *
     * @details。別コア（幻想蛮怒）は自分の板を
     * `<exe_dir>/gensoband/assets/slab` に持っており、そこと既定の置き場の**両方**から
     * 引く必要がある——名寄せで転用する変愚由来の板（`R0955` 等）は既定側に、
     * 幻想蛮怒固有の ASCII 板（`GA121_14` 等）は申告側にしかない。
     * 探す順は［**申告 → 既定**］。申告側を先にするのは、同じ名前の板を別コアが
     * 上書きしたいときにそれが効くようにするため。
     * @note **変愚コアは申告しない**ので、ここは空のまま＝従来と 1 バイトも変わらない。
     */
    void set_declared_dir(const std::string &dir) { this->declared_dir_ = dir; }
    const std::string &declared_dir() const { return this->declared_dir_; }

    /*!
     * @brief タイル索引に対応する板を引く。無ければその場で読んで GPU へ載せる。
     * @param tile_path 目録が持っている PNG のパス。**名前だけ**を使う（`R955.png` → `R955`）。
     * @return 板の番号。読めなければ **-1**（呼び出し側は描かない）。
     */
    int acquire(std::uint16_t tile_index, const std::string &tile_path, VoxelRenderer &renderer);

    GpuPrefab &gpu(int id) { return this->entries_[static_cast<std::size_t>(id)].gpu; }
    const GpuPrefab &gpu(int id) const { return this->entries_[static_cast<std::size_t>(id)].gpu; }

    std::size_t loaded() const { return this->entries_.size(); }
    //! 読めなかった索引の数（**捏造せずに数える**）。
    std::size_t missing() const { return this->missing_; }
    //! 直近に読めなかった名前（起動直後の切り分け用。1 つだけ覚える）。
    const std::string &last_error() const { return this->last_error_; }

private:
    struct Entry {
        Prefab prefab;
        GpuPrefab gpu;
    };

    std::string dir_{ "assets/voxel/slab" };
    //! コアの申告（設計 §5.4）。空 = 申告なし＝従来どおり `dir_` だけを見る。
    std::string declared_dir_;
    //! タイル索引 → `entries_` の添字。**-1 は「読めなかった」**で、再試行しない印でもある。
    std::unordered_map<std::uint16_t, int> index_;
    std::vector<Entry> entries_;
    std::size_t missing_{ 0 };
    std::string last_error_;
};

//! `tilework/sfc/R955.png` → `R955`。板の名前はタイルの名前と同じにしてある。
std::string slab_name_from_path(const std::string &tile_path);

} // namespace hd2d
