/*!
 * @file hud_snapshot.h
 * @brief HUD 表示スナップショット（設計書 §4.4）
 */
#pragma once

#include <string>
#include <vector>

struct HudSnapshot {
    std::string name;
    int hp{};
    int hp_max{};
    /*!
     * @brief `hp` の名札。**空なら画面の既定（`HP`）**（SH-08 の続き。2026-08-22）。
     *
     * @details 由来は `sp_label` と同じで、**そちらを入れたら片方だけ日本語になった**
     * ——Sil-Q の状態列はコアが `生命力` と書くのに、HUD のゲージだけ `HP` と `声` が
     * 並ぶ画になった（実機の絵で判った）。語を運ぶなら 2 本とも運ぶ。
     */
    std::string hp_label;
    int sp{};
    int sp_max{};
    /*!
     * @brief `sp` の名札。**空なら画面の既定（`SP`）**（SH-08 / W5）。
     *
     * @details 画面は「魔力」という語をどこにも持たない——**コア固有の知識を画面へ
     * 持ち込まない**という必守制約 1 のため。`SP` はもともと変愚の語で、
     * **Sil-Q に魔力は無い**（あちらのその欄は歌の力＝`Voice` である）。
     * ベタ書きのままだと Sil-Q の画面に `SP 15/18` と出て、原作を知る人ほど読み違える。
     *
     * **語はコアが決める。**訳もコアの側で当たっている（コアはその版の言語を知っている）ので、
     * ここへ来る時点で**表示できる UTF-8** である。
     *
     * @note 空を送るコアは 1 画素も変わらない（変愚・幻想蛮怒・短愚蛮怒）。
     * **短くすること**——画面は名札の幅で桁を採るが、長いとゲージが痩せる。
     */
    std::string sp_label;
    /*!
     * @brief 所持金。**負なら「このコアに金銭という概念が無い」**（0 は「持っていない」）。
     * @details 描画側（`hd2d/ui/game_hud.cpp`）は負なら `AU` の項目を**組まない**。
     * Sil-Q（`SilCore.exe`）が -1 を送ってくる。**0 で送ると `AU 0` が画面に並ぶ**が、
     * 「0 なら出さない」では直せない——変愚では所持金 0 が当たり前にあり、
     * そのとき本物の `AU 0` まで消えてしまう。**「無い」と「0」は別の事実**である。
     * @note 変愚・幻想蛮怒・短愚蛮怒は負にならないので、絵は 1 画素も変わらない。
     */
    int gold{};
    int depth{};
    //! プレイヤレベル。**負なら「このコアにレベルが無い」**（`gold` と同じ約束）。
    //! Sil-Q は経験値を技能に振る作りでレベルを持たないので -1 を送ってくる。
    int level{};
    std::string status_line;
    std::vector<std::string> right_top_lines;
    std::vector<std::string> right_bottom_lines;
};
