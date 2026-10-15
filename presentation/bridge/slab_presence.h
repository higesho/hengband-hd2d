/*!
 * @file slab_presence.h
 * @brief 目録の実体の行のうち、画面が引く板（.vox）がどの置き場にも無い数を数える。
 *
 * 古い C のコア（幻想蛮怒・Sil-Q・Frox）は、目録を読んだ記録に「PNG が無い数」を出していた。PNG は開発の頃の
 * 2D の絵で、HD2D は使わない（配布物には 1 枚も入っていない）ので、配布物では毎回「全部無い」と出ていた。
 * その行がコアの異常のダイアログの末尾に出て、「PNG が無いのが原因」と読めてしまった（遊んだ人の報告 2026-10-06。
 * 実際の原因は日本語の名前のフォルダでの握手の失敗で、PNG とは無関係だった）。数えるべきは画面が実際に引く板。
 *
 * 画面側（hd2d/world/slab_library.cpp）は、目録のパスのファイル名（拡張子を除く）を板の名前にして、
 * コアが申告した置き場（hello_ack.asset_roots.slab）→ 既定の置き場（assets/voxel/slab）の順に探す。
 * 板を引くのは実体（R・K・P）のセルだけで、地形（F）の行は引かない（hd2d/world/entity_view.cpp）。
 * 置き場のパスはコアの持つ形（Windows は ANSI＝CP932）のまま渡す（ファイルの一覧を読むだけなので変換しない）。
 */
#pragma once

#include "frame/protocol_messages.h"
#include "portable/legacy_os.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace presentation {

struct SlabPresence {
    int entity_rows = 0; //!< 目録の実体の行（R・K・P）の数
    int missing = 0; //!< そのうち、どの置き場にも板が無い数
};

//! 既定の板の置き場（`<data_dir>/assets/voxel/slab`。画面側の既定と同じ）。
inline std::string default_slab_dir()
{
    const std::string base = portable::data_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + "assets" + portable::kPathSep + "voxel" + portable::kPathSep + "slab";
}

inline SlabPresence count_slab_presence(const AssetManifestMessage &manifest, const std::vector<std::string> &dirs)
{
    std::unordered_set<std::string> present;
    for (const auto &dir : dirs) {
        if (dir.empty()) {
            continue;
        }
        for (const auto &name : portable::list_files(dir)) {
            if ((name.size() > 4) && (name.compare(name.size() - 4, 4, ".vox") == 0)) {
                present.insert(name.substr(0, name.size() - 4));
            }
        }
    }
    SlabPresence out;
    for (const auto &asset : manifest.assets) {
        if ((asset.kind != "R") && (asset.kind != "K") && (asset.kind != "P")) {
            continue;
        }
        ++out.entity_rows;
        const std::size_t slash = asset.path.find_last_of("/\\");
        const std::string base = (slash == std::string::npos) ? asset.path : asset.path.substr(slash + 1);
        const std::size_t dot = base.find_last_of('.');
        if (present.count((dot == std::string::npos) ? base : base.substr(0, dot)) == 0) {
            ++out.missing;
        }
    }
    return out;
}

} // namespace presentation
