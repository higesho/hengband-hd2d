/*!
 * @file sq_main.cpp
 * @brief `SilCore.exe` の入口（プロトコル v1 の core 側）。**P2 本実装**。
 *
 * 基準は Sil-Q コアの設計（§3 / §8 P2 の受け入れ）と、
 * プロトコル v1 の仕様
 * （§3 握手 / §4 カタログ / §5 UiSeam 対応 / §6.4 合体規則 / §8 pad_commands）。
 *
 * ## この TU の立ち位置
 * `platform/windows/core_main.cpp`（変愚コアの入口）と
 * `gensoband/adapter/gb_main.cpp`（幻想蛮怒コアの入口）の弟である。
 * 受信スレッド 1 本・送信 mutex・stdout はプロトコル専用・stderr がログ、という
 * 骨格をそのまま写した。違うのは中身の出どころだけ:
 *
 * | | 変愚 | Sil-Q |
 * |---|---|---|
 * | 画づくり | `presentation::Bridge::capture` | `sq::capture_frame`（`sq_shim.h` 越し） |
 * | Term 待ち | `SdlNullTerm::on_event` | `sq_null_term.c` の `TERM_XTRA_EVENT` |
 * | ゲームを回す | `presentation::run_sdl_game` | `sq_run_game()` |
 *
 * ## 幻想蛮怒版との違い（どれも「あちらに要ったものが要らない」側）
 * - **文字コード変換が無い**（Sil-Q は純 ASCII。設計 §3.1）
 * - **タイトルとセーブ選択を自作しない**（Sil-Q の `initial_menu()` に任せる。§3.3）
 * - **`--savefile=` が無い**（枠の決め方はコアの中にある。代わりに `--resume` だけ持つ
 *   ——直前に保存した人物を自動で開く。言語切り替えの立て直しのため。追補 A2）
 *
 * ## Sil-Q のヘッダは見ない
 * ここは C++ の TU なので、触ってよいのは `sq_shim.h`（純 C ABI）だけ（設計 §3）。
 * `angband.h` を include してはいけない。
 *
 * ## stdout / stderr
 * stdout は**プロトコル専用**。人間向けの出力は全部 stderr（v1 §1.1）。
 */

#include "sq_shim.h"

#include "sq_frame.h"
#include "sq_lang.h"
#include "sq_manifest.h"
#include "sq_pad_commands.h"
#include "sq_text.h"
#include "sq_sub_panels.h"

#include "bridge/input_event_adapter.h"
#include "frame/frame_codec.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"

#include "portable/legacy_os.h"

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

/*! 主 Term の大きさ。設計 §3.2 で M0 は 80x27 固定。 */
constexpr int kTermCols = 80;
constexpr int kTermRows = 27;

//! v1 §1.1 の起動引数。これが無い core は「単体で動く従来版」を名乗る立場に無い。
constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
//! v1 §11.2 の会話ログ。既定 OFF。
constexpr std::string_view kProtocolLogArg = "--protocol-log=";
//! 起動列だけ走らせて畳む（設計 §8 P1 の受け入れ）。
constexpr std::string_view kSelfTestArg = "--selftest";
/*!
 * @brief 名前の組み立てを並べて出す。
 * @details `--selftest` と同じ立場の**診断の口**で、遊びの筋には入らない。
 * 銘つきの品や風味つきの器に実際に出会うのを待つと何百手も掛かるので、
 * 型ごとに 1 つ作って見る。P4（説明文）と P5（画面）でも使う。
 */
constexpr std::string_view kNameCheckArg = "--name-check";
//! lib の置き場を明示する（既定は `<exe_dir>/silq/lib`）。検証用の抜け道。
constexpr std::string_view kLibArg = "--lib=";
/*!
 * @name 日本語化
 * @details `--lang=` があれば**それが勝つ**（`ui_state` を待たない）。
 * 駆動器（`tools/silq/sq_protocol_driver.py`）と `--selftest` はこれを使う。
 * @{
 */
constexpr std::string_view kLangArg = "--lang=";
//! 引けなかった鍵を書き出す（設計 §4「未訳の見える化」）。
constexpr std::string_view kReportUntranslatedArg = "--report-untranslated=";
/*!
 * @brief 直前に保存した人物を自動で開く（言語切り替えの立て直し）。
 * @details 詳細は `sq_shim.h` の `sq_set_resume()`。
 */
constexpr std::string_view kResumeArg = "--resume";
//! 最初の `ui_state` を待つ上限（設計 §3.3）。時間切れなら英語で立てる。
constexpr int kUiStateWaitMs = 2000;
/*! @} */

/* ============================================================ 会話ログ（v1 §11.2） */

/*!
 * @brief 全送受信メッセージを JSON Lines で残す。`core_main.cpp` の写し。
 * @details 1 行 = `{"time","dir","t","len","payload"}`。**`frame` も全文残す**。
 */
class ProtocolLog {
public:
    void open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb");
        if (this->fp_ == nullptr) {
            std::fprintf(stderr, "[silq] could not open the protocol log: %s\n", path.c_str());
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

/*!
 * @brief 受信キュー。**中身は CP932 のバイト**である（設計 §3.1）。
 * @note 2026-08-22 まで「ASCII のバイト」だった。日本語入力（A1-3）を通したので、
 * 2 バイト文字が 2 つの要素として並ぶ——読む側（`sq_null_term.c` の `TERM_XTRA_EVENT`）は
 * 1 バイトずつ渡すだけで、組み立てるのは `askfor_aux()` の仕事である。
 */
std::mutex g_inbox_mutex;
std::deque<int> g_key_queue;
/*!
 * @brief 線（UTF-8）→ コア（CP932）の復号器。**変換の家はここ 1 つ**（設計 §3.1）。
 * @details `g_inbox_mutex` の下でだけ触る（`enqueue_keys()` の中）。
 */
sq::SqKeyDecoder g_key_decoder;
//! 捨てたキーの数（何度も言わないための数え上げ）。
unsigned long g_dropped_keys = 0;

//! 直近の `ui_state`。視界の大きさとカメラの追従に効く（v1 §7）。
presentation::UiStateMessage g_ui_state;
bool g_ui_state_dirty = false;

/*!
 * @name 最初の `ui_state`（設計 §3.3）
 * @details 日本語で `edit` を読ませるには、**`sq_bootstrap()` より前**に言語が
 * 決まっていなければならない。画面側は握手の直後に必ず 1 回送ってくる
 * （`hd2d/app/hd2d_app.cpp:10332` の「握手直後に必ず 1 回」）ので、それを待つ。
 * 待つのは**起動列の 1 度だけ**で、以後この旗は見ない。
 * @{
 */
std::atomic<bool> g_first_ui_state_seen{ false };
//! 最初の `ui_state` が申告した言語。空なら申告なし＝英語。
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
        std::fprintf(stderr, "[silq] send \"%s\" failed: %s\n", type.c_str(), err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    g_log.record("out", type, payload);
    return true;
}

//! `fatal` を送って（送れる状態なら）理由を stderr にも残す。v1 §9.3。
void send_fatal(const std::string &reason)
{
    std::fprintf(stderr, "[silq] fatal: %s\n", reason.c_str());
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
 * @brief `input_event` をキー列へ展開する。**`set_number` だけ Sil-Q の作法で書く。**
 *
 * @details 共用の `append_input_event_keys()` は `set_number` を
 * 「`KTRL('E')`（行末へ）→ `0x08` を 10 個 → 0 詰めの数字」に展開する
 * （`input_event_adapter.cpp`）。前の 2 つは変愚の `askfor` の編集規約である。
 *
 * **Sil-Q の `askfor_aux()`（`util.c:2913`）に `KTRL('E')` は無い。**
 * 印字文字でない鍵は `bell("Illegal edit key!")` へ落ち、`bell()` は最後に
 * `flush()` を呼ぶ（`util.c:2073`）。`flush()` は `inkey_xtra` を立て、次の
 * `inkey()` が `Term_flush()` で**溜まっている鍵を全部捨てる**——つまり後ろに
 * 続く `0x08` と数字がまるごと消え、**個数がいつまでも変わらない**。
 *
 * 消すだけでよい。Sil-Q の編集器は `k = 0` から始まり、**最初の印字文字が
 * 既定値を切り落とす**（`buf[k++] = ch` の後 `buf[k] = '\0'`）ので、
 * 行末へ行く必要がそもそも無い。`0x08` は素直に 1 文字消す（`case '\010'`）。
 *
 * 共用 TU は 1 バイトも変えない（必守制約 3）。**ここで枝を分ける。**
 */
void append_keys_for_silq(const presentation::InputEventsMessage &message, std::vector<int> &keys)
{
    presentation::InputEventsMessage shared; // `set_number` 以外はそのまま共用 TU へ
    for (const auto &event : message.events) {
        if (event.e != "set_number") {
            shared.events.push_back(event);
            continue;
        }
        //! **順序を崩さない**。ここまでに溜めたぶんを先に展開してから割り込む。
        presentation::append_input_event_keys(shared, keys);
        shared.events.clear();

        std::string text = std::to_string(event.number);
        while (static_cast<int>(text.size()) < event.digits) {
            text.insert(text.begin(), '0');
        }
        for (int i = 0; i < 10; ++i) {
            keys.push_back(0x08); // 全消し（`askfor_aux` の編集規約。7 桁までしか入らない）
        }
        for (const char c : text) {
            keys.push_back(static_cast<unsigned char>(c));
        }
    }
    presentation::append_input_event_keys(shared, keys);
}

/*!
 * @brief 線から来たキーを**内部コードへ直して**キューへ積む（A1-3。2026-08-22）。
 *
 * @details 線の上は UTF-8、コアは CP932。**変換はここ 1 か所**（設計 §3.1）で、
 * 変愚が `sdl_null_term.cpp` の `convert_text_input_to_system_encoding` でやっているのと
 * 同じ位置づけである。
 *
 * **2026-08-22 まではここで 0x80 以上を捨てていた。** 捨てているかぎり日本語は
 * 1 文字も入らないので、銘（`{`）も地形の記憶（`:`）も英語しか打てなかった。
 *
 * @note **名前だけは日本語を通さない**（決めたこと。設計 §12.5 の問 A-2 の案 c）。
 * その門はここではなく `askfor_name()` の側にある——**セーブファイルの綴りが
 * 潰れる**からで、理由は向こうの `//SQ:` の註記に書いてある。
 */
void enqueue_keys(const std::vector<int> &raw)
{
    std::vector<int> converted;
    converted.reserve(raw.size());

    std::lock_guard<std::mutex> lock(g_inbox_mutex);
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
                std::fprintf(stderr, "[silq] receive stopped: %s%s%s\n",
                    presentation::transport_result_name(result), err.empty() ? "" : " - ", err.c_str());
            } else {
                std::fprintf(stderr, "[silq] stdin closed (the ui is gone)\n");
            }
            g_ui_gone.store(true);
            return;
        }

        const std::string type = presentation::peek_message_type(payload);
        g_log.record("in", type, payload);

        if (type == "keys") {
            presentation::KeysMessage message;
            if (!presentation::decode_keys(payload, message, err)) {
                std::fprintf(stderr, "[silq] bad \"keys\": %s\n", err.c_str());
                continue;
            }
            enqueue_keys(message.keys);
            continue;
        }
        if (type == "input_event") {
            /*
             * v1 §8.3。**画面側（hd2d）が実際に送ってくるのはこちら**である
             * （`keys` の送り手は試験の駆動器だけ）。展開器は変愚と**同じ TU を
             * そのまま**使う（`input_event_adapter.cpp` は `frame/protocol_messages.h`
             * しか見ないので、コアに依らない）。
             */
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) {
                std::fprintf(stderr, "[silq] bad \"input_event\": %s\n", err.c_str());
                continue;
            }
            std::vector<int> keys;
            append_keys_for_silq(message, keys);
            enqueue_keys(keys);
            continue;
        }
        if (type == "ui_state") {
            presentation::UiStateMessage message;
            if (!presentation::decode_ui_state(payload, message, err)) {
                std::fprintf(stderr, "[silq] bad \"ui_state\": %s\n", err.c_str());
                continue;
            }
            /*
             * push-latest（v1 §7）。**適用はゲームスレッド**（`host_present`）で行う。
             * ここで `sq::set_view_size()` を直に呼ぶと、地図を切り出している最中に
             * 窓の大きさが変わりうる（P3 で効く話だが、形は先に正しくしておく）。
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
            std::fprintf(stderr, "[silq] quit_request received\n");
            continue;
        }
        // 未知の種別は黙って捨てる（v1 §2.3）。
    }
}

/* ================================================================ 合体規則（§6.4） */

/*!
 * @brief 合体判定に使う鍵（＝`frame_id` を抜いたペイロード）。`core_main.cpp` の写し。
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

//! 表を送ったか（v1 §8）。**Sil-Q に配列の切り替えは無い**ので送り直しも無い。
bool g_tables_sent = false;

//! タイル目録（設計 §5）。起動時に 1 回読んで、以後 `sq_frame` が索引に使う。
sq::TileManifest g_manifest;

/*!
 * @brief 初期化完了時の表（v1 §8 / §8.1）。
 * @details `sub_panel_kinds` は **M0.5 ② で中身が入った**。
 * かつては空で送っていて、そのとき画面側は**当てずっぽうで番号を動かさない**
 * （`feature_menu.cpp:1164`）ので、機能メニューで種類を選べなかった。
 * `asset_manifest` は **地形だけ**の目録である（設計 §5）
 * ——実体の絵は M2（要件 R4）で、それまではアスキー実体モードが受ける。
 */
void send_startup_tables()
{
    if (g_tables_sent) {
        return;
    }
    g_tables_sent = true;
    (void)send_message("pad_commands", presentation::encode_pad_commands(sq::build_pad_commands()));
    (void)send_message(
        "sub_panel_kinds", presentation::encode_sub_panel_kinds(sq::build_sub_panel_kinds()));
    if (!g_manifest.message().assets.empty()) {
        (void)send_message("asset_manifest", presentation::encode_asset_manifest(g_manifest.message()));
    }
}

/* ==================================================== ホストのフック（v1 §5 の対応） */

//! 直近の安い digest（`sq_change_digest`）。capture を丸ごと省くための門。
unsigned long long g_last_digest = 0;
bool g_has_last_digest = false;

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
    sq::set_view_size(state.view_w, state.view_h);
    sq::set_camera_follow_player(state.camera_follow_player);
    /*
     * **効果音を誰が鳴らすか**。Sil-Q は自分では
     * 鳴らせない（音の実体を持たない）ので、真なら `frame.sounds` へ書き留め、
     * 偽なら何もしない＝無音になる。**`use_sound` は落とさない**——コアの `sound()` は
     * あれが偽だと `Term_xtra` すら呼ばず、書き留める機会ごと消える
     * （`sq_bootstrap.c` で立てたまま）。
     */
    sq_sound_set_wanted(state.sound_events ? 1 : 0);
    /*
     * サブパネルの希望（v1 §7。M0.5 ②）。**枚数は画面と揃っている**
     * （`kProtocolSubPanelCount` ＝ `kSubPanelCount` ＝ `SQ_SUB_PANELS` ＝ 7）。
     * `has_sub_panel_kinds` が偽なら種類は触らない——キーごと省かれたときに
     * 「全部 UI 既定」で塗り潰さないため。
     */
    for (int i = 0; i < presentation::kProtocolSubPanelCount; ++i) {
        sq::set_sub_panel_cells(i, state.sub_panel_cells[i].cols, state.sub_panel_cells[i].rows);
        if (state.has_sub_panel_kinds) {
            sq::set_sub_panel_kind(i, state.sub_panel_kinds[i]);
        }
    }
    return true;
}

//! 品書きを出だしから見せる仕掛けを済ませたか（`sq_prime_item_list_view`）。
bool g_item_list_primed = false;

void host_present()
{
    /*
     * @ が出来た（または読み込めた）直後に **1 度だけ**、品書きを出す側へ倒す。
     * 理屈は `sq_shim.h` の `sq_prime_item_list_view()` の註記——これが無いと
     * `w` を素で押したとき、コントローラーには選ぶ手立てが 1 つも無い。
     */
    if (!g_item_list_primed && (sq_character_generated() != 0)) {
        g_item_list_primed = true;
        sq_prime_item_list_view();
        /*
         * サブパネルの割り当ても当て直す（M0.5 ②）。**セーブの読み込みが
         * `op_ptr->window_flag[]` を丸ごと書き戻した後**なので、ここで戻さないと
         * 画面側の割り当てが起動のたびにセーブの値に負ける（`sq_sub_panels.h`）。
         */
        sq::reset_sub_panel_apply();
    }

    /*
     * **安い門**。`host_present` は待ちの間も 10ms ごとに回るので、変わっていない
     * フレームを毎回組み直して比べるのは割に合わない。Term の画・`turn`・@ の状態から
     * digest を採り、変わっていなければ capture ごと省く。
     * 下の払い出し済みペイロード比較（v1 §6.4）は**そのまま残す**——digest は
     * 衝突しうるが、こちらは厳密だから。
     */
    const bool view_changed = apply_ui_state();
    const unsigned long long digest = sq_change_digest();
    if (g_has_last_digest && !view_changed && (digest == g_last_digest)) {
        return;
    }
    g_last_digest = digest;
    g_has_last_digest = true;

    const GameFrame frame = sq::capture_frame(++g_frame_id);
    const std::string wire = presentation::encode_game_frame(frame);
    std::string key = frame_compare_key(wire);
    if (g_has_last_frame && (key == g_last_frame_key)) {
        return; // 前回送信と同じ内容。送らない（v1 §6.4）
    }
    g_last_frame_key = std::move(key);
    g_has_last_frame = true;
    (void)send_message("frame", wire);
}

/*!
 * @brief スクリプトでキーを流し込む口（検証用）。
 *
 * @details **変愚の `HENGBAND_SDL2_INJECT_FILE` と同じ書式**である。基準は
 * `presentation/term/sdl_null_term.cpp` の `ScriptedKeyInjector`——あちらは共用の木で、
 * Sil-Q から触らない約束（設計 §1 の制約）なので**書式だけ写した**。
 * 書式を変えるときは両方を直すこと（片方だけ直すとスクリプトが黙って別の意味になる）。
 *
 * | 環境変数 | 内容 |
 * |---|---|
 * | `SILQ_INJECT_KEYS` | スクリプトそのもの |
 * | `SILQ_INJECT_FILE` | スクリプトの入ったファイル（`#` から行末は註記。空白と改行は落とす） |
 *
 * | 綴り | 意味 |
 * |---|---|
 * | `\r` `\n` `\e` `\t` `\s` | Enter / 改行 / ESC / Tab / 空白 |
 * | `\^A`〜`\^Z` | Ctrl+A〜Ctrl+Z |
 * | `\xHH` | 生のバイト |
 * | `\.` | **キーを積まずに約 0.25 秒待つ**（`-more-` や演出の後で使う） |
 * | `\\` | `\` そのもの |
 *
 * **なぜ要るか**: 新 HD2D の窓は外から `WM_CHAR`（印字文字）しか受け取れず、
 * Enter も ESC も送れない。Sil-Q のタイトルはセーブ名の確定に Enter が要るので、
 * スクリプトが無いと**外から遊びに入れない**（2026-08-21 に実際に詰まった）。
 */
class ScriptKeys {
public:
    //! 環境変数から読む。**1 度だけ**呼ぶ（`sq_run_game()` の前）。
    void load()
    {
        if (const char *const inline_script = std::getenv("SILQ_INJECT_KEYS")) {
            this->parse(inline_script, false);
        }
        const char *const path = std::getenv("SILQ_INJECT_FILE");
        if ((path != nullptr) && (path[0] != '\0')) {
            std::FILE *fp = std::fopen(path, "rb");
            if (fp == nullptr) {
                std::fprintf(stderr, "[silq] inject: cannot open %s\n", path);
            } else {
                std::string body;
                char buf[1024];
                while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
                    std::string line = buf;
                    /* `#` から行末までは註記。スクリプトへ `#` を積むなら `\x23`。 */
                    const std::string::size_type hash = line.find('#');
                    if (hash != std::string::npos) {
                        line.erase(hash);
                    }
                    body += line;
                }
                std::fclose(fp);
                this->parse(body.c_str(), true);
            }
        }
        if (!this->keys_.empty()) {
            std::fprintf(stderr, "[silq] inject: %u key(s) scripted\n",
                static_cast<unsigned>(this->keys_.size()));
        }
    }

    /*!
     * @brief 次の 1 個。
     * @return 積むキー、または 0（待ちの最中／打ち止め＝以降は人間の操作を待つ）
     */
    int next()
    {
        if (this->done()) {
            return 0;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < this->wait_until_) {
            return 0;
        }
        const int key = this->keys_[this->next_++];
        if (key == kWaitToken) {
            this->wait_until_ = now + std::chrono::milliseconds(kWaitMs);
            return 0;
        }
        return key;
    }

    bool done() const { return this->next_ >= this->keys_.size(); }

private:
    //! `\.` 1 個の待ち。変愚は 10ms x 25 周で数えるが、こちらは**時刻で測る**
    //! （Sil-Q の空回りの速さに依らないようにするため。意味は同じ 0.25 秒）。
    static constexpr int kWaitMs = 250;
    static constexpr int kWaitToken = -1;

    void parse(const char *script, bool strip_space)
    {
        for (std::size_t i = 0; script[i] != '\0'; ++i) {
            const char c = script[i];
            if (c != '\\') {
                if (strip_space && ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))) {
                    continue;
                }
                this->keys_.push_back(static_cast<unsigned char>(c));
                continue;
            }
            const char esc = script[i + 1];
            if (esc == '\0') {
                break;
            }
            ++i;
            switch (esc) {
            case 'r': this->keys_.push_back(0x0D); break;
            case 'n': this->keys_.push_back(0x0A); break;
            case 'e': this->keys_.push_back(0x1B); break;
            case 't': this->keys_.push_back(0x09); break;
            case 's': this->keys_.push_back(' '); break;
            case '.': this->keys_.push_back(kWaitToken); break;
            case '^': {
                const char ctrl = script[i + 1];
                if ((ctrl >= 'A') && (ctrl <= 'Z')) {
                    this->keys_.push_back(ctrl - 'A' + 1);
                    ++i;
                } else if ((ctrl >= 'a') && (ctrl <= 'z')) {
                    this->keys_.push_back(ctrl - 'a' + 1);
                    ++i;
                }
                break;
            }
            case 'x': {
                int value = 0;
                int digits = 0;
                while (digits < 2) {
                    const char h = script[i + 1];
                    int d = -1;
                    if ((h >= '0') && (h <= '9')) {
                        d = h - '0';
                    } else if ((h >= 'a') && (h <= 'f')) {
                        d = h - 'a' + 10;
                    } else if ((h >= 'A') && (h <= 'F')) {
                        d = h - 'A' + 10;
                    }
                    if (d < 0) {
                        break;
                    }
                    value = (value * 16) + d;
                    ++i;
                    ++digits;
                }
                if ((digits > 0) && (value != 0)) {
                    this->keys_.push_back(value);
                }
                break;
            }
            default: this->keys_.push_back(static_cast<unsigned char>(esc)); break;
            }
        }
    }

    std::vector<int> keys_;
    std::size_t next_{ 0 };
    std::chrono::steady_clock::time_point wait_until_{};
};

ScriptKeys g_script;

int host_next_key()
{
    {
        std::lock_guard<std::mutex> lock(g_inbox_mutex);
        if (!g_key_queue.empty()) {
            const int key = g_key_queue.front();
            g_key_queue.pop_front();
            return key; // **人（UI）のキーが先**。スクリプトは空いているときだけ流す
        }
    }
    return g_script.next();
}

void host_drop_keys()
{
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (!g_key_queue.empty()) {
        std::fprintf(stderr, "[silq] FLUSH drops %u key(s)\n",
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
 * @brief 自 exe のあるディレクトリ。**cwd には依らない。**
 * @details 画面側が `SilCore.exe` をどこから起こしても lib を見失わないため。
 */
std::string exe_dir()
{
    // Windows は `GetModuleFileNameA` の親、Android は cwd（`legacy_os.h` の註）。
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

//! 地形 → 絵の対応表（設計 §5）。
std::string terrain_map_path()
{
    return under_exe("silq", "tilework", "terrain_map.csv");
}

/*!
 * @brief Sil-Q の lib の場所。`<exe_dir>/silq/lib`（設計 §2）。
 * @details **変愚の `lib/` とは交差しない**（§1 制約 6）。セーブもスコアもここに閉じる。
 */
std::string silq_lib_dir()
{
    return under_exe("silq", "lib");
}

void print_usage()
{
    std::fprintf(stderr,
        "%s (%s) speaks the core protocol v%d.\n"
        "\n"
        "  usage: SilCore.exe %s [--protocol-log=<path>] [--lib=<dir>]\n"
        "                      [--lang=ja|en] [--report-untranslated=<path>]\n"
        "                      [--resume]\n"
        "         SilCore.exe --selftest [--lang=ja]\n"
        "\n"
        "  --selftest   read lib, run init_angband() and print the version.\n"
        "  --name-check run init_angband() and print every assembled item and\n"
        " monster name (diagnostic; see 8).\n"
        "  --lib=       use this lib directory (default: <exe dir>\\silq\\lib).\n"
        "  --lang=      core language. without it the first \"ui_state\" decides\n"
        " (see section 3.3).\n"
        "  --resume     open the character the previous run saved and skip the\n"
        "               title menu. the frontend passes this when it restarts the\n"
        "               core to change language (see sq_shim.h, sq_set_resume).\n"
        "  --report-untranslated=  write every key the catalog could not\n"
        "               translate, ready to paste into messages.ja.txt.\n"
        "\n"
        "This executable is not meant to be started by hand. The frontend\n"
        "launches it as a child process and speaks the core protocol over\n"
        "stdin/stdout.\n",
        sq_core_name(), sq_core_version(), presentation::kProtocolVersion, kUiProtocolArg.data());
    std::fflush(stderr);
}

/*!
 * @brief 訳文カタログの置き場を組む。`<lib_dir>/../lang/<lang>`（設計 §2 の配置）。
 * @details 場所を渡す引数は**足さない**。`--lib=` を動かせば付いて動く。
 */
std::string lang_dir_for(const std::string &lib_dir, const std::string &lang)
{
    std::string base = lib_dir;
    while (!base.empty() && ((base.back() == '\\') || (base.back() == '/'))) {
        base.pop_back();
    }
    const std::size_t cut = base.find_last_of("\\/");
    if (cut == std::string::npos) {
        return std::string();
    }
    return base.substr(0, cut + 1) + "lang" + portable::kPathSep + lang;
}

/*!
 * @brief 言語を確定してカタログを読む（設計 §3.3 の起動列 6）。
 * @details **`sq_bootstrap()` より前に呼ぶこと。** 読めなくても止めない
 * ——名前は `lib-ja/edit` から来るので、カタログが空でも日本語は出る（設計 §5 の註）。
 */
void apply_language(const std::string &lib_dir, const std::string &lang, const std::string &report_path)
{
    const std::string dir = lang_dir_for(lib_dir, lang.empty() ? std::string("en") : lang);
    const sq::LangLoadReport rep = sq::lang_init(lang, dir, report_path);
    if (!rep.enabled) {
        std::fprintf(stderr, "[silq] language = en (the japanese layer is off)\n");
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr, "[silq] language = ja, catalog = %s\n", dir.c_str());
    std::fprintf(stderr, "[silq] catalog: %d entry(ies), %d silent, %d rejected, %d not-in-cp932\n",
        rep.entries, rep.silent, rep.rejected, rep.not_in_cp932);
    if (!rep.error.empty()) {
        /* **黙って英語にしない。** 読み込みの失敗は必ず出す（設計 §5）。 */
        std::fprintf(stderr, "[silq] catalog problem: %s\n", rep.error.c_str());
    }
    std::fflush(stderr);
}

/*!
 * @brief `--selftest`。起動列を通し、通ったことの証拠を出す。
 * @return プロセスの終了コード
 *
 * **フックを差さない**ので、null term は「ESC を積むだけ」で回る。
 * パイプもプロトコルも要らない（設計 §8 P1 の受け入れを壊さないこと）。
 */
int run_selftest(const std::string &lib_dir, const std::string &lang, const std::string &report_path,
    bool name_check)
{
    if (lib_dir.empty()) {
        std::fprintf(stderr, "[silq] cannot resolve the exe directory\n");
        return 1;
    }

    std::fprintf(stderr, "[silq] lib = %s\n", lib_dir.c_str());
    std::fflush(stderr);

    apply_language(lib_dir, lang, report_path);

    int rc = sq_term_install(kTermCols, kTermRows);
    if (rc != SQ_OK) {
        std::fprintf(stderr, "[silq] sq_term_install failed (%d)\n", rc);
        return 1;
    }

    rc = sq_bootstrap(lib_dir.c_str(), lang.c_str());
    if (rc != SQ_OK) {
        std::fprintf(stderr, "[silq] bootstrap failed (%d): %s\n", rc, sq_last_error());
        sq_term_remove();
        return 1;
    }

    int cols = 0;
    int rows = 0;
    (void)sq_term_size(&cols, &rows);

    /* 23 行目 = `note()` の書き込み先（`init2.c:1587`）。 */
    char line[512];
    const int cells = sq_term_row(23, line, nullptr, static_cast<int>(sizeof(line)));
    std::fprintf(stderr, "[silq] term %dx%d, row23(%d) = \"%s\"\n",
        cols, rows, cells, (cells > 0) ? line : "");

    if (name_check) {
        sq_name_check();
    }

    /*
     * **画面が読む旗が digest に混ざっているか**（`sq_digest_flag_check()` の註記）。
     * 混ざっていないと旗が真のフレームが 1 枚も送られず、しかも**絵は正常に見える**。
     * ここで落としておかないと、実機で「A が効かない」まで分からない。
     */
    {
        char missing[64] = { 0 };
        if (sq_digest_flag_check(missing, static_cast<int>(sizeof(missing))) != 0) {
            std::fprintf(stderr,
                "[silq] **FAIL** digest に混ざっていない旗があります: %s"
                "（`sq_change_digest()` に足すこと）\n",
                missing);
            sq_term_remove();
            return 1;
        }
        std::fprintf(stderr,
            "[silq] digest flags: text_input_active / awaiting_command / panel_wx / locate_active OK\n");
    }

    sq_term_remove();

    /* 受け入れの合図（設計 §8 P1 / §9-1）。 */
    std::fprintf(stderr, "[silq] %s\n", sq_core_version());
    std::fflush(stderr);

    return 0;
}

/* ==================================================================== 本体（P2） */

/*!
 * @brief プロトコルを回す本体。**輸送路は呼び手が決める。**
 * @details Windows は `main()` が標準入出力を渡す（`SilCore.exe` は画面側の子プロセス）。
 * Android はコアが同じプロセスの別スレッドなので、入口（`hengband_core_entry`）が
 * `pipe()` の両端を渡す。ここから下は**どちらでも同じ道**を通る。
 */
int run_protocol(presentation::TransportHandle in, presentation::TransportHandle out,
    const std::string &log_path, const std::string &lib_dir,
    const std::string &lang_arg, const std::string &report_path, bool resume)
{
    g_in = in;
    g_out = out;
    if ((g_in == nullptr) || (g_out == nullptr)) {
        std::fprintf(stderr, "[silq] no transport endpoint; cannot speak the protocol\n");
        return 3;
    }
    if (!log_path.empty()) {
        g_log.open(log_path);
    }

    /* --- 握手（v1 §3.1）。最初のメッセージは hello でなければならない。 --- */
    std::string payload;
    std::string err;
    const presentation::TransportResult got = presentation::read_message(g_in, payload, err);
    if (got != presentation::TransportResult::Ok) {
        std::fprintf(stderr, "[silq] failed to read the first message: %s%s%s\n",
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
    std::fprintf(stderr, "[silq] hello from %s %s (protocol %d)\n",
        hello.ui_name.empty() ? "<unnamed ui>" : hello.ui_name.c_str(),
        hello.ui_version.empty() ? "?" : hello.ui_version.c_str(), hello.protocol);

    /*
     * 目録は**握手より前に**読む。`hello_ack` の申告と対になっているものなので、
     * 片方だけ申告する状態を作らない。
     */
    const std::string csv = terrain_map_path();
    if (!g_manifest.load(csv, exe_dir())) {
        /* 致命ではない——地形の絵が出ないだけで遊べる。**捏造せずに言う**。 */
        std::fprintf(stderr, "[silq] terrain manifest unavailable: %s\n", g_manifest.error().c_str());
    } else {
        std::fprintf(stderr, "[silq] terrain manifest: %u entries from %s (%d png missing)\n",
            static_cast<unsigned>(g_manifest.message().assets.size()), csv.c_str(),
            g_manifest.missing_files());
        /*
         * 実体の絵（M2）。**在るぶんだけ**足す——1 枚も無ければ 0 で、従来どおり
         * アスキー実体で遊べる。数は必ず出す（黙って 0 枚だと気づけない）。
         */
        const int entities = g_manifest.add_entities("silq/tilework/128", exe_dir());
        std::fprintf(stderr, "[silq] entity tiles: %d (R/K/P from silq/tilework/128), %d alias(es)\n",
            entities, g_manifest.alias_count());
        sq::set_tile_manifest(&g_manifest);
    }

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = sq_core_name();
    ack.core_version = sq_core_version();
    ack.features.emplace_back("lang-restart");
    ack.features.emplace_back("audio");
    /*
     * `features`（v1 §3.1）。リアルタイム進行は名乗らない——変愚だけの機能なので、
     * 名乗らなければ画面側のその節が自然に隠れる。
     *
     * **`audio` は名乗る**（2026-08-31）。意味は移設後に変わっている
     * ——かつては「コアが自分で鳴らせる」だったが、
     * いまは「**音の出来事を出せる**」である。Sil-Q は音の実体を持たないが、
     * `frame.sounds` へ名前とマスを載せるので画面側が鳴らせる。名乗らないと
     * 機能メニューの「音」の節が入口から消え、利用者が音量も入切も触れなくなる。
     *
     * **`lang-restart` だけ名乗る**（2026-08-23。追補 A2）。意味は
     * 「言語を替えるには**このコアを起こし直す**必要がある」で、画面側はこれを見て
     * 保存 → 終了 → 起こし直し（`--resume`）を回す。Sil-Q の言語は
     * `init_angband()` に入る前に決まっていなければならないためである（設計 §12.2 の案 A）。
     * **画面側にコア名の分岐は要らない**（必守制約 1）——名乗ったコアだけが立て直る。
     *
     * `asset_roots.graf` は**出さない**（8/16px の面を持たない）。
     *
     * `slab` は**出す**（M2。2026-08-21）——Sil-Q 固有の板が `silq\assets\slab` に
     * 在る。画面側は［申告された置き場 → 既定の置き場］の 2 段で探すので、
     * 申告しておけば Sil-Q の板が先に当たり、無い名前は従来どおり既定へ落ちる。
     */
    ack.asset_root_slab = under_exe("silq", "assets", "slab");
    if (!send_message("hello_ack", presentation::encode_hello_ack(ack))) {
        return 5;
    }

    /*
     * 受信スレッド（v1 §5 の「キー待ちブロックは core 内でパイプ読みに置き換わる」）。
     * detach する: 終了経路は `sq_run_game()` からの復帰か `quit()` で、join する場所が無い。
     */
    std::thread(receive_loop).detach();

    /*
     * --- 言語を決める（設計 §3.3）。**`sq_bootstrap()` より前でなければ意味がない。**
     *
     * `--lang=` があればそれが勝つ。無ければ最初の `ui_state` を待つ——画面側は
     * 握手の直後に必ず 1 回送ってくる（`hd2d/app/hd2d_app.cpp` の
     * 「握手直後に必ず 1 回（v1 §7）」）。送ってこない画面のために上限を置き、
     * 時間切れなら英語で立てる。
     *
     * **待った実測を stderr に出す**（P0 の受け入れ 1）。2000 ms は上限であって
     * 見込みではない。ここを測らずに「たぶんすぐ来る」で済ませない。
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
            std::fprintf(stderr, "[silq] first ui_state after %lu ms, lang = %s\n",
                static_cast<unsigned long>(waited), lang.empty() ? "(not declared)" : lang.c_str());
        } else {
            std::fprintf(stderr, "[silq] no ui_state within %lu ms; starting in english\n",
                static_cast<unsigned long>(waited));
        }
        std::fflush(stderr);
    } else {
        std::fprintf(stderr, "[silq] language forced by --lang=%s (not waiting for ui_state)\n",
            lang.c_str());
        std::fflush(stderr);
    }
    apply_language(lib_dir, lang, report_path);

    /* --- Term を立てて起動列を通す。ここから先の `Term_fresh` は frame になる。 --- */
    if (lib_dir.empty()) {
        send_fatal("cannot resolve the exe directory");
        return 1;
    }
    std::fprintf(stderr, "[silq] lib = %s\n", lib_dir.c_str());

    int rc = sq_term_install(kTermCols, kTermRows);
    if (rc != SQ_OK) {
        send_fatal("could not install the headless term");
        return 1;
    }
    /*
     * サブパネル用の Term 7 枚（M0.5 ②。`sq_sub_panels.h`）。**本線の後・起動列の前**に
     * 立てる——`init_angband()` より後だと、セーブから読んだ `window_flag` を
     * 受ける相手が居ないまま `window_stuff()` が走る。
     * 立てられなくても**遊びは続けられる**（サブパネルが空になるだけ）ので止めない。
     */
    if (sq_sub_terms_install() != SQ_OK) {
        std::fprintf(stderr, "[silq] sub terms not installed (the side panels stay empty)\n");
    }

    /* スクリプト（検証用）。**フックを差す前に**読む——読んだ数を先に言えるようにする。 */
    g_script.load();

    sq_host_hooks hooks{};
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.drop_keys = host_drop_keys;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    sq_set_host_hooks(&hooks);

    sq_set_resume(resume ? 1 : 0);

    rc = sq_bootstrap(lib_dir.c_str(), lang.c_str());
    if (rc != SQ_OK) {
        sq_set_host_hooks(nullptr);
        /*
         * **「起動に失敗した」と「起動中に ui が消えた」を取り違えない。**
         * 後者では null term の待ちが `sq_shutdown_and_quit()` → `quit(NULL)` を通り、
         * 起動の段の跳び先に落ちて `SQ_ERR_QUIT` に見える。ここで `fatal` を送ると
         * 「Sil-Q コアが壊れている」という嘘の記録が残る。
         */
        if (g_ui_gone.load() || g_quit_requested.load()) {
            std::fprintf(stderr, "[silq] shut down during startup (the ui went away)\n");
            send_exit_once(g_ui_gone.load() ? 1 : 0);
            g_log.close();
            return 0;
        }
        const std::string reason = sq_last_error();
        send_fatal(std::string("bootstrap failed: ") + (reason.empty() ? "(unknown)" : reason));
        sq_term_remove();
        g_log.close();
        return 1;
    }

    /* 初期化完了（v1 §8）。表はここで 1 回。 */
    send_startup_tables();

    /*
     * ゲームを回す。**戻ってくるのは終わったとき**。
     * タイトル（`initial_menu`）とセーブ選択は `sq_run_game()` の中＝**コア自身**が
     * 描く（設計 §3.3）。アダプタは何も出さない。
     */
    rc = sq_run_game();
    std::fprintf(stderr, "[silq] game finished (%d)\n", rc);

    sq_set_host_hooks(nullptr);

    if (g_dropped_keys != 0) {
        std::fprintf(stderr, "[silq] dropped %lu non-ascii key(s) during the session\n", g_dropped_keys);
    }

    if (rc == SQ_ERR_CORE) {
        /*
         * コアが `core()` を踏んだ＝続行不能。v1 §4.2 では `exit` ではなく
         * `fatal` を送る場面である（`exit` は「正常終了の直前」に限る）。
         */
        const std::string reason = sq_last_error();
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
 * （`platform/android/hd2d_entry_android.cpp` の resolver）。名前は 4 コアで共通である
 * ——**同時に開くのは 1 つ**（`RTLD_LOCAL` で開き、選ばれたコアだけを起こす）なので
 * ぶつからない。変愚の `platform/windows/core_main.cpp` の同名の包みと対になる。
 *
 * `argv` に当たるものは `forwarded_args`。**`--ui-protocol` の有無は見ない**
 * ——ここへ来ている時点でプロトコルで喋ると決まっている。見るのは `--lang=` と
 * `--resume` だけで、lib の place は `silq_lib_dir()`（Android では cwd の下）から取る。
 */
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    std::string lang;
    /*
     * **`--resume` もここで拾う**（追補 A2）。画面側は言語を替えるとき、同じコアを
     * 起こし直して `forwarded_args` へ `--resume` を積む（`hd2d_app.cpp:12423`）。
     * Windows は `main()` の引数解釈が拾うが、**Android の入口はここしか無い**
     * ——`#if !_WIN32` の中なので、抜けても Windows のビルドでは気づけない。
     */
    bool resume = false;
    for (const std::string &opt : forwarded_args) {
        if (opt.rfind(kLangArg, 0) == 0) {
            const std::string value = opt.substr(std::string_view(kLangArg).size());
            if ((value == "ja") || (value == "en")) {
                lang = value;
            }
            continue;
        }
        if (opt == kResumeArg) {
            resume = true;
        }
    }
    const int rc = run_protocol(in, out, protocol_log_path, silq_lib_dir(), lang, std::string(), resume);
    (void)sq::lang_write_report();
    return rc;
}

#endif /* !_WIN32 */

int main(int argc, char **argv)
{
    bool selftest = false;
    bool name_check = false;
    bool has_protocol_arg = false;
    bool resume = false;
    std::string log_path;
    std::string lib_dir = silq_lib_dir();
    //! 空 = 「最初の `ui_state` に従う」（設計 §3.3）。`--lang=` があればそれが勝つ。
    std::string lang;
    std::string report_path;

    for (int i = 1; i < argc; i++) {
        const std::string_view opt = argv[i];
        if (opt == kSelfTestArg) {
            selftest = true;
            continue;
        }
        if (opt == kNameCheckArg) {
            name_check = true;
            continue;
        }
        if (opt == kUiProtocolArg) {
            has_protocol_arg = true;
            continue;
        }
        if (opt == kResumeArg) {
            resume = true;
            continue;
        }
        if (opt.rfind(kProtocolLogArg, 0) == 0) {
            log_path = std::string(opt.substr(kProtocolLogArg.size()));
            continue;
        }
        if (opt.rfind(kLibArg, 0) == 0) {
            lib_dir = std::string(opt.substr(kLibArg.size()));
            continue;
        }
        if (opt.rfind(kLangArg, 0) == 0) {
            lang = std::string(opt.substr(kLangArg.size()));
            if ((lang != "ja") && (lang != "en")) {
                std::fprintf(stderr, "[silq] unknown language: %s (expected \"ja\" or \"en\")\n",
                    lang.c_str());
                return 1;
            }
            continue;
        }
        if (opt.rfind(kReportUntranslatedArg, 0) == 0) {
            report_path = std::string(opt.substr(kReportUntranslatedArg.size()));
            continue;
        }

        std::fprintf(stderr, "[silq] unknown argument: %s\n", argv[i]);
        print_usage();
        return 1;
    }

    if (selftest || name_check) {
        const int rc = run_selftest(lib_dir, lang, report_path, name_check);
        (void)sq::lang_write_report();
        return rc;
    }
    if (!has_protocol_arg) {
        print_usage();
        return 2;
    }

    // 長さ枠が改行変換で壊れるのを止める。プロトコルを 1 バイトでも流す前に行う。
    presentation::set_stdio_binary_mode();

    const int rc = run_protocol(presentation::transport_stdin(), presentation::transport_stdout(),
        log_path, lib_dir, lang, report_path, resume);
    /*
     * 未訳の書き出しは**ゲームの糸が終わってから**（`sq_lang.cpp` の「糸」の註）。
     * `run_protocol()` のどの出口を通っても 1 回は書けるよう、ここに置く。
     */
    (void)sq::lang_write_report();
    return rc;
}
