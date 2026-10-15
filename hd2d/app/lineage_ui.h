/*!
 * @file lineage_ui.h
 * @brief ローグライクの系譜図の画面。
 *
 * ## 何のためのものか
 * `assets/ui/lineage.json` に書いてある系譜（Rogue から現行の各コアまでの
 * ソースの直系・影響関係）を、コア選択画面から開ける 1 枚の地図として見せる。
 * 
 *
 * ## 作り
 * データ（`load_lineage`）・並べ方（`layout_lineage`）・描画と入力（`run_lineage`）を
 * 分けてある。並べ方までは GL も窓も要らないので、`--lineage-check` はここだけを通す。
 */
#pragma once

#include "ui/ui_layout.h"

#include <string>
#include <vector>

struct SDL_Window;

namespace hd2d {

class TextOverlay;
class UiPaint;
class UiImagePainter;
class GamePad;
struct Hd2dSettings;

//! リンク 1 本（任意項目。無い項目には出さない）。
struct LineageLink {
    std::string kind;  //!< home / source / wiki / spoiler / community / download
    std::string label_ja;
    std::string label_en;
};
//! `LineageLink` に URL を足したもの（読み込み側だけが持つ。描画側は上を使う）。
struct LineageLinkFull : LineageLink {
    std::string url;
};

/*!
 * @brief 1 項目ぶんの読み物（任意）。`summary`/`note` より長く、遊んでみたくなる書き方の解説。
 * @details `assets/ui/lineage.json` の `guide.ja` / `guide.en`。両方無ければ
 * `summary` → `note` の順に落ちる（`display_overview` を見る側）。
 */
struct LineageGuide {
    std::string lead;                        //!< 導入の段落
    std::vector<std::string> highlights;      //!< 「遊びどころ」の箇条書き（1〜7 個）
    std::string lineage_text;                 //!< 「系譜での位置」の段落
    std::vector<std::string> tips;            //!< 「はじめる人へ」の箇条書き（0〜4 個。空でもよい）
    bool empty() const { return lead.empty() && highlights.empty() && lineage_text.empty() && tips.empty(); }
};

//! 系譜図の 1 項目。`assets/ui/lineage.json` の 1 要素にそのまま対応する。
struct LineageNode {
    std::string id;
    std::string parent;      //!< ソースの直系の親。空なら無し
    std::string influence;   //!< 影響だけの親（点線）。空なら無し
    std::vector<std::string> extra_parents; //!< もう 1 つのソースの親（細い点線・琥珀色）
    std::string name;
    std::string name_en;     //!< 無ければ `name` を使う
    std::string year;
    std::string authors;
    std::string authors_en;  //!< 無ければ `authors` を使う
    std::string status;      //!< core / candidate / info / closed
    std::string core;        //!< status==core のときのコア鍵（rogue36 など）。他は空
    std::string license;     //!< G/N/L/B/A/P/AGPL/closed/unknown
    bool verified{ false };
    std::string note_ja;
    std::string note_en;
    //! 概要（任意。2〜5 文程度）。あれば注記の代わりにこちらを出す。
    std::string summary_ja;
    std::string summary_en;
    //! 読み物（任意。概要よりさらに長い解説）。あれば概要・注記より先に使う。
    LineageGuide guide_ja;
    LineageGuide guide_en;
    //! リンク（任意）。無ければ「リンク」欄自体を出さない。
    std::vector<LineageLinkFull> links;
};

//! `lineage.json` の中身。
struct LineageData {
    std::vector<LineageNode> nodes;
    std::string root; //!< ルートの id（`nodes` の添字ではない）
    int root_index{ -1 };
};

/*!
 * @brief `lineage.json` を読む。失敗したら false・`error` に理由。
 * @param path 既定は `assets/ui/lineage.json`。検査・撮影の用に別ファイルを指せる
 * （`--lineage-data=` / `run_lineage_check` の引数）。
 */
bool load_lineage(LineageData &out, std::string &error, const std::string &path = "assets/ui/lineage.json");

//! 1 項目ぶんの置き場所（世界座標・左上原点）と、木としての段。
struct LineageNodeRect {
    RectPx rect{};
    int depth{ 0 };
};

enum class LineageEdgeKind {
    Source,    //!< ソースの直系（実線）
    Influence, //!< 影響だけ（点線）
    Extra,     //!< もう 1 つのソースの親（細い琥珀色の点線）
};

struct LineageEdge {
    int from{ -1 }; //!< `LineageData::nodes` の添字
    int to{ -1 };
    LineageEdgeKind kind{ LineageEdgeKind::Source };
};

//! 並べ方の結果。`nodes[i]` は `LineageData::nodes[i]` に対応する（添字が同じ）。
struct LineageLayout {
    std::vector<LineageNodeRect> nodes;
    std::vector<LineageEdge> edges;
    int content_w{ 0 };
    int content_h{ 0 };
};

/*!
 * @brief 左から右へ伸びる木として並べる。
 * @details 深さ＝親からの段数を x に、JSON の並び順の DFS で葉から順に振った行を y にする
 * （内側の節は子の行の中間）。ソース・影響のどちらか一方（データの取り決めどおり
 * ちょうど 1 つ）を木の枝として使う。`extra_parents` は枝ではなく、後から足す線。
 * @param node_w 全項目に共通の幅。
 * @param node_heights 項目ごとの高さ（`data.nodes` と同じ添字・同じ個数）。
 * **コアの項目はバナー比 4:1 で高さが違う**ので、ここを 1 本の数ではなく列にしてある。
 * 足りない・多すぎるときは既定の高さで埋める／切り捨てる。
 */
LineageLayout layout_lineage(
    const LineageData &data, int node_w, const std::vector<int> &node_heights, int col_gap, int row_gap);

//! `run_lineage` の返り方。
enum class LineageAction {
    Back,   //!< コア選択画面へ戻る
    Quit,   //!< 窓を閉じる（`SDL_QUIT`）
    Launch, //!< `core_key` のコアで遊ぶ
};

struct LineageResult {
    LineageAction action{ LineageAction::Back };
    std::string core_key;
};

/*!
 * @brief 系譜図の画面を 1 本回す。**自前の `SDL_PollEvent` と swap を持つ**
 * （`run_core_select` / `run_core_import` と同じ作り）。
 * @param images コア項目のバナー（`assets/ui/banner_<鍵>.png`）を貼るための道具
 * （`run_core_select` と同じ `UiImagePainter`）。
 * @param available_core_keys この配布に実際に入っているコアの鍵（`decide()` に渡せるもの）。
 * @param initial_core_key 開いたときに選んでおく項目のコア鍵。空、または一致が無ければルート。
 * @param shot_path 空でなければ、`shot_after` 枚描いたところで自分のフレームバッファを
 * BMP へ書き出し、`Back` として返る（`--lineage-shot=`。`run_core_select` の同名のオプションと同じ作り）。
 * @param data_path `lineage.json` の代わりに読む場所（既定は本物。検査・撮影用）。
 */
LineageResult run_lineage(SDL_Window *window, TextOverlay &text, UiPaint &paint, UiImagePainter &images,
    GamePad *pad, const Hd2dSettings &cfg, const std::vector<std::string> &available_core_keys,
    const std::string &initial_core_key, const std::string &shot_path = std::string(), int shot_after = 30,
    const std::string &data_path = "assets/ui/lineage.json");

/*!
 * @brief `--lineage-check`。窓を開かず、読み込みと並べ方だけを確かめる。
 * @return 0 なら通った（stderr に個数・段数・大きさを出す）。1 なら失敗。
 */
int run_lineage_check(const std::string &data_path = "assets/ui/lineage.json");

} // namespace hd2d
