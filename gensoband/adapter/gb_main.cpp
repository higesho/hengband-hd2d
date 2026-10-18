#if defined(HENGBAND_DESKTOP_LINUX)
#include <csignal>
#endif
#include "bridge/native_store_menu.h"
#include "bridge/native_item_display_impl.h"
#include "bridge/native_inventory_menu.h"
extern "C" void gb_native_describe_item(const void *,native_store_item *);
extern "C" int gb_native_owned_item(int,native_store_item *);
static presentation::NativeInventoryMenu g_inventory_menu;
extern "C" int gb_native_store_info(native_store_info *);
extern "C" int gb_native_store_item(int,native_store_item *);
#include "bridge/native_modern.h"
#include "bridge/native_save_picker.h"
#include <variant>
/*!
 * @file gb_main.cpp
 * @brief `GensobandCore.exe` の入口（プロトコル v1 の core 側）。**P2 本実装**。
 *
 * / §8（P2 の受け入れ）と、
 * プロトコルの プロトコル v1 の仕様
 * （§3 握手 / §4 カタログ / §5 UiSeam 対応 / §6.4 合体規則 / §8 pad_commands）。
 *
 * ## この TU の立ち位置
 * `platform/windows/core_main.cpp`（変愚コアの入口）の**弟**である。
 * 受信スレッド 1 本・送信 mutex・stdout はプロトコル専用・stderr がログ、という
 * 骨格をそのまま写した。違うのは中身の出どころだけ:
 *
 * | | 変愚 | 幻想蛮怒 |
 * |---|---|---|
 * | 画づくり | `presentation::Bridge::capture` | `gb::capture_frame`（`gb_shim.h` 越し） |
 * | Term 待ち | `SdlNullTerm::on_event` | `gb_null_term.c` の `TERM_XTRA_EVENT` |
 * | ゲームを回す | `presentation::run_sdl_game` | `gb_run_game()` |
 *
 * ## 幻想蛮怒のヘッダは見ない
 * ここは C++ の TU なので、触ってよいのは `gb_shim.h`（純 C ABI）だけ（設計 §3）。
 * `angband.h` を include してはいけない。
 *
 * ## stdout / stderr
 * stdout は**プロトコル専用**。人間向けの出力は全部 stderr（v1 §1.1）。
 *
 * ## 文字コード
 * このファイルは UTF-8（BOM 付き）。execution-charset は ACP のままなので、
 * **プロトコルへ出す文字列リテラルは ASCII に限る**（設計 §3.1）。
 * 日本語は `gb::sjis_to_utf8()` を通したものだけが線に乗る。
 *
 * ## サブシステム
 * CONSOLE。窓は持たない（`HengbandCore.exe` と同じ。パイプで受けて待たせる運用）。
 */

#include "gb_shim.h"

#include "gb_frame.h"
#include "gb_lang.h"
#include "gb_lang_c.h"
#include "gb_manifest.h"
#include "gb_menu.h"
#include "gb_pad_commands.h"
#include "gb_sub_panels.h"
#include "gb_text.h"

#include "bridge/input_event_adapter.h"
#include "frame/frame_codec.h"
#include "bridge/native_targeting_runtime.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"

#include "portable/legacy_os.h"
#include "bridge/slab_presence.h" //!< 目録の記録は PNG ではなく板（.vox）の有無を数える

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

/*! 主 Term の大きさ。作成画面の作品一覧の末尾（行27）を含める。 */
constexpr int kTermCols = 80;
// birth.c draws entry p at zero-based row 27; keep that choice visible.
constexpr int kTermRows = 28;

//! v1 §1.1 の起動引数。これが無い core は「単体で動く従来版」を名乗る立場に無い。
constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
//! v1 §11.2 の会話ログ。既定 OFF。
constexpr std::string_view kProtocolLogArg = "--protocol-log=";
//! セーブ枠の指定（設計 §3.2 版 1.1）。無ければ既定 `player`（か覚えている枠）。
constexpr std::string_view kSavefileArg = "--savefile=";
//! 既定のセーブ枠（設計 §3.2）。
constexpr const char *kDefaultSlot = "player";
//! 前回遊んだ枠の覚え書き。**アダプタの持ち物**（コアのセーブ形式には触らない）。
constexpr const char *kLastSlotFile = "last_slot.txt";
//! 言語の指定。無ければ最初の `ui_state.lang` を待つ。
constexpr std::string_view kLangArg = "--lang=";
//! 引けなかった鍵の書き出し先（同 §6）。
constexpr std::string_view kReportUntranslatedArg = "--report-untranslated=";
//! 言語切り替えの立て直し（Sil-Q A2 の写し）。画面側が**起こし直しの回だけ**付ける。
constexpr std::string_view kResumeArg = "--resume";
//! 立て直しで開き直す枠の覚え書き。**`.` で始まる**ので枠一覧（`list_save_slots`）には出ない。
constexpr const char *kResumeFile = ".resume";

/* ============================================================ 会話ログ（v1 §11.2） */

/*!
 * @brief 全送受信メッセージを JSON Lines で残す。`core_main.cpp:125` の写し。
 * @details 1 行 = `{"time","dir","t","len","payload"}`。**`frame` も全文残す**。
 */
class ProtocolLog {
public:
    void open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb");
        if (this->fp_ == nullptr) {
            std::fprintf(stderr, "[gensoband] could not open the protocol log: %s\n", path.c_str());
        }
    }

    void record(const char *dir, const std::string &type, const std::string &payload)
    {
        if (this->fp_ == nullptr) {
            return;
        }
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->fp_ == nullptr) {
            return;
        }
        std::string line = "{\"time\":\"";
        line += iso_now();
        line += "\",\"dir\":\"";
        line += dir;
        line += "\",\"t\":\"";
        append_escaped(line, type);
        line += "\",\"len\":";
        line += std::to_string(payload.size());
        line += ",\"payload\":\"";
        append_escaped(line, payload);
        line += "\"}\n";
        std::fwrite(line.data(), 1, line.size(), this->fp_);
        std::fflush(this->fp_);
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->fp_ != nullptr) {
            std::fclose(this->fp_);
            this->fp_ = nullptr;
        }
    }

private:
    static std::string iso_now()
    {
        return portable::iso_utc_now();
    }

    static void append_escaped(std::string &out, const std::string &text)
    {
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(byte));
                    out += buf;
                } else {
                    out += c;
                }
                break;
            }
        }
    }

    std::mutex mutex_;
    std::FILE *fp_{ nullptr };
};

ProtocolLog g_log;

/* ================================================================ 送受信の土台 */

presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;

//! ui が消えた（stdin EOF / 読み書きエラー）。v1 §9.2。
std::atomic<bool> g_ui_gone{ false };
//! `quit_request` を受け取った。v1 §4.1。
std::atomic<bool> g_quit_requested{ false };
//! `exit` を送ったか（二重送信よけ）。
std::atomic<bool> g_exit_sent{ false };

//! 受信キュー。**中身はコア内部コード（SJIS）に直したあとのバイト**である。
std::mutex g_inbox_mutex;
presentation::NativeModernMenu g_native_menu;
presentation::NativeStoreMenu g_store_menu;
extern "C" int gb_native_test_open(int);
extern "C" int gb_name_en_selftest(void);
bool g_native_test_started=false;
std::deque<std::variant<int,presentation::InputEventWire>> g_key_queue;
gb::GbKeyDecoder g_key_decoder;

//! 直近の `ui_state`。視界の大きさとカメラの追従に効く（v1 §7）。
presentation::UiStateMessage g_ui_state;
bool g_ui_state_present = false;
//! まだゲームスレッドへ渡していない `ui_state` がある。
bool g_ui_state_dirty = false;
//! 画面が望む Term の高さ（行数）。0 は未指定。`apply_wanted_term_rows()` が安全な間にだけ反映する。
int g_wanted_term_rows = 0;

/*!
 * @name 最初の `ui_state`（言語の確定。設計 §4.1）
 * @details 画面は握手の直後に必ず 1 回送る（Sil-Q P0 の実測 15 ms）。
 * `lang` はその 1 回目のものを使う——bootstrap の前に確定していなければならない。
 * @{
 */
std::atomic<bool> g_first_ui_state_seen{ false };
//! 最初の `ui_state` が申告した言語。`g_inbox_mutex` の下。空なら申告なし＝日本語。
std::string g_first_ui_state_lang;
/*! @} */

bool send_message(const std::string &type, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    if (g_out == nullptr) {
        return false;
    }
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[gensoband] send \"%s\" failed: %s\n", type.c_str(), err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    g_log.record("out", type, payload);
    return true;
}

//! `fatal` を送って（送れる状態なら）理由を stderr にも残す。v1 §9.3。
void send_fatal(const std::string &reason)
{
    std::fprintf(stderr, "[gensoband] fatal: %s\n", reason.c_str());
    presentation::FatalMessage fatal;
    fatal.reason = reason;
    (void)send_message("fatal", presentation::encode_fatal(fatal));
}

void send_exit_once(int code)
{
    if (g_exit_sent.exchange(true)) {
        return;
    }
    presentation::ExitMessage bye;
    bye.code = code;
    (void)send_message("exit", presentation::encode_exit(bye));
}

/*!
 * @brief 受け取ったキーを内部コードへ直してキューへ積む。
 * @details 変換は**ここ 1 か所**（設計 §3.1・V5）。線の上は UTF-8、コアは CP932。
 * 変愚が `sdl_null_term.cpp` の `convert_text_input_to_system_encoding` で
 * やっているのと同じ位置づけである。
 */
void enqueue_keys(const std::vector<int> &raw)
{
    std::vector<int> converted;
    converted.reserve(raw.size());

    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    g_key_decoder.feed(raw, converted);
    for (const int key : converted) {
        g_key_queue.push_back(key);
    }
}

/* ================================================================== 受信スレッド */

void receive_loop()
{
    for (;;) {
        std::string payload;
        std::string err;
        const auto result = presentation::read_message(g_in, payload, err);
        if (result != presentation::TransportResult::Ok) {
            if (result != presentation::TransportResult::Eof) {
                std::fprintf(stderr, "[gensoband] receive stopped: %s%s%s\n",
                    presentation::transport_result_name(result), err.empty() ? "" : " - ", err.c_str());
            } else {
                std::fprintf(stderr, "[gensoband] stdin closed (the ui is gone)\n");
            }
            g_ui_gone.store(true);
            return;
        }

        const std::string type = presentation::peek_message_type(payload);
        g_log.record("in", type, payload);

        if (type == "keys") {
            presentation::KeysMessage message;
            if (!presentation::decode_keys(payload, message, err)) {
                std::fprintf(stderr, "[gensoband] bad \"keys\": %s\n", err.c_str());
                continue;
            }
            enqueue_keys(message.keys);
            continue;
        }
        if (type == "input_event") {
            /*
             * v1 §8.3。**画面側（hd2d）が実際に送ってくるのはこちら**である
             * （`hd2d/app/hd2d_app.cpp:9750`。`keys` の送り手は試験の駆動器だけ）。
             * 展開器は変愚と**同じ TU をそのまま**使う（`input_event_adapter.cpp` は
             * `frame/protocol_messages.h` しか見ないので、コアに依らない）。
             */
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) {
                std::fprintf(stderr, "[gensoband] bad \"input_event\": %s\n", err.c_str());
                continue;
            }
            for (const auto &event : message.events) {
                if (event.e == "ui_action") {
                    std::lock_guard<std::mutex> lock(g_inbox_mutex);
                    g_key_queue.push_back(event);
                } else {
                    presentation::InputEventsMessage one; one.events.push_back(event);
                    std::vector<int> keys; presentation::append_input_event_keys(one, keys);
                    enqueue_keys(keys);
                }
            }
            continue;
        }
        if (type == "ui_state") {
            presentation::UiStateMessage message;
            if (!presentation::decode_ui_state(payload, message, err)) {
                std::fprintf(stderr, "[gensoband] bad \"ui_state\": %s\n", err.c_str());
                continue;
            }
            /*
             * push-latest（v1 §7）。**適用はゲームスレッド**（`host_present`）で行う。
             * ここで `gb::set_view_size()` を直に呼ぶと、地図を切り出している最中に
             * 窓の大きさが変わりうる（受け皿と走査量が食い違う）。
             */
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            if (!g_first_ui_state_seen.load()) {
                g_first_ui_state_lang = message.lang;
            }
            g_ui_state = std::move(message);
            g_ui_state_present = true;
            g_ui_state_dirty = true;
            g_first_ui_state_seen.store(true);
            continue;
        }
        if (type == "quit_request") {
            g_quit_requested.store(true);
            std::fprintf(stderr, "[gensoband] quit_request received\n");
            continue;
        }
        // 未知の種別は黙って捨てる（v1 §2.3）。
    }
}

/* ================================================================ 合体規則（§6.4） */

/*!
 * @brief 合体判定に使う鍵（＝`frame_id` を抜いたペイロード）。`core_main.cpp:817` の写し。
 * @details `frame_id` は capture ごとに必ず増えるので、素のバイト比較では
 * 永久に一致しない。数字だけ抜いて比べる。
 */
std::string frame_compare_key(const std::string &payload)
{
    static const std::string marker = "\"frame_id\":";
    const auto pos = payload.find(marker);
    if (pos == std::string::npos) {
        return payload;
    }
    auto end = pos + marker.size();
    while ((end < payload.size()) && (((payload[end] >= '0') && (payload[end] <= '9')) || (payload[end] == '-'))) {
        ++end;
    }
    std::string out = payload.substr(0, pos + marker.size());
    out.append(payload, end, std::string::npos);
    return out;
}

std::string g_last_frame_key;
bool g_has_last_frame = false;
unsigned long long g_frame_id = 0;

//! 表を送ったか／送ったときのキー配列（切替を見張って送り直す。v1 §8）。
bool g_tables_sent = false;
int g_last_rogue_like = -1;
/*!
 * @brief 表を送ったときの `gb_pad_table_stamp()`。
 * @details 名札は職と広域マップで変わる（`special_menu_info`）。配列だけを
 * 見張っていると、**誕生で職が決まった瞬間の差し替えを取りこぼす**
 * （超能力者なら「魔法/特殊能力」が「超能力/特殊能力」になる）。
 */
int g_last_pad_stamp = -1;

//! タイル目録（設計 §5.1）。起動時に 1 回読んで、以後 `gb_frame` が索引に使う。
gb::TileManifest g_manifest;

/*!
 * @brief 一覧のアイコン用 tile_index。`kind_id` が有ればその種類の板、無ければ
 * `unaware_category`（正体の分からないアイテム）の代表の板。
 */
uint16_t gb_tile_for_row(int kind_id, int unaware_category)
{
    if (kind_id) {
        return g_manifest.lookup('K', kind_id);
    }
    if (unaware_category) {
        return g_manifest.lookup_unaware_representative(static_cast<presentation::UnawareItemCategory>(unaware_category));
    }
    return 0;
}

void send_pad_commands()
{
    (void)send_message("pad_commands", presentation::encode_pad_commands(gb::build_pad_commands()));
    g_last_rogue_like = gb_rogue_like_commands();
    g_last_pad_stamp = gb_pad_table_stamp();
}

/*!
 * @brief 初期化完了時の表（v1 §8 / §8.1）。
 * @details `sub_panel_kinds` は**コアの `window_flag_desc` から組む**（M1 / S1。
 * M0 では空で送っていたので、画面の機能メニューに選択肢が 1 つも出なかった）。
 * `asset_manifest` は mapping.csv から組んだ目録そのもの。
 */
void send_startup_tables()
{
    if (g_tables_sent) {
        return;
    }
    g_tables_sent = true;
    send_pad_commands();
    (void)send_message(
        "sub_panel_kinds", presentation::encode_sub_panel_kinds(gb::build_sub_panel_kinds()));
    if (!g_manifest.message().assets.empty()) {
        auto manifest = g_manifest.message();
        manifest.root = portable::path_to_utf8(manifest.root); // パスは ANSI で持つ。JSON に載せるときだけ UTF-8 にする（日本語の名前のフォルダで握手が落ちていた。2026-10-06）
        (void)send_message("asset_manifest", presentation::encode_asset_manifest(manifest));
    }
}

/* ==================================================== ホストのフック（v1 §5 の対応） */

//! 直近の安い digest（`gb_change_digest`）。capture を丸ごと省くための門。
unsigned long long g_last_digest = 0;
bool g_has_last_digest = false;

int pop_native_key_locked() {
    while (!g_key_queue.empty()) {
        auto queued = std::move(g_key_queue.front()); g_key_queue.pop_front();
        if (auto key = std::get_if<int>(&queued)) return *key;
        std::vector<int> translated;
        const auto &event=std::get<presentation::InputEventWire>(queued);
        if(!presentation::native_targeting().input(event,translated)&&!g_inventory_menu.input(event,translated)&&!g_store_menu.input(event,translated))g_native_menu.input(event, translated);
        g_has_last_digest = false;
        for (auto it = translated.rbegin(); it != translated.rend(); ++it) g_key_queue.push_front(*it);
    }
    return 0;
}

//! 覚え書きを書いたか（`last_slot.txt`）。@ が出来た最初の 1 回だけ書く。
bool g_slot_remembered = false;

//! セーブの置き場に「前回遊んだ枠」を残す。次の起動が続きから始められるように。
void remember_slot_once()
{
    if (g_slot_remembered || !gb_character_generated()) {
        return;
    }
    g_slot_remembered = true;

    const std::string dir = gb_save_dir();
    const std::string base = gb_savefile_base();
    if (dir.empty() || base.empty()) {
        return;
    }
    std::string path = dir;
    if ((path.back() != '\\') && (path.back() != '/')) {
        path.push_back(portable::kPathSep);
    }
    path += kLastSlotFile;

    std::FILE *fp = std::fopen(path.c_str(), "wb");
    if (fp == nullptr) {
        return;
    }
    std::fwrite(base.data(), 1, base.size(), fp);
    std::fputc('\n', fp);
    std::fclose(fp);
    std::fprintf(stderr, "[gensoband] remembered save slot \"%s\"\n", base.c_str());
}

/*!
 * @brief Term の高さを変えてよい時か（`gb_screen_flags()` の値と `gb_awaiting_command()` の値で決める）。
 * @details Term の高さを変えると、原作の `Term_resize()` が `resize_hook`（`dungeon.c` が入れる `resize_map()`。`xtra2.c`）を呼ぶ。
 * `resize_map()` は `PR_WIPE`（`msg_print(NULL)` と `Term_clear()`。`xtra1.c` の `redraw_stuff()`）を含む再描画を立てて
 * `handle_stuff()` するので、地図の上で問い（個数・はい/いいえ・文字入力）が出ている最中に高さが変わると、
 * Term の行 0 の問いが消える（`character_icky` が立つ店・建物・一覧では `redraw_stuff()` が何もしない）。
 * Android では、問いで `text_input_active` が立つとソフトキーボードが出て窓の使える高さが縮み、
 * 画面が送る `main_term_rows` が変わる（Frox で再現して直した件と同じ。気づいたこと 2026-10-11）。
 *
 * `inkey_flag`（`gb_awaiting_command()`）が真なのは `request_command()` が最上位のコマンドを待つ間。
 * ただし店のコマンド待ちも `request_command()` を使うので真になる。店・建物の中は `character_icky` が立つので外す。
 * ダンジョンがまだ無い間（`resize_map()` が何もしない）は、すぐ反映する。
 * 反映できなかった分は覚えておき、次の呼び出しで再び試す（`gb_term_resize_rows()` は同じ高さなら何もしない）。
 */
bool term_resize_allowed(int screen_flags, int awaiting_command)
{
    return ((screen_flags & GB_SCREEN_DUNGEON) == 0) || ((awaiting_command != 0) && ((screen_flags & GB_SCREEN_ICKY) == 0));
}

/*! @return Term の高さを実際に変えたか（変えたら画面へ送る絵が変わるので、呼び手は送り直しの判定に使う）。 */
bool apply_wanted_term_rows()
{
    if (g_wanted_term_rows <= 0) {
        return false;
    }
    if (!term_resize_allowed(gb_screen_flags(), gb_awaiting_command())) {
        return false;
    }
    int cols = 0;
    int rows = 0;
    if ((gb_term_size(&cols, &rows) != GB_OK) || (rows == g_wanted_term_rows)) {
        return false;
    }
    return gb_term_resize_rows(g_wanted_term_rows) == GB_OK;
}

//! 溜まっている `ui_state` をゲームスレッド側へ渡す（`host_present` の頭で 1 回）。
bool apply_ui_state()
{
    presentation::UiStateMessage state;
    {
        std::lock_guard<std::mutex> lock(g_inbox_mutex);
        if (!g_ui_state_dirty) {
            return false;
        }
        g_ui_state_dirty = false;
        state = g_ui_state;
    }
    g_native_menu.set_enabled(state.modern_ui);
    g_native_menu.set_language(state.lang);
    g_native_menu.set_document_profile("legacy");
    g_inventory_menu.configure(state.modern_ui,state.lang,"legacy");
    presentation::configure_native_targeting(state.modern_ui&&state.targeting_ui,state.lang,[](const char *s){return gb::sjis_to_utf8(std::string(s));});
    g_store_menu.set_enabled(state.modern_ui,state.lang!="en","genso");
    gb::set_view_size(state.view_w, state.view_h);
    if(state.main_term_rows>0)g_wanted_term_rows=std::clamp(state.main_term_rows,24,80);
    gb::set_camera_follow_player(state.camera_follow_player);
    gb::set_cursor_mode(state.cursor_mode);
    /*
     * 音の入切と音量（その3 / a）。**持ち主は画面側**（`hd2d.cfg` → `ui_state.audio`。
     * 機能メニュー ＞ 音）。**効果音にも BGM にも効く**が、当て方は別である
     * （`gb_audio_set_volume` の注記。`GENSOBAND_SYSTEM_ACCEPT.md` §3.6）。
     */
    gb_audio_set_enabled(state.sound_on ? 1 : 0, state.music_on ? 1 : 0);
    gb_audio_set_volume(state.sound_volume_index, state.music_volume_index);
    /*
     * **効果音を誰が鳴らすか**。真なら
     * `gb_audio.c` は効果音に手を出さず、`frame.sounds` へ書き留めて画面側に渡す
     * ——マスで定位できる（HRTF が効く）のは画面側だけだからである。偽なら
     * 従来どおりコアが winmm で鳴らす。**曲はどちらの場合もコアが鳴らす。**
     *
     * **`use_sound` は落とさない**——コアの `sound()` はあれが偽だと `Term_xtra`
     * すら呼ばず、書き留める機会ごと消える（`gb_audio_enable_core()` が起動時に
     * 立てたまま）。「遊ぶ人が効果音を切った」は `sound_on` の側の話である。
     */
    gb_sound_set_wanted(state.sound_events ? 1 : 0);
    /*
     * サブパネルの希望（v1 §7）。**枚数は画面と揃っている**（`kProtocolSubPanelCount`
     * ＝ `kSubPanelCount` ＝ 7）。`has_sub_panel_kinds` が偽なら種類は触らない
     * ——キーごと省かれたときに「全部 UI 既定」で塗り潰さないため。
     */
    for (int i = 0; i < presentation::kProtocolSubPanelCount; ++i) {
        gb::set_sub_panel_cells(i, state.sub_panel_cells[i].cols, state.sub_panel_cells[i].rows);
        if (state.has_sub_panel_kinds) {
            gb::set_sub_panel_kind(i, state.sub_panel_kinds[i]);
        }
    }
    return true;
}

void host_present()
{
    /*
     * **安い門**（設計の P2 申し送り 6）。地図が入ってから 1 capture は 1,000 マス超に
     * なるので、待ちの間の 100 回/秒を全部組み直すと割に合わない。Term の画・turn・
     * @ の状態から digest を採り、変わっていなければ capture ごと省く。
     * 下の払い出し済みペイロード比較（v1 §6.4）は**そのまま残す**——digest は
     * 衝突しうるが、こちらは厳密だから。
     */
    bool view_changed = apply_ui_state();
    if (apply_wanted_term_rows()) {
        view_changed = true;     // Term の高さが替わると、フレームの Term の写しの行数が替わる
    }
    /*
     * **曲の終わりを取りに行く**（その3 / a）。MCI はメッセージで知らせてくるが、
     * コアは窓を持たない作りなので誰も回していない。ここで回さないと
     * **曲が 1 回鳴って止まる**。`host_present()` は待ちの間も 100 回/秒 呼ばれるので、
     * 門（digest）より**手前**に置く——省かれたフレームでも回す必要がある。
     */
    gb_audio_pump();
    /*
     * **カーソル選択は門より手前で当てる**（S2）。`request_command()` は
     * コマンドを 1 つ受けるたびに `use_menu` を落とす（`util.c:4727`）ので、
     * 「画が変わらないので capture を省いた」フレームでも立て直しが要る。
     * 費用は代入 2 つ。
     */
    gb::apply_cursor_mode();
    const unsigned long long digest = gb_change_digest();
    // 魔法・矢弾の効果の記録が溜まっているときは、画が同じでも組む（汲まれるまで残るので、組まないと届かない）。
    if (g_has_last_digest && !view_changed && (digest == g_last_digest) && !gb::attack_fx_ready()) {
        return;
    }
    g_last_digest = digest;
    g_has_last_digest = true;

    remember_slot_once();

    GameFrame frame = gb::capture_frame(++g_frame_id);
    native_store_info store_info{};
    int (*read_store_item)(int,native_store_item *)=nullptr;
    if(gb_native_store_info(&store_info))read_store_item=gb_native_store_item;
    std::vector<ModernRow> store_rows;
    if(read_store_item)for(int i=0;;++i){
        native_store_item raw{};if(!read_store_item(i,&raw))break;if(!raw.count)continue;
        ModernRow row;row.tab=raw.tab;row.slot=raw.slot;row.selection=raw.selection;row.key=raw.key;
        row.hotkey=raw.tab?raw.key:('a'+raw.slot%std::max(1,store_info.page_size));
        row.count=raw.count;row.price=raw.price;row.price_known=raw.price_known!=0;row.available=raw.available!=0;row.equipped=raw.location==1;
        row.label=gb::sjis_to_utf8(std::string(raw.name));row.weight=raw.weight;
        row.description=gb::sjis_to_utf8(std::string(raw.description));row.reason=gb::sjis_to_utf8(std::string(raw.reason));
        if(!row.available&&row.reason.empty())row.reason="This store does not accept this item.";
        row.tile=gb_tile_for_row(raw.kind_id,raw.unaware_category);
        row.glyph=gb::sjis_to_utf8(std::string(raw.glyph));row.glyph_color=raw.glyph_color;
        row.id=presentation::native_store_id(store_info.kind,raw,row.label);
        const bool ja=g_first_ui_state_lang!="en";
        const char *action=raw.tab?(store_info.kind==1?"sell":(store_info.kind==2?"deposit":"donate")):(store_info.kind==1?"buy":(store_info.kind==2?"take":"remove"));
        const char *label=raw.tab?(store_info.kind==1?(ja?"売る":"Sell"):(store_info.kind==2?(ja?"置く":"Deposit"):(ja?"寄贈する":"Donate"))):(store_info.kind==1?(ja?"買う":"Buy"):(store_info.kind==2?(ja?"取る":"Take"):(ja?"展示を取り除く":"Remove exhibit")));
        row.actions.push_back({action,gb::sjis_to_utf8(std::string(label)),row.reason,row.available,raw.tab?store_info.put_key:store_info.get_key});
        if(raw.tab==0&&store_info.kind==2&&store_info.remove_key)row.actions.push_back({"remove",gb::sjis_to_utf8(std::string(ja?"破棄する":"Destroy")),"",true,store_info.remove_key});
        store_rows.push_back(std::move(row));
    }
    g_store_menu.set_title(gb::sjis_to_utf8(std::string(store_info.title)));
    g_store_menu.set_heading(gb::sjis_to_utf8(std::string(store_info.heading)));
    g_store_menu.capture(frame,store_info,std::move(store_rows));
    presentation::prepare_native_targeting_menu_frame(frame);
    const auto items=g_native_menu.enabled()?presentation::read_native_item_rows(gb_native_describe_item,
        [](const char *s){return gb::sjis_to_utf8(s);},gb_tile_for_row):std::vector<presentation::NativeItemRow>{};
    g_native_menu.capture(frame,{},items);
    bool birth_race_choices=frame.pre_game_menu&&frame.modern.content_type=="choice_grid"
        &&(frame.modern.title=="Choose a race"||frame.modern.title==gb::sjis_to_utf8(std::string("種族の選択")));
    if(frame.pre_game_menu&&frame.modern.content_type=="choice_grid"&&!birth_race_choices) {
        // The legacy Japanese title can arrive through a mixed CP932 source
        // path.  Identify this page by the actual birth-race table instead of
        // depending on the encoding of one heading literal.
        int compared=0,matched=0;
        for(const auto &row:frame.modern.rows) {
            const auto raw=gb::utf8_to_sjis(row.label);
            ++compared;if(gb_birth_race_score_mult_named(raw.c_str())>=0)++matched;
        }
        birth_race_choices=compared>=1&&matched==compared;
    }
    if(birth_race_choices) {
        const bool ja=frame.modern.language=="ja";
        frame.modern.title=ja?gb::sjis_to_utf8(std::string("種族の選択")):"Choose a race";
        const std::string warning=ja
            ?gb::sjis_to_utf8(std::string("注意\n括弧内の割合はスコア倍率です。100%が基準で、最終スコアにこの倍率が掛かります。"))
            :"Note\nThe percentage in parentheses is the score multiplier. 100% is the baseline and this multiplier is applied to the final score.";
        for(auto &row:frame.modern.rows) {
            const auto raw=gb::utf8_to_sjis(row.label);
            int score=gb_birth_race_score_mult_named(raw.c_str());
            if(score<0) {
                int index=-1;
                if(row.key>='a'&&row.key<='z')index=row.key-'a';
                else if(row.key>='A'&&row.key<='Z')index=26+row.key-'A';
                score=gb_birth_race_score_mult(index);
            }
            if(score<0)continue;
            row.label+=" ("+std::to_string(score)+"%)";
            if(!row.description.empty())row.description+="\n\n";
            row.description+=warning;
            for(auto &action:row.actions)if(action.id=="choice")action.label=row.label;
        }
    }
    presentation::apply_native_targeting_menu_frame(frame);
    g_inventory_menu.capture(frame,[]{return presentation::read_native_owned_items(gb_native_owned_item,
        [](const char *s){return gb::sjis_to_utf8(s);},gb_tile_for_row);},[]{return gb::build_pad_commands();});
    const std::string wire = presentation::encode_game_frame(frame);
    std::string key = frame_compare_key(wire);
    if (g_has_last_frame && (key == g_last_frame_key)) {
        return; // 前回送信と同じ内容。送らない（v1 §6.4）
    }
    g_last_frame_key = std::move(key);
    g_has_last_frame = true;
    (void)send_message("frame", wire);

    // キー配列と名札の切替を見張る（v1 §8）。表を送ったあとだけ。
    if (g_tables_sent
        && ((gb_rogue_like_commands() != g_last_rogue_like) || (gb_pad_table_stamp() != g_last_pad_stamp))) {
        send_pad_commands();
    }
}

int host_next_key()
{
    if(presentation::native_targeting().take_disabled_wake())return 27;
    if(!g_native_test_started && gb_awaiting_command()) {
        const char *test=std::getenv("HD2D_TEST_STORE");
        if(test) {
            g_native_test_started=true;
            std::fprintf(stderr,"[native-menu-test] entering store %s\n",test);
            gb_native_test_open(std::atoi(test));
            g_has_last_digest=false;
        }
    }
    /* 検査用（環境変数 HD2D_TEST_MONSTER_ACT が無ければ何もしない）。命令ファイルがあれば敵に魔法・直接攻撃をさせる。 */
    if(const int wake=gb_test_monster_act_poll())return wake;
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (g_key_queue.empty()) {
        return 0;
    }
    return pop_native_key_locked();
}

void host_drop_keys()
{
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (!g_key_queue.empty()) {
        std::fprintf(stderr, "[gensoband] FLUSH drops %u key(s)\n",
            static_cast<unsigned>(g_key_queue.size()));
        for(const auto &queued:g_key_queue)if(const auto *event=std::get_if<presentation::InputEventWire>(&queued)) {
            g_native_menu.adopt_request(event->request_id,"rejected");
            g_store_menu.reject_queued_request(event->request_id);
            g_has_last_digest=false;
        }
        g_key_queue.clear();
    }
    g_key_decoder.reset();
}

void host_sleep_ms(int ms)
{
    portable::sleep_ms(ms);
}

int host_shutdown_requested()
{
    return (g_quit_requested.load() || g_ui_gone.load()) ? 1 : 0;
}

/* ==================================================================== 起動の下拵え */

/*!
 * @brief exe のあるディレクトリ（末尾に区切りを付けて返す）。
 *
 * cwd には依らない。`HengbandHd2d.exe` が子として起こす都合上、
 * 作業ディレクトリが何であっても lib を見つけられなければならない。
 */
std::string exe_dir()
{
    // 配布物の cores/ 配置でもゲームデータのルートを返す。
    return portable::data_dir();
}

//! `<exe_dir>` から下へ 1 段ずつ降りる。区切りは平台のものを使う。
std::string under_exe(const char *a, const char *b, const char *c = nullptr)
{
    const std::string base = exe_dir();
    if (base.empty()) {
        return std::string();
    }
    std::string out = base + a + portable::kPathSep + b;
    if (c != nullptr) {
        out += portable::kPathSep;
        out += c;
    }
    return out;
}

/*!
 * @brief 幻想蛮怒の lib の場所。`<exe_dir>/gensoband/lib`（設計 §2）。
 *
 * **変愚の `lib/` とは交差しない**（R5・§1 制約 6）。セーブもスコアも
 * ここから下に閉じる。
 */
std::string gensoband_lib_dir()
{
    return under_exe("gensoband", "lib");
}

/*!
 * @brief 焼いた ASCII 板の置き場。`hello_ack.asset_roots.slab` に載せる（設計 §5.4）。
 * @details 画面側の `SlabLibrary` は［ここ → 既定の置き場］の 2 段で板を探す。
 * 名寄せで転用する変愚由来の板（`R0955` 等）は既定側で見つかり、
 * 幻想蛮怒固有の板（`GA121_14` 等）はここで見つかる。
 */
std::string gensoband_slab_dir()
{
    return under_exe("gensoband", "assets", "slab");
}

//! 実体 → 絵の対応表（設計 §5.1）。
std::string mapping_csv_path()
{
    return under_exe("gensoband", "tilework", "mapping.csv");
}

bool path_is_file(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    if (portable::dir_exists(path)) {
        return false;
    }
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return false;
    }
    std::fclose(fp);
    return true;
}

/*!
 * @brief 前回遊んだセーブ枠を読む（`<save>/last_slot.txt`）。無ければ空。
 * @details 新規作成では**先方が誕生画面でセーブ名を訊く**（`files.c:10184`
 * `process_player_name` の `mod140316` 改造）ので、こちらが決めた枠は使われない。
 * その名を覚えておかないと次の起動で続きから遊べない（P3 で照合した事実）。
 */
std::string read_remembered_slot()
{
    const std::string dir = gb_save_dir();
    if (dir.empty()) {
        return std::string();
    }
    std::string path = dir;
    if ((path.back() != '\\') && (path.back() != '/')) {
        path.push_back(portable::kPathSep);
    }
    path += kLastSlotFile;

    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return std::string();
    }
    char buf[64]{};
    const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, fp);
    std::fclose(fp);

    std::string slot(buf, n);
    while (!slot.empty() && ((slot.back() == '\n') || (slot.back() == '\r') || (slot.back() == ' '))) {
        slot.pop_back();
    }
    /* 道の区切りが入っていたら受けない（枠の名はディレクトリを含まない）。 */
    if (slot.find_first_of("\\/:") != std::string::npos) {
        return std::string();
    }
    return slot;
}

/* ============================================ 言語切り替えの立て直し（Sil-Q A2 の写し） */

//! `.resume` のパス。セーブの置き場が決まる前（bootstrap 前）は空。
std::string resume_file_path()
{
    std::string dir = gb_save_dir();
    if (dir.empty()) {
        return std::string();
    }
    if ((dir.back() != '\\') && (dir.back() != '/')) {
        dir.push_back(portable::kPathSep);
    }
    return dir + kResumeFile;
}

/*!
 * @brief `.resume` を読み、**必ず消す**（一度きり。持ち越さない）。
 * @details `--resume` 無しで起きたときも呼ばれて消す——古い印を次の立て直しに
 * 拾わせない（Sil-Q A2 の 3 と同じ規則）。
 */
std::string take_resume_slot()
{
    const std::string path = resume_file_path();
    if (path.empty()) {
        return std::string();
    }
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        return std::string();
    }
    char buf[64]{};
    const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, fp);
    std::fclose(fp);
    std::remove(path.c_str());

    std::string slot(buf, n);
    while (!slot.empty() && ((slot.back() == '\n') || (slot.back() == '\r') || (slot.back() == ' '))) {
        slot.pop_back();
    }
    if (slot.find_first_of("\\/:") != std::string::npos) {
        return std::string();
    }
    return slot;
}

/*!
 * @brief 立て直しへ向けて `.resume` を書く。
 * @details 呼ぶのは **`quit_request` で畳んだとき**だけ（画面が言語切り替えで
 * 起こし直す経路。`gb_shutdown_and_quit()` の強制保存がそのまま「自動セーブ」になる）。
 * 保存の実体が無ければ書かない——次の回が空の枠を開こうとして新規に化ける。
 */
void write_resume_slot()
{
    const char *base = gb_savefile_base();
    if ((base == nullptr) || (base[0] == '\0')) {
        return;
    }
    if (!path_is_file(gb_savefile_path())) {
        return;
    }
    const std::string path = resume_file_path();
    if (path.empty()) {
        return;
    }
    std::FILE *fp = std::fopen(path.c_str(), "wb");
    if (fp == nullptr) {
        return;
    }
    std::fwrite(base, 1, std::strlen(base), fp);
    std::fputc('\n', fp);
    std::fclose(fp);
    std::fprintf(stderr, "[gensoband] wrote the resume slot \"%s\" for the relaunch\n", base);
}

/*!
 * @brief セーブ枠を決めて `savefile` を据える。
 * @param requested `--savefile=` の値（空なら覚えている枠 → 既定 `player`）
 * @return 真なら**新規作成**（枠の実体が無い）、偽ならロード
 *
 * @details **`play_game(FALSE)` を「無いセーブ」に対して呼んではいけない。**
 * Windows では `load_player()` の `access()` 前検査が `#if !defined(WINDOWS)` で
 * 外れており（`save.c:2268`）、そのまま `fd_open` に落ちて失敗 →
 * `play_game` が `quit("セーブファイルが壊れています")` を踏む。
 * 「在ればロード・無ければ新規」（設計 §3.2）は、**呼ぶ側が在るかを見て
 * 引数を決める**形でしか成立しない。P3 で照合した事実。
 */
bool choose_savefile(const std::string &requested)
{
    std::string slot = requested;
    if (slot.empty()) {
        slot = read_remembered_slot();
    }
    if (slot.empty()) {
        slot = kDefaultSlot;
    }

    const int rc = gb_set_savefile(slot.c_str());
    if (rc != GB_OK) {
        std::fprintf(stderr, "[gensoband] could not set the save slot \"%s\" (%d); falling back to a new game\n",
            slot.c_str(), rc);
        gb_clear_savefile();
        return true;
    }

    if (path_is_file(gb_savefile_path())) {
        std::fprintf(stderr, "[gensoband] save slot \"%s\" -> %s (load)\n", slot.c_str(), gb_savefile_path());
        return false;
    }

    /*
     * 無い。**名を外してから**新規で始める（`gb_clear_savefile()` の説明）。
     * 名は誕生画面でプレイヤが付ける。その名は `remember_slot_once()` が覚える。
     */
    std::fprintf(stderr, "[gensoband] no save at %s; starting a new game\n", gb_savefile_path());
    gb_clear_savefile();
    return true;
}

/* ========================================================= タイトルとセーブ選択（P5） */

/*!
 * @brief セーブ枠の一覧（`gb_save_dir()` 直下の、名前に `.` を含まないファイル）。
 * @details `.` 持ちを外すのは `last_slot.txt`・`.gitkeep`・`koma.bak` のような
 * 道具類を隠すため（枠の名は誕生画面の入力で、`.` は普通入らない）。
 * 名前は**コア内部コード（SJIS）のまま**扱う——表示先の Term も SJIS である。
 */
std::vector<std::string> list_save_slots()
{
    std::vector<std::string> slots;
    std::string dir = gb_save_dir();
    if (dir.empty()) {
        return slots;
    }
    if ((dir.back() != '\\') && (dir.back() != '/')) {
        dir.push_back(portable::kPathSep);
    }
    for (const std::string &name : portable::list_files(dir)) {
        // 拡張子の付いたものは枠ではない（`last_slot.txt` 等）。
        if (name.empty() || (name.find('.') != std::string::npos)) {
            continue;
        }
        slots.push_back(name);
    }
    std::sort(slots.begin(), slots.end());
    return slots;
}

/*!
 * @brief メニュー用にキーを 1 個待つ。
 * @return キー（コア内部コードのバイト）、または **-1**（終了要求）
 * @details 待ちの間も `host_present()` を回す——`ui_state` の適用と、
 * 送り漏れの補償はあちらの安い門（digest）が面倒を見る。
 */
int wait_key_for_menu()
{
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            if (!g_key_queue.empty()) {
                return pop_native_key_locked();
            }
        }
        if (g_ui_gone.load() || g_quit_requested.load()) {
            return -1;
        }
        host_present();
        portable::sleep_ms(20);
    }
}

/*
 * メニュー文言の日本語リテラルは**そのまま `gb_term_putstr()` へ渡す**。
 * このソースは UTF-8 だが `/source-charset:utf-8`＋実行文字セット既定（CP932）で
 * 組まれるので、**リテラルはコンパイル時に SJIS のバイト列になっている**
 * （vcxproj の註記）。`gb::utf8_to_sjis()` に通すと「不正な UTF-8」で空になる
 * ——P5 の初版で実際に見出しと n) の行だけが消えた。
 */

/*!
 * @brief タイトル（`lib/file/news_j.txt`）とセーブ選択を出し、枠を決める。
 * @return 0 = 選んだ枠をロード（`savefile` 据え済み）/ 1 = 新規 / -1 = 終了要求
 *
 * @details 決めたこと（2026-08-19 P5）:「セーブデータ選択を実装」
 * 「幻想もタイトルがあるなら表示させて」。先方にタイトルメニューは無い
 * （「開く」は Win32 ファイルダイアログ直結）ので、news の画面へ letter 式の
 * 一覧を重ねる形を**アダプタが持つ**。コアには 1 行も足していない。
 * `--savefile=` 指定時はここへ来ない（`run_protocol`。検証・自動運転のオプション）。
 *
 * 画面は 80×27: 上 21 行が news そのまま・下 6 行が選択（見出し＋3 列×3 行＋新規）。
 * letter は a〜i の 9 件まで。それを超えたぶんは見出しに件数だけ正直に書く
 * （隠れた枠は `--savefile=` で開ける）。
 */
int modern_title_and_pick_savefile()
{
    const bool ja=g_first_ui_state_lang!="en";
    presentation::NativeSavePicker picker("Gensoband",ja,list_save_slots,
        [](const std::string &name){return presentation::remove_native_save(gb_save_dir(),name);},
        [](const std::string &name){return gb::sjis_to_utf8(name);});
    for(;;) {
        const auto page=picker.page();gb_term_clear();
        gb_term_putstr(2,1,1,gb::utf8_to_sjis(page.title).c_str());
        std::vector<MenuChoice> choices;
        for(size_t i=0;i<page.rows.size();++i) {
            const auto &row=page.rows[i];const int at=4+int(i);
            const auto line=std::string(1,char(row.key))+") "+row.label;
            gb_term_putstr(2,at,row.selected?14:1,gb::utf8_to_sjis(line).c_str());
            MenuChoice choice;choice.line_index=at;choice.span_begin=2;choice.span_len=int(line.size());choice.key_begin=2;choice.key_len=1;
            choice.key=row.key;choice.selected=row.selected;choice.label_utf8=row.label;choice.description_utf8=page.description;choices.push_back(std::move(choice));
        }
        if(page.rows.size()<4) {
            std::string description=page.description;size_t pos=0;int at=10;
            while(pos<description.size()&&at<kTermRows) {auto end=description.find('\n',pos);auto line=description.substr(pos,end==std::string::npos?end:end-pos);gb_term_putstr(2,at++,1,gb::utf8_to_sjis(line).c_str());if(end==std::string::npos)break;pos=end+1;}
        }
        gb::set_pregame_choices(choices);gb_term_present();
        const int result=picker.key(wait_key_for_menu());
        if(result==-2)continue;
        gb::set_pregame_choices({});
        if(result<0)return -1;
        if(result==1){gb_clear_savefile();return 1;}
        return choose_savefile(picker.selected())?1:0;
    }
}

int title_and_pick_savefile()
{
    if(g_native_menu.enabled())return modern_title_and_pick_savefile();
    const std::vector<std::string> slots = list_save_slots();
    const std::string last = read_remembered_slot();
    constexpr int kMaxListed = 9;
    const int listed = std::min<int>(static_cast<int>(slots.size()), kMaxListed);
    const int hidden = static_cast<int>(slots.size()) - listed;

    gb_term_clear();

    /* --- タイトル（news_j.txt そのまま。SJIS）。無ければ題字 1 行で代える。 --- */
    {
        /*
         * **英語のときは英訳の題字を読む**（設計 §5 の「アダプタ側」・§7 の題字）。
         * `lib/file/news.txt` は変愚 2.1.2 の題字で幻想蛮怒のものではないので、
         * `gb_lang_file()` の名前の引き替えには載せない。題字はコアの file 機構を
         * 通らず**この関数だけが読む**ので、訳文の置き場（`lang/en/file`）から直に取る。
         */
        std::string path = gensoband_lib_dir() + portable::kPathSep + "file"
            + portable::kPathSep + "news_j.txt";
        if (gb_lang_enabled()) {
            const std::string en = under_exe("gensoband", "lang", "en")
                + portable::kPathSep + "file" + portable::kPathSep + "news.txt";
            if (path_is_file(en)) {
                path = en;
            } else {
                std::fprintf(stderr, "[gensoband] no english title art at %s\n", en.c_str());
            }
        }
        std::FILE *fp = std::fopen(path.c_str(), "rb");
        if (fp != nullptr) {
            char line[256];
            int row = 0;
            while ((row < (kTermRows - 6)) && (std::fgets(line, sizeof(line), fp) != nullptr)) {
                std::size_t n = std::strlen(line);
                while ((n > 0) && ((line[n - 1] == '\n') || (line[n - 1] == '\r'))) {
                    line[--n] = '\0';
                }
                gb_term_putstr(2, row, 1, line);
                ++row;
            }
            std::fclose(fp);
        } else {
            std::fprintf(stderr, "[gensoband] no title art at %s\n", path.c_str());
            gb_term_putstr(34, 2, 1, "幻想蛮怒");
        }
    }

    /* --- セーブ選択（下 6 行: 見出し 22・一覧 23〜25・新規 26）。 --- */
    std::string head = "どのセーブデータで始めますか？（* = 前回）";
    if (hidden > 0) {
        head += "（ほか " + std::to_string(hidden) + " 件は --savefile= で）";
    }
    gb_term_putstr(6, kTermRows - 5, 11, head.c_str());
    if (slots.empty()) {
        gb_term_putstr(8, kTermRows - 4, 2, "（セーブデータはまだありません）");
    }
    /*
     * 選択肢は**画面側のカーソル層にも宣言する**（P5 その2・2026-08-19。気づいたこと
     * 「セーブデータ選択が文字指定でしか選択できない」）。`frame.menu_choices` に
     * 載れば、矢印・パッド・クリックは hd2d の既存実装（`ui_cursor.cpp` と
     * `menu_choice_at`）が letter へ翻訳して送ってくる——コア側の受けは従来の
     * letter のまま。span は **SJIS の行バッファのバイト位置 ＝ Term の桁**を
     * UTF-8 長へ換算して出す（ミラー行は SJIS→UTF-8 で運ばれる。`row_to_utf8` は
     * 行頭・行中の空白を保つので、行の先頭からの換算で一致する）。
     */
    std::vector<MenuChoice> choices;
    const auto utf8_len = [](const std::string &sjis) {
        return static_cast<int>(gb::sjis_to_utf8(sjis).size());
    };
    for (int row = 0; row < 3; ++row) {
        std::string buf; //!< この Term 行の全体（SJIS）。1 回で描く
        for (int col = 0; col < 3; ++col) {
            const int i = (row * 3) + col;
            if (i >= listed) {
                break;
            }
            const auto want = static_cast<std::size_t>(8 + (col * 24));
            if (buf.size() < want) {
                buf.resize(want, ' ');
            } else if (!buf.empty()) {
                buf.push_back(' '); //!< 長い枠名が桁を越えたら詰めて続ける（切らない）
            }
            const std::size_t begin = buf.size();
            buf.push_back(static_cast<char>('a' + i));
            buf += ") ";
            buf += slots[static_cast<std::size_t>(i)];
            const bool is_last = slots[static_cast<std::size_t>(i)] == last;
            if (is_last) {
                buf += " *";
            }
            MenuChoice mc{};
            mc.line_index = (kTermRows - 4) + row;
            mc.span_begin = utf8_len(buf.substr(0, begin));
            mc.span_len = utf8_len(buf.substr(begin));
            mc.key_begin = mc.span_begin;
            mc.key_len = 1;
            /*
             * **前回の枠だけ key を `\r`（Enter）にする。**カーソル層の初期位置が
             * ここへ来て（`ui_cursor.cpp`「Enter の選択肢があるなら初期カーソルは
             * そこへ」）、Enter／A ボタン一発で続きから遊べる。コアの受けは
             * `\r` = 前回の枠なので意味も同じ。**letter の選択肢を同じ span に
             * 重ねてはいけない**——choices に 2 つ並ぶと左右移動が重複ぶんで
             * 足踏みし、「カーソルを 2 回押さないと枠が動かない」（気づいたこと
             * 2026-08-19）。letter で直接選ぶ道は choices と無関係にコアが受ける。
             */
            mc.key = is_last ? '\r' : ('a' + i);
            choices.push_back(mc);
        }
        if (!buf.empty()) {
            gb_term_putstr(0, (kTermRows - 4) + row, 1, buf.c_str());
        }
    }
    {
        const char *const tail = "n) 新しく始める";
        MenuChoice mc{};
        mc.line_index = kTermRows - 1;
        mc.span_begin = 8;
        mc.span_len = utf8_len(tail);
        mc.key_begin = 8;
        mc.key_len = 1;
        mc.key = 'n';
        choices.push_back(mc);
        gb_term_putstr(8, kTermRows - 1, 14, tail);
    }
    gb::set_pregame_choices(choices);
    gb_term_present();

    for (;;) {
        const int key = wait_key_for_menu();
        if (key < 0) {
            gb::set_pregame_choices({});
            return -1;
        }
        if ((key == 'n') || (key == 'N')) {
            gb::set_pregame_choices({});
            gb_clear_savefile();
            return 1;
        }
        if ((key == '\r') && !last.empty()) {
            /* Enter = 前回の枠（カーソル層の初期位置と同じ意味。無ければ何もしない）。 */
            for (int i = 0; i < listed; ++i) {
                if (slots[static_cast<std::size_t>(i)] == last) {
                    if (!choose_savefile(last)) {
                        gb::set_pregame_choices({});
                        return 0;
                    }
                    break;
                }
            }
        }
        if ((key >= 'a') && (key < ('a' + listed))) {
            const std::string &slot = slots[static_cast<std::size_t>(key - 'a')];
            gb::set_pregame_choices({});
            if (!choose_savefile(slot)) {
                return 0;
            }
            /* 在ったはずの枠が消えている（外で消された等）。新規に倒す。 */
            return 1;
        }
        /* それ以外は無視して待つ（ESC も——戻る先が無い）。 */
    }
}

void print_usage()
{
    std::fprintf(stderr,
        "GensobandCore (%s %s) - protocol v%d, stdio transport\n"
        "\n"
        "  usage: GensobandCore.exe %s [--protocol-log=<path>] [--savefile=<slot>]\n"
        "                            [--lang=ja|en] [--report-untranslated=<path>] [--resume]\n"
        "         GensobandCore.exe --selftest\n"
        "\n"
        "  --selftest   lib を読み、init_angband() まで走らせて版を出す。\n"
        "  --savefile=  gensoband/lib/save/<slot> を使う。在ればロード・無ければ新規。\n"
        "               省略するとタイトルとセーブ選択画面が出る（P5）。\n"
        "  --lang=      言語。省略すると最初の ui_state.lang を待つ（既定は日本語）。\n"
        "  --resume     言語切り替えの立て直しの回（画面側が付ける）。.resume の枠を開く。\n"
        "  --report-untranslated=  英語モードで引けなかった鍵を書き出す。\n"
        "\n"
        "This executable is not meant to be started by hand. The frontend\n"
        "launches it as a child process and speaks the core protocol over\n"
        "stdin/stdout.\n",
        gb_core_name(), gb_core_version(), presentation::kProtocolVersion, kUiProtocolArg.data());
    std::fflush(stderr);
}

/*!
 * @brief `--selftest`。起動列を通し、通ったことの証拠を出す。
 * @return プロセスの終了コード
 *
 * **フックを差さない**ので、null term は P1 と同じ「ESC を積むだけ」で回る。
 * パイプもプロトコルも要らない（設計 §8 P1 の受け入れを壊さないこと）。
 */
int run_selftest()
{
    const std::string lib = gensoband_lib_dir();
    if (lib.empty()) {
        std::fprintf(stderr, "[gensoband] cannot resolve the exe directory\n");
        return 1;
    }

    std::fprintf(stderr, "[gensoband] lib = %s\n", lib.c_str());
    std::fflush(stderr);

    int rc = gb_term_install(kTermCols, kTermRows);
    if (rc != GB_OK) {
        std::fprintf(stderr, "[gensoband] gb_term_install failed (%d)\n", rc);
        return 1;
    }

    rc = gb_bootstrap(lib.c_str());
    if (rc != GB_OK) {
        std::fprintf(stderr, "[gensoband] bootstrap failed (%d): %s\n", rc, gb_last_error());
        gb_term_remove();
        return 1;
    }

    int cols = 0;
    int rows = 0;
    (void)gb_term_size(&cols, &rows);

    /* 23 行目 = note() の書き込み先（init2.c:2232）。中身は SJIS のまま出す。 */
    char line[512];
    const int cells = gb_term_row(23, line, nullptr, static_cast<int>(sizeof(line)));
    std::fprintf(stderr, "[gensoband] term %dx%d, row23(%d) = \"%s\"\n",
        cols, rows, cells, (cells > 0) ? line : "");

    const int name_failures = gb_name_en_selftest();
    std::fprintf(stderr, "[gensoband] name rebuild: %s\n", name_failures == 0 ? "ok" : "FAIL");
    const int menu_failures = gb::menu_semantics_selftest();
    std::fprintf(stderr, "[gensoband] menu semantics: %s\n", menu_failures == 0 ? "ok" : "FAIL");
    const int mirror_failures = gb::term_mirror_baseline_selftest();
    std::fprintf(stderr, "[gensoband] term mirror baseline: %s\n", mirror_failures == 0 ? "ok" : "FAIL");

    int command_failures = 0;
    const auto pad_commands = gb::build_pad_commands();
    const auto usable_command = [&](int command) {
        const auto it = std::find_if(pad_commands.entries.begin(), pad_commands.entries.end(),
            [command](const auto &entry) { return entry.command == command; });
        return it != pad_commands.entries.end() && !it->seq_original.empty() && !it->seq_rogue.empty();
    };
    if (!usable_command('m')) {
        ++command_failures;
        std::fprintf(stderr, "[gensoband] command regression FAIL: m (use spell)\n");
    }
    if (!usable_command('b')) {
        ++command_failures;
        std::fprintf(stderr, "[gensoband] command regression FAIL: b (browse spells/powers)\n");
    }
    std::fprintf(stderr, "[gensoband] m/b command regression: %s\n", command_failures == 0 ? "ok" : "FAIL");

    /* Term の高さを変えてよい時（問いの最中に変えると、原作の resize_map() が問いの行を消す） */
    int term_resize_failures = 0;
    {
        const auto expect = [&](bool ok, const char *what) {
            if (!ok) {
                ++term_resize_failures;
                std::fprintf(stderr, "[gensoband] selftest: term resize gate: FAIL - %s\n", what);
            }
        };
        expect(term_resize_allowed(0, 0), "no dungeon yet: resize now");
        expect(term_resize_allowed(GB_SCREEN_GENERATED | GB_SCREEN_DUNGEON, 1), "dungeon, waiting for a command: resize now");
        expect(!term_resize_allowed(GB_SCREEN_GENERATED | GB_SCREEN_DUNGEON, 0), "dungeon, inside a prompt: defer");
        expect(!term_resize_allowed(GB_SCREEN_GENERATED | GB_SCREEN_DUNGEON | GB_SCREEN_ICKY, 0), "dungeon, inside a menu: defer");
        expect(!term_resize_allowed(GB_SCREEN_GENERATED | GB_SCREEN_DUNGEON | GB_SCREEN_ICKY, 1), "dungeon, store command wait (inkey_flag is true but icky): defer");
        std::fprintf(stderr, "[gensoband] selftest: term resize gate: %s\n", (term_resize_failures == 0) ? "ok" : "FAIL");
    }

    /* 光源の種類の判定（光の届く範囲）。キャラクターは要らない。 */
    char light_err[160];
    const int light_failures = gb_light_selftest(light_err, static_cast<int>(sizeof(light_err)));
    if (light_failures != 0) {
        std::fprintf(stderr, "[gensoband] selftest: light kind: FAIL - %d case(s), first: %s\n", light_failures, light_err);
    } else {
        std::fprintf(stderr, "[gensoband] selftest: light kind: ok\n");
    }

    gb_term_remove();

    /*
     * 状態のアイコンの表の自己検査。キャラクターは要らない。
     * 環境変数 HENGBAND_STATUS_ICON_TABLE に `hd2d/ui/status_icon_table.inc` の絶対パスがあれば、
     * 送る鍵が全部その表にあることまで見る（無ければ表の中の整合だけ。関門はパスを渡す）。
     */
    char status_err[160];
    int status_checked = 0;
    const int status_failures = gb_status_selftest(status_err, static_cast<int>(sizeof(status_err)), &status_checked);
    if (status_failures != 0) {
        std::fprintf(stderr, "[gensoband] selftest: status icons: FAIL - %d case(s), first: %s\n", status_failures, status_err);
    } else {
        std::fprintf(stderr, "[gensoband] selftest: status icons: ok (%d keys checked against the icon table, %d bar entries, %d permanent rows)\n",
            status_checked, gb_status_bar_table_count(), gb_status_perm_row_count());
    }

    /* 受け入れの合図（設計 §8 P1 / §9-1）。**この 1 行だけは必ず ASCII**。 */
    std::fprintf(stderr, "%s %s\n", gb_core_name(), gb_core_version());
    std::fflush(stderr);

    return (menu_failures == 0 && mirror_failures == 0 && name_failures == 0 && command_failures == 0 && term_resize_failures == 0 && light_failures == 0 && status_failures == 0) ? 0 : 1;
}

/* ==================================================================== 本体（P2） */

/*! @brief 起動引数の束（`main()` と `hengband_core_entry()` が埋める）。 */
struct RunArgs {
    std::string log_path;       //!< `--protocol-log=`
    std::string savefile_slot;  //!< `--savefile=`
    std::string lang;           //!< `--lang=`（空なら最初の `ui_state` を待つ）
    std::string report_path;    //!< `--report-untranslated=`
    bool resume{ false };       //!< `--resume`（言語切り替えの立て直しの回）
};

/*!
 * @brief プロトコルを回す本体。**輸送路は呼び手が決める。**
 * @details Windows は `main()` が標準入出力を渡す（`GensobandCore.exe` は画面側の子プロセス）。
 * Android はコアが同じプロセスの別スレッドなので、入口（`hengband_core_entry`）が
 * `pipe()` の両端を渡す。ここから下は**どちらでも同じ道**を通る。
 */
int run_protocol(presentation::TransportHandle in, presentation::TransportHandle out,
    const RunArgs &args)
{
    g_in = in;
    g_out = out;
    if ((g_in == nullptr) || (g_out == nullptr)) {
        std::fprintf(stderr, "[gensoband] no transport endpoint; cannot speak the protocol\n");
        return 3;
    }
    if (!args.log_path.empty()) {
        g_log.open(args.log_path);
    }

    /* --- 握手（v1 §3.1）。最初のメッセージは hello でなければならない。 --- */
    std::string payload;
    std::string err;
    const presentation::TransportResult got = presentation::read_message(g_in, payload, err);
    if (got != presentation::TransportResult::Ok) {
        std::fprintf(stderr, "[gensoband] failed to read the first message: %s%s%s\n",
            presentation::transport_result_name(got), err.empty() ? "" : " - ", err.c_str());
        return 4;
    }
    const std::string type = presentation::peek_message_type(payload);
    g_log.record("in", type, payload);
    if (type != "hello") {
        send_fatal(std::string("expected \"hello\" as the first message but got \"")
            + (type.empty() ? std::string("<not a protocol message>") : type) + "\"");
        return 1;
    }

    presentation::HelloMessage hello;
    if (!presentation::decode_hello(payload, hello, err)) {
        send_fatal(std::string("could not decode hello: ") + err);
        return 1;
    }
    if (hello.protocol != presentation::kProtocolVersion) {
        char buf[128]{};
        std::snprintf(buf, sizeof(buf), "protocol version mismatch: ui speaks %d, core speaks %d",
            hello.protocol, presentation::kProtocolVersion);
        send_fatal(buf);
        return 1;
    }
    std::fprintf(stderr, "[gensoband] hello from %s %s (protocol %d)\n",
        hello.ui_name.empty() ? "<unnamed ui>" : hello.ui_name.c_str(),
        hello.ui_version.empty() ? "?" : hello.ui_version.c_str(), hello.protocol);

    /*
     * 目録は**握手より前に**読む。`hello_ack` に載せる `asset_roots.slab` と
     * 対になっているものなので、片方だけ申告する状態を作らない。
     */
    const std::string csv = mapping_csv_path();
    if (!g_manifest.load(csv, exe_dir())) {
        /* 致命ではない——絵が出ないだけで遊べる。**捏造せずに数えて言う**（§5.1）。 */
        std::fprintf(stderr, "[gensoband] tile manifest unavailable: %s\n", g_manifest.error().c_str());
    } else {
        /*
         * 正体の分からないアイテムの代表タイルを追加登録する。
         * 新しい絵は作らない——変愚の既存の板（tilework/sfc/K<id>.png）を指す行を
         * 目録の末尾へ足すだけ（gb_manifest.cpp の add_unaware_representative_tiles() 参照）。
         */
        g_manifest.add_unaware_representative_tiles();
        // 目録の記録は、画面が実際に引く板（.vox）の有無を数える。PNG は HD2D が使わない開発の頃の 2D の絵で、
        // 配布物には無いので「全部無い」と出て、異常のダイアログで原因に見えていた（2026-10-06。slab_presence.h）。
        const auto slabs = presentation::count_slab_presence(g_manifest.message(), { gensoband_slab_dir(), presentation::default_slab_dir() });
        std::fprintf(stderr, "[gensoband] tile manifest: %u entries from %s (entity slabs: %d of %d missing)\n",
            static_cast<unsigned>(g_manifest.message().assets.size()), csv.c_str(), slabs.missing, slabs.entity_rows);
        gb::set_tile_manifest(&g_manifest);
    }

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = gb_core_name();
    ack.core_version = gb_core_version();
    /*
     * `features`（v1 §3.1）。リアルタイム進行は変愚だけの機能なので、ここで名乗らない
     * ＝画面側のリアルタイム設定が自然に隠れる（設計 §6.3）。
     * **`audio` は名乗る**——`gb_audio.c` が効果音（PlaySound）と BGM（MCI）を鳴らすので、
     * 機能メニューの「音」の節が出る。
     * ただし**鳴らせるのは Windows だけ**である（winmm の PlaySound / MCI）。Android では
     * `gb_audio.c` が空実装になるので、名乗らない＝出せない節を出さない。
     * `asset_roots.graf` は**出さない**——8/16px の面を持たないので、
     * 画面側に該当スタイルを無効化させる。`slab` は出す（設計 §5.4）。
     */
#if defined(_WIN32)
    ack.features.emplace_back("audio");
#endif
    /*
     * `lang-restart`（Sil-Q A2 の写し）:
     * 言語が替わったら画面がこのコアを**起こし直す**（`hd2d/app/hd2d_app.cpp` の
     * 一般形。コア名の分岐は無い）。起こし直しの回には `--resume` が付き、
     * `.resume` に覚えた枠を開き直す。申告しないと `ui_state.lang` が次のフレームから
     * 効く扱いになるが、edit と raw は読み直せないので言語は変わらない。
     */
    ack.features.emplace_back("lang-restart");
    ack.asset_root_slab = portable::path_to_utf8(gensoband_slab_dir()); // パスは ANSI で持つ。JSON に載せるときだけ UTF-8 にする（日本語の名前のフォルダで握手が落ちていた。2026-10-06）
    if (!send_message("hello_ack", presentation::encode_hello_ack(ack))) {
        return 5;
    }

    /*
     * 受信スレッド（v1 §5 の「キー待ちブロックは core 内でパイプ読みに置き換わる」）。
     * detach する: 終了経路は `gb_run_game()` からの復帰か `quit()` で、join する場所が無い。
     */
    std::thread(receive_loop).detach();

    /* --- Term を立てて起動列を通す。ここから先の Term_fresh は frame になる。 --- */
    const std::string lib = gensoband_lib_dir();
    if (lib.empty()) {
        send_fatal("cannot resolve the exe directory");
        return 1;
    }
    std::fprintf(stderr, "[gensoband] lib = %s\n", lib.c_str());

    /*
     * 言語を決める（設計 §4.1）。`--lang=` があればそれが勝つ。無ければ**最初の
     * `ui_state` を待つ**（上限 2000 ms。画面は握手の直後に必ず 1 回送る——
     * Sil-Q P0 の実測 15 ms。時間切れは日本語＝今までどおり）。
     * `gb_bootstrap()` が `gb_lang_enabled()` を見て edit/data を差し替えるので、
     * カタログの読み込みは **bootstrap より前**でなければならない。
     */
    std::string lang = args.lang;
    if (lang.empty()) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto waited_ms = [&t0]() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        };
        while (!g_first_ui_state_seen.load()) {
            if (g_ui_gone.load() || g_quit_requested.load() || (waited_ms() >= 2000)) {
                break;
            }
            portable::sleep_ms(5);
        }
        {
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            lang = g_first_ui_state_lang;
        }
        std::fprintf(stderr, "[gensoband] first ui_state after %lld ms, lang = %s\n",
            static_cast<long long>(waited_ms()), lang.empty() ? "(none)" : lang.c_str());
    }
    {
        const gb::LangLoadReport lrep
            = gb::lang_init(lang, under_exe("gensoband", "lang", "en"), lib, args.report_path);
        if (lrep.enabled) {
            std::fprintf(stderr,
                "[gensoband] english catalog: %d entries (%d silent, %d overridden, "
                "%d rejected, %d not in cp932)%s%s\n",
                lrep.entries, lrep.silent, lrep.overridden, lrep.rejected, lrep.not_in_cp932,
                lrep.error.empty() ? "" : " - ", lrep.error.c_str());
        }
    }

    int rc = gb_term_install(kTermCols, kTermRows);
    if (rc != GB_OK) {
        send_fatal("could not install the headless term");
        return 1;
    }

    gb_host_hooks hooks{};
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.drop_keys = host_drop_keys;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    gb_set_host_hooks(&hooks);

    rc = gb_bootstrap(lib.c_str());
    if (rc != GB_OK) {
        gb_set_host_hooks(nullptr);
        /*
         * **「起動に失敗した」と「起動中に ui が消えた」を取り違えない。**
         * 後者では null term の待ちが `gb_shutdown_and_quit()` → `quit(NULL)` を通り、
         * 起動の段の跳び先に落ちて `GB_ERR_QUIT` に見える。ここで `fatal` を送ると
         * 「幻想蛮怒コアが壊れている」という嘘の記録が残る。
         */
        if (g_ui_gone.load() || g_quit_requested.load()) {
            std::fprintf(stderr, "[gensoband] shut down during startup (the ui went away)\n");
            send_exit_once(g_ui_gone.load() ? 1 : 0);
            g_log.close();
            return 0;
        }
        /* 説明はコア内部コード（SJIS）。**線に乗せる前に UTF-8 へ直す**（設計 §3.1）。 */
        const std::string reason = gb::sjis_to_utf8(gb_last_error());
        send_fatal(std::string("bootstrap failed: ") + (reason.empty() ? "(unknown)" : reason));
        gb_term_remove();
        g_log.close();
        return 1;
    }

    /*
     * **サブパネル用の Term を 7 枚立てる**（S1）。`gb_bootstrap()` の後で
     * なければならない——`init_angband()` が `angband_term[]` を触る。
     * 立てるだけで `window_flag` は触らない（種類は画面が決める）。
     */
    if (gb_sub_terms_install() != GB_OK) {
        std::fprintf(stderr, "[gensoband] sub panel terms could not be installed\n");
    }

    /*
     * 音の設定ファイルを読む（その3 / a）。**`gb_bootstrap()` の後**でなければならない
     * ——道（`ANGBAND_DIR_XTRA_SOUND` / `_MUSIC`）は `init_angband()` が決める。
     * 素材が無ければ 0 件で読み終わり、以後は黙って鳴らない。
     */
    gb_audio_init();
    //! コアが音の縁を投げてくるようにする（理由は `gb_audio.c` の注記）。
    gb_audio_enable_core();

    /* 初期化完了（v1 §8）。表はここで 1 回。 */
    send_startup_tables();

    /*
     * ゲームを回す。**戻ってくるのは終わったとき**。
     * 枠の決め方（P5・2026-08-19）: `--savefile=` があれば従来どおり黙って決める
     * （固定スロット方式のまま——検証・自動運転のオプション）。無ければ**タイトルと
     * セーブ選択**を出して遊ぶ人が選ぶ（決めたこと「セーブデータ選択を実装」
     * 「幻想もタイトルがあるなら表示させて」）。
     */
    int pick;
    const std::string resumed = take_resume_slot(); //!< **必ず読んで消す**（持ち越さない）
    /*
     * 誕生の最初の画面で取消されたら（`GB_BIRTH_CANCEL`）、タイトルとセーブ選択を出し直す（修-61。コアのプロセスは終わらない）。
     * 2 周目以降は `--savefile=`・`--resume` の指定を使わず、いつもの選択画面から始める。
     */
    for (bool first_round = true;; first_round = false) {
        if (first_round && args.resume && !resumed.empty()) {
            /* 言語切り替えの立て直し（Sil-Q A2）。一覧を出さずに直前の枠を開き直す。 */
            std::fprintf(stderr, "[gensoband] resuming the save slot \"%s\" (relaunch)\n", resumed.c_str());
            pick = choose_savefile(resumed) ? 1 : 0;
        } else if (first_round && !args.savefile_slot.empty()) {
            pick = choose_savefile(args.savefile_slot) ? 1 : 0;
        } else {
            pick = title_and_pick_savefile();
        }
        if (pick < 0) {
            std::fprintf(stderr, "[gensoband] shut down at the save picker (the ui went away)\n");
            send_exit_once(g_ui_gone.load() ? 1 : 0);
            gb_set_host_hooks(nullptr);
            gb_term_remove();
            g_log.close();
            return 0;
        }
        rc = gb_run_game((pick == 1) ? 1 : 0);
        std::fprintf(stderr, "[gensoband] game finished (%d)\n", rc);
        if (rc != GB_BIRTH_CANCEL) {
            break;
        }
        gb_clear_savefile();
    }

    /*
     * `quit_request` で畳んだ（＝画面が起こし直すかもしれない）ときだけ、
     * 開き直す枠を `.resume` へ残す（設計 §4.2）。普通の終了では残さない
     * ——次の手起動は今までどおり枠選びの画面から始まる。
     */
    if (g_quit_requested.load() && !g_ui_gone.load()) {
        write_resume_slot();
    }
    (void)gb::lang_write_report();

    gb_set_host_hooks(nullptr);

    if (rc == GB_ERR_CORE) {
        /*
         * コアが `core()` を踏んだ＝続行不能。v1 §4.2 では `exit` ではなく
         * `fatal` を送る場面である（`exit` は「正常終了の直前」に限る）。
         */
        const std::string reason = gb::sjis_to_utf8(gb_last_error());
        send_fatal(std::string("the core aborted: ") + (reason.empty() ? "(unknown)" : reason));
        g_log.close();
        return 1;
    }

    /*
     * `exit` の `code` が終了理由の正である（v1 §4.2）。
     * ui が消えて畳んだときだけ非 0。プレイヤが自分で終えたときは 0。
     */
    const int code = g_ui_gone.load() ? 1 : 0;
    send_exit_once(code);
    g_log.close();
    return 0;
}

} // namespace

#if !defined(_WIN32)

/*!
 * @brief dlsym 用の C 入口（Android の多コア形）。
 *
 * @details 画面（`libmain.so`）はコア選択で選ばれた `.so` からこの名前を引く
 * （`platform/android/hd2d_entry_android.cpp` の resolver）。名前は 4 コアで共通である
 * ——**同時に開くのは 1 つ**（`RTLD_LOCAL` で開き、選ばれたコアだけを起こす）ので
 * ぶつからない。変愚の `platform/windows/core_main.cpp` の同名の包みと対になる。
 *
 * `--savefile=` は**受けない**。あれは検証・自動運転のための手段で、Android には
 * 起動引数そのものが無い（`hd2d_entry_android.cpp` の註）。枠選びの画面が出る。
 */
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    RunArgs args;
    args.log_path = protocol_log_path;
    /*
     * 画面が転送してくる引数から、こちらが知っている物だけ拾う（知らない物は
     * 従来どおり捨てる）。`--resume` は言語切り替えの立て直しの回に付く
     * （`hd2d/app/hd2d_app.cpp` の `forwarded_args`。設計 §4.2）。
     */
    for (const std::string &opt : forwarded_args) {
        if (opt == kResumeArg) {
            args.resume = true;
        } else if (opt.rfind(kLangArg, 0) == 0) {
            args.lang = opt.substr(kLangArg.size());
        } else if (opt.rfind(kReportUntranslatedArg, 0) == 0) {
            args.report_path = opt.substr(kReportUntranslatedArg.size());
        }
    }
    return run_protocol(in, out, args);
}

#endif /* !_WIN32 */

int main(int argc, char **argv)
{
#if defined(HENGBAND_DESKTOP_LINUX)
    std::signal(SIGPIPE, SIG_IGN);
#endif
    bool selftest = false;
    bool has_protocol_arg = false;
    RunArgs args;

    for (int i = 1; i < argc; i++) {
        const std::string_view opt = argv[i];
        if (opt == "--selftest") {
            selftest = true;
            continue;
        }
        if (opt == kUiProtocolArg) {
            has_protocol_arg = true;
            continue;
        }
        if (opt.rfind(kProtocolLogArg, 0) == 0) {
            args.log_path = std::string(opt.substr(kProtocolLogArg.size()));
            continue;
        }
        if (opt.rfind(kSavefileArg, 0) == 0) {
            args.savefile_slot = std::string(opt.substr(kSavefileArg.size()));
            continue;
        }
        if (opt.rfind(kLangArg, 0) == 0) {
            args.lang = std::string(opt.substr(kLangArg.size()));
            continue;
        }
        if (opt.rfind(kReportUntranslatedArg, 0) == 0) {
            args.report_path = std::string(opt.substr(kReportUntranslatedArg.size()));
            continue;
        }
        if (opt == kResumeArg) {
            args.resume = true;
            continue;
        }

        std::fprintf(stderr, "[gensoband] unknown argument: %s\n", argv[i]);
        print_usage();
        return 1;
    }

    if (selftest) {
        return run_selftest();
    }
    if (!has_protocol_arg) {
        print_usage();
        return 2;
    }

    // 長さ枠が改行変換で壊れるのを止める。プロトコルを 1 バイトでも流す前に行う。
    presentation::set_stdio_binary_mode();

    return run_protocol(presentation::transport_stdin(), presentation::transport_stdout(), args);
}
