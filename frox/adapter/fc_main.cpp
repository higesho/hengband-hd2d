/*!
 * @file fc_main.cpp
 * @brief `FroxCore.exe` の入口（プロトコル v1 の core 側）。**P6 本実装**。
 *
 * 基準は Frox コアの設計（§3 / §3.6 起動の順序 / §8 P6 の受け入れ）と、
 * プロトコル v1 の仕様。
 *
 * ## この TU の立ち位置
 * `gensoband/adapter/gb_main.cpp` の弟である。受信スレッド 1 本・送信 mutex・
 * stdout はプロトコル専用・stderr がログ、という骨格をそのまま写した。
 *
 * ## 幻想蛮怒版との違い
 * - **起動の順序が §3.6**（M1 を見越して最初からこの形）: Term → 目録 → 握手 →
 *   受信スレッド → **最初の `ui_state` を待つ（上限 2000 ms）** → 言語の確定
 *   （M0 は読み捨てて常に英語） → `fc_bootstrap()` → 以降は同じ
 * - **文字コード変換が無い**（M0 は純 ASCII）。非 ASCII のキーは数えて捨てる
 * - **音が無い**（Frox に `TERM_XTRA_MUSIC_*` が無く、効果音は設計 §10 で後段）
 * - タイトルとセーブ選択は幻想蛮怒と同じくアダプタが描く（§3.3。**文言は英語**）
 *
 * ## Frox のヘッダは見ない
 * ここは C++ の TU なので、触ってよいのは `fc_shim.h`（純 C ABI）だけ（設計 §3）。
 * `angband.h` を include してはいけない。
 *
 * ## stdout / stderr
 * stdout は**プロトコル専用**。人間向けの出力は全部 stderr（v1 §1.1）。
 * **プロトコルへ出す文字列リテラルは ASCII に限る**（設計 §3.1）。
 */

#include "fc_shim.h"

#include "fc_frame.h"
#include "fc_lang.h" //!< 訳文カタログ（設計 §5）
#include "fc_lang_c.h" //!< 日本語層の C の皮（追補 A1）
#include "fc_manifest.h"
#include "fc_text.h" //!< 線（UTF-8）→ コア（CP932）の復号器（追補 A1）
#include "fc_menu.h" //!< `--selftest` の個数の綴り（FH-15）
#include "fc_pad_commands.h"
#include "fc_sub_panels.h"

#include "bridge/input_event_adapter.h"
#include "frame/frame_codec.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"

#include "portable/legacy_os.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

/*! 主 Term の大きさ。80x27（`note()` が 23 行目に書くので 24 行未満は不可）。 */
constexpr int kTermCols = 80;
constexpr int kTermRows = 27;

//! v1 §1.1 の起動引数。これが無い core は「単体で動く従来版」を名乗る立場に無い。
constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
//! v1 §11.2 の会話ログ。既定 OFF。
constexpr std::string_view kProtocolLogArg = "--protocol-log=";
//! セーブ枠の指定（検証・自動運転のオプション）。無ければタイトルとセーブ選択が出る。
constexpr std::string_view kSavefileArg = "--savefile=";
//! lib の置き場の差し替え（検証用）。
constexpr std::string_view kLibArg = "--lib=";
//! 言語。無ければ最初の `ui_state` の申告に従う（設計 §3.6・§3.3）。
constexpr std::string_view kLangArg = "--lang=";
//! 言語切り替えの立て直し（追補 A2。設計 §9・§8.25）。詳細は `fc_shim.h` の `fc_resume_take()`。
constexpr std::string_view kResumeArg = "--resume";
//! 引けなかった鍵の書き出し先（J4 の「出た順に埋める」を回す道具。設計 §8.2）。
constexpr std::string_view kReportUntranslatedArg = "--report-untranslated=";
//! 訳文カタログの置き場の差し替え（**検査用**。既定は `<exe_dir>/frox/lang/<lang>`）。
constexpr std::string_view kLangDirArg = "--lang-dir=";
//! 既定のセーブ枠。
constexpr const char *kDefaultSlot = "player";
//! 前回遊んだ枠の地形の記憶。**アダプタの持ち物**（コアのセーブ形式には触らない）。
constexpr const char *kLastSlotFile = "last_slot.txt";
//! 最初の `ui_state` を待つ上限（設計 §3.6 の 5）。
constexpr int kUiStateWaitMs = 2000;

//! `--report-untranslated=` の書き先。空なら記録しない。`main()` が置く。
std::string g_untranslated_report;

/*!
 * @brief `--lang-dir=` の差し替え先。空なら `frox_lang_dir(lang)`。
 * @details **検査用の口である。** 訳を 1 件も配らない段（J2）でも
 * 「日本語がコアを通って画面まで出るか」を機械で押さえられるようにするため、
 * 使い捨てのカタログを指させる（`fc_protocol_driver.py j2`）。
 */
std::string g_lang_dir_override;

/* ============================================================ 会話ログ（v1 §11.2） */

/*!
 * @brief 全送受信メッセージを JSON Lines で残す。`gb_main.cpp` の写し。
 * @details 1 行 = `{"time","dir","t","len","payload"}`。**`frame` も全文残す**。
 */
class ProtocolLog {
public:
    void open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb");
        if (this->fp_ == nullptr) {
            std::fprintf(stderr, "[frox] could not open the protocol log: %s\n", path.c_str());
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
        line += portable::iso_utc_now();
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

//! 受信キュー。**M0 は ASCII だけ**を積む（非 ASCII は数えて捨てる）。
std::mutex g_inbox_mutex;
std::deque<int> g_key_queue;
//! 捨てた非 ASCII キーの数（終了時に stderr へ言う。黙って握り潰さない）。
unsigned long g_dropped_keys = 0;
//! 線（UTF-8）→ コア（CP932）。**ステートフル**なので 1 つを持ち回す（追補 A1）。
fc::FcKeyDecoder g_key_decoder;

//! 直近の `ui_state`。視界の大きさとカメラの追従に効く（v1 §7）。
presentation::UiStateMessage g_ui_state;
bool g_ui_state_dirty = false;
//! 最初の `ui_state` が来たか（設計 §3.6 の 5。M0 は lang を読み捨てるだけ）。
std::atomic<bool> g_first_ui_state_seen{ false };
std::string g_first_ui_state_lang;

bool send_message(const std::string &type, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    if (g_out == nullptr) {
        return false;
    }
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[frox] send \"%s\" failed: %s\n", type.c_str(), err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    g_log.record("out", type, payload);
    return true;
}

//! `fatal` を送って（送れる状態なら）理由を stderr にも残す。v1 §9.3。
void send_fatal(const std::string &reason)
{
    std::fprintf(stderr, "[frox] fatal: %s\n", reason.c_str());
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
 * @brief 線から来たキーを**内部コードへ直して**キューへ積む（追補 A1）。
 *
 * @details 線の上は UTF-8、コアは CP932。**変換はここ 1 か所**（設計 §3.1）である。
 *
 * **2026-08-27 まではここで 0x80 以上を捨てていた。** 捨てているかぎり日本語は
 * 1 文字も入らないので、名前も銘も地形の記憶も英語しか打てなかった。
 *
 * 復号器（`FcKeyDecoder`）が**ステートフル**なのは、UTF-8 の塊が
 * **メッセージをまたいで割れる**からである（`fc_text.h` の註記）。
 */
void enqueue_keys(const std::vector<int> &raw)
{
    std::vector<int> converted;
    converted.reserve(raw.size());

    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (!fc_lang_enabled()) {
        /* **英語では M0 と同じ**（設計 §1 制約 1）。0x80 以上は数えて捨てる。 */
        for (const int key : raw) {
            if ((key <= 0) || (key >= 0x80)) {
                if (key >= 0x80) {
                    ++g_dropped_keys;
                }
                continue;
            }
            g_key_queue.push_back(key);
        }
        return;
    }
    g_key_decoder.feed(raw, converted);
    for (const int key : converted) {
        if (key <= 0) {
            ++g_dropped_keys;
            continue;
        }
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
                std::fprintf(stderr, "[frox] receive stopped: %s%s%s\n",
                    presentation::transport_result_name(result), err.empty() ? "" : " - ", err.c_str());
            } else {
                std::fprintf(stderr, "[frox] stdin closed (the ui is gone)\n");
            }
            g_ui_gone.store(true);
            return;
        }

        const std::string type = presentation::peek_message_type(payload);
        g_log.record("in", type, payload);

        if (type == "keys") {
            presentation::KeysMessage message;
            if (!presentation::decode_keys(payload, message, err)) {
                std::fprintf(stderr, "[frox] bad \"keys\": %s\n", err.c_str());
                continue;
            }
            enqueue_keys(message.keys);
            continue;
        }
        if (type == "input_event") {
            /*
             * v1 §8.3。**画面側（hd2d）が実際に送ってくるのはこちら**である。
             * 展開器は変愚と**同じ TU をそのまま**使う（コアに依らない）。
             */
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) {
                std::fprintf(stderr, "[frox] bad \"input_event\": %s\n", err.c_str());
                continue;
            }
            std::vector<int> keys;
            presentation::append_input_event_keys(message, keys);
            enqueue_keys(keys);
            continue;
        }
        if (type == "ui_state") {
            presentation::UiStateMessage message;
            if (!presentation::decode_ui_state(payload, message, err)) {
                std::fprintf(stderr, "[frox] bad \"ui_state\": %s\n", err.c_str());
                continue;
            }
            /*
             * push-latest（v1 §7）。**適用はゲームスレッド**（`host_present`）で行う。
             * ここで `fc::set_view_size()` を直に呼ぶと、地図を切り出している最中に
             * 窓の大きさが変わりうる。
             */
            {
                std::lock_guard<std::mutex> lock(g_inbox_mutex);
                if (!g_first_ui_state_seen.load()) {
                    g_first_ui_state_lang = message.lang;
                }
                g_ui_state = std::move(message);
                g_ui_state_dirty = true;
            }
            g_first_ui_state_seen.store(true);
            continue;
        }
        if (type == "quit_request") {
            g_quit_requested.store(true);
            std::fprintf(stderr, "[frox] quit_request received\n");
            continue;
        }
        // 未知の種別は黙って捨てる（v1 §2.3）。
    }
}

/* ================================================================ 合体規則（§6.4） */

/*!
 * @brief 合体判定に使う鍵（＝`frame_id` を抜いたペイロード）。
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

//! 表を送ったか／送ったときの版（切替を見張って送り直す。v1 §8）。
bool g_tables_sent = false;
int g_last_rogue_like = -1;
//! 表を送ったときの `fc_pad_table_stamp()`（名札は職と広域マップで変わる）。
int g_last_pad_stamp = -1;

//! タイル目録（設計 §4.2）。起動時に 1 回読んで、以後 `fc_frame` が索引に使う。
fc::TileManifest g_manifest;

void send_pad_commands()
{
    (void)send_message("pad_commands", presentation::encode_pad_commands(fc::build_pad_commands()));
    g_last_rogue_like = fc_rogue_like_commands();
    g_last_pad_stamp = fc_pad_table_stamp();
}

/*!
 * @brief 初期化完了時の表（v1 §8 / §8.1）。
 * @details `sub_panel_kinds` はコアの `window_flag_desc` ＋矢筒から組む。
 * `asset_manifest` は terrain_map.csv から組んだ目録そのもの。
 */
void send_startup_tables()
{
    if (g_tables_sent) {
        return;
    }
    g_tables_sent = true;
    send_pad_commands();
    (void)send_message(
        "sub_panel_kinds", presentation::encode_sub_panel_kinds(fc::build_sub_panel_kinds()));
    if (!g_manifest.message().assets.empty()) {
        (void)send_message("asset_manifest", presentation::encode_asset_manifest(g_manifest.message()));
    }
}

/* ==================================================== ホストのフック（v1 §5 の対応） */

//! 直近の安い digest（`fc_change_digest`）。capture を丸ごと省くための門。
unsigned long long g_last_digest = 0;
bool g_has_last_digest = false;
//! 地形の記憶を書いたか（`last_slot.txt`）。@ が出来た最初の 1 回だけ書く。
bool g_slot_remembered = false;
//! @ が出来た直後の 1 回きりの仕込み（サブパネルの当て直し）を済ませたか。
bool g_post_birth_primed = false;

//! セーブの置き場に「前回遊んだ枠」を残す。次の起動が続きから始められるように。
void remember_slot_once()
{
    if (g_slot_remembered || !fc_character_generated()) {
        return;
    }
    g_slot_remembered = true;

    const std::string dir = fc_save_dir();
    const std::string base = fc_savefile_base();
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
    std::fprintf(stderr, "[frox] remembered save slot \"%s\"\n", base.c_str());
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
    fc::set_view_size(state.view_w, state.view_h);
    fc::set_camera_follow_player(state.camera_follow_player);
    fc::set_cursor_mode(state.cursor_mode);
    /*
     * **効果音を誰が鳴らすか**。Frox は自分では鳴らせない
     * （winmm を叩く実体を持たない）ので、真なら `frame.sounds` へ書き留め、偽なら
     * 何もしない＝無音になる。**`use_sound` は落とさない**——コアの `sound()` は
     * あれが偽だと `Term_xtra` すら呼ばず、書き留める機会ごと消える
     * （`fc_bootstrap.c` で立てたまま）。
     */
    fc_sound_set_wanted(state.sound_events ? 1 : 0);
    /*
     * サブパネルの希望（v1 §7）。`has_sub_panel_kinds` が偽なら種類は触らない
     * ——キーごと省かれたときに「全部 UI 既定」で塗り潰さないため。
     */
    for (int i = 0; i < presentation::kProtocolSubPanelCount; ++i) {
        fc::set_sub_panel_cells(i, state.sub_panel_cells[i].cols, state.sub_panel_cells[i].rows);
        if (state.has_sub_panel_kinds) {
            fc::set_sub_panel_kind(i, state.sub_panel_kinds[i]);
        }
    }
    return true;
}

void host_present()
{
    /*
     * @ が出来た（または読み込めた）直後に **1 度だけ**、サブパネルの割り当てを
     * 当て直す。**セーブの読み込みが `window_flag[]` を丸ごと書き戻した後**なので、
     * ここで戻さないと画面側の割り当てが起動のたびにセーブの値に負ける
     * （`fc_sub_panels.h`。Sil-Q で実測した穴と同じ）。
     */
    if (!g_post_birth_primed && (fc_character_generated() != 0)) {
        g_post_birth_primed = true;
        fc::reset_sub_panel_apply();
    }

    /*
     * **安い門**。`host_present` は待ちの間も 10ms ごとに回るので、変わっていない
     * フレームを毎回組み直して比べるのは割に合わない。Term の画・`game_turn`・
     * @ の状態から digest を採り、変わっていなければ capture ごと省く。
     * 下の払い出し済みペイロード比較（v1 §6.4）は**そのまま残す**——digest は
     * 衝突しうるが、こちらは厳密だから。
     */
    const bool view_changed = apply_ui_state();
    /*
     * **カーソル選択は門より手前で当てる。** `request_command()` はコマンドを
     * 1 つ受けるたびに `use_menu` を落とすので、「画が変わらないので capture を
     * 省いた」フレームでも立て直しが要る。費用は代入 2 つ。
     */
    fc::apply_cursor_mode();
    const unsigned long long digest = fc_change_digest();
    if (g_has_last_digest && !view_changed && (digest == g_last_digest)) {
        return;
    }
    g_last_digest = digest;
    g_has_last_digest = true;

    remember_slot_once();

    const GameFrame frame = fc::capture_frame(++g_frame_id);
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
        && ((fc_rogue_like_commands() != g_last_rogue_like) || (fc_pad_table_stamp() != g_last_pad_stamp))) {
        send_pad_commands();
    }
}

int host_next_key()
{
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (g_key_queue.empty()) {
        return 0;
    }
    const int key = g_key_queue.front();
    g_key_queue.pop_front();
    return key;
}

void host_drop_keys()
{
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (!g_key_queue.empty()) {
        std::fprintf(stderr, "[frox] FLUSH drops %u key(s)\n",
            static_cast<unsigned>(g_key_queue.size()));
        g_key_queue.clear();
    }
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
 * @brief exe のあるディレクトリ（末尾に区切りは付かない）。
 * @details cwd には依らない。`HengbandHd2d.exe` が子として起こす都合上、
 * 作業ディレクトリが何であっても lib を見つけられなければならない。
 */
std::string exe_dir()
{
    return portable::exe_dir();
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
 * @brief Frox の lib の場所。`<exe_dir>/frox/lib`（設計 §2）。
 * @details **変愚の `lib/` とは交差しない**（設計 §1 制約 2）。
 */
std::string frox_lib_dir()
{
    return under_exe("frox", "lib");
}

/*!
 * @brief 訳文カタログの原本の置き場（`<exe_dir>/frox/lang/<lang>`。設計 §2）。
 * @details **`lib` の下ではない**——`frox/lang/ja` は人が書いて git で読む原本で、
 * `frox/lib-ja` のほうが作成物である（設計 §2 の「追跡するもの」）。
 */
std::string frox_lang_dir(const std::string &lang)
{
    return under_exe("frox", "lang", lang.c_str());
}

//! 地形の名寄せ表（設計 §4.2）。
std::string terrain_map_csv_path()
{
    return under_exe("frox", "tilework", "terrain_map.csv");
}

//! 実体 → 絵の正。
std::string mapping_csv_path()
{
    return under_exe("frox", "tilework", "mapping.csv");
}

/*!
 * @brief 作った板の置き場。`hello_ack.asset_roots.slab` に載せる。
 * @details 画面側の `SlabLibrary` は［ここ → 既定の置き場］の 2 段で板を探す。
 * 変愚の絵を流用する実体の板（`R955` など）は既定側で見つかり、Frox 固有の板
 * （`FR19` など）はここで見つかる。**頭の `F` はこの 2 段のためにある**
 * ——素の `R955` で置くと、変愚の絵を借りている別の実体の板を横取りする。
 */
std::string frox_slab_dir()
{
    return under_exe("frox", "assets", "slab");
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
 * @details 新規作成では**先方が誕生でセーブ名を決める**（`process_player_name`）ので、
 * こちらが決めた枠は使われない。その名を覚えておかないと次の起動で続きから遊べない。
 */
std::string read_remembered_slot()
{
    const std::string dir = fc_save_dir();
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

/*!
 * @brief セーブ枠を決めて `savefile` を据える。
 * @param requested `--savefile=` の値（空なら覚えている枠 → 既定 `player`）
 * @return 真なら**新規作成**（枠の実体が無い）、偽ならロード
 *
 * @details **`play_game(FALSE)` を「無いセーブ」に対して呼んではいけない**
 * （幻想蛮怒の P3 で照合した穴と同じ系譜——Windows では読み込みの前検査が外れて
 * いて、無いセーブを開こうとすると `quit()` を踏む）。「在ればロード・無ければ
 * 新規」は、**呼ぶ側が在るかを見て引数を決める**形でしか成立しない。
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

    const int rc = fc_set_savefile(slot.c_str());
    if (rc != FC_OK) {
        std::fprintf(stderr, "[frox] could not set the save slot \"%s\" (%d); falling back to a new game\n",
            slot.c_str(), rc);
        fc_clear_savefile();
        return true;
    }

    if (path_is_file(fc_savefile_path())) {
        std::fprintf(stderr, "[frox] save slot \"%s\" -> %s (load)\n", slot.c_str(), fc_savefile_path());
        return false;
    }

    /* 無い。**名を外してから**新規で始める。名は誕生でプレイヤが付ける。 */
    std::fprintf(stderr, "[frox] no save at %s; starting a new game\n", fc_savefile_path());
    fc_clear_savefile();
    return true;
}

/* ========================================================= タイトルとセーブ選択（§3.3） */

/*!
 * @brief セーブ枠の一覧（`fc_save_dir()` 直下の、名前に `.` を含まないファイル）。
 * @details `.` 持ちを外すのは `last_slot.txt` のような道具類を隠すため。
 */
std::vector<std::string> list_save_slots()
{
    std::vector<std::string> slots;
    std::string dir = fc_save_dir();
    if (dir.empty()) {
        return slots;
    }
    if ((dir.back() != '\\') && (dir.back() != '/')) {
        dir.push_back(portable::kPathSep);
    }
    for (const std::string &name : portable::list_files(dir)) {
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
 * @return キー、または **-1**（終了要求）
 * @details 待ちの間も `host_present()` を回す——`ui_state` の適用と、
 * 送り漏れの補償はあちらの安い門（digest）が面倒を見る。
 */
int wait_key_for_menu()
{
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            if (!g_key_queue.empty()) {
                const int key = g_key_queue.front();
                g_key_queue.pop_front();
                return key;
            }
        }
        if (g_ui_gone.load() || g_quit_requested.load()) {
            return -1;
        }
        host_present();
        portable::sleep_ms(20);
    }
}

/*!
 * @brief タイトル（`frox/lib/file/news.txt`）とセーブ選択を出し、枠を決める。
 * @return 0 = 選んだ枠をロード（`savefile` 据え済み）/ 1 = 新規 / -1 = 終了要求
 *
 * @details 先方にタイトルメニューは無い（`main-win.c` の Win32 メニュー直結。
 * 設計 §3.3）ので、news の画面へ letter 式の一覧を重ねる形を**アダプタが持つ**。
 * コアには 1 行も足していない。`--savefile=` 指定時はここへ来ない。
 *
 * 画面は 80×27: 上 21 行が news そのまま・下 6 行が選択。letter は a〜i の
 * 9 件まで。超えたぶんは見出しに件数だけ正直に書く（隠れた枠は `--savefile=` で開ける）。
 * **文言は英語**（M0。ASCII なので span の位置＝桁位置で換算が要らない）。
 */
int title_and_pick_savefile()
{
    const std::vector<std::string> slots = list_save_slots();
    const std::string last = read_remembered_slot();
    constexpr int kMaxListed = 9;
    const int listed = std::min<int>(static_cast<int>(slots.size()), kMaxListed);
    const int hidden = static_cast<int>(slots.size()) - listed;

    fc_term_clear();

    /* --- タイトル（news.txt そのまま）。無ければ題字 1 行で代える。 --- */
    {
        const std::string path = frox_lib_dir() + portable::kPathSep + "file"
            + portable::kPathSep + "news.txt";
        std::FILE *fp = std::fopen(path.c_str(), "rb");
        if (fp != nullptr) {
            /*
             * **doc のタグを剥がす。** Frox の news.txt は `<color:G>`・`<style:screenshot>`・
             * `<$:intro_version>` の印を含み、あちらでは `z-doc.c` が解釈する。
             * 当方は Term に素で書くので、剥がさないとタグの字面がそのまま画面に出る
             * （P6 の握手検査で実際に出た）。色は 1 色に落ちるが、読めることのほうが大事。
             *
             * - **受け皿は 512**——最長の行は 304 バイトある（タグ込み。実測）。
             *   256 で切るとタグが行の途中で割れ、残り半分（`:G>`）が画面に出る。
             * - 剥がすのは **`<` と `>` の間に `:` があるもの**だけ。ASCII アートの
             *   `<`（上り階段）や `>` は独立した字なので巻き込まれない。
             */
            char line[512];
            int row = 0;
            while ((row < (kTermRows - 6)) && (std::fgets(line, sizeof(line), fp) != nullptr)) {
                std::size_t n = std::strlen(line);
                while ((n > 0) && ((line[n - 1] == '\n') || (line[n - 1] == '\r'))) {
                    line[--n] = '\0';
                }
                std::string plain;
                plain.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    if (line[i] == '<') {
                        const char *close = std::strchr(line + i + 1, '>');
                        if (close != nullptr) {
                            const char *colon = std::strchr(line + i + 1, ':');
                            if ((colon != nullptr) && (colon < close)) {
                                i = static_cast<std::size_t>(close - line);
                                continue;
                            }
                        }
                    }
                    plain.push_back(line[i]);
                }
                fc_term_putstr(2, row, 1, plain.c_str());
                ++row;
            }
            std::fclose(fp);
        } else {
            std::fprintf(stderr, "[frox] no title art at %s\n", path.c_str());
            fc_term_putstr(30, 2, 1, "FroxComposband");
        }
    }

    /* --- セーブ選択（下 6 行: 見出し・一覧 3 行・新規）。 --- */
    std::string head = "Which save do you want to play? (* = last)";
    if (hidden > 0) {
        head += " (" + std::to_string(hidden) + " more via --savefile=)";
    }
    fc_term_putstr(6, kTermRows - 5, 11, head.c_str());
    if (slots.empty()) {
        fc_term_putstr(8, kTermRows - 4, 2, "(no saves yet)");
    }
    /*
     * 選択肢は**画面側のカーソル層にも宣言する**（`frame.menu_choices`）。
     * 矢印・パッド・クリックは hd2d の既存実装が letter へ翻訳して送ってくる
     * ——コア側の受けは従来の letter のまま。M0 は ASCII なので span の位置は
     * そのまま桁位置である。
     */
    std::vector<MenuChoice> choices;
    for (int row = 0; row < 3; ++row) {
        std::string buf; //!< この Term 行の全体。1 回で描く
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
            mc.span_begin = static_cast<int>(begin);
            mc.span_len = static_cast<int>(buf.size() - begin);
            mc.key_begin = mc.span_begin;
            mc.key_len = 1;
            /*
             * **前回の枠だけ key を `\r`（Enter）にする。**カーソル層の初期位置が
             * ここへ来て、Enter／A ボタン一発で続きから遊べる。**letter の選択肢を
             * 同じ span に重ねてはいけない**——choices に 2 つ並ぶと左右移動が
             * 足踏みする（幻想蛮怒の P5 で気づいた穴）。
             */
            mc.key = is_last ? '\r' : ('a' + i);
            choices.push_back(mc);
        }
        if (!buf.empty()) {
            fc_term_putstr(0, (kTermRows - 4) + row, 1, buf.c_str());
        }
    }
    {
        const char *const tail = "n) Start a new game";
        MenuChoice mc{};
        mc.line_index = kTermRows - 1;
        mc.span_begin = 8;
        mc.span_len = static_cast<int>(std::strlen(tail));
        mc.key_begin = 8;
        mc.key_len = 1;
        mc.key = 'n';
        choices.push_back(mc);
        fc_term_putstr(8, kTermRows - 1, 14, tail);
    }
    fc::set_pregame_choices(choices);
    fc_term_present();

    for (;;) {
        const int key = wait_key_for_menu();
        if (key < 0) {
            fc::set_pregame_choices({});
            return -1;
        }
        if ((key == 'n') || (key == 'N')) {
            fc::set_pregame_choices({});
            fc_clear_savefile();
            return 1;
        }
        if ((key == '\r') && !last.empty()) {
            /* Enter = 前回の枠（カーソル層の初期位置と同じ意味。無ければ何もしない）。 */
            for (int i = 0; i < listed; ++i) {
                if (slots[static_cast<std::size_t>(i)] == last) {
                    if (!choose_savefile(last)) {
                        fc::set_pregame_choices({});
                        return 0;
                    }
                    break;
                }
            }
        }
        if ((key >= 'a') && (key < ('a' + listed))) {
            const std::string &slot = slots[static_cast<std::size_t>(key - 'a')];
            fc::set_pregame_choices({});
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
        "FroxCore (%s %s) - protocol v%d, stdio transport\n"
        "\n"
        "  usage: FroxCore.exe %s [--protocol-log=<path>] [--savefile=<slot>]\n"
        "                       [--lib=<dir>] [--lang=ja|en] [--resume]\n"
        "                       [--report-untranslated=<path>] [--lang-dir=<dir>]\n"
        "         FroxCore.exe --selftest [--lib=<dir>]\n"
        "\n"
        "  --selftest   read lib, run init_angband() and cross-check the terrain map.\n"
        "  --savefile=  use frox/lib/save/<slot>. load if present, else a new game.\n"
        "               without it the title and save picker appear.\n"
        "  --lib=       use this lib directory (default: <exe dir>\\frox\\lib).\n"
        "  --lang=      core language. without it the first ui_state decides.\n"
        "  --resume     open the character the previous run saved and skip the\n"
        "               title. the frontend passes this when it restarts the\n"
        "               core to change language (see fc_shim.h, fc_resume_take).\n"
        "  --report-untranslated=  write the keys the catalog could not answer,\n"
        "               ready to paste into frox/lang/ja/ui/messages.ja.txt.\n"
        "  --lang-dir=  read the catalog from here instead of frox/lang/<lang>.\n"
        "               for tests only; it does not move lib-ja.\n"
        "\n"
        "This executable is not meant to be started by hand. The frontend\n"
        "launches it as a child process and speaks the core protocol over\n"
        "stdin/stdout.\n",
        fc_core_name(), fc_core_version(), presentation::kProtocolVersion, kUiProtocolArg.data());
    std::fflush(stderr);
}

/*!
 * @brief `--selftest`。起動列を通し、**地形の名寄せ表を全数照合する**（設計 §9 の 4）。
 * @return プロセスの終了コード
 *
 * **フックを差さない**ので、null term は「ESC を積むだけ」の逃げ道で回る。
 * パイプもプロトコルも要らない。
 */
int run_selftest(const std::string &lib_dir)
{
    std::fprintf(stderr, "[frox] selftest: core=%s version=%s\n", fc_core_name(), fc_core_version());
    std::fprintf(stderr, "[frox] selftest: lib=%s\n", lib_dir.c_str());

    int rc = fc_term_install(kTermCols, kTermRows);
    if (rc != FC_OK) {
        std::fprintf(stderr, "[frox] selftest: fc_term_install failed (%d)\n", rc);
        return 1;
    }

    rc = fc_bootstrap(lib_dir.c_str(), "en");
    if (rc != FC_OK) {
        std::fprintf(stderr, "[frox] selftest: fc_bootstrap failed (%d): %s\n", rc, fc_last_error());
        fc_term_remove();
        return 1;
    }

    int failures = 0;

    int limits[6] = { 0 };
    if (fc_read_limits(limits, 6) != 6) {
        std::fprintf(stderr, "[frox] selftest: FAIL - fc_read_limits\n");
        failures++;
    } else {
        std::fprintf(stderr,
            "[frox] selftest: max_r_idx=%d max_k_idx=%d max_f_idx=%d "
            "max_a_idx=%d max_e_idx=%d max_d_idx=%d\n",
            limits[0], limits[1], limits[2], limits[3], limits[4], limits[5]);
        if (limits[0] < 2 || limits[2] < 2) {
            std::fprintf(stderr, "[frox] selftest: FAIL - lib/edit did not load\n");
            failures++;
        }
    }

    /*
     * **地形の名寄せ表の全数照合**（設計 §4.2・§9 の 4）。
     * f_info に定義のある feat 全部に表の行が在ることを見る。上流が地形を足すと
     * ここで捕まる——「黙って別の絵になる」を止めるのがこの検査の役目である。
     */
    {
        const std::string csv = terrain_map_csv_path();
        fc::TileManifest manifest;
        if (!manifest.load(csv, exe_dir())) {
            std::fprintf(stderr, "[frox] selftest: FAIL - terrain map: %s\n", manifest.error().c_str());
            failures++;
        } else {
            int defined = 0;
            int missing = 0;
            for (int feat = 0; feat < ((limits[2] > 0) ? limits[2] : 255); ++feat) {
                if (fc_terrain_defined(feat) != 1) {
                    continue;
                }
                ++defined;
                if (manifest.lookup_terrain(feat) == 0) {
                    std::fprintf(stderr, "[frox] selftest: FAIL - feat %d has no terrain_map row\n", feat);
                    ++missing;
                }
            }
            std::fprintf(stderr,
                "[frox] selftest: terrain map: %d rows, %d defined feats, %d missing, %d png missing\n",
                manifest.terrain_count(), defined, missing, manifest.missing_files());
            if ((missing > 0) || (manifest.missing_files() > 0)) {
                failures++;
            }
        }
    }

    /*
     * **実体の目録の全数照合**。
     * `r_info` / `k_info` に定義のある id 全部に行が在ることを見る。
     * **絵が無いのと目録に無いのは別**——絵の実体は `missing_files()` が数える。
     * 上流が実体を足すとここで捕まる。
     */
    {
        const std::string csv = terrain_map_csv_path();
        fc::TileManifest manifest;
        if (!manifest.load(csv, exe_dir())) {
            std::fprintf(stderr, "[frox] selftest: FAIL - entity map needs the terrain map first\n");
            failures++;
        } else if (!manifest.load_entities(mapping_csv_path(), exe_dir())) {
            std::fprintf(stderr, "[frox] selftest: FAIL - entity map: %s\n", manifest.error().c_str());
            failures++;
        } else {
            int want = 0;
            int missing = 0;
            /* 実体の id は疎（欠番が在る）ので、**定義のあるものだけ**数える。 */
            for (int r = 1; r < limits[0]; ++r) {
                if (fc_monster_defined(r) != 1) {
                    continue;
                }
                ++want;
                if (manifest.lookup_entity('R', r) == 0) {
                    ++missing;
                }
            }
            for (int k = 1; k < limits[1]; ++k) {
                if (fc_object_defined(k) != 1) {
                    continue;
                }
                ++want;
                if (manifest.lookup_entity('K', k) == 0) {
                    ++missing;
                }
            }
            std::fprintf(stderr,
                "[frox] selftest: entity map: %d rows, %d defined entities, %d missing, %d png missing\n",
                manifest.entity_count(), want, missing, manifest.missing_files());
            if ((missing > 0) || (manifest.missing_files() > 0)) {
                failures++;
            }
        }
    }

    /*
     * **個数入力の綴り**（FH-15）。`get_quantity()` の `(1-5): ` と
     * `msg_input_num()` の `(1 to 5): ` の両方を読めること。後者は
     * **店・我が家・建物の個数**で、そこへはボットが届かない（`fc_menu.h` の註記）。
     */
    {
        const int bad = fc::numeric_spelling_selftest();
        std::fprintf(stderr, "[frox] selftest: numeric spellings: %s\n",
            (bad == 0) ? "ok" : "FAIL");
        failures += bad;
    }

    /*
     * **命令列の札**（`[b/p/g] Buy` 形）。**訳で札の頭が日本語になっても拾えること**。
     * ここが落ちると店の帯が 1 つも札にならず、カーソルが品行へ落ちて
     * パッドから店のコマンドが選べなくなる（2026-08-28 の実機報告）。
     * 店へはボットが届かないので、numeric と同じくここで押さえる。
     */
    {
        const int bad = fc::command_label_selftest();
        std::fprintf(stderr, "[frox] selftest: command labels: %s\n",
            (bad == 0) ? "ok" : "FAIL");
        failures += bad;
    }

    /*
     * **品選びの窓が重なった画面**（フック #37）。店で `s`（売る）を押すと
     * `obj_prompt` の窓が店の上に重なり、letter の並びが 2 つ出る。
     * 上の 1 つだけを札にできること。ここへは駆動器が届きにくいうえ、
     * 間違えても札は出るので**押すまで気づけない**。
     */
    {
        const int bad = fc::obj_prompt_choice_selftest();
        std::fprintf(stderr, "[frox] selftest: obj-prompt choices: %s\n",
            (bad == 0) ? "ok" : "FAIL");
        failures += bad;
    }

    /*
     * **店の矩形での読み取り**（FH-13 / FH-15）。店は箱を `rect(0,0,80,3)` へ
     * 移すので、**68 桁目以降**に出た `[y/n]` と個数を拾えなければならない。
     * ボットは店へ入れないので、`msg_line_init()` を同じ引数で呼んで矩形だけ
     * 再現し、Term に実物どおりの行を置いて読ませる。
     */
    {
        //! 実物の文言（`shop.c:1976`）。**`[y/n]` は 68 桁目より後ろに来る長さ**。
        static const char *const kSell =
            "Really sell a Broad Sword (2d6) (+0,+0) for 15000 gold pieces? [y/n]";
        static const char *const kQty = "Quantity (1 to 21): 1";
        int bad = 0;

        fc_msg_rect_use_shop_for_test(1);
        {
            fc_term_clear();
            fc_term_putstr(0, 0, 1, kSell);
            GameFrame probe;
            fc::fill_row0_prompt(probe);
            if (probe.prompt.choices.size() < 2) {
                std::fprintf(stderr,
                    "[frox] selftest: shop rect: [y/n] not read (len=%d)\n",
                    static_cast<int>(std::strlen(kSell)));
                ++bad;
            }
        }
        {
            fc_term_clear();
            fc_term_putstr(0, 1, 1, kQty); //!< 行 0 ではない（箱の 2 行目）
            GameFrame probe;
            fc::fill_row0_prompt(probe);
            if (!probe.numeric.active || (probe.numeric.max != 21)) {
                std::fprintf(stderr, "[frox] selftest: shop rect: quantity not read\n");
                ++bad;
            }
        }
        fc_term_clear();
        fc_msg_rect_use_shop_for_test(0);

        std::fprintf(stderr, "[frox] selftest: shop-rect prompts: %s\n",
            (bad == 0) ? "ok" : "FAIL");
        failures += bad;
    }

    /*
     * **日本語層**（M1 の J2。設計 §11 の 5）。カタログの判断・バイト算・
     * ドキュメントの単語切りを、**訳を 1 件も配らずに**押さえる。
     * @warning 日本語層の入切を触るので、**いちばん最後**に置く（終わりに英語へ戻す）。
     */
    {
        const int bad = fc::lang_selftest();
        std::fprintf(stderr, "[frox] selftest: japanese layer: %s\n",
            (bad == 0) ? "ok" : "FAIL");
        failures += bad;
    }

    //! Term のミラーが取れるか（フレームがここから作られる）。
    char row[256] = { 0 };
    const int n = fc_term_row(0, row, nullptr, static_cast<int>(sizeof(row)));
    if (n <= 0) {
        std::fprintf(stderr, "[frox] selftest: FAIL - fc_term_row (%d)\n", n);
        failures++;
    }

    fc_term_remove();

    if (failures) {
        std::fprintf(stderr, "[frox] selftest: FAIL (%d)\n", failures);
        std::fflush(stderr);
        return 1;
    }
    std::fprintf(stderr, "[frox] selftest: PASS\n");
    std::fflush(stderr);
    return 0;
}

/* ==================================================================== 本体（P6） */

/*!
 * @brief プロトコルを回す本体。**輸送路は呼び手が決める。**
 * @details Windows は `main()` が標準入出力を渡す。Android はコアが同じプロセスの
 * 別スレッドなので、入口（`hengband_core_entry`）が `pipe()` の両端を渡す
 * （**M0 の対象外**。設計 §10）。ここから下はどちらでも同じ道を通る。
 */
int run_protocol(presentation::TransportHandle in, presentation::TransportHandle out,
    const std::string &log_path, const std::string &lib_dir, const std::string &lang_arg,
    const std::string &savefile_slot, bool resume)
{
    g_in = in;
    g_out = out;
    if ((g_in == nullptr) || (g_out == nullptr)) {
        std::fprintf(stderr, "[frox] no transport endpoint; cannot speak the protocol\n");
        return 3;
    }
    if (!log_path.empty()) {
        g_log.open(log_path);
    }

    /*
     * --- 順序は設計 §3.6。**1: Term を立てる**（M1 を見越して最初からこの形）。
     * 幻想蛮怒は握手の後に立てているが、Sil-Q は言語決定の作り直しでここへ
     * 行き着いた。Frox は M1 が確定しているので
     * 最初から同じ順で作る。
     */
    int rc = fc_term_install(kTermCols, kTermRows);
    if (rc != FC_OK) {
        std::fprintf(stderr, "[frox] could not install the headless term (%d)\n", rc);
        return 1;
    }

    /* --- 2: 目録（地形の名寄せ表）を読む。`hello_ack` と対になるもの。 --- */
    const std::string csv = terrain_map_csv_path();
    if (!g_manifest.load(csv, exe_dir())) {
        /* 致命ではない——絵が出ないだけで遊べる。**捏造せずに数えて言う**。 */
        std::fprintf(stderr, "[frox] tile manifest unavailable: %s\n", g_manifest.error().c_str());
    } else {
        std::fprintf(stderr, "[frox] tile manifest: %u entries from %s (%d png missing)\n",
            static_cast<unsigned>(g_manifest.message().assets.size()), csv.c_str(),
            g_manifest.missing_files());
        fc::set_tile_manifest(&g_manifest);
        /*
         * **実体（R / K / P）を同じ目録へ継ぎ足す**（M2）。地形だけでも遊べるので、
         * 読めなくても致命ではない——**捏造せずに数えて言う**。
         */
        const std::string map_csv = mapping_csv_path();
        if (!g_manifest.load_entities(map_csv, exe_dir())) {
            std::fprintf(stderr, "[frox] entity manifest unavailable: %s\n", g_manifest.error().c_str());
        } else {
            std::fprintf(stderr, "[frox] entity manifest: %d entries from %s (%d png missing in total)\n",
                g_manifest.entity_count(), map_csv.c_str(), g_manifest.missing_files());
        }
    }

    /* --- 3: 握手（v1 §3.1）。最初のメッセージは hello でなければならない。 --- */
    std::string payload;
    std::string err;
    const presentation::TransportResult got = presentation::read_message(g_in, payload, err);
    if (got != presentation::TransportResult::Ok) {
        std::fprintf(stderr, "[frox] failed to read the first message: %s%s%s\n",
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
    std::fprintf(stderr, "[frox] hello from %s %s (protocol %d)\n",
        hello.ui_name.empty() ? "<unnamed ui>" : hello.ui_name.c_str(),
        hello.ui_version.empty() ? "?" : hello.ui_version.c_str(), hello.protocol);

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    /*
     * 版は `defines.h` の `VER_*` から組む（設計 §3.6 の 3。`Makefile.src` の
     * `VERSION` ではない——どうせ同じ値だが、写しの中の正はヘッダである）。
     */
    ack.core_name = fc_core_name();
    ack.core_version = fc_core_version();
    /*
     * `features`（v1 §3.1）。**`lang-restart` と `audio` を名乗る**（追補 A2）。
     * リアルタイム進行は変愚だけなので出さない。
     *
     * `audio` の意味は移設後に変わっている——
     * かつては「コアが自分で鳴らせる」だったが、いまは「**音の出来事を出せる**」である。
     * Frox は winmm を叩く実体を持たないが、`frame.sounds` へ名前とマスを載せるので
     * 画面側が鳴らせる。名乗らないと機能メニューの「音」の節が入口から消え、
     * 利用者が音量も入切も触れなくなる。
     *
     * `lang-restart` の意味は「**遊んでいる途中に言語を替えるなら、保存して
     * 起こし直してくれ**」である。Frox の言語は `init_angband()` より前に
     * 決まっていなければならない（`lib-ja/edit` を読ませるため。設計 §3.3）ので、
     * `ui_state.lang` を替えても途中では効かない。画面側は名乗ったコアにだけ
     * `quit_request` → `exit` → `--resume` 付きで起こし直す環を回す
     * （`hd2d_app.cpp` の (d2)。**コア名の分岐は書かれていない**）。
     *
     * **`asset_roots.slab` は M2 で出すようにした**（2026-08-27）。画面側は
     * ［申告 → 既定］の 2 段で板を探すので、Frox 固有の `FR19` などはここで、
     * 変愚から流用する `R955` などは既定側で見つかる。`graf` は出さない
     * ——8/16px の面を持たないので、画面側に該当スタイルを無効化させる。
     */
    ack.asset_root_slab = frox_slab_dir();
    ack.features.emplace_back("lang-restart");
    ack.features.emplace_back("audio");
    if (!send_message("hello_ack", presentation::encode_hello_ack(ack))) {
        return 5;
    }

    /* --- 4: 受信スレッド。detach する（join する場所が無い）。 --- */
    std::thread(receive_loop).detach();

    /*
     * --- 5: 最初の `ui_state` を待つ（上限 2000 ms）。--- 画面側は握手の直後に
     * 必ず 1 回送ってくる。**M0 では lang を読み捨てるだけ**だが、待つ形を
     * ここで作っておくのが §3.6 の眼目である（M1 で中身が入る）。
     * 待った実測を stderr に出す——「たぶんすぐ来る」で済ませない。
     */
    std::string lang = lang_arg;
    if (lang.empty()) {
        const std::uint64_t began = portable::tick_ms();
        while (!g_first_ui_state_seen.load() && !g_ui_gone.load()) {
            if ((portable::tick_ms() - began) >= static_cast<std::uint64_t>(kUiStateWaitMs)) {
                break;
            }
            portable::sleep_ms(2);
        }
        const std::uint64_t waited = portable::tick_ms() - began;
        if (g_first_ui_state_seen.load()) {
            {
                std::lock_guard<std::mutex> lock(g_inbox_mutex);
                lang = g_first_ui_state_lang;
            }
            std::fprintf(stderr, "[frox] first ui_state after %lu ms, lang = %s\n",
                static_cast<unsigned long>(waited), lang.empty() ? "(not declared)" : lang.c_str());
        } else {
            std::fprintf(stderr, "[frox] no ui_state within %lu ms\n",
                static_cast<unsigned long>(waited));
        }
        std::fflush(stderr);
    }
    /*
     * --- 6: 言語の確定。**カタログはここで読む**（設計 §3.3）。---
     *
     * `fc_bootstrap()` より前でなければならない——`init_angband()` に入った後では
     * `lib-ja/edit` へ向け直せないし、`my_fgets()` の `fc_is_text_byte()` が
     * まだ 0x80 以上を落とす側に居ると、実体データの日本語が 1 文字も入らない。
     *
     * **カタログが読めなくても英語へ落とさない**（`fc_lang.h` の註記）。
     * 名前は `lib-ja/edit` から来るので、カタログが空でも日本語は出るし、
     * ここで下げると `fc_is_text_byte()` がその日本語を空白にしてしまう。
     */
    if (lang.empty()) {
        lang = "en";
    }
    {
        const std::string lang_dir = g_lang_dir_override.empty() ? frox_lang_dir(lang)
                                                                 : g_lang_dir_override;
        const fc::LangLoadReport rep = fc::lang_init(lang, lang_dir, g_untranslated_report);
        if (rep.enabled) {
            std::fprintf(stderr,
                "[frox] japanese: %d entries, %d silent, %d rejected, %d tag mismatch, %d not in cp932\n",
                rep.entries, rep.silent, rep.rejected, rep.tag_mismatch, rep.not_in_cp932);
            if (!rep.error.empty()) {
                /* **止めない。** 訳が無ければ英語で出る（設計 §1 制約 4）。 */
                std::fprintf(stderr, "[frox] catalog: %s\n", rep.error.c_str());
            }
        }
        std::fflush(stderr);
    }

    /* --- 7: 起動列。ここから先の `Term_fresh` は frame になる。 --- */
    if (lib_dir.empty()) {
        send_fatal("cannot resolve the exe directory");
        return 1;
    }
    std::fprintf(stderr, "[frox] lib = %s\n", lib_dir.c_str());

    /*
     * サブパネル用の Term 7 枚。**本線の後・起動列の前**に立てる——
     * `init_angband()` より後だと、セーブから読んだ `window_flag` を受ける相手が
     * 居ないまま `window_stuff()` が走る（Sil-Q と同じ判断）。
     * 立てられなくても遊びは続けられる（サブパネルが空になるだけ）ので止めない。
     */
    if (fc_sub_terms_install() != FC_OK) {
        std::fprintf(stderr, "[frox] sub terms not installed (the side panels stay empty)\n");
    }

    fc_host_hooks hooks{};
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.drop_keys = host_drop_keys;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    fc_set_host_hooks(&hooks);

    rc = fc_bootstrap(lib_dir.c_str(), lang.c_str());
    if (rc != FC_OK) {
        fc_set_host_hooks(nullptr);
        /*
         * **「起動に失敗した」と「起動中に ui が消えた」を取り違えない。**
         * 後者では null term の待ちが `fc_shutdown_and_quit()` → `quit(NULL)` を通り、
         * 起動の段の跳び先に落ちて `FC_ERR_QUIT` に見える。ここで `fatal` を送ると
         * 「Frox コアが壊れている」という嘘の記録が残る。
         */
        if (g_ui_gone.load() || g_quit_requested.load()) {
            std::fprintf(stderr, "[frox] shut down during startup (the ui went away)\n");
            send_exit_once(g_ui_gone.load() ? 1 : 0);
            g_log.close();
            return 0;
        }
        const std::string reason = fc_last_error();
        send_fatal(std::string("bootstrap failed: ") + (reason.empty() ? "(unknown)" : reason));
        fc_term_remove();
        g_log.close();
        return 1;
    }

    /* --- 8: 以降は幻想蛮怒と同じ。初期化完了の表はここで 1 回（v1 §8）。 --- */
    send_startup_tables();

    /*
     * ゲームを回す。**戻ってくるのは終わったとき**。
     * `--savefile=` があれば従来どおり黙って決める（検証・自動運転のオプション）。
     * 無ければタイトルとセーブ選択を出して利用者が選ぶ。
     */
    int pick;
    if (!savefile_slot.empty()) {
        pick = choose_savefile(savefile_slot) ? 1 : 0;
        fc_resume_clear(); //!< 枠を名指しされた。印は持ち越さない（追補 A2）
    } else if (resume) {
        /*
         * **言語切り替えの立て直し**（追補 A2）。直前の自分が保存した枠を、
         * タイトルを出さずに開く。印は `fc_resume_take()` が消すので一度きりである。
         *
         * 印が無い・セーブが消えている・新規で始まってしまう——どれもタイトルへ
         * 落とす。**黙って新規を始めない**：立て直しは「同じ人物で続ける」意味だから、
         * 別の何かが始まるくらいならタイトルで選ばせるほうが正しい。
         */
        char slot[64];
        pick = -2;
        if (fc_resume_take(slot, sizeof(slot)) && !choose_savefile(slot)) {
            std::fprintf(stderr, "[frox] --resume opened %s\n", slot);
            pick = 0;
        }
        if (pick != 0) {
            std::fprintf(stderr, "[frox] --resume found nothing to open; showing the title\n");
            fc_clear_savefile();
            pick = title_and_pick_savefile();
        }
    } else {
        fc_resume_clear(); //!< ふつうの起動。**残っている印を消す**（持ち越さない）
        pick = title_and_pick_savefile();
    }
    if (pick < 0) {
        std::fprintf(stderr, "[frox] shut down at the save picker (the ui went away)\n");
        send_exit_once(g_ui_gone.load() ? 1 : 0);
        fc_set_host_hooks(nullptr);
        fc_term_remove();
        g_log.close();
        return 0;
    }
    rc = fc_run_game((pick == 1) ? 1 : 0);
    std::fprintf(stderr, "[frox] game finished (%d)\n", rc);

    /* 引けなかった鍵を書き出す（`--report-untranslated=`）。ゲームのスレッドは終わっている。 */
    fc::lang_write_report();

    fc_set_host_hooks(nullptr);

    if ((g_dropped_keys != 0) || (g_key_decoder.dropped() != 0)) {
        std::fprintf(stderr, "[frox] dropped %lu key(s) and %lu byte(s) that would not convert\n",
            g_dropped_keys, g_key_decoder.dropped());
    }

    if (rc == FC_ERR_CORE) {
        /* コアが `core()` を踏んだ＝続行不能。`exit` ではなく `fatal`（v1 §4.2）。 */
        const std::string reason = fc_last_error();
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
 * @brief dlsym 用の C 入口。
 *
 * @details 画面（`libmain.so`）はコア選択で選ばれた `.so` からこの名前を引く
 * （`platform/android/hd2d_entry_android.cpp` の resolver）。名前は 5 コアで共通
 * ——**同時に開くのは 1 つ**（`RTLD_LOCAL`）なのでぶつからない。
 * `silq/adapter/sq_main.cpp` の同名の包みと同じ形（FH-07）。
 *
 * `argv` に当たるものは `forwarded_args`。見るのは `--lang=` だけ（M0 は受けて
 * 捨てる——設計 §3.6。M1 の受け皿）。lib の場所は `frox_lib_dir()`
 * （Android では cwd の下）から取る。セーブ枠の指定は Windows 専用の口なので見ない。
 */
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    std::string lang;
    bool resume = false;
    for (const std::string &opt : forwarded_args) {
        if (opt.rfind(kLangArg, 0) == 0) {
            const std::string value = opt.substr(std::string_view(kLangArg).size());
            if ((value == "ja") || (value == "en")) {
                lang = value;
            }
        }
        /*
         * **`--resume` もここで拾う**（追補 A2）。画面側は言語を替えるとき、同じコアを
         * 起こし直して `forwarded_args` へ `--resume` を積む（`hd2d_app.cpp` の (d2)）。
         * Android も同じ道を通る——立て直しは平台に依らない。
         */
        if (opt == kResumeArg) {
            resume = true;
        }
    }
    return run_protocol(in, out, protocol_log_path, frox_lib_dir(), lang, std::string(), resume);
}

#endif /* !_WIN32 */

int main(int argc, char **argv)
{
    bool selftest = false;
    bool has_protocol_arg = false;
    std::string log_path;
    std::string lib_dir = frox_lib_dir();
    std::string lang;
    std::string savefile_slot;
    bool resume = false;

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
            log_path = std::string(opt.substr(kProtocolLogArg.size()));
            continue;
        }
        if (opt.rfind(kSavefileArg, 0) == 0) {
            savefile_slot = std::string(opt.substr(kSavefileArg.size()));
            continue;
        }
        if (opt == kResumeArg) {
            resume = true;
            continue;
        }
        if (opt.rfind(kLibArg, 0) == 0) {
            lib_dir = std::string(opt.substr(kLibArg.size()));
            continue;
        }
        if (opt.rfind(kReportUntranslatedArg, 0) == 0) {
            g_untranslated_report = std::string(opt.substr(kReportUntranslatedArg.size()));
            continue;
        }
        if (opt.rfind(kLangDirArg, 0) == 0) {
            g_lang_dir_override = std::string(opt.substr(kLangDirArg.size()));
            continue;
        }
        if (opt.rfind(kLangArg, 0) == 0) {
            lang = std::string(opt.substr(kLangArg.size()));
            if ((lang != "ja") && (lang != "en")) {
                std::fprintf(stderr, "[frox] unknown language: %s (expected \"ja\" or \"en\")\n",
                    lang.c_str());
                return 1;
            }
            continue;
        }

        std::fprintf(stderr, "[frox] unknown argument: %s\n", argv[i]);
        print_usage();
        return 1;
    }

    if (selftest) {
        return run_selftest(lib_dir);
    }
    if (!has_protocol_arg) {
        print_usage();
        return 2;
    }

    // 長さ枠が改行変換で壊れるのを止める。プロトコルを 1 バイトでも流す前に行う。
    presentation::set_stdio_binary_mode();

    return run_protocol(presentation::transport_stdin(), presentation::transport_stdout(),
        log_path, lib_dir, lang, savefile_slot, resume);
}
