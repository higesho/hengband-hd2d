// DCSS コア: 画面の部品（dcss-ui）の地図のマウス操作（足元の左・右クリック・マスにマウスを載せたときのツールチップ）を、ゲームの中から答える。段 U3・U4。
//
// libhd2d.cpp だけが取り込む（原作のヘッダを引く TU。`AppHdr.h` を先に引いてから取り込むこと）。Linux・Android の組み立てでも libhd2d.cpp が組まれるので、
// ファイルの登録は要らない。原作のソースは書き換えない。
//
// 何をするか:
//   原作の手元のタイル版は、地図の区画（`tilereg-dgn.cc`。`USE_TILE_LOCAL` の中にしか無く、このコアには組まれない）がマウスを受ける。
//   その中の次の 2 つを、ウェブタイル版では画面が送れない（`click_cell` は足元を区別しない・ツールチップが無い）ので、ここで同じ規則で組み直す。
//   - 足元のクリック（`DungeonRegion::handle_mouse` の `you.pos() == gc`）: 左 = アイテムがあれば拾う（複数なら拾うメニュー）・
//     階段なら上り下り・どれでもなければ 1 ターン待つ。Shift + 左 = 階段だけ。右 = キャラクターの概要。Shift + 右 = 信仰の画面。
//   - ツールチップ（`tile_dungeon_tip`）と、マスの説明の 1 行（`get_cell_mouseover_tag`・雲の名前）。
//   足元以外のマスのクリック（左 = 旅・移動・攻撃、右 = 見る）は、ウェブタイル版が持つ `click_cell` をそのまま使うので、ここは通らない。
//
// 受ける行（画面が `webtiles_input` で送る。原作の制御メッセージではないので `dcss_posix_shim.cpp` の recvfrom が、受けた行の `msg` が `hd2d_` で
// 始まるときに、このファイルの関数へ渡す）:
//   {"msg":"hd2d_click_self","button":1|3,"mods":N}      mods: 1 = Shift・2 = Ctrl
//   {"msg":"hd2d_hover","dx":N,"dy":N}                  プレイヤーのマスから見た相対（原作の座標の原点は画面の側が知らないため）
// 返すもの:
//   - 入力待ちへ返す 1 行（足元のクリックが命令になるときの `{"msg":"key","keycode":-N}`）。空なら何も返さない（原作は入力待ちを続ける）
//   - 画面への返事（`hd2d_hover` へ `{"t":"dcss_ui","k":"hover",…}`）
//
// 呼び出しはすべてゲームのスレッド（入力待ちの中）。
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cio.h"
#include "cloud.h"
#include "command.h"
#include "coord.h"
#include "database.h"
#include "describe.h"
#include "directn.h"
#include "env.h"
#include "item-prop.h"
#include "items.h"
#include "jobs.h"
#include "libutil.h"
#include "macro.h"
#include "mon-util.h"
#include "message.h"
#include "output.h"
#include "libconsole.h"
#include "player.h"
#include "quiver.h"
#include "religion.h"
#include "species.h"
#include "state.h"
#include "stash.h"
#include "stringutil.h"
#include "terrain.h"
#include "tile-env.h"
#include "travel.h"
#include "view.h"

namespace dcss_ui_mouse {

//! 画面が `mods` に入れる値（原作の `TILES_MOD_SHIFT`・`TILES_MOD_CTRL` と同じ意味）。
constexpr int kModShift = 1;
constexpr int kModCtrl = 2;

namespace detail {

//! `"key":123` の数を取る（無ければ `fallback`）。
inline int find_int(const std::string &line, const char *key, int fallback)
{
    const std::string pat = std::string("\"") + key + "\":";
    const std::size_t at = line.find(pat);
    if (at == std::string::npos)
        return fallback;
    return std::atoi(line.c_str() + at + pat.size());
}

inline void put_json_string(std::string &out, const std::string &v)
{
    out += '"';
    for (const unsigned char c : v)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            }
            else
                out += (char)c;
        }
    }
    out += '"';
}

//! ツールチップの本文と、本文の `%` へ当てる命令の並び（`insert_commands` に渡す）。
struct Tip
{
    std::string text;
    std::vector<command_type> keys;

    //! 1 行足す（すでに字があれば改行で区切る）。
    void line(const std::string &t)
    {
        if (!text.empty())
            text += "\n";
        text += t;
    }
    //! 1 行足し、その行の `%` に当てる命令も控える。
    void line(const std::string &t, command_type c)
    {
        line(t);
        keys.push_back(c);
    }
};

//! 命令を待っている場面か（足元のクリック・ツールチップはこの間だけ働く）。
inline bool awaiting_command()
{
    return mouse_control::current_mode() == MOUSE_MODE_COMMAND;
}

//! 階段の上り下りの命令か。
inline bool is_stair_command(command_type c)
{
    return c == CMD_GO_DOWNSTAIRS || c == CMD_GO_UPSTAIRS;
}

//! 旅で行けるマスなら「旅」、行けなければ「向かって動く」の行を足す。
inline void travel_or_approach(Tip &tip, const coord_def &gc)
{
    tip.line(jtrans(click_travel_safe(gc) ? "[L-Click] Travel" : "[L-Click] Move towards"));
}

//! 見える敵がいるマスの左クリックの行を足す。敵が見えていれば真（その場合は別の行を足さない）。
inline bool monster_click_lines(Tip &tip, const coord_def &gc)
{
    const monster *mon = monster_at(gc);
    if (!mon || !you.can_see(*mon))
        return false;
    if (mon->friendly())
    {
        tip.line(jtrans("[L-Click] Move"));
        return true;
    }
    if (!you.see_cell_no_trans(mon->pos()))
        return true;

    tip.text = mon->name(DESC_A);
    const item_def *wielded = you.weapon();
    const bool shoots = wielded && is_range_weapon(*wielded);
    const auto &primary = quiver::get_primary_action();
    const auto &secondary = quiver::get_secondary_action();
    const bool same_action = shoots && primary == secondary;
    if (shoots)
    {
        if (!same_action)
            tip.line(jtrans_notrim("[L-Click] ") + primary->quiver_description().tostring() + " (%)", CMD_PRIMARY_ATTACK);
    }
    else if ((gc - you.pos()).rdist() <= (wielded ? weapon_reach(*wielded) : 1))
        tip.line(jtrans("[L-Click] Attack"));
    else
        tip.line(jtrans("[L-Click] Move towards"));

    if (secondary->is_valid())
    {
        const std::string lead = jtrans_notrim(same_action ? "[L-Click / Shift + L-Click] " : "[Shift + L-Click] ");
        tip.line(lead + secondary->quiver_description().tostring() + " (%)", CMD_FIRE);
    }
    return true;
}

//! 敵のいない足元以外のマスの左クリックの行（移動・戸の開け閉め・旅）。
inline void plain_cell_lines(Tip &tip, const coord_def &gc)
{
    const dungeon_feature_type here = env.grid(gc);
    const bool next_to_me = adjacent(gc, you.pos());
    if (!cell_is_solid(gc))
    {
        if (next_to_me)
        {
            tip.line(jtrans("[L-Click] Move"));
            if (feat_is_open_door(here))
                tip.line(jtrans("[Shift + L-Click] Close (%)"), CMD_CLOSE_DOOR);
        }
        else if (env.map_knowledge(gc).feat() != DNGN_UNSEEN)
            travel_or_approach(tip, gc);
    }
    else if (feat_is_closed_door(here))
    {
        if (next_to_me)
            tip.line(jtrans("[L-Click] Open door (%)"), CMD_OPEN_DOOR);
        else
            travel_or_approach(tip, gc);
    }
}

//! 敵がいないマスで、足元かどうかに関わらず出る行（アイテムを拾う・階段などの Shift + 左クリック）。
inline void item_and_stair_lines(Tip &tip, const coord_def &gc)
{
    const item_def *seen = you.see_cell(gc) ? env.map_knowledge(gc).item() : nullptr;
    if (seen && !item_is_stationary(*seen))
        tip.line(jtrans("[L-Click] Pick up items (%)"), CMD_PICKUP);

    const dungeon_feature_type feat = env.map_knowledge(gc).feat();
    const command_type dir = feat_stair_direction(feat);
    if (dir == CMD_NO_CMD)
        return;
    const char *what = "use stairs";
    if (feat == DNGN_ENTER_SHOP)
        what = "enter shop";
    else if (feat_is_altar(feat) && player_can_join_god(feat_altar_god(feat)))
        what = "pray at altar";
    else if (feat_is_gate(feat))
        what = "enter gate";
    tip.line(jtrans_notrim("[Shift + L-Click] ") + jtrans(what) + " (%)", dir);
}

//! 足元のマスの左・右クリックの行のうち、足元だけの物。
inline void self_cell_lines(Tip &tip, const coord_def &gc)
{
    if (you.visible_igrd(gc) == NON_ITEM)
    {
        const dungeon_feature_type feat = env.grid(gc);
        const command_type dir = feat_stair_direction(feat);
        if (is_stair_command(dir))
            tip.line(jtrans("[L-Click] Use stairs (%)"), dir);
        else if (feat_is_altar(feat) && player_can_join_god(feat_altar_god(feat)))
            tip.line(jtrans("[L-Click] Pray at altar (%)"), dir);
        else
            tip.line(jtrans("[L-Click] Wait one turn (%)"), CMD_WAIT);
    }
    tip.line(jtrans("[R-Click] Overview (%)"), CMD_RESISTS_SCREEN);
    if (you.religion != GOD_NO_GOD)
        tip.line(jtrans("[Shift + R-Click] Religion (%)"), CMD_DISPLAY_RELIGION);
}

//! 原作 `tile_dungeon_tip`（`tilereg-dgn.cc`）と同じ内容。地図のマス `gc` の左・右クリックの説明。
inline std::string dungeon_tip(const coord_def &gc)
{
    Tip tip;
    const bool at_feet = gc == you.pos();
    bool enemy_there = false;

    if (at_feet)
    {
        tip.text = you.your_name + " (" + species::get_abbrev(you.species) + get_job_abbrev(you.char_class) + ")";
    }
    else
    {
        enemy_there = monster_click_lines(tip, gc);
        if (!enemy_there)
            plain_cell_lines(tip, gc);
    }
    if (!enemy_there)
        item_and_stair_lines(tip, gc);

    if (at_feet)
        self_cell_lines(tip, gc);
    else if (you.see_cell(gc) && env.map_knowledge(gc).feat() != DNGN_UNSEEN)
        tip.line(jtrans("[R-Click] Describe"));

    if (!tip.text.empty())
        insert_commands(tip.text, tip.keys, false);
    return tip.text;
}

//! 原作 `DungeonRegion::handle_mouse` の `MOVE` の枝: マスの説明の 1 行（床は出さない。雲があれば雲の名前を先に）。
inline std::string cell_tag(const coord_def &gc)
{
    std::string terrain = get_cell_mouseover_tag(gc);
    if (terrain == "floor" || terrain == tagged_jtrans("[feature]", "floor"))
        terrain.clear();
    cloud_struct *cloud = you.see_cell(gc) ? cloud_at(gc) : nullptr;
    if (!cloud)
        return terrain;
    const std::string cloud_name = tagged_jtrans("[cloud]", cloud->cloud_name(true));
    return terrain.empty() ? cloud_name : cloud_name + "\n" + terrain;
}

inline std::string key_line(command_type cmd)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{\"msg\":\"key\",\"keycode\":%d}", encode_command_as_key(cmd));
    return buf;
}

//! 足元の床が階段なら、その上り下りの命令（階段でなければ `CMD_NO_CMD`）。
inline command_type stair_under_feet()
{
    const command_type cmd = feat_stair_direction(env.grid(you.pos()));
    return is_stair_command(cmd) ? cmd : CMD_NO_CMD;
}

//! 足元の左クリック（Shift なし）: アイテムがあれば拾う・階段なら上り下り・どちらでもなければ 1 ターン待つ。
inline std::string click_feet_plain()
{
    const int pile = you.visible_igrd(you.pos());
    if (pile == NON_ITEM)
    {
        const command_type stairs = stair_under_feet();
        return key_line(stairs != CMD_NO_CMD ? stairs : CMD_WAIT);
    }
    if (count_movable_items(pile) < 2)
        return key_line(CMD_PICKUP);
    // 複数あるときは、拾うメニューをすぐ開く（原作と同じ）。
    pickup_menu(pile);
    flush_prev_message();
    redraw_screen();
    update_screen();
    return "";
}

//! 足元のクリック 1 回の結果。返す字は入力待ちへ渡す 1 行（空なら何も返さない）。
inline std::string click_feet(int button, bool shift)
{
    if (button == 1)
    {
        if (!shift)
            return click_feet_plain();
        const command_type stairs = stair_under_feet();
        return stairs == CMD_NO_CMD ? std::string() : key_line(stairs);
    }
    if (button != 3)
        return "";
    if (!shift)
        return key_line(CMD_RESISTS_SCREEN);
    return you.religion != GOD_NO_GOD ? key_line(CMD_DISPLAY_RELIGION) : std::string();
}

} // namespace detail

/*!
 * @brief 画面が送った `hd2d_*` の行を 1 つ処理する。
 * @param line 受けた行（JSON の字）
 * @param reply 画面へ返す 1 通（`{"t":"dcss_ui",…}`）。無ければ空のまま
 * @return 入力待ちへ返す 1 行。空なら何も返さない
 */
inline std::string handle_line(const std::string &line, std::string &reply)
{
    using namespace detail;
    reply.clear();
    if (line.find("\"hd2d_click_self\"") != std::string::npos)
    {
        // 足元のクリックは、命令を待っているときだけ意味がある（原作は `MOUSE_MODE_COMMAND` のときだけ見る）。
        if (!awaiting_command())
            return "";
        const int mods = find_int(line, "mods", 0);
        return click_feet(find_int(line, "button", 1), (mods & kModShift) != 0);
    }
    if (line.find("\"hd2d_hover\"") != std::string::npos)
    {
        const int dx = find_int(line, "dx", 0);
        const int dy = find_int(line, "dy", 0);
        const coord_def gc = you.pos() + coord_def(dx, dy);
        std::string tip;
        std::string tag;
        // ツールチップは命令を待っているときだけ（原作: 狙いの最中は出さない）。マスの説明は地図の中の既知のマスだけ。
        if (map_bounds(gc))
        {
            if (awaiting_command())
                tip = dungeon_tip(gc);
            if (env.map_knowledge(gc).seen() && mouse_control::current_mode() != MOUSE_MODE_NORMAL)
                tag = cell_tag(gc);
        }
        reply = "{\"t\":\"dcss_ui\",\"k\":\"hover\",\"dx\":" + std::to_string(dx) + ",\"dy\":" + std::to_string(dy) + ",\"tip\":";
        put_json_string(reply, tip);
        reply += ",\"tag\":";
        put_json_string(reply, tag);
        reply += "}";
        return "";
    }
    return "";
}

} // namespace dcss_ui_mouse
