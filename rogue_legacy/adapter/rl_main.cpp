#include "rl_frame.h"
#include "rl_host.h"
#include "rl_curses_api.h"
#include "rl_exit.h"
#include "rl_i18n.h"
#include "rl_inventory_api.h"
#include "rl_sound.h"

#include "bridge/asset_manifest_builder.h"
#include "bridge/rogue_borrowed_tiles.h"
#include "bridge/input_event_adapter.h"
#include "bridge/native_inventory_menu.h"
#include "bridge/native_modern.h"
#include "bridge/player_identity.h"
#include "bridge/rogue_sub_panels.h"
#include "frame/frame_codec.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"
#include "portable/legacy_os.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef RL_CORE_NAME
#define RL_CORE_NAME "Rogue"
#endif
#ifndef RL_CORE_VERSION
#define RL_CORE_VERSION "unknown"
#endif
#ifndef RL_GAME_MAIN
#define RL_GAME_MAIN rogue_game_main
#endif

extern "C" int RL_GAME_MAIN(int argc, char **argv, char **envp);

namespace {

constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;
std::mutex g_input_mutex;
std::deque<int> g_keys;
std::atomic<bool> g_ui_gone{ false };
std::atomic<bool> g_quit_requested{ false };
std::atomic<bool> g_refresh_requested{ false };
std::atomic<std::uint64_t> g_generation{ 0 };
std::uint64_t g_frame_id = 0;
std::string g_last_frame;
std::mutex g_modern_mutex;
presentation::NativeModernMenu g_native_menu;
presentation::NativeInventoryMenu g_inventory_menu;
presentation::PadCommandsMessage g_pad_commands;
presentation::PadCommandsMessage build_rogue_pad_commands(bool english, bool is_54, bool with_hidden);
//! 画面が選んだサブパネルの種類（`ui_state.sub_panel_kinds`）。g_modern_mutex で守る。
std::array<int, kSubPanelCount> g_sub_panel_kinds = { -1, -1, -1, -1, -1, -1, -1 };
/*
 * HD2D: 新規ゲームの好きな果物の既定値（項目 2）に使う UI 言語。`ui_state` は
 * 握手直後に画面が必ず 1 回送るので、
 * ゲーム本体を起こす前に短く待てば取れる。届かなければ「ja」のまま進む
 * （既存の `g_native_menu.set_language` の既定と同じ）。
 */
std::atomic<bool> g_ui_state_received{ false };
std::mutex g_ui_lang_mutex;
std::string g_ui_lang = "ja";

std::string identity_core_key()
{
    return std::string_view(RL_CORE_VERSION).starts_with("5.") ? "rogue54" : "rogue36";
}

void rewrite_player_name_editor(std::vector<int> &keys)
{
    const auto id = presentation::player_identity_snapshot();
    for (int i = 0; i < 80; ++i) keys.push_back(0x08);
    /*
     * 全部消した欄は空のまま。空の名前を player_identity_core_name() に通すと、日本語名の
     * ための仮名（"P" ＋ハッシュ、例 "P5W2UTT3"）が返り、欄に勝手な字が入っていた
     * （気づいたこと 2026-09-26「名前をすべて消すと何か文字が勝手に入る」）。
     */
    const std::string &source = id.edit_touched ? id.edit_name_utf8 : id.display_name_utf8;
    if (source.empty()) return;
    const std::string alias = presentation::player_identity_core_name(source, 8);
    for (const unsigned char c : alias) keys.push_back(c);
}

bool handle_player_name_event(const presentation::InputEventWire &event, std::vector<int> &keys)
{
    if (!rl_player_name_input_active()) return false;
    auto id = presentation::player_identity_snapshot();
    if (!id.edit_active) {
        /*
         * HD2D: PlayerIdentity にまだ名前が無いときの表示名は、原作の
         * 既定名 "nobody"（rogue54/mdport.c の md_getusername、USERNAME→
         * LOGNAME→USER が無ければ "nobody"）に合わせる。実名の OS
         * アカウント名は絶対に出さない（rogue_restore 第 2 回・項目 1）。
         */
        std::string display = id.display_name_utf8.empty() ? "nobody" : id.display_name_utf8;
        std::string core = id.core_name_ascii.empty() ? "nobody" : id.core_name_ascii;
        /*
         * 設定（o）から入ったときは、原作が今持っている名前（whoami）が正。PlayerIdentity の
         * 名前と食い違う（ROGUEOPTS の name= など）なら、その値から編集を始める。
         */
        const char *current = rl_text_input_original();
        if (current != nullptr && *current != '\0' && core != current) {
            display = current;
            core = current;
        }
        presentation::player_identity_begin_edit(identity_core_key(), "rogue.save", display, core);
    }
    /*
     * PlayerIdentity の編集は「最初の 1 字で打ち直す」作りで、触る前の BS は何もしない。
     * Rogue の設定では今の名前が入った状態から消す・書き足すので、
     * 最初の操作の前に今の名前を編集中の値へ写す。
     */
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
        rewrite_player_name_editor(keys);
        return true;
    }
    if (event.e == "key" && event.name == "backspace") {
        touch();
        presentation::player_identity_backspace_edit();
        rewrite_player_name_editor(keys);
        return true;
    }
    // Ctrl-U は欄を空にする（原作の killchar）。
    if (event.e == "key" && event.ctrl && (event.chr == "u" || event.chr == "U")) {
        presentation::player_identity_clear_edit();
        rewrite_player_name_editor(keys);
        return true;
    }
    // ほかの Ctrl 併用（画面側の欄の中の移動 Ctrl-B/F/P/N）は字ではない。
    if (event.e == "key" && event.ctrl) return true;
    if (event.e == "key" && !event.chr.empty()) {
        touch();
        presentation::player_identity_append_edit(event.chr);
        rewrite_player_name_editor(keys);
        return true;
    }
    if (event.e == "cancel") {
        presentation::player_identity_cancel_edit();
        keys.push_back(0x1b);
        return true;
    }
    if (event.e == "confirm") {
        /*
         * 空のまま確定したら取消と同じ（名前は変えない）。欄を空にして Enter を送ると、
         * rl_option_edit_string も原作の get_str も元の値を残す。原作が空を nobody にする
         * 動きはこの経路では使わない。
         */
        const auto now = presentation::player_identity_snapshot();
        if (now.edit_touched && now.edit_name_utf8.empty()) {
            presentation::player_identity_cancel_edit();
            for (int i = 0; i < 80; ++i) keys.push_back(0x08);
            keys.push_back('\r');
            return true;
        }
        const auto committed = presentation::player_identity_commit_edit(8, "rogue.save");
        for (int i = 0; i < 80; ++i) keys.push_back(0x08);
        for (const unsigned char c : committed.core_name_ascii) keys.push_back(c);
        keys.push_back('\r');
        return true;
    }
    return false;
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

bool send_message(const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[rogue-legacy] send failed: %s\n", err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    return true;
}

void enqueue(const std::vector<int> &keys)
{
    std::lock_guard<std::mutex> lock(g_input_mutex);
    for (int key : keys) if (key > 0 && key <= 255) g_keys.push_back(key);
}

char rogue_direction_key(int dx, int dy)
{
    static constexpr char table[3][3] = {
        { 'y', 'k', 'u' }, { 'h', '.', 'l' }, { 'b', 'j', 'n' },
    };
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
    return table[dy + 1][dx + 1];
}

void append_rogue_input_keys(const presentation::InputEventsMessage &message, std::vector<int> &out)
{
    for (const auto &event : message.events) {
        if (event.e == "move") {
            const char key = rogue_direction_key(event.dx, event.dy);
            if (key != 0) out.push_back(static_cast<unsigned char>(key));
            continue;
        }
        presentation::InputEventsMessage one;
        one.events.push_back(event);
        presentation::append_input_event_keys(one, out);
    }
}

std::vector<ModernRow> legacy_inventory_rows()
{
    std::vector<ModernRow> rows;
    const bool is_54 = std::string_view(RL_CORE_VERSION).starts_with("5.");
    const int count = rl_inventory_count();
    std::vector<rl_inventory_item_state> items;
    items.reserve(static_cast<std::size_t>((std::max)(0, count)));
    bool armor_equipped = false;
    int rings_equipped = 0;
    for (int i = 0; i < count; ++i) {
        rl_inventory_item_state item{};
        if (!rl_inventory_item(i, &item)) continue;
        armor_equipped = armor_equipped || (item.equipped_flags & 2u) != 0u;
        if ((item.equipped_flags & 12u) != 0u) ++rings_equipped;
        items.push_back(item);
    }
    rows.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        ModernRow row;
        row.id = "item:";
        if (item.key >= 0x20 && item.key <= 0x7e) row.id.push_back(static_cast<char>(item.key));
        const char *translated = rl_ja_text(item.description);
        row.label = (translated != nullptr && *translated != '\0') ? translated : item.description;
        row.description = row.label;
        row.key = item.key;
        row.hotkey = item.key;
        row.slot = static_cast<int>(i);
        row.selection = 0;
        row.count = (std::max)(1, item.quantity);
        row.equipped = item.equipped_flags != 0u;
        row.glyph.assign(1, static_cast<char>(item.type));
        row.tile = rogue_legacy::object_tile_for_glyph(static_cast<std::uint32_t>(static_cast<unsigned char>(item.type)));

        const auto action = [&](int key) {
            row.actions.push_back({"", "", "", true, key});
        };
        action('I'); // inspect
        // 呪いは外す・捨てる・投げるを試したときに原作が初めて知らせる。項目の有無で先に漏らさない。
        action('d'); // drop
        if (item.callable) action('c');
        switch (item.type) {
        case ':': action('e'); break;
        case '!': action('q'); break;
        case '?': action('r'); break;
        case '/':
            action('z');
            if (!is_54) action('p');
            break;
        case ')':
            if ((item.equipped_flags & 1u) == 0u) action('w');
            action('t');
            break;
        case ']':
            if ((item.equipped_flags & 2u) != 0u) {
                action('T');
            } else if (!armor_equipped) action('W');
            break;
        case '=':
            if ((item.equipped_flags & 12u) != 0u) {
                action('R');
            } else if (rings_equipped < 2) action('P');
            break;
        default: break;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<presentation::NativeItemRow> legacy_prompt_item_rows()
{
    std::vector<presentation::NativeItemRow> result;
    if (!rl_prompt_active() || !rl_prompt_is_item()) return result;
    const auto rows = legacy_inventory_rows();
    result.reserve(rows.size());
    for (const auto &row : rows) {
        bool valid = false;
        for (int i = 0; i < rl_prompt_choice_count(); ++i) {
            if (rl_prompt_choice_key(i) == row.key) { valid = true; break; }
        }
        if (!valid) continue;
        result.push_back({row, 0, -1, 1});
    }
    return result;
}

bool continuation_visible()
{
    bool has_continue_kanji = false;
    std::string line;
    for (int col = 0; col < rl_term_cols(); ++col) {
        const auto cp = rl_term_codepoint(0, col);
        if (cp == 0x7d9a) has_continue_kanji = true; // 続
        if (cp >= 0x20 && cp <= 0x7e) line.push_back(static_cast<char>(cp));
    }
    return has_continue_kanji
        || line.find("--More--") != std::string::npos
        || line.find("More") != std::string::npos
        || line.find("Press space to continue") != std::string::npos
        || line.find("press space to continue") != std::string::npos;
}

/*
 * 原作が wait_for(' ') で Space だけを待つ全画面の一覧（`i` の持ち物・発見済みの物・得点・墓標など）が
 * 出ているか。案内の字（日本語は「［スペースで続ける］」、英語は "--Press space to continue--"）が
 * どの行にあっても真（3.6 は最下行、5.4 の重ねた一覧は右上）。
 * continuation_visible() は行 0 しか見ないので、この一覧では決定（Enter）も Space に直らず閉じなかった。
 */
bool space_wait_visible()
{
    static const std::u32string kJa = U"スペースで続ける";
    static const std::string kEn = "press space to continue";
    for (int row = 0; row < rl_term_rows(); ++row) {
        std::u32string wide;
        std::string ascii;
        for (int col = 0; col < rl_term_cols(); ++col) {
            const auto cp = rl_term_codepoint(row, col);
            if (cp == 0) continue;
            wide.push_back(static_cast<char32_t>(cp));
            if (cp >= 0x20 && cp <= 0x7e) {
                ascii.push_back(static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp));
            }
        }
        if (wide.find(kJa) != std::u32string::npos || ascii.find(kEn) != std::string::npos) return true;
    }
    return false;
}

bool direction_continue_marker_visible()
{
    /*
     * 方向入力を Space へ変換する条件は、表示中の [続く] / ［続く］（英語は --More--）
     * そのものに限定する。"スペースで続ける" は別の待ちであり、
     * 方向キーを奪ってはいけない。
     */
    std::vector<uint32_t> row;
    row.reserve(static_cast<std::size_t>(rl_term_cols()));
    for (int col = 0; col < rl_term_cols(); ++col) {
        row.push_back(rl_term_codepoint(0, col));
    }
    for (std::size_t i = 0; i + 3 < row.size(); ++i) {
        const bool open = row[i] == '[' || row[i] == 0xff3b;
        const bool close = row[i + 3] == ']' || row[i + 3] == 0xff3d;
        if (open && row[i + 1] == 0x7d9a && row[i + 2] == 0x304f && close) return true;
    }
    // 英語の表示（修-46）：原作の "--More--"。
    std::string ascii;
    for (const auto cp : row) if (cp >= 0x20 && cp <= 0x7e) ascii.push_back(static_cast<char>(cp));
    return ascii.find("--More--") != std::string::npos;
}

std::string configure_storage()
{
    const bool is_54 = std::string_view(RL_CORE_VERSION).starts_with("5.");
    std::string root = portable::data_dir();
    if (root.empty()) return {};
    if (root.back() != portable::kPathSep) root.push_back(portable::kPathSep);
    root += is_54 ? "rogue54" : "rogue36";
    if (!portable::make_dir(root)) return {};
    root.push_back(portable::kPathSep);
#if !defined(__ANDROID__)
    /*
     * 原作に長いパスを渡さない（2026-10-06）。Rogue 3.6 は保存先のフォルダに "rogue.save" を足して
     * **80 バイトの file_name へ長さを見ずに写す**（main.c）。5.4 も保存のときに "file name: <パス>" を
     * 145 バイトのメッセージの入れ物へ長さを見ずにつなぐ（io.c の doadd）。配布物を置いたフォルダの
     * パスが長い（日本語なら 1 字 2 バイト）と隣の変数を壊していた。そこで**コアのプロセスが保存先へ移り**、
     * 原作には ".\" だけを渡す（空にすると原作は遊ぶ人のホームのフォルダへ切り替える）。
     * デスクトップのコアは画面とは別のプロセスなので、移っても画面には響かない。Android はコアが画面と
     * 同じプロセスのスレッドなので移らない（保存先は 50 バイトほどと短い）。
     */
    if (std::filesystem::path(root).is_absolute()) {
        std::error_code ec;
        std::filesystem::current_path(std::filesystem::path(root), ec);
        if (!ec) {
            const std::string here = std::string(".") + portable::kPathSep;
            rl_set_home_dir(here.c_str());
            return here + "rogue.save";
        }
    }
#endif
    rl_set_home_dir(root.c_str());
    return root + "rogue.save";
}

void receive_loop(std::uint64_t generation, presentation::TransportHandle input)
{
    while (generation == g_generation.load() && !g_ui_gone.load()) {
        std::string payload, err;
        const auto result = presentation::read_message(input, payload, err);
        if (result != presentation::TransportResult::Ok) {
            if (generation == g_generation.load()) g_ui_gone.store(true);
            return;
        }
        if (generation != g_generation.load()) return;
        const auto type = presentation::peek_message_type(payload);
        if (type == "keys") {
            presentation::KeysMessage message;
            if (presentation::decode_keys(payload, message, err)) enqueue(message.keys);
        } else if (type == "input_event") {
            presentation::InputEventsMessage message;
            if (presentation::decode_input_events(payload, message, err)) {
                /*
                 * UI の操作を受け付けてからキーを列に積むまでを、画面の取り込み
                 * （host_present も g_modern_mutex を取る）と同じロックの中で行う。
                 * 間に取り込みが入ると「受け付けた、でも中身は古い」画面が送られる。
                 */
                std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
                std::vector<int> keys;
                presentation::InputEventsMessage shared;
                for (const auto &event : message.events) {
                    if (event.e == "move" && (event.dx != 0 || event.dy != 0) && direction_continue_marker_visible()) {
                        // Rogue 3.6/5.4 の［続く］表示中だけ、方向キーも
                        // 決定と同じ Space として扱う。待ち解除後の通常移動には触れない。
                        keys.push_back(' ');
                        continue;
                    }
                    /*
                     * 文字入力の間の方向は文字にしない。以前は Rogue の移動キー（上＝k）へ
                     * 訳されて入力欄へ字として入っていた（気づいたこと 2026-09-26「好きな果物:
                     * xkkkkk」）。欄の中の移動は画面側の文字入力 UI が持つ。
                     */
                    if (event.e == "move" && rl_text_input_active()) continue;
                    if (handle_player_name_event(event, keys)) continue;
                    if (event.e == "ui_action") {
                        std::vector<int> translated;
                        bool handled = false;
                        handled = g_inventory_menu.input(event, translated);
                        if (!handled) {
                            const bool closing_rogue_settings=event.action=="cancel"&&g_native_menu.rogue_settings_active();
                            handled = g_native_menu.input(event, translated);
                            if(handled&&closing_rogue_settings&&translated.size()==1&&translated.front()==27)
                                translated.push_back(' ');
                            /*
                             * ［続く］は Space しか受けない（ESC は読み捨てられ、画も来ないまま止まる）。
                             * どの窓の上でも、［続く］が出ているときの cancel は continue と同じ Space にする
                             * （パッドの B でも抜けられるように。2026-09-26）。
                             */
                            else if(handled&&event.action=="cancel"&&translated.size()==1&&translated.front()==27
                                &&direction_continue_marker_visible())
                                translated.front()=' ';
                        }
                        /*
                         * 名前の入力欄の決定・取消は、実窓では input_event の confirm / cancel ではなく
                         * ui_action の text_confirm / cancel で届き（`text_confirm_action`）、上の
                         * g_native_menu が Enter / Esc の 1 キーへ訳す。そのキーをそのまま原作へ渡すと
                         * PlayerIdentity の編集が確定されず、HUD の名前（PlayerIdentity の表示名）が
                         * 古いまま残っていた（気づいたこと 2026-09-26「名前を変えてもマップの
                         * ステータス箇所の名前が書き換わらない」）。受け付けた決定・取消は
                         * handle_player_name_event の confirm / cancel と同じ道へ通す。
                         */
                        if (handled && rl_player_name_input_active() && translated.size() == 1
                            && (translated.front() == 13 || translated.front() == 27)) {
                            presentation::InputEventWire end_event;
                            end_event.e = translated.front() == 13 ? "confirm" : "cancel";
                            std::vector<int> name_keys;
                            if (handle_player_name_event(end_event, name_keys)) translated = std::move(name_keys);
                        }
                        if (handled) {
                            /* UI-only state transitions do not wake the original
                             * Rogue input loop by themselves.  Ask the game
                             * thread for a fresh frame so inventory open/cancel
                             * and other no-key Modern actions become visible. */
                            if (translated.empty()) g_refresh_requested.store(true);
                            keys.insert(keys.end(), translated.begin(), translated.end());
                            continue;
                        }
                    }
                    shared.events.push_back(event);
                }
                append_rogue_input_keys(shared, keys);
                enqueue(keys);
            }
        } else if (type == "ui_state") {
            presentation::UiStateMessage state;
            if (presentation::decode_ui_state(payload, state, err)) {
                {
                    std::lock_guard<std::mutex> lang_lock(g_ui_lang_mutex);
                    g_ui_lang = state.lang.empty() ? "ja" : state.lang;
                }
                g_ui_state_received.store(true);
                std::lock_guard<std::mutex> lock(g_modern_mutex);
                /*
                 * 表示言語（修-46）。日本語以外は原作の英語をそのまま出す。切り替わったら、
                 * (1) 訳す・訳さないの旗を替え、(2) ゲームのスレッドに命令待ちの間の書き直しを頼み
                 * （状態行などは書いた時点の言語で残るため）、(3) コマンドメニューを新しい言語で送り直す。
                 * 送り直しは番号（id）とコマンド文字を変えない（画面側の割り当ての保存を崩さない）。
                 */
                {
                    const bool english = !(state.lang.empty() || state.lang == "ja");
                    if ((rl_i18n_is_english() != 0) != english) {
                        const bool is_54 = std::string_view(RL_CORE_VERSION).starts_with("5.");
                        rl_i18n_set_english(english ? 1 : 0);
                        rl_language_changed();
                        g_pad_commands = build_rogue_pad_commands(english, is_54, true);
                        (void)send_message(presentation::encode_pad_commands(build_rogue_pad_commands(english, is_54, false)));
                        // サブパネルの種類の名前も新しい言語で送り直す（番号は同じ）。
                        (void)send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(english)));
                    }
                }
                g_native_menu.set_enabled(state.modern_ui);
                g_native_menu.set_language(state.lang.empty() ? "ja" : state.lang);
                g_native_menu.set_document_profile("rogue");
                g_inventory_menu.configure(state.modern_ui, state.lang.empty() ? "ja" : state.lang, "rogue");
                if (state.has_sub_panel_kinds) {
                    for (int i = 0; i < kSubPanelCount; ++i) g_sub_panel_kinds[static_cast<std::size_t>(i)] = state.sub_panel_kinds[i];
                }
                /*
                 * 効果音を誰が鳴らすか。Rogue 3.6/5.4 は
                 * 自分では鳴らせない（winmm 等を叩く実体を持たない）ので、真なら
                 * `frame.sounds` へ書き留め、偽なら何もしない＝無音になる。
                 */
                rl_sound_set_wanted(state.sound_events ? 1 : 0);
                g_refresh_requested.store(true);
            }
        } else if (type == "quit_request") {
            g_quit_requested.store(true);
        }
    }
}

void host_present()
{
    if (g_out == nullptr || g_ui_gone.load()) return;
    GameFrame frame = rogue_legacy::capture_frame(g_frame_id + 1, RL_CORE_NAME);
    {
        /*
         * まだ処理していないキーがあるうちは送らない。受け付けた操作（設定の左右など）の
         * キーを原作が読む前に取り込むと、「受け付けた、でも中身は古い」画面が先に出て、
         * 画面側はそれを操作の結果と取り違える。キーを読み終えれば次の描画で送られる。
         *
         * 確認は UI 状態の取り込みと同じ g_modern_mutex の中で行う。受信側も
         * 「受け付け→キーを積む」をこのロックの中で行うので、両者の間に割り込めない。
         */
        std::lock_guard<std::mutex> lock(g_modern_mutex);
        {
            std::lock_guard<std::mutex> input_lock(g_input_mutex);
            if (!g_keys.empty()) return;
        }
        auto prompt_items = legacy_prompt_item_rows();
        g_native_menu.capture(frame, {}, prompt_items);
        g_inventory_menu.capture(frame, [] { return legacy_inventory_rows(); }, [] { return g_pad_commands; });
        presentation::apply_rogue_sub_panel_kinds(frame, g_sub_panel_kinds);
    }
    // 魔法・投射物の効果は、フレームを送ると決まったこの時点で汲む（上の早期の return で捨てない）。
    rogue_legacy::fill_attack_fx(frame);
    const std::string wire = presentation::encode_game_frame(frame);
    const std::string key = frame_compare_key(wire);
    if (key == g_last_frame) return;
    ++g_frame_id;
    frame.frame_id = g_frame_id;
    const std::string final_wire = presentation::encode_game_frame(frame);
    g_last_frame = frame_compare_key(final_wire);
    (void)send_message(final_wire);
}

int host_next_key()
{
    if (g_refresh_requested.exchange(false)) {
        /*
         * UI 側だけで変わった状態（持ち物一覧の開閉など）を取り込み直す。
         * 前回の記録は消さない——消すと中身が同じフレームがもう 1 枚出て、
         * 画面側と検査が「操作の結果」と取り違える。変わっていれば照合で送られる。
         */
        host_present();
    }
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (g_keys.empty()) return 0;
    int key = g_keys.front();
    g_keys.pop_front();
    if ((key == '\r' || key == '\n') && (continuation_visible() || space_wait_visible())) {
        key = ' ';
    } else if (key == 27 && !rl_text_input_active() && !rl_prompt_active()
        && (direction_continue_marker_visible() || space_wait_visible())) {
        /*
         * Space だけを待つ一覧・［続く］では、取消（B・Esc）も原作は読み捨てて止まって見えた。
         * 画面側の案内（cancel_key）どおり閉じられるよう、Space へ直す。
         */
        key = ' ';
    } else if (key == 0x10 && !std::string_view(RL_CORE_VERSION).starts_with("5.")
        && rl_command_input_active() && !rl_child_input_active() && !rl_text_input_active()) {
        /*
         * 画面共通のメッセージ履歴のキー Ctrl+P（0x10）を、3.6 では「直前のメッセージを表示」の ^R（0x12）に直す（修-48）。
         * 3.6 の原作では ^P は魔法使い（wizard）のパスワードの問いで、押すと英語の「Wizard's Password:」が出て
         * 続けて打った字を消費していた。5.4・RogueClone2 は ^P がもともと「直前のメッセージを表示」なので、そのまま通す。
         * 命令待ちのときだけ直し、問いや文字入力の最中の 0x10 には触れない（名前の入力欄の Ctrl 併用は
         * handle_player_name_event が先に受ける）。
         */
        key = 0x12;
    }
    return key;
}

void host_sleep_ms(int ms) { portable::sleep_ms(ms); }
int host_shutdown_requested() { return g_ui_gone.load() || g_quit_requested.load(); }

/*
 * 窓を閉じたときの終了コード（修-49）。
 * 3.6 の auto_save()（save.c。窓を閉じたときの自動セーブ）は、保存に成功しても失敗しても、
 * そもそも保存先を開けなくても `exit(1)` で終わる（5.4 は常に 0）。そのまま返すと、
 * 保存できて正常に終わったのに画面が終了コード 1 で終わる（VOXEL_HD2D_DESIGN.md §2.1 は 0）。
 * 原作は触らないので、アダプタが保存ファイルを見て判じる：
 *   - 新しく書かれた（更新時刻が起動のころ以降）
 *   - 大きさが、必ず入る部分の合計以上（見出し 80 バイト×3 と、3 つの窓の全マス。
 *     窓は state.c の rs_write_window が 4 バイト×行×桁＋見出し 12 バイトずつ書く。
 *     窓は保存の終わり近くなので、書き途中で切れたファイルはここに届かない）
 * 両方を満たしたときだけ 0 にする。満たさないときは原作の値（1）のまま返し、保存の失敗を隠さない。
 */
bool autosave_file_complete(const std::filesystem::path &path, std::filesystem::file_time_type started, int rows, int cols)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    const std::uintmax_t window_bytes = 12u + 4u * static_cast<std::uintmax_t>(rows) * static_cast<std::uintmax_t>(cols);
    const std::uintmax_t least = 80u * 3u + 3u * window_bytes;
    if (size < least) return false;
    const auto written = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    // 古い FAT は更新時刻が 2 秒刻みなので、その分だけ余裕を見る。
    return written >= started - std::chrono::seconds(2);
}

int exit_code_after_autosave(int rc, bool autosave_ran, bool is_54, const std::filesystem::path &save_path,
    std::filesystem::file_time_type started, int rows, int cols)
{
    if (!autosave_ran || is_54 || rc == 0) return rc;
    if (autosave_file_complete(save_path, started, rows, cols)) return 0;
    std::fprintf(stderr, "%s: autosave on window close did not complete (save file missing, stale or too small); keeping exit code %d\n",
        RL_CORE_NAME, rc);
    return rc;
}

void add_command(presentation::PadCommandsMessage &commands, int id, char key, const char *group, const char *subgroup, const char *label)
{
    presentation::PadCommandWireEntry entry;
    entry.id = id;
    entry.command = static_cast<unsigned char>(key);
    entry.group_utf8 = group;
    entry.subgroup_utf8 = subgroup != nullptr ? subgroup : "";
    entry.label_utf8 = label;
    entry.seq_original = { static_cast<unsigned char>(key) };
    entry.seq_rogue = entry.seq_original;
    commands.entries.push_back(std::move(entry));
}

/*
 * コマンドメニュー（パッドの表）。**番号（id）はコマンドの並びの順と条件（3.6/5.4）だけで決まり、言語によらない**
 * （画面側の割り当ての保存は番号で持つ）。ja/en は同じ並びで文字だけ替える。
 * with_hidden が真なら、持ち物の一覧の項目操作（隠しグループ `@inventory-action`）を足す（画面へは送らない）。
 */
struct Bi {
    const char *ja;
    const char *en;
};

presentation::PadCommandsMessage build_rogue_pad_commands(bool english, bool is_54, bool with_hidden)
{
    presentation::PadCommandsMessage commands;
    commands.current_keymap = "rogue";
    int command_id = 1;
    const auto text = [english](const Bi &b) { return english ? b.en : b.ja; };
    const auto add = [&](char key, Bi group, Bi subgroup, Bi label) {
        add_command(commands, command_id++, key, text(group), text(subgroup), text(label));
    };
    const Bi act{ "行動", "Actions" }, info{ "情報", "Info" }, opt{ "設定", "Settings" }, none{ "", "" };
    const Bi move{ "移動", "Movement" }, search{ "探索", "Search" }, combat{ "戦闘", "Combat" }, sys{ "システム", "System" },
        disp{ "表示", "Display" }, items{ "アイテム", "Items" };
    const Bi run_dir{ "方向を指定して連続移動", "Run in a direction" };
    const Bi prev_msg{ "直前のメッセージを表示", "Show the previous message" };
    const Bi rest{ "その場で休む", "Rest in place" };
    add('>', act, move, { "階段を降りる", "Go down stairs" });
    add('<', act, move, { "階段を上る", "Go up stairs" });
    if (is_54) add(',', act, move, { "足元の物を拾う", "Pick up the item underfoot" });
    add('s', act, search, { "周囲を探索する", "Search around" });
    if (is_54) add('.', act, move, rest);
    if (!is_54) add(' ', act, move, rest);
    if (is_54) add('f', act, combat, { "指定方向の敵と連続して戦う", "Fight in a direction repeatedly" });
    if (is_54) add('F', act, combat, { "指定方向の敵を倒すまで戦う", "Fight in a direction to the death" });
    // 5.4 には 3.6 の f のような走行の接頭コマンドが無い（決めたこと 2026-09-24
    // 「5.4 の走るを特例として 3.6 と同様に（空きキー g）」）。5.4 自身の f（戦闘）は
    // そのまま残し、空きキー g に 3.6 の f と同じ役割を足す（prepare_sources.py の
    // rogue54 command.c 手当て）。ラベルは 3.6 の f と同じにする（指示のとおり）。
    if (is_54) add('g', act, move, run_dir);
    if (is_54) add('a', act, move, { "直前のコマンドを繰り返す", "Repeat the previous command" });
    if (is_54) add('^', act, search, { "指定方向の罠を調べる", "Identify a trap in a direction" });
    if (is_54) add('m', act, move, { "指定方向の物の上へ移動する", "Move onto an object in a direction" });
    if (!is_54) add('f', act, move, run_dir);
    add('S', act, sys, { "保存して終了", "Save and quit" });
    add('Q', act, sys, { "ゲームを終了", "Quit the game" });
    add('/', info, none, { "記号の意味を調べる", "Identify a symbol" });
    add('?', info, none, { "原作ヘルプ", "Original help" });
    add('v', info, none, { "バージョン情報", "Version info" });
    // rogue_restore 第 2 回・項目 3: 原作の `rogue -s` と同じ一覧を、ゲームを
    // 終了せずに見られる手段が無かった。'V' は両世代の command.c で未使用
    // （tools/rogue_legacy/prepare_sources.py の command.c 手当て）。
    add('V', info, none, { "スコア一覧を表示（ゲームは終了しない）", "Show the score list (the game continues)" });
    if (is_54) add('D', info, none, { "発見済みの物を表示", "Show discovered items" });
    if (is_54) add('@', info, none, { "現在の状態を表示", "Show the current status" });
    // rogue_restore 第 2 回・項目 3: パッドから届かなかった原作の情報コマンド
    // （方向を要らない単発のキー。command.c 354〜359 行目付近）。
    if (is_54) add(')', info, none, { "装備中の武器を表示", "Show the wielded weapon" });
    if (is_54) add(']', info, none, { "装備中の防具を表示", "Show the worn armor" });
    if (is_54) add('=', info, none, { "はめている指輪を表示", "Show the rings worn" });
    if (is_54) {
        add(static_cast<char>(0x10), info, none, prev_msg);
        add(static_cast<char>(0x12), act, disp, { "画面を再描画", "Redraw the screen" });
    } else {
        add(static_cast<char>(0x12), info, none, prev_msg);
        add(static_cast<char>(0x0c), act, disp, { "画面を再描画", "Redraw the screen" });
    }
    add('o', opt, none, { "原作の設定", "Original options" });
    // 数字で前置きする繰り返し回数（原作 command() の isdigit の輪）。RogueClone2 と同じ名前。
    // 番号がずれないよう末尾に足す（割り当ての保存を崩さない）。
    // 回数は数値の枠で入れ、決定の後でコマンドを選ぶ（prepare_sources.py の command.c 手当て）。
    add('0', act, move, { "繰り返し回数を指定", "Set a repeat count" });
    /*
     * アイテムを使うコマンド（2026-10-02。決めたこと「メニューとキー割り当てに組み込む。
     * 行動メニューの中に入れて」）。c40c1b3d47 で持ち物一覧の項目操作だけへ移していた。
     * 番号がずれないよう末尾に足す。持ち物一覧はキーで最初の項目を引くので、
     * 下の隠しグループと重なっても害は無い。
     */
    add('t', act, items, { "投げる", "Throw" });
    add('q', act, items, { "飲む", "Quaff" });
    add('r', act, items, { "読む", "Read" });
    add('e', act, items, { "食べる", "Eat" });
    add('z', act, items, { "杖を振る", "Zap a wand" });
    if (!is_54) add('p', act, items, { "方向を指定して杖を振る", "Zap a wand in a direction" });
    if (is_54) add(' ', act, move, { "何もしない", "Do nothing" });

    if (!with_hidden) return commands;
    int hidden_id = 10000;
    const auto add_inventory_action = [&](char key, Bi label) {
        add_command(commands, hidden_id++, key, "@inventory-action", "", text(label));
    };
    add_inventory_action('I', { "詳しく調べる", "Examine" });
    add_inventory_action('t', { "投げる", "Throw" });
    add_inventory_action('d', { "捨てる", "Drop" });
    add_inventory_action('c', { "名前を付ける", "Name it" });
    add_inventory_action('e', { "食べる", "Eat" });
    add_inventory_action('q', { "飲む", "Quaff" });
    add_inventory_action('r', { "読む", "Read" });
    add_inventory_action('z', { "杖を振る", "Zap" });
    if (!is_54) add_inventory_action('p', { "方向を指定して杖を振る", "Zap in a direction" });
    add_inventory_action('w', { "武器として装備", "Wield as a weapon" });
    add_inventory_action('W', { "防具を装備", "Wear armor" });
    add_inventory_action('T', { "装備を外す", "Take off" });
    add_inventory_action('P', { "指輪をはめる", "Put on a ring" });
    add_inventory_action('R', { "指輪を外す", "Remove a ring" });
    return commands;
}

int run_protocol_handles(presentation::TransportHandle in, presentation::TransportHandle out)
{
    const std::uint64_t generation = g_generation.fetch_add(1) + 1;
    g_ui_gone.store(false);
    g_quit_requested.store(false);
    g_frame_id = 0;
    g_last_frame.clear();
    rogue_legacy::reset_frame_state(); // 前のゲームの「自キャラを見た」「ゲーム終了」を引き継がない（Android は同じプロセスで起こし直す）
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_keys.clear();
    }
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
    ack.core_name = RL_CORE_NAME;
    ack.core_version = RL_CORE_VERSION;
    ack.asset_root_slab = presentation::rogue_borrowed_slab_dir(); // Frox の板を借りる（rogue_borrowed_tiles.h）
    if (!send_message(presentation::encode_hello_ack(ack))) return 5;
    const auto assets = presentation::build_rogue_asset_manifest();
    if (!send_message(presentation::encode_asset_manifest(assets))) return 5;

    // 握手の時点では画面の言語がまだ届いていないので、日本語で送る。ui_state で英語と分かったら送り直す。
    // 番号（id）とコマンド文字は言語によらず同じ（割り当ての保存を崩さない）。
    const bool is_54 = std::string_view(RL_CORE_VERSION).starts_with("5.");
    presentation::PadCommandsMessage commands = build_rogue_pad_commands(false, is_54, false);
    {
        std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
        g_pad_commands = build_rogue_pad_commands(false, is_54, true);
    }
    if (!send_message(presentation::encode_pad_commands(commands))) return 5;
    // 握手の時点の言語は日本語（上のコマンド表と同じ）。英語と分かったら ui_state の受信で送り直す。
    const presentation::SubPanelKindsMessage sub_panels = presentation::rogue_sub_panel_kinds(false);
    if (!send_message(presentation::encode_sub_panel_kinds(sub_panels))) return 5;

    std::thread(receive_loop, generation, g_in).detach();
    rl_host_hooks hooks{};
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    rl_set_host_hooks(&hooks);
    const std::string save_path = configure_storage();
    /*
     * HD2D: 原作の既定名は OS のアカウント名（md_getusername()）。Windows の実名が
     * 墓標とスコアに出てしまう問題があったため、PlayerIdentity の名前を既定値として
     * 渡す（設計の判断、2026-09-24）。名前は原作どおり設定画面（o コマンド）で
     * 変えられるので、ここは初期値だけを決める。
     * アプリ側の PlayerIdentity にまだ名前が無いとき（初回起動など）は、
     * 仮の名前 "HD2D" を出さず、原作の既定名 "nobody" に合わせる
     * （rogue54/mdport.c の md_getusername、USERNAME→LOGNAME→USER が無ければ
     * "nobody"）。OS のアカウント名は絶対に使わない（rogue_restore 第 2 回・
     * 項目 1。PlayerIdentity に名前が設定済みならそちらを使う従来動作は変えない）。
     */
    presentation::player_identity_activate(identity_core_key(), "rogue.save", "nobody", "nobody");
    {
        const auto id = presentation::player_identity_snapshot();
        const std::string default_name = presentation::player_identity_core_name(
            id.display_name_utf8.empty() ? id.core_name_ascii : id.display_name_utf8, 24);
        rl_set_player_name(default_name.c_str());
    }
    {
        /*
         * HD2D: 好きな果物の既定値（項目 2）。`ui_state` は握手直後に画面が
         * 必ず 1 回送るので、届くまで短く
         * 待つ。届かなければ既存の既定と同じ「ja」のまま進む（原作の
         * "slime-mold" 側は main.c 手当てで env/ROGUEOPTS を優先するので、
         * ここで決め切れなくても壊れない）。
         */
        for (int waited_ms = 0; waited_ms < 300 && !g_ui_state_received.load(); waited_ms += 5) {
            portable::sleep_ms(5);
        }
        std::string lang;
        {
            std::lock_guard<std::mutex> lang_lock(g_ui_lang_mutex);
            lang = g_ui_lang;
        }
        // 「スライムモールド」(UTF-8)。
        static constexpr char kJaSlimeMold[] =
            "\xe3\x82\xb9\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\xa0\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\xab\xe3\x83\x89";
        rl_set_default_fruit(lang == "ja" ? kJaSlimeMold : "slime-mold");
    }

    char program[] = RL_CORE_NAME;
    char *argv[] = { program, nullptr, nullptr };
    int game_argc = 1;
    std::string restore_path;
    if (!save_path.empty()) {
        std::error_code ec;
        /*
         * `save_path` は `portable::data_dir()` から作るので、Windows では ANSI（CP932）である。
         * `u8path` は UTF-8 として読み、日本語の名前のフォルダ（`C:\ゲーム\…`）では libc++ が
         * 「locale not supported」を投げてコアが落ちていた（2026-10-06）。`path(std::string)` は
         * Windows では ANSI として、それ以外ではバイト列のまま受けるので、どちらでも正しい。
         */
        if (std::filesystem::is_regular_file(std::filesystem::path(save_path), ec) && !ec) {
            restore_path = save_path;
            argv[1] = restore_path.data();
            game_argc = 2;
        }
    }
    /*
     * HD2D: 空の envp を渡すと、復元処理の `environ = envp;`（3.6 save.c:216、
     * 5.4 save.c:259）でプロセス全体の環境変数がここで消えていた。Android はコアが
     * ホストプロセスの中の1スレッドなので、他機能の環境も壊しうる不具合だった
     * （監査で見つかった不具合、2026-09-24）。`environ` は Windows（_environ）と
     * Android で綴りが違うため、ここで実環境を渡す代わりに、その代入自体を
     * prepare_sources.py の save.c 手当てで外した。envp はここでは使われないので
     * 空のままでよい。
     */
    char *envp[] = { nullptr };
    const auto run_started = std::filesystem::file_time_type::clock::now();
    const int game_rc = rl_run_game(RL_GAME_MAIN, game_argc, argv, envp);
    rl_set_host_hooks(nullptr);
    // 窓を閉じたときの 3.6 の自動セーブは、保存できても exit(1) で終わる。保存ファイルを見て 0 に直す（修-49）。
    const int rc = exit_code_after_autosave(game_rc, rl_shutdown_autosave_take() != 0,
        std::string_view(RL_CORE_VERSION).starts_with("5."), std::filesystem::path(save_path), run_started,
        rl_term_rows(), rl_term_cols());
    presentation::ExitMessage exit_message;
    exit_message.code = rc;
    (void)send_message(presentation::encode_exit(exit_message));
    return rc;
}

} // namespace

#if !defined(_WIN32)
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    (void)protocol_log_path;
    (void)forwarded_args;
    return run_protocol_handles(in, out);
}
#endif

/*
 * 原作の字を 1 行ずつ受け取り、訳が当たるかを答える（開発用）。
 * 画面に出る字は必ず rl_ja_text() を通るので、ここで同じ関数に通せば
 * 「遊ばないと分からない英語の残り」を数えられる。
 */
int run_i18n_scan()
{
    std::string line;
    int total = 0, missing = 0;
    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        ++total;
        const char *ja = rl_ja_text(line.c_str());
        const bool hit = ja != nullptr && line != ja;
        if (!hit) ++missing;
        std::printf("%s\t%s\t%s\n", hit ? "HIT" : "MISS", line.c_str(), hit ? ja : "");
    }
    std::fprintf(stderr, "i18n scan: %d lines, %d untranslated\n", total, missing);
    return 0;
}

/*
 * 窓を閉じたときの終了コードの判定（exit_code_after_autosave）を、一時フォルダの保存ファイルで確かめる
 * （修-49 の再発を見る手段。ゲームは起こさない）。成功は 0、失敗（保存ファイルが無い・小さい・古い）は
 * 原作の 1 のまま、自動セーブを通っていない終わり方・5.4・もともと 0 は触らない。
 */
int run_exit_code_selftest()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const int rows = 24, cols = 80;
    const std::uintmax_t least = 80u * 3u + 3u * (12u + 4u * rows * cols);
    const fs::path dir = fs::temp_directory_path(ec)
        / ("rl_exit_selftest_" + std::to_string(static_cast<unsigned long long>(fs::file_time_type::clock::now().time_since_epoch().count())));
    if (ec || !fs::create_directory(dir, ec)) {
        std::fprintf(stderr, "exit code judgement: FAILED (cannot make a temporary folder)\n");
        return 1;
    }
    const fs::path file = dir / "rogue.save";
    const auto make = [&](std::uintmax_t bytes) {
        std::FILE *f = std::fopen(file.string().c_str(), "wb");
        if (f == nullptr) return;
        const std::string block(4096, 'x');
        for (std::uintmax_t left = bytes; left > 0;) {
            const std::size_t n = static_cast<std::size_t>(left < block.size() ? left : block.size());
            std::fwrite(block.data(), 1, n, f);
            left -= n;
        }
        std::fclose(f);
    };
    int failed = 0;
    const auto check = [&](const char *what, int got, int want) {
        if (got == want) return;
        ++failed;
        std::fprintf(stderr, "exit code judgement: %s: got %d, want %d\n", what, got, want);
    };
    const auto before = fs::file_time_type::clock::now();
    const auto judge = [&](int rc, bool ran, bool is_54, fs::file_time_type started) {
        return exit_code_after_autosave(rc, ran, is_54, file, started, rows, cols);
    };
    check("save file missing", judge(1, true, false, before), 1);
    make(100);
    check("tiny save file", judge(1, true, false, before), 1);
    make(least - 1);
    check("one byte short", judge(1, true, false, before), 1);
    make(least);
    check("complete save file", judge(1, true, false, before), 0);
    make(least + 3000);
    check("larger save file", judge(1, true, false, before), 0);
    check("stale save file", judge(1, true, false, before + std::chrono::hours(1)), 1);
    check("exit without autosave", judge(1, false, false, before), 1);
    check("5.4 untouched", judge(1, true, true, before), 1);
    check("already 0", judge(0, true, false, before), 0);
    fs::remove(file, ec);
    fs::remove(dir, ec);
    std::fprintf(stderr, "exit code judgement: %s\n", failed == 0 ? "ok" : "FAILED");
    return failed == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    bool protocol = false;
    bool exit_selftest = false;
    bool i18n_selftest = false;
    bool i18n_scan = false;
    bool status_selftest = false;
    bool fx_selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == kUiProtocolArg) protocol = true;
        if (std::string_view(argv[i]) == "--selftest-i18n") i18n_selftest = true;
        if (std::string_view(argv[i]) == "--i18n-scan") i18n_scan = true;
        if (std::string_view(argv[i]) == "--selftest-status") status_selftest = true;
        if (std::string_view(argv[i]) == "--selftest-fx") fx_selftest = true;
        if (std::string_view(argv[i]) == "--selftest-exit") exit_selftest = true;
    }
    if (exit_selftest) return run_exit_code_selftest();
    if (fx_selftest) return rogue_legacy::attack_fx_selftest();
    if (status_selftest) {
        // 状態のアイコンの集め方（rl_inventory.c）を、未鑑定の指輪・空腹・重なりまで全数で照合する（ゲームの初期化は要らない）。
        const bool ok = rl_status_effects_selftest() == 0;
        std::fprintf(stderr, "status effects: %s\n", ok ? "ok" : "FAILED");
        return ok ? 0 : 1;
    }
    if (i18n_scan) {
        presentation::set_stdio_binary_mode();
        return run_i18n_scan();
    }
    if (i18n_selftest) {
        const bool ok = rl_i18n_selftest() != 0;
        std::fprintf(stderr, "i18n messages: %s\n", ok ? "ok" : "FAILED");
        return ok ? 0 : 1;
    }
    if (!protocol) {
        std::fprintf(stderr, "%s --ui-protocol=stdio\n", RL_CORE_NAME);
        return 2;
    }
    presentation::set_stdio_binary_mode();
    return run_protocol_handles(presentation::transport_stdin(), presentation::transport_stdout());
}
