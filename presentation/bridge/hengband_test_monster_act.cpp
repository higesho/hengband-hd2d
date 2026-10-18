/*!
 * @file hengband_test_monster_act.cpp
 * @brief 検査用の「敵の行動を指定する入口」の実装（`hengband_test_monster_act.h`）
 * @details
 * ## 原作の関数を呼ぶだけにする
 * - 魔法 … 原作がモンスターの番の中で呼ぶ `monspell_to_player()`（`mspell/assign-monster-spell.h`）。
 *   魔法の名前は原作の `r_info_ability_flags`（`info-reader/race-info-tokens-table.h`）で引く。
 * - 直接攻撃 … 原作が移動の末に呼ぶ `MonsterAttackPlayer(...).make_attack_normal()`。
 *
 * - 敵の移動 … 原作が敵をテレポートさせる `cave_monster_teleportable_bold()` で立てるかを見て、`teleport_monster_to()` と同じ更新をする。
 * - 行動の停止 … 命令待ちのたびに、止めている敵の `energy_need` を大きな値に置き直す（原作の `sweep_monster_process()` を書き換えない）。
 *
 * 乱数にも乱数の種にも触らない（原作の関数が引く分だけが進む）。
 *
 * ## 番の外から呼ぶために整えた前提
 * 原作の `make_attack_spell()`（`mspell/mspell-attack.cpp`）は、魔法を選んだあと
 * `monspell_to_player(player, 魔法, プレイヤーの y, x, m_idx)` を呼ぶだけで、その前後に持ち込む
 * 状態は無い。番の外から呼ぶために、ここで次を整える。
 *
 * 1. 呼ぶ時点を「コマンド待ち」に限る（`inkey_flag`・全画面の表示が出ていない・キャラクター作成済み・
 *    生きている・フロアを出ようとしていない）。魔法の効果が描画の途中や店の中へ割り込まないため。
 * 2. 撃たせる敵は、有効なモンスター（`is_valid()`・HP が正）に限る。時間停止中（`timewalk_m_idx`）は断る。
 * 3. 魔法はプレイヤーの位置へ向けて撃たせる（原作の `msa_ptr->y, x` と同じ）。弾道が通らない・射程の外の敵は
 *    断る（通らないまま撃つと、壁に当たるだけで検査にならないため）。
 * 4. 撃ったあと `handle_stuff()` で、魔法が立てた更新・再描画の旗を処理する（原作は番のあと、ゲームの
 *    主ループが処理する）。メッセージ・体力の変化は、そのあとの次のフレームに載る。
 * 5. 命令ファイルは実行の**前に**消す。魔法のメッセージが `-more-` を出して入力待ち（この入口の呼び出し箇所）へ
 *    入り直しても、同じ命令を二度実行しないため。入り直しの間は実行中の旗で止める。
 * 6. 実行の間だけ `inkey_flag` を偽にし、終わったら元に戻す。原作はモンスターの番を「コマンド待ちではない」間に
 *    進めるので、その状態に合わせる。真のままだと、魔法のメッセージが出す `-more-` の待ちを、画面側（`Bridge::capture`）が
 *    「コマンド待ち」と見分けられず、行 0 の `-続く-` がフレームに載らない（画面側が続きの待ちに気づけない）。
 */
#include "bridge/hengband_test_monster_act.h"

#include "core/stuff-handler.h"
#include "grid/grid.h"
#include "info-reader/race-info-tokens-table.h"
#include "io/input-key-acceptor.h"
#include "monster-attack/monster-attack-player.h"
#include "monster-race/race-ability-flags.h"
#include "monster-race/race-brightness-mask.h"
#include "monster/monster-update.h"
#include "mspell/assign-monster-spell.h"
#include "mspell/mspell-result.h"
#include "spell/spells-util.h"
#include "system/angband-system.h"
#include "system/floor/floor-info.h"
#include "system/grid-type-definition.h"
#include "system/monrace/monrace-definition.h"
#include "system/monster-entity.h"
#include "system/player-type-definition.h"
#include "system/redrawing-flags-updater.h"
#include "target/projection-path-calculator.h"
#include "world/world.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace presentation {

namespace {

//! 命令の実行中（`-more-` などで入力待ちへ入り直したとき、二重に実行しない）。
bool s_running = false;
//! 命令ファイルを消すことも名前を変えることもできなかった。繰り返し実行しないよう、以後は何もしない。
bool s_disabled = false;

//! 行動を止めている間、敵の `energy_need`（次の行動までに必要なエネルギー）に置く値。毎ターン 100 ほど減るだけなので、数千ターン持つ。
constexpr ACTION_ENERGY HOLD_ENERGY = 32000;
//! 本来の `energy_need` は数百以内。これより大きければ、置いた値がまだ残っているとみなす（別の敵が同じ番号に入った・書き換えられた、の見分け）。
constexpr ACTION_ENERGY HELD_THRESHOLD = 2000;

//! 止めている敵 1 体の記録。
struct HeldMonster {
    MonraceId race{}; //!< 止めたときの種族。同じ番号に別の敵が入ったことを見分ける
    ACTION_ENERGY original_energy = 0; //!< 止める前の `energy_need`。解除で戻す
};

//! 止めている敵（m_idx → 記録）。
std::map<MONSTER_IDX, HeldMonster> s_held;
//! `hold all on` の間は真。新しく現れた敵も止める。
bool s_hold_all = false;
//! `hold all on` の間に、個別に `off` された敵。
std::set<MONSTER_IDX> s_hold_all_except;

void report(const char *verdict, const std::string &text)
{
    std::fprintf(stderr, "[test-monster-act] %s %s\n", verdict, text.c_str());
    std::fflush(stderr);
}

std::vector<std::string> split_words(const std::string &line)
{
    std::vector<std::string> words;
    std::string cur;
    for (const char c : line) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) {
                words.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }

    if (!cur.empty()) {
        words.push_back(cur);
    }

    return words;
}

bool read_file(const char *path, std::string &body)
{
    std::FILE *const fp = std::fopen(path, "rb");
    if (fp == nullptr) {
        return false;
    }

    char buf[1024];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
        body.append(buf, n);
    }

    std::fclose(fp);
    return true;
}

//! 消す。消せなければ名前を変える。どちらもできなければ false。
bool remove_or_rename(const char *path)
{
    if (std::remove(path) == 0) {
        return true;
    }

    for (int i = 0; i < 100; ++i) {
        const std::string renamed = std::string(path) + ".done" + std::to_string(i);
        if (std::rename(path, renamed.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

bool is_number(const std::string &s)
{
    if (s.empty()) {
        return false;
    }

    for (const char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    return true;
}

/*!
 * @brief 対象の敵を決める。
 * @param token `nearest` か m_idx の数字。空なら `nearest`
 * @param why 決められないときの理由
 * @return m_idx。決められなければ 0
 * @details `nearest` は、プレイヤーに見えている（`ml`）敵対的なモンスターのうち、原作の距離でいちばん近いもの。
 */
MONSTER_IDX find_monster(PlayerType *player_ptr, const std::string &token, std::string &why)
{
    const auto &floor = *player_ptr->current_floor_ptr;
    if (!token.empty() && token != "nearest") {
        if (!is_number(token) || token.size() > 5) {
            why = "bad-target " + token;
            return 0;
        }

        const auto idx = std::atoi(token.c_str());
        if (idx <= 0 || idx >= floor.m_max || idx >= static_cast<int>(floor.m_list.size()) || !floor.m_list[idx].is_valid() || floor.m_list[idx].hp <= 0) {
            why = "no-such-monster " + token;
            return 0;
        }

        return static_cast<MONSTER_IDX>(idx);
    }

    MONSTER_IDX best = 0;
    auto best_dist = 0;
    const auto player_pos = player_ptr->get_position();
    for (MONSTER_IDX i = 1; i < floor.m_max && i < static_cast<MONSTER_IDX>(floor.m_list.size()); ++i) {
        const auto &monster = floor.m_list[i];
        if (!monster.is_valid() || monster.hp <= 0 || !monster.ml || !monster.is_hostile()) {
            continue;
        }

        const auto dist = Grid::calc_distance(player_pos, monster.get_position());
        if (best == 0 || dist < best_dist) {
            best = i;
            best_dist = dist;
        }
    }

    if (best == 0) {
        why = "no-visible-monster";
    }

    return best;
}

//! 魔法 1 つ。
bool do_cast(PlayerType *player_ptr, const std::vector<std::string> &words, std::string &why)
{
    if (words.size() < 2 || words.size() > 3) {
        why = "usage: cast <spell> [nearest|<m_idx>]";
        return false;
    }

    const auto found = r_info_ability_flags.find(words[1]);
    if (found == r_info_ability_flags.end()) {
        why = "unknown-spell " + words[1];
        return false;
    }

    const auto m_idx = find_monster(player_ptr, words.size() == 3 ? words[2] : std::string(), why);
    if (m_idx == 0) {
        return false;
    }

    const auto &floor = *player_ptr->current_floor_ptr;
    const auto &monster = floor.m_list[m_idx];
    const auto monster_pos = monster.get_position();
    const auto player_pos = player_ptr->get_position();
    if (Grid::calc_distance(player_pos, monster_pos) > AngbandSystem::get_instance().get_max_range()) {
        why = "out-of-range m_idx=" + std::to_string(m_idx);
        return false;
    }

    if (!projectable(floor, monster_pos, player_pos)) {
        why = "not-projectable m_idx=" + std::to_string(m_idx);
        return false;
    }

    const auto result = monspell_to_player(player_ptr, found->second, player_ptr->y, player_ptr->x, m_idx);
    if (!result.valid) {
        why = "spell-not-usable-on-player " + words[1];
        return false;
    }

    return true;
}

//! 直接攻撃 1 回。
bool do_melee(PlayerType *player_ptr, const std::vector<std::string> &words, std::string &why)
{
    if (words.size() > 2) {
        why = "usage: melee [nearest|<m_idx>]";
        return false;
    }

    const auto m_idx = find_monster(player_ptr, words.size() == 2 ? words[1] : std::string(), why);
    if (m_idx == 0) {
        return false;
    }

    const auto &monster = player_ptr->current_floor_ptr->m_list[m_idx];
    if (Grid::calc_distance(player_ptr->get_position(), monster.get_position()) != 1) {
        why = "not-adjacent m_idx=" + std::to_string(m_idx);
        return false;
    }

    MonsterAttackPlayer(player_ptr, m_idx).make_attack_normal();
    return true;
}

//! 符号つきの整数（`-3`・`+2`・`0`）。桁は 4 桁まで。
bool parse_offset(const std::string &s, int &value)
{
    if (s.empty()) {
        return false;
    }

    const auto digits = (s[0] == '-' || s[0] == '+') ? s.substr(1) : s;
    if (!is_number(digits) || digits.size() > 4) {
        return false;
    }

    value = std::atoi(digits.c_str());
    if (s[0] == '-') {
        value = -value;
    }

    return true;
}

/*!
 * @brief 敵を、プレイヤーの位置から (dx, dy) ずれたマスへ動かす。時間は使わない。
 * @details
 * 立てるかどうかは、原作が敵をテレポートさせるときに使う `cave_monster_teleportable_bold()`
 * （地形が立てる・ほかの敵やプレイヤーがいない・守りのルーンや爆発のルーンが無い・その種族が通れる地形）で決める。
 * 動かしたあとの更新は、`teleport_monster_to()`（`spell-kind/spells-teleport.cpp`）と同じ：
 * マスの `m_idx` の付け替え・`set_position()`・`update_monster(full)`・両方のマスの `lite_spot()`・光る敵なら `MONSTER_LITE`。
 */
bool do_place(PlayerType *player_ptr, const std::vector<std::string> &words, std::string &why)
{
    int dx = 0;
    int dy = 0;
    if (words.size() != 4 || !parse_offset(words[2], dx) || !parse_offset(words[3], dy)) {
        why = "usage: place <nearest|<m_idx>> <dx> <dy>";
        return false;
    }

    const auto m_idx = find_monster(player_ptr, words[1], why);
    if (m_idx == 0) {
        return false;
    }

    auto &floor = *player_ptr->current_floor_ptr;
    const auto player_pos = player_ptr->get_position();
    const Pos2D dest(player_pos.y + dy, player_pos.x + dx);
    if (!floor.contains(dest, FloorBoundary::OUTER_WALL_EXCLUSIVE)) {
        why = "out-of-map";
        return false;
    }

    auto &monster = floor.m_list[m_idx];
    const auto orig = monster.get_position();
    if (orig == dest) {
        return true; // すでにそこにいる
    }

    if (player_ptr->is_located_at(dest)) {
        why = "player-is-there";
        return false;
    }

    if (floor.get_grid(dest).has_monster()) {
        why = "occupied-by-monster";
        return false;
    }

    if (!cave_monster_teleportable_bold(player_ptr, m_idx, dest.y, dest.x, TELEPORT_SPONTANEOUS)) {
        why = "cannot-stand-there";
        return false;
    }

    floor.get_grid(orig).m_idx = 0;
    floor.get_grid(dest).m_idx = m_idx;
    monster.set_position(dest);
    update_monster(player_ptr, m_idx, true);
    lite_spot(player_ptr, orig);
    lite_spot(player_ptr, dest);
    if (monster.get_monrace().brightness_flags.has_any_of(ld_mask)) {
        RedrawingFlagsUpdater::get_instance().set_flag(StatusRecalculatingFlag::MONSTER_LITE);
    }

    return true;
}

//! m_idx の敵が、止めた記録のとおり、まだ同じ敵として残っているか。
bool is_still_same_monster(const FloorType &floor, MONSTER_IDX m_idx, const HeldMonster &held)
{
    if (m_idx <= 0 || m_idx >= floor.m_max || m_idx >= static_cast<MONSTER_IDX>(floor.m_list.size())) {
        return false;
    }

    const auto &monster = floor.m_list[m_idx];
    return monster.is_valid() && monster.hp > 0 && monster.get_monrace_id() == held.race;
}

/*!
 * @brief 止めている敵の `energy_need` を、番が来ない値に置き直す。命令待ちのたびに呼ぶ。
 * @details
 * 原作の敵の番（`sweep_monster_process()`）は、毎ターン `energy_need` から速度ぶんを引き、0 以下になった敵だけ行動させる。
 * ここで大きな値に置き直しておけば、次の命令待ちまでの間、その敵は動かない・魔法を撃たない・殴らない。
 * 眠らせるのとは違い、敵の状態（睡眠・恐怖など）は変わらない。副作用は、止めている間 `energy_need` が書き換わっていること
 * （この間にセーブすると値が残る。解除で止める前の値へ戻す）。
 * 敵が死んだ・階が替わって同じ番号に別の敵が入った・置いた値が書き換えられたときは、記録から外す（値は戻さない）。
 */
void refresh_hold(PlayerType *player_ptr)
{
    auto &floor = *player_ptr->current_floor_ptr;
    for (auto it = s_held.begin(); it != s_held.end();) {
        if (!is_still_same_monster(floor, it->first, it->second) || floor.m_list[it->first].energy_need < HELD_THRESHOLD) {
            it = s_held.erase(it);
            continue;
        }

        floor.m_list[it->first].energy_need = HOLD_ENERGY;
        ++it;
    }

    for (auto it = s_hold_all_except.begin(); it != s_hold_all_except.end();) {
        const auto idx = *it;
        const auto alive = idx > 0 && idx < floor.m_max && idx < static_cast<MONSTER_IDX>(floor.m_list.size()) && floor.m_list[idx].is_valid();
        it = alive ? std::next(it) : s_hold_all_except.erase(it);
    }

    if (!s_hold_all) {
        return;
    }

    for (MONSTER_IDX i = 1; i < floor.m_max && i < static_cast<MONSTER_IDX>(floor.m_list.size()); ++i) {
        auto &monster = floor.m_list[i];
        if (!monster.is_valid() || monster.hp <= 0 || s_held.count(i) != 0 || s_hold_all_except.count(i) != 0) {
            continue;
        }

        s_held[i] = { monster.get_monrace_id(), monster.energy_need };
        monster.energy_need = HOLD_ENERGY;
    }
}

//! 1 体の行動を止める。
void hold_on(FloorType &floor, MONSTER_IDX m_idx)
{
    s_hold_all_except.erase(m_idx);
    if (s_held.count(m_idx) != 0) {
        return;
    }

    auto &monster = floor.m_list[m_idx];
    s_held[m_idx] = { monster.get_monrace_id(), monster.energy_need };
    monster.energy_need = HOLD_ENERGY;
}

//! 1 体の止めを解く（記録が残っていて置いた値がそのままなら、止める前の値へ戻す）。
void hold_off(FloorType &floor, MONSTER_IDX m_idx)
{
    const auto found = s_held.find(m_idx);
    if (found == s_held.end()) {
        return;
    }

    if (is_still_same_monster(floor, m_idx, found->second) && floor.m_list[m_idx].energy_need >= HELD_THRESHOLD) {
        floor.m_list[m_idx].energy_need = found->second.original_energy;
    }

    s_held.erase(found);
}

//! `hold <nearest|m_idx|all> on|off`。
bool do_hold(PlayerType *player_ptr, const std::vector<std::string> &words, std::string &why)
{
    if (words.size() != 3 || (words[2] != "on" && words[2] != "off")) {
        why = "usage: hold <nearest|<m_idx>|all> on|off";
        return false;
    }

    auto &floor = *player_ptr->current_floor_ptr;
    const auto on = words[2] == "on";
    if (words[1] == "all") {
        s_hold_all_except.clear();
        if (on) {
            s_hold_all = true;
            refresh_hold(player_ptr);
        } else {
            s_hold_all = false;
            while (!s_held.empty()) {
                hold_off(floor, s_held.begin()->first);
            }
        }

        return true;
    }

    const auto m_idx = find_monster(player_ptr, words[1], why);
    if (m_idx == 0) {
        return false;
    }

    if (on) {
        hold_on(floor, m_idx);
    } else {
        hold_off(floor, m_idx);
        if (s_hold_all) {
            s_hold_all_except.insert(m_idx);
        }
    }

    return true;
}

//! 1 行を実行する。成功なら true。
bool run_line(PlayerType *player_ptr, const std::string &line)
{
    const auto words = split_words(line);
    std::string why;
    auto done = false;
    if (words[0] == "cast" || words[0] == "melee") {
        if (AngbandWorld::get_instance().timewalk_m_idx != 0) {
            report("fail", "time-stopped: " + line);
            return false;
        }

        done = words[0] == "cast" ? do_cast(player_ptr, words, why) : do_melee(player_ptr, words, why);
    } else if (words[0] == "place") {
        done = do_place(player_ptr, words, why);
    } else if (words[0] == "hold") {
        done = do_hold(player_ptr, words, why);
    } else {
        why = "unknown-command " + words[0];
    }

    if (!done) {
        report("fail", why + ": " + line);
        return false;
    }

    handle_stuff(player_ptr);
    report("ok", line);
    return true;
}

//! 実行してよい時点か（コマンド待ち）。
bool is_waiting_for_command(PlayerType *player_ptr)
{
    const auto &world = AngbandWorld::get_instance();
    if (!world.character_generated || world.character_icky_depth > 0) {
        return false;
    }

    return inkey_flag && player_ptr->playing && !player_ptr->is_dead && !player_ptr->leaving && (player_ptr->current_floor_ptr != nullptr);
}

} // namespace

void poll_test_monster_act(PlayerType *player_ptr)
{
    static const char *const path = std::getenv("HD2D_TEST_MONSTER_ACT");
    if (path == nullptr || path[0] == '\0' || player_ptr == nullptr || s_running || s_disabled) {
        return;
    }

    if (!is_waiting_for_command(player_ptr)) {
        return;
    }

    // 止めている敵の行動の順番を待つ値を置き直す（命令ファイルの有無に関わらず、命令待ちのたびに）
    if (!s_held.empty() || s_hold_all) {
        refresh_hold(player_ptr);
    }

    std::string body;
    if (!read_file(path, body) || body.empty()) {
        return; // まだ置かれていない（空のファイルは書きかけとみなして待つ）
    }

    s_running = true;
    if (!remove_or_rename(path)) {
        s_disabled = true;
        s_running = false;
        report("fail", "cannot-remove-command-file");
        return;
    }

    // 実行する行を取り出す（空行と `#` で始まる行は読み飛ばす）
    std::vector<std::string> lines;
    std::string line;
    for (const char c : body + "\n") {
        if (c != '\n') {
            line += c;
            continue;
        }

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }

        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] != '#') {
            lines.push_back(line.substr(first));
        }

        line.clear();
    }

    // 上から順に、ゲームの時間を進めずに全部実行する。途中で失敗したら、残りは実行せず skip を出す
    const auto saved_inkey_flag = inkey_flag;
    inkey_flag = false;
    auto failed = false;
    for (const auto &each : lines) {
        if (failed) {
            report("skip", each);
            continue;
        }

        failed = !run_line(player_ptr, each);
    }

    inkey_flag = saved_inkey_flag;
    s_running = false;
}

} // namespace presentation
