/*!
 * @file core_main.cpp
 * @brief `HengbandCore.exe` の入口（プロトコル v1 の core 側）。**Step 4b 本実装**。
 *
 * 基準はプロトコル v1 の仕様
 * （§3 握手 / §4 カタログ / §5 UiSeam 対応 / §6.4 合体規則 / §7 ui_state /
 *  §8・§8.1 pad_commands・sub_panel_kinds / §9 生存管理 / §11.2 会話ログ）、
 * 実装計画は分割の第 4 段の §3・§4。
 *
 * ## この TU の立ち位置
 * 旧 SDL2 の入口（削除済み）の**双子**である。あちらが「旧 2D UI を UiSeam に
 * 束ねて `run_sdl_game` へ渡す合成ルート」なら、こちらは「**パイプの向こうの ui** を
 * UiSeam に束ねて同じ `run_sdl_game` へ渡す合成ルート」。`presentation/` と `src/` は
 * どちらのルートでも 1 行も変わらない（`bootstrap` 無改変が分割の受け入れ条件）。
 *
 * ## スレッド構成
 * - **受信スレッド 1 本**: `read_message` を回し、種別ごとに置き場へ振り分ける。
 *   EOF / 読み取りエラーは「ui 消滅」（v1 §9.2）。
 * - **ゲームスレッド（main）**: `run_sdl_game` がそのまま回る。UiSeam の 5 関数は
 *   すべてこのスレッドから呼ばれるので、コアの状態を触るのはここだけになる。
 * - 送信は呼び出しスレッドから直接行い、`g_send_mutex` で直列化する。
 *
 * ## サブシステム
 * CONSOLE（`main()`）。窓は持たない（`HENGBAND_SDL2_UI=1` で `src/main-win.cpp` の
 * `WinMain` は無効化されているので、入口はここだけ）。
 *
 * ## stdout の扱い
 * stdout は**プロトコル専用**である。人間向けの出力は必ず stderr へ出す（v1 §1.1）。
 * `--bot-json-output=-` は snapshot を `std::cout` へ流す（`src/bot/bot-json-output.cpp:694`）ので
 * **プロトコルを破壊する**。起動時に見つけたら `fatal` を送って終わる（計画 §3-7）。
 *
 * ## 文字コード
 * このファイルは `/execution-charset:shift-jis` で焼く（`presentation/` のコア接触 TU と同じ）。
 * コアのヘッダの `_()` 文字列（緊急セーブの `died_from` など）がコア内部コードで
 * 焼かれる必要があるため。**プロトコルに出す文字列は全部 ASCII** なので影響を受けない
 * （UTF-8 が要る `label_utf8` 類は、既に UTF-8 で持たれている値をそのまま運ぶだけ）。
 */
// この TU は Android でもそのまま組む（入口は `platform/shared/core_protocol_main.h`）。
// Windows 固有なのは stdio ハンドル・MCI のメッセージポンプ・::Sleep の 3 点だけで、
// それぞれ #if defined(_WIN32) に閉じてある。
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// 上流 3.0.2.4 の `src/external-lib/include/xoshiro.h` は `min()`/`max()` を静的メンバに持つ。
// windows.h の同名マクロに食われて構文エラーになるので、取り込む前に黙らせておく。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include "../shared/core_protocol_main.h"
#include "locale/character-encoding.h" //!< internal_character_encoding()（ファイル名の符号化を合わせる）
#include "locale/localized-format.h" //!< i18n::set_language（コア側の言語）
#include "locale/text-encoding.h" //!< --text-check（文字幅の層の自己検査）

#include "frame/frame_codec.h"
#include "frame/pad_command_table.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"
#include "audio/sound_event_queue.h"
#include "frame/sdl_ui_options.h"
#include "frame/sub_panel_kinds.h"
#include "frame/ui_seam.h"

#include "bootstrap/sdl_game_bootstrap.h"
#include "bridge/asset_manifest_builder.h"
#include "bridge/input_event_adapter.h"
#include "bridge/pad_command_bridge.h" //!< register_pad_command_table()（言語切替で名札を組み直す）
#include "bridge/presentation_bridge.h"
#include "term/sdl_sub_window_terms.h"

#include "cmd-io/macro-util.h" //!< `macro_patterns`（マクロのトリガー。v1 §8.4）
#include "game-option/input-options.h"
#include "game-option/runtime-arguments.h"
#include "io/signal-handlers.h"
#include "locale/language-switcher.h"
#include "save/save.h"
#include "system/angband-system.h"
#include "system/redrawing-flags-updater.h" //!< 言語切替時の全面描き直し
#include "util/enum-range.h"
#include "system/angband-version.h"
#include "system/floor/floor-info.h"
#include "system/player-type-definition.h"
#include "term/z-util.h"
#include "world/world.h"

#include <atomic>
#include <chrono>
#include <clocale>
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

extern PlayerType *p_ptr;

namespace {

//! v1 §1.1 の起動引数。これが無い core は「単体で動く従来版」を名乗る立場に無い。
constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
//! v1 §11.2 の会話ログ。既定 OFF（未決事項 5 は「常時 OFF・要求時だけ」で決着させる）。
constexpr std::string_view kProtocolLogArg = "--protocol-log=";
//! `src/bot/bot-json-output.cpp` が stdout へ流す指定。プロトコルと同居できない。
constexpr std::string_view kBotJsonArg = "--bot-json-output=";

/* ============================================================ 会話ログ（v1 §11.2） */

/*!
 * @brief 全送受信メッセージを JSON Lines で残す。
 * @details 1 行 = `{"time","dir","t","len","payload"}`。**`frame` も全文残す**
 * （長さと `frame_id` だけに間引きたくなるが、この記録の主目的は
 *  「ui 無しで core を回すリプレイ」（v1 §11.2）なので、中身が無いと使えない）。
 * `payload` は元の JSON を**文字列として**入れる。ログ行そのものが常に妥当な
 * JSON になり、壊れたペイロード（プロトコル違反の調査対象）も同じ形で残せる。
 */
class ProtocolLog {
public:
    void open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb");
        if (this->fp_ == nullptr) {
            std::fprintf(stderr, "[core] could not open the protocol log: %s\n", path.c_str());
        }
    }

    bool enabled() const { return this->fp_ != nullptr; }

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
    //! ISO 8601（UTC・ミリ秒まで）。ログを 2 本並べて時系列で突き合わせるための時刻。
    static std::string iso_now()
    {
#if defined(_WIN32)
        SYSTEMTIME st{};
        ::GetSystemTime(&st);
        char buf[40]{};
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return std::string(buf);
#else
        const auto now = std::chrono::system_clock::now();
        const std::time_t sec = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm {};
        (void)gmtime_r(&sec, &tm);
        char buf[40]{};
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
        return std::string(buf);
#endif
    }

    /*!
     * @brief JSON 文字列リテラルとして安全な形へ直して追記する。
     * @details 非 ASCII バイトはそのまま通す（ペイロードは UTF-8 なので、
     * ログ行も UTF-8 のまま妥当な JSON になる）。壊れた UTF-8 が来たときは
     * ログ行も同じだけ壊れるが、**それが記録すべき事実**なので直さない。
     */
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
//! `exit` メッセージに載せる終了コード（ui 消滅で終わるときだけ非 0）。
std::atomic<int> g_exit_code{ 0 };

//! 受信キュー（`keys`）と最新値スロット（`ui_state`）。
std::mutex g_inbox_mutex;
std::deque<int> g_key_queue;
presentation::UiStateMessage g_ui_state;
bool g_ui_state_present = false;

/*!
 * @brief 1 メッセージ送る。失敗したら「ui 消滅」に倒す。
 * @details 書き込みは複数のスレッドから来ないが（送信はゲームスレッドだけ）、
 * 緊急セーブ経路と `quit_aux` フックが割り込む余地があるので排他しておく。
 */
bool send_message(const std::string &type, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    if (g_out == nullptr) {
        return false;
    }
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[core] send \"%s\" failed: %s\n", type.c_str(), err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    g_log.record("out", type, payload);
    return true;
}

//! `fatal` を送って（送れる状態なら）理由を stderr にも残す。v1 §9.3。
void send_fatal(const std::string &reason)
{
    std::fprintf(stderr, "[core] fatal: %s\n", reason.c_str());
    presentation::FatalMessage fatal;
    fatal.reason = reason;
    (void)send_message("fatal", presentation::encode_fatal(fatal));
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
                std::fprintf(stderr, "[core] receive stopped: %s%s%s\n",
                    presentation::transport_result_name(result), err.empty() ? "" : " - ", err.c_str());
            } else {
                std::fprintf(stderr, "[core] stdin closed (the ui is gone)\n");
            }
            g_ui_gone.store(true);
            return;
        }

        const std::string type = presentation::peek_message_type(payload);
        g_log.record("in", type, payload);

        if (type == "keys") {
            presentation::KeysMessage message;
            if (!presentation::decode_keys(payload, message, err)) {
                std::fprintf(stderr, "[core] bad \"keys\": %s\n", err.c_str());
                continue;
            }
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            for (const int key : message.keys) {
                g_key_queue.push_back(key);
            }
            continue;
        }
        if (type == "input_event") {
            // v1 §8.3（Phase 2 P2-2a）。アダプタでキーバイト列へ展開し、keys と
            // **同じ入れ物**へ同じ順序で積む（等価性の根拠。到着順も保存される）。
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) {
                std::fprintf(stderr, "[core] bad \"input_event\": %s\n", err.c_str());
                continue;
            }
            std::vector<int> keys;
            presentation::append_input_event_keys(message, keys);
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            for (const int key : keys) {
                g_key_queue.push_back(key);
            }
            continue;
        }
        if (type == "ui_state") {
            presentation::UiStateMessage message;
            if (!presentation::decode_ui_state(payload, message, err)) {
                std::fprintf(stderr, "[core] bad \"ui_state\": %s\n", err.c_str());
                continue;
            }
            // push-latest（v1 §7）。履歴に意味は無いので上書きでよい。
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            g_ui_state = std::move(message);
            g_ui_state_present = true;
            continue;
        }
        if (type == "quit_request") {
            g_quit_requested.store(true);
            std::fprintf(stderr, "[core] quit_request received\n");
            continue;
        }
        // 未知の種別は黙って捨てる（v1 §2.3）。v2 予約種別（§12）もここへ落ちる。
    }
}

/* ============================================== Windows メッセージポンプ（MCI 通知） */

#if defined(_WIN32)
//! 取りこぼしの検査用。`MM_MCINOTIFY` を何回ディスパッチしたか（stderr に出す）。
unsigned long g_mci_notify_count = 0;
#endif

/*!
 * @brief 自スレッドのメッセージを掃く。**BGM のループ再生に要る**。
 *
 * @details `presentation/audio/sdl_win_audio.cpp:50` の `create_mci_window()` が
 * `HWND_MESSAGE` の窓を作り、`setup_mci()` でそこへ `MM_MCINOTIFY` を届けさせる。
 * 曲が終わったという通知がこの窓に来て初めて次の曲が始まる（`on_mci_notify`）。
 * 合成 exe ではこれを **SDL の `SDL_PumpEvents`**（内部で `PeekMessage(NULL,...)`）が
 * 掃いていた。core には SDL が無いので、掃く者をここで用意する。
 *
 * ### なぜここ（seam の中）で掃くのが正しいか
 * 1. `PeekMessage` が取れるのは**呼び出しスレッドのキュー**だけである。MCI の窓を
 *    作るのは `run_sdl_game` の手順 6d（`sdl_win_audio_init`）で、これはゲーム
 *    スレッド＝UiSeam の 5 関数が呼ばれるスレッドと同じ。別スレッド（受信スレッド）で
 *    掃いても 1 通も取れない。
 * 2. コアが入力を待っている間は `SdlNullTerm::on_event` の待ちループが
 *    `delay_ms(10)` を毎周回呼ぶ（`presentation/term/sdl_null_term.cpp:462`）ので、
 *    待ち中は 100 回/秒の頻度が保証される。
 * 3. 待ちに入らない長い処理（階の生成・休息・移動の連続）では `delay_ms` が来ないので、
 *    `pump_input` からも掃く（同 :445 / :468 / :490 の 3 経路）。両方に置くことで
 *    「入力待ち」でも「処理中」でも取りこぼさない。メッセージはキューに溜まるだけで
 *    消えないため、遅れることはあっても落ちることはない。
 */
void pump_win_messages()
{
#if defined(_WIN32)
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (msg.message == MM_MCINOTIFY) {
            ++g_mci_notify_count;
            std::fprintf(stderr, "[core] MM_MCINOTIFY dispatched (#%lu)\n", g_mci_notify_count);
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
#endif
    // 非 Windows: MCI が無く、音は SDL_mixer が自前のスレッドで回すので、掃くものが無い。
}

/* ============================================================ 終了と緊急セーブ */

void (*g_prev_quit_aux)(std::string_view) = nullptr;
std::atomic<bool> g_exit_sent{ false };

/*!
 * @brief `quit()` の直前フック。**ここが core の最後の口**（`quit` は `std::exit` する）。
 * @details 既存 `sdl_quit_hook`（`presentation/bootstrap/sdl_game_bootstrap.cpp:62`）を
 * 潰さずに前へ流してから `exit` を送る（理由と backtrace の stderr 記録は残したい）。
 */
void core_quit_hook(std::string_view reason)
{
    if (g_prev_quit_aux != nullptr) {
        g_prev_quit_aux(reason);
    }
    if (!g_exit_sent.exchange(true)) {
        presentation::ExitMessage bye;
        bye.code = g_exit_code.load();
        (void)send_message("exit", presentation::encode_exit(bye));
    }
    g_log.close();
}

/*!
 * @brief `run_sdl_game` が入れた `quit_aux` を包み直す（1 回だけ）。
 * @details `run_sdl_game` の手順 3 が `quit_aux` を上書きするので、**その後**でしか
 * 包めない。最初の `before_capture`（`init_angband` 中の `term_fresh` から来る）が
 * その「後」の最初の機会になる。`run_sdl_game` の前に呼ばれた場合は `quit_aux` が
 * まだ `nullptr` なので、その条件で弾いている（弾かないと手順 3 に上書きされて消える）。
 */
void install_quit_hook_once()
{
    if ((quit_aux == nullptr) || (quit_aux == &core_quit_hook)) {
        return;
    }
    g_prev_quit_aux = quit_aux;
    quit_aux = &core_quit_hook;
}

/*!
 * @brief ui が消えたときの緊急セーブ（v1 §9.2）。**戻らない**。
 *
 * @details `src/io/signal-handlers.cpp:120` の `handle_signal_abort` と同じ経路を踏む。
 * ただし**画面へ出す 2 行と冒険日誌への記録は行わない**:
 *   - 画面出力は `term_fresh` を呼ぶ＝`capture` → `present` → 壊れたパイプへの書き込み
 *     になる。見る相手がいない絵のために、セーブ前に I/O エラーの機会を増やす意味が無い。
 *   - 日誌はプレイヤの死因記録で、ここは「フロントエンドが落ちた」だけ。ゲーム内の
 *     出来事として残すべき事象ではない（`died_from` は `handle_signal_abort` と同じに
 *     しておく。セーブ形式が要求するため）。
 */
std::atomic<bool> g_panic_saving{ false };

[[noreturn]] void emergency_save_and_exit(const char *why)
{
    if (g_panic_saving.exchange(true)) {
        // 到達しない（呼び出し口は全部 `check_ui_alive` を通り、そこで弾かれる）。
        // 万一の再入で**セーブの途中を壊さない**ための最後の砦。
        std::_Exit(1);
    }
    std::fprintf(stderr, "[core] the ui is gone (%s); panic save\n", why);
    g_exit_code.store(1);

    const auto &world = AngbandWorld::get_instance();
    if (!world.character_generated || world.character_saved) {
        quit(""); // 守るものが無い（タイトル中・セーブ済み）
    }

    auto &floor = *p_ptr->current_floor_ptr;
    floor.forget_lite();
    floor.forget_view();
    floor.forget_mon_lite();

    AngbandSystem::get_instance().set_panic_save(true);
    p_ptr->died_from = _("(緊急セーブ)", "(panic save)");
    signals_ignore_tstp();

    const bool saved = save_player(p_ptr, SaveType::CLOSE_GAME);
    std::fprintf(stderr, "[core] panic save %s\n", saved ? "succeeded" : "FAILED");
    quit("");

    // quit() は必ず std::exit する。到達しないが [[noreturn]] を満たすため。
    std::_Exit(1);
}

/*!
 * @brief ui の生死を見て、消えていたら緊急セーブして終わる。seam の各所から呼ぶ。
 * @details **緊急セーブの最中は何もしない。** `save_player` はコアの都合で Term を
 * 触る（`term_fresh` が挟まると `SdlNullTerm::on_fresh` → `before_capture` → ここ、と
 * 戻ってくる）ので、この守りが無いとセーブの途中で `_Exit` して**書きかけのセーブを
 * 残す**。ローグライクでこれは最悪の壊れ方なので、入口を 1 本にして塞いである。
 */
void check_ui_alive(const char *where)
{
    if (g_panic_saving.load()) {
        return;
    }
    if (g_ui_gone.load()) {
        emergency_save_and_exit(where);
    }
}

/* ==================================================== ui_state の適用（v1 §7・計画 §4） */

int clamp_int(int value, int lo, int hi)
{
    if (value < lo) {
        return lo;
    }
    return (value > hi) ? hi : value;
}

/*!
 * @brief `map_style`（graf_px / graf_tag）→ `SdlUiOptions::map_style_index` の逆引き。
 * @return 見つからなければ -1（＝触らない）。
 * @details 表の持ち主は `SdlUiOptions` なので、ここで独自の対応表を作らずに問い合わせる。
 * @note `graf_px == 0 && graf_tag == "ascii"` は索引 0（HD タイル）と 3（アスキーアート）の
 * 両方に当たるが、**コアから見た違いは無い**（どちらも `use_graphics` を立てない）。
 * 見た目の違いは ui 側だけの話なので、先に当たった方を採ってよい。
 */
int map_style_index_from(int graf_px, const std::string &graf_tag)
{
    for (int i = 0; i < SdlUiOptions::kMapStyleCount; ++i) {
        if (SdlUiOptions::map_style_graf_px(i) != graf_px) {
            continue;
        }
        if (graf_tag == SdlUiOptions::map_style_graf_tag(i)) {
            return i;
        }
    }
    return -1;
}

/*!
 * @brief 最新の `ui_state` を自プロセスへ流し込む（計画 §4）。
 *
 * @details 分界は「cfg の持ち主は ui、`sdl_ui_options()` シングルトンは受け皿」。
 * こう書くことで `presentation/bridge`・`audio`・`sub_window_terms` の既存の
 * 読み取りコードが**無改変**で動く。`before_capture` から毎回呼ぶ（v1 §5 の
 * 「core は capture のたびに最新の ui_state を適用する」）。
 *
 * @note 音量系だけは値が変わったときだけ `apply_sdl_ui_options()` を叩く。
 * このフックは `sdl_win_audio_apply_options()` → `select_floor_music()` まで行くので、
 * 毎フレーム叩くと選曲をやり直し続けることになる。
 * @note 毎回書き戻すのには意味がある。`sdl_win_audio_init`（`run_sdl_game` 手順 6d）が
 * `load_sdl_ui_options()` でシングルトンを cfg の内容へ**戻してしまう**ため、
 * 「ui_state が来たときだけ書く」にすると、その 1 回で ui の設定が失われる。
 */
void apply_ui_state(presentation::Bridge &bridge)
{
    presentation::UiStateMessage state;
    {
        std::lock_guard<std::mutex> lock(g_inbox_mutex);
        if (!g_ui_state_present) {
            return; // まだ 1 通も来ていない。既定値のままで capture する（v1 §7）
        }
        state = g_ui_state;
    }

    /*
     * コア側の言語（実行時多言語化の第 5 段）。**画面のつまみをここまで届かせる。**
     * 空なら触らない——環境変数 `HENGBAND_LANG` で指定した値を潰さないため
     * （検査モードは cfg を読まないので、あちらが唯一の口になる場面がある）。
     */
    if (!state.lang.empty()) {
        const auto generation_before = i18n::language_generation();
        i18n::set_language((state.lang == "en") ? i18n::Language::ENGLISH : i18n::Language::JAPANESE);
        if (i18n::language_generation() != generation_before) {
            /*
             * **言語が実際に変わった。**Term に入っている絵は前の言語のままなので、
             * 本線もサブパネルも丸ごと描き直させる（差分描画は「変わっていない」と思っている）。
             */
            auto &rfu = RedrawingFlagsUpdater::get_instance();
            rfu.fill_up_sub_flags();
            rfu.set_flags(EnumClassFlagGroup<MainWindowRedrawingFlag>(EnumRange(MainWindowRedrawingFlag::TITLE, MainWindowRedrawingFlag::MAX)));
        }
    }

    auto &opts = sdl_ui_options();
    opts.cursor_mode_enabled = state.cursor_mode;
    if (state.has_sub_panel_kinds) {
        for (int i = 0; i < presentation::kProtocolSubPanelCount; ++i) {
            opts.sub_panel_kind[i] = state.sub_panel_kinds[i];
        }
    }
    if (const int style = map_style_index_from(state.map_style_graf_px, state.map_style_graf_tag); style >= 0) {
        opts.map_style_index = style;
    }
    // bot JSON は core 側のコマンドライン／環境変数が優先（v1 §7 の注）。
    opts.bot_json_enabled = state.bot_json_enabled;
    if (!presentation::bot_json_forced() && !state.bot_json_path.empty()) {
        arg_bot_json_output_path = state.bot_json_path;
    }

    /*
     * リアルタイム進行。**時計を回すのはこちら側**で、
     * 設定を持っているのは画面側なので、届いた値をそのまま写す。
     * 実際に `RealtimeClock` へ入れるのは `apply_realtime_mode`（`presentation_bridge.cpp`）。
     */
    opts.realtime_enabled = state.realtime_enabled;
    opts.realtime_speed_index = clamp_int(state.realtime_speed_index, 0, SdlUiOptions::kRealtimeSpeedCount - 1);
    opts.realtime_prompt_live = state.realtime_prompt_live;
    opts.realtime_self_span_index = clamp_int(state.realtime_self_span_index, 0, SdlUiOptions::kRealtimeSelfSpanCount - 1);

    /*
     * **効果音を誰が鳴らすか**。真なら、こちらは鳴らさずに
     * `frame.sounds` へ書き留める。`use_sound` は**立てたまま**にすること——
     * コアの `sound()` はあれが偽だと `Term_xtra` すら呼ばず、書き留める機会ごと消える。
     */
    presentation::set_sound_events_wanted(state.sound_events);

    const int sound_volume = clamp_int(state.sound_volume_index, 0, SdlUiOptions::kVolumeLevels - 1);
    const int music_volume = clamp_int(state.music_volume_index, 0, SdlUiOptions::kVolumeLevels - 1);
    const bool audio_changed = (opts.sound_enabled != state.sound_on) || (opts.music_enabled != state.music_on) || (opts.sound_volume_index != sound_volume) || (opts.music_volume_index != music_volume);
    opts.sound_enabled = state.sound_on;
    opts.music_enabled = state.music_on;
    opts.sound_volume_index = sound_volume;
    opts.music_volume_index = music_volume;
    if (audio_changed) {
        apply_sdl_ui_options();
    }

    // ここから下は旧 SDL2 の入口（削除済み）の before_capture の写し。
    // **タイル px・レイアウト・可視行列の計算は ui の仕事**（描画経路を知っているのは ui だけ）
    // なので、core は送られてきた結果をそのまま流すだけにする。
    if ((state.view_w > 0) && (state.view_h > 0)) {
        bridge.set_view_size(state.view_w, state.view_h);
    }
    bridge.set_camera_follow_player(state.camera_follow_player);
    for (int panel = 0; panel < presentation::kProtocolSubPanelCount; ++panel) {
        const auto &cells = state.sub_panel_cells[panel];
        if ((cells.cols > 0) && (cells.rows > 0)) {
            presentation::set_sub_panel_term_cells(panel, cells.cols, cells.rows);
        }
    }
}

/* ================================================ pad_commands / sub_panel_kinds（§8） */

bool g_tables_sent = false;
bool g_last_rogue_like = false;
bool g_pad_commands_resend = false;
//! 表を送ったときの言語の世代。言語が変わったら**表を組み直して**送り直す。
int g_tables_language_generation = -1;
//! 直近に送ったマクロのトリガー（`macro_trigger_signature()` の値）。増減の見張りに使う。
std::string g_last_macro_signature;

//! `pad_command_table()` ＋ 両モードの解決結果を丸ごとワイヤ型へ写す（v1 §8）。
std::string build_pad_commands_payload()
{
    presentation::PadCommandsMessage message;
    message.current_keymap = rogue_like_commands ? "rogue" : "original";
    for (const auto &entry : pad_command_table()) {
        presentation::PadCommandWireEntry wire;
        wire.id = static_cast<int>(entry.id);
        wire.command = static_cast<int>(entry.command);
        wire.group_utf8 = (entry.group_utf8 != nullptr) ? entry.group_utf8 : "";
        wire.label_utf8 = (entry.label_utf8 != nullptr) ? entry.label_utf8 : "";
        for (const uint8_t key : pad_command_key_sequence(entry.command, PadKeymapMode::Original)) {
            wire.seq_original.push_back(static_cast<int>(key));
        }
        for (const uint8_t key : pad_command_key_sequence(entry.command, PadKeymapMode::Rogue)) {
            wire.seq_rogue.push_back(static_cast<int>(key));
        }
        message.entries.push_back(std::move(wire));
    }
    return presentation::encode_pad_commands(message);
}

/*!
 * @brief いま登録されているマクロのトリガーをそのまま運ぶ（v1 §8.4）。
 * @details ui は自分が作るボタンのトリガー列（`pad_input_trigger_sequence`）と
 * 突き合わせて「このボタンにマクロが乗っているか」を知る。**コアはパッドを知らない。**
 */
std::string build_macro_triggers_payload()
{
    presentation::MacroTriggersMessage message;
    for (auto i = 0; i < active_macros; ++i) {
        std::vector<int> pattern;
        pattern.reserve(macro_patterns[i].size());
        for (const char one_byte : macro_patterns[i]) {
            pattern.push_back(static_cast<unsigned char>(one_byte));
        }
        if (!pattern.empty()) {
            message.patterns.push_back(std::move(pattern));
        }
    }
    return presentation::encode_macro_triggers(message);
}

/*!
 * @brief マクロが増減したか見るための印。
 * @details `@` でいつでも増減するので、握手のときの 1 回では足りない。
 * トリガーは数も長さも小さい（既定で 0 件、多い人でも数十件×数バイト）ので、
 * 中身をそのまま並べた文字列で比べてしまうのがいちばん確かで安い。
 */
std::string macro_trigger_signature()
{
    std::string signature;
    for (auto i = 0; i < active_macros; ++i) {
        signature += macro_patterns[i];
        signature.push_back('\n');
    }
    return signature;
}

//! `sub_panel_kind_entries()`（コアの `window_flag_desc` 由来）をそのまま運ぶ（v1 §8.1）。
std::string build_sub_panel_kinds_payload()
{
    presentation::SubPanelKindsMessage message;
    for (const auto &entry : sub_panel_kind_entries()) {
        presentation::SubPanelKindWireEntry wire;
        wire.flag = entry.flag;
        wire.label_utf8 = entry.label_utf8;
        message.entries.push_back(std::move(wire));
    }
    return presentation::encode_sub_panel_kinds(message);
}

/*!
 * @brief 資産の基準ディレクトリ。Windows は exe のあるディレクトリ、他は cwd。
 * @details Windows で cwd を使わないのは、将来 ui が別 cwd から core を起こしても
 * 壊れないようにするため。Android には「exe のディレクトリ」という概念が無く、
 * 入口（`hd2d_entry_android.cpp`）が内部ストレージへ chdir 済みなので cwd が基準になる
 */
std::filesystem::path resolve_base_dir()
{
#if defined(_WIN32)
    char buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    const std::filesystem::path exe_path(std::string(buf, (len > 0) ? len : 0));
    return exe_path.parent_path();
#else
    std::error_code ec;
    return std::filesystem::current_path(ec);
#endif
}

/*!
 * @brief タイル資産目録（v1 §8.2）。root は基準ディレクトリ（絶対パス）。
 * @details official_tile_table の path はリポジトリ直下相対で、exe もリポジトリ直下に
 * 置かれる（ビルド後イベント）ので、この基準で正しい。
 */
std::string build_asset_manifest_payload()
{
    return presentation::encode_asset_manifest(
        presentation::build_official_asset_manifest(resolve_base_dir().string()));
}

/*!
 * @brief 表の送信（初回 1 回＋キー配列切替時の pad_commands 再送）。
 * @details 「初期化完了時に 1 回」（v1 §8）の実体は `register_pad_command_table()` の
 * 直後だが、`bootstrap` は無改変で通す約束なので、**表が埋まったことを present から
 * 見張る**形にする（`run_sdl_game` 手順 6b が済むまで `pad_command_table()` は空。
 * `init_angband` 中の `term_fresh` から来る最初の present はまだ空である）。
 */
void send_tables_if_needed()
{
    if (!g_tables_sent) {
        if (pad_command_table().empty()) {
            return; // まだ手順 6b（register_pad_command_table）を通っていない
        }
        g_tables_sent = true;
        g_last_rogue_like = rogue_like_commands;
        g_last_macro_signature = macro_trigger_signature();
        g_tables_language_generation = i18n::language_generation();
        /*
         * 手順 6b（register_pad_command_table）と最初の present の**間**に言語が
         * 届いていることがある（cfg の言語は ui_state で来る）。名札は組んだ時に
         * 固まるので、送る直前に**いまの言語で組み直して**から送る。
         */
        presentation::register_pad_command_table();
        (void)send_message("pad_commands", build_pad_commands_payload());
        (void)send_message("sub_panel_kinds", build_sub_panel_kinds_payload());
        (void)send_message("asset_manifest", build_asset_manifest_payload());
        (void)send_message("macro_triggers", build_macro_triggers_payload());
        return;
    }

    /*
     * **言語が変わったら名札の表を組み直して送り直す。**
     * `register_pad_command_table()` は組んだ時に `_L()` の名札を固める（`.get()` を
     * 解決した `const char *` を溜める）ので、送り直すだけでは前の言語のままである。
     * サブパネルの種類名（`sub_panel_kind_entries()`）は世代を見て自分で組み直す。
     * これが無いと、途中で英語にしても機能メニューの名札とパネル名が日本語のまま残る
     * （実機の報告 2026-08-16。Windows は起動時に cfg の言語が先に届くので出にくいだけ）。
     */
    if (i18n::language_generation() != g_tables_language_generation) {
        g_tables_language_generation = i18n::language_generation();
        presentation::register_pad_command_table();
        (void)send_message("pad_commands", build_pad_commands_payload());
        (void)send_message("sub_panel_kinds", build_sub_panel_kinds_payload());
    }

    if (g_pad_commands_resend) {
        g_pad_commands_resend = false;
        (void)send_message("pad_commands", build_pad_commands_payload());
    }
    /*
     * マクロは `@` でいつでも増減する（セーブの読み込みでも入れ替わる）。
     * **変わったら送り直す**——送らないと、画面のボタンに出す「マクロ」の札が
     * 登録した瞬間には出ず、次に起動するまで嘘のままになる。
     */
    if (auto signature = macro_trigger_signature(); signature != g_last_macro_signature) {
        g_last_macro_signature = std::move(signature);
        (void)send_message("macro_triggers", build_macro_triggers_payload());
    }
}

/* ================================================================ 合体規則（§6.4） */

/*!
 * @brief 合体判定に使う鍵（＝`frame_id` を抜いたペイロード）。
 * @details v1 §6.4 は「内容が前回送信フレームと同一なら送らなくてよい」だが、
 * `frame_id` は capture ごとに必ず増える（同 §6.1 で常に出す例外でもある）ので、
 * 素のバイト比較では**永久に一致しない**。数字だけ抜いて比べる。
 * codec の正準形（同じ内容から常に同じバイト列。`frame_codec.h` 冒頭）が
 * あるので、これで「1 ビットでも違えば送る」を満たせる。
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

/* ==================================================================== 起動の下ごしらえ */

//! 基準ディレクトリ配下の lib パス（旧 SDL2 の入口と同じ解き方）。
std::filesystem::path resolve_lib_path()
{
    return resolve_base_dir() / "lib";
}

/*!
 * @brief `asset_roots.graf`（v1 §3.1）。exe と同じ場所の `lib/xtra/graf` の絶対パス。
 * @details 実在しなければ**空**を返す＝「申告なし」。ui は該当スタイルを無効化する。
 */
std::string resolve_graf_root()
{
    std::error_code ec;
    const std::filesystem::path graf = resolve_lib_path() / "xtra" / "graf";
    if (!std::filesystem::is_directory(graf, ec)) {
        return std::string();
    }
    return std::filesystem::absolute(graf, ec).generic_string();
}

//! 版文字列（v1 §3.1 の `core_version`）。`angband-version.h` の H_VER_* から作る。
std::string core_version_string()
{
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d", H_VER_MAJOR, H_VER_MINOR, H_VER_PATCH, H_VER_EXTRA);
    return std::string(buf);
}

void print_usage(const char *exe_name)
{
    std::fprintf(stderr,
        "HengbandCore - Hengband game core (protocol v%d, stdio transport)\n"
        "\n"
        "usage: %s %s [--protocol-log=<path>]\n"
        "\n"
        "This executable is not meant to be started by hand. The frontend\n"
        "(HengbandHd2d.exe) launches it as a child process and speaks the core\n"
        "protocol over stdin/stdout. Run HengbandHd2d.exe instead.\n",
        // kUiProtocolArg は文字列リテラル由来なので終端が付いている（data() をそのまま渡せる）。
        presentation::kProtocolVersion, exe_name, kUiProtocolArg.data());
}

/*!
 * @brief stdout ガード（計画 §3-7）。
 * @return bot JSON の出力先が stdout に指定されていたら true。
 * @details `run_sdl_game` の `parse_bot_json_output_args()` が採るのと同じ 2 つの口を、
 * **それが走る前に**自分で覗く（走った後では `std::cout` への 1 行目に間に合わない）。
 */
bool bot_json_would_hijack_stdout(const std::vector<std::string> &args)
{
    for (const auto &arg : args) {
        const std::string_view opt = arg;
        if (opt.starts_with(kBotJsonArg) && (opt.substr(kBotJsonArg.size()) == "-")) {
            return true;
        }
    }
    const char *const env = std::getenv("HENGBAND_BOT_JSON");
    return (env != nullptr) && (std::strcmp(env, "-") == 0);
}

} // namespace

/*!
 * @brief プロトコル v1 のコア側本体（宣言は `platform/shared/core_protocol_main.h`）。
 * @details Windows は下の `main()` が stdio ハンドルで呼ぶ。Android は hd2d の
 * `CoreLink`（1 プロセス 2 スレッド）がパイプの fd をコアスレッドから渡す。
 * 中身は従来の `main()` の後半そのまま（挙動を変えない括り出し）。
 */
int hengband_core_protocol_main(presentation::TransportHandle in, presentation::TransportHandle out,
    const std::string &protocol_log_path, const std::vector<std::string> &forwarded_args)
{
    const std::string &log_path = protocol_log_path;
    g_in = in;
    g_out = out;
    if ((g_in == nullptr) || (g_out == nullptr)) {
        std::fprintf(stderr, "[core] no transport endpoint; cannot speak the protocol\n");
        return 3;
    }
    if (!log_path.empty()) {
        g_log.open(log_path);
    }

    // --- 握手（v1 §3.1）。最初のメッセージは hello でなければならない。 ---
    std::string payload;
    std::string err;
    const presentation::TransportResult got = presentation::read_message(g_in, payload, err);
    if (got != presentation::TransportResult::Ok) {
        std::fprintf(stderr, "[core] failed to read the first message: %s%s%s\n",
            presentation::transport_result_name(got), err.empty() ? "" : " - ", err.c_str());
        return 4;
    }
    const std::string type = presentation::peek_message_type(payload);
    g_log.record("in", type, payload);
    if (type != "hello") {
        // hello 前の他メッセージはプロトコル違反（v1 §9.3）。未知種別を黙って捨てる
        // 規則（§2.3）は**握手の後**の話で、ここでは適用しない。
        send_fatal(std::string("expected \"hello\" as the first message but got \"") + (type.empty() ? std::string("<not a protocol message>") : type) + "\"");
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
    std::fprintf(stderr, "[core] hello from %s %s (protocol %d)\n",
        hello.ui_name.empty() ? "<unnamed ui>" : hello.ui_name.c_str(),
        hello.ui_version.empty() ? "?" : hello.ui_version.c_str(), hello.protocol);

    /*
     * stdout ガード（計画 §3-7）。握手の**後**に見るのは、fatal を送れる状態にしてから
     * 落としたいから（握手前に落とすと ui からは「黙って死んだ core」に見える）。
     */
    if (bot_json_would_hijack_stdout(forwarded_args)) {
        send_fatal("--bot-json-output=- (or HENGBAND_BOT_JSON=-) writes the bot snapshot to stdout, "
                   "which is the protocol channel; pick a file path instead");
        return 1;
    }

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
#ifdef TANGBAND
    ack.core_name = "tangband";
#else
    ack.core_name = "hengband";
#endif
    ack.core_version = core_version_string();
    /*
     * 持っている機能を申告する（v1 §3.1 の `features`。キーの追加なので版は上げない）。
     * `realtime` は**このコアだけ**が持つ。
     * 画面側はこれを見てリアルタイムの設定を出し入れする
     */
    ack.features.emplace_back("realtime");
    /*
     * `audio` は「**音を鳴らせる**」の申告（2026-08-21 に決めた）。画面側はこれを見て
     * 機能メニューの「音」の節を出し入れする。ここは Windows なら `sdl_win_audio.cpp`、
     * Android なら `sdl_mixer_audio.cpp` が受け持つので、どちらの組でも鳴らせる。
     */
    ack.features.emplace_back("audio");
    ack.asset_root_graf = resolve_graf_root();
    if (!send_message("hello_ack", presentation::encode_hello_ack(ack))) {
        return 5;
    }

    // 受信スレッド（v1 §5 の「キー待ちブロックは core 内でパイプ読みに置き換わる」）。
    // detach する: この先の終了経路はすべて quit() → std::exit なので join する場所が無い。
    std::thread(receive_loop).detach();

    /* ------------------------------------------------------------- ProtocolSeam */

    presentation::Bridge bridge;
    UiSeam seam;

    // before_capture: 最新 ui_state を適用する（v1 §5 / 計画 §4）。
    seam.before_capture = [&bridge]() {
        install_quit_hook_once();
        check_ui_alive("before_capture");
        apply_ui_state(bridge);
        // キー配列（オリジナル / ローグライク）の切替を capture 時に見張る（v1 §8）。
        if (g_tables_sent && (rogue_like_commands != g_last_rogue_like)) {
            g_last_rogue_like = rogue_like_commands;
            g_pad_commands_resend = true;
        }
    };

    // present: frame を送る。前回送信と同じ内容なら送らない（v1 §6.4）。
    seam.present = [](const GameFrame &frame) {
        if (g_panic_saving.load()) {
            // 緊急セーブ中。見る相手のいない絵のために壊れたパイプを叩かない。
            return;
        }
        send_tables_if_needed();
        const std::string wire = presentation::encode_game_frame(frame);
        std::string key = frame_compare_key(wire);
        if (g_has_last_frame && (key == g_last_frame_key)) {
            return;
        }
        g_last_frame_key = std::move(key);
        g_has_last_frame = true;
        (void)send_message("frame", wire);
    };

    // pump_input: 受信キューを非待機で吸い上げる（v1 §5）。
    seam.pump_input = [](KeyQueue &out) {
        pump_win_messages();
        check_ui_alive("pump_input");
        std::lock_guard<std::mutex> lock(g_inbox_mutex);
        while (!g_key_queue.empty()) {
            out.push_back(g_key_queue.front());
            g_key_queue.pop_front();
        }
    };

    // delay_ms: プロセス内 sleep（プロトコルには現れない。v1 §5）＋ MCI 通知の掃き出し。
    seam.delay_ms = [](int ms) {
        pump_win_messages();
        if (ms > 0) {
#if defined(_WIN32)
            ::Sleep(static_cast<DWORD>(ms));
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
        }
    };

    // quit_requested: 現行と同じく ESC 化される（挙動保存。v1 §5）。
    seam.quit_requested = []() {
        check_ui_alive("quit_requested");
        return g_quit_requested.load();
    };

    /*
     * now_ms: リアルタイム進行の締切に使う単調時計。
     * **これを束ねないとリアルタイムは働かない**（`SdlNullTerm::rt_begin` が false を返し、
     * 従来どおりのターン制へ落ちる）。SDL ではなく `steady_clock` を使うのは、
     * このプロセスが画面を持たず SDL のタイマを初期化していないため。
     * @note 握りっぱなしの方向キー（`held_dir_key`）は**束ねない**。この分割構成では
     * キーボードを見ているのは画面側で、こちらには握りが届かない。押しっぱなしの歩行は
     * 画面側が出すキーの繰り返しで起き、1 刻み 1 個に切り詰めるのは `rt_pump_single_key`。
     */
    seam.now_ms = []() {
        using namespace std::chrono;
        static const auto origin = steady_clock::now();
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - origin).count());
    };

    // 初回も view/cell を揃えておく（旧 SDL2 の入口と同じ理由の安全値）。
    // この時点の `quit_aux` は nullptr なので、フックの差し替えは起きない。
    seam.before_capture();

    const int rc = presentation::run_sdl_game(resolve_lib_path(), bridge, seam);

    /*
     * ここへは来ない（`run_sdl_game` は末尾で `quit("")` する＝`core_quit_hook` が
     * `exit` を送ってから `std::exit(0)`）。将来 bootstrap が戻るようになったときの保険。
     */
    if (!g_exit_sent.exchange(true)) {
        presentation::ExitMessage bye;
        bye.code = rc;
        (void)send_message("exit", presentation::encode_exit(bye));
    }
    g_log.close();
    return rc;
}

#if defined(_WIN32)

/*!
 * @brief CRT が「不正な引数」で即死する前に、どこで死んだかを stderr へ出す。
 * @details `_invalid_parameter` は `__fastfail`（`STATUS_STACK_BUFFER_OVERRUN` = 0xC0000409）を
 * 起こすので、**SEH でも VEH でも捕まらない**。ここで先に呼ばれる隙にだけ足跡が取れる。
 * Release では関数名などは空なので、戻りアドレスの並びを出して map/pdb と突き合わせる。
 */
static void report_invalid_parameter(const wchar_t *expression, const wchar_t *function,
    const wchar_t *file, unsigned int line, uintptr_t)
{
    const auto narrow = [](const wchar_t *w) -> std::string {
        if ((w == nullptr) || (*w == L'\0')) {
            return "(none)";
        }
        const auto len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 0) {
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
        }
        return out;
    };

    std::fprintf(stderr, "[core] FATAL invalid parameter: expr=%s func=%s file=%s line=%u\n",
        narrow(expression).c_str(), narrow(function).c_str(), narrow(file).c_str(), line);

    void *frames[32]{};
    const auto captured = CaptureStackBackTrace(0, 32, frames, nullptr);
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (USHORT i = 0; i < captured; ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(frames[i]);
        std::fprintf(stderr, "[core]   #%02u %p (HengbandCore+0x%llx)\n", i, frames[i],
            static_cast<unsigned long long>(addr - base));
    }

    std::fflush(stderr);
}

/*!
 * @brief 未捕捉例外で落ちる前に、何が投げられたかを stderr へ出す。
 * @details 画面が別プロセスなので、黙って `abort()` されると
 * 「パイプが EOF になった」しか判らない。**例外の中身をここで残す。**
 */
static void report_terminate()
{
    if (const auto eptr = std::current_exception()) {
        try {
            std::rethrow_exception(eptr);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "[core] FATAL uncaught exception: %s: %s\n", typeid(e).name(), e.what());
        } catch (...) {
            std::fprintf(stderr, "[core] FATAL uncaught exception (not a std::exception)\n");
        }
    } else {
        std::fprintf(stderr, "[core] FATAL std::terminate without an exception\n");
    }

    void *frames[32]{};
    const auto captured = CaptureStackBackTrace(0, 32, frames, nullptr);
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (USHORT i = 0; i < captured; ++i) {
        const auto addr = reinterpret_cast<uintptr_t>(frames[i]);
        std::fprintf(stderr, "[core]   #%02u %p (HengbandCore+0x%llx)\n", i, frames[i],
            static_cast<unsigned long long>(addr - base));
    }

    std::fflush(stderr);
    std::_Exit(3);
}

/*!
 * @brief `HengbandCore.exe` の入口。引数を読んで stdio ハンドルで本体を呼ぶだけ。
 */
int main(int argc, char **argv)
{
    _set_invalid_parameter_handler(report_invalid_parameter);
    std::set_terminate(report_terminate);

    /*
     * **ファイル名も外との境目である。**
     * MSVC の `std::filesystem::path` は narrow ⇔ wide を **CRT ロケールの符号化**で行い
     * （`__std_fs_code_page()` が `___lc_codepage_func()` を返す）、`fopen` のナローな
     * ファイル名も同じものを見る。既定は CP932 なので、内部を UTF-8 にすると
     *   - ディレクトリから読んだ名前が SJIS のバイト列で返り、画面で化ける
     *     （セーブ一覧の「ファル」が `□t□@□□` になった）
     *   - 逆に UTF-8 の名前で作ったファイルが化けた名前でディスクに載る
     * の両方が起きる。**内部符号化に合わせておけば両方向とも辻褄が合う。**
     */
    if constexpr (internal_character_encoding() == CharacterEncoding::UTF_8) {
        if (std::setlocale(LC_CTYPE, ".UTF-8") == nullptr) {
            std::fprintf(stderr, "[core] warning: could not switch the CRT locale to UTF-8;"
                                 " file names with non-ASCII characters may be garbled\n");
        }
    }

    /*
     * コア側の言語（実行時多言語化の第 1 段）。
     * **いまは `_F()` を通した書式文字列だけ**が切り替わる。`_()` はまだコンパイル時に潰れるので、
     * メニューや固定表示は日本語のままである。
     * 画面側の `HD2D_LANG` と同じ綴りにしてある。いずれ `ui_state` で画面から降りてくる（第 5 段）。
     */
    if (const auto *lang = std::getenv("HENGBAND_LANG"); lang != nullptr) {
        const std::string_view name(lang);
        if ((name == "en") || (name == "en_US") || (name == "English")) {
            i18n::set_language(i18n::Language::ENGLISH);
        }
    }

    bool has_protocol_arg = false;
    std::string log_path;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view opt = argv[i];
        /*
         * 文字幅の層（`src/locale/text-encoding.h`）の自己検査。
         * **パイプもゲームも要らない**ので、握手の判定より先に返す。
         * 内部 UTF-8 化はこの層へ呼び出しを移してから符号化を切り替える段取りなので、
         * 移す途中で層が壊れていないかを、いつでも 1 行で確かめられるようにしておく。
         */
        if (opt == "--text-check") {
            std::string report;
            const bool ok = text::self_check(report);
            //! 区切りは ASCII にする。em ダッシュは実行文字コード（CP932）に無い
            std::fprintf(stderr, "[core] text-check: %s%s%s\n", ok ? "PASS" : "FAIL",
                ok ? "" : " - ", report.c_str());
            return ok ? 0 : 1;
        }
        args.emplace_back(opt);
        if (opt == kUiProtocolArg) {
            has_protocol_arg = true;
        } else if (opt.starts_with(kProtocolLogArg)) {
            log_path = std::string(opt.substr(kProtocolLogArg.size()));
        }
    }
    if (!has_protocol_arg) {
        print_usage((argc > 0) ? argv[0] : "HengbandCore.exe");
        return 2;
    }

    // 長さ枠が改行変換で壊れるのを止める。プロトコルを 1 バイトでも流す前に行う。
    presentation::set_stdio_binary_mode();

    return hengband_core_protocol_main(presentation::transport_stdin(), presentation::transport_stdout(), log_path, args);
}

#endif /* _WIN32 */

#if !defined(_WIN32)

/*!
 * @brief dlsym 用の C 入口。
 * @details UI（libmain.so）はコアを直結せず、コア選択で選ばれた .so（libhengcore.so /
 * libtangcore.so）からこの名前を引く（`hd2d_entry_android.cpp` の resolver）。
 * C++ の飾り名だと dlsym で引けないための包みで、中身は共有本体そのもの。
 */
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    return hengband_core_protocol_main(in, out, protocol_log_path, forwarded_args);
}

#endif /* !_WIN32 */
