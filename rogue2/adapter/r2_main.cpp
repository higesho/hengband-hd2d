#include "r2_frame.h"
#include "r2_host.h"
#include "r2_shim.h"
#include "compat/curses.h"

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
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern "C" void r2_autosave_set_path(const char *path);
extern "C" void r2_options_set_path(const char *path);
extern "C" void r2_options_load(void);
extern "C" void r2_autosave_invalidate(void);

extern "C" void r2_message_history_push(const char *text);
extern "C" const char *r2_byebye_text(void);
extern "C" void r2_farewell_show(const char *text);

/*
 * HD2D: OS のアカウント名の代わりに PlayerIdentity の名前を既定値として machdep.c の
 * md_gln へ渡す。ゲーム全体が UTF-8 なので表示名をそのまま
 * 使い、login_name[30] に収まるよう 29 バイトで（UTF-8 の文字の途中では切らずに）打ち切る。
 * 空文字列なら「無い」で、md_gln() は原作の既定値「A FIGHTER」へ落ちる。
 * 名前は原作どおり設定画面（o コマンド）で変えられる。
 */
extern "C" const char *r2_host_player_name(void)
{
    static std::string name;
    const auto id = presentation::player_identity_snapshot();
    name = id.display_name_utf8;
    constexpr std::size_t kMaxBytes = 29; // login_name[30] - 1
    if (name.size() > kMaxBytes) {
        std::size_t cut = kMaxBytes;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xc0u) == 0x80u) --cut;
        name.resize(cut);
    }
    return name.c_str();
}

namespace {

constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
constexpr std::string_view kMessageArg = "--mesg=";

presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;
std::mutex g_input_mutex;
std::mutex g_modern_mutex;
struct QueuedKey {
    int key{};
    bool keep_through_continue{};
};
std::deque<QueuedKey> g_keys;
std::atomic<bool> g_ui_gone{ false };
std::atomic<bool> g_quit_requested{ false };
std::atomic<bool> g_refresh_requested{ false };
//! 画面が選んだ言語が英語か（ui_state の lang）。コマンド表の再送とコア側の mesg の差し替えを決める。
std::atomic<bool> g_language_en{ false };
/*
 * 設定一覧（settings_list_rogue）を cancel で閉じている間は、原作の options() が
 * 終わるまで（r2_term_modal_active() が偽になるまで）フレームを送らない。
 * options() は抜ける前に print_stats() で refresh() するので、設定の字と状態の行が
 * 混ざった Term が「document」の窓として 1 枚送られ、タブを移る途中に別の画面が
 * 挟まっていた（気づいたこと 2026-09-26「タブを切り替える際に一度別の画面を一瞬挟んでないか」。
 * tools/hd2d_verify/menu_tabs_audit.py）。原作がキーを待つ所まで来たら必ず送る（host_next_key）。
 */
std::atomic<bool> g_closing_rogue_settings{ false };
std::uint64_t g_frame_id = 0;
std::string g_last_frame_key;
presentation::NativeModernMenu g_native_modern;
presentation::NativeInventoryMenu g_inventory_menu;
presentation::PadCommandsMessage g_pad_commands;
//! 画面が選んだサブパネルの種類（`ui_state.sub_panel_kinds`）。g_modern_mutex で守る。
std::array<int, kSubPanelCount> g_sub_panel_kinds = { -1, -1, -1, -1, -1, -1, -1 };

std::string frame_compare_key(const std::string &payload)
{
    static const std::string marker = "\"frame_id\":";
    const auto pos = payload.find(marker);
    if (pos == std::string::npos) return payload;
    auto end = pos + marker.size();
    while (end < payload.size() && payload[end] >= '0' && payload[end] <= '9') ++end;
    std::string key = payload;
    key.erase(pos + marker.size(), end - (pos + marker.size()));
    return key;
}

bool send_message(const char *type, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[rogue2] send %s failed: %s\n", type, err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    return true;
}

void send_fatal(const std::string &reason)
{
    presentation::FatalMessage fatal;
    fatal.reason = reason;
    std::fprintf(stderr, "[rogue2] fatal: %s\n", reason.c_str());
    (void)send_message("fatal", presentation::encode_fatal(fatal));
}

void enqueue(const std::vector<int> &keys)
{
    std::lock_guard<std::mutex> lock(g_input_mutex);
    for (const int key : keys) {
        if (key > 0 && key <= 255) g_keys.push_back(QueuedKey{ key, false });
    }
}

char rogue_direction_key(int dx, int dy)
{
    static constexpr char table[3][3] = {
        { 'y', 'k', 'u' },
        { 'h', '.', 'l' },
        { 'b', 'j', 'n' },
    };
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
    return table[dy + 1][dx + 1];
}

bool append_repeat_number_keys(int number, std::vector<int> &keys)
{
    if (number < 1 || number > 999) return false;
    keys.push_back(0x15); // Ctrl-U: clear the accumulated Rogue repeat prefix.
    const std::string digits = std::to_string(number);
    for (const unsigned char digit : digits) keys.push_back(digit);
    keys.push_back('\r');
    return true;
}

void append_rogue_input_event_keys(const presentation::InputEventsMessage &message, std::vector<int> &out)
{
    /*
     * 共通の入力変換は move をテンキー '1'..'9' にするが、RogueClone2 では数字は
     * 「次の命令を何回繰り返すか」の前置きであって移動ではない。矢印・テンキー・
     * ゲームパッドから来る意味付き move だけを Rogue 本来の 8 方向へ変換する。
     * 生のキー列は触らないので h/j/k/l/y/u/b/n や数字の回数指定も従来どおり使える。
     */
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

bool continuation_prompt_visible()
{
    /*
     * 原作の wait_for_ack() は Space だけを受け付ける。日本語版の「［続く］」と、
     * 将来英語メッセージを扱う場合の "More" の両方を見て、HD2D の決定キーも
     * Space として扱えるようにする。通常画面では Enter の意味を変えない。
     */
    bool has_continue_kanji = false;
    std::string ascii;
    for (int col = 0; col < r2_term_cols(); ++col) {
        const uint32_t cp = r2_term_codepoint(0, col);
        if (cp == 0x7d9a) has_continue_kanji = true; // 続
        if (cp >= 0x20 && cp <= 0x7e) ascii.push_back(static_cast<char>(cp));
    }
    return has_continue_kanji || (ascii.find("More") != std::string::npos);
}

bool direction_continue_marker_visible()
{
    /*
     * 方向入力を Space へ変換してよいのは、画面上に実際の
     * [続く] / ［続く］ が出ている時だけ。単なる "More" や
     * "Press space to continue" まで広げると、通常の方向入力を
     * 誤って継続キーとして消費する。
     */
    std::vector<uint32_t> row;
    row.reserve(static_cast<std::size_t>(r2_term_cols()));
    for (int col = 0; col < r2_term_cols(); ++col) {
        row.push_back(r2_term_codepoint(0, col));
    }
    for (std::size_t i = 0; i + 3 < row.size(); ++i) {
        const bool open = row[i] == '[' || row[i] == 0xff3b;
        const bool close = row[i + 3] == ']' || row[i + 3] == 0xff3d;
        if (open && row[i + 1] == 0x7d9a && row[i + 2] == 0x304f && close) return true;
    }
    return false;
}

/*
 * コマンドメニュー（パッドのコマンド表）。言語ごとに作り直して送る（修-56）。
 * **割り当ての番号（id）は言語によらず同じ**にする（画面側が割り当てを id で保存している）。
 * 変えるのは group_utf8・subgroup_utf8・label_utf8 だけ。hidden_id の連番も同じ並びで振る。
 */
struct PadCommandSet {
    presentation::PadCommandsMessage visible; //!< 画面へ送る分
    presentation::PadCommandsMessage full;    //!< 持ち物の項目操作の隠しグループを足した分（g_pad_commands）
};

PadCommandSet build_pad_commands(bool english)
{
    const auto L = [english](const char *japanese, const char *en) { return english ? en : japanese; };
    const char *const g_action = L("行動", "Actions");
    const char *const g_info = L("情報", "Info");
    const char *const g_settings = L("設定", "Settings");
    const char *const s_move = L("移動", "Movement");
    const char *const s_search = L("探索", "Search");
    const char *const s_fight = L("戦闘", "Combat");
    const char *const s_system = L("システム", "System");
    const char *const s_item = L("アイテム", "Items");

    presentation::PadCommandsMessage commands;
    commands.current_keymap = "rogue";
    const auto add_command = [&commands](int id, char command, const char *group, const char *subgroup, const char *label) {
        presentation::PadCommandWireEntry entry;
        entry.id = id;
        entry.command = static_cast<unsigned char>(command);
        entry.group_utf8 = group;
        entry.subgroup_utf8 = subgroup != nullptr ? subgroup : "";
        entry.label_utf8 = label;
        entry.seq_original = { static_cast<unsigned char>(command) };
        entry.seq_rogue = entry.seq_original;
        commands.entries.push_back(std::move(entry));
    };
    add_command(2, '>', g_action, s_move, L("階段を降りる", "Descend stairs"));
    add_command(12, '<', g_action, s_move, L("階段を上る", "Ascend stairs"));
    add_command(13, ',', g_action, s_move, L("足元の物を拾う", "Pick up item underfoot"));
    add_command(28, 'm', g_action, s_move, L("物の上へ移動する", "Move onto an object"));
    add_command(34, 'a', g_action, s_move, L("直前のコマンドを繰り返す", "Repeat previous command"));
    add_command(35, '.', g_action, s_move, L("その場で休む", "Rest in place"));
    add_command(45, '0', g_action, s_move, L("繰り返し回数を指定", "Set repeat count"));
    add_command(14, 's', g_action, s_search, L("周囲を探索する", "Search around"));
    add_command(17, '^', g_action, s_search, L("罠を調べる", "Identify a trap"));
    add_command(15, 'f', g_action, s_fight, L("指定方向へ連続攻撃", "Attack repeatedly in a direction"));
    add_command(26, 'F', g_action, s_fight, L("指定方向へ移動しながら戦う", "Fight while moving in a direction"));
    /*
     * 走行は原作では H〜N（物の所で止まる）と ^H〜^N（通路の角も曲がる）で、キーが方向そのもの。
     * パッドからは rogue2.json の再構成が play.c に足した ^F（方向を聞いてから走る）で送る。
     * 同じく足した ^R（物の所で止まる走り）は、原作の rgetchar()（message.c）が ^R を画面の描き直し
     * として先に受け取り、呼び出し元の play.c へ返さないので届かない。項目を出しても何も起きない
     * ため、メニューから外した（修-50。id 46 は欠番。原作とレシピは触らない）。
     * 物の所で止まる走りは、キーボードの H〜N（Shift＋方向）で使える。
     */
    add_command(47, static_cast<char>(0x06), g_action, s_move, L("指定方向へ走る（角も曲がる）", "Run in a direction (turns corners)"));
    add_command(24, 'S', g_action, s_system, L("保存して終了", "Save and quit"));
    add_command(25, 'Q', g_action, s_system, L("ゲームを終了", "Quit the game"));
    add_command(19, 'v', g_info, "", L("RogueClone2の版情報", "RogueClone2 version info"));
    add_command(20, 'D', g_info, "", L("発見済みの物を種類別に表示", "List discovered items by type"));
    add_command(21, '/', g_info, "", L("記号の意味を調べる", "Look up a map symbol"));
    add_command(22, '@', g_info, "", L("現在の状態を表示", "Show current status"));
    add_command(48, ')', g_info, "", L("装備中の武器を表示", "Show wielded weapon"));
    add_command(49, ']', g_info, "", L("装備中のよろいを表示", "Show worn armor"));
    add_command(50, '=', g_info, "", L("装備中の指輪を表示", "Show worn rings"));
    add_command(51, static_cast<char>(0x10), g_info, "", L("直前のメッセージを再表示", "Show previous message again"));
    add_command(52, '*', g_info, "", L("得点表を見る", "View score list"));
    add_command(33, '?', g_info, "", L("原作のヘルプ", "Original help"));
    add_command(44, static_cast<char>(0x01), g_info, "", L("平均HPを表示", "Show average HP"));
    add_command(23, 'o', g_settings, "", L("RogueClone2の設定", "RogueClone2 options"));
    /*
     * アイテムを使うコマンド（2026-10-02。決めたこと「メニューとキー割り当てに組み込む。
     * 行動メニューの中に入れて」）。c40c1b3d47 で持ち物一覧の項目操作だけへ移していた。
     * 持ち物一覧はキーで最初の項目を引くので、下の隠しグループと重なっても害は無い。
     */
    add_command(53, 'e', g_action, s_item, L("食べる", "Eat"));
    add_command(54, 'q', g_action, s_item, L("飲む", "Quaff"));
    add_command(55, 'r', g_action, s_item, L("読む", "Read"));
    add_command(56, 'd', g_action, s_item, L("捨てる", "Drop"));
    add_command(57, 'z', g_action, s_item, L("杖を振る", "Zap a wand"));
    add_command(58, 't', g_action, s_item, L("投げる", "Throw"));
    add_command(59, ' ', g_action, s_move, L("何もしない", "Do nothing"));

    PadCommandSet set;
    set.visible = commands;
    set.full = commands;
    int hidden_id = 10000;
    const auto add_inventory_action = [&](char command, const char *label) {
        presentation::PadCommandWireEntry entry;
        entry.id = hidden_id++;
        entry.command = static_cast<unsigned char>(command);
        entry.group_utf8 = "@inventory-action";
        entry.label_utf8 = label;
        entry.seq_original = { static_cast<unsigned char>(command) };
        entry.seq_rogue = entry.seq_original;
        set.full.entries.push_back(std::move(entry));
    };
    add_inventory_action('e', L("食べる", "Eat"));
    add_inventory_action('q', L("飲む", "Quaff"));
    add_inventory_action('r', L("読む", "Read"));
    add_inventory_action('z', L("杖を振る", "Zap a wand"));
    add_inventory_action('w', L("武器として装備", "Wield as weapon"));
    add_inventory_action('W', L("防具を装備", "Wear armor"));
    add_inventory_action('T', L("装備を外す", "Take off"));
    add_inventory_action('P', L("指輪をはめる", "Put on ring"));
    add_inventory_action('R', L("指輪を外す", "Remove ring"));
    add_inventory_action('t', L("投げる", "Throw"));
    add_inventory_action('I', L("詳しく調べる", "Inspect"));
    add_inventory_action('c', L("名前を付ける", "Name it"));
    add_inventory_action('d', L("捨てる", "Drop"));
    return set;
}

void receive_loop()
{
    while (!g_ui_gone.load()) {
        std::string payload;
        std::string err;
        const auto result = presentation::read_message(g_in, payload, err);
        if (result != presentation::TransportResult::Ok) {
            if (result != presentation::TransportResult::Eof) {
                std::fprintf(stderr, "[rogue2] receive stopped: %s - %s\n",
                    presentation::transport_result_name(result), err.c_str());
            }
            g_ui_gone.store(true);
            return;
        }
        const std::string type = presentation::peek_message_type(payload);
        if (type == "keys") {
            presentation::KeysMessage message;
            if (presentation::decode_keys(payload, message, err)) enqueue(message.keys);
            continue;
        }
        if (type == "input_event") {
            presentation::InputEventsMessage message;
            if (presentation::decode_input_events(payload, message, err)) {
                std::vector<int> keys;
                presentation::InputEventsMessage ordinary;
                for (const auto &event : message.events) {
                    if (event.e == "move" && (event.dx != 0 || event.dy != 0) && direction_continue_marker_visible()) {
                        // 地図を残したまま出る戦闘メッセージの［続く］も、
                        // 決定と同じく方向入力1回で先へ進める。原作 wait_for_ack()
                        // が受ける実キーは Space のままなのでゲーム側の意味は変えない。
                        keys.push_back(' ');
                        continue;
                    }
                    /*
                     * 文字入力の間の方向は文字にしない（Rogue 3.6/5.4 と同じ手当て。上が k として
                     * 入力欄へ入っていた。気づいたこと 2026-09-26）。欄の中の移動は画面側が持つ。
                     */
                    if (event.e == "move" && r2_term_text_input_active()) continue;
                    if (event.e == "ui_action" && event.action == "number" && r2_term_numeric_active()
                        && event.number >= 1 && event.number <= 999) {
                        (void)append_repeat_number_keys(event.number, keys);
                        continue;
                    }
                    if (event.e == "ui_action") {
                        std::vector<int> modern_keys;
                        bool handled = false;
                        bool handled_by_native_modern = false;
                        bool rogue_settings = false;
                        {
                            /*
                             * input() の判定（pending_=true など）から、そのキーが実際に
                             * g_keys へ積まれるまでを同じ g_modern_mutex の区間に収める。
                             * 前は区間の外（ループを抜けたあと）でまとめて enqueue しており、
                             * その隙間でゲームスレッドの host_present()/capture() が
                             * 「まだキーは届いていないのに pending_=true だけが見える」
                             * 状態を観測できた。原作はまだ直前のコマンド境界のままなので
                             * capture() が command_boundary を真と判定し、一覧の再オープン
                             * 処理が pending_ を即座に取り消してしまう（Linux 版
                             * rogue2/protocol_audit.py の equipment_audit が装備の持ち替えで
                             * 再現。Windows は隙間に間に合わず見えなかった）。
                             * g_input_mutex は g_modern_mutex より内側でだけ取る
                             * （host_next_key() は逆順に取らない）ので、ここで入れ子にしても
                             * デッドロックしない。
                             */
                            std::lock_guard<std::mutex> lock(g_modern_mutex);
                            handled = g_inventory_menu.input(event, modern_keys);
                            if (!handled) {
                                handled_by_native_modern = g_native_modern.input(event, modern_keys);
                                handled = handled_by_native_modern;
                                rogue_settings = g_native_modern.rogue_settings_active();
                            }
                            if (handled) {
                                /*
                                 * NativeModernMenu は capture() 時点の continue_key を内部にも保持する。
                                 * RogueClone2 の受動的な一覧は、その後フレーム表示だけを Space に補正していたため、
                                 * UI には Space と見せつつ内部では Enter を返す不一致が起きていた。
                                 * 原作が wait_for_ack() 中なら、受理済み continue の実キーも必ず Space に揃える。
                                 */
                                if (event.action == "continue" && r2_term_modal_active() && !modern_keys.empty()) {
                                    modern_keys.assign(1, ' ');
                                }
                                /*
                                 * RogueClone2 の受動的な全画面一覧・説明は wait_for_ack() で
                                 * Space だけを待つ。共通 UI の「戻る」は Cancel=Esc なので、
                                 * そのまま渡すと原作側が Esc を読み捨てて同じ画面へ戻り続ける。
                                 *
                                 * ただし options() の設定一覧そのものでは Esc が正規の終了キー。
                                 * また文字/数値/1文字/持ち物プロンプトにもそれぞれ固有の Cancel が
                                 * あるため、ここで Space にするのは「受動的な modal 待ち」だけに限る。
                                 */
                                if (handled_by_native_modern && event.action == "cancel" && r2_term_modal_active()
                                    && !rogue_settings
                                    && !r2_term_text_input_active() && !r2_term_numeric_active()
                                    && r2_term_char_prompt_kind() == 0 && r2_term_pack_prompt_mask() == 0u
                                    && modern_keys.size() == 1 && modern_keys.front() == 27) {
                                    modern_keys.front() = ' ';
                                }
                                /*
                                 * inventory_open / cancel / preview-style Modern
                                 * actions can change only adapter-side UI state and
                                 * deliberately emit no legacy game key.  The game
                                 * thread is blocked in next_key() at that point, so
                                 * request one capture explicitly or the accepted UI
                                 * transition is never sent back to the frontend.
                                 */
                                if (modern_keys.empty()) {
                                    g_refresh_requested.store(true);
                                } else {
                                    if (handled_by_native_modern && rogue_settings && event.action == "cancel") {
                                        //! 旗とキーは同じ区間で立てる（host_next_key が「キーが無いのに旗だけ」を見ない）。
                                        std::lock_guard<std::mutex> input_lock(g_input_mutex);
                                        g_closing_rogue_settings.store(true);
                                        for (const int key : modern_keys) {
                                            if (key > 0 && key <= 255) g_keys.push_back(QueuedKey{ key, false });
                                        }
                                    } else {
                                        enqueue(modern_keys);
                                    }
                                }
                            }
                        }
                        if (handled) {
                            continue;
                        }
                    }
                    ordinary.events.push_back(event);
                }
                append_rogue_input_event_keys(ordinary, keys);
                enqueue(keys);
            }
            continue;
        }
        if (type == "ui_state") {
            presentation::UiStateMessage state;
            if (presentation::decode_ui_state(payload, state, err)) {
                std::lock_guard<std::mutex> lock(g_modern_mutex);
                const std::string language = state.lang.empty() ? "ja" : state.lang;
                g_native_modern.set_enabled(state.modern_ui);
                g_native_modern.set_language(language);
                const bool english = (language == "en");
                if (english != g_language_en.load()) {
                    /*
                     * 言語が替わった（修-56）。mesg の差し替えと状態の行の描き直しはゲームのスレッドが行う
                     * （r2_language_service）。コマンドメニューはここで作り直して送り直す。割り当ての番号は同じ。
                     */
                    g_language_en.store(english);
                    r2_set_language_english(english ? 1 : 0);
                    PadCommandSet set = build_pad_commands(english);
                    g_pad_commands = set.full;
                    (void)send_message("pad_commands", presentation::encode_pad_commands(set.visible));
                    (void)send_message("sub_panel_kinds", presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(english)));
                }
                g_native_modern.set_document_profile("rogue");
                g_inventory_menu.configure(state.modern_ui, language, "rogue");
                if (state.has_sub_panel_kinds) {
                    for (int i = 0; i < kSubPanelCount; ++i) g_sub_panel_kinds[static_cast<std::size_t>(i)] = state.sub_panel_kinds[i];
                }
                /*
                 * 効果音を誰が鳴らすか。RogueClone2 は自分では
                 * 鳴らせないので、真なら `frame.sounds` へ書き留め、偽なら無音になる
                 * （Frox・Sil-Q と同じ流儀）。
                 */
                r2_sound_set_wanted(state.sound_events ? 1 : 0);
                g_refresh_requested.store(true);
            }
            continue;
        }
        if (type == "quit_request") {
            g_quit_requested.store(true);
            continue;
        }
        /* Unknown messages are ignored for forward-compatible protocol extensions. */
    }
}

void host_present()
{
    if (g_out == nullptr || g_ui_gone.load()) return;
    if (g_closing_rogue_settings.load()) {
        if (r2_term_modal_active()) return;
        g_closing_rogue_settings.store(false);
    }
    GameFrame frame = rogue2::capture_frame(g_frame_id + 1);
    {
        std::lock_guard<std::mutex> lock(g_modern_mutex);
        std::vector<presentation::NativeItemRow> prompt_items;
        const unsigned int pack_mask = r2_term_pack_prompt_mask();
        if (pack_mask != 0u) {
            auto rows = rogue2::inventory_rows(pack_mask);
            prompt_items.reserve(rows.size());
            for (auto &row : rows) prompt_items.push_back({ std::move(row), 0, -1, 1 });
        }
        g_native_modern.capture(frame, {}, prompt_items);
        g_inventory_menu.capture(frame, [] { return rogue2::inventory_rows(); }, [] { return g_pad_commands; });
        presentation::apply_rogue_sub_panel_kinds(frame, g_sub_panel_kinds);
        /*
         * RogueClone2 の一覧・説明画面は wait_for_ack() を使い、Space だけを
         * 受け付けるものが多い。共通変換は入力情報のない detail を既定で
         * Enter にするため、RogueClone2 の受動的な詳細画面だけ Space に補正する。
         */
        if (frame.menu_open && frame.modern.stage == "detail"
            && frame.modern.continue_key == 13 && frame.modern.actions.empty()) {
            frame.modern.continue_key = ' ';
        }
    }
    // 魔法・投射物の効果は、フレームを送ると決まったこの時点で汲む。
    rogue2::fill_attack_fx(frame);
    const std::string wire = presentation::encode_game_frame(frame);
    const std::string key = frame_compare_key(wire);
    if (key == g_last_frame_key) return;
    ++g_frame_id;
    frame.frame_id = g_frame_id;
    const std::string final_wire = presentation::encode_game_frame(frame);
    g_last_frame_key = frame_compare_key(final_wire);
    (void)send_message("frame", final_wire);
}

/*
 * 原作が wait_for_ack() で Space だけを待っている間か。設定の一覧（options()。決定・取消が
 * 正規のキー）、文字・数値・1 文字・持ち物の問いは、それぞれ固有の決定・取消を持つので除く。
 * 画面側の UI を使わない設定（modern_ui が偽）では原作のキーをそのまま通す。
 */
bool passive_ack_wait()
{
    if (!r2_term_modal_active()) return false;
    if (r2_term_text_input_active() || r2_term_numeric_active()
        || r2_term_char_prompt_kind() != 0 || r2_term_pack_prompt_mask() != 0u) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_modern_mutex);
    return g_native_modern.enabled() && !g_native_modern.rogue_settings_active();
}

int host_next_key()
{
    r2_language_service(); // 言語の反映（mesg の差し替え・状態の行の描き直し）。ゲームのスレッドから呼ばれる
    if (g_closing_rogue_settings.load()) {
        bool idle = false;
        {
            std::lock_guard<std::mutex> lock(g_input_mutex);
            idle = g_closing_rogue_settings.load() && g_keys.empty();
        }
        if (idle) {
            //! 設定を閉じきらずにキーを待つ（文字の入力欄の取消など）。そこは画を送る。
            g_closing_rogue_settings.store(false);
            g_last_frame_key.clear();
            host_present();
        }
    }
    if (g_refresh_requested.exchange(false)) {
        g_last_frame_key.clear();
        host_present();
    }
    int key = 0;
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (g_keys.empty()) return 0;
        /*
         * 持ち物画面の「装備する」等は、原作では
         *   コマンド -> 直前メッセージの Space 確認 -> 持ち物の文字
         * の順になることがある。一括で W,b のように送ると b が Space 待ちに消費される。
         * 選択用のキーだけは「［続く］」が見えている間はキューに残し、Space を先に返す。
         */
        if (g_keys.front().keep_through_continue && continuation_prompt_visible()) return ' ';
        key = g_keys.front().key;
        g_keys.pop_front();
        if ((key == '\r' || key == '\n') && (continuation_prompt_visible() || r2_player_hp() <= 0)) key = ' ';
    }
    /*
     * キーボードの `i` などが地図の上へ出す一覧は、原作では wait_for_ack() が Space だけを待つ。
     * 画面側は決定（A・Enter）と取消（B・Esc）で閉じると案内するので、そのまま原作へ渡すと
     * 読み捨てられて一覧が閉じず、止まったように見えた。Space 待ちの間に限って Space へ直す。
     * g_modern_mutex は g_input_mutex の外で取る（受信側は g_modern_mutex の内で g_input_mutex を取る）。
     */
    if ((key == '\r' || key == '\n' || key == 27) && passive_ack_wait()) key = ' ';
    return key;
}

void host_sleep_ms(int milliseconds)
{
    portable::sleep_ms(milliseconds);
}

int host_shutdown_requested()
{
    return (g_ui_gone.load() || g_quit_requested.load()) ? 1 : 0;
}

/*
 * 休息・探索・走行の途中は原作がキーを読まないので、画面側のキャンセル（Esc / Ctrl-C）は
 * キューに溜まるだけだった。先頭にキャンセルが来ていれば取り出して「中断」と伝える。
 * 先頭が別のキー（移動など）なら触らない——原作どおり中断後にそのキーが読まれる。
 */
int host_take_interrupt()
{
    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (g_keys.empty()) return 0;
    const int key = g_keys.front().key;
    if (key != 27 && key != 3) return 0;
    g_keys.pop_front();
    return 1;
}

std::string default_message_path()
{
    const std::string exe = portable::exe_dir();
    if (!exe.empty()) {
        const std::string beside = exe + "mesg";
        if (FILE *fp = std::fopen(beside.c_str(), "rb")) {
            std::fclose(fp);
            return beside;
        }
    }
    const std::string base = portable::data_dir();
    if (base.empty()) return std::string();
    return base + "rogue2" + portable::kPathSep + "mesg";
}

int run_protocol_handles(presentation::TransportHandle in, presentation::TransportHandle out,
    const std::string &message_path)
{
    g_in = in;
    g_out = out;
    if (g_in == nullptr || g_out == nullptr) return 3;

    {
        std::lock_guard<std::mutex> lock(g_modern_mutex);
        g_native_modern.set_enabled(false);
        g_native_modern.set_enabled(true);
        g_native_modern.set_language("ja");
        g_native_modern.set_title("RogueClone2");
        g_native_modern.set_document_profile("rogue");
        g_inventory_menu.configure(true, "ja", "rogue");
    }

    std::string payload;
    std::string err;
    const auto got = presentation::read_message(g_in, payload, err);
    if (got != presentation::TransportResult::Ok) {
        std::fprintf(stderr, "[rogue2] failed to read hello: %s\n", err.c_str());
        return 4;
    }
    if (presentation::peek_message_type(payload) != "hello") {
        send_fatal("expected hello as the first protocol message");
        return 1;
    }
    presentation::HelloMessage hello;
    if (!presentation::decode_hello(payload, hello, err) || hello.protocol != presentation::kProtocolVersion) {
        send_fatal("protocol version mismatch or malformed hello");
        return 1;
    }

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = "RogueClone2";
    ack.core_version = "6.0";
    ack.asset_root_slab = presentation::rogue_borrowed_slab_dir(); // Frox の板を借りる（rogue_borrowed_tiles.h）
    if (!send_message("hello_ack", presentation::encode_hello_ack(ack))) return 5;

    const presentation::AssetManifestMessage assets = presentation::build_rogue_asset_manifest();
    if (!send_message("asset_manifest", presentation::encode_asset_manifest(assets))) return 5;

    {
        PadCommandSet set = build_pad_commands(g_language_en.load());
        {
            std::lock_guard<std::mutex> lock(g_modern_mutex);
            g_pad_commands = set.full;
        }
        if (!send_message("pad_commands", presentation::encode_pad_commands(set.visible))) return 5;
    }

    /*
     * RogueClone2 は Angband 系の独立したサブウインドウを持たない。HD2D 側の固定サブパネル
     * （メッセージ／装備／視界の敵／キャラクター情報／持ち物）だけを使うため、選べる種類は
     * UI 既定だけと明示する。これで旧設定に残った 6/8/4... の内部番号は -1 へ正規化される。
     */
    const presentation::SubPanelKindsMessage sub_panels = presentation::rogue_sub_panel_kinds(g_language_en.load());
    if (!send_message("sub_panel_kinds", presentation::encode_sub_panel_kinds(sub_panels))) return 5;

    std::thread(receive_loop).detach();

    r2_host_hooks hooks{};
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    hooks.take_interrupt = host_take_interrupt;
    r2_set_host_hooks(&hooks);

    const std::string save_dir = portable::data_dir() + "rogue2";
    (void)portable::make_dir(save_dir);
    /*
     * r6 までの autosave.r2 は、画面を 1 バイト文字として保存していたうえ、
     * write_string() が実行中の文字列を直接 XOR していた版で作られている。
     * v4 とは安全に互換にできないため、起動後の restore() で無言終了させるのではなく、
     * 読み込む前に旧名だけを破棄し、最初の起動から新規ゲームへ進める。
     */
    const std::string legacy_save_path = save_dir + portable::kPathSep + "autosave.r2";
    const std::string save_path = save_dir + portable::kPathSep + "autosave-v4.r2";
    if (std::remove(legacy_save_path.c_str()) == 0) {
        std::fprintf(stderr, "[rogue2] discarded incompatible autosave = %s\n", legacy_save_path.c_str());
    }
    r2_autosave_set_path(save_path.c_str());
    /*
     * 設定画面の値は原作ではセーブに入らない。HD2D 版で保存した値を、ゲーム開始前に戻す。
     * 環境変数 ROGUEOPTS があれば、原作の init() がそのあとで上書きする。
     */
    const std::string options_path = save_dir + portable::kPathSep + "options.txt";
    r2_options_set_path(options_path.c_str());
    r2_options_load();
    /*
     * 名前はアプリ側の PlayerIdentity（<data root>/player-identities/rogue2/）から読む。
     * 無ければ表示名は空のままにして、md_gln() が原作の既定値「A FIGHTER」へ落ちるようにする
     * （「HD2D」のような仮の名前を挨拶や墓標に出さない）。
     */
    presentation::player_identity_activate("rogue2", "rogue.save", "", "FIGHTER");

    std::vector<char> path(message_path.begin(), message_path.end());
    path.push_back('\0');
    char program[] = "Rogue2Core";
    bool resume = false;
    if (FILE *fp = std::fopen(save_path.c_str(), "rb")) {
        std::fclose(fp);
        resume = true;
    }
    std::vector<char> save(save_path.begin(), save_path.end());
    save.push_back('\0');
    char *argv_new[] = { program, path.data(), nullptr };
    char *argv_resume[] = { program, path.data(), save.data(), nullptr };
    std::fprintf(stderr, "[rogue2] message file = %s\n", message_path.c_str());
    std::fprintf(stderr, "[rogue2] autosave = %s%s\n", save_path.c_str(), resume ? " (resume)" : "");
    const int rc = r2_game_main(resume ? 3 : 2, resume ? argv_resume : argv_new);

    r2_set_host_hooks(nullptr);
    presentation::ExitMessage exit_message;
    exit_message.code = rc;
    (void)send_message("exit", presentation::encode_exit(exit_message));
    return rc;
}

int run_protocol(const std::string &message_path)
{
    return run_protocol_handles(presentation::transport_stdin(), presentation::transport_stdout(), message_path);
}

void print_usage()
{
    std::fprintf(stderr, "Rogue2Core --ui-protocol=stdio [--mesg=PATH]\n");
}

int run_selftest()
{
    int failures = rogue2::semantic_selftest();
    failures += rogue2::attack_fx_selftest();
    const auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::fprintf(stderr, "[rogue2] selftest FAIL: %s\n", what);
        ++failures;
    };

    const struct DirectionCase { int dx, dy; char key; } directions[] = {
        {-1,-1,'y'}, {0,-1,'k'}, {1,-1,'u'}, {-1,0,'h'},
        {1,0,'l'}, {-1,1,'b'}, {0,1,'j'}, {1,1,'n'},
    };
    for (const auto &entry : directions)
        check(rogue_direction_key(entry.dx, entry.dy) == entry.key, "direction mapping");

    std::vector<int> repeat;
    check(append_repeat_number_keys(123, repeat), "repeat prefix accepted");
    check(repeat == std::vector<int>({0x15, '1', '2', '3', '\r'}), "repeat prefix key sequence");
    std::vector<int> invalid;
    check(!append_repeat_number_keys(0, invalid) && invalid.empty(), "repeat prefix lower bound");
    check(!append_repeat_number_keys(1000, invalid) && invalid.empty(), "repeat prefix upper bound");

    // Regression: battle text used to live in a fixed 80/512-byte buffer. Exercise
    // the shared growable string well beyond those historical limits.
    std::string long_name(8192, 'A');
    char *dynamic_text = nullptr;
    check(r2_text_setf(&dynamic_text, "hit:%s", long_name.c_str()) != 0, "dynamic battle text format");
    check(std::strlen(r2_text_cstr(dynamic_text)) == long_name.size() + 4, "dynamic battle text length");
    check(r2_text_append(&dynamic_text, ":dead") != 0, "dynamic battle text append");
    check(std::strlen(r2_text_cstr(dynamic_text)) == long_name.size() + 9, "dynamic battle text appended length");
    r2_text_clear(&dynamic_text);
    check(dynamic_text == nullptr, "dynamic battle text clear");

    std::fprintf(stderr, "[rogue2] selftest: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}

} // namespace

/*
 * 原作の clean_up(estr) は終了文を stdout へ書いて exit() する。パイプ越しではその文が
 * 誰にも見えないので、ここで画面側へ届ける（第 2 回、2026-09-24。終了時の文は 2026-10-12 に見える形にした）:
 *   - 「それでは、またね。」（mesg[12]）は正常終了。原作の message() と同じ作法で行 0 へ出し、
 *     ［続く］を付けて決定・取消・Space を待ってから exit（code 0）を送る。以前は文を履歴へ積んだ直後に
 *     exit を送っていたので、画面側が終了を受けて閉じ、文は誰の目にも触れなかった。
 *     画面がもう閉じている（接続が切れた・quit_request を受けた）ときは待たない。
 *   - それ以外の空でない文（復元に失敗した理由・名前が長すぎる・メモリ不足）は続行不能
 *     なので fatal（reason=その文）で送る。画面側は fatal の reason を表示する。
 *   - 空文字列（死亡後の得点表のあと・S で保存して終了）は exit（code 0）だけ。
 * exit メッセージには文を載せる欄が無い（ExitMessage は code だけ）。欄を足す案は
 * いまはコアの側で待つ形にして、presentation と画面側は触らない。
 */
extern "C" void r2_host_clean_up(const char *estr)
{
    const std::string text = (estr != nullptr) ? estr : "";
    const char *byebye = r2_byebye_text();
    const bool farewell = !text.empty() && byebye != nullptr && text == byebye;
    if (farewell && !g_ui_gone.load() && !g_quit_requested.load()) {
        // 履歴への積みも message() が行う。待ちの間は host_present() が［続く］付きの行 0 を送る。
        r2_farewell_show(text.c_str());
        g_last_frame_key.clear();
        host_present();
    } else if (!text.empty()) {
        r2_message_history_push(text.c_str());
        g_last_frame_key.clear();
        host_present();
    }
    if (!text.empty() && !farewell) {
        // 続行不能の理由は、英語のときは英語にして画面へ届ける（原作が直書きする日本語の文。修-56）。
        char localized[1024];
        send_fatal(r2_localize_text(text.c_str(), localized, sizeof(localized)) ? std::string(localized) : text);
        return;
    }
    presentation::ExitMessage exit_message;
    exit_message.code = 0;
    (void)send_message("exit", presentation::encode_exit(exit_message));
}

#if !defined(_WIN32)
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    (void)protocol_log_path;
    (void)forwarded_args;
    const std::string message_path = default_message_path();
    if (message_path.empty()) return 2;
    return run_protocol_handles(in, out, message_path);
}
#endif

int main(int argc, char **argv)
{
    bool protocol = false;
    std::string message_path = default_message_path();
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == kUiProtocolArg) {
            protocol = true;
        } else if (arg.rfind(kMessageArg, 0) == 0) {
            message_path = std::string(arg.substr(kMessageArg.size()));
        } else if (arg == "--selftest") {
            return run_selftest();
        } else if (arg == "--selftest-i18n" || arg.rfind("--selftest-i18n=", 0) == 0) {
            // 英語の表示の自己検査（修-56 の第 2 回）。日本語のメッセージ表を読み、英語への差し替えと語順の組み直しを確かめる。
            const std::string path = arg.size() > 16 ? std::string(arg.substr(16)) : message_path;
            return r2_i18n_selftest(path.c_str());
        } else if (arg.rfind("--protocol-log=", 0) == 0) {
            /*
             * 画面側の `--core-protocol-log=` はこの引数になってコアへ届く。RogueClone2 には
             * コア側の会話ログが無いが、知らない引数として usage で終了（code 2）すると
             * 画面側が「ゲームコアが予期せず停止しました」と出す。受け取って無視する（2026-09-25）。
             */
            std::fprintf(stderr, "[rogue2] --protocol-log= はこのコアでは未対応です（無視します）\n");
        } else {
            print_usage();
            return 2;
        }
    }
    if (!protocol || message_path.empty()) {
        print_usage();
        return 2;
    }
    presentation::set_stdio_binary_mode();
    return run_protocol(message_path);
}
