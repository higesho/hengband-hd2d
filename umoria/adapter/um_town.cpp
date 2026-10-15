/*
 * Umoria の町を固定の地図から作る。
 *
 * 原作の townGeneration() がレシピの手当てでここを呼ぶ。地図が無い・読めないときは false を返し、
 * 原作の作り方（乱数の配置）へ落ちる。地図は `<exe>/umoria/hd2d/town.txt`（66×22。1 文字 1 マス）。
 *
 * Umoria には水・木・山の地形が無いので、コアの中では絶壁・湖・柊・廃墟・店の建物はどれも壊せない壁にする
 * （町で掘らせない。原作の町の外周と同じ扱い）。画面へ何の地形として送るかは town_visual() が答える。
 */

#include "um_town.h"

#include "headers.h"
#ifndef _WIN32
// ui.h renames open/fopen to the tilde-expanding topen/tfopen (defined in um_io.cpp) for the upstream
// files. The adapter does not need that, and the macros would also rename std::fopen and fstream::open.
#undef open
#undef fopen
#endif

#include "portable/legacy_os.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kTownRows = 22;
constexpr int kTownCols = 66;

std::vector<std::string> g_map; //!< 読めた地図（読めなければ空）
Coord_t g_start{ -1, -1 };
/*
 * 読みに行ったか。`town_visual()` は町のマスを描くたびに呼ぶので、地図が無いときに毎回開き直すと、
 * 1 枚の画ごとに 1,452 回ファイルを開いて標準エラーへ書き、パイプが満杯になってコアが止まっていた
 * （2026-10-03。地図を置かずに組んだ exe がキャラ作成の後で止まった）。
 */
bool g_map_tried = false;

bool load_map()
{
    if (!g_map.empty()) return true;
    if (g_map_tried) return false;
    g_map_tried = true;
    std::string path = portable::exe_dir();
    if (!path.empty() && path.back() != portable::kPathSep) path.push_back(portable::kPathSep);
    path += std::string("umoria") + portable::kPathSep + "hd2d" + portable::kPathSep + "town.txt";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "[umoria] town map not found: %s (using the original layout)\n", path.c_str());
        return false;
    }
    std::vector<std::string> rows;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        rows.push_back(line);
    }
    int stores = 0;
    int stairs = 0;
    bool start = false;
    bool ok = static_cast<int>(rows.size()) == kTownRows;
    for (const std::string &row : rows) {
        if (static_cast<int>(row.size()) != kTownCols) ok = false;
        for (const char c : row) {
            if (c >= '1' && c <= '6') ++stores;
            if (c == '>') ++stairs;
            if (c == '@') start = true;
        }
    }
    if (!ok || stores != 6 || stairs != 1 || !start) {
        std::fprintf(stderr, "[umoria] town map is malformed (%zu rows, %d stores, %d stairs) (using the original layout)\n",
            rows.size(), stores, stairs);
        return false;
    }
    g_map = std::move(rows);
    return true;
}

void place_object(int y, int x, int object_id)
{
    const int at = popt();
    dg.floor[y][x].treasure_id = static_cast<uint8_t>(at);
    inventoryItemCopyTo(object_id, game.treasure.list[at]);
}

} // namespace

bool umoria_build_town_from_map()
{
    g_start = Coord_t{ -1, -1 };
    if (!load_map()) return false;
    for (int y = 0; y < kTownRows; ++y) {
        for (int x = 0; x < kTownCols; ++x) {
            Tile_t &tile = dg.floor[y][x];
            const char c = g_map[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            switch (c) {
            case '.':
            case '"':
                tile.feature_id = TILE_DARK_FLOOR;
                break;
            case '@':
                tile.feature_id = TILE_DARK_FLOOR;
                g_start = Coord_t{ y, x };
                break;
            case '>':
                tile.feature_id = TILE_DARK_FLOOR;
                place_object(y, x, config::dungeon::objects::OBJ_DOWN_STAIR);
                break;
            default:
                if (c >= '1' && c <= '6') {
                    // 原作の店の入口と同じ（dungeonBuildStore: 通路の床＋店の入口の物）。
                    tile.feature_id = TILE_CORR_FLOOR;
                    place_object(y, x, config::dungeon::objects::OBJ_STORE_DOOR + (c - '1'));
                } else {
                    tile.feature_id = TILE_BOUNDARY_WALL; // X # ~ T R B
                }
                break;
            }
        }
    }
    return true;
}

void umoria_town_start_position(Coord_t &coord)
{
    if (g_start.y >= 0) coord = g_start;
}

namespace umoria {

TownVisual town_visual(int y, int x)
{
    /*
     * 地図はここでも読む。セーブから町で再開すると町は作られない（原作は階をセーブから読む）ので、
     * `umoria_build_town_from_map()` を通らず地図が空のままになり、木・水・崖・廃墟が全部
     * 永久の壁として送られていた（気づいたこと 2026-10-03「木の横に立って…おかしくなってる」）。
     */
    if (dg.current_level == 0) (void) load_map();
    if (g_map.empty() || dg.current_level != 0 || y < 0 || y >= kTownRows || x < 0 || x >= kTownCols) {
        return TownVisual::None;
    }
    switch (g_map[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]) {
    case '#': return TownVisual::Cliff;
    case '~': return TownVisual::Water;
    case 'T': return TownVisual::Tree;
    case '"': return TownVisual::Grass;
    case 'R': return TownVisual::Ruin;
    default: return TownVisual::None;
    }
}

} // namespace umoria
