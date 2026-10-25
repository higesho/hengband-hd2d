// DCSS コア: 画面の部品（dcss-ui）の開始メニューへ送る中身（モードの一覧・セーブデータの一覧と人形）を、ゲームの中から読んで組む。段 U3。
//
// libhd2d.cpp だけが取り込む（原作のヘッダを引く TU。`dcss_tabs_data.h` の後に取り込むこと。`dcss_tabs_data::Out` を使う）。原作のソースは書き換えない。
//
// 背景: 原作のタイル版（`USE_TILE_LOCAL`）の開始メニュー（`startup.cc` の `UIStartupMenu`）は、ゲームモードとセーブデータの各項目に絵（モードの絵・
// キャラクターの人形）が付き、行の高さが広い縦の一覧で、セーブデータは別の欄でスクロールする。ウェブタイル版（このコアが組む `USE_TILE_WEB`）は同じ画面を
// 80×24 の文字画面（`txt`）で送るだけなので、絵も項目の位置も分からない。そこで、`startup.cc` が項目を作るときに使う材料を、アダプタが同じ関数・同じ規則で読んで
// `{"t":"dcss_ui","k":"startup",…}` として送り、画面が原作のタイル版と同じ形に描く。選択中の項目・名前・説明・操作の案内は、これまでどおり `txt`（と原作のキー処理）が持つ。
//
//   - モードの一覧: `startup.cc` の `entries`（ファイルの中だけの static で外から引けないので、同じ並び・同じ条件で持ち直す）。絵は `tileidx_gametype`
//     （`USE_TILE_LOCAL` の中だけなので同じ対応で持ち直す）。
//   - セーブデータ: 原作の `find_all_saved_characters()`（`UIStartupMenu` の constructor と同じ呼び出し）。人形は `player_save_info::doll` を、ウェブタイル版の
//     `TilesFramework::send_doll` と同じ順・同じ切り方で `[タイル番号, 下を切る高さ]` の並びにする。
//
// 呼び出しはゲームのスレッド（原作が出力を流すときに呼ばれる sink の中）。
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

#include "files.h"
#include "game-type.h"
#include "ng-input.h"
#include "japanese.h"
#include "player-save-info.h"
#include "state.h"
#include "stringutil.h"
#include "tiledoll.h"
#include "tile-player-flag-cut.h"
#include "tilepick-p.h"
#include "version.h"
#include "rltiles/tiledef-gui.h"

namespace dcss_ui_startup {

//! 原作 `startup.cc` の `game_modes_menu_item`（`label` と `description` は表示するとき `jtrans` で訳す）。
struct ModeItem {
    game_type id;
    const char *label;
    const char *description;
    unsigned tile;
};

static const ModeItem kModes[] = {
    { GAME_TYPE_NORMAL, "Dungeon Crawl", "Dungeon Crawl: The main game: full of monsters, items, gods and danger!", TILEG_STARTUP_STONESOUP },
    { GAME_TYPE_CUSTOM_SEED, "Choose Game Seed", "Play with a chosen custom dungeon seed.", TILEG_STARTUP_STONESOUP },
    { GAME_TYPE_TUTORIAL, "Tutorial for Dungeon Crawl", "Tutorial that covers the basics of Dungeon Crawl survival.", TILEG_STARTUP_TUTORIAL },
    { GAME_TYPE_HINTS, "Hints Mode for Dungeon Crawl", "A mostly normal game that provides more advanced hints than the tutorial.", TILEG_STARTUP_HINTS },
    { GAME_TYPE_DESCENT, "Dungeon Descent", "Mode with a branching, one-way path through the Dungeon.", TILEG_STARTUP_IRONSOUP },
    { GAME_TYPE_SPRINT, "Dungeon Sprint", "Hard, fixed single level game mode.", TILEG_STARTUP_SPRINT },
    { GAME_TYPE_INSTRUCTIONS, "Instructions", "Help menu.", TILEG_STARTUP_INSTRUCTIONS },
    { GAME_TYPE_ARENA, "The Arena", "Pit computer controlled teams versus each other!", TILEG_STARTUP_ARENA },
    { GAME_TYPE_HIGH_SCORES, "High Scores", "View the high score list.", TILEG_STARTUP_HIGH_SCORES },
};

/*!
 * @brief 開始メニューの中身を 1 通の JSON にする。ゲームが始まっていれば空。
 * @details 画面は、直前の文字画面（`txt`）の選択中の項目の字を、ここの一覧の字と突き合わせて、選択中の項目の番号（モード → セーブデータ → 「新しいゲーム」の順）を知る。
 */
static std::string build_json()
{
    if (crawl_state.game_started)
        return std::string();

    std::vector<player_save_info> chars;
    try
    {
        chars = find_all_saved_characters();
    }
    catch (...)
    {
        chars.clear();
    }

    dcss_tabs_data::Out o;
    o.raw("{\"t\":\"dcss_ui\",\"k\":\"startup\",\"prompts\":{\"name\":");
    o.str(jtrans("Enter your name:"));
    o.raw(",\"modes\":");
    o.str(jtrans("Choices:"));
    o.raw(",\"saves\":");
    o.str(jtrans("Saved games:"));
    o.raw(",\"new_game\":");
    o.str(jtrans("New Game"));
    o.raw("},\"modes\":[");
    bool first = true;
    for (const ModeItem &m : kModes)
    {
        if (m.id == GAME_TYPE_DESCENT && Version::ReleaseType != VER_ALPHA)
            continue;
        if (!first)
            o.raw(",");
        first = false;
        o.raw("{\"id\":");
        o.num((long long)m.id);
        o.raw(",\"label\":");
        o.str(jtrans(m.label));
        o.raw(",\"desc\":");
        o.str(jtrans(m.description));
        o.raw(",\"tile\":");
        o.num((long long)m.tile);
        o.raw("}");
    }
    o.raw("],\"saves\":[");
    for (std::size_t i = 0; i < chars.size(); ++i)
    {
        const player_save_info &c = chars[i];
        std::string text = c.short_desc();
        const bool wiz = strip_suffix(text, " (WIZ)");
        if (i > 0)
            o.raw(",");
        o.raw("{\"name\":");
        o.str(c.name);
        o.raw(",\"label\":");
        o.str(text);
        o.raw(",\"wiz\":");
        o.raw(wiz ? "true" : "false");
        o.raw(",\"loadable\":");
        o.raw(c.save_loadable ? "true" : "false");
        o.raw(",\"doll\":[");
        int order[TILEP_PART_MAX];
        int flags[TILEP_PART_MAX];
        tilep_fill_order_and_flags(c.doll, order, flags);
        bool first_part = true;
        for (int k = 0; k < TILEP_PART_MAX; ++k)
        {
            const int p = order[k];
            if (!c.doll.parts[p] || flags[p] == TILEP_FLAG_HIDE)
                continue;
            if (!first_part)
                o.raw(",");
            first_part = false;
            o.raw("[");
            o.num((long long)c.doll.parts[p]);
            o.raw(",");
            o.num(flags[p] == TILEP_FLAG_CUT_BOTTOM ? 18 : TILE_Y);
            o.raw("]");
        }
        o.raw("]}");
    }
    o.raw("]}");
    return o.s;
}

/*!
 * @brief 名前の問い（`newgame.cc` の `_choose_name`）で Enter を押した名前に、同じ名前のセーブがもう在るかを答える（`hd2d_name_check` の行。画面が Enter と一緒に送る）。
 * @details `_choose_name` は、名前を確定したとき `save_exists(get_save_filename(名前))` で上書きかを決め、上書きなら「本当に上書きしますか？ [Y/n]」を自分の窓の文字に出して
 * `Y` を待つ（`overwrite_prompt`）。**この窓はウェブへ送られない**（`push_ui_layout` を呼ばない）ので、画面からは待っていることが見えない。同じ関数・同じ規則（空の名前は
 * 乱数の名前なので重ならない・`is_good_name` でなければ確定しない）で在るかを読み、画面へ `{"t":"dcss_ui","k":"name_check","name":…,"exists":…}` で返す。
 * 呼ばれるのはゲームのスレッド（名前の問いが入力を待つ間）。
 */
static std::string name_check_reply(const std::string &line)
{
    std::string name;
    const std::string pat = "\"name\":\"";
    const std::size_t at = line.find(pat);
    if (at != std::string::npos)
    {
        for (std::size_t i = at + pat.size(); i < line.size() && line[i] != '"'; ++i)
        {
            if (line[i] == '\\' && i + 1 < line.size())
            {
                ++i;
                if (line[i] == 'u' && i + 4 < line.size())
                {
                    const unsigned cp = (unsigned)std::strtoul(line.substr(i + 1, 4).c_str(), nullptr, 16);
                    if (cp < 0x80)
                        name += (char)cp;
                    i += 4;
                }
                else
                    name += line[i];
            }
            else
                name += line[i];
        }
    }
    std::string trimmed = name;
    trim_string(trimmed);
    const bool exists = !trimmed.empty() && is_good_name(name, true) && save_exists(get_save_filename(trimmed));
    dcss_tabs_data::Out o;
    o.raw("{\"t\":\"dcss_ui\",\"k\":\"name_check\",\"name\":");
    o.str(name);
    o.raw(",\"exists\":");
    o.raw(exists ? "true" : "false");
    o.raw("}");
    return o.s;
}

} // namespace dcss_ui_startup
