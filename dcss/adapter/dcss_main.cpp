// DCSS コア: プロトコル v1（長さ付きの JSON・握手）の入口。
//
// 原作の main()（main.cc）は組み立てで dcss_original_main に名前を替えてあり（vcxproj の PreprocessorDefinitions。原作の行は書き換えない）、
// ここが本物の main()。画面（HengbandHd2d.exe）が起こすときの `--ui-protocol=stdio` があれば:
//   1. 標準出力をプロトコル専用にして（CRT の標準出力は標準エラーへ向ける）、握手（hello / hello_ack）を済ませる。
//   2. 原作の送信（tileweb.cc の sendto）を dcss_posix_shim.cpp の差し込み口で受け、dcss_frame.cpp で GameFrame にして送る。
//   3. 受信スレッドが画面の入力（keys / input_event / ui_state / quit_request）を、原作のウェブタイル版の制御メッセージ
//      （{"msg":"key","keycode":N}）に直して原作へ渡す。
//   4. 原作の main を呼ぶ。終わったら exit を送る。
// 無ければ従来の仮の入出力（m0_check.py が使う。標準入力の 1 行 = 1 制御メッセージ）。
#include "dcss_frame.h"
#include "dcss_posix_shim.h"
#include "dcss_text.h"

#include "bridge/native_modern.h"
#include "bridge/rogue_sub_panels.h"
#include "frame/frame_codec.h"
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
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>

#ifdef _WIN32
#include <io.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <csignal>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

int dcss_original_main(int argc, char *argv[]);

namespace {

constexpr const char *kCoreName = "dcss";
constexpr const char *kCoreVersion = "0.34.1-ja4";
//! 原作（ウェブタイル版）の JSON を加工せずに画面へ渡す経路の名前（features）。コアが hello_ack で申告し、画面が hello の features で挙げたときだけ使う。
//! 出す: {"t":"webtiles","line":"<原作が出した 1 メッセージの JSON の字（先頭 * はサーバ宛の制御）>"}。
//! 受ける: {"t":"webtiles_input","line":"<原作の制御メッセージの JSON の字。例 {\"msg\":\"key\",\"keycode\":13}>"}。
constexpr const char *kFeatureWebtiles = "dcss-webtiles";
//! 画面が原作のタイル版の UI を再現する専用の部品を持っているときの申告（hello の features に画面が挙げたときだけ、hello_ack でも名乗る）。
//! 名乗った画面は `webtiles` の行から状態欄・メッセージ欄・ミニマップを自分で描く（`hd2d/dcss/`）。コアの側の挙動は変わらない。
constexpr const char *kFeatureUi = "dcss-ui";

// 原作の cio.h の特殊キー（負の数）
constexpr int kCkDelete = -255;
constexpr int kCkUp = -254;
constexpr int kCkDown = -253;
constexpr int kCkLeft = -252;
constexpr int kCkRight = -251;
#ifdef _WIN32
constexpr int kCkF1 = -368; // Windows 版の CK_F1（CK_F15 = -382 から数えて）
#else
constexpr int kCkF1 = -265; // Linux 版の CK_F1（CK_F15 = -279、ncurses の -KEY_F1 から数えて）
#endif

presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;
std::atomic<bool> g_ui_gone{ false };
std::atomic<int> g_quit_requests{ 0 }; //!< 画面から受けた `quit_request` の数（終わりの記録用）
std::atomic<bool> g_exit_sent{ false };
std::atomic<bool> g_webtiles_passthrough{ false }; //!< 画面が dcss-webtiles を挙げた
std::atomic<bool> g_ui_offered{ false }; //!< 画面が dcss-ui を挙げた（hello_ack でも名乗った）
std::string g_last_char_abbrev; //!< 最後に画面へ送った種族と職業の略称（ゲームのスレッドだけが触る）
std::uint64_t g_frame_id = 0;
std::string g_last_frame;
std::chrono::steady_clock::time_point g_last_emit{};

//! 新しい画面の窓（modern_ui）。原作が文字の画面で出す物を、他の文字のコア（Umoria など）と同じ部品で窓にする（M3 で DCSS 専用の窓へ置き換える）。
std::mutex g_modern_mutex;
presentation::NativeModernMenu g_native_menu;

std::mutex g_first_state_mutex;
std::condition_variable g_first_state_cv;
bool g_first_state_seen = false;
std::string g_first_state_lang;
std::string g_crawl_dir;

bool send_message(const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[dcss] send failed: %s\n", err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    return true;
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

// ---- 画面へのフレーム ----

std::string g_slab_dir; //!< 実行時に作る板（怪物・アイテム・プレイヤー）の置き場。画面へは hello_ack.asset_roots.slab で伝える

//! 新しい板ができていれば、目録（asset_manifest）を全部送り直す。**板のファイルは書き終えてある**（dcss_slab.h）。
//! 画面は目録を受けて初めて、その番号の板を読む（読めなければ -1 を覚えて二度と読み直さない）。目録を送ってからフレームを送る順を守り、
//! 画面の周回が目録を取り込む間（約 2 周回）だけ待ってから、その番号を使うフレームを送る。
void send_new_assets()
{
    std::vector<dcss_frame::AssetEntry> all;
    if (!dcss_frame::take_new_assets(all)) return;
    presentation::AssetManifestMessage manifest;
    manifest.root = portable::path_to_utf8(g_slab_dir); // パスは ANSI で持つ。JSON に載せるときだけ UTF-8 にする
    manifest.assets.reserve(all.size());
    for (const dcss_frame::AssetEntry &e : all) {
        presentation::AssetManifestWireEntry wire;
        wire.index = e.index;
        wire.kind = std::string(1, e.kind);
        wire.id = e.id;
        wire.path = e.name + ".png"; // 画面は拡張子とフォルダを捨てた名前で板を探す
        manifest.assets.push_back(std::move(wire));
    }
    if (send_message(presentation::encode_asset_manifest(manifest))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}

//! 画面の部品（dcss-ui）へ、地図の上の印（状態のアイコン・態度・強さの旗）を送る。変わったときだけ。フレームの直前に送る（同じ時点の状態）。
std::string g_last_marks;

void send_marks_if_changed()
{
    if (!g_ui_offered.load() || g_ui_gone.load()) return;
    const std::string json = dcss_frame::marks_json();
    if (json == g_last_marks) return;
    g_last_marks = json;
    // 何も無くなったときは空の一覧を送って、画面の印を消す。
    (void)send_message(json.empty() ? std::string("{\"t\":\"dcss_ui\",\"k\":\"marks\",\"m\":[]}") : json);
}

void emit_frame(bool force)
{
    if (g_ui_gone.load()) return;
    GameFrame frame;
    dcss_frame::build(frame);
    send_marks_if_changed();
    send_new_assets();
    // 画面が `dcss-ui` を挙げているときは、窓（メニュー・説明など）を画面の部品が原作の見た目で自分で描く。HD2D の新しい窓（変愚風）へは写さない。
    if (!g_ui_offered.load()) {
        std::lock_guard<std::mutex> lock(g_modern_mutex);
        g_native_menu.capture(frame, {}, {});
    }
    frame.frame_id = g_frame_id + 1;
    const std::string wire = presentation::encode_game_frame(frame);
    const std::string key = frame_compare_key(wire);
    if (!force && key == g_last_frame) return;
    g_last_frame = key;
    ++g_frame_id;
    (void)send_message(wire);
    g_last_emit = std::chrono::steady_clock::now();
}

//! 画面の部品（dcss-ui）へ、タブの欄の中身（持ち物・呪文・能力・怪物・技能・命令。ウェブタイル版の JSON には無い）を送る。変わったときだけ。ゲームのスレッドから。
std::string g_last_tabs;

void send_tabs_if_changed()
{
    if (!g_ui_offered.load() || g_ui_gone.load()) return;
    const std::string json = dcss_adapter_tabs_json();
    if (json.empty() || json == g_last_tabs) return;
    g_last_tabs = json;
    (void)send_message(json);
}

//! 画面の部品（dcss-ui）へ、ゲームパッドの割り当てられるコマンドの表（番号・名前・いまのキー）を送る。ゲームが始まってから 1 回だけ（rc の bindkey は始まる前に読まれる）。ゲームのスレッドから。
bool g_pad_table_sent = false;

void send_pad_table_once()
{
    if (g_pad_table_sent || !g_ui_offered.load() || g_ui_gone.load()) return;
    const std::string json = dcss_adapter_pad_json();
    if (json.empty()) return;
    g_pad_table_sent = true;
    (void)send_message(json);
}

void on_game_idle()
{
    emit_frame(false);
    send_tabs_if_changed();
    send_pad_table_once();
}

void on_game_poll()
{
    // 走り・休息などで原作が入力の有無だけを何度も見に来る間の、間引いた更新（80 ミリ秒に 1 回まで）。
    const auto now = std::chrono::steady_clock::now();
    if (now - g_last_emit < std::chrono::milliseconds(80)) return;
    emit_frame(false);
    send_tabs_if_changed();
}

// ---- 画面の部品（dcss-ui）の地図のマウス操作 ----

//! 足元のクリック・ツールチップの問い合わせ（`hd2d_*`）を、ゲームのスレッド（recvfrom の中）で処理する（libhd2d.cpp）。画面への返事は `dcss_ui` メッセージ。
std::string on_ui_line(const std::string &line, std::string &reply) { return dcss_adapter_handle_ui_line(line, reply); }
void on_ui_reply(const std::string &reply) { (void)send_message(reply); }

// ---- 原作の出力（JSON のかたまり）----

std::string g_sink_buffer;
FILE *g_trace = nullptr; //!< 環境変数 DCSS_TRACE_JSON のファイル（原作が出した JSON を 1 行ずつ残す。調べもの用）

// 画面の部品（dcss-ui）へ、種族と職業の略称（JSON には無い）を送る。名前の入った `player` を出したとき、変わっていれば 1 回。
void send_char_abbrev_if_changed(const std::string &line)
{
    if (line.compare(0, 15, "{\"msg\":\"player\"") != 0) return;
    // 最初の `player`（名前が空）の後、名前が入ってから（種族・職業が決まってから）だけ読む。
    const std::size_t at = line.find("\"name\":\"");
    if (at == std::string::npos || line.size() <= at + 8 || line[at + 8] == '"') return;
    const std::string abbrev = dcss_adapter_char_abbrev();
    if (abbrev.empty() || abbrev == g_last_char_abbrev) return;
    g_last_char_abbrev = abbrev;
    nlohmann::json message;
    message["t"] = "dcss_ui";
    message["k"] = "char";
    message["abbrev"] = abbrev;
    (void)send_message(message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

// 画面の部品（dcss-ui）へ、開始メニューの中身（モードの一覧・セーブデータと人形）を送る。文字画面（`menu` の `type:"crt"`）が開くたびに 1 回（ゲームが始まるまで）。
void send_startup_if_needed(const std::string &line)
{
    if (line.rfind("{\"msg\":\"menu\",\"type\":\"crt\"", 0) != 0) return;
    const std::string json = dcss_adapter_startup_json();
    if (!json.empty()) (void)send_message(json);
}

void on_sink(const char *data, size_t len)
{
    g_sink_buffer.append(data, len);
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = g_sink_buffer.find('\n', start);
        if (nl == std::string::npos) break;
        const std::string line = g_sink_buffer.substr(start, nl - start);
        if (g_webtiles_passthrough.load()) {
            nlohmann::json wrapper;
            wrapper["t"] = "webtiles";
            wrapper["line"] = line;
            (void)send_message(wrapper.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
        if (g_trace != nullptr) {
            std::fwrite(line.data(), 1, line.size(), g_trace);
            std::fputc('\n', g_trace);
            std::fflush(g_trace);
        }
        dcss_frame::ingest(line);
        if (g_ui_offered.load()) {
            send_char_abbrev_if_changed(line);
            send_startup_if_needed(line);
        }
        start = nl + 1;
    }
    g_sink_buffer.erase(0, start);
}

// ---- 画面の入力 → 原作の制御メッセージ ----

//! 画面側だけで変わった状態（窓のカーソルの移動など）を、ゲームのスレッドへ知らせて最新のフレームを組み直させる。
//! 原作は知らないメッセージを黙って飛ばし、入力待ちへ戻る（そこで idle のコールバックがフレームを出す）。
void request_refresh() { dcss_shim_feed_line("{\"msg\":\"hd2d_refresh\"}"); }

void feed_key(int keycode)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{\"msg\":\"key\",\"keycode\":%d}", keycode);
    dcss_shim_feed_line(buf);
    dcss_frame::note_text_key(keycode);
}

int vi_key(int dx, int dy)
{
    static const char table[3][3] = { { 'y', 'k', 'u' }, { 'h', '.', 'l' }, { 'b', 'j', 'n' } };
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) return 0;
    return table[dy + 1][dx + 1] == '.' ? 0 : table[dy + 1][dx + 1];
}

int arrow_key(int dx, int dy)
{
    if (dx == 0 && dy == -1) return kCkUp;
    if (dx == 0 && dy == 1) return kCkDown;
    if (dx == -1 && dy == 0) return kCkLeft;
    if (dx == 1 && dy == 0) return kCkRight;
    return 0;
}

void translate_event(const presentation::InputEventWire &ev, std::vector<int> &keys)
{
    const dcss_frame::InputScene scene = dcss_frame::input_scene();
    if (ev.e == "move") {
        const bool arrows = scene == dcss_frame::InputScene::Window || scene == dcss_frame::InputScene::Text;
        const int k = arrows ? arrow_key(ev.dx, ev.dy) : vi_key(ev.dx, ev.dy);
        if (k != 0) keys.push_back(k);
    } else if (ev.e == "cancel") {
        keys.push_back(27);
    } else if (ev.e == "confirm") {
        keys.push_back(13);
    } else if (ev.e == "answer") {
        keys.push_back(ev.value == "yes" ? 'y' : 'n');
    } else if (ev.e == "key") {
        if (!ev.name.empty()) {
            if (ev.name == "tab") keys.push_back(9);
            else if (ev.name == "backspace") keys.push_back(8);
            else if (ev.name == "delete") keys.push_back(kCkDelete);
        } else if (!ev.chr.empty()) {
            const unsigned char c = static_cast<unsigned char>(ev.chr[0]);
            keys.push_back(ev.ctrl ? (c & 0x1F) : c);
        }
    } else if (ev.e == "text") {
        for (const std::uint32_t cp : dcss_text::utf8_to_codepoints(ev.text)) keys.push_back(static_cast<int>(cp));
    } else if (ev.e == "fkey") {
        if (ev.n >= 1 && ev.n <= 12) keys.push_back(kCkF1 - (ev.n - 1));
    }
}

void on_ui_gone()
{
    g_ui_gone.store(true);
    dcss_shim_signal_eof(); // 原作が保存して終わる道に乗せる（標準入力の EOF と同じ扱い）
}

void receive_loop()
{
    while (!g_ui_gone.load()) {
        std::string payload;
        std::string err;
        if (presentation::read_message(g_in, payload, err) != presentation::TransportResult::Ok) {
            on_ui_gone();
            return;
        }
        const std::string type = presentation::peek_message_type(payload);
        if (type == "keys") {
            presentation::KeysMessage message;
            if (presentation::decode_keys(payload, message, err)) {
                for (const int k : message.keys) {
                    if (k > 0 && k <= 255) feed_key(k);
                }
            }
        } else if (type == "input_event") {
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) continue;
            std::vector<int> keys;
            bool refresh = false;
            for (const auto &ev : message.events) {
                if (ev.e == "ui_action") {
                    std::vector<int> translated;
                    std::lock_guard<std::mutex> lock(g_modern_mutex);
                    if (g_native_menu.input(ev, translated)) {
                        if (translated.empty()) refresh = true;
                        keys.insert(keys.end(), translated.begin(), translated.end());
                        continue;
                    }
                }
                translate_event(ev, keys);
            }
            for (const int k : keys) feed_key(k);
            if (refresh) request_refresh();
        } else if (type == "ui_state") {
            presentation::UiStateMessage state;
            if (presentation::decode_ui_state(payload, state, err)) {
                if (state.view_w > 0 && state.view_h > 0) dcss_frame::set_view_cells(state.view_w, state.view_h);
                if (!state.lang.empty()) dcss_frame::set_language(state.lang);
                {
                    std::lock_guard<std::mutex> lock(g_modern_mutex);
                    g_native_menu.set_enabled(state.modern_ui);
                    g_native_menu.set_language(state.lang.empty() ? "ja" : state.lang);
                }
                request_refresh();
                std::lock_guard<std::mutex> lock(g_first_state_mutex);
                if (!g_first_state_seen) {
                    g_first_state_seen = true;
                    g_first_state_lang = state.lang;
                    g_first_state_cv.notify_all();
                }
            }
        } else if (type == "webtiles_input") {
            // 画面が原作の制御メッセージをそのまま渡す（挙げたときだけ）。1 行の JSON の字。
            if (!g_webtiles_passthrough.load()) continue;
            const nlohmann::json m = nlohmann::json::parse(payload, nullptr, false);
            if (m.is_object() && m.contains("line") && m["line"].is_string()) dcss_shim_feed_line(m["line"].get<std::string>().c_str());
        } else if (type == "dcss_action") {
            // 画面のタブの欄（持ち物・呪文など）への操作。実行は原作の入力待ちの中（libhd2d.cpp の getch_ck）。控えを積んで入力待ちを起こす。
            if (!g_ui_offered.load()) continue;
            dcss_adapter_queue_tab_action(payload);
            char key_line[64];
            std::snprintf(key_line, sizeof(key_line), "{\"msg\":\"key\",\"keycode\":%d}", dcss_adapter_tab_action_key());
            dcss_shim_feed_line(key_line);
        } else if (type == "quit_request") {
            g_quit_requests.fetch_add(1);
            // 窓を閉じた・言語を替える。原作に保存して終わらせる（EOF と同じ道）。画面はまだ居るので、最後に exit を送る（v1 §9.1）。
            dcss_shim_signal_eof();
            return;
        }
    }
}

// ---- 終了 ----

std::string join(std::string dir, const char *name)
{
    if (!dir.empty() && dir.back() != portable::kPathSep) dir.push_back(portable::kPathSep);
    return dir + name;
}

void send_exit(int code)
{
    if (g_out == nullptr || g_ui_gone.load() || g_exit_sent.exchange(true)) return;
    presentation::ExitMessage message;
    message.code = code;
    (void)send_message(presentation::encode_exit(message));
}

void write_last_name()
{
    if (g_crawl_dir.empty()) return;
    const std::string name = dcss_frame::player_name();
    if (name.empty() || dcss_frame::game_ended()) return;
    std::ofstream out(join(g_crawl_dir, "hd2d_last_name.txt"), std::ios::binary | std::ios::trunc);
    out << name;
}

#ifdef _WIN32
//! 検査用: 環境変数 DCSS_EXIT_TRACE があるとき、終わるときの呼び出しの並び（`exit` を呼んだ側）を標準エラーへ出す（終了コード 0 で終わる道を突き止めるため）。記号は実行体の隣の .pdb から引く。
void print_exit_stack()
{
    const char *flag = std::getenv("DCSS_EXIT_TRACE");
    if (flag == nullptr || *flag == '\0') return;
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    void *frames[48];
    const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (USHORT i = 0; i < n; ++i) {
        SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(buffer);
        std::memset(sym, 0, sizeof(SYMBOL_INFO));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &displacement, sym)) {
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD line_disp = 0;
            if (SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(frames[i]), &line_disp, &line)) {
                std::fprintf(stderr, "[dcss] exit stack #%u %s (%s:%lu)\n", static_cast<unsigned>(i), sym->Name, line.FileName, static_cast<unsigned long>(line.LineNumber));
            } else {
                std::fprintf(stderr, "[dcss] exit stack #%u %s\n", static_cast<unsigned>(i), sym->Name);
            }
        } else {
            std::fprintf(stderr, "[dcss] exit stack #%u %p\n", static_cast<unsigned>(i), frames[i]);
        }
    }
    std::fflush(stderr);
}
#endif

void at_process_end()
{
    // 終わり方の記録（標準エラー。画面の記録に残る）。終了コード 0 で終わる道は複数ある（画面からの終了要求・画面が居なくなった・原作が「終わる道」〔seen_hups〕に入った・原作の通常の終わり）ので、
    // どれを通ったかを後から突き止められるようにする。seen_hups は、画面が居なくなったとき・コンソールが閉じたとき（Windows の CTRL_CLOSE_EVENT）にも立つ。
    std::fprintf(stderr, "[dcss] core exit: quit_requests=%d ui_gone=%d seen_hups=%d\n", g_quit_requests.load(), g_ui_gone.load() ? 1 : 0, dcss_adapter_seen_hups());
    std::fflush(stderr);
#ifdef _WIN32
    print_exit_stack();
#endif
    write_last_name();
    // 原作が失敗で終わるとき（`end(1, …)`。例: マップの定義の読み込みの誤り）は、終了コード 0 にせず、理由を添えて画面へ知らせる
    // （以前はここがいつも 0 で、起動の途中の失敗が「正常に終わった」ように見え、画面が黙って終わっていた。2026-10-23）。
    if (dcss_adapter_exit_failed() && g_out != nullptr && !g_ui_gone.load() && !g_exit_sent.load()) {
        presentation::FatalMessage fatal;
        fatal.reason = "DCSS が異常終了しました（原作が終了コード 1 で終わりました）。標準エラーの記録を確認してください。起動の途中なら、もう一度起動すると直ることがあります。";
        (void)send_message(presentation::encode_fatal(fatal));
        send_exit(1);
        return;
    }
    send_exit(0);
}

// ---- 原作のマップの定義（.des）の読み込みの前もっての実行 ----
//
// 原作は起動のたびに `read_maps()`（startup.cc）でマップの定義を読み、結果を `saves/<版>/des` に 1 ファイルずつ残す（次からは残したものを読む）。
// この読み込みの検査には乱数が入る（`map_def::has_exit`。`rng::seed()` は起動のたびに無作為）ので、`lair.des` の `regret_index_lairdec_grave_grove` のように
// 「出口が無い」と判じられる確率の小さい取りこぼしがあり、そのとき原作は `end(1)` で終わる。残したものが無い最初の起動（作業用フォルダ・新しく入れた直後）の約 0.1% で起こる
// （2026-10-23。画面が「終了コード 0」で終わる件の原因。原作のデータ・ソースは書き換えない）。
// そこで最初の 1 回だけ、読み込みだけを別プロセス（`-builddb`。読み込みを済ませて終わる）で、成功するまで繰り返してから本番を起こす。
// 読み込みは 1 ファイルずつ残るので、繰り返しは失敗した所から再開する。

#if !defined(HENGBAND_ANDROID)
std::string prepare_crawl_dir(std::string &why); // 下（始めの準備）で定義
constexpr int kWarmMaxTries = 8;

std::string warm_marker_path() { return join(g_crawl_dir, "hd2d_des_ready.txt"); }

bool warm_marker_ok()
{
    std::ifstream in(warm_marker_path(), std::ios::binary);
    std::string v((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return v == kCoreVersion;
}

//! 子（`--dcss-warm-cache`）を 1 回走らせて、終了コードを返す。起こせない・時間切れは -1。
int run_warm_child(bool english)
{
#ifdef _WIN32
    wchar_t exe[MAX_PATH * 2] = {};
    if (GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(sizeof(exe) / sizeof(exe[0]))) == 0) return -1;
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --dcss-warm-cache --lang=" + (english ? L"en" : L"ja");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    const DWORD waited = WaitForSingleObject(pi.hProcess, 300000);
    DWORD code = 0;
    int result = -1;
    if (waited == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code)) {
        result = static_cast<int>(code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
#else
    char exe[4096] = {};
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return -1;
    const pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        const char *lang = english ? "--lang=en" : "--lang=ja";
        ::execl(exe, exe, "--dcss-warm-cache", lang, static_cast<char *>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

//! 最初の 1 回だけ、読み込みを前もって済ませる。何回やっても失敗したら、そのまま本番へ進む（本番が失敗を画面へ知らせる）。
void warm_des_cache(const std::string &lang)
{
    if (warm_marker_ok()) return;
    for (int attempt = 1; attempt <= kWarmMaxTries; ++attempt) {
        const int rc = run_warm_child(lang == "en");
        std::fprintf(stderr, "[dcss] des cache warm-up %d/%d: exit code %d\n", attempt, kWarmMaxTries, rc);
        if (rc == 0) {
            std::ofstream out(warm_marker_path(), std::ios::binary | std::ios::trunc);
            out << kCoreVersion;
            return;
        }
    }
}

//! `--dcss-warm-cache`（子）。本番と同じデータの置き場・言語で、原作に読み込みだけをさせる（`-builddb`）。標準入出力は使わない。
int run_warm_cache(int argc, char **argv)
{
    std::string lang = "ja";
    for (int i = 1; i < argc; ++i) {
        const std::string_view a(argv[i]);
        if (a.rfind("--lang=", 0) == 0) lang = std::string(a.substr(7));
    }
    std::string why;
    g_crawl_dir = prepare_crawl_dir(why);
    if (g_crawl_dir.empty()) return 6;
    // 標準エラーは記録のファイルへ、標準出力は捨てる（子には端末が無い）。
    (void)std::freopen(join(g_crawl_dir, "des_warm.log").c_str(), "wb", stderr);
#ifdef _WIN32
    (void)std::freopen("NUL", "wb", stdout);
    _putenv_s("CRAWL_DIR", g_crawl_dir.c_str());
#else
    (void)std::freopen("/dev/null", "wb", stdout);
    ::setenv("CRAWL_DIR", g_crawl_dir.c_str(), 1);
#endif
    const std::string rc_path = join(g_crawl_dir, "init_warm.txt");
    {
        std::ofstream rc(rc_path, std::ios::binary | std::ios::trunc);
        rc << "language = " << (lang == "en" ? "en" : "ja") << "\n";
    }
    if (const char *fails = std::getenv("DCSS_WARM_TEST_FAILS"); fails != nullptr && *fails != 0) {
        // 検査用: 環境変数の回数だけ（子の起こし直しをまたいで数える）、読み込みに失敗したことにして終わる。繰り返しの確かめ（tools/dcss/dc_warm_check.py）に使う。
        const std::string counter = join(g_crawl_dir, "warm_test_count.txt");
        int done = 0;
        {
            std::ifstream in(counter, std::ios::binary);
            in >> done;
        }
        if (done < std::atoi(fails)) {
            std::ofstream out(counter, std::ios::binary | std::ios::trunc);
            out << (done + 1);
            std::fprintf(stderr, "[dcss] warm test: failing on purpose (%d)\n", done + 1);
            return 1;
        }
    }
    std::vector<std::string> args = { argv[0], "-rc", rc_path, "-builddb" };
    std::vector<char *> argv2;
    for (std::string &s : args) argv2.push_back(s.data());
    argv2.push_back(nullptr);
    dcss_shim_disable_stdin_reader();
    return dcss_original_main(static_cast<int>(args.size()), argv2.data());
}
#endif

// ---- 画面へ最初に送る表（命令の一覧）----

presentation::PadCommandsMessage build_pad_commands(bool ja)
{
    presentation::PadCommandsMessage commands;
    commands.current_keymap = "original";
    int id = 1;
    const auto add_seq = [&](std::vector<int> seq, const char *group_ja, const char *group_en, const char *ja_label, const char *en_label) {
        presentation::PadCommandWireEntry entry;
        entry.id = id++;
        entry.command = seq.front();
        // 画面は、命令の表に `i` `e` があると「持ち物・装備を新しい窓で開く」入力として横取りし、返事（NativeInventoryMenu）を待つ
        // （hd2d/app/input_controller.cpp）。DCSS の `i` は持ち物、`e` は食べる（装備ではない）うえ、持ち物の窓は M3。
        // M0 では原作の画面をそのまま出すので、この 2 つだけは命令の番号を字から外して、キーは原作へ素通しにする。
        if (entry.command == 'i' || entry.command == 'e') entry.command = 0x100 + entry.command;
        entry.group_utf8 = ja ? group_ja : group_en;
        entry.label_utf8 = ja ? ja_label : en_label;
        entry.seq_original = seq;
        entry.seq_rogue = seq;
        commands.entries.push_back(std::move(entry));
    };
    const auto add = [&](int key, const char *gj, const char *ge, const char *lj, const char *le) { add_seq({ key }, gj, ge, lj, le); };
    const int ctrl_q = 'Q' & 0x1F;
    const int ctrl_x = 'X' & 0x1F;
    const int ctrl_p = 'P' & 0x1F;
    add('o', "行動", "Action", "自動探索", "Auto-explore");
    add(9, "行動", "Action", "近くの敵へ自動で進む", "Auto-fight");
    add('s', "行動", "Action", "1 ターン待つ", "Wait one turn");
    add('5', "行動", "Action", "休息", "Rest");
    add('<', "行動", "Action", "上り階段", "Go up stairs");
    add('>', "行動", "Action", "下り階段", "Go down stairs");
    add('g', "行動", "Action", "拾う", "Pick up");
    add('O', "行動", "Action", "扉を開ける", "Open a door");
    add('C', "行動", "Action", "扉を閉める", "Close a door");
    add('x', "行動", "Action", "調べる", "Examine");
    add('X', "行動", "Action", "階の全体地図", "Level map");
    add('_', "行動", "Action", "移動先を選ぶ", "Travel");
    add('f', "戦闘", "Combat", "撃つ", "Fire");
    add('F', "戦闘", "Combat", "撃つ（次の的）", "Fire (next target)");
    add('z', "魔法", "Magic", "呪文を唱える", "Cast a spell");
    add('a', "魔法", "Magic", "能力を使う", "Use an ability");
    add('V', "魔法", "Magic", "道具を発動する", "Evoke an item");
    add('i', "持ち物", "Items", "持ち物", "Inventory");
    add('e', "持ち物", "Items", "食べる", "Eat");
    add('q', "持ち物", "Items", "飲む", "Quaff");
    add('r', "持ち物", "Items", "読む", "Read");
    add('w', "持ち物", "Items", "武器を構える", "Wield a weapon");
    add('W', "持ち物", "Items", "防具を着る", "Wear armour");
    add('T', "持ち物", "Items", "防具を脱ぐ", "Take off armour");
    add('P', "持ち物", "Items", "装身具をつける", "Put on jewellery");
    add('R', "持ち物", "Items", "装身具を外す", "Remove jewellery");
    add('d', "持ち物", "Items", "置く", "Drop");
    add('@', "情報", "Information", "キャラクターの情報", "Character info");
    add(ctrl_p, "情報", "Information", "メッセージの履歴", "Message history");
    add('?', "情報", "Information", "ヘルプ", "Help");
    add('S', "システム", "System", "保存して終わる", "Save and quit");
    add(ctrl_x, "システム", "System", "保存して終わる（別の入力）", "Save and quit (alt)");
    add(ctrl_q, "システム", "System", "あきらめる", "Quit (abandon)");
    return commands;
}

// ---- 始めの準備 ----

//! 原作の CRAWL_DIR（<データの根>/dcss/lib）。作る前でも道だけは返す。
std::string crawl_dir_path() { return join(join(portable::data_dir(), "dcss"), "lib"); }

//! 読むだけのデータ（原作の dat・docs・settings）と書き込む場所（saves・morgue・db・des・slabs）を同じ置き場にする。
//! 置き場は `<データの根>/dcss/lib`（`HENGBAND_DATA_ROOT`、無ければ実行ファイルの隣）。データの根が実行ファイルの隣でないときは、
//! 隣の `dcss/lib` からデータ（と、あればタイルの画像ページ tiles）を写す（配布の箱の形）。見つからなければ空を返す。
std::string prepare_crawl_dir(std::string &why)
{
    namespace fs = std::filesystem;
    const std::string root = portable::data_dir();
    const std::string home = join(root, "dcss");
    const std::string crawl_dir = crawl_dir_path();
    if (!portable::make_dir(home) || !portable::make_dir(crawl_dir)) {
        why = "cannot create " + crawl_dir;
        return {};
    }
    const std::string src = join(join(portable::exe_dir(), "dcss"), "lib");
    std::error_code ec;
    for (const char *sub : { "dat", "docs", "settings" }) {
        const std::string dst = join(crawl_dir, sub);
        if (portable::dir_exists(dst)) continue;
        const std::string from = join(src, sub);
        if (!portable::dir_exists(from)) {
            why = std::string("DCSS data is missing: ") + from + " (run tools/dcss/prepare_runtime_data.py)";
            return {};
        }
        fs::copy(from, dst, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        if (ec) {
            why = std::string("cannot copy ") + from + " -> " + dst + ": " + ec.message();
            return {};
        }
    }
    // タイルの画像ページ（tools/dcss/dcss_tile_atlas.py → prepare_runtime_data.py が dcss/lib/tiles に置く）。無くても遊べる（怪物・アイテムは字で出る）ので必須にしない。
    {
        const std::string dst = join(crawl_dir, "tiles");
        const std::string from = join(src, "tiles");
        if (!portable::dir_exists(dst) && portable::dir_exists(from)) {
            fs::copy(from, dst, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
            if (ec) std::fprintf(stderr, "[dcss] cannot copy %s -> %s: %s\n", from.c_str(), dst.c_str(), ec.message().c_str());
        }
    }
    (void)portable::make_dir(join(crawl_dir, "saves"));
    (void)portable::make_dir(join(crawl_dir, "morgue"));
    (void)portable::make_dir(join(crawl_dir, "slabs"));
    return crawl_dir;
}

bool lang_is_english(const std::string &lang) { return lang == "en"; }

//! 握手から原作の main までの本体。輸送路（g_in・g_out）は呼んだ側が用意しておく
//! （Windows・Linux は標準入出力 = run_protocol、Android・Quest は画面と同じプロセスのパイプ = hengband_core_entry）。
int run_protocol_common(const char *argv0, const std::string &lang_arg, bool resume);

int run_protocol(int argc, char **argv)
{
    std::string lang_arg;
    bool resume = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a(argv[i]);
        if (a.rfind("--lang=", 0) == 0) lang_arg = std::string(a.substr(7));
        else if (a == "--resume") resume = true;
    }

    presentation::set_stdio_binary_mode();
    g_in = presentation::transport_stdin();
    g_out = presentation::transport_stdout();
    if (g_in == nullptr || g_out == nullptr) return 3;
#ifdef _WIN32
    {
        // 標準出力のハンドルは CRT の fd 1 が持っている。下で fd 1 を付け替えると閉じられるので、複製をプロトコル用に取る。
        HANDLE dup = nullptr;
        if (!::DuplicateHandle(::GetCurrentProcess(), static_cast<HANDLE>(g_out), ::GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) return 3;
        g_out = static_cast<presentation::TransportHandle>(dup);
    }
    // 原作が printf で書くものがプロトコルに混ざらないよう、CRT の標準出力は標準エラーへ向ける（プロトコルは上で取った Win32 のハンドルで書く）。
    (void)std::fflush(stdout);
    _dup2(_fileno(stderr), _fileno(stdout));
    ::SetStdHandle(STD_OUTPUT_HANDLE, ::GetStdHandle(STD_ERROR_HANDLE));
#else
    // 相手（画面）が閉じたパイプへの書き込みでプロセスごと落ちないよう、EPIPE で返させる（protocol_transport.cpp の注意）。
    std::signal(SIGPIPE, SIG_IGN);
    {
        // fd 1 を付け替える前に、プロトコル用の複製（fd）を取る。
        (void)std::fflush(stdout);
        const int proto_fd = ::dup(1);
        if (proto_fd < 0) return 3;
        g_out = presentation::transport_from_fd(proto_fd);
    }
    // 原作が printf で書くものがプロトコルに混ざらないよう、標準出力（fd 1）は標準エラーへ向ける。
    if (::dup2(2, 1) < 0) return 3;
#endif
    return run_protocol_common(argv[0], lang_arg, resume);
}

int run_protocol_common(const char *argv0, const std::string &lang_arg, bool resume)
{
    std::string payload, err;
    if (presentation::read_message(g_in, payload, err) != presentation::TransportResult::Ok) return 4;
    if (presentation::peek_message_type(payload) != "hello") return 4;
    presentation::HelloMessage hello;
    if (!presentation::decode_hello(payload, hello, err) || hello.protocol != presentation::kProtocolVersion) return 4;

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = kCoreName;
    ack.core_version = kCoreVersion;
    ack.features = { "lang-restart", kFeatureWebtiles };
    g_slab_dir = join(crawl_dir_path(), "slabs");
    ack.asset_root_slab = portable::path_to_utf8(g_slab_dir); // 画面は板をここから読む（実行時に作る。dcss_slab.h）
    g_webtiles_passthrough.store(std::find(hello.features.begin(), hello.features.end(), std::string(kFeatureWebtiles)) != hello.features.end());
    if (g_webtiles_passthrough.load() && std::find(hello.features.begin(), hello.features.end(), std::string(kFeatureUi)) != hello.features.end()) {
        ack.features.push_back(kFeatureUi);
        g_ui_offered.store(true);
        dcss_frame::set_ui_offered(true);
    }
    if (!send_message(presentation::encode_hello_ack(ack))) return 5;
    const bool ja_early = lang_arg.empty() ? true : !lang_is_english(lang_arg);
    if (!send_message(presentation::encode_pad_commands(build_pad_commands(ja_early)))) return 5;
    if (!send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(!ja_early)))) return 5;

    std::thread(receive_loop).detach();

    // 言語: --lang= があればそれが勝つ。無ければ最初の ui_state（画面の言語）を待つ（3 秒まで。来なければ日本語）。
    std::string lang = lang_arg;
    if (lang.empty()) {
        std::unique_lock<std::mutex> lock(g_first_state_mutex);
        g_first_state_cv.wait_for(lock, std::chrono::seconds(3), [] { return g_first_state_seen; });
        lang = g_first_state_lang;
    }
    if (lang.empty()) lang = "ja";
    dcss_frame::set_language(lang);
    if (lang_is_english(lang) == !ja_early) {
        // 握手の直後に送った表の言語と同じ。
    } else {
        (void)send_message(presentation::encode_pad_commands(build_pad_commands(!lang_is_english(lang))));
        (void)send_message(presentation::encode_sub_panel_kinds(presentation::rogue_sub_panel_kinds(lang_is_english(lang))));
    }

    std::string why;
    g_crawl_dir = prepare_crawl_dir(why);
    if (g_crawl_dir.empty()) {
        std::fprintf(stderr, "[dcss] %s\n", why.c_str());
        presentation::FatalMessage fatal;
        fatal.reason = why;
        (void)send_message(presentation::encode_fatal(fatal));
        return 6;
    }
    // 怪物・アイテム・プレイヤーの板の材料。読めなければ字で出る（遊べる）。
    (void)dcss_frame::init_slabs(join(g_crawl_dir, "tiles"), g_slab_dir);
    // CRAWL_DIR（原作は -dir の代わりにこちらを読む。-dir は起動のメッセージに残る）
#ifdef _WIN32
    _putenv_s("CRAWL_DIR", g_crawl_dir.c_str());
#else
    ::setenv("CRAWL_DIR", g_crawl_dir.c_str(), 1);
#endif
    const std::string rc_path = join(g_crawl_dir, "init.txt");
    {
        std::ofstream rc(rc_path, std::ios::binary | std::ios::trunc);
        rc << "language = " << (lang_is_english(lang) ? "en" : "ja") << "\n";
        // 原作は、ウェブ版（ブラウザが画面を描く形）では途中の絵を省く（reduce_animations の既定が真）。手元のタイル版と同じに、ビームの弾・矢・爆発を
        // 1 マス・1 輪ずつ途中の絵を出して待つ（演出。段 4）。原作の rc の項目で、ルールには効かない。
        rc << "reduce_animations = false\n";
    }

#if !defined(HENGBAND_ANDROID)
    warm_des_cache(lang); // 最初の 1 回だけ、マップの定義の読み込みを別プロセスで前もって済ませる（乱数による取りこぼしを繰り返しで吸収する）
#endif
    std::vector<std::string> args = { argv0, "-rc", rc_path, "-webtiles-socket", "dcss-hd2d", "-await-connection" };
    if (resume) {
        std::ifstream in(join(g_crawl_dir, "hd2d_last_name.txt"), std::ios::binary);
        std::string name((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!name.empty()) {
            args.push_back("-name");
            args.push_back(name);
        }
    }
    // 検査用: 追加の引数（例 `-seed 1`）。空白で区切る。
    const char *extra = std::getenv("DCSS_EXTRA_ARGS");
#if defined(HENGBAND_ANDROID)
    // 環境変数を渡せない Android の検査用: 外部ストレージのアプリの場所（adb で読み書きできる）に `dcss_extra_args.txt` があれば、その 1 行目を `DCSS_EXTRA_ARGS` の代わりに使う
    // （例 `-wizard -seed 5 -extra-opt-first pickup_menu_limit=0`）。無ければ従来どおり。
    std::string extra_from_file;
    {
        std::ifstream f("/storage/emulated/0/Android/data/org.hengband.hd2d/files/dcss_extra_args.txt", std::ios::binary);
        if (f) {
            std::getline(f, extra_from_file);
            while (!extra_from_file.empty() && (extra_from_file.back() == '\r' || extra_from_file.back() == '\n')) extra_from_file.pop_back();
            if (!extra_from_file.empty()) extra = extra_from_file.c_str();
        }
    }
#endif
    if (extra != nullptr && *extra != '\0') {
        std::string cur;
        for (const char *p = extra;; ++p) {
            if (*p == ' ' || *p == '\0') {
                if (!cur.empty()) args.push_back(cur);
                cur.clear();
                if (*p == '\0') break;
            } else {
                cur.push_back(*p);
            }
        }
    }
    std::vector<char *> argv2;
    for (std::string &s : args) argv2.push_back(s.data());
    argv2.push_back(nullptr);

    if (const char *trace = std::getenv("DCSS_TRACE_JSON"); trace != nullptr && *trace != '\0') g_trace = std::fopen(trace, "wb");
#if defined(HENGBAND_ANDROID)
    // 環境変数を渡せない Android の検査用: 外部ストレージのアプリの場所（adb で読み書きできる）に `dcss_trace.flag` があれば、同じ場所の `dcss_trace.jsonl` へ残す。
    if (g_trace == nullptr && std::filesystem::exists("/storage/emulated/0/Android/data/org.hengband.hd2d/files/dcss_trace.flag"))
        g_trace = std::fopen("/storage/emulated/0/Android/data/org.hengband.hd2d/files/dcss_trace.jsonl", "wb");
#endif
    dcss_shim_disable_stdin_reader();
    dcss_shim_set_sink(on_sink);
    dcss_shim_set_idle_callbacks(on_game_idle, on_game_poll);
    if (g_ui_offered.load()) dcss_shim_set_ui_line_hook(on_ui_line, on_ui_reply);
    std::atexit(at_process_end);
    if (const char *flag = std::getenv("DCSS_TEST_EXIT_FAIL"); flag != nullptr && *flag != 0) {
        // 検査用: 原作が失敗（end(1)）で終わった場合と同じ印を立てて終わる。終了コードを正しく画面へ送る道の確かめ（tools/dcss/dc_warm_check.py）。
        dcss_adapter_test_mark_failed();
        std::exit(1);
    }

    const int rc = dcss_original_main(static_cast<int>(args.size()), argv2.data());
    write_last_name();
    send_exit(rc);
    return rc;
}

} // namespace

#if !defined(_WIN32)
/*!
 * Android・Quest の入口（platform/android/hd2d_entry_android.cpp が dlsym で引く。Frox・Umoria・Angband と同じ形）。
 * 画面（libmain.so）と同じプロセスの別スレッドで呼ばれ、輸送路は画面が作ったパイプの両端。標準入出力には触れない。
 * 見る引数は `--lang=ja|en` と `--resume`（言語を替えて起こし直すとき）。
 * 原作は終わるとき exit を呼ぶ（end.cc）。1 プロセスに同居するのでアプリごと終わる（
 * 保存はその前に済む）。`std::atexit(at_process_end)` が画面へ exit を送る。
 */
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path, const std::vector<std::string> &forwarded_args)
{
    (void)protocol_log_path;
    std::string lang_arg;
    bool resume = false;
    for (const std::string &a : forwarded_args) {
        if (a.rfind("--lang=", 0) == 0) lang_arg = a.substr(7);
        else if (a == "--resume") resume = true;
    }
    g_in = in;
    g_out = out;
    if (g_in == nullptr || g_out == nullptr) return 3;
    // 呼ばれるスレッド（画面が起こす std::thread）のスタックは小さい（bionic の既定は 1 MB）。原作は階の生成（ヴォールトの再帰）や Lua で
    // 深く入るので、Linux の主スレッド（8 MB）より小さい余裕では心配が残る。大きいスタックの専用スレッドで走らせて、終わりを待つ。
    struct Job {
        std::string lang;
        bool resume;
        int rc;
    } job{ lang_arg, resume, 0 };
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, static_cast<size_t>(16) * 1024 * 1024);
    pthread_t thread;
    const int created = pthread_create(
        &thread, &attr,
        [](void *p) -> void * {
            Job *j = static_cast<Job *>(p);
            j->rc = run_protocol_common("dcss", j->lang, j->resume);
            return nullptr;
        },
        &job);
    pthread_attr_destroy(&attr);
    if (created != 0) return run_protocol_common("dcss", lang_arg, resume); // 作れなければ呼ばれたスレッドで走らせる
    pthread_join(thread, nullptr);
    return job.rc;
}
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && std::string_view(argv[1]).rfind("--dcss-slab-", 0) == 0) return dcss_frame::slab_tool(argc, argv); // 板の作り方の検査（ゲームは起こさない）
#if !defined(HENGBAND_ANDROID)
    if (argc > 1 && std::string_view(argv[1]) == "--dcss-warm-cache") return run_warm_cache(argc, argv); // 読み込みだけの子（warm_des_cache が起こす）
#endif
    bool protocol = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--ui-protocol=stdio") protocol = true;
    }
    if (!protocol) return dcss_original_main(argc, argv); // 従来の仮の入出力（dcss/adapter/m0_check.py）
    return run_protocol(argc, argv);
}
