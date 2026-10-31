/*
 * NetHack のコアの入口とプロトコル。
 *
 * frame に入れる物: 地図のマス（cells・minimap）・メッセージ・ステータス・メニュー・文字の窓・問い・文字入力。
 * 地図のマスは View に溜め（nh_host_map_cell）、nh_frame.cpp が cells と minimap に直す。
 *
 * ゲームは main のスレッドで動く（原作の moveloop。nh_winport.c）。原作の画面の関数は、キーが要るところで
 * nh_host_getkey などを呼ぶ。ここで frame を送ってからキーを待つ（Umoria の um_main.cpp と同じ形）。
 * 受信の糸（receive_loop）はキーの列と ui_state・quit_request を受けるだけで、ゲームの状態には触らない。
 *
 * 専用の画面（握手で features に nethack-ui を申告し合ったとき。nh_ui_impl.h・）では、窓の字
 * （メッセージ・メニュー・問い・ステータス）を frame ではなく nethack_ui の知らせで送り、答えを nethack_reply で受ける。
 * frame は地図と最小の hud だけ。申告が無いときは、下の共通の画面向けの作りのまま。
 *
 * 新しい画面（ui_state.modern_ui。既定）では、frame を送る直前に Umoria と同じ部品（NativeModernMenu）で窓のモデル
 * （frame.modern）を作り、画面の操作（ui_action）をキーに直す。持ち物一覧のタブ（inventory_open）は
 * NativeInventoryMenu が答え、中身は g_inventory（開かれたときだけ原作の i を流して写し取る）。
 */

#include "nh_host.h"
#include "nh_view.h"

#include "bridge/input_event_adapter.h"
#include "bridge/native_inventory_menu.h"
#include "bridge/native_modern.h"
#include "bridge/rogue_sub_panels.h"
#include "frame/frame_codec.h"
#include "frame/game_frame.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"
#include "portable/legacy_os.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#endif

namespace {

#ifdef NH_LANG_JA
constexpr bool kBuildJa = true;
constexpr const char *kCoreName = "NetHackJP";
constexpr const char *kCoreVersion = "NetHackJP-5.0.1-20261024";
constexpr const char *kLangDir = "ja";
#else
constexpr bool kBuildJa = false;
constexpr const char *kCoreName = "NetHack";
constexpr const char *kCoreVersion = "5.0.1-34e91f177";
constexpr const char *kLangDir = "en";
#endif

presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;
std::mutex g_input_mutex;
std::condition_variable g_input_cv;
std::deque<int> g_keys;
//! コマンドの欄（cmdbar）で選んだ拡張コマンドの名前。`#` のキーと一緒に積み、拡張コマンドの問い（nh_host_ask_text）で使う。g_input_mutex で守る
std::string g_pending_extcmd;
std::atomic<bool> g_ui_gone{ false };
std::atomic<bool> g_quit_requested{ false };
std::atomic<bool> g_exit_sent{ false };
//! UI から来る move（矢印・パッドの十字）をキーにしてよい状態か（メニュー・文字入力の間は捨てる）。
std::atomic<bool> g_moves_ok{ true };
std::atomic<bool> g_refresh_requested{ false };
std::atomic<bool> g_force_frame{ false };
//! 原作の hangup(0) を呼んだか。
std::atomic<bool> g_hangup_called{ false };
std::uint64_t g_frame_id = 0;
std::string g_last_frame;
//! 画面側の言語（ui_state で届く）。届く前は空。
std::mutex g_lang_mutex;
std::string g_ui_lang;

//! 画面に見せている内容。ゲームを動かすスレッドだけが書き換える。
nethack::View g_view;

/*
 * 新しい画面（ui_state.modern_ui が真。既定）の窓。Umoria（um_main.cpp）と同じ部品を使う。
 * frame の menu_*・prompt・text_input_active から窓のモデル（frame.modern）を作って送り、画面の操作（ui_action）を
 * キーに直す。モデルを送らないと、画面は「画面の切り替えを待っています…」のまま入力を送らない。
 * 受信スレッド（ui_action・ui_state）とゲームのスレッド（present）が同じ部品を触るので g_modern_mutex で守る。
 */
std::mutex g_modern_mutex;
presentation::NativeModernMenu g_native_menu;
//! 画面の「持ち物一覧」タブ（inventory_open）に答える部品。応答が無いと画面が止まるので、中身が空でも置く。
presentation::NativeInventoryMenu g_inventory_menu;
//! 新しい画面か（ui_state.modern_ui）。frame の窓ごとの欄の組み方が少し変わる（nh_frame.cpp）。
std::atomic<bool> g_modern_ui{ false };

/*
 * 持ち物一覧のタブに見せる、持ち物の一覧（見るだけ。行ごとの命令は付けない）。
 * 持ち物は原作の側（C）にあって、ここへは出していない。そこで、画面が一覧のタブを開いたとき（inventory_open）だけ、
 * 次の命令待ちで原作の命令 i をこちらから 1 回流し（wait_key）、出てきたメニューを窓に出さずに写し取る（nh_host_menu）。
 * 毎回流さないのは、数の指定（20s など）の途中に割り込まないためと、やり直し（^A）が最後の命令の i に置き換わるのを
 * 開いたときだけに留めるため。g_inventory はゲームを動かすスレッドだけが読み書きする（ロックは要らない）。
 */
struct InventoryRow {
    std::string label;
    int key{ 0 };
};
std::vector<InventoryRow> g_inventory;
//! 一覧のタブが開かれた（受信スレッドが立て、ゲームのスレッドが命令待ちで下ろす）。
std::atomic<bool> g_inventory_refresh{ false };
//! 流した i のメニューを待っている間は真。メニューが出ずに命令待ちへ戻ったら（持ち物が空）偽に戻す。
bool g_inventory_capturing = false;
//! 命令待ちで直前に読んだキーが数字（数の指定の途中）か。
bool g_in_count = false;

bool send_message(const std::string &payload);

// 専用の画面（nethack-ui）のコアの側。無名名前空間の中に取り込む（このファイルの送信・待ちの関数と状態を使う）
#include "nh_ui_impl.h"

bool lang_is_ja()
{
    std::lock_guard<std::mutex> lock(g_lang_mutex);
    return g_ui_lang.empty() ? kBuildJa : g_ui_lang == "ja";
}

bool send_message(const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[nethack] send failed: %s\n", err.c_str());
        g_ui_gone.store(true);
        g_input_cv.notify_all();
        return false;
    }
    return true;
}

void enqueue(const std::vector<int> &keys)
{
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        for (const int key : keys) {
            if (key > 0 && key <= 255) g_keys.push_back(key);
        }
    }
    g_input_cv.notify_all();
}

/*!
 * NetHack の既定の移動キー（vi キー）。0,0 は「その場で 1 手」（.）。
 */
int direction_key(int dx, int dy)
{
    static constexpr char table[3][3] = { { 'y', 'k', 'u' }, { 'h', '.', 'l' }, { 'b', 'j', 'n' } };
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
    return table[dy + 1][dx + 1];
}

presentation::PadCommandsMessage build_pad_commands(bool ja);

void receive_loop(presentation::TransportHandle input)
{
    while (!g_ui_gone.load()) {
        std::string payload, err;
        if (presentation::read_message(input, payload, err) != presentation::TransportResult::Ok) {
            g_ui_gone.store(true);
            g_input_cv.notify_all();
            return;
        }
        const auto type = presentation::peek_message_type(payload);
        if (type == "keys") {
            presentation::KeysMessage message;
            if (presentation::decode_keys(payload, message, err)) enqueue(message.keys);
        } else if (type == "nethack_reply") {
            // 専用の画面の返事（nethack-ui が有効なときだけ）。msgwin はここで覚え、pos は位置の列へ、ほかは要求の返事として積む
            if (!g_ui_active.load()) continue;
            Json body = Json::parse(payload, nullptr, false);
            if (body.is_discarded() || !body.is_object()) continue;
            try {
                const std::string a = body.value("a", std::string());
                if (a == "msgwin") {
                    const int rows = body.value("rows", 0);
                    const int cols = body.value("cols", 0);
                    if (rows > 0) g_msgwin_rows.store(rows);
                    if (cols > 0) g_msgwin_cols.store(cols);
                    continue;
                }
                if (a == "extcmd") {
                    // コマンドの欄の拡張コマンド（祈る・終了など）。`#` を押したことにして、原作が名前を聞く所でこの名前を答える
                    const std::string name = body.value("name", std::string());
                    if (name.empty()) continue;
                    {
                        std::lock_guard<std::mutex> lock(g_input_mutex);
                        g_pending_extcmd = name;
                    }
                    enqueue({ '#' });
                    continue;
                }
                if (a == "pos") {
                    UiPos pos;
                    pos.x = body.value("x", 0);
                    pos.y = body.value("y", 0);
                    pos.mod = body.value("mod", 1);
                    {
                        std::lock_guard<std::mutex> lock(g_input_mutex);
                        g_ui_pos.push_back(pos);
                    }
                    g_input_cv.notify_all();
                    continue;
                }
                UiReply reply;
                reply.r = body.value("r", 0L);
                reply.body = std::move(body);
                {
                    std::lock_guard<std::mutex> lock(g_input_mutex);
                    g_ui_replies.push_back(std::move(reply));
                }
                g_input_cv.notify_all();
            } catch (const std::exception &) {
                // 型の合わない返事は捨てる
            }
        } else if (type == "nethack_query") {
            // 専用の画面からの問い合わせ（nethack-ui が有効なときだけ）。hover は地図のマスの説明。ゲームのスレッドの入力待ちの中で答える
            if (!g_ui_active.load()) continue;
            Json body = Json::parse(payload, nullptr, false);
            if (body.is_discarded() || !body.is_object()) continue;
            try {
                if (body.value("q", std::string()) == "hover") {
                    std::lock_guard<std::mutex> lock(g_input_mutex);
                    g_ui_hover.x = body.value("x", 0);
                    g_ui_hover.y = body.value("y", 0);
                    g_ui_hover.pending = true;
                }
            } catch (const std::exception &) {
                // 型の合わない問い合わせは捨てる
            }
            g_input_cv.notify_all();
        } else if (type == "input_event") {
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) continue;
            /*
             * 受け付けてからキーを積むまでを、窓のモデルを作る側（present）と同じロックの中で行う
             * （間に present が入ると「受け付けた、でも中身は古い」画が送られる。Umoria と同じ）。
             */
            std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
            std::vector<int> keys;
            for (const auto &event : message.events) {
                if (event.e == "ui_action" && !g_ui_active.load()) {
                    // 新しい画面の窓の操作（選ぶ・決める・取り消す・続ける・答える）をキーに直す
                    if (event.action == "inventory_open") g_inventory_refresh.store(true); // 一覧の中身を写し直させる
                    std::vector<int> translated;
                    if (g_inventory_menu.input(event, translated) || g_native_menu.input(event, translated)) {
                        if (translated.empty()) g_refresh_requested.store(true);
                        keys.insert(keys.end(), translated.begin(), translated.end());
                        continue;
                    }
                }
                if (event.e == "move") {
                    if (!g_moves_ok.load()) continue;
                    const int key = direction_key(event.dx, event.dy);
                    if (key != 0) keys.push_back(key);
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
                {
                    std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
                    g_native_menu.set_enabled(state.modern_ui);
                    g_native_menu.set_language(state.lang.empty() ? "ja" : state.lang);
                    g_inventory_menu.configure(state.modern_ui, state.lang.empty() ? "ja" : state.lang, "rogue");
                    g_modern_ui.store(state.modern_ui);
                }
                const bool was_ja = lang_is_ja();
                {
                    std::lock_guard<std::mutex> lock(g_lang_mutex);
                    g_ui_lang = state.lang;
                }
                if (was_ja != lang_is_ja()) {
                    // 名札は握手の直後（言語が届く前）の言語で送ってある。変わったら送り直す。
                    (void)send_message(presentation::encode_pad_commands(build_pad_commands(lang_is_ja())));
                    (void)send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(!lang_is_ja())));
                    g_force_frame.store(true);
                }
                g_refresh_requested.store(true);
                g_input_cv.notify_all();
            }
        } else if (type == "quit_request") {
            g_quit_requested.store(true);
            g_input_cv.notify_all();
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

/*!
 * 画面に見せている内容を frame にして送る。同じ中身なら送らない（合体の規則。v1 §6.4）。
 * 読んでいないキーがあるうちは送らない（古い画を操作の結果と取り違えさせない）。force は終了の知らせ用。
 */
void present(bool force)
{
    if (g_out == nullptr || g_ui_gone.load()) return;
    if (!force) {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (!g_keys.empty()) return;
    }
    // 地図の見え方（いま見えているマスと主人公の位置）は、frame を組む直前に原作から写す（ゲームを動かすスレッドの中）
    g_view.map_valid = g_view.phase == NH_PHASE_GAME
        && nh_snapshot_view(g_view.visible.data(), &g_view.hero_x, &g_view.hero_y) != 0;
    const bool ui = g_ui_active.load();
    g_view.ui_mode = ui;
    GameFrame frame;
    nethack::build_frame(g_view, lang_is_ja(), kCoreName, g_modern_ui.load() && !ui, frame);
    {
        std::lock_guard<std::mutex> modern_lock(g_modern_mutex);
        if (!force) {
            // 読んでいないキーがあるうちは送らない（上と同じ確かめを、窓のモデルを作る直前にもう一度）
            std::lock_guard<std::mutex> lock(g_input_mutex);
            if (!g_keys.empty()) return;
        }
        // 専用の画面（nethack-ui）は窓を自分で描く。新しい画面の窓のモデル（frame.modern）は載せない
        if (!ui) g_native_menu.capture(frame, {}, {});
        // 持ち物一覧のタブ（見るだけ）。中身は g_inventory（上の説明）
        if (!ui) g_inventory_menu.capture(
            frame,
            [] {
                std::vector<ModernRow> rows;
                for (const InventoryRow &item : g_inventory) {
                    ModernRow row;
                    row.id = "nethack-item:" + std::string(1, static_cast<char>(item.key));
                    row.label = item.label;
                    row.key = item.key;
                    row.hotkey = item.key;
                    row.slot = static_cast<int>(rows.size());
                    row.selection = 0;
                    row.count = 1;
                    row.price_known = false;
                    rows.push_back(std::move(row));
                }
                return rows;
            },
            [] { return presentation::PadCommandsMessage{}; });
    }
    frame.frame_id = g_frame_id + 1;
    const std::string wire = presentation::encode_game_frame(frame);
    const std::string key = frame_compare_key(wire);
    if (key == g_last_frame && !g_force_frame.exchange(false)) return;
    g_last_frame = key;
    ++g_frame_id;
    (void)send_message(wire);
}

void send_exit(int code)
{
    if (g_out == nullptr || g_ui_gone.load() || g_exit_sent.exchange(true)) return;
    presentation::ExitMessage message;
    message.code = code;
    (void)send_message(presentation::encode_exit(message));
}

//! 原作は終わるとき exit() を呼ぶ（nethack_exit）。その直前に exit を送る。
void send_exit_at_process_end() { send_exit(0); }

/*!
 * UI が居なくなった・終了を求められたとき。原作の hangup(0)（保存して終わる道。windows 版は SAFERHANGUP で、
 * 次に命令を読む所で保存して終わる）へ入り、ESC を返す。
 */
int handle_shutdown()
{
    static int calls = 0;
    if (!g_hangup_called.exchange(true)) nh_request_hangup();
    if (++calls > 5000) {
        // hangup の後も画面の関数が呼ばれ続けるなら、これ以上は待たずに終える。
        std::fprintf(stderr, "[nethack] shutdown did not finish; exiting\n");
        std::exit(1);
    }
    return 0x1B;
}

/*!
 * キーを 1 つ待つ。待つ直前に、いまの内容を frame にして送る（専用の画面では、そのあと wait の知らせも送る）。
 * @param mode NH_KEY_*。地図の命令待ちかどうかで frame の awaiting_command が変わる
 * @param pos 位置の選択（NH_KEY_POS）のとき、地図のクリックが来たら 0 を返してここへ入れる。要らなければ nullptr
 */
int wait_key_impl(int mode, UiPos *pos)
{
    const bool ui = g_ui_active.load();
    if (!ui && mode == NH_KEY_POS) mode = NH_KEY_OTHER; // 共通の画面は位置の選択を区別しない
    if (mode == NH_KEY_COMMAND) g_inventory_capturing = false; // 流した i がメニューを出さずに終わった（持ち物が空）
    g_view.key_mode = mode;
    // 専用の画面の窓は nh_ui_impl.h が扱う（ここは地図の状態）ので、移動は --More-- 以外で受ける
    g_moves_ok.store(ui ? (mode != NH_KEY_MORE) : (g_view.mode == nethack::Mode::Map || g_view.mode == nethack::Mode::Yn));
    if (g_hangup_called.load()) return handle_shutdown();
    present(false);
    // 地図のクリック（pos の返事）を受けるのは、命令の待ちと位置の選択の待ちだけ（原作の GUI 版の nh_poskey）
    const bool pos_ok = ui && (mode == NH_KEY_POS || mode == NH_KEY_COMMAND) && pos != nullptr;
    if (ui) {
        // wait を送る前に届いていたクリックと問い合わせは、前の待ちへの物なので捨てる（画面は wait を見てから送る）
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_ui_pos.clear();
        g_ui_hover.pending = false;
    }
    if (ui) ui_send_wait(ui_wait_reason(mode), 0, mode == NH_KEY_MORE ? ui_more_text() : nullptr);
    std::unique_lock<std::mutex> lock(g_input_mutex);
    for (;;) {
        if (pos_ok && !g_ui_pos.empty()) {
            *pos = g_ui_pos.front();
            g_ui_pos.pop_front();
            return 0;
        }
        if (pos_ok && g_ui_hover.pending) {
            // 地図のマスの説明（hover）。キーが来ていても先に答える（答えは軽く、ゲームの状態を変えない）
            const int hx = g_ui_hover.x;
            const int hy = g_ui_hover.y;
            g_ui_hover.pending = false;
            lock.unlock();
            ui_answer_hover(hx, hy);
            lock.lock();
            continue;
        }
        if (!g_keys.empty()) {
            const int key = g_keys.front();
            g_keys.pop_front();
            if (mode == NH_KEY_COMMAND) g_in_count = key >= '0' && key <= '9';
            return key;
        }
        if (!pos_ok) {
            g_ui_pos.clear(); // クリックを受ける待ちでないときのクリックと問い合わせは捨てる
            g_ui_hover.pending = false;
        }
        // 一覧のタブが開かれた。命令待ちで、数の指定の途中でなければ、i を流して持ち物を写し取る（frame は送らない。戻ってきたら送る）
        if (!ui && mode == NH_KEY_COMMAND && g_inventory_refresh.load() && !g_in_count && g_modern_ui.load()) {
            g_inventory_refresh.store(false);
            g_inventory_capturing = true;
            g_inventory.clear();
            return 'i';
        }
        if (g_ui_gone.load() || g_quit_requested.load()) {
            lock.unlock();
            return handle_shutdown();
        }
        if (g_refresh_requested.exchange(false)) {
            lock.unlock();
            present(false);
            lock.lock();
            continue;
        }
        g_input_cv.wait_for(lock, std::chrono::milliseconds(100));
    }
}

int wait_key(int mode) { return wait_key_impl(mode, nullptr); }

void push_history(const std::string &text)
{
    if (text.empty()) return;
    g_view.history.push_back(text);
    while (g_view.history.size() > nethack::kHistoryMax) g_view.history.pop_front();
}

//! 窓を閉じて地図の状態へ戻す。
void back_to_map()
{
    g_view.mode = nethack::Mode::Map;
    g_view.prompt.clear();
    g_view.lines.clear();
    g_view.yn_choices.clear();
    g_view.typed.clear();
    g_view.how = NH_PICK_NONE;
}

//! 文字入力の最後の、途中で切れた UTF-8 を落とす。
void trim_incomplete_utf8(std::string &text)
{
    std::size_t i = text.size();
    std::size_t back = 0;
    while (i > 0 && back < 4 && (static_cast<unsigned char>(text[i - 1]) & 0xC0) == 0x80) {
        --i;
        ++back;
    }
    if (i == 0) return;
    const unsigned char lead = static_cast<unsigned char>(text[i - 1]);
    std::size_t need = 1;
    if (lead >= 0xF0) need = 4;
    else if (lead >= 0xE0) need = 3;
    else if (lead >= 0xC0) need = 2;
    if (lead >= 0xC0 && back + 1 < need) text.resize(i - 1);
}

//! 入力の最後の 1 字（UTF-8 の連なりごと）を消す。
void erase_last_char(std::string &text)
{
    while (!text.empty()) {
        const unsigned char c = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

const char *yn_label(int key, bool ja)
{
    switch (key) {
    case 'y': return ja ? "はい" : "Yes";
    case 'n': return ja ? "いいえ" : "No";
    case 'q': return ja ? "中止" : "Quit";
    case 'a': return ja ? "すべて" : "All";
    // 持ち物の字を聞く問い（getobj）の特別な字
    case '?': return ja ? "一覧から選ぶ" : "List";
    case '*': return ja ? "全部の一覧" : "List all";
    case '-': return ja ? "なし（素手）" : "None (hands)";
    case '$': return ja ? "金貨" : "Gold";
    default: return nullptr;
    }
}

/*!
 * 問いの文の最後の括弧の中から、選べる字を取り出す。例: 「[ynaq]」→ y n a q、「[fg or ?*]」→ f g ? *、
 * 「[- a-d or ?*]」→ - a b c d ? *。「or」は読み飛ばす。括弧が無い・字が無いときは空。
 * 字・数字・「$ ? * -」だけを拾う（「#」は個数の指定なので拾わない）。多すぎる（60 超）ときは打ち切る。
 */
std::vector<int> letters_in_brackets(const std::string &text)
{
    std::vector<int> keys;
    const std::size_t open = text.rfind('[');
    if (open == std::string::npos) return keys;
    const std::size_t close = text.find(']', open);
    if (close == std::string::npos || close <= open + 1 || close - open > 80) return keys;
    const std::string body = text.substr(open + 1, close - open - 1);
    const auto add = [&](int key) {
        for (const int k : keys) {
            if (k == key) return;
        }
        if (keys.size() < 60) keys.push_back(key);
    };
    const auto is_alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        // 単語の「or」
        if (c == 'o' && i + 1 < body.size() && body[i + 1] == 'r' && (i == 0 || body[i - 1] == ' ')
            && (i + 2 >= body.size() || body[i + 2] == ' ')) {
            ++i;
            continue;
        }
        // 範囲「a-d」
        if (is_alnum(c) && i + 2 < body.size() && body[i + 1] == '-' && is_alnum(body[i + 2]) && body[i + 2] > c) {
            for (char k = c; k <= body[i + 2]; ++k) {
                if (is_alnum(k)) add(static_cast<unsigned char>(k));
            }
            i += 2;
            continue;
        }
        if (is_alnum(c) || c == '$' || c == '?' || c == '*' || c == '-') add(static_cast<unsigned char>(c));
    }
    return keys;
}

std::string yn_choice_label(int key, bool ja)
{
    if (const char *label = yn_label(key, ja)) return label;
    return std::string(1, static_cast<char>(key));
}

} // namespace

// ---- C の側（nh_winport.c）から呼ばれる ----

extern "C" {

void nh_host_map_clear(void)
{
    std::fill(g_view.map.begin(), g_view.map.end(), nethack::MapCell{});
}

void nh_host_map_cell(int x, int y, int ch, int color, int glyph, int glyph_class, int glyph_index, unsigned glyph_flags,
                      int terrain_cmap, int terrain_variant)
{
    if (x < 0 || x >= nethack::kMapCols || y < 0 || y >= nethack::kMapRows) return;
    nethack::MapCell &cell = g_view.map[static_cast<std::size_t>(y) * nethack::kMapCols + x];
    cell.ch = ch;
    cell.color = color;
    cell.glyph = glyph;
    cell.glyph_class = glyph_class;
    cell.glyph_index = glyph_index;
    cell.flags = glyph_flags;
    cell.terrain_cmap = terrain_cmap;
    cell.terrain_variant = terrain_variant;
}

void nh_host_map_cursor(int x, int y)
{
    g_view.cursor_x = x;
    g_view.cursor_y = y;
}

void nh_host_message(const char *text)
{
    if (g_inventory_capturing) return; // 持ち物を写し取る間の i の知らせ（持ち物が空など）は履歴に残さない
    push_history(nethack::clean_utf8(text));
}

void nh_host_status_field(int field, const char *text)
{
    if (field < 0 || field >= NH_BL_MAX) return;
    g_view.status[field] = nethack::clean_utf8(text);
}

void nh_host_status_conditions(unsigned long mask, const char *names)
{
    g_view.conditions = mask;
    g_view.condition_names = nethack::clean_utf8(names);
}

void nh_host_status_flush(const char *player_name)
{
    g_view.player_name = nethack::clean_utf8(player_name);
    g_view.status_seen = true;
}

int nh_host_menu(int how, const char *prompt, const nh_menu_item *items, int count, int *chosen, long *counts)
{
    using nethack::Mode;
    if (g_inventory_capturing) {
        // wait_key が流した i のメニュー。窓に出さずに持ち物の一覧へ写し、何も選ばずに閉じる
        g_inventory_capturing = false;
        g_inventory.clear();
        for (int i = 0; i < count; ++i) {
            if (items[i].selectable == 0 || items[i].selector == 0) continue;
            g_inventory.push_back({ nethack::clean_utf8(items[i].text), static_cast<unsigned char>(items[i].selector) });
        }
        return 0;
    }
    nethack::View &v = g_view;
    v.prompt = nethack::clean_utf8(prompt);
    v.lines.clear();
    v.how = how;
    std::vector<int> item_of_line;
    std::vector<long> line_counts;
    bool used_selector[256] = {};
    bool any_selectable = false;
    for (int i = 0; i < count; ++i) {
        nethack::Line line;
        line.text = nethack::clean_utf8(items[i].text);
        line.selectable = items[i].selectable != 0 && how != NH_PICK_NONE;
        line.selector = static_cast<unsigned char>(items[i].selector);
        line.selected = line.selectable && items[i].preselected != 0;
        if (line.selectable) {
            any_selectable = true;
            if (line.selector != 0) used_selector[line.selector] = true;
        }
        v.lines.push_back(std::move(line));
        item_of_line.push_back(i);
        line_counts.push_back(-1);
    }
    // 選ぶ字が無い項目には a〜z A〜Z を順に振る（原作の tty と同じ考え方。使われている字は飛ばす）
    {
        static const char kLetters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::size_t next = 0;
        for (nethack::Line &line : v.lines) {
            if (!line.selectable || line.selector != 0) continue;
            while (kLetters[next] != '\0' && used_selector[static_cast<unsigned char>(kLetters[next])]) ++next;
            if (kLetters[next] == '\0') break;
            line.selector = static_cast<unsigned char>(kLetters[next]);
            used_selector[line.selector] = true;
        }
    }
    v.mode = (any_selectable && how != NH_PICK_NONE) ? Mode::Menu : Mode::Text;

    int result = 0;
    long pending_count = -1;
    const auto finish = [&]() {
        int n = 0;
        for (std::size_t i = 0; i < v.lines.size(); ++i) {
            if (!v.lines[i].selectable || !v.lines[i].selected) continue;
            if (chosen != nullptr) chosen[n] = item_of_line[i];
            if (counts != nullptr) counts[n] = line_counts[i];
            ++n;
        }
        return n;
    };
    for (;;) {
        const int key = wait_key(NH_KEY_OTHER);
        if (v.mode == Mode::Text) {
            if (key == 0x1B) {
                result = (how == NH_PICK_NONE) ? 0 : -1;
                break;
            }
            if (key == ' ' || key == '\r' || key == '\n') {
                result = 0;
                break;
            }
            continue;
        }
        if (key == 0x1B) {
            result = -1;
            break;
        }
        if (key == '\r' || key == '\n' || key == ' ') {
            result = finish();
            break;
        }
        // 項目の字（先に調べる。'-' や '$' が項目の字のことがある）
        int hit = -1;
        for (std::size_t i = 0; i < v.lines.size(); ++i) {
            if (v.lines[i].selectable && v.lines[i].selector == key) {
                hit = static_cast<int>(i);
                break;
            }
        }
        if (hit >= 0) {
            nethack::Line &line = v.lines[static_cast<std::size_t>(hit)];
            if (how == NH_PICK_ONE) {
                line.selected = true;
                line_counts[static_cast<std::size_t>(hit)] = pending_count;
                result = finish();
                break;
            }
            line.selected = !line.selected;
            line_counts[static_cast<std::size_t>(hit)] = line.selected ? pending_count : -1;
            pending_count = -1;
            continue;
        }
        // グループの字（同じ字の項目をまとめて切り替える）
        if (how == NH_PICK_ANY) {
            bool group_hit = false;
            bool any_unselected = false;
            for (std::size_t i = 0; i < v.lines.size(); ++i) {
                if (v.lines[i].selectable && static_cast<unsigned char>(items[i].group) == key && items[i].group != 0) {
                    group_hit = true;
                    if (!v.lines[i].selected) any_unselected = true;
                }
            }
            if (group_hit) {
                for (std::size_t i = 0; i < v.lines.size(); ++i) {
                    if (v.lines[i].selectable && static_cast<unsigned char>(items[i].group) == key) v.lines[i].selected = any_unselected;
                }
                continue;
            }
        }
        if (key >= '0' && key <= '9') {
            pending_count = (pending_count < 0 ? 0 : pending_count) * 10 + (key - '0');
            if (pending_count > 99999999L) pending_count = 99999999L;
            continue;
        }
        if (how == NH_PICK_ANY) {
            // 原作のメニューの命令（全部に効く。ページ分けはしないのでページの命令も全部に効く）
            if (key == '.' || key == ',') {
                for (nethack::Line &line : v.lines) {
                    if (line.selectable) line.selected = true;
                }
            } else if (key == '-' || key == '\\') {
                for (nethack::Line &line : v.lines) line.selected = false;
            } else if (key == '@' || key == '~') {
                for (nethack::Line &line : v.lines) {
                    if (line.selectable) line.selected = !line.selected;
                }
            }
        }
    }
    back_to_map();
    return result;
}

void nh_host_text(const char *const *lines, int count)
{
    std::vector<nh_menu_item> items;
    for (int i = 0; i < count; ++i) {
        nh_menu_item item{};
        item.text = lines[i];
        item.color = -1;
        item.glyph = -1;
        items.push_back(item);
    }
    (void)nh_host_menu_win(0, 4, NH_PICK_NONE, nullptr, items.data(), count, nullptr, nullptr);
}

char nh_host_yn(const char *query, const char *choices, char def)
{
    if (ui_on()) return ui_yn(query, choices, def);
    nethack::View &v = g_view;
    const bool ja = lang_is_ja();
    const bool has_choices = choices != nullptr && *choices != '\0';
    std::string shown = nethack::clean_utf8(query);
    v.yn_choices.clear();
    if (has_choices) {
        std::string letters;
        for (const char* p = choices; *p != '\0'; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c < 0x21 || c > 0x7E || c == '#') continue;
            letters.push_back(static_cast<char>(c));
            nethack::YnChoice choice;
            choice.key = c;
            choice.label = yn_choice_label(c, ja);
            v.yn_choices.push_back(std::move(choice));
        }
        // 問いの字に選べる字が無いときは、原作の tty と同じく「[ynq] (n)」を足す
        if (shown.find('[') == std::string::npos) {
            shown += " [" + letters + "]";
            if (def != '\0' && def != ' ') shown += std::string(" (") + def + ")";
        }
    } else {
        // 問いの字の中の括弧（「[ynaq]」「[fg or ?*]」「[- abc or ?*]」）は、選べる字として読む。
        // キャラクタ作成の問いと、ゲーム中の持ち物の字を聞く問い。新しい画面は、ここに無い字を受けない
        for (const int key : letters_in_brackets(shown)) {
            nethack::YnChoice choice;
            choice.key = key;
            // ゲーム中の括弧は持ち物の字（a は「すべて」ではなく持ち物 a）。特別な字（? * - $）だけ名札を付ける
            const bool special = key == '?' || key == '*' || key == '-' || key == '$';
            choice.label = (v.phase == NH_PHASE_PREGAME || special) ? yn_choice_label(key, ja) : std::string(1, static_cast<char>(key));
            v.yn_choices.push_back(std::move(choice));
        }
        // ゲーム中の問いは、取り消しの選択肢を足す（方向の問い・位置の問いなど、選べる字を当てられないときも含む）
        if (v.phase == NH_PHASE_GAME || v.yn_choices.empty()) {
            nethack::YnChoice cancel;
            cancel.key = 0x1B;
            cancel.label = ja ? "キャンセル" : "Cancel";
            v.yn_choices.push_back(std::move(cancel));
        }
    }
    v.prompt = shown;
    v.mode = nethack::Mode::Yn;

    char result = def;
    for (;;) {
        const int key = wait_key(NH_KEY_OTHER);
        if (!has_choices) {
            result = static_cast<char>(key); // 何でも受ける（原作の tty も同じ）
            break;
        }
        if (key == 0x1B) {
            if (std::strchr(choices, 'q') != nullptr) result = 'q';
            else if (std::strchr(choices, 'n') != nullptr) result = 'n';
            else result = def;
            break;
        }
        if (key == ' ' || key == '\r' || key == '\n') {
            result = def;
            break;
        }
        const int lower = (key >= 'A' && key <= 'Z') ? key - 'A' + 'a' : key;
        const int upper = (key >= 'a' && key <= 'z') ? key - 'a' + 'A' : key;
        if (std::strchr(choices, key) != nullptr) {
            result = static_cast<char>(key);
            break;
        }
        if (std::strchr(choices, lower) != nullptr) {
            result = static_cast<char>(lower);
            break;
        }
        if (std::strchr(choices, upper) != nullptr) {
            result = static_cast<char>(upper);
            break;
        }
        // 選べない字は捨てて、もう一度待つ
    }
    // 問いと答えをメッセージの履歴に残す（ゲーム中だけ。キャラクタ作成の問いは残さない）
    if (v.phase == NH_PHASE_GAME && result >= 0x21 && result < 0x7F) push_history(shown + " " + result);
    back_to_map();
    return result;
}

int nh_host_getlin(const char *query, char *buf, int bufsize)
{
    if (ui_on()) return ui_ask_text(NH_ASK_GETLIN, query, buf, bufsize, nullptr, 0);
    nethack::View &v = g_view;
    v.prompt = nethack::clean_utf8(query);
    v.typed.clear();
    v.mode = nethack::Mode::Getlin;
    const std::size_t limit = bufsize > 1 ? static_cast<std::size_t>(bufsize - 1) : 0;
    bool accepted = false;
    for (;;) {
        const int key = wait_key(NH_KEY_OTHER);
        if (key == 0x1B) break;
        if (key == '\r' || key == '\n') {
            accepted = true;
            break;
        }
        if (key == 0x08 || key == 0x7F) {
            erase_last_char(v.typed);
        } else if (key == 0x15 || key == 0x18) { // ^U ^X: 全部消す
            v.typed.clear();
        } else if (key >= 0x20 && v.typed.size() < limit) {
            v.typed.push_back(static_cast<char>(key));
        }
    }
    std::string text = v.typed;
    trim_incomplete_utf8(text);
    const std::string prompt = v.prompt;
    if (accepted && buf != nullptr && bufsize > 0) {
        std::snprintf(buf, static_cast<std::size_t>(bufsize), "%s", text.c_str());
    }
    if (v.phase == NH_PHASE_GAME) push_history(prompt + " " + (accepted ? text : std::string()));
    back_to_map();
    return accepted ? 1 : 0;
}

int nh_host_getkey(int mode)
{
    back_to_map();
    return wait_key(mode);
}

void nh_host_set_phase(int phase) { g_view.phase = phase; }

void nh_host_exit_notice(const char *text)
{
    if (ui_on()) {
        Json m;
        m["k"] = "exit";
        if (text != nullptr && *text != '\0') m["text"] = ui_text(text);
        (void)ui_send(std::move(m));
    }
    if (text != nullptr && *text != '\0') push_history(nethack::clean_utf8(text));
    back_to_map();
    g_view.key_mode = NH_KEY_OTHER;
    present(true);
}

void nh_host_show_message_history(void)
{
    std::vector<std::string> lines(g_view.history.begin(), g_view.history.end());
    if (lines.empty()) lines.push_back(lang_is_ja() ? "(メッセージはまだありません)" : "(no messages yet)");
    std::vector<nh_menu_item> items;
    for (const std::string &line : lines) {
        nh_menu_item item{};
        item.text = line.c_str();
        item.color = -1;
        item.glyph = -1;
        items.push_back(item);
    }
    const std::string title = lang_is_ja() ? "メッセージ履歴" : "Message history";
    if (ui_on()) {
        // 専用の画面は自分の履歴の窓を持つが、原作の ^P は届くので、コアの履歴をテキストの窓として見せる（窓の番号 0）
        ui_text_window("text", 0, items.data(), static_cast<int>(items.size()), nullptr);
        return;
    }
    (void)nh_host_menu(NH_PICK_NONE, title.c_str(), items.data(), static_cast<int>(items.size()), nullptr, nullptr);
}

int nh_host_lang_is_ja(void) { return lang_is_ja() ? 1 : 0; }

// ---- nethack-ui（nh_host.h の説明と nh_ui_impl.h）----

int nh_host_ui_enabled(void) { return ui_on() ? 1 : 0; }

void nh_host_ui_win(int op, int id, int type, int blocking)
{
    if (!ui_on()) return;
    static const char *const kTypes[] = { "", "message", "status", "map", "menu", "text", "invent" };
    const char *type_name = type == kNhwRip ? "rip" : ((type >= 1 && type <= 6) ? kTypes[type] : "");
    const char *op_name = "";
    switch (op) {
    case NH_UI_WIN_CREATE: op_name = "create"; break;
    case NH_UI_WIN_CLEAR: op_name = "clear"; break;
    case NH_UI_WIN_DISPLAY: op_name = "display"; break;
    case NH_UI_WIN_DESTROY: op_name = "destroy"; break;
    default: return;
    }
    // メッセージの窓の模型（--More-- の規則）の始末
    if (type == 1) {
        if (op == NH_UI_WIN_CREATE) {
            g_msg = MsgWin{};
        } else if (op == NH_UI_WIN_CLEAR) {
            msg_clear();
        }
    }
    Json m;
    m["k"] = "win";
    m["op"] = op_name;
    m["id"] = id;
    m["type"] = type_name;
    if (blocking != 0) m["blocking"] = true;
    (void)ui_send(std::move(m));
}

void nh_host_ui_wait_any_key(void)
{
    if (!ui_on()) return;
    back_to_map();
    (void)wait_key(NH_KEY_OTHER);
}

void nh_host_ui_putstr(int id, int attr, const char *text, int msg_lines)
{
    if (!ui_on() || text == nullptr) return;
    ui_message_putstr(id, attr, ui_text(text), msg_lines);
}

void nh_host_ui_curs(int id, int x, int y)
{
    if (!ui_on()) return;
    Json m;
    m["k"] = "curs";
    m["id"] = id;
    m["x"] = x;
    m["y"] = y;
    (void)ui_send(std::move(m));
}

void nh_host_ui_cliparound(int x, int y)
{
    if (!ui_on()) return;
    Json m;
    m["k"] = "cliparound";
    m["x"] = x;
    m["y"] = y;
    (void)ui_send(std::move(m));
}

void nh_host_ui_status(int field, const char *text, int color, int attr, int pct)
{
    if (!ui_on() || field < 0 || field >= NH_BL_MAX || text == nullptr) return;
    UiStatusField &f = g_ui_status[field];
    const std::string shown = ui_text(text);
    if (f.set && f.text == shown && f.color == color && f.attr == attr && f.pct == pct) return;
    f.text = shown;
    f.color = color;
    f.attr = attr;
    f.pct = pct;
    f.set = true;
    f.dirty = true;
}

void nh_host_ui_status_cond(unsigned long mask, int n, const char *const *names, const int *colors, const int *attrs)
{
    (void)mask;
    if (!ui_on()) return;
    std::vector<UiCond> list;
    for (int i = 0; i < n; ++i) {
        UiCond cond;
        cond.name = ui_text(names[i]);
        cond.color = colors[i];
        cond.attr = attrs[i];
        list.push_back(std::move(cond));
    }
    bool same = g_ui_conds_set && list.size() == g_ui_conds.size();
    for (std::size_t i = 0; same && i < list.size(); ++i) {
        same = list[i].name == g_ui_conds[i].name && list[i].color == g_ui_conds[i].color && list[i].attr == g_ui_conds[i].attr;
    }
    if (same) return;
    g_ui_conds = std::move(list);
    g_ui_conds_set = true;
    g_ui_conds_dirty = true;
}

void nh_host_ui_status_flush(int is_reset)
{
    if (!ui_on()) return;
    Json fields = Json::array();
    for (int i = 0; i < NH_BL_MAX; ++i) {
        UiStatusField &f = g_ui_status[i];
        if (!f.set || (!f.dirty && is_reset == 0)) continue;
        Json j;
        j["f"] = i;
        j["text"] = f.text;
        j["color"] = f.color;
        j["attr"] = f.attr;
        if (f.pct >= 0) j["pct"] = f.pct;
        fields.push_back(std::move(j));
        f.dirty = false;
    }
    const bool send_cond = g_ui_conds_set && (g_ui_conds_dirty || is_reset != 0);
    if (fields.empty() && !send_cond && is_reset == 0) return;
    Json m;
    m["k"] = "status";
    m["fields"] = std::move(fields);
    if (send_cond) {
        Json cond = Json::array();
        for (const UiCond &c : g_ui_conds) {
            Json j;
            j["name"] = c.name;
            j["color"] = c.color;
            j["attr"] = c.attr;
            cond.push_back(std::move(j));
        }
        m["cond"] = std::move(cond);
        g_ui_conds_dirty = false;
    }
    if (is_reset != 0) m["reset"] = true;
    else m["flush"] = true;
    (void)ui_send(std::move(m));
}

void nh_host_ui_bell(void)
{
    if (!ui_on()) return;
    Json m;
    m["k"] = "bell";
    (void)ui_send(std::move(m));
}

void nh_host_ui_pref(const char *name, const char *value)
{
    if (!ui_on() || name == nullptr) return;
    Json m;
    m["k"] = "pref";
    m["name"] = ui_text(name);
    m["value"] = ui_text(value);
    (void)ui_send(std::move(m));
}

int nh_host_menu_win(int win_id, int win_type, int how, const char *prompt, const nh_menu_item *items, int count, int *chosen,
                     long *counts)
{
    if (ui_on()) return ui_menu(win_id, win_type, how, prompt, items, count, chosen, counts);
    return nh_host_menu(how, prompt, items, count, chosen, counts);
}

void nh_host_text_win(int win_id, const nh_menu_item *lines, int count)
{
    if (ui_on()) {
        ui_text_window("text", win_id, lines, count, nullptr);
        return;
    }
    (void)nh_host_menu(NH_PICK_NONE, nullptr, lines, count, nullptr, nullptr);
}

void nh_host_rip(int win_id, const char *name, long gold, const char *killer, int year, const nh_menu_item *lines, int count)
{
    if (ui_on()) {
        Json rip;
        rip["name"] = ui_text(name);
        rip["gold"] = gold;
        rip["killer"] = ui_text(killer);
        rip["year"] = year;
        ui_text_window("rip", win_id, lines, count, &rip);
        return;
    }
    (void)nh_host_menu(NH_PICK_NONE, nullptr, lines, count, nullptr, nullptr);
}

int nh_host_ask_text(int kind, const char *query, char *buf, int bufsize, const char *const *cmds, int ncmds)
{
    if ((kind == NH_ASK_EXT_CMD) && (buf != nullptr) && (bufsize > 0)) {
        // コマンドの欄で選んだ拡張コマンド（receive_loop の extcmd）。窓を出さずに、その名前を答える
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (!g_pending_extcmd.empty()) {
            std::snprintf(buf, static_cast<std::size_t>(bufsize), "%s", g_pending_extcmd.c_str());
            g_pending_extcmd.clear();
            return 1;
        }
        // パッドの割り当ての命令（`#`・名前・Enter の列。build_pad_commands）。名前が Enter まで積まれていれば、窓を出さずにその名前を答える
        const auto enter = std::find_if(g_keys.begin(), g_keys.end(), [](int key) { return key == 0x0D || key == 0x0A; });
        if (enter != g_keys.end()) {
            std::string name;
            for (auto it = g_keys.begin(); it != enter; ++it) name.push_back(static_cast<char>(*it));
            g_keys.erase(g_keys.begin(), enter + 1);
            std::snprintf(buf, static_cast<std::size_t>(bufsize), "%s", name.c_str());
            return 1;
        }
    }
    if (ui_on()) return ui_ask_text(kind, query, buf, bufsize, cmds, ncmds);
    return nh_host_getlin(query, buf, bufsize);
}

int nh_host_select(const nh_select_table *table, int result[4])
{
    if (!ui_on()) {
        result[0] = result[1] = result[2] = result[3] = -1;
        return 1;
    }
    return ui_select(table, result);
}

int nh_host_getkey_pos(int mode, int *x, int *y, int *mod)
{
    back_to_map();
    UiPos pos;
    const int key = wait_key_impl(mode, &pos);
    if (key == 0) {
        if (x != nullptr) *x = pos.x;
        if (y != nullptr) *y = pos.y;
        if (mod != nullptr) *mod = pos.mod;
    }
    return key;
}

} // extern "C"

namespace {

std::string join(std::string dir, const char *name)
{
    if (!dir.empty() && dir.back() != portable::kPathSep) dir.push_back(portable::kPathSep);
    return dir + name;
}

/*
 * 原作のタイルの絵から作ったボクセルの板（決定 D19）。
 *
 * 板の置き場は <実行ファイルの隣>/nethack/data/slab（tools/nethack/tiles_to_slabs.py が組み立てのときに作る。言語に依らない）。
 * 表 slabs.tsv は「絵の番号<TAB>板の名前」（板にする絵だけ）。glyph → 絵の番号は各言語のデータの tiles.json（glyph2tile）。
 * どちらかが無い・読めないときは、板を使わない（絵の索引を付けず、従来どおり字の板で出す）。
 */
struct SlabTable {
    struct Entry {
        int tile{ 0 };
        std::string name;
    };
    std::string dir;                 //!< 板の置き場（実行ファイルの隣の native パス）
    std::vector<Entry> entries;      //!< slabs.tsv の行
    std::vector<int> glyph_to_tile;  //!< tiles.json の glyph2tile
    int monsters_last{ -1 };         //!< tiles.json の tile_ranges.monsters の最後（怪物の板は 0〜ここ）
    int objects_first{ -1 };
    int objects_last{ -1 };
    int statues_first{ -1 };
    int statues_last{ -1 };
    int nummons{ 0 };
    //! 絵の番号 → 目録の id（R: 怪物の番号 + 1、K: アイテムの番号 + 1。マスの monster_id / object_id と同じ数え方）。無ければ 0。
    std::vector<int> id_of_tile;
    bool ok() const { return !entries.empty() && !glyph_to_tile.empty(); }
};

SlabTable g_slabs;

SlabTable load_slab_table(const std::string &slab_dir, const std::string &data_dir)
{
    SlabTable table;
    table.dir = slab_dir;
    std::ifstream tsv(join(slab_dir, "slabs.tsv"), std::ios::binary);
    if (!tsv) return table;
    std::string line;
    while (std::getline(tsv, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 >= line.size()) continue;
        SlabTable::Entry entry;
        entry.tile = std::atoi(line.c_str());
        entry.name = line.substr(tab + 1);
        if (entry.tile < 0 || entry.name.empty()) continue;
        table.entries.push_back(std::move(entry));
    }
    std::ifstream tiles_file(join(data_dir, "tiles.json"), std::ios::binary);
    if (!tiles_file) {
        table.entries.clear();
        return table;
    }
    const Json tiles = Json::parse(tiles_file, nullptr, false);
    if (!tiles.is_object() || !tiles.contains("glyph2tile") || !tiles["glyph2tile"].is_array()) {
        table.entries.clear();
        return table;
    }
    table.glyph_to_tile.reserve(tiles["glyph2tile"].size());
    for (const Json &v : tiles["glyph2tile"]) table.glyph_to_tile.push_back(v.is_number_integer() ? v.get<int>() : -1);
    const auto range = [&](const char *key, int &first, int &last) {
        if (tiles.contains("tile_ranges") && tiles["tile_ranges"].contains(key) && tiles["tile_ranges"][key].is_array()
            && tiles["tile_ranges"][key].size() == 2) {
            first = tiles["tile_ranges"][key][0].get<int>();
            last = tiles["tile_ranges"][key][1].get<int>();
        }
    };
    int monsters_first = -1;
    range("monsters", monsters_first, table.monsters_last);
    range("objects", table.objects_first, table.objects_last);
    range("statues", table.statues_first, table.statues_last);
    if (tiles.contains("nummons") && tiles["nummons"].is_number_integer()) table.nummons = tiles["nummons"].get<int>();
    if (table.monsters_last < 0 || table.objects_first < 0 || table.statues_first < 0) {
        table.entries.clear();
        return table;
    }
    /*
     * 目録の id は glyph から逆に引く（アイテムの絵は原作のアイテムの番号より 3 個多く、絵の番号と番号が単純にはずれない）。
     * 怪物は男・女の glyph（GLYPH_MON_OFF から NUMMONS × 2 個）、アイテムは GLYPH_OBJ_OFF から num_objects 個。
     * 同じ絵を指す glyph が複数あれば、小さい番号を採る。
     */
    table.id_of_tile.assign(tiles.value("tile_count", 0) > 0 ? static_cast<std::size_t>(tiles["tile_count"].get<int>()) : 0, 0);
    const auto set_id = [&](int glyph, int id) {
        if (glyph < 0 || static_cast<std::size_t>(glyph) >= table.glyph_to_tile.size()) return;
        const int tile = table.glyph_to_tile[static_cast<std::size_t>(glyph)];
        if (tile < 0 || static_cast<std::size_t>(tile) >= table.id_of_tile.size()) return;
        if (table.id_of_tile[static_cast<std::size_t>(tile)] == 0) table.id_of_tile[static_cast<std::size_t>(tile)] = id;
    };
    if (table.nummons > 0) {
        for (int g = 0; g < 2 * table.nummons; ++g) set_id(g, g % table.nummons + 1);
    }
    if (tiles.contains("glyph_offsets") && tiles["glyph_offsets"].contains("GLYPH_OBJ_OFF") && tiles.contains("num_objects")) {
        const int obj_off = tiles["glyph_offsets"]["GLYPH_OBJ_OFF"].get<int>();
        for (int n = 0; n < tiles["num_objects"].get<int>(); ++n) set_id(obj_off + n, n + 1);
    }
    return table;
}

/*!
 * 板の目録（asset_manifest）。絵の番号 n の索引は n + 1（0 は「絵は無い」の意味。nethack::tile_index_of）、path は「<板の名前>.png」
 * （画面は拡張子とフォルダを捨てた名前で、hello_ack.asset_roots.slab の置き場から板を探す）。種別: 怪物・主人公の職業・見えない印は "R"、
 * アイテムは "K"、石像は "P"。id は R が怪物の番号 + 1（男女の絵とも。どの怪物の glyph も指さない絵〔見えない印など〕は絵の番号 + 1）、
 * K が原作のアイテムの番号 + 1（マスの object_id と同じ。持ち物のアイコンの引き当てに使われる）、P は 0。
 */
std::string build_asset_manifest(const SlabTable &table)
{
    presentation::AssetManifestMessage manifest;
    manifest.root = portable::path_to_utf8(table.dir);
    manifest.assets.reserve(table.entries.size());
    for (const SlabTable::Entry &entry : table.entries) {
        presentation::AssetManifestWireEntry wire;
        wire.index = nethack::tile_index_of(entry.tile);
        wire.path = entry.name + ".png";
        const int mapped_id = (static_cast<std::size_t>(entry.tile) < table.id_of_tile.size()) ? table.id_of_tile[static_cast<std::size_t>(entry.tile)] : 0;
        if (entry.tile <= table.monsters_last) {
            wire.kind = "R";
            wire.id = mapped_id > 0 ? mapped_id : entry.tile + 1;
        } else if (entry.tile >= table.objects_first && entry.tile <= table.objects_last) {
            wire.kind = "K";
            wire.id = mapped_id > 0 ? mapped_id : entry.tile - table.objects_first + 1;
        } else if (entry.tile >= table.statues_first && entry.tile <= table.statues_last) {
            wire.kind = "P";
            wire.id = 0;
        } else {
            continue; // 板にならない範囲（地形ほか・印）。slabs.tsv には載らない
        }
        manifest.assets.push_back(std::move(wire));
    }
    return presentation::encode_asset_manifest(manifest);
}

/*
 * パッドとコマンドメニューの命令の一覧。原作の命令表（cmd.c の extcmdlist）のうち、ウィザードモード専用・内部用・この環境で使えない物
 * （shell・suspend・bugreport）を除いた全部（決めたこと 2026-10-31「すべての行動を割り当てられるように」）。
 * 番号（id）は命令ごとに固定（cfg の割り当てがこの番号を覚える。増やすときは新しい番号を足す）。
 * key が 0 の命令はキーで打てない（Meta のキー・割り当ての無い物）ので、`#` と名前と Enter の列で送る（nh_host_ask_text が窓を出さずに答える）。
 * ローグライクの配置は無いので、どちらも同じ列を送る。空の一覧を送るとコマンドメニューを開いた画面側が落ちる（既知の罠）。
 */
presentation::PadCommandsMessage build_pad_commands(bool ja)
{
    enum Group { Act, Move, Item, Magic, Info, Sys };
    struct Row {
        int id;
        Group group;
        int key; //!< 0 = 拡張コマンドの名前で送る
        const char *name;
        const char *en;
        const char *ja;
    };
    const auto ctrl = [](char c) { return c & 0x1f; };
    const Row rows[] = {
        // 行動
        { 11, Act, '#', "#", "Extended command", "拡張コマンド" },
        { 1, Act, '.', "wait", "Rest", "休む" },
        { 2, Act, 's', "search", "Search", "探す" },
        { 3, Act, ',', "pickup", "Pick up", "拾う" },
        { 4, Act, '<', "up", "Go up", "上る" },
        { 5, Act, '>', "down", "Go down", "下りる" },
        { 6, Act, 'o', "open", "Open", "開ける" },
        { 7, Act, 'c', "close", "Close", "閉める" },
        { 8, Act, ctrl('D'), "kick", "Kick", "蹴る" },
        { 9, Act, ':', "look", "Look here", "足元を見る" },
        { 10, Act, 'p', "pay", "Pay", "払う" },
        { 100, Act, 'F', "fight", "Fight (prefix)", "強制攻撃（接頭）" },
        { 101, Act, 'm', "reqmenu", "Menu request (prefix)", "メニュー要求（接頭）" },
        { 102, Act, 0, "chat", "Chat", "話す" },
        { 103, Act, 0, "force", "Force a lock", "鍵をこじ開ける" },
        { 104, Act, 0, "jump", "Jump", "跳ぶ" },
        { 105, Act, 0, "loot", "Loot", "箱を開ける" },
        { 106, Act, 0, "monster", "Monster ability", "怪物の能力" },
        { 107, Act, 0, "offer", "Offer", "捧げる" },
        { 108, Act, 0, "ride", "Ride", "乗る・降りる" },
        { 109, Act, 0, "sit", "Sit", "座る" },
        { 110, Act, ctrl('T'), "teleport", "Teleport", "テレポート" },
        { 111, Act, 0, "turn", "Turn undead", "アンデッド退散" },
        { 112, Act, 0, "untrap", "Untrap", "罠を外す" },
        { 113, Act, 0, "wipe", "Wipe face", "顔を拭く" },
        { 114, Act, ctrl('A'), "repeat", "Repeat", "繰り返す" },
        { 115, Act, '_', "travel", "Travel", "移動先を指定" },
        { 116, Act, 0, "retravel", "Travel again", "前の移動先へ" },
        { 117, Act, 0, "herecmdmenu", "Actions here", "ここでできる行動" },
        { 118, Act, 0, "therecmdmenu", "Actions next to you", "隣でできる行動" },
        { 119, Act, 'E', "engrave", "Engrave", "床に刻む" },
        // 移動
        { 130, Move, 0, "movewest", "Move west", "西へ動く" },
        { 131, Move, 0, "movenorthwest", "Move northwest", "北西へ動く" },
        { 132, Move, 0, "movenorth", "Move north", "北へ動く" },
        { 133, Move, 0, "movenortheast", "Move northeast", "北東へ動く" },
        { 134, Move, 0, "moveeast", "Move east", "東へ動く" },
        { 135, Move, 0, "movesoutheast", "Move southeast", "南東へ動く" },
        { 136, Move, 0, "movesouth", "Move south", "南へ動く" },
        { 137, Move, 0, "movesouthwest", "Move southwest", "南西へ動く" },
        { 140, Move, 0, "rushwest", "Rush west", "西へ突進" },
        { 141, Move, 0, "rushnorthwest", "Rush northwest", "北西へ突進" },
        { 142, Move, 0, "rushnorth", "Rush north", "北へ突進" },
        { 143, Move, 0, "rushnortheast", "Rush northeast", "北東へ突進" },
        { 144, Move, 0, "rusheast", "Rush east", "東へ突進" },
        { 145, Move, 0, "rushsoutheast", "Rush southeast", "南東へ突進" },
        { 146, Move, 0, "rushsouth", "Rush south", "南へ突進" },
        { 147, Move, 0, "rushsouthwest", "Rush southwest", "南西へ突進" },
        { 150, Move, 0, "runwest", "Run west", "西へ走る" },
        { 151, Move, 0, "runnorthwest", "Run northwest", "北西へ走る" },
        { 152, Move, 0, "runnorth", "Run north", "北へ走る" },
        { 153, Move, 0, "runnortheast", "Run northeast", "北東へ走る" },
        { 154, Move, 0, "runeast", "Run east", "東へ走る" },
        { 155, Move, 0, "runsoutheast", "Run southeast", "南東へ走る" },
        { 156, Move, 0, "runsouth", "Run south", "南へ走る" },
        { 157, Move, 0, "runsouthwest", "Run southwest", "南西へ走る" },
        { 158, Move, 'G', "run", "Run (prefix)", "走る（接頭）" },
        { 159, Move, 'g', "rush", "Rush (prefix)", "突進（接頭）" },
        // 道具
        { 20, Item, 'i', "inventory", "Inventory", "持ち物" },
        { 160, Item, 'I', "inventtype", "Inventory by type", "種類別の持ち物" },
        { 21, Item, 'a', "apply", "Apply", "使う" },
        { 22, Item, 'e', "eat", "Eat", "食べる" },
        { 23, Item, 'q', "quaff", "Quaff", "飲む" },
        { 24, Item, 'r', "read", "Read", "読む" },
        { 25, Item, 'w', "wield", "Wield", "構える" },
        { 26, Item, 'W', "wear", "Wear", "着る" },
        { 27, Item, 'T', "takeoff", "Take off", "脱ぐ" },
        { 161, Item, 'A', "takeoffall", "Take off all", "全部脱ぐ" },
        { 28, Item, 'P', "puton", "Put on", "身につける" },
        { 29, Item, 'R', "remove", "Remove", "外す" },
        { 30, Item, 'd', "drop", "Drop", "置く" },
        { 162, Item, 'D', "droptype", "Drop many", "まとめて置く" },
        { 31, Item, 't', "throw", "Throw", "投げる" },
        { 32, Item, 'f', "fire", "Fire", "撃つ" },
        { 33, Item, 'x', "swap", "Swap weapons", "武器の持ち替え" },
        { 163, Item, 'X', "twoweapon", "Two-weapon combat", "二刀流の切り替え" },
        { 34, Item, 'z', "zap", "Zap a wand", "杖を振る" },
        { 62, Item, 'Q', "quiver", "Quiver", "矢筒に入れる" },
        { 164, Item, 0, "dip", "Dip", "浸す" },
        { 165, Item, 0, "invoke", "Invoke", "力を引き出す" },
        { 166, Item, 0, "rub", "Rub", "こする" },
        { 167, Item, 0, "tip", "Tip container", "容器を空ける" },
        { 168, Item, 0, "adjust", "Adjust letters", "持ち物の文字を替える" },
        { 169, Item, 'C', "call", "Call", "名前を付ける" },
        { 170, Item, 0, "name", "Name", "名付ける" },
        // 魔法
        { 40, Magic, 'Z', "cast", "Cast a spell", "呪文を唱える" },
        { 41, Magic, '+', "showspells", "Known spells", "覚えた呪文" },
        { 42, Magic, 0, "pray", "Pray", "祈る" },
        { 171, Magic, 0, "enhance", "Enhance skills", "技能を伸ばす" },
        // 情報
        { 50, Info, ';', "glance", "Farlook", "遠くを見る" },
        { 51, Info, '/', "whatis", "What is this?", "記号の説明" },
        { 188, Info, '&', "whatdoes", "What does this key do?", "キーの説明" },
        { 185, Info, 0, "lookaround", "Describe surroundings", "周りの説明" },
        { 186, Info, '^', "showtrap", "Adjacent trap", "隣の罠を見る" },
        { 187, Info, 0, "terrain", "Terrain map", "地形の地図" },
        { 52, Info, '\\', "known", "Discoveries", "発見したもの" },
        { 178, Info, '`', "knownclass", "Discoveries by class", "種類別の発見" },
        { 53, Info, ctrl('P'), "prevmsg", "Previous messages", "過去のメッセージ" },
        { 54, Info, ctrl('X'), "attributes", "Attributes", "能力" },
        { 55, Info, '$', "showgold", "Gold", "所持金" },
        { 172, Info, '*', "seeall", "Equipment in use", "使っている装備" },
        { 173, Info, '"', "seeamulet", "Amulet worn", "身につけた魔除け" },
        { 174, Info, '[', "seearmor", "Armor worn", "着ている鎧" },
        { 175, Info, '=', "seerings", "Rings worn", "はめている指輪" },
        { 176, Info, '(', "seetools", "Tools in use", "使っている道具" },
        { 177, Info, ')', "seeweapon", "Weapon wielded", "構えた武器" },
        { 179, Info, ctrl('O'), "overview", "Dungeon overview", "迷宮の概要" },
        { 180, Info, 0, "annotate", "Name this level", "この階に名前を付ける" },
        { 181, Info, 'v', "chronicle", "Chronicle", "出来事の記録" },
        { 182, Info, 0, "conduct", "Conduct", "守っている誓い" },
        { 183, Info, 0, "genocided", "Genocided", "絶滅させた怪物" },
        { 184, Info, 0, "vanquished", "Vanquished", "倒した怪物" },
        { 189, Info, 0, "history", "History", "ゲームの歴史" },
        { 190, Info, 0, "version", "Version (full)", "バージョンの詳細" },
        { 64, Info, 'V', "versionshort", "Version", "バージョン" },
        { 191, Info, '|', "perminv", "Scroll inventory window", "持ち物の窓を送る" },
        // システム
        { 60, Sys, 'O', "options", "Options", "オプション" },
        { 192, Sys, 0, "optionsfull", "All options", "全オプション" },
        { 193, Sys, 0, "toggle", "Toggle an option", "オプションの入切" },
        { 194, Sys, 0, "saveoptions", "Save options", "オプションを保存" },
        { 196, Sys, '@', "autopickup", "Toggle autopickup", "自動拾いの入切" },
        { 197, Sys, ctrl('R'), "redraw", "Redraw", "画面を描き直す" },
        { 63, Sys, '?', "help", "Help", "ヘルプ" },
        { 198, Sys, 0, "?", "List extended commands", "拡張コマンドの一覧" },
        { 199, Sys, 0, "exploremode", "Explore mode", "探検モード" },
        { 61, Sys, 'S', "save", "Save", "セーブ" },
        { 195, Sys, 0, "quit", "Quit", "終了" },
    };
    static const char *const kGroups[][2] = {
        { "Action", "行動" }, { "Movement", "移動" }, { "Items", "道具" }, { "Magic", "魔法" }, { "Information", "情報" }, { "System", "システム" },
    };
    presentation::PadCommandsMessage commands;
    commands.current_keymap = "original";
    for (const Row &row : rows) {
        presentation::PadCommandWireEntry entry;
        entry.id = row.id;
        entry.group_utf8 = kGroups[row.group][ja ? 1 : 0];
        entry.label_utf8 = ja ? row.ja : row.en;
        std::vector<int> seq;
        if (row.key != 0) {
            entry.command = row.key;
            seq.push_back(row.key);
        } else {
            seq.push_back('#');
            for (const char *c = row.name; *c != '\0'; ++c) seq.push_back(static_cast<unsigned char>(*c));
            seq.push_back(0x0D);
        }
        entry.seq_rogue = seq;
        entry.seq_original = std::move(seq);
        commands.entries.push_back(std::move(entry));
    }
    return commands;
}

/*!
 * @brief コマンドの欄（nethack-ui の cmdbar）。タッチだけの端末でもコマンドを打てるよう、画面が絵のボタンとして並べる（決めたこと 2026-10-28。
 * ）。並びは使う頻度の順。絵は nh_cmdbar_glyph（無ければキーの字）。キーで打てない物は拡張コマンドの名前で送る。
 */
Json build_cmdbar()
{
    struct Item {
        int id;
        std::vector<int> keys; //!< 空なら ext を使う
        const char *ext;
        const char *text; //!< 絵が無いときに描く字
        const char *en;
        const char *ja;
    };
    const auto ctrl = [](char c) { return c & 0x1f; };
    const Item items[] = {
        { 20, { 'i' }, nullptr, "i", "Inventory", "持ち物" },
        { 3, { ',' }, nullptr, ",", "Pick up", "拾う" },
        { 30, { 'd' }, nullptr, "d", "Drop", "置く" },
        { 22, { 'e' }, nullptr, "e", "Eat", "食べる" },
        { 23, { 'q' }, nullptr, "q", "Quaff", "飲む" },
        { 24, { 'r' }, nullptr, "r", "Read", "読む" },
        { 26, { 'W' }, nullptr, "W", "Wear", "着る" },
        { 27, { 'T' }, nullptr, "T", "Take off", "脱ぐ" },
        { 25, { 'w' }, nullptr, "w", "Wield", "構える" },
        { 32, { 'f' }, nullptr, "f", "Fire", "撃つ" },
        { 31, { 't' }, nullptr, "t", "Throw", "投げる" },
        { 21, { 'a' }, nullptr, "a", "Apply", "使う" },
        { 34, { 'z' }, nullptr, "z", "Zap a wand", "杖を振る" },
        { 40, { 'Z' }, nullptr, "Z", "Cast a spell", "呪文を唱える" },
        { 28, { 'P' }, nullptr, "P", "Put on", "身につける" },
        { 29, { 'R' }, nullptr, "R", "Remove", "外す" },
        { 33, { 'x' }, nullptr, "x", "Swap weapons", "武器の持ち替え" },
        { 35, { 'Q' }, nullptr, "Q", "Quiver", "矢筒に入れる" },
        { 6, { 'o' }, nullptr, "o", "Open", "開ける" },
        { 7, { 'c' }, nullptr, "c", "Close", "閉める" },
        { 8, { ctrl('D') }, nullptr, "^D", "Kick", "蹴る" },
        { 4, { '<' }, nullptr, "<", "Go up", "上る" },
        { 5, { '>' }, nullptr, ">", "Go down", "下りる" },
        { 2, { 's' }, nullptr, "s", "Search", "探す" },
        { 1, { '.' }, nullptr, ".", "Rest", "休む" },
        { 9, { ':' }, nullptr, ":", "Look here", "足元を見る" },
        { 10, { 'p' }, nullptr, "p", "Pay", "払う" },
        { 42, {}, "pray", "#", "Pray", "祈る" },
        { 41, { '+' }, nullptr, "+", "Known spells", "覚えた呪文" },
        { 50, { ';' }, nullptr, ";", "Farlook", "遠くを見る" },
        { 51, { '/' }, nullptr, "/", "What is this?", "記号の説明" },
        { 52, { '\\' }, nullptr, "\\", "Discoveries", "発見したもの" },
        { 53, { ctrl('P') }, nullptr, "^P", "Previous messages", "過去のメッセージ" },
        { 54, { ctrl('X') }, nullptr, "^X", "Attributes", "能力" },
        { 55, { '$' }, nullptr, "$", "Gold", "所持金" },
        { 60, { 'O' }, nullptr, "O", "Options", "オプション" },
        { 63, { '?' }, nullptr, "?", "Help", "ヘルプ" },
        { 61, { 'S' }, nullptr, "S", "Save", "セーブ" },
        { 62, {}, "quit", "#", "Quit", "終了" },
        { 11, { '#' }, nullptr, "#", "Extended command", "拡張コマンド" },
    };
    Json list = Json::array();
    for (const Item &it : items) {
        Json j;
        j["id"] = it.id;
        if (it.ext != nullptr) {
            j["ext"] = it.ext;
        } else {
            j["keys"] = it.keys;
        }
        j["glyph"] = nh_cmdbar_glyph(it.id);
        j["text"] = it.text;
        j["en"] = it.en;
        j["ja"] = it.ja;
        list.push_back(std::move(j));
    }
    Json m;
    m["k"] = "cmdbar";
    m["items"] = std::move(list);
    return m;
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

    /*
     * データは実行ファイルの隣の nethack/data/<en|ja>。原作のタイルの絵から作った板は nethack/data/slab（言語に依らない）。
     * 板の表が読めたときだけ、板の置き場を hello_ack.asset_roots.slab で申告し、目録（asset_manifest）を送る
     * （読めなければ、従来どおり怪物・アイテムは字の板で出る）。
     */
    const std::string data = join(join(join(portable::exe_dir(), "nethack"), "data"), kLangDir);
    g_slabs = load_slab_table(join(join(join(portable::exe_dir(), "nethack"), "data"), "slab"), data);

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = kCoreName;
    ack.core_version = kCoreVersion;
    if (g_slabs.ok()) ack.asset_root_slab = portable::path_to_utf8(g_slabs.dir);
    // 画面が専用の画面の部品を持っている（hello に nethack-ui）なら、こちらも申告して知らせを送る
    if (std::find(hello.features.begin(), hello.features.end(), std::string(kFeatureUi)) != hello.features.end()) {
        ack.features.push_back(kFeatureUi);
        g_ui_active.store(true);
        g_view.ui_mode = true;
    }
    std::string ack_wire = presentation::encode_hello_ack(ack);
    if (ui_on()) {
        /*
         * 専用の画面（nethack-ui）にだけ、タイルの絵（tiles.png・tiles.json。メニューの行の頭の絵）の置き場を
         * asset_roots.nethack_tiles として申告する。encode_hello_ack は graf と slab しか
         * 書かないので、できた JSON へ足す。画面の握手の読み取りは未知のキーを捨てるので、同じ値を pref（nethack_tiles）でも送る。
         */
        Json wire = Json::parse(ack_wire, nullptr, false);
        const std::string tiles_dir = join(join(join(portable::exe_dir(), "nethack"), "data"), kLangDir);
        if (wire.is_object() && portable::dir_exists(tiles_dir)) {
            wire["asset_roots"]["nethack_tiles"] = tiles_dir;
            ack_wire = wire.dump(-1, ' ', false, Json::error_handler_t::replace);
        }
    }
    if (!send_message(ack_wire)) return 5;
    if (!send_message(presentation::encode_pad_commands(build_pad_commands(lang_is_ja())))) return 5;
    // サブパネルの種類。原作に別窓は無いので、Rogue 系と同じく画面側の既定の 5 種を選ばせる。
    if (!send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(!lang_is_ja())))) return 5;
    if (g_slabs.ok()) {
        std::vector<int> slab_tiles;
        slab_tiles.reserve(g_slabs.entries.size());
        for (const SlabTable::Entry &entry : g_slabs.entries) slab_tiles.push_back(entry.tile);
        nethack::set_tile_tables(g_slabs.glyph_to_tile, slab_tiles);
        // 目録は板がそろった今、1 回だけ（最初の frame より前）。ゲームの途中では送り直さない
        if (!send_message(build_asset_manifest(g_slabs))) return 5;
        std::fprintf(stderr, "[nethack] slabs: %zu tiles from %s\n", g_slabs.entries.size(), g_slabs.dir.c_str());
    } else {
        std::fprintf(stderr, "[nethack] no slab table under %s (monsters and items are drawn as letters)\n", g_slabs.dir.c_str());
    }

    std::thread(receive_loop, g_in).detach();

    /*
     * データは実行ファイルの隣の nethack/data/<en|ja>、書くもの（セーブ・ロック・スコア）は
     * data_dir()/nethack/<en|ja>。開発ツリーの直下へは書かない。言語ごとに分ける
     * （英語版のセーブは日本語版で読めるが、その逆は読めない）。
     */
    if (!portable::dir_exists(data)) {
        std::fprintf(stderr, "[nethack] data directory not found: %s\n", data.c_str());
        return 6;
    }
    const std::string root = portable::data_dir();
    if (root.empty()) return 6;
    const std::string nethack_home = join(root, "nethack");
    const std::string home = join(nethack_home, kLangDir);
    if (!portable::make_dir(nethack_home) || !portable::make_dir(home)) {
        std::fprintf(stderr, "[nethack] cannot prepare the data directory\n");
        return 6;
    }
    std::atexit(send_exit_at_process_end);
    if (ui_on()) {
        // タイルの絵の置き場（画面は握手の asset_roots を読まないので、pref でも知らせる）
        Json tiles;
        tiles["k"] = "pref";
        tiles["name"] = "nethack_tiles";
        tiles["value"] = data;
        (void)ui_send(std::move(tiles));
        (void)ui_send(build_cmdbar()); // コマンドの欄（絵は glyph。画面がタイルの絵の表で引く）
    }

    const int rc = nh_game_main(data.c_str(), home.c_str());
    send_exit(rc);
    return rc;
}

} // namespace

int main(int argc, char **argv)
{
    bool protocol = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--ui-protocol=stdio") protocol = true;
    }
    if (!protocol) {
        std::fprintf(stderr, "%s --ui-protocol=stdio\n", kCoreName);
        return 2;
    }
    presentation::set_stdio_binary_mode();
    presentation::TransportHandle out = presentation::transport_stdout();
#ifdef _WIN32
    /*
     * 原作や Lua が標準出力へ字を書くとプロトコルが壊れる。プロトコル用の出力は複製したハンドルで持ち、
     * CRT の標準出力は標準エラーへ向ける（複製せずに向け直すと、元のハンドルが閉じてしまう）。
     */
    HANDLE original = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE copy = nullptr;
    if (original != INVALID_HANDLE_VALUE && original != nullptr
        && DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        if (_dup2(_fileno(stderr), _fileno(stdout)) == 0) out = static_cast<presentation::TransportHandle>(copy);
        else CloseHandle(copy);
    }
#endif
    return run_protocol(presentation::transport_stdin(), out);
}
