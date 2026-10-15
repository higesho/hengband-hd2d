/*
 * UmoriaCore の入口とプロトコル。
 *
 * M1: 24×80 の端末の字をそのまま `menu_term_lines` で送り、文字画面として遊べるようにする。
 * 立体の地図（状態からフレームを組む）は M2。
 */

#include "um_host.h"
#include "um_frame.h"
#include "um_lang.h"
#include "um_recall_ja.h"

#include "headers.h"
#ifndef _WIN32
// ui.h renames open/fopen to the tilde-expanding topen/tfopen (defined in um_io.cpp) for the upstream
// files. The adapter does not need that, and the macros would also rename std::fopen and fstream::open.
#undef open
#undef fopen
#endif

#include "bridge/input_event_adapter.h"
#include "bridge/native_inventory_menu.h"
#include "bridge/native_modern.h"
#include "bridge/native_save_picker.h"
#include "bridge/player_identity.h"
#include "bridge/rogue_borrowed_tiles.h"
#include "bridge/rogue_sub_panels.h"
#include "frame/frame_codec.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"
#include "portable/legacy_os.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

int um_game_main(int argc, char *argv[]);

#if defined(HENGBAND_ANDROID)
/*
 * Android ではコアが UI と同じプロセスのスレッドなので、原作の `exit(0)`（game.cpp の 2 か所。
 * セーブして終えるときと死んだとき）がそのままだとアプリごと終わる。原作の .cpp は
 * `exit=um_game_exit` で組み（android/hd2d/src/main/cpp/CMakeLists.txt）、ここで例外にして
 * `run_protocol` の `um_game_main` の呼び出しまで戻す。Rogue 系の `rl_game_exit` と同じ役目。
 * bionic の `exit` の宣言は noexcept ではないので、名前を差し替えた宣言のまま例外を投げられる。
 */
struct UmGameExit {
    int code;
};
extern "C" [[noreturn]] void um_game_exit(int code) { throw UmGameExit{ code }; }
#endif

namespace {

constexpr const char *kCoreName = "Umoria";
constexpr const char *kCoreVersion = "5.7.15+3bf8abc";

presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;
std::mutex g_input_mutex;
std::deque<int> g_keys;
/*
 * 矢印・テンキー・パッドの十字（`move`）の印（2026-10-07）。`kDirectionMark + '1'〜'9'` を積んでおき、
 * **コアが取り出す瞬間に**キー配置を見て字へ直す（`resolve_keyset_key`）。積む時点で直すと、
 * 続けて押した 2 つ目の矢印を「まだコマンドを待っていない」と読んでしまう。
 */
constexpr int kDirectionMark = 0x100;
/*!
 * ローグライク: パッドの「走る」（`^` `.`）・「掘る」（`^` `,`）の後。次の方向を Shift 付きの字（走る。'S'）か
 * Ctrl 付きの字（掘る。'C'）にする。0 なら待っていない。g_input_mutex で守る
 */
int g_direction_pending = 0;
//! ローグライク: コマンドの `^`（原作の「Control-」の問い）の後。次の方向の印を英字にする（掘る）。g_input_mutex で守る
bool g_caret_pending = false;
//! パッドの表を送ったときのキー配置（`use_roguelike_keys`）。変わったら送り直す。
bool g_tables_roguelike = false;
std::atomic<bool> g_ui_gone{ false };
std::atomic<bool> g_quit_requested{ false };
std::atomic<bool> g_exit_sent{ false };
std::uint64_t g_frame_id = 0;
std::string g_last_frame;
//! モダン UI の窓（画面側の ui_state.modern_ui が真のとき）。g_modern_mutex で守る。
std::mutex g_modern_mutex;
presentation::NativeModernMenu g_native_menu;
/*
 * 画面側の「持ち物一覧」タブ（`inventory_open`）に答える部品（Rogue 系と同じ）。対応していないと
 * タブを選んだ後に返事が来ず、画面が止まった（気づいたこと 2026-10-03「持ち物一覧を開こうとすると…操作できなくなった」）。
 */
presentation::NativeInventoryMenu g_inventory_menu;
//! 持ち物一覧の行の命令の名札とキー列を引く表（隠しグループ `@inventory-action` を含む）。
presentation::PadCommandsMessage g_item_table;
//! いま出ているアイテムを選ぶ問いの品の行（取り込みのたびに作り直す）。
std::vector<presentation::NativeItemRow> g_item_prompt_rows;
//! タイトルとセーブの一覧（原作の前にアダプタが出す）の選択肢。空なら出していない。
std::vector<MenuChoice> g_pregame_choices;
//! その見出し（日本語のこともあるので端末の字ではなく窓の小見出しで運ぶ）。
std::string g_pregame_title;
std::mutex g_pregame_mutex;
//! 画面側の言語（ui_state で届く）。届く前は空。
std::string g_ui_lang;
std::atomic<bool> g_ui_state_seen{ false };
//! キャラ作成の選ぶ画面（種族・性別・職業・能力値）を出している間は真。パッドの取消を捨てる。
std::atomic<bool> g_creation_no_cancel{ false };
/*!
 * 保存済みの（死んでいない）キャラを読み込んでいる間は真（`run_protocol` が原作の起動の前に立てる）。
 * 読み込みが終わってキャラができるか、キャラ作成の画面が出たら降ろす。
 * 原作はこの間も誕生と同じ「キャラクター作成」の窓・phase=birth の画面にしてしまうため、
 * 読み込みのコマはタイトルの種類にする（`startup_look`）。
 */
std::atomic<bool> g_loading_save{ false };
//! 読み込み中のうち、スプラッシュ（続行のキーを待つ画面）のコマだけ真。`host_present` が窓をタイトルの種類にする。
std::atomic<bool> g_loading_splash{ false };
//! 次の取り込みで、前と同じ画でもフレームを送る（捨てた操作に返事をするため）。
std::atomic<bool> g_force_frame{ false };
std::atomic<bool> g_refresh_requested{ false };
//! 画面が選んだサブパネルの種類（`ui_state.sub_panel_kinds`）。g_modern_mutex で守る。
std::array<int, kSubPanelCount> g_sub_panel_kinds = { -1, -1, -1, -1, -1, -1, -1 };

bool send_message(const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[umoria] send failed: %s\n", err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    return true;
}

void enqueue(const std::vector<int> &keys)
{
    std::lock_guard<std::mutex> lock(g_input_mutex);
    for (const int key : keys) {
        if ((key > 0 && key <= 255) || (key > kDirectionMark + '0' && key <= kDirectionMark + '9')) g_keys.push_back(key);
    }
}

/*!
 * 方向はテンキーの数字（原作の既定のキー配列）。**ローグライクのキー配置では、コマンドを待っている所の
 * 数字は「回数の指定」になる**（原作 `game_run.cpp` の `getCommandRepeatCount`）ので、そのまま積まずに
 * 印（`kDirectionMark`）にして、取り出すときに字へ直す（2026-10-07）。方向を聞く問いでは数字のまま効く。
 */
char direction_key(int dx, int dy)
{
    static constexpr char table[3][3] = { { '7', '8', '9' }, { '4', '5', '6' }, { '1', '2', '3' } };
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
    return table[dy + 1][dx + 1];
}

presentation::PadCommandsMessage build_pad_commands(bool with_item_actions);

/*
 * 日本語のプレイヤー名（「日本語のプレイヤー名」。Rogue 系の rl_main.cpp と同じ形）。
 * 原作の名前（py.misc.name、23 字まで）とセーブは ASCII のまま持つ。日本語の表示名は共通の仕組み（player_identity.h）が
 * セーブごとに別のファイルへ持ち、原作へは ASCII の仮名（英数字だけの名前ならそのまま、ほかは "P"＋ハッシュの 8 字）を渡す。
 * 画面に出す字の中の仮名は、送る前に表示名へ置き換える（apply_display_name）。
 */
constexpr const char *kIdentityCoreKey = "umoria";
std::string g_identity_save_id; //!< いまのセーブのファイル名（表示名の鍵）

//! 原作の名前の入力（ui.cpp の getCharacterName / changeCharacterName。人物表の 2 行目・15 桁目の欄）か。
bool name_input_active()
{
    return umoria::term_text_input_active() && umoria::term_text_input_row() == 2 && umoria::term_text_input_col() == 15;
}

//! 原作へ渡す ASCII の名前。英数字だけならそのまま（23 字まで）、ほかは仮名（8 字）。
std::size_t identity_core_max(const std::string &display)
{
    for (const unsigned char c : display) {
        if (c < 0x20 || c >= 0x7f) return 8;
    }
    return 23;
}

void rewrite_name_field(std::vector<int> &keys)
{
    const auto id = presentation::player_identity_snapshot();
    keys.push_back(CTRL_KEY('D')); // 欄を空にする（um_io.cpp の getStringInput）
    const std::string &source = id.edit_touched ? id.edit_name_utf8 : id.display_name_utf8;
    if (source.empty()) return;
    for (const unsigned char c : presentation::player_identity_core_name(source, identity_core_max(source))) keys.push_back(c);
}

bool handle_player_name_event(const presentation::InputEventWire &event, std::vector<int> &keys)
{
    if (!name_input_active()) return false;
    auto id = presentation::player_identity_snapshot();
    if (!id.edit_active) {
        presentation::player_identity_begin_edit(kIdentityCoreKey, g_identity_save_id, id.display_name_utf8, id.core_name_ascii);
    }
    const auto touch = [] {
        const auto now = presentation::player_identity_snapshot();
        if (now.edit_touched) return;
        const std::string shown = now.display_name_utf8;
        presentation::player_identity_clear_edit();
        presentation::player_identity_append_edit(shown);
    };
    if (event.e == "text") {
        touch();
        presentation::player_identity_append_edit(event.text);
        rewrite_name_field(keys);
        return true;
    }
    if (event.e == "key" && event.name == "backspace") {
        touch();
        presentation::player_identity_backspace_edit();
        rewrite_name_field(keys);
        return true;
    }
    if (event.e == "key" && event.ctrl) return true; // 欄の中の移動など。字ではない
    if (event.e == "key" && !event.chr.empty()) {
        touch();
        presentation::player_identity_append_edit(event.chr);
        rewrite_name_field(keys);
        return true;
    }
    if (event.e == "cancel") {
        presentation::player_identity_cancel_edit();
        keys.push_back(ESCAPE);
        return true;
    }
    if (event.e == "confirm") {
        const auto now = presentation::player_identity_snapshot();
        const std::string next = now.edit_touched ? now.edit_name_utf8 : now.display_name_utf8;
        if (next.empty()) {
            // 空のまま確定: 原作が既定の名前（X）にする道に任せる（空の名前を仮名にすると意味の無い字が入った）。
            presentation::player_identity_cancel_edit();
            keys.push_back(CTRL_KEY('D'));
            keys.push_back('\r');
            return true;
        }
        const auto committed = presentation::player_identity_commit_edit(identity_core_max(next), g_identity_save_id);
        keys.push_back(CTRL_KEY('D'));
        for (const unsigned char c : committed.core_name_ascii) keys.push_back(c);
        keys.push_back('\r');
        return true;
    }
    return false;
}

//! 画面に出す字の中の仮名を、日本語の表示名に置き換える（言語によらない）。
void replace_alias(std::string &text, const std::string &alias, const std::string &shown)
{
    for (std::size_t pos = text.find(alias); pos != std::string::npos; pos = text.find(alias, pos + shown.size())) {
        text.replace(pos, alias.size(), shown);
    }
}

void apply_display_name(GameFrame &frame)
{
    const auto id = presentation::player_identity_snapshot();
    const bool editing = id.edit_active && id.edit_touched && !id.edit_name_utf8.empty();
    const std::string shown = editing ? id.edit_name_utf8 : id.display_name_utf8;
    const std::string alias = editing ? presentation::player_identity_core_name(shown, identity_core_max(shown)) : id.core_name_ascii;
    if (shown.empty() || alias.empty() || shown == alias) return;
    for (TermMirrorLine &line : frame.menu_term_lines) replace_alias(line.text_utf8, alias, shown);
    for (SubPanelLine &line : frame.status_col_lines) replace_alias(line.text_utf8, alias, shown);
    for (MessageEvent &message : frame.messages) replace_alias(message.text_utf8, alias, shown);
    for (MenuChoice &choice : frame.menu_choices) {
        replace_alias(choice.label_utf8, alias, shown);
        replace_alias(choice.description_utf8, alias, shown);
    }
    replace_alias(frame.prompt.text_utf8, alias, shown);
    replace_alias(frame.hud.name, alias, shown);
}

void receive_loop(presentation::TransportHandle input)
{
    while (!g_ui_gone.load()) {
        std::string payload, err;
        if (presentation::read_message(input, payload, err) != presentation::TransportResult::Ok) {
            g_ui_gone.store(true);
            return;
        }
        const auto type = presentation::peek_message_type(payload);
        if (type == "keys") {
            presentation::KeysMessage message;
            if (presentation::decode_keys(payload, message, err)) enqueue(message.keys);
        } else if (type == "input_event") {
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) continue;
            /*
             * 受け付けてからキーを積むまでを、取り込み（host_present）と同じロックの中で行う
             * （間に取り込みが入ると「受け付けた、でも中身は古い」画が送られる。rl_main.cpp と同じ）。
             */
            std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
            std::vector<int> keys;
            for (const auto &event : message.events) {
                /*
                 * キャラ作成の選ぶ画面では、パッドの取消（B）を受けない（気づいたこと 2026-10-03
                 * 「職業選択でキャンセルで戻れなかった」）。原作には前の画面へ戻る道が無く、種族・性別・職業では
                 * ESC を鳴らして撥ね、**能力値の画面では ESC が「決める」**なので、取消のつもりで押すと決まってしまう。
                 * キーボードの ESC（`keys`）は従来どおり原作へ渡す。
                 */
                if (g_creation_no_cancel.load()
                    && ((event.e == "cancel") || ((event.e == "ui_action") && (event.action == "cancel")))) {
                    /*
                     * モダン UI の操作は部品（g_native_menu）に渡して「受け取った」印を返させる。渡さずに捨てると
                     * 画面側が「応答待ちです」のまま止まった。出てきたキー（ESC）だけを捨てる。
                     */
                    std::vector<int> ignored;
                    if (event.e == "ui_action") (void) g_native_menu.input(event, ignored);
                    g_force_frame.store(true);
                    g_refresh_requested.store(true);
                    continue;
                }
                if (event.e == "ui_action") {
                    std::vector<int> translated;
                    if (g_inventory_menu.input(event, translated) || g_native_menu.input(event, translated)) {
                        if (translated.empty()) g_refresh_requested.store(true);
                        keys.insert(keys.end(), translated.begin(), translated.end());
                        continue;
                    }
                }
                if (handle_player_name_event(event, keys)) continue;
                if (event.e == "move") {
                    if (umoria::term_text_input_active()) continue;
                    const char key = direction_key(event.dx, event.dy);
                    if (key != 0 && key != '5') keys.push_back(kDirectionMark + key);
                    else if (key != 0) keys.push_back(static_cast<unsigned char>(key));
                    continue;
                }
                presentation::InputEventsMessage one;
                one.events.push_back(event);
                presentation::append_input_event_keys(one, keys);
            }
            enqueue(keys);
        } else if (type == "ui_state") {
            presentation::UiStateMessage state;
            if (presentation::decode_ui_state(payload, state, err)) {
                std::lock_guard<std::mutex> lock(g_modern_mutex);
                g_native_menu.set_enabled(state.modern_ui);
                g_native_menu.set_language(state.lang.empty() ? "ja" : state.lang);
                {
                    // コマンドメニューの名札は握手の直後（言語が届く前）に英語で送ってある。言語が変わったら訳して送り直す。
                    const bool was_ja = umoria::lang_ja();
                    umoria::set_language(state.lang);
                    if (was_ja != umoria::lang_ja()) {
                        (void) send_message(presentation::encode_pad_commands(build_pad_commands(false)));
                        // サブパネルの種類の名前も新しい言語で送り直す（番号は同じ）。
                        (void) send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(!umoria::lang_ja())));
                        g_item_table = build_pad_commands(true); // g_modern_mutex は上で握っている（host_present が読む）
                        g_force_frame.store(true);
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(g_pregame_mutex);
                    g_ui_lang = state.lang;
                }
                g_ui_state_seen.store(true);
                g_inventory_menu.configure(state.modern_ui, state.lang.empty() ? "ja" : state.lang, "rogue");
                if (state.has_sub_panel_kinds) {
                    for (int i = 0; i < kSubPanelCount; ++i) g_sub_panel_kinds[static_cast<std::size_t>(i)] = state.sub_panel_kinds[i];
                }
                g_refresh_requested.store(true);
            }
        } else if (type == "quit_request") {
            g_quit_requested.store(true);
        }
    }
}

std::string frame_compare_key(std::string payload)
{
    const std::string marker = "\"frame_id\":";
    const auto pos = payload.find(marker);
    if (pos == std::string::npos) return payload;
    auto end = pos + marker.size();
    while (end < payload.size() && payload[end] >= '0' && payload[end] <= '9') ++end;
    payload.erase(pos + marker.size(), end - (pos + marker.size()));
    return payload;
}

/*
 * `menu_term_lines` の行ごとの「元の桁 → 送る字のバイト位置」（M5）。選択肢は端末の桁で範囲を作るので、
 * 訳した行を送るときは localize_frame() がこれで範囲を直す。英語のときは桁とバイトが同じ。
 */
std::vector<std::vector<int>> g_line_byte_at_col;

//! 端末の 24 行をそのまま `menu_term_lines` へ写す（メニュー・店・ヘルプ・キャラ作成の画）。
void fill_term_lines(GameFrame &frame)
{
    const int cursor_row = umoria::term_cursor_row();
    g_line_byte_at_col.clear();
    for (int row = 0; row < umoria::kTermRows; ++row) {
        umoria::TermLineText text = umoria::term_line(row, 0, umoria::kTermCols);
        g_line_byte_at_col.push_back(std::move(text.byte_at_col));
        TermMirrorLine line;
        line.text_utf8 = std::move(text.utf8);
        line.color = 1;
        line.source_row = row;
        if (row == cursor_row) {
            frame.menu_term_curs_row = static_cast<int>(frame.menu_term_lines.size());
            frame.menu_term_curs_col = umoria::term_cursor_col();
        }
        frame.menu_term_lines.push_back(std::move(line));
    }
}

/*
 * M3: 問いを選択肢にする（パッドだけで答えられるように）。
 *
 * - アイテムを選ぶ問い（`inventoryGetInputForItemId`。原作の ui_inventory.cpp）は 1 行目の字で見分ける。
 *   原作の文言は 2 通り:「(Inven: a-e, 0-9, * to see, / for Equip, or ESC) <問い>」と
 *   「(Items a-e, 0-9, * for inventory list, ESC to exit) <問い>」。字の範囲だけを読み、
 *   選択肢の名前は状態（`py.inventory`）から引く。原作の mask（その問いに合う種類）は外から見えないので、
 *   範囲の全部を並べる（合わない物は原作が鳴らして撥ねる）。
 * - [y/n] とその他の 1 キーの問いは、端末が今どの関数で待っているか（`umoria::term_question()`）で見分ける。
 */
void add_menu_choice(GameFrame &frame, int key, std::string label)
{
    MenuChoice choice{};
    choice.line_index = 0;
    choice.key = key;
    choice.label_utf8 = std::move(label);
    frame.menu_choices.push_back(std::move(choice));
}

std::string item_label(const Inventory_t &item)
{
    obj_desc_t text = { '\0' };
    itemDescription(text, item, true);
    std::string label(text);
    while (!label.empty() && (label.back() == '.' || label.back() == ' ')) label.pop_back();
    return label;
}

bool fill_item_question(GameFrame &frame, const std::string &top)
{
    const bool inven = top.rfind("(Inven: ", 0) == 0;
    bool equip = top.rfind("(Equip: ", 0) == 0;
    const bool items = top.rfind("(Items ", 0) == 0;
    /*
     * 3 つめの形（`buildCommandHeading`。持ち物・装備の画面から w / t / d などを打ったとき）:
     * 「(b-e, * to list, 0-9, space to break, ESC to exit) Wear/Wield which one?」。
     * 装備から選ぶか持ち物から選ぶかは命令で決まる（`selectItemCommands`）: Take off と Throw off は装備、
     * Wear/Wield と Drop は持ち物。画面の種類では決まらない（地図から t を打つと画面はまだ持ち物のまま）。
     */
    const bool heading = (top.size() > 5) && (top[0] == '(') && (top[2] == '-') && (top.find("which one?") != std::string::npos);
    if (heading) {
        equip = (top.find(") Take off which one?") != std::string::npos) || (top.find(") Throw off which one?") != std::string::npos);
    }
    if (!inven && !equip && !items && !heading) return false;
    const std::size_t at = top.find('-');
    const std::size_t close = top.find(") ");
    if (at == std::string::npos || at < 1 || at + 1 >= top.size() || close == std::string::npos) return false;
    const int from = top[at - 1] - 'a';
    const int to = top[at + 1] - 'a';
    if (from < 0 || to < from || to > 25) return false;

    frame.menu_open = true;
    frame.pre_game_menu = false;
    frame.menu_over_map = true;
    frame.menu_popup = true;
    frame.awaiting_command = false;
    frame.menu_choices.clear();
    frame.menu_term_lines.clear();
    frame.menu_term_curs_row = -1;
    frame.menu_term_curs_col = -1;
    TermMirrorLine line;
    line.source_row = 0;
    line.color = 1;
    // 問いの字は組み立てた行なので、画面の字の訳（term_line）を通らない。ここで訳す（英語のまま出ていた。2026-10-04）。
    line.text_utf8 = umoria::tr(top.substr(close + 2));
    frame.menu_term_lines.push_back(std::move(line));

    if (equip) {
        // 原作の装備の一覧（displayEquipment）と同じ並び: 着けている物だけを Wield から順に a, b, …
        int letter = 0;
        for (int slot = PlayerEquipment::Wield; slot < PLAYER_INVENTORY_SIZE; ++slot) {
            const Inventory_t &item = py.inventory[slot];
            if (item.category_id == TV_NOTHING) continue;
            if (letter >= from && letter <= to) add_menu_choice(frame, 'a' + letter, item_label(item));
            ++letter;
        }
    } else {
        for (int i = from; i <= to && i < PlayerEquipment::Wield; ++i) {
            const Inventory_t &item = py.inventory[i];
            if (item.category_id == TV_NOTHING) continue;
            add_menu_choice(frame, 'a' + i, item_label(item));
        }
    }
    const bool can_switch = (top.find("/ for Equip") != std::string::npos) || (top.find("/ for Inven") != std::string::npos);
    if (top.find("/ for Equip") != std::string::npos) add_menu_choice(frame, '/', "Equipment...");
    if (top.find("/ for Inven") != std::string::npos) add_menu_choice(frame, '/', "Inventory...");
    add_menu_choice(frame, ESCAPE, "Cancel");
    /*
     * 同じ品を部品へも行の形で渡す。持ち物一覧から命令を選んだとき、部品がこの窓の中から
     * その品を自動で選び直す（字・持ち物か装備か・番号・名前・個数で突き合わせる）。
     */
    g_item_prompt_rows.clear();
    for (auto &row : umoria::owned_rows()) {
        if (row.selection != (equip ? 1 : 0) || row.slot < from || row.slot > to) continue;
        row.actions.clear();
        g_item_prompt_rows.push_back({ std::move(row), 0, -1, can_switch ? 3 : (equip ? 2 : 1) });
    }
    return true;
}

std::string term_row(int row)
{
    std::string text;
    for (int col = 0; col < umoria::kTermCols; ++col) text.push_back(umoria::term_char(row, col));
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

//! 店の画面か（4 行目に「Asking Price」の見出し）。原作 store.cpp の displayStoreHeader の並び。
bool store_screen() { return term_row(4).find("Asking Price") != std::string::npos; }

/*
 * 店で買う品を選ぶ問い（「(Items a-f, ESC to exit) Which item are you interested in?」）。
 * 形は持ち物の問いと同じなので、店の画面のときは**画面に出ている店の品**（5 行目からの「a) 名前  値段」）を並べる。
 */
bool fill_store_item_question(GameFrame &frame, const std::string &top)
{
    if (!store_screen() || top.find("Which item are you interested in?") == std::string::npos) return false;
    // 店の中なので地図の上の小窓にはしない（立体の地図が透けて見えた）。店の画面と同じ窓で聞く。
    frame.menu_open = true;
    frame.pre_game_menu = false;
    frame.menu_over_map = false;
    frame.menu_popup = false;
    frame.awaiting_command = false;
    frame.menu_choices.clear();
    frame.menu_term_lines.clear();
    frame.menu_term_curs_row = -1;
    frame.menu_term_curs_col = -1;
    TermMirrorLine line;
    line.source_row = 0;
    line.color = 1;
    line.text_utf8 = umoria::tr("Which item are you interested in?");
    frame.menu_term_lines.push_back(std::move(line));
    for (int row = 5; row < 17; ++row) {
        const std::string text = term_row(row);
        if (text.size() < 4 || text[1] != ')' || text[0] < 'a' || text[0] > 'z') continue;
        std::string name = text.substr(3);
        // 値切りを済ませた品は値段の後ろに「[Fixed]」が付く（store.cpp）。値段と読み違えて「55  ([Fixed] gp)」になっていた（2026-10-04）。
        if (name.size() > 8 && name.compare(name.size() - 8, 8, " [Fixed]") == 0) name.resize(name.size() - 8);
        std::string price;
        const std::size_t last_space = name.find_last_of(' ');
        if (last_space != std::string::npos) {
            price = name.substr(last_space + 1);
            name = name.substr(0, last_space);
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
        add_menu_choice(frame, text[0], price.empty() ? name : (name + "  (" + price + " gp)"));
    }
    add_menu_choice(frame, ESCAPE, "Cancel");
    return true;
}

/*
 * 店の値段の交渉（store.cpp の storeGetHaggle。「What do you offer? 」「What price do you ask? 」）。
 * 数字は原作の文字入力の欄で受けるので、画面側の数値入力（上下で増減・左右で桁・決定で Enter）を重ねる。
 * 画面側は値を変えるたびに `set_number` を送り、アダプタが Ctrl-D（欄を空にする。um_io.cpp）と数字に直す。
 * 上限は言い値の 2 倍か 9999 の大きいほう（言い値を超える申し出は原作が断る）。
 */
bool fill_haggle_question(GameFrame &frame, const std::string &top)
{
    // 1 行目は末尾の空白を落として渡ってくるので、問いの文言も空白なしで比べる。
    const bool buy = top.rfind("What do you offer?", 0) == 0;
    const bool sell = top.rfind("What price do you ask?", 0) == 0;
    /*
     * 休む回数（player.cpp。「Rest for how long? 」の 5 桁の欄）も同じ数値入力にする。
     * 「回復するまで」（`*`）は数値で言えないので、コマンドメニューの「Rest as needed」（R * Enter）で送る。
     */
    if (top.rfind("Rest for how long?", 0) == 0 && umoria::term_text_input_active()) {
        const std::string typed = top.size() > 19 ? top.substr(19) : std::string();
        frame.numeric.active = true;
        frame.numeric.prompt_utf8 = "Rest for how long?";
        frame.numeric.min = 0;
        frame.numeric.max = 9999;
        frame.numeric.value = std::clamp(std::atoi(typed.c_str()), 0, 9999);
        frame.numeric.digits = 4;
        frame.numeric.text_len = static_cast<int>(typed.size());
        frame.numeric.line_index = -1;
        return true;
    }
    if ((!buy && !sell) || !umoria::term_text_input_active()) return false;
    int asking = 0;
    const std::string row1 = term_row(1);
    const std::size_t colon = row1.find(':');
    if (colon != std::string::npos) asking = std::atoi(row1.c_str() + colon + 1);
    std::string typed = top.size() > static_cast<std::size_t>(umoria::term_text_input_col())
        ? top.substr(static_cast<std::size_t>(umoria::term_text_input_col()))
        : std::string();
    const int maximum = std::max(9999, asking * 2);
    frame.numeric.active = true;
    frame.numeric.prompt_utf8 = std::string(buy ? "What do you offer?" : "What price do you ask?") + " (" + row1 + ")";
    frame.numeric.min = 0;
    frame.numeric.max = maximum;
    frame.numeric.value = std::clamp(std::atoi(typed.c_str()), 0, maximum);
    frame.numeric.digits = static_cast<int>(std::to_string(maximum).size());
    frame.numeric.text_len = static_cast<int>(typed.size());
    frame.numeric.line_index = -1;
    return true;
}

/*
 * 店の最初の画面（「You may:」の下の命令）を選択肢にする。字の位置へ枠を重ねるので、
 * 選択肢は端末の行（`menu_term_lines` の添字＝行）と字の範囲を持つ。
 */
void fill_store_commands(GameFrame &frame)
{
    if (!store_screen() || term_row(20).rfind("You may:", 0) != 0) return;
    // 「-more-」の待ちでは出さない（決定で ESC が飛ぶと、残りのメッセージと一緒に店まで出てしまう）。
    if (term_row(0).find("-more-") != std::string::npos) return;
    struct Command {
        const char *token;
        int key;
        const char *label;
    };
    static const Command kCommands[] = {
        { "p) ", 'p', "Purchase an item" },
        { "s) ", 's', "Sell an item" },
        { "b) ", 'b', "Browse store's inventory" },
        { "i/e/t/w/x) ", 'i', "Inventory list" },
        { "ESC) ", ESCAPE, "Exit from building" },
    };
    frame.menu_choices.clear();
    for (const Command &command : kCommands) {
        for (int row = 21; row < umoria::kTermRows; ++row) {
            const std::string text = term_row(row);
            const std::size_t at = text.find(command.token);
            if (at == std::string::npos) continue;
            std::size_t end = text.find("  ", at + std::strlen(command.token));
            if (end == std::string::npos) end = text.size();
            MenuChoice choice{};
            choice.line_index = row;
            choice.span_begin = static_cast<int>(at);
            choice.span_len = static_cast<int>(end - at);
            choice.key_begin = static_cast<int>(at);
            choice.key_len = static_cast<int>(std::strlen(command.token)) - 2;
            choice.key = command.key;
            choice.label_utf8 = command.label;
            frame.menu_choices.push_back(std::move(choice));
            break;
        }
    }
}

//! 端末の 1 行から「x) 名前」の並び（2 つ以上の空白で区切る）を拾い、字の位置に枠を重ねる選択肢にする。
void add_letter_choices(GameFrame &frame, int row)
{
    const std::string text = term_row(row);
    std::size_t at = 0;
    while (at + 3 < text.size()) {
        if (text[at] >= 'a' && text[at] <= 'z' && text[at + 1] == ')' && text[at + 2] == ' '
            && (at == 0 || text[at - 1] == ' ')) {
            std::size_t end = text.find("  ", at + 3);
            if (end == std::string::npos) end = text.size();
            MenuChoice choice{};
            choice.line_index = row;
            choice.span_begin = static_cast<int>(at);
            choice.span_len = static_cast<int>(end - at);
            choice.key_begin = static_cast<int>(at);
            choice.key_len = 1;
            choice.key = text[at];
            choice.label_utf8 = text.substr(at + 3, end - at - 3);
            frame.menu_choices.push_back(std::move(choice));
            at = end;
        } else {
            ++at;
        }
    }
}

//! 行の中の決まった文言に枠を重ねる選択肢を 1 つ足す。
void add_phrase_choice(GameFrame &frame, int row, const char *phrase, int key, const char *label)
{
    const std::string text = term_row(row);
    const std::size_t at = text.find(phrase);
    MenuChoice choice{};
    choice.line_index = row;
    if (at != std::string::npos) {
        choice.span_begin = static_cast<int>(at);
        choice.span_len = static_cast<int>(std::strlen(phrase));
    }
    choice.key = key;
    choice.label_utf8 = label;
    frame.menu_choices.push_back(std::move(choice));
}

//! 読み込み中のコマの見せ方（`startup_look`）。
enum class StartupLook {
    Normal,  //!< 読み込み中ではない（または誕生の画面が出た）。今までどおり
    Opening, //!< 読み込みの経過を出すだけで、操作の要らないコマ。窓を出さず、タイトルの背景の状態にする
    Splash,  //!< スプラッシュ（続行のキーを待つ）。窓は出すが、種類はタイトル（`start`）にする
};

/*!
 * @brief 読み込み中のコマの見せ方を決める。
 * @param row20 端末の 20 行目の字（キャラ作成の選ぶ画面の見分けに使う。`fill_creation_choices` と同じ印）
 * @param creating 読み込みを抜けて誕生の画面に入ったと判った（呼び出し側が旗を降ろす）なら真が返る
 */
StartupLook startup_look(bool loading, bool in_game, bool game_over, bool text_input, const std::string &row20, bool has_choices, bool &creating)
{
    creating = false;
    if (!loading || in_game || game_over) return StartupLook::Normal;
    // 読み込みに失敗すると原作はそのままキャラ作成へ進む。選ぶ画面か名前の入力が出たら誕生として扱う。
    if (text_input || row20.rfind("  Choose a ", 0) == 0 || row20.rfind("  Hit space to re-roll", 0) == 0) {
        creating = true;
        return StartupLook::Normal;
    }
    return has_choices ? StartupLook::Splash : StartupLook::Opening;
}

/*
 * キャラ作成の画面（原作 character.cpp）。どれも 20〜23 行目の字で見分ける。
 * 名前の入力は文字入力（`text_input_active`）なので、画面側の文字入力の仕組みに任せる。
 */
void fill_creation_choices(GameFrame &frame)
{
    // 名前などの文字入力の最中は選択肢を出さない（墓石の記録の問いだけは下で別に扱う）。
    if (umoria::term_text_input_active() && term_row(22).rfind("Character record?", 0) != 0) return;
    const std::string row20 = term_row(20);
    g_creation_no_cancel.store((row20.rfind("  Choose a ", 0) == 0) || (row20.rfind("  Hit space to re-roll", 0) == 0));
    if (row20.rfind("  Choose a ", 0) == 0) {
        frame.menu_choices.clear();
        add_letter_choices(frame, 21);
        add_letter_choices(frame, 22);
    } else if (row20.rfind("  Hit space to re-roll or ESC to accept", 0) == 0) {
        frame.menu_choices.clear();
        add_phrase_choice(frame, 20, "ESC to accept", ESCAPE, "Accept these characteristics");
        add_phrase_choice(frame, 20, "Hit space to re-roll", ' ', "Re-roll");
    } else if (term_row(22).rfind("Character record?", 0) == 0) {
        /*
         * 墓石の画面（game_death.cpp）。原作は文字入力で「ESC で残さない・Enter で画面に出す・字ならファイル名」。
         * パッドでは前の 2 つを選択肢にする。文字入力の印は下ろす（立てたままだと画面側が文字入力の窓を開き、
         * 選択肢が出ない）。ファイル名はキーボードで打てば従来どおり入る。
         */
        frame.text_input_active = false;
        frame.menu_choices.clear();
        add_phrase_choice(frame, 23, "ESC to abort", ESCAPE, "Do not keep a record");
        add_phrase_choice(frame, 23, "return to print on screen", 0x0D, "Show the record on screen");
    } else if (term_row(23).find("press any key to continue") != std::string::npos) {
        // キャラ作成の最後の人物表と、得点表（死んだ後）。
        frame.menu_choices.clear();
        add_phrase_choice(frame, 23, "press any key to continue", ' ', "Continue");
    }
}

/*
 * 呪文・祈りを選ぶ問い（M3）。
 *
 * - 覚える（`Learn which spell?`。player.cpp）と、唱える問いで `*` を押した後は、画面に一覧
 *   （「a) Magic Missile   1  1  21%」）が出ているので、その字から選択肢を作る。
 * - 唱える・祈る（`(Spells a-d, *=List, <ESCAPE>=exit) Cast which spell?`。spells.cpp の spellGetId）は
 *   範囲しか出ない。持っている本の中から、原作の castSpellGetId と同じ手順（本の呪文のうち覚えていて
 *   段階の足りるもの。字は本の最初の呪文からの番号）で範囲が一致する本を探し、名前と消費と失敗率を並べる。
 */
bool spell_list_on_screen()
{
    for (int row = 1; row < 4; ++row) {
        const std::string text = term_row(row);
        if (text.find("Name") != std::string::npos && text.find("Mana") != std::string::npos) return true;
    }
    return false;
}

//! 呪文の選択肢の添え書き（唱える問いと、画面に一覧が出ている問いで同じ形にする）。
std::string spell_detail(bool ja, int level, int mana, int fail)
{
    char detail[96];
    if (ja) {
        std::snprintf(detail, sizeof(detail), reinterpret_cast<const char *>(u8"  （Lv %d・マナ %d・失敗率 %d%%）"), level, mana, fail);
    } else {
        std::snprintf(detail, sizeof(detail), "  (Lv %d, %d mana, %d%% fail)", level, mana, fail);
    }
    return detail;
}

/*
 * 画面に出ている呪文の一覧（原作 displaySpellsList。覚える問いと、唱える問いで `*` を押した後）の行を選択肢にする。
 * 行は「  a) 名前（30 桁）Lv マナ 失敗率%」。名前のあとの空白で切ると Lv・マナ・失敗率が落ちるので、桁で読む。
 * 一覧は見出し行（`Name` と `Lv Mana Fail`）の `Name` の 5 桁手前から始まる。端末の全行を写すと左の地図や状態の列が窓の本文に
 * 混ざるので、呼び出し側は見出し 1 行だけを窓に残す（修-45）。読めた行が 1 つも無ければ false。
 */
bool add_spell_list_choices(GameFrame &frame)
{
    const bool ja = umoria::lang_ja();
    int base = -1;
    for (int row = 1; row < 4 && base < 0; ++row) {
        const std::string text = term_row(row);
        const std::size_t name = text.find("Name");
        if (name != std::string::npos && text.find("Mana") != std::string::npos && name >= 5) base = static_cast<int>(name) - 5;
    }
    if (base < 0) return false;
    bool any = false;
    for (int row = 2; row < umoria::kTermRows; ++row) {
        const std::string text = term_row(row);
        const std::size_t at = static_cast<std::size_t>(base) + 2;
        if (text.size() < at + 33 || text[at] < 'a' || text[at] > 'z' || text[at + 1] != ')' || text[at + 2] != ' ') continue;
        std::string name = text.substr(at + 3, 30);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        int level = 0;
        int mana = 0;
        int fail = 0;
        if (name.empty() || std::sscanf(text.c_str() + at + 33, "%d %d %d%%", &level, &mana, &fail) != 3) continue;
        add_menu_choice(frame, text[at], (ja ? umoria::tr(name) : name) + spell_detail(ja, level, mana, fail));
        any = true;
    }
    return any;
}

bool fill_spell_question(GameFrame &frame, const std::string &top)
{
    const bool asks = (top.find("which spell?") != std::string::npos) || (top.find("which prayer?") != std::string::npos);
    if (!asks) return false;
    frame.menu_choices.clear();
    if (spell_list_on_screen()) {
        frame.menu_open = true;
        frame.menu_over_map = true;
        frame.menu_popup = false;
        frame.awaiting_command = false;
        fill_term_lines(frame);
        frame.menu_choices.clear();
        if (add_spell_list_choices(frame)) {
            /*
             * 行の字は選択肢の名札（名前と Lv・マナ・失敗率）に載せたので、窓の本文は問いの 1 行だけにする
             * （端末の全行を写すと、左の地図や状態の列の断片が本文に混ざった）。
             */
            frame.menu_popup = true;
            frame.pre_game_menu = false;
            TermMirrorLine heading = std::move(frame.menu_term_lines.front());
            frame.menu_term_lines.clear();
            frame.menu_term_lines.push_back(std::move(heading));
            frame.menu_term_curs_row = -1;
            frame.menu_term_curs_col = -1;
        } else {
            for (int row = 1; row < umoria::kTermRows; ++row) add_letter_choices(frame, row);
        }
        MenuChoice cancel{};
        cancel.line_index = 0;
        cancel.key = ESCAPE;
        cancel.label_utf8 = "Cancel";
        frame.menu_choices.push_back(std::move(cancel));
        return true;
    }
    if (top.rfind("(Spells ", 0) != 0 || top.size() < 11) return false;
    const int want_first = top[8] - 'a';
    const int want_last = top[10] - 'a';
    const bool mage = classes[py.misc.class_id].class_to_use_mage_spells == config::spells::SPELL_TYPE_MAGE;
    const int offset = mage ? config::spells::NAME_OFFSET_SPELLS : config::spells::NAME_OFFSET_PRAYERS;
    const uint8_t book_type = mage ? TV_MAGIC_BOOK : TV_PRAYER_BOOK;
    Spell_t *spells = magic_spells[py.misc.class_id - 1];
    for (int i = 0; i < PlayerEquipment::Wield; ++i) {
        const Inventory_t &book = py.inventory[i];
        if (book.category_id != book_type) continue;
        uint32_t flags = book.flags;
        const int first_spell = getAndClearFirstBit(flags);
        flags = book.flags & py.flags.spells_learnt;
        std::vector<int> ids;
        while (flags != 0u) {
            const int pos = getAndClearFirstBit(flags);
            if (spells[pos].level_required <= py.misc.level) ids.push_back(pos);
        }
        if (ids.empty() || ids.front() - first_spell != want_first || ids.back() - first_spell != want_last) continue;

        frame.menu_open = true;
        frame.pre_game_menu = false;
        frame.menu_over_map = true;
        frame.menu_popup = true;
        frame.awaiting_command = false;
        frame.menu_term_lines.clear();
        frame.menu_term_curs_row = -1;
        frame.menu_term_curs_col = -1;
        TermMirrorLine line;
        line.source_row = 0;
        line.color = 1;
        /*
         * 見出しは訳の層（localize_frame）が字を訳した後にここで足すので、自分で訳す。英語のままだと日本語の画面で
         * 唱える窓の見出しが「Cast which spell?」と出ていた（遊ぶ人に届いた報告の調査 2026-10-06）。添え書きも同じ。
         */
        const bool ja = umoria::lang_ja();
        const std::string question = top.substr(top.find(") ") + 2);
        line.text_utf8 = ja ? umoria::tr(question) : question;
        frame.menu_term_lines.push_back(std::move(line));
        for (const int id : ids) {
            const std::string detail = spell_detail(ja, spells[id].level_required, spells[id].mana_required, spellChanceOfSuccess(id));
            const std::string name = spell_names[id + offset];
            add_menu_choice(frame, 'a' + id - first_spell, (ja ? umoria::tr(name) : name) + detail);
        }
        add_menu_choice(frame, '*', "Show the list");
        add_menu_choice(frame, ESCAPE, "Cancel");
        return true;
    }
    return false;
}

/*
 * 持ち物・装備の一覧の画面（`i` `e`。ui_inventory.cpp の uiCommandDisplayInventory）。
 * 一覧の下に「e/i/t/w/x/d/?/ESC:」と出してキーを待つ。選択肢が無いと画面側は決定で Enter を送るだけで、
 * 原作は Enter を受けないので止まって見えた（気づいたこと 2026-10-03「持ち物一覧を開こうとしたら…動かなくなった」）。
 */
bool fill_inventory_commands(GameFrame &frame)
{
    int row = -1;
    for (int r = 0; r < umoria::kTermRows; ++r) {
        if (term_row(r).find("e/i/t/w/x/d/?/ESC:") != std::string::npos) {
            row = r;
            break;
        }
    }
    if (row < 0) return false;
    struct Command {
        int key;
        const char *label;
    };
    static const Command kCommands[] = {
        { ESCAPE, "Close the list" },
        { 'w', "Wear or wield an item" },
        { 't', "Take off an item" },
        { 'd', "Drop an item" },
        { 'x', "Exchange weapons" },
        { 'i', "Inventory list" },
        { 'e', "Equipment list" },
        { '?', "Help" },
    };
    frame.menu_choices.clear();
    for (const Command &command : kCommands) {
        MenuChoice choice{};
        choice.line_index = row;
        choice.key = command.key;
        choice.label_utf8 = command.label;
        frame.menu_choices.push_back(std::move(choice));
    }
    return true;
}

/*
 * 記号を 1 字聞く問い（「Enter character to be identified :」「Which type of creature do you wish exterminated?」）。
 * パッドでは字を打てないので、記号の一覧（原作の文字の説明と同じ字）を選択肢の小窓にする（2026-10-04）。
 */
bool fill_symbol_question(GameFrame &frame, const std::string &top)
{
    if (umoria::term_question() != umoria::Question::Tile) return false;
    frame.menu_open = true;
    frame.pre_game_menu = false;
    frame.menu_over_map = true;
    frame.menu_popup = true;
    frame.awaiting_command = false;
    frame.prompt = {};
    frame.menu_choices.clear();
    frame.menu_term_lines.clear();
    frame.menu_term_curs_row = -1;
    frame.menu_term_curs_col = -1;
    TermMirrorLine line;
    line.source_row = 0;
    line.color = 1;
    line.text_utf8 = umoria::tr(top);
    frame.menu_term_lines.push_back(std::move(line));
    for (const auto &[symbol, description] : umoria::symbol_descriptions()) add_menu_choice(frame, symbol, description);
    add_menu_choice(frame, ESCAPE, "Cancel");
    return true;
}

/*
 * オプションの画面（game.cpp の setGameOptions。「y/n で設定、<return> か - でカーソル移動」）。キーの操作しか無く、
 * パッドでは設定を変えられなかったので、今の項目を はい／いいえ にする・上下へ移る・終わる、の選択肢を付ける（2026-10-04）。
 */
bool fill_options_choices(GameFrame &frame)
{
    if (term_row(0).find("y/n to set options") == std::string::npos) return false;
    std::string name = term_row(umoria::term_cursor_row());
    const std::size_t colon = name.rfind(':');
    if (colon != std::string::npos) name.resize(colon);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    frame.menu_choices.clear();
    if (!name.empty()) {
        add_menu_choice(frame, 'y', "Set to yes: " + name);
        add_menu_choice(frame, 'n', "Set to no: " + name);
    }
    add_menu_choice(frame, '-', "Previous option");
    add_menu_choice(frame, '\r', "Next option");
    add_menu_choice(frame, ESCAPE, "Done");
    return true;
}

/*
 * [y/n] の問い。画面下の帯（軽い問い＝lightweight）ではなく、ほかのコアと同じ確認の窓（NativeModernMenu が
 * stage=confirm にする）として出す（決めたこと 2026-10-12「確認の窓にする」。帯では指で「はい」「いいえ」を押せなかった）。
 * 答えは窓の操作（answer）から g_native_menu.input() を通って 'y'・'n' のキーに直り、取消は ESC（原作は「いいえ」扱い）。
 */
void fill_confirm_question(GameFrame &frame, const std::string &top)
{
    frame.prompt = {};
    frame.prompt.text_utf8 = top;
    frame.prompt.line_index = -1;
    PromptChoice yes;
    yes.label_utf8 = "Yes";
    yes.key = 'y';
    PromptChoice no;
    no.label_utf8 = "No";
    no.key = 'n';
    frame.prompt.choices.push_back(std::move(yes));
    frame.prompt.choices.push_back(std::move(no));
}

/*
 * M2: キャラができてからは地図を状態から組んで立体で描かせる。端末の字を送るのは、
 * 地図が覆われている間（持ち物・店・ヘルプ・人物表など）とキャラ作成・死亡の画だけ。
 */
GameFrame capture_frame()
{
    g_item_prompt_rows.clear();
    g_line_byte_at_col.clear();
    GameFrame frame;
    frame.floor.place_name_utf8 = kCoreName;
    frame.hud.name = kCoreName;
    frame.text_input_active = umoria::term_text_input_active();
    const bool in_game = game.character_generated && !game.character_is_dead;
    if (in_game) umoria::fill_world_frame(frame);
    if (in_game && umoria::map_screen_active()) {
        frame.menu_open = false;
        /*
         * ローグライクの「走る」「掘る」（パッドとコマンドメニューの 2 キー `^` `.`／`^` `,`）の後は、原作の
         * コマンド待ち（getKeyInput）の中のまま「Which direction?」を出して方向のキーを待つ（resolve_keyset_key）。
         * このときは umoria_command_input_active が真のままで問い（Question）の印も立たないため、下の帯の判定に
         * 掛からず、方向を聞いているのに画面側にはコマンド待ちとしか見えなかった（調-14・F-umoria-10）。
         * 標準の配置の方向の問いと同じく、コマンド待ちではなく問い（帯）として渡す。
         */
        bool direction_pending = false;
        {
            std::lock_guard<std::mutex> lock(g_input_mutex);
            direction_pending = g_direction_pending != 0;
        }
        frame.awaiting_command = umoria_command_input_active && !direction_pending;
        // 1 行目に出ている問い（方向は？・-more- など）は、メッセージとは別に画面側へ見せる。
        std::string top;
        for (int col = 0; col < umoria::kTermCols; ++col) top.push_back(umoria::term_char(0, col));
        while (!top.empty() && top.back() == ' ') top.pop_back();
        /*
         * 帯（prompt）に出すのは**キーを待っている問いだけ**（方向・[y/n]・1 キーの問い・数値入力）。
         * ただのメッセージまで帯に入れていたので、戦闘のたびに帯の窓が出ては消え、ちらついた
         *。メッセージはサブパネル（fill_messages）に出る。
         */
        const bool asking = umoria::term_question() != umoria::Question::None || umoria::term_text_input_active();
        // アイテムと呪文を選ぶ問いは原作の関数が直接キーを読む（「問いを待っている」の印が立たない）ので、先に見分ける。
        if (!top.empty() && !umoria_command_input_active
            && (fill_symbol_question(frame, top) || fill_spell_question(frame, top) || fill_item_question(frame, top))) {
            return frame;
        }
        if (!top.empty() && !umoria_command_input_active && asking) {
            frame.prompt.text_utf8 = top;
            frame.prompt.line_index = -1;
            if (umoria::term_question() == umoria::Question::Confirm) {
                fill_confirm_question(frame, top);
            } else {
                fill_haggle_question(frame, top); // 休む回数（地図の上で聞かれる）
            }
        }
        if (direction_pending) {
            frame.prompt.text_utf8 = top.empty() ? std::string("Which direction?") : top;
            frame.prompt.line_index = -1;
        }
        // 検査用（tools/umoria/umoria_driver.py）。端末の字も載せる。画面側は字があると立体を描かないので、検査の外では使わない。
        static const bool mirror = std::getenv("UMORIA_TEST_TERM_MIRROR") != nullptr;
        if (mirror) fill_term_lines(frame);
        return frame;
    }
    // 持ち物・装備の画面の上でアイテムを聞かれているときも、選択肢の小窓にする（M3）。
    if (in_game) {
        std::string top;
        for (int col = 0; col < umoria::kTermCols; ++col) top.push_back(umoria::term_char(0, col));
        while (!top.empty() && top.back() == ' ') top.pop_back();
        if (umoria::term_question() == umoria::Question::Confirm) {
            fill_confirm_question(frame, top);
        } else if (fill_symbol_question(frame, top) || fill_store_item_question(frame, top) || fill_spell_question(frame, top)
            || fill_item_question(frame, top)) {
            return frame;
        }
    }
    frame.menu_open = true;
    frame.pre_game_menu = !in_game;
    //! 死んだ（引退を含む）あとの墓石・得点表・名前の入力。題と phase を誕生と分ける（修-10）。
    frame.game_over = game.character_generated && game.character_is_dead;
    frame.menu_over_map = in_game;
    fill_term_lines(frame);
    std::vector<MenuChoice> pregame;
    std::string pregame_title;
    {
        std::lock_guard<std::mutex> lock(g_pregame_mutex);
        pregame = g_pregame_choices;
        pregame_title = g_pregame_title;
    }
    if (!in_game && !pregame.empty()) {
        frame.menu_choices = std::move(pregame);
        frame.menu_box_header_utf8 = pregame_title;
        g_creation_no_cancel.store(false);
    } else if (!in_game) {
        fill_creation_choices(frame);
    } else {
        g_creation_no_cancel.store(false);
    }
    {
        /*
         * セーブの読み込み中（スプラッシュ・「記憶を復元中」など）は誕生ではない。以前は窓の題が「キャラクター作成」、
         * phase が birth になっていた。経過だけのコマは窓を出さずタイトルの状態にし（phase=title）、
         * スプラッシュは窓の種類を `host_present` でタイトルにする。
         */
        bool creating = false;
        const StartupLook look = startup_look(g_loading_save.load(), in_game, frame.game_over, frame.text_input_active,
            term_row(20), !frame.menu_choices.empty(), creating);
        if (creating) g_loading_save.store(false);
        g_loading_splash.store(look == StartupLook::Splash);
        if (look == StartupLook::Opening) {
            frame.menu_open = false;
            frame.pre_game_menu = false;
            frame.menu_term_lines.clear();
            frame.title_screen = true;
            return frame;
        }
    }
    if (in_game) {
        fill_haggle_question(frame, term_row(0));
        if (!frame.numeric.active && !umoria::term_text_input_active()) {
            if (!fill_options_choices(frame) && !fill_inventory_commands(frame)) fill_store_commands(frame);
        }
    }
    return frame;
}

/*
 * セーブの隣の説明（`<セーブ>.info`）。セーブの一覧に「名前・種族・職業・レベル・深さ」を出すため、
 * 遊んでいる間に中身が変わったら書き直す（Umoria のセーブは符号化されていて、外から読むより確実）。
 */
void write_save_info()
{
    if (!game.character_generated || config::files::save_game.empty()) return;
    char text[160];
    const std::string depth = dg.current_level == 0 ? std::string("town") : std::to_string(dg.current_level * 50) + " ft";
    // セーブの一覧に出す名前は日本語の表示名（あれば）。
    const auto id = presentation::player_identity_snapshot();
    const std::string name = id.display_name_utf8.empty() ? std::string(py.misc.name) : id.display_name_utf8;
    std::snprintf(text, sizeof(text), "%s, %s %s, level %d, %s", name.c_str(), character_races[py.misc.race_id].name,
        classes[py.misc.class_id].title, py.misc.level, depth.c_str());
    static std::string last;
    if (last == text) return;
    last = text;
    if (FILE *fp = std::fopen((config::files::save_game + ".info").c_str(), "wb"); fp != nullptr) {
        std::fputs(text, fp);
        std::fclose(fp);
    }
}

/*
 * 日本語のとき、組み上がったフレームの字を訳す。
 * 字を読んで問いを見分ける所（capture_frame の中）は英語のまま終わっているので、ここで訳しても読み取りは壊れない。
 */
void localize_frame(GameFrame &frame)
{
    if (!umoria::lang_ja()) return;
    const auto to_byte = [](int line, int col) {
        if (line < 0 || line >= static_cast<int>(g_line_byte_at_col.size())) return col;
        const std::vector<int> &map = g_line_byte_at_col[static_cast<std::size_t>(line)];
        if (map.empty()) return col;
        if (col < 0) return col;
        return map[static_cast<std::size_t>(std::min<int>(col, static_cast<int>(map.size()) - 1))];
    };
    /*
     * 範囲の字が、訳した 1 文（1 かたまり）の途中にあるか。かたまりの全マスは同じ先頭のバイトへ写るので、
     * 英語の字が 1 桁ごとに 1 バイト進むはずの所で 2 桁目が動かなければ、そのかたまりが訳されている。
     * （「Hit space to re-roll or ESC to accept」のような 1 文の中の言い回しは、そのままでは範囲の長さが 1 になって
     * 訳した字の 1 バイト目だけを指し、窓の本文が U+FFFD に化けた。修-42）
     */
    const auto inside_translated_chunk = [&](int line, int col, int len) {
        if (len < 2 || line < 0 || line >= static_cast<int>(g_line_byte_at_col.size())) return false;
        const std::vector<int> &map = g_line_byte_at_col[static_cast<std::size_t>(line)];
        if (col < 0 || col + 1 >= static_cast<int>(map.size())) return false;
        // 全角の字（2 桁が同じバイトへ写る）を訳したかたまりと取り違えない。言い回しは英字で始まる。
        const auto is_alpha = [&](int c) {
            const unsigned char ch = static_cast<unsigned char>(umoria::term_char(line, c));
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        };
        return is_alpha(col) && is_alpha(col + 1) && map[static_cast<std::size_t>(col)] == map[static_cast<std::size_t>(col + 1)];
    };
    for (MenuChoice &choice : frame.menu_choices) {
        if (choice.span_len > 0 && inside_translated_chunk(choice.line_index, choice.span_begin, choice.span_len)) {
            /*
             * 言い回しの訳（screens.ja.txt）を、訳した行の中から探して範囲にする。見つからなければ、そのかたまり全体にする
             * （かたまりの先頭のバイトから、次に別のバイトへ写る桁の手前まで。字の途中では切らない）。
             */
            const std::vector<int> &map = g_line_byte_at_col[static_cast<std::size_t>(choice.line_index)];
            const std::string phrase = term_row(choice.line_index).substr(
                static_cast<std::size_t>(choice.span_begin), static_cast<std::size_t>(choice.span_len));
            const std::string phrase_ja = umoria::tr(phrase);
            const std::string *line_text = nullptr;
            if (choice.line_index < static_cast<int>(frame.menu_term_lines.size())) {
                line_text = &frame.menu_term_lines[static_cast<std::size_t>(choice.line_index)].text_utf8;
            }
            const int chunk_begin = map[static_cast<std::size_t>(choice.span_begin)];
            const std::size_t found = (line_text != nullptr && phrase_ja != phrase) ? line_text->find(phrase_ja) : std::string::npos;
            if (found != std::string::npos) {
                choice.span_begin = static_cast<int>(found);
                choice.span_len = static_cast<int>(phrase_ja.size());
            } else {
                int chunk_end = line_text != nullptr ? static_cast<int>(line_text->size()) : chunk_begin + 1;
                for (const int at : map) {
                    if (at > chunk_begin) chunk_end = std::min(chunk_end, at);
                }
                // かたまりの後ろに足した埋めの空白は範囲に入れない。
                while (line_text != nullptr && chunk_end > chunk_begin + 1 && (*line_text)[static_cast<std::size_t>(chunk_end) - 1] == ' ') --chunk_end;
                choice.span_begin = chunk_begin;
                choice.span_len = std::max(chunk_end - chunk_begin, 1);
            }
        } else if (choice.span_len > 0) {
            const int begin = to_byte(choice.line_index, choice.span_begin);
            const int end = to_byte(choice.line_index, choice.span_begin + choice.span_len);
            choice.span_begin = begin;
            choice.span_len = std::max(end - begin, 1);
        }
        if (choice.key_len > 0) {
            const int begin = to_byte(choice.line_index, choice.key_begin);
            const int end = to_byte(choice.line_index, choice.key_begin + choice.key_len);
            choice.key_begin = begin;
            choice.key_len = std::max(end - begin, 1);
        }
        choice.label_utf8 = umoria::tr(choice.label_utf8);
        choice.description_utf8 = umoria::tr(choice.description_utf8);
    }
    frame.menu_box_header_utf8 = umoria::tr(frame.menu_box_header_utf8);
    frame.numeric.prompt_utf8 = umoria::tr(frame.numeric.prompt_utf8);
    /*
     * 問いの選択肢は本文のバイト位置を持つ（[y/n] の y）。選択肢の無い問いはそのまま訳す。選択肢のある問いは、
     * いちばん前の選択肢より前の字（「Drop all 4 Rations of Food? 」）だけを訳し、選択肢の位置を訳で増えたバイト数だけずらす
     * （選択肢のある問いを訳していなかったので、新しい UI の問いの見出しが英語のままだった。2026-10-04）。
     */
    if (frame.prompt.choices.empty()) {
        frame.prompt.text_utf8 = umoria::tr(frame.prompt.text_utf8);
    } else {
        std::size_t head = frame.prompt.text_utf8.size();
        for (const PromptChoice &choice : frame.prompt.choices) {
            if (choice.len > 0 && choice.begin >= 0) head = std::min(head, static_cast<std::size_t>(choice.begin));
        }
        // 選択肢の前の「[」（「[y/n]」の頭）までを訳さずに残す。選択肢が位置を持たない問いでも、末尾の「[…]」は残す
        // （「[y/n]」ごと引いて当たらず、「Continuing with inventory command? [y/n]」が英語のままだった。2026-10-04）。
        const std::size_t bracket = frame.prompt.text_utf8.rfind('[', head == frame.prompt.text_utf8.size() ? std::string::npos : head);
        if (bracket != std::string::npos && (bracket + 2 >= head || frame.prompt.text_utf8.back() == ']')) head = bracket;
        const std::string before = frame.prompt.text_utf8.substr(0, head);
        const std::string translated = umoria::tr(before);
        if (translated != before) {
            const int shift = static_cast<int>(translated.size()) - static_cast<int>(before.size());
            frame.prompt.text_utf8 = translated + frame.prompt.text_utf8.substr(head);
            for (PromptChoice &choice : frame.prompt.choices) {
                if (choice.len > 0 && choice.begin >= static_cast<int>(head)) choice.begin += shift;
            }
        }
    }
    for (PromptChoice &choice : frame.prompt.choices) choice.label_utf8 = umoria::tr(choice.label_utf8);
}

/*
 * 経歴の文の日本語（「文章の日本語化」）。原作は段（chart）ごとに部品を選んで英語の順につなぎ、
 * 60 桁で 4 行に折り返して py.misc.history に入れる（character.cpp）。折り返した行は訳せないので、ここで部品に分け直し、
 * 変愚の日本語の部品（history.ja.txt）を日本語の段の順に並べてつなぎ、60 桁（全角は 2 桁）で折り返して、元の行ごとに訳として登録する。
 */
std::string collapse_spaces(const std::string &text)
{
    std::string out;
    for (const char c : text) {
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void update_history_translation()
{
    static std::array<std::string, 4> last;
    static bool last_ja = false;
    std::array<std::string, 4> now;
    for (std::size_t i = 0; i < now.size(); ++i) now[i] = py.misc.history[i];
    if (now == last && last_ja == umoria::lang_ja()) return;
    last = now;
    last_ja = umoria::lang_ja();
    static std::vector<std::string> registered;
    for (const std::string &key : registered) umoria::remove_screen_translation(key);
    registered.clear();
    if (!umoria::lang_ja()) return;
    std::string block;
    for (const std::string &line : now) {
        const std::string text = collapse_spaces(line);
        if (text.empty()) continue;
        if (!block.empty()) block.push_back(' ');
        block += text;
    }
    if (block.empty()) return;
    std::vector<std::pair<int, std::string>> parts;
    std::size_t pos = 0;
    int chart = py.misc.race_id * 3 + 1;
    for (int guard = 0; chart >= 1 && guard < 32 && pos < block.size(); ++guard) {
        const Background_t *best = nullptr;
        std::size_t best_len = 0;
        for (const Background_t &bg : character_backgrounds) {
            if (bg.info == nullptr || bg.chart != chart) continue;
            const std::string info = collapse_spaces(bg.info);
            if (info.size() > best_len && block.compare(pos, info.size(), info) == 0) {
                best = &bg;
                best_len = info.size();
            }
        }
        if (best == nullptr) return; // 分け直せない（英語のまま出す）
        std::string ja;
        int ja_chart = 0;
        if (!umoria::history_fragment(chart, best->info, ja, ja_chart)) return;
        parts.emplace_back(ja_chart, ja);
        pos += best_len;
        while (pos < block.size() && block[pos] == ' ') ++pos;
        chart = best->next;
    }
    std::stable_sort(parts.begin(), parts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::string text;
    for (const auto &part : parts) text += part.second;
    // 60 桁で折り返す（全角は 2 桁。UTF-8 の 3 バイトの字を全角とみなす）。
    std::vector<std::string> wrapped(1);
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2));
        const int cols = len >= 3 ? 2 : 1;
        if (width + cols > 60) {
            wrapped.emplace_back();
            width = 0;
        }
        wrapped.back() += text.substr(i, len);
        width += cols;
        i += len;
    }
    // 元の行ごとに登録する。日本語の行が余れば最後の行に続け、足りなければ残りの行は空にする。
    std::vector<std::size_t> rows;
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (!collapse_spaces(now[i]).empty()) rows.push_back(i);
    }
    for (std::size_t k = 0; k < rows.size(); ++k) {
        std::string ja;
        if (k < wrapped.size()) ja = wrapped[k];
        if (k + 1 == rows.size()) {
            for (std::size_t extra = k + 1; extra < wrapped.size(); ++extra) ja += wrapped[extra];
        }
        // 鍵は画面の行の前後の空白だけを外した形（um_lang の tr と同じ。行の中の「.  」の 2 つの空白は残る）。
        const std::string &line = now[rows[k]];
        const std::size_t first = line.find_first_not_of(' ');
        const std::size_t last_char = line.find_last_not_of(' ');
        umoria::set_screen_translation(line.substr(first, last_char - first + 1), ja);
        registered.push_back(line.substr(first, last_char - first + 1));
    }
}

//! 端末の 1 行（右の空白を落とした英語）。
std::string term_row_text(int row)
{
    std::string text;
    for (int col = 0; col < umoria::kTermCols; ++col) text.push_back(umoria::term_char(row, col));
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

//! UTF-8 の字を画面の桁で折り返す（全角は 2 桁。3 バイト以上の字を全角とみなす）。改行で段落を分ける。
std::vector<std::string> wrap_columns(const std::string &text, int width)
{
    std::vector<std::string> lines(1);
    int used = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            lines.emplace_back();
            used = 0;
            ++i;
            continue;
        }
        const std::size_t len = c < 0x80 ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2));
        const int cols = len >= 3 ? 2 : 1;
        if (used + cols > width) {
            lines.emplace_back();
            used = 0;
        }
        lines.back() += text.substr(i, len);
        used += cols;
        i += len;
    }
    return lines;
}

/*
 * 怪物の思い出の日本語（「怪物の思い出」）。原作（recall.cpp の memoryRecall）は 0 行目に
 * 「The 〈名前〉:」を書き、部品をつないで 79 桁で折り返す。折り返した行は訳せないので、0 行目の名前から怪物を見つけ、
 * um_recall_ja.cpp が同じ条件で組んだ日本語を 79 桁で折り返して、元の行ごとに訳として登録する。
 */
void update_recall_translation()
{
    static std::vector<std::string> registered;
    static std::string last_key;
    const std::string head = term_row_text(0);
    int monster_id = -1;
    if (umoria::lang_ja() && head.size() > 5 && head.compare(0, 4, "The ") == 0 && head.back() == ':') {
        const std::string name = head.substr(4, head.size() - 5);
        for (int i = 0; i < MON_MAX_CREATURES; ++i) {
            if (creatures_list[i].name != nullptr && name == creatures_list[i].name) {
                monster_id = i;
                break;
            }
        }
    }
    // 思い出の行（0 行目から空の行の手前まで）。
    std::vector<std::string> rows;
    if (monster_id >= 0) {
        for (int row = 0; row < umoria::kTermRows; ++row) {
            const std::string text = term_row_text(row);
            if (text.empty()) break;
            rows.push_back(text);
        }
    }
    std::string key;
    for (const std::string &row : rows) key += row + "\n";
    if (key == last_key) return;
    last_key = key;
    for (const std::string &old : registered) umoria::remove_screen_translation(old);
    registered.clear();
    if (monster_id < 0 || rows.empty()) return;
    const std::vector<std::string> ja = wrap_columns(umoria::recall_japanese(monster_id), 79);
    for (std::size_t k = 0; k < rows.size(); ++k) {
        std::string line = k < ja.size() ? ja[k] : std::string();
        if (k + 1 == rows.size()) {
            for (std::size_t extra = k + 1; extra < ja.size(); ++extra) line += ja[extra];
        }
        const std::size_t first = rows[k].find_first_not_of(' ');
        const std::string trimmed = rows[k].substr(first);
        umoria::set_screen_translation(trimmed, line);
        registered.push_back(trimmed);
    }
}

void host_present()
{
    write_save_info();
    if (g_out == nullptr || g_ui_gone.load()) return;
    /*
     * キー配置（設定の「Rogue like commands」。セーブの読み込みでも変わる）が変わったら、パッドの表を
     * 送り直す（2026-10-07）。**番号（id）とコマンド文字は変えない**ので画面側の割り当ては動かない。
     */
    if (config::options::use_roguelike_keys != g_tables_roguelike) {
        g_tables_roguelike = config::options::use_roguelike_keys;
        (void) send_message(presentation::encode_pad_commands(build_pad_commands(false)));
        std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
        g_item_table = build_pad_commands(true);
    }
    if (game.character_generated) umoria::set_player_class(classes[py.misc.class_id].title);
    update_history_translation();
    update_recall_translation();
    GameFrame frame = capture_frame();
    localize_frame(frame);
    apply_display_name(frame);
    umoria::write_misses();
    {
        std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
        {
            // 読んでいないキーがあるうちは送らない（古い画を操作の結果と取り違えさせない）。
            std::lock_guard<std::mutex> lock(g_input_mutex);
            if (!g_keys.empty()) return;
        }
        g_native_menu.capture(frame, {}, g_item_prompt_rows);
        // 読み込み中のスプラッシュは、窓の題と種類をタイトルのものにする（誕生の「キャラクター作成」にしない）。
        if (g_loading_splash.load() && !frame.modern.stage.empty()) {
            frame.modern.content_type = "start";
            frame.modern.title = kCoreName;
            // 原作のスプラッシュは取消（Esc）でも先へ進むだけで、タイトルのように終了しない。案内に「取消: 終了」を出させない
            // （決めたこと 2026-10-15「取消は先へ進む」）。取消はそのまま原作へ渡す。
            frame.modern.no_cancel = true;
        }
        g_inventory_menu.capture(frame, [] { return umoria::owned_rows(); }, [] { return g_item_table; });
        // キャラ作成の選ぶ画面は原作に戻る道が無いので、画面側に「取消: 戻る」を出させない。
        if (!frame.modern.stage.empty() && g_creation_no_cancel.load()) frame.modern.no_cancel = true;
        presentation::apply_rogue_sub_panel_kinds(frame, g_sub_panel_kinds);
    }
    // 魔法・投射物の効果は、フレームを送ると決まったこの時点で汲む（上の早期の return で捨てない）。
    if (game.character_generated) umoria::fill_attack_fx(frame, umoria_command_input_active);
    frame.frame_id = g_frame_id + 1;
    const std::string wire = presentation::encode_game_frame(frame);
    const std::string key = frame_compare_key(wire);
    if (key == g_last_frame && !g_force_frame.exchange(false)) return;
    g_last_frame = key;
    ++g_frame_id;
    (void)send_message(wire);
}

//! テンキーの数字 → ローグライクのキー配置の方向の字（小文字）。
char rogue_direction_letter(int digit)
{
    switch (digit) {
    case '1': return 'b';
    case '2': return 'j';
    case '3': return 'n';
    case '4': return 'h';
    case '6': return 'l';
    case '7': return 'y';
    case '8': return 'k';
    case '9': return 'u';
    default: return 0;
    }
}

/*!
 * @brief 取り出したキーを、いまのキー配置に合わせて直す（2026-10-07）。g_input_mutex を握って呼ぶ。
 *
 * @details 原作のローグライクのキー配置（`use_roguelike_keys`）では:
 *   - 方向は `hjklyubn`、走るは Shift＋方向、掘るは Ctrl＋方向。数字は「回数の指定」。
 *   - 方向を聞く問い（`getDirectionWithMemory`）は字も数字も受ける。
 * そこで方向の印は「コマンドを待っている所」だけ字にし、ほかは数字のまま渡す（`y` `n` を
 * 「はい／いいえ」の問いへ渡すと答えになってしまう）。
 *
 * 標準の配置の「走る」（`.` → 方向）と「掘る」（`T` → 方向）には、ローグライクの配置に同じ形が無い。
 * パッドとコマンドメニューのために:
 *   - 走る: `^` `.`、掘る: `^` `,`（どちらも原作では「Type ^ <letter>」で何も起きない並び）を
 *     ここで受け、次の方向を Shift 付きの字（走る）・Ctrl 付きの字（掘る）にする。待つ間は
 *     原作の「Which direction?」を出す（`ask_direction`）。標準の配置と同じく方向を聞く形になる。
 *   - キーボードで打った `^`（原作の「Control-」の問い）の後の矢印は英字にする（`^` → ← で左を掘る）。
 */
int resolve_keyset_key(int key, bool &ask_direction, bool &clear_line)
{
    const bool rogue = config::options::use_roguelike_keys;
    const bool command = umoria_command_input_active;
    const bool mark = key > kDirectionMark;
    const int digit = mark ? key - kDirectionMark : 0;
    if (g_direction_pending != 0) {
        const int kind = g_direction_pending;
        g_direction_pending = 0;
        clear_line = true;
        int letter = mark ? rogue_direction_letter(digit) : rogue_direction_letter(key);
        if (letter == 0 && key >= 'a' && key <= 'z' && std::strchr("hjklyubn", key) != nullptr) letter = key;
        if (letter != 0) return (kind == 'S') ? (letter - 'a' + 'A') : (letter & 0x1f);
        return mark ? 0 : key; // 方向でなければ取り消し（ESC などはそのまま渡す）
    }
    if (g_caret_pending) {
        g_caret_pending = false;
        if (mark) return rogue_direction_letter(digit);
    }
    if (rogue && command && key == '^') {
        if (!g_keys.empty() && (g_keys.front() == '.' || g_keys.front() == ',')) {
            g_direction_pending = (g_keys.front() == '.') ? 'S' : 'C';
            g_keys.pop_front();
            ask_direction = true;
            return 0;
        }
        g_caret_pending = true;
        return key;
    }
    if (mark) return (rogue && command) ? rogue_direction_letter(digit) : digit;
    return key;
}

int host_next_key()
{
    // 画面側だけで変わった状態（モダン UI の切り替えなど）を取り込み直す。
    if (g_refresh_requested.exchange(false)) host_present();
    int key = 0;
    bool ask_direction = false;
    bool clear_line = false;
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (g_keys.empty()) return 0;
        key = g_keys.front();
        g_keys.pop_front();
        /*
         * 履歴（^P）は、原作では 1 回押すと上の 1 行に直前のメッセージだけを出す。一覧（最大 22 行）が出るのは、前のコマンドも ^P
         * のとき、または回数を前置きしたとき（game_run.cpp の calculateMaxMessageCount）。画面の Ctrl+P やコマンドメニューの
         * 「^P」は 1 回しか送らないので、一覧にするため、前のコマンドが ^P でなく回数も無いときは 2 回目の ^P を続けて渡す
         * （修-44。ゲームのルールは変えず、原作の「前のコマンド」の判定をそのまま使う。標準でもローグライクでも ^P は同じ意味）。
         */
        constexpr int kCtrlP = 0x10;
        if (key == kCtrlP && umoria_command_input_active && game.command_count == 0 && game.last_command != kCtrlP) {
            g_keys.push_front(kCtrlP);
        }
        key = resolve_keyset_key(key, ask_direction, clear_line);
    }
    if (ask_direction) {
        putStringClearToEOL("Which direction?", Coord_t{ 0, 0 });
        g_refresh_requested.store(true);
    } else if (clear_line) {
        putStringClearToEOL("", Coord_t{ 0, 0 });
    }
    return key;
}

void host_sleep_ms(int ms) { portable::sleep_ms(ms); }
bool host_shutdown_requested() { return g_ui_gone.load() || g_quit_requested.load(); }

void send_exit(int code)
{
    if (g_out == nullptr || g_ui_gone.load() || g_exit_sent.exchange(true)) return;
    presentation::ExitMessage message;
    message.code = code;
    (void)send_message(presentation::encode_exit(message));
}

//! 原作は終わるとき exit(0) を呼ぶ（game.cpp の exitProgram）。その直前に exit を送る。
void send_exit_at_process_end() { send_exit(0); }

std::string join(std::string dir, const char *name)
{
    if (!dir.empty() && dir.back() != portable::kPathSep) dir.push_back(portable::kPathSep);
    return dir + name;
}

/*
 * ファイルの在り処を決める。読むだけのデータは実行ファイルの隣の `umoria/data/`、
 * 書くもの（セーブ・スコア）は `data_dir()/umoria/`。開発ツリーの直下へは書かない。
 * @return セーブファイルのパス。作れなければ空。
 */
std::string configure_paths()
{
    const std::string data = join(join(portable::exe_dir(), "umoria"), "data");
    namespace f = config::files;
    f::splash_screen = join(data, "splash.txt");
    f::welcome_screen = join(data, "welcome.txt");
    f::license = join(data, "LICENSE");
    f::versions_history = join(data, "versions.txt");
    f::help = join(data, "help.txt");
    f::help_wizard = join(data, "help_wizard.txt");
    f::help_roguelike = join(data, "rl_help.txt");
    f::help_roguelike_wizard = join(data, "rl_help_wizard.txt");
    f::death_tomb = join(data, "death_tomb.txt");
    f::death_royal = join(data, "death_royal.txt");

    const std::string root = portable::data_dir();
    if (root.empty()) return {};
    const std::string home = join(root, "umoria");
    if (!portable::make_dir(home)) return {};
    f::scores = join(home, "scores.dat");
    f::save_game = join(home, "game.sav");
    // 原作はスコアファイルを "rb+" で開くので、無ければ空で作っておく。
    if (FILE *fp = std::fopen(f::scores.c_str(), "ab"); fp != nullptr) std::fclose(fp);
    return f::save_game;
}

/*
 * パッドとコマンドメニューの命令の一覧。原作のヘルプ（`help.txt`）の
 * 全コマンドを載せる。空の一覧を送るとコマンドメニューを開いた画面側が落ちる（既知の罠）。
 * 名札は英語版で組み切る段なので英語（日本語化は M5）。画面側の既定の割り当ては**字**で引くので
 * （`apply_default_pad_binds`）、`f` `<` `>` `s` `T` `o` `c` `a` `u` はそのまま既定のボタンに載る。
 * 方向を聞く命令（開ける・閉じる・トンネルなど）は原作が方向を聞くので、字 1 つで足りる。
 */
presentation::PadCommandsMessage build_pad_commands(bool with_item_actions)
{
    presentation::PadCommandsMessage commands;
    /*
     * キー配置（2026-10-07）。表の字は標準の配置の字で持ち（`command`。既定の割り当てはこちらで引く）、
     * ローグライクの配置のキー列は原作 `originalCommands()`（`game_run.cpp`。標準の字をローグライクの
     * 字へ直す関数）と同じ対応で作る。**番号（id）とコマンド文字は配置によらず同じ。**
     */
    commands.current_keymap = config::options::use_roguelike_keys ? "rogue" : "original";
    const auto rogue_seq = [](const std::vector<int> &seq) {
        std::vector<int> out = seq;
        const int ctrl_k = 'K' & 0x1f;
        switch (seq.front()) {
        case '.': return std::vector<int>{ '^', '.' }; // 走る: 次の方向を Shift 付きに（resolve_keyset_key）
        case 'T': return std::vector<int>{ '^', ',' }; // 掘る: 次の方向を Ctrl 付きに（resolve_keyset_key）
        case 'B': out.front() = 'f'; break;            // 体当たり
        case 'L': out.front() = 'W'; break;            // 現在地
        case 'S': out.front() = '#'; break;            // 探索モード
        case 'a': out.front() = 'z'; break;            // 魔法棒を向ける
        case 'b': out.front() = 'P'; break;            // 本を読む
        case 'f': out.front() = 't'; break;            // 投げる
        case 'j': out.front() = 'S'; break;            // くさびを打つ
        case 'l': out.front() = 'x'; break;            // 見る
        case 't': out.front() = 'T'; break;            // 外す
        case 'u': out.front() = 'Z'; break;            // 杖を使う
        case 'x': out.front() = 'X'; break;            // 武器の持ち替え
        default:
            if (seq.front() == ctrl_k) out.front() = 'Q'; // 終了
            break;
        }
        return out;
    };
    /*
     * 分類は 5 つ。最初は全部を Action に入れていて多すぎ、変愚の原作の
     * 9 分類（src/cmd-io/cmd-menu-content-table.cpp）に合わせたら今度は細かすぎた（同日「少し統廃合して 5 種くらいに」）。
     * 変愚の分類を寄せて: 行動＋扉/箱 → Action、道具(使用)＋道具(その他)＋装備 → Items、魔法 → Magic、
     * 情報 → Information、設定＋その他 → System。並びの中の順は変愚のまま。
     *
     * **番号（id）は命令ごとに固定**。保存されたパッドの割り当て（hd2d-UmoriaCore.cfg）は番号で持つので、
     * 分類や並びを変えても番号は変えない（最初の版の並びで振った番号のまま）。
     */
    const auto add_seq = [&](int id, std::vector<int> seq, const char *group, const char *label) {
        presentation::PadCommandWireEntry entry;
        entry.id = id;
        entry.command = seq.front();
        // 日本語のときは名札と分類を訳す（M5。言語が届いたら送り直す。receive_loop の ui_state）。
        entry.group_utf8 = umoria::tr(group);
        entry.label_utf8 = umoria::tr(label);
        entry.seq_rogue = rogue_seq(seq);
        entry.seq_original = std::move(seq);
        commands.entries.push_back(std::move(entry));
    };
    const auto add = [&](int id, int key, const char *group, const char *label) { add_seq(id, { key }, group, label); };
    const auto ctrl = [](char c) { return c & 0x1f; };
    const char *const action = "Action";
    const char *const items = "Items";
    const char *const magic = "Magic";
    const char *const info = "Information";
    const char *const system = "System";
    add(5, 'R', action, "Rest");
    add_seq(6, { 'R', '*', 0x0D }, action, "Rest as needed"); // 0x0D = Enter。数値入力では言えないので命令として持つ
    add(7, 's', action, "Search");
    add(8, 'S', action, "Search mode on/off");
    add(9, 'l', action, "Look");
    add(2, '<', action, "Go up stairs");
    add(1, '>', action, "Go down stairs");
    add(3, '.', action, "Run");
    add(4, '-', action, "Move without pickup");
    add(10, 'o', action, "Open");
    add(11, 'c', action, "Close");
    add(14, 'B', action, "Bash");
    add(13, 'j', action, "Jam a door");
    add(12, 'D', action, "Disarm a trap or chest");
    add(15, 'T', action, "Tunnel");
    add(20, 'r', items, "Read a scroll");
    add(19, 'q', items, "Quaff a potion");
    add(18, 'u', items, "Use a staff");
    add(17, 'a', items, "Aim a wand");
    add(21, 'E', items, "Eat");
    add(16, 'f', items, "Fire or throw an item");
    add(23, 'w', items, "Wear");
    add(24, 't', items, "Take off");
    add(25, 'x', items, "Exchange weapons");
    add(22, 'F', items, "Refuel a lamp");
    add(26, 'd', items, "Drop an item");
    add(27, '{', items, "Inscribe an item");
    add(32, 'i', items, "Inventory list");
    add(33, 'e', items, "Equipment list");
    add(28, 'm', magic, "Cast a spell");
    add(29, 'p', magic, "Pray");
    add(30, 'b', magic, "Browse a book");
    add(31, 'G', magic, "Gain new spells");
    add(36, 'M', info, "Full map");
    add(35, 'L', info, "Locate");
    add(34, 'C', info, "Character status");
    add(37, '/', info, "Identify symbol");
    add(38, ctrl('P'), info, "Show previous messages");
    add(39, 'V', info, "Scoreboard");
    add(42, '=', system, "Set options");
    add(43, ctrl('X'), system, "Save and quit");
    add(41, '?', system, "Help");
    add(40, 'v', system, "Version info");
    add(44, ctrl('K'), system, "Quit");
    int id = 45;
    /*
     * 持ち物の一覧から項目を選んで行う操作（隠しグループ `@inventory-action`）。**画面側へは送らない**——
     * 送るとコマンドメニューに分類として出る（Rogue2 も送らずに、コアの持ち物メニューだけで使う）。
     * 持ち物メニュー（M3 の次の段）が `build_pad_commands(true)` で引く。
     */
    if (!with_item_actions) return commands;
    const auto item = [&](int key, const char *label) {
        presentation::PadCommandWireEntry entry;
        entry.id = 10000 + id++;
        entry.command = key;
        entry.group_utf8 = "@inventory-action";
        entry.label_utf8 = label;
        entry.seq_original = { key };
        entry.seq_rogue = rogue_seq(entry.seq_original);
        commands.entries.push_back(std::move(entry));
    };
    item('E', "Eat");
    item('q', "Quaff");
    item('r', "Read");
    item('a', "Aim");
    item('u', "Use");
    item('w', "Wear / wield");
    item('t', "Take off");
    item('f', "Fire / throw");
    item('b', "Browse");
    item('F', "Fill a lamp");
    item('{', "Inscribe");
    item('d', "Drop");
    return commands;
}

/*
 * タイトルとセーブの一覧（決めたこと 2026-10-03「つけよう」。セーブを複数持つ）。
 * 原作は決まった 1 つのセーブ（game.sav）を使うが、起動の引数でファイルを渡せる（main.cpp）。
 * そこで原作を呼ぶ前にアダプタが一覧を出し、選んだファイルを渡す。新しく始めるときは日時で名前を付ける
 * （原作はキャラを作る前にセーブの名前が決まっている必要がある）。部品は Frox・幻想蛮怒・Sil-Q と同じ
 * `NativeSavePicker`。原作のソースには手を入れない。
 * @return 選んだ（または新しい）セーブのパス。終了の求めなら空。
 */
/*
 * セーブが「死んだキャラ」のものかを、ファイルの頭から読んで調べる（原作 `game_save.cpp` の `loadGame` と同じ並び）。
 * 原作は死んだキャラのセーブを読み込むと「Restoring Memory of a departed spirit...」を出して**キャラ作成**へ進む
 * （怪物の記憶とオプションだけを引き継ぐ。`.sav` は死ぬときにも書かれる）。一覧に目印が無いと、
 * 読み込んだのにキャラ作成になる不具合に見えるので、一覧に「死亡」と出す（2026-10-07）。
 * 符号化: 先頭の 3 バイト（版）は平文、4 バイト目が鍵の初期値、以後は「1 バイト前の生の値との排他的論理和」。
 */
bool save_file_is_dead(const std::filesystem::path &path)
{
    FILE *fp = std::fopen(path.string().c_str(), "rb");
    if (fp == nullptr) return false;
    bool dead = false;
    unsigned char head[4];
    if (std::fread(head, 1, 4, fp) == 4) {
        unsigned char prev = head[3];
        const auto next = [&](int &value) {
            const int c = std::fgetc(fp);
            if (c == EOF) return false;
            value = c ^ prev;
            prev = static_cast<unsigned char>(c);
            return true;
        };
        bool ok = true;
        // 怪物の記憶: (id 2) + 長整数 2 つ + 短整数 3 つ + 1 バイト 2 つ + 攻撃 4 = 22 バイトの項目を 0xFFFF まで並べる。
        for (int guard = 0; ok && guard < 4096; ++guard) {
            int lo = 0, hi = 0;
            ok = next(lo) && next(hi);
            if (!ok || (lo == 0xFF && hi == 0xFF)) break;
            for (int i = 0; ok && i < 20; ++i) {
                int skip = 0;
                ok = next(skip);
            }
        }
        if (ok) {
            int b[4] = {};
            ok = next(b[0]) && next(b[1]) && next(b[2]) && next(b[3]);
            if (ok) dead = (b[3] & 0x80) != 0; // 長整数の最上位ビット（0x80000000。`game.character_is_dead`）
        }
    }
    std::fclose(fp);
    return dead;
}

std::string pick_save(const std::string &home)
{
    namespace fs = std::filesystem;
    // 言語は ui_state で届く。届くまで少し待つ（届かなければ日本語）。
    for (int i = 0; i < 50 && !g_ui_state_seen.load() && !host_shutdown_requested(); ++i) portable::sleep_ms(20);
    std::string lang;
    {
        std::lock_guard<std::mutex> lock(g_pregame_mutex);
        lang = g_ui_lang;
    }
    const auto list = [home]() {
        std::vector<std::pair<fs::file_time_type, std::string>> found;
        std::error_code ec;
        for (fs::directory_iterator it(home, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".sav") == 0 && it->is_regular_file(ec)) {
                found.emplace_back(it->last_write_time(ec), name);
            }
        }
        // 新しい順。
        std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::vector<std::string> names;
        for (auto &entry : found) names.push_back(std::move(entry.second));
        return names;
    };
    const auto remove = [home](const std::string &name) -> std::string {
        std::error_code ec;
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos) return "Invalid save name.";
        fs::remove(fs::path(home) / name, ec);
        if (ec) return "The save could not be deleted.";
        fs::remove(fs::path(home) / (name + ".info"), ec);
        presentation::player_identity_remove(kIdentityCoreKey, name);
        return {};
    };
    const bool japanese = lang != "en";
    const auto display = [home, japanese](const std::string &name) {
        std::string text;
        if (FILE *fp = std::fopen((fs::path(home) / (name + ".info")).string().c_str(), "rb"); fp != nullptr) {
            char line[200] = {};
            if (std::fgets(line, sizeof(line), fp) != nullptr) text = line;
            std::fclose(fp);
        }
        if (text.empty()) text = name;
        // 死んだキャラのセーブは読み込むとキャラ作成になる（原作の振る舞い）ので、一覧で分かるようにする。
        if (save_file_is_dead(fs::path(home) / name)) {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            text += japanese ? reinterpret_cast<const char *>(u8"（死亡。読み込むと新しいキャラを作る）") : " (dead: loading starts a new character)";
        }
        return text;
    };
    presentation::NativeSavePicker picker(kCoreName, lang != "en", list, remove, display);
    for (;;) {
        const auto page = picker.page();
        clearScreen();
        // 端末は 1 バイトの字の格子なので、日本語の見出しは書かない（「????」になった）。窓の小見出しで運ぶ。
        bool ascii = true;
        for (const char c : page.title) ascii = ascii && (static_cast<unsigned char>(c) < 0x80);
        putString(ascii ? page.title.c_str() : kCoreName, Coord_t{ 1, 2 });
        std::vector<MenuChoice> choices;
        for (std::size_t i = 0; i < page.rows.size(); ++i) {
            const auto &row = page.rows[i];
            const int at = 4 + static_cast<int>(i);
            // 端末の字は ASCII だけを書く（日本語の名札は選択肢の label で運ぶ）。
            std::string line = std::string(1, static_cast<char>(row.key)) + ") ";
            for (const char c : row.label) line.push_back((static_cast<unsigned char>(c) < 0x80) ? c : ' ');
            if (line.size() > 76) line.resize(76);
            putString(line.c_str(), Coord_t{ at, 2 });
            MenuChoice choice;
            choice.line_index = at;
            choice.span_begin = 2;
            choice.span_len = static_cast<int>(line.size());
            choice.key_begin = 2;
            choice.key_len = 1;
            choice.key = row.key;
            choice.selected = row.selected;
            choice.label_utf8 = row.label;
            choice.description_utf8 = page.description;
            choices.push_back(std::move(choice));
        }
        {
            std::lock_guard<std::mutex> lock(g_pregame_mutex);
            g_pregame_choices = std::move(choices);
            g_pregame_title = ascii ? std::string() : page.title;
        }
        int key = 0;
        while (key == 0) {
            if (host_shutdown_requested()) {
                key = -1;
                break;
            }
            host_present();
            key = host_next_key();
            if (key == 0) portable::sleep_ms(20);
        }
        const int result = picker.key(key);
        if (result == -2) continue;
        {
            std::lock_guard<std::mutex> lock(g_pregame_mutex);
            g_pregame_choices.clear();
        }
        clearScreen();
        if (result < 0) return {};
        if (result == 1) {
            const std::time_t now = std::time(nullptr);
            char name[64];
            std::strftime(name, sizeof(name), "umoria-%Y%m%d-%H%M%S.sav", std::localtime(&now));
            g_identity_save_id = name;
            presentation::player_identity_remove(kIdentityCoreKey, g_identity_save_id); // 同じ名前の古い表示名を残さない
            presentation::player_identity_activate(kIdentityCoreKey, g_identity_save_id);
            return (fs::path(home) / name).string();
        }
        g_identity_save_id = picker.selected();
        presentation::player_identity_activate(kIdentityCoreKey, g_identity_save_id);
        return (fs::path(home) / picker.selected()).string();
    }
}

int run_protocol(presentation::TransportHandle in, presentation::TransportHandle out)
{
    g_in = in;
    g_out = out;
    if (g_in == nullptr || g_out == nullptr) return 3;

    std::string payload, err;
    if (presentation::read_message(g_in, payload, err) != presentation::TransportResult::Ok) return 4;
    if (presentation::peek_message_type(payload) != "hello") return 4;
    presentation::HelloMessage hello;
    if (!presentation::decode_hello(payload, hello, err) || hello.protocol != presentation::kProtocolVersion) return 4;

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = kCoreName;
    ack.core_version = kCoreVersion;
    if (!send_message(presentation::encode_hello_ack(ack))) return 5;
    g_tables_roguelike = config::options::use_roguelike_keys;
    if (!send_message(presentation::encode_pad_commands(build_pad_commands(false)))) return 5;
    g_item_table = build_pad_commands(true);
    // 絵の目録（変愚の公式タイル表と、Rogue 系が Frox から借りる分）。地形・怪物・アイテムの tile_index はこの番号。
    if (!send_message(presentation::encode_asset_manifest(presentation::build_rogue_asset_manifest()))) return 5;
    // サブパネルの種類。原作に別窓は無いので、Rogue 系と同じく画面側の既定の 5 種を選ばせる。
    // 言語はまだ届いていない（既定は英語。上のコマンド表と同じ）。日本語と分かったら ui_state の受信で送り直す。
    if (!send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(!umoria::lang_ja())))) return 5;

    std::thread(receive_loop, g_in).detach();
    umoria::HostHooks hooks;
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    umoria::set_host_hooks(hooks);

    const std::string save_path = configure_paths();
    if (save_path.empty()) {
        std::fprintf(stderr, "[umoria] cannot prepare the data directory\n");
        return 6;
    }
#if !defined(HENGBAND_ANDROID)
    std::atexit(send_exit_at_process_end);
#endif

    const std::string home = std::filesystem::path(save_path).parent_path().string();
    const std::string chosen = pick_save(home);
    if (chosen.empty()) {
        send_exit(0);
        return 0;
    }

    {
        // 既にある、死んでいないキャラのセーブを読み込むときだけ、読み込み中の見せ方にする（新しいゲームと、死んだキャラは誕生へ進む）。
        std::error_code ec;
        g_loading_save.store(std::filesystem::exists(chosen, ec) && !ec && !save_file_is_dead(chosen));
    }
    char program[] = "umoria";
    std::string save_arg = chosen;
    char *argv[] = { program, save_arg.data(), nullptr };
#if defined(HENGBAND_ANDROID)
    int rc = 0;
    try {
        rc = um_game_main(2, argv);
    } catch (const UmGameExit &done) {
        rc = done.code;
    }
#else
    const int rc = um_game_main(2, argv);
#endif
    send_exit(rc);
    return rc;
}

/*
 * `UmoriaCore.exe --selftest`（読み込み中の見せ方の分）：セーブを読み込む途中のコマが誕生（phase=birth・題「キャラクター作成」）に
 * ならないこと、新しいゲーム・死んだキャラ・読み込みに失敗してキャラ作成へ進む場合は今までどおり誕生のままであることを、
 * `startup_look` の判定で照合する。画面側の phase の決め方（`ui_probe_phase`）が窓の種類 `start` を見ることとの対応は
 * `.taskwork/play_tests/fixing/small_umoria_load/load_phase_check.py` が実機のフレームで確かめる。
 */
int loading_look_selftest()
{
    int failures = 0;
    const auto expect = [&](const char *what, StartupLook want, bool want_creating, bool loading, bool in_game, bool game_over,
                            bool text_input, const char *row20, bool has_choices) {
        bool creating = false;
        const StartupLook got = startup_look(loading, in_game, game_over, text_input, row20, has_choices, creating);
        if (got != want || creating != want_creating) {
            std::fprintf(stderr, "[umoria] selftest: loading look: FAIL %s\n", what);
            ++failures;
        }
    };
    expect("loading splash (continue key)", StartupLook::Splash, false, true, false, false, false, "", true);
    expect("loading progress text (no choices)", StartupLook::Opening, false, true, false, false, false, "", false);
    expect("not loading (new game splash)", StartupLook::Normal, false, false, false, false, false, "", true);
    expect("loaded (character exists)", StartupLook::Normal, false, true, true, false, false, "", false);
    expect("dead character screen", StartupLook::Normal, false, true, false, true, false, "", false);
    expect("load failed -> race selection is birth", StartupLook::Normal, true, true, false, false, false, "  Choose a race (a-h):", true);
    expect("load failed -> re-roll is birth", StartupLook::Normal, true, true, false, false, false, "  Hit space to re-roll or ESC to accept", true);
    expect("load failed -> name input is birth", StartupLook::Normal, true, true, false, false, true, "", false);
    if (failures == 0) std::fprintf(stderr, "selftest: loading look: ok\n");
    return failures == 0 ? 0 : 1;
}

} // namespace

#if !defined(_WIN32)
//! Android の入口（platform/android/hd2d_entry_android.cpp が dlsym で引く。Rogue 系・Angband と同じ形）。
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    (void)protocol_log_path;
    (void)forwarded_args;
    return run_protocol(in, out);
}
#endif

int main(int argc, char **argv)
{
    bool protocol = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--ui-protocol=stdio") protocol = true;
        if (std::string_view(argv[i]) == "--selftest") return umoria::status_effects_selftest() | umoria::attack_fx_selftest() | loading_look_selftest();
    }
    if (!protocol) {
        std::fprintf(stderr, "UmoriaCore --ui-protocol=stdio\n");
        return 2;
    }
    presentation::set_stdio_binary_mode();
    return run_protocol(presentation::transport_stdin(), presentation::transport_stdout());
}
