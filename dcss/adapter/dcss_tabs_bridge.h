// DCSS コア: タブの欄のデータ（dcss_tabs_data.h。原作のヘッダを引く TU）が、地図の JSON を受けて覚えている層（dcss_frame.cpp。原作のヘッダは引かない TU）へ
// 問い合わせるための、標準の型だけの宣言。
#pragma once

#include <array>
#include <vector>

namespace dcss_tabs_bridge {

/*!
 * @brief プレイヤーのマスから (dx, dy) だけ離れたマスの前景（怪物）の層を返す。怪物の欄の絵（原作はダンジョンのそのマスの絵を丸ごと使う）に使う。
 * @param layers 下から順に [タイル番号, x のずれ, y のずれ, y_max（0 = 切らない）]
 * @param rel_x,rel_y そのマスの、原作のウェブタイル版の JSON が使う相対座標（`click_cell` に渡す座標）
 * @return 地図の座標に収まっていれば真（前景が無ければ層は空）
 * @details ゲームのスレッドから。
 */
bool entity_layers_at(int dx, int dy, std::vector<std::array<int, 4>> &layers, int &rel_x, int &rel_y);

} // namespace dcss_tabs_bridge
