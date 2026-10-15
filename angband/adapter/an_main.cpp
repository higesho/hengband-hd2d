/*!
 * @file an_main.cpp
 * @brief `AngbandCore.exe` の入口（プロトコル v1 の core 側）。S1〜S3 を通して育ててきた
 * ファイル（見直しの指摘 11。以前の「S1 足場」という註記は、この後 S2・S3 の中身が
 * 全部ここへ足された時点で古くなっていた）。
 *
 * （S1〜S3 の受け入れ）。
 * 手本は `rogue_legacy/adapter/rl_main.cpp`（骨格）と `silq/adapter/sq_main.cpp`
 * （同じ z-term 系コアの握手・受信スレッド・setjmp/longjmp との接続）。
 *
 * ## いまここに揃っているもの
 * 握手・受信スレッド・`quit_request`・`exit`・`--selftest`（S1）、マップ・実体・
 * HUD・メッセージ（S2。設計 §6.2）、コマンド一覧・持ち物・確認・数量・名前・店・
 * サブパネル・`NativeModernMenu`（`g_native_menu`）を使うカーソル式ダイアログ（S3。
 * 設計 §6.3）。`frame.menu_open` が真の間（birth・セーブ選択・ゲーム内メニュー）は
 * Term の写し（`menu_term_lines`）だけを送り、偽の間（実際にダンジョン・町を
 * 歩いている間）は `an_frame.cpp` がマップ・HUD・メッセージを埋める——**両方を
 * 同時には送らない**（`menu_term_lines` は「icky 中の Term 全文ミラー」なので、
 * 地図を送る場面で埋めたままだと画面側が立体を描かない）。
 *
 * ## Angband のヘッダは見ない
 * ここは C++ の TU なので、触ってよいのは `an_shim.h` / `an_map_shim.h`（純 C ABI）と
 * `an_frame.h` / `an_manifest.h`（どちらも angband.h を見ない）だけ。`angband.h` は
 * `an_bootstrap.c` / `an_null_term.c` / `an_map_shim.c` の中に閉じる
 * （Sil-Q と同じ規約）。
 *
 * ## stdout / stderr
 * stdout は**プロトコル専用**。人間向けの出力は全部 stderr（v1 §1.1）。
 */

#include "an_frame.h"
#include "an_lang.h" /* */
#include "an_lang_c.h"
#include "an_manifest.h"
#include "an_map_shim.h" /* AH-04: an_native_owned_item() / an_kind_name_and_tval() */
#include "an_pad_shim.h"
#include "an_shim.h"
#include "an_targeting.h"
#include "an_ui_shim.h"

#include "bridge/input_event_adapter.h"
#include "bridge/native_inventory_menu.h" /* AH-04 */
#include "bridge/native_item_display_impl.h" /* AH-04: read_native_owned_items() */
#include "bridge/native_modern.h"
#include "bridge/native_store_menu.h" /* AH-41 */
#include "bridge/native_targeting_runtime.h"
#include "bridge/prompt_semantics.h" /* AH-10。 */
#include "bridge/rogue_sub_panels.h" /* サブパネルの種類（Rogue 系と同じ 5 種） */
#include "bridge/symbol_prompt.h" /* AH-10。 */
#include "frame/frame_codec.h"
#include "frame/message_more.h"
#include "frame/protocol_messages.h"
#include "frame/protocol_transport.h"

#include "portable/legacy_os.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

/*! 主 Term の大きさ。設計 §6.1 で S1 は 80x27 固定。 */
constexpr int kTermCols = 80;
constexpr int kTermRows = 27;

constexpr std::string_view kUiProtocolArg = "--ui-protocol=stdio";
constexpr std::string_view kSelfTestArg = "--selftest";
constexpr std::string_view kLibArg = "--lib=";
constexpr std::string_view kDataArg = "--data=";
//! v1 §11.2 の会話ログ。既定 OFF。手本は `silq/adapter/sq_main.cpp` の `ProtocolLog`
//! （あちらの註に「core_main.cpp の写し」とあるとおり、コアどうし共通の道具）。
constexpr std::string_view kProtocolLogArg = "--protocol-log=";
/*!
 * @name 日本語化
 * @details `--lang=` があれば**それが勝つ**（`ui_state` を待たない。駆動器と検査用）。
 * 無ければ最初の `ui_state.lang` を待つ（上限 `kUiStateWaitMs`。時間切れなら英語）。
 * @{
 */
constexpr std::string_view kLangArg = "--lang=";
//! Term へ書いた字のうちカタログに無かったものを書き出す（`E:`/`J:` の形。設計 §11）。
constexpr std::string_view kReportUntranslatedArg = "--report-untranslated=";
/*!
 * @brief 言語を替えたときの起こし直しで画面側が足す（`hd2d_app.cpp` の `lang-restart`）。
 * @details Angband はセーブ選択のメニューを必ず通るので、いまは受け付けて読み捨てる
 * （起動の失敗にしない）。直前のセーブを自動で開く作りは未着手（設計 §4 に追記）。
 */
constexpr std::string_view kResumeArg = "--resume";
//! 最初の `ui_state` を待つ上限（設計 §4。Sil-Q と同じ形）。
constexpr int kUiStateWaitMs = 1500;
/*! @} */

/* ================================================================ 会話ログ（v1 §11.2） */

/*!
 * @brief 送受信の全メッセージを JSON Lines で残す（`silq/adapter/sq_main.cpp` の
 * `ProtocolLog` の写し）。「応答待ちです」に固まる不具合（S3 見直し）を、
 * 画面側の `--protocol-log=` と突き合わせて調べるための道具。1 行 =
 * `{"time","dir","t","len","payload"}`。`payload` は届いた JSON そのもの
 * （`input_event` の `ui_action`・`frame` の `modern_ui` を含む全文）。
 */
class ProtocolLog {
public:
    void open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb");
        if (this->fp_ == nullptr) {
            std::fprintf(stderr, "[angband] could not open the protocol log: %s\n", path.c_str());
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
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
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

/* ================================================================ 送受信の仕組み */

presentation::TransportHandle g_in = nullptr;
presentation::TransportHandle g_out = nullptr;
std::mutex g_send_mutex;

std::atomic<bool> g_ui_gone{ false };
std::atomic<bool> g_quit_requested{ false };
std::atomic<bool> g_exit_sent{ false };

std::mutex g_inbox_mutex;
std::deque<int> g_key_queue;

/*!
 * @brief 「応答待ちです。操作を自動再送しません。」に固まる不具合の直し（見直しで特定）。
 * @details 根本原因: `g_native_menu.input()`（`receive_loop()` のスレッド）が
 * `ui_action` を受けて `consumed_=true` を立てても、**それを拾って
 * `frame.modern.revision`/`request_id`/`result` を更新した新しいフレームを
 * 組み直すのは `host_present()`（ゲームスレッド）の役目**——ところが
 * `host_present()` は `an_change_digest()`（Term の文字グリッドと手番・
 * `inkey_flag` 等だけを見る安いしきい値。見直しの指摘 6）が変わらない限り
 * `build_frame()` 自体を呼ばない。`an_change_digest()` は `g_native_menu` の
 * 存在を知らない（`an_null_term.c` 側にあり、`an_main.cpp` の `g_native_menu`
 * を見られない）ので、**「Term の絵は変わらないが `ui_action` の返事だけは
 * 新しく作らないといけない」瞬間を安いしきい値が黙って捨てていた**。
 * 実機の `--protocol-log=` で再現: `request_id=5` の `ui_action` は
 * `handled=1 new_keys=1` で確かに処理されたのに、`an_menu_epoch()` はそこで
 * 止まったまま（＝コアの中では次の画面へまだ進んでいない）、`request_id=6` も
 * 二度と来なかった——`host_present()` が一度も `build_frame()` を呼ばず、
 * `consumed_` を拾えなかったことの跡。
 *
 * 直しは、`ui_action` を 1 つ処理するたびにこの旗を立て、`host_present()` の
 * 安いしきい値を**その回だけ**バイパスする（`an_change_digest()` 自体は変えない
 * ——共用ではなく `an_null_term.c` の中の道具なので直せるが、Term に
 * 現れない変化まで拾おうとすると意味が薄れる。ここは `g_native_menu` を
 * 持つ `an_main.cpp` 側だけの事情なので、ここで閉じる）。
 *
 * **ただし、この旗だけでは順序が逆になっていた**（2026-09-23。3 回目の調査で
 * 実機の `--protocol-log=` から確認）。受信スレッドが翻訳したキーはまず
 * `g_key_queue` に入り、ゲームスレッドの `an_term_event()` は
 * 「`present()` → `an_drain_keys_into_term()` → 10ms 眠る」の順で回るので、
 * 旗を見た `present()` は**原作がそのキーを読む前**に走る。結果、`ui_action`
 * 1 つにつきフレームが 2 枚出ていた——1 枚目は `request_id=N, result=accepted`
 * なのに中身は**前の画面のまま**、2 枚目（1〜10ms 後）で初めて次の画面。
 * 画面側（`hd2d/ui/modern_store_ui.cpp` の `sync()`）は `request_id` が自分の
 * 送った番号と一致した最初のフレームで `pending_` を下ろすので、1 枚目で
 * 「操作は済んだ」と判じて古い画面のまま次の操作を受け付ける。その操作は
 * 古い `revision` を背負うため、コアで拒否される（キーが 1 つ消える）か、
 * 2 枚目の `capture()` より先に届けば古い画面の意味で翻訳されて次の画面へ
 * 渡る（画面側が見ていない画面へキーが届く）。共用の `NativeModernMenu` は
 * 「1 つの捕捉状態を消費できる操作は 1 つ」（`consumed_`）を前提に
 * しているが、早い 1 枚目がその状態を先に更新してしまい、前提が崩れていた。
 * 直しは `host_present()` の頭（待ち行列が空になるまで組まない）と
 * `receive_loop()`（翻訳と積むのを同じロックで一続きに）。
 */
std::atomic<bool> g_native_menu_dirty{ false };

/*!
 * @brief 新 UI のカーソル式ダイアログ（設計 §6.3）。手本は `silq/adapter/sq_main.cpp`
 * の `g_native_menu`——**画面が送ってくる `input_event`（`e == "ui_action"`）を
 * 実キーへ翻訳する側と、`build_frame()` で `frame.modern` を組む側の両方**で
 * この 1 個を共有する（`receive_loop()` が前者、`build_frame()` が後者。
 * 定義をここへ置くのは、`receive_loop()` の中からも参照するため）。
 * 直接 `hd2d/` を触らずに直す方法の詳細は `build_frame()` 直前の註記を参照。
 *
 * @details **排他**（見直しの指摘 4）: `input()` は受信スレッド（`receive_loop()`）、
 * `capture()` はゲームスレッド（`build_frame()` 経由の `host_present()`）から呼ばれる。
 * `NativeModernMenu` 自身にロックは無いので、呼び出し側の `g_native_menu_mutex` で
 * 両方を包む（`rogue_legacy/adapter/rl_main.cpp` の `g_modern_mutex` と同じ形）。
 */
std::mutex g_native_menu_mutex;
presentation::NativeModernMenu g_native_menu;

/*!
 * @name 最初の `ui_state`
 * @details 日本語でカタログを読ませるには `an_bootstrap()` より前に言語が決まって
 * いなければならない。画面側は握手の直後に必ず 1 回送ってくるので、それを待つ。
 * 待つのは起動列の 1 度だけ（Sil-Q `sq_main.cpp` と同じ形）。
 * @{
 */
std::atomic<bool> g_first_ui_state_seen{ false };
std::mutex g_first_ui_state_mutex;
std::string g_first_ui_state_lang;
/*! @} */

/*!
 * 画面が選んだサブパネルの種類（`ui_state.sub_panel_kinds`）。g_sub_panel_kinds_mutex で守る。
 * Angband はコアのサブウインドウを申告しないので、Rogue 系と同じく画面側の既定の 5 種
 * （`bridge/rogue_sub_panels.h`）を種類として申告する。以前は空の一覧を送っていたので、
 * 機能メニューで種類を 1 つも選べなかった（2026-10-02。気づいたこと
 * 「Android angband でサブパネル変更出来ない」。Windows でも同じだった）。
 */
std::mutex g_sub_panel_kinds_mutex;
std::array<int, kSubPanelCount> g_sub_panel_kinds = { -1, -1, -1, -1, -1, -1, -1 };

/*!
 * @brief アダプタ自身の字を画へ出す（設計 §1 制約 11・§8）。日本語なら `E:` を引く。
 */
std::string ui_text(const char *en)
{
    return an::ui_text(en);
}

/*!
 * @brief 原作の文言で画面を見分ける所（設計 §8）を英日の両方の綴りで見る。
 * @details 日本語の綴りは**カタログの `E:` を引いた結果**を使い、アダプタに二重に
 * 書かない（`an_tr(en)`。訳が無ければ英語と同じ字なので 1 回見るだけ）。
 */
bool starts_with_marker(const std::string &text, const char *en)
{
    if (text.rfind(en, 0) == 0) {
        return true;
    }
    const char *ja = an_tr(en);
    return (ja != en) && (text.rfind(ja, 0) == 0);
}

bool contains_marker(const std::string &text, const char *en)
{
    if (text.find(en) != std::string::npos) {
        return true;
    }
    const char *ja = an_tr(en);
    return (ja != en) && (text.find(ja) != std::string::npos);
}

bool ends_with_marker(const std::string &text, const char *en)
{
    auto ends = [&text](const char *s) {
        const std::size_t n = std::strlen(s);
        return (text.size() > n) && (text.compare(text.size() - n, n, s) == 0);
    };
    if (ends(en)) {
        return true;
    }
    const char *ja = an_tr(en);
    return (ja != en) && ends(ja);
}

/*!
 * @brief AH-51: 行 0（右の空白を落とした字）が `Command:`（どのコマンドを繰り返すかの 1 キー待ち）か。英日の両方で見る。
 * @details 鍵が末尾の空白を持つ `Command: ` は、`an_tr_term()` が末尾の空白付きの鍵も引くようになった（修-40）ので、
 * 日本語では行 0 が `コマンド:` で届く。英語の綴りだけを見ていると反復回数の続きを見失う。
 */
bool is_command_prompt(const std::string &text)
{
    if (text == "Command:") {
        return true;
    }
    std::string ja = an_tr("Command: ");
    while (!ja.empty() && (ja.back() == ' ')) {
        ja.pop_back();
    }
    return (ja != "Command:") && (text == ja);
}

/*!
 * @brief ヘルプの目次（`index.txt`。`.. menu:: [a] ...` のあるファイル）を開いている画面か。
 * @details 見出しが `[Angband ..., Line ` で始まり、最終行が「[Press a Letter, or ESC to exit.]」
 * （日本語は「[英字で選ぶ・ESC で戻る]」）。普通のヘルプ本文は最終行が違う。
 * `fill_help_index_choices()`（目次の行を選択肢にする）と `an_declared_menu_actions()`（`%`）の両方が使う。
 */
bool is_help_index_frame(const GameFrame &frame)
{
    if (frame.menu_term_lines.size() < 3) {
        return false;
    }
    const std::string &first = frame.menu_term_lines.front().text_utf8;
    const std::string &last = frame.menu_term_lines.back().text_utf8;
    if ((first.rfind("[Angband", 0) != 0) || (first.find(", Line ") == std::string::npos)) {
        return false;
    }
    return starts_with_marker(last, "[Press a Letter, or ESC to exit.]");
}

/*!
 * @brief AH-04: `i`/`e` が開く持ち物一覧の根。
 * @details 手本は `silq/adapter/sq_main.cpp` の `g_inventory_menu`。`input()`/`capture()`
 * の排他は `g_native_menu` と同じ `g_native_menu_mutex` に相乗りする（別ロックにすると
 * `g_native_menu` と食い違うタイミングで壊れる）。
 */
presentation::NativeInventoryMenu g_inventory_menu;
/*!
 * @brief AH-41: 店の画面を他のコア（幻想蛮怒・Frox）と同じ「店の品／持ち物」の切り替えで出す。
 * @details 原作の店（`ui-store.c`）は一覧から 'p' で買い、'd'/'s' で持ち物の一覧（`store_sell()`）
 * を開いて売る。以前は一覧を `NativeModernMenu` の汎用の選択肢として出し、売るのは
 * 「売る」ボタン（AH-06）だけだったので、持ち物側の一覧が店の画面に無かった。
 * 読み取りは `an_native_inventory.c` の `an_native_store_info()`/`an_native_store_item()`。
 */
presentation::NativeStoreMenu g_store_menu;

bool send_message(const std::string &type, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(g_send_mutex);
    if (g_out == nullptr) {
        return false;
    }
    std::string err;
    if (!presentation::write_message(g_out, payload, err)) {
        std::fprintf(stderr, "[angband] send \"%s\" failed: %s\n", type.c_str(), err.c_str());
        g_ui_gone.store(true);
        return false;
    }
    g_log.record("out", type, payload);
    return true;
}

void send_fatal(const std::string &reason)
{
    std::fprintf(stderr, "[angband] fatal: %s\n", reason.c_str());
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
 * @brief AH-48: 休憩（`R`。原作の `textui_cmd_rest()`）の問いの状態。
 * @details 原作は `get_string()`（`askfor_aux()`）で `0`〜`9999`・`&`（必要なだけ）・`*`（HP と SP が
 * 全回復するまで）・`!`（HP か SP のどちらかが満ちるまで）を受ける。行 0 は画面側へ渡らないので、
 * 文字入力の窓にしても問いも入力中の字も見えなかった。`fill_prompts()` が選択肢
 * （`&` `*` `!` と「ターン数を指定」）を出し、ターン数の指定を選んだら数値の窓に切り替える。
 * `g_rest_prompt` は `fill_prompts()`（ゲームスレッド）が書き、受信スレッドが読む。
 */
std::atomic<bool> g_rest_prompt{ false };
std::atomic<bool> g_rest_count_mode{ false };
constexpr int kRestCountKey = '#'; //!< 「ターン数を指定」の選択肢の合成キー（原作の休憩の問いでは使えない字）。

/*!
 * @brief AH-59: 持ち物の窓の「全部落とす」（原作 `context_menu_object()` の Drop All）の予約。
 * @details 原作の右クリックメニューは山の全部を数量の問い無しで落とす。持ち物の窓の操作は共用の表の
 * 「キー列を送る」だけで数量を先へ運べないので、**アダプタの中だけ**で次のように運ぶ:
 *  1. 持ち物用の表（`build_inventory_commands()`）の「全部置く」は、キー列の頭に印
 *     （`kDropAllMark`。原作へは送らない）を付けた `d`。
 *  2. 受信スレッドが、印を見つけたらキー列から外し、`d` と同じ区間（`g_inbox_mutex`）で予約を立てる
 *     （`rewrite_drop_all_keys()`）。
 *  3. 原作は `d` → 「どれを置きますか」（画面側が対象を選ぶ）→ 数量の問い（`do_cmd_drop()` の
 *     `cmd_get_quantity()`）へ進む。`an_ui_shim.c` の `an_wrapped_get_quantity()` が予約を消費して山の
 *     全部で答える（数量の窓は出ない）。
 *  4. 数量の問いに至らなかった（対象を選ばず ESC・1 個の山）予約は古いので、ゲームが命令待ちで
 *     入力を待つ回に何も読まなかったとき（`an_null_term.c` の `an_term_event()` → `an_drop_all_idle()`）に
 *     捨てる。**判定は待ち行列と同じロックの中で待ち行列が空のときだけ**行うので、予約と `d` を積む
 *     区間と入れ違いにならない。（`host_next_key()` で捨てると、原作へ渡すキーを Term の待ち行列へ
 *     移した直後の「空」で、まだ読まれていない `d` の予約を捨ててしまう——実測で見つけた。）
 * `g_drop_all_armed` は受信スレッド（立てる）とゲームスレッド（消費・捨てる）が触る。
 */
std::atomic<bool> g_drop_all_armed{ false };
constexpr int kDropAllMark = 0x20000001; //!< キー列の中の印。原作へは送らない（`AN_KEY_MAP_CLICK` のビット 30 とも別）。

/*!
 * @brief AH-59: 持ち物の窓が積んだキー列から「全部落とす」の印を外す。
 * @return 印があれば真（呼び手が予約を立てる）
 */
bool rewrite_drop_all_keys(std::vector<int> &keys, std::size_t before)
{
    bool found = false;
    std::vector<int> rewritten;
    for (std::size_t i = before; i < keys.size(); ++i) {
        if (keys[i] == kDropAllMark) {
            found = true;
            continue;
        }
        rewritten.push_back(keys[i]);
    }
    if (found) {
        keys.resize(before);
        keys.insert(keys.end(), rewritten.begin(), rewritten.end());
    }
    return found;
}

/*!
 * @brief AH-48: 休憩の問いで選択肢が積んだキーを原作の入力の並びへ直す。
 * @details `&` `*` `!` は原作では「その字を打って Enter」なので Enter を足す。
 * 合成キー `#` は数値の窓へ切り替えるだけで、原作へは送らない。ESC は問いごと取り消す。
 */
void rewrite_rest_keys(std::vector<int> &keys, std::size_t before)
{
    if (!g_rest_prompt.load()) {
        return;
    }
    std::vector<int> rewritten;
    for (std::size_t i = before; i < keys.size(); ++i) {
        const int k = keys[i];
        if (k == kRestCountKey) {
            g_rest_count_mode.store(true);
            continue;
        }
        rewritten.push_back(k);
        if ((k == '&') || (k == '*') || (k == '!')) {
            rewritten.push_back(13);
        }
    }
    keys.resize(before);
    keys.insert(keys.end(), rewritten.begin(), rewritten.end());
}

/*!
 * @brief AH-63: `navigate`（十字キー・パッドの上下左右）が積んだ数字キーを原作の矢印キーへ直す。
 * @details 共用の `native_modern.h`・`modern_store.cpp` は、カーソルを持つ画面の上下左右を
 * 数字キー `2`/`8`/`4`/`6`（テンキーの方向）で積む。変愚・短愚・幻想蛮怒・Frox・Sil-Q の画面は
 * これを方向として読むが、Angband の `~`（知識）の一覧（`ui-knowledge.c` の
 * `reset_main_knowledge_menu()`）は `cmd_keys` に `"123456789"`・`keys_hook` に
 * `handle_store_shortcuts()` を持ち、`2` を「カーソルを下へ」ではなく**店 2 の品揃えを開く近道**として読む
 * （`8` は店 8＝我が家）。`navigate` で下へ降りて決定する普通の操作ができなかった。
 * 原作の矢印キー（`ui-event.h` の `ARROW_DOWN`=0x80・`ARROW_LEFT`=0x81・`ARROW_RIGHT`=0x82・
 * `ARROW_UP`=0x83）は、メニュー（`menu_handle_keypress()`）も素の入力ループ
 * （`target_dir()`・能力値の割り振りなど）も数字と同じ方向として受け、`cmd_keys` の近道には掛からない。
 * そこで、**`navigate` が積んだ数字だけ**（同じ区間の他のキーと `focus`・`native_key` などの別の経路は触らない）を
 * 矢印へ替える。共用のコードは他のコアの `2`/`8` を方向として読む画面があるので変えない。
 */
void rewrite_navigate_keys(std::vector<int> &keys, std::size_t before)
{
    constexpr int kArrowDown = 0x80;  // ARROW_DOWN（ui-event.h）
    constexpr int kArrowLeft = 0x81;  // ARROW_LEFT
    constexpr int kArrowRight = 0x82; // ARROW_RIGHT
    constexpr int kArrowUp = 0x83;    // ARROW_UP
    for (std::size_t i = before; i < keys.size(); ++i) {
        switch (keys[i]) {
        case '2': keys[i] = kArrowDown; break;
        case '4': keys[i] = kArrowLeft; break;
        case '6': keys[i] = kArrowRight; break;
        case '8': keys[i] = kArrowUp; break;
        default: break;
        }
    }
}

/*!
 * @brief 修-39: `text`（自由文字入力。UTF-8）を、原作が読む「符号位置のキー」に直して積む。
 * @details 原作 Angband のキーは UTF-32 の符号位置（`keycode_t`）で、`askfor_aux_keypress()` は
 * `keycode_isprint()` が真の符号位置だけを `utf32_to_utf8()` で名前の欄に足す。共用の
 * `append_input_event_keys()`（`input_event_adapter.cpp`）は `text` を 1 バイトずつ整数にして積む
 * ——ほかのコアは `sdl_null_term.cpp` 側などで符号を直すが、Angband の `an_null_term.c` は
 * `Term_keypress((keycode_t)key, 0)` で整数をそのまま符号位置として渡す。そのため「試」の 3 バイト
 * （E8 A9 A6）が U+00E8・U+00A9・U+00A6 の 3 字（`è©¦` の化け）になり、さらに 0x80〜0x9F の
 * バイトは原作の特別キー（`ui-event.h` の `ARROW_*`＝0x80〜0x83、`KC_ENTER`＝0x9C、`KC_TAB`＝0x9D、
 * `KC_DELETE`＝0x9E、`KC_BACKSPACE`＝0x9F）と衝突して、決定や退格として読まれていた。
 * 共用の変換器は他のコアが頼っているので触らず、受信側のここだけで UTF-8 を復号する。
 *
 * - 0x80 未満（英数字・記号）は従来どおりそのまま積む。
 * - 2〜4 バイトの正しい並びは、符号位置 1 個にして積む。U+0080〜U+009F（制御文字）は特別キーと
 *   同じ値になるので捨てる。サロゲート・冗長な長い表現も捨てる。Windows では U+FFFF を超える字も捨てる
 *   （`wchar_t` が 16 ビットで、原作の `Term` は 1 セルに収められない）。
 * - 壊れた並び（先頭以外の継続バイトだけ・途中で切れた並び）は、そのバイトを 1 個捨てて先へ進む。
 *   バイトのまま積むと 0x80〜0x9F が特別キーになるため。
 */
void append_text_event_codepoints(const std::string &text, std::vector<int> &out)
{
#ifdef _WIN32
    constexpr unsigned int kMaxTextCodepoint = 0xFFFF; // `wchar_t` が 16 ビット。
#else
    constexpr unsigned int kMaxTextCodepoint = 0x10FFFF;
#endif
    const std::size_t size = text.size();
    std::size_t i = 0;
    while (i < size) {
        const unsigned char c0 = static_cast<unsigned char>(text[i]);
        if (c0 < 0x80) {
            out.push_back(c0);
            ++i;
            continue;
        }
        int extra = 0;
        unsigned int cp = 0;
        unsigned int min_cp = 0;
        if ((c0 & 0xE0) == 0xC0) {
            extra = 1;
            cp = c0 & 0x1F;
            min_cp = 0x80;
        } else if ((c0 & 0xF0) == 0xE0) {
            extra = 2;
            cp = c0 & 0x0F;
            min_cp = 0x800;
        } else if ((c0 & 0xF8) == 0xF0) {
            extra = 3;
            cp = c0 & 0x07;
            min_cp = 0x10000;
        } else {
            ++i; // 継続バイトだけ、または 0xF8 以上。
            continue;
        }
        if (i + static_cast<std::size_t>(extra) >= size) {
            ++i; // 並びが途中で切れている（最後のバイトの位置 `i + extra` が範囲の外）。
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char ck = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((ck & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (ck & 0x3F);
        }
        if (!ok) {
            ++i;
            continue;
        }
        i += static_cast<std::size_t>(extra) + 1;
        if ((cp < min_cp) || (cp > kMaxTextCodepoint) || ((cp >= 0x80) && (cp <= 0x9F)) || ((cp >= 0xD800) && (cp <= 0xDFFF))) {
            continue;
        }
        out.push_back(static_cast<int>(cp));
    }
}

/*!
 * @brief 受信スレッド。`keys`（生のコード列。検査用）・`input_event`（HD2D の
 * 実運用が送ってくる形。新 UI から来る）・`quit_request` を受ける。
 * @details `input_event` は `presentation::append_input_event_keys()`
 * （`presentation/bridge/input_event_adapter.cpp`）へそのまま渡す——この変換器は
 * 「コアに依らない」（z-term 系の askfor・KTRL の約束に沿うだけ）ので、生のキー・方向は
 * Angband 用に手を入れない（design「新 UI の描画側に Angband 用の条件を書かない」と
 * 同じ理由）。**ただし数量入力（`action=="number"`）だけは例外**——共用の消去キー
 * （`KTRL('B')`/`KTRL('D')`）が Angband の `askfor_aux_keypress()` では効かないので、
 * `g_native_menu.input()` が積んだキーをここで Angband 向けに差し替える
 * （見直しの指摘 3。詳細は下の該当箇所のコメント）。
 */
void receive_loop()
{
    for (;;) {
        std::string payload;
        std::string err;
        const auto result = presentation::read_message(g_in, payload, err);
        if (result != presentation::TransportResult::Ok) {
            if (result != presentation::TransportResult::Eof) {
                std::fprintf(stderr, "[angband] receive stopped: %s%s%s\n",
                    presentation::transport_result_name(result), err.empty() ? "" : " - ", err.c_str());
            } else {
                std::fprintf(stderr, "[angband] stdin closed (the ui is gone)\n");
            }
            g_ui_gone.store(true);
            return;
        }

        const std::string type = presentation::peek_message_type(payload);
        g_log.record("in", type, payload);

        if (type == "keys") {
            presentation::KeysMessage message;
            if (!presentation::decode_keys(payload, message, err)) {
                std::fprintf(stderr, "[angband] bad \"keys\": %s\n", err.c_str());
                continue;
            }
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            for (const int key : message.keys) {
                g_key_queue.push_back(key);
            }
            continue;
        }
        if (type == "input_event") {
            presentation::InputEventsMessage message;
            if (!presentation::decode_input_events(payload, message, err)) {
                std::fprintf(stderr, "[angband] bad \"input_event\": %s\n", err.c_str());
                continue;
            }
            /*
             * `e == "ui_action"` は新 UI のカーソル式ダイアログ（設計 §6.3。
             * `frame.modern` を返している間）が送ってくる抽象操作
             * （選ぶ・確認・数量・カーソル移動）——`g_native_menu.input()` で
             * 実際のキーへ翻訳してから積む（`silq/adapter/sq_main.cpp` の
             * `pop_native_key_locked()` と同じ役目）。
             *
             * 見直しの指摘 4: 以前ここには「Angband は 1 個ずつしか同時に処理しない
             * ので、ここで即時に翻訳しても実害が無い」と書いてあったが誤り
             * ——`g_native_menu` は `build_frame()`（ゲームスレッド）の `capture()`
             * とこのスレッドの `input()` の両方から、ロック無しに触られていた
             * （データ競合）。`g_native_menu_mutex` で両方を包む。それ以外
             * （生のキー・方向）は従来どおり `append_input_event_keys()` に任せる。
             */
            presentation::InputEventsMessage legacy_only;
            std::vector<int> keys;
            bool arm_drop_all = false; // AH-59: 「全部落とす」の予約（キーを積む区間で立てる）
            /*
             * 根本の直し（2026-09-23。「応答待ち」3 回目の調査）: 翻訳（`input()`）から
             * `g_key_queue` へ積み終えるまでを、`host_present()` と同じ
             * `g_native_menu_mutex` の中で**一続き**に行う（手本は
             * `rogue_legacy/adapter/rl_main.cpp` の受信側。あちらも「受け付け→
             * キーを積む」を `g_modern_mutex` の中で行う）。以前は `input()` だけを
             * 包み、積むのはロックの外だった——その隙に `host_present()` が入ると、
             * `consumed_` は立っているのに待ち行列は空、という瞬間を見て
             * 「受理済み・中身は古い」フレームを組んでしまう（`g_native_menu_dirty`
             * の註記）。
             */
            std::lock_guard<std::mutex> menu_lock(g_native_menu_mutex);
            for (const auto &event : message.events) {
                if (event.e == "ui_action") {
                    const std::size_t before = keys.size();
                    /*
                     * 設計 §8.5/§8.5.1（T1〜T4）: 目標選択が開いている間の `aim_*`
                     * 操作（`hd2d/ui/targeting_ui.h` が送る）は、ほかのコアの
                     * `_main.cpp`（`sq_main.cpp`・`gb_main.cpp`・`fc_main.cpp`・
                     * `platform/windows/core_main.cpp`）と同じ優先順位で
                     * `native_targeting().input()` に先に渡す。**これが抜けていると、
                     * 実際の画面操作（マウス・Enter・Escape・ゲームパッド）の
                     * 確定・取消・巡回・「詳細」が黙って届かなくなる**——矢印キーの
                     * 移動だけは別経路（下の raw "move" フォールバック）で動くので
                     * 気づきにくい。T4 の確認中に見つかった（見る/目標選択の
                     * 確定が新 UI から一切効かなかった）。
                     */
                    const bool handled = presentation::native_targeting().input(event, keys)
                        || g_inventory_menu.input(event, keys)
                        || g_store_menu.input(event, keys)
                        || g_native_menu.input(event, keys);
                    if (event.action == "number") {
                        /*
                         * 見直しの指摘 3。共用の `set_number`（`native_modern.h`
                         * → `input_event_adapter.cpp`）は、既定値を消すのに
                         * `KTRL('B')`（0x02。左へ）×10 ＋ `KTRL('D')`（0x04。前方削除）×10
                         * を送る——変愚・Frox はこれで消えるが、Angband の
                         * `askfor_aux_keypress()`（原作 `ui-input.c`）はこの 2 つを
                         * 「印字できない」として `bell()` で捨てるだけで、カーソルも
                         * 動かない。結果、既定値 "1" の前に数字が挿し込まれて
                         * "12" のような違う値になる。
                         *
                         * **共用の `input_event_adapter.cpp` は変えない**（他コアの
                         * 消し方まで壊す）。ここ（アダプタの受信側）だけ、Angband 向けに
                         * 送るキーを差し替える: Angband の `Term_keypress()`
                         * （`complex_input` が偽のとき）は素の `0x08` を
                         * `KC_BACKSPACE` に直し、`askfor_aux_keypress()` は
                         * 「firsttime（この欄で最初のキー）の backspace は全消去」
                         * なので、`0x02`/`0x04` を 1 個の `0x08` に丸めれば足りる
                         * （2 個目以降は firsttime が終わっているので送らない）。
                         */
                        std::vector<int> rewritten;
                        rewritten.reserve(keys.size() - before);
                        bool backspace_sent = false;
                        for (std::size_t i = before; i < keys.size(); ++i) {
                            const int k = keys[i];
                            if ((k == 0x02) || (k == 0x04)) { // KTRL('B') / KTRL('D')
                                if (!backspace_sent) {
                                    rewritten.push_back(0x08);
                                    backspace_sent = true;
                                }
                                continue;
                            }
                            if (k == 0x7F) {
                                /*
                                 * AH-51: 反復回数（`textui_get_count()`）の入力の消去。共用の `set_number` は
                                 * 退格を 0x7F で送るが、Angband の `Term_keypress()` は 0x7F を退格に直さず
                                 * 「数字以外」として読み、回数の入力を打ち切って残りの数字を歩行コマンドにしてしまう。
                                 * 素の 0x08（`KC_BACKSPACE`）へ替える（回数は最大 4 桁なので 5 個で足りる）。
                                 */
                                rewritten.push_back(0x08);
                                continue;
                            }
                            rewritten.push_back(k);
                        }
                        keys.resize(before);
                        keys.insert(keys.end(), rewritten.begin(), rewritten.end());
                    }
                    if (event.action == "navigate") {
                        rewrite_navigate_keys(keys, before); // AH-63
                    }
                    rewrite_rest_keys(keys, before); // AH-48
                    if (rewrite_drop_all_keys(keys, before)) { // AH-59
                        arm_drop_all = true;
                    }
                    g_native_menu_dirty.store(true); // 安いしきい値のバイパス（上の註記）。
                    /*
                     * 「応答待ちです」で固まる不具合の切り分け用（見直しで追加）。
                     * `action`/`target`/`ui_revision`/`request_id` と、翻訳の結果
                     * 積んだキーの個数を stderr へ出す——`--protocol-log=` の
                     * `in`/`ui_action` 行と突き合わせれば、どの操作で
                     * `handled=1` かつ `新規キー 0`（＝`input()` が「読んだことに
                     * した」だけで実キーを出さなかった。`ui_revision` 不一致か
                     * `consumed_` 済みの早期 return の可能性が高い）になっているかを追える。
                     */
                    std::string new_key_text;
                    for (std::size_t i = before; i < keys.size(); ++i) {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%d(%c) ", keys[i],
                            (keys[i] >= 32 && keys[i] < 127) ? static_cast<char>(keys[i]) : '?');
                        new_key_text += buf;
                    }
                    std::fprintf(stderr,
                        "[angband] ui_action: action=%s target=%s number=%lld ui_revision=%d "
                        "request_id=%d handled=%d new_keys=%zu [%s]\n",
                        event.action.c_str(), event.target.c_str(), event.number, event.ui_revision,
                        event.request_id, handled ? 1 : 0, keys.size() - before, new_key_text.c_str());
                } else if (event.e == "fkey") {
                    /*
                     * AH-08: `append_input_event_keys`
                     * （共用の `input_event_adapter.cpp`）は F キーを 31 で始まる並び
                     * （31, [C][S][A], 'x', 16 進 2 桁, 13）へ翻訳する。この並びは、
                     * 原作の `keymap_add()` が読み込む pref ファイル（`pc_at.prf` 相当）
                     * にトリガーが定義されていて初めて 1 個のコマンドとして消費される。
                     * この Angband のヘッドレス組み（`an_null_term.c`）はそうした
                     * pref ファイルを読んでおらず、F1〜F12 はどれも割り当てが無い
                     * ——並びが素の文字として 1 バイトずつ `askfor`/`request_command`
                     * に渡り、`31` は無視されても後続の `'x'`（移動コマンド）や
                     * 16 進の数字（テンキー扱いの移動）が実コマンドとして拾われ、
                     * 1 歩動いて松明を持ち替える、という誤動作になっていた。
                     * 割り当てが無い以上、並びを丸ごと捨てて何もしない方が正しい
                     * （共用コードは他の 5 コアの pref 定義済みの F キーを壊すので触らない）。
                     */
                    continue;
                } else if (event.e == "map_click") {
                    /*
                     * AH-57: 地図のマスのクリック。`hello_ack.features` の `"map_click"` を見た画面だけが
                     * 送る（の AH-57）。キーと同じ列へ**整数 1 個**として積み
                     * （`an_shim.h` の `AN_MAP_CLICK_PACK`）、順序を保つ。ゲームスレッドが入力を取りに
                     * 来たとき `an_null_term.c` の `an_map_click_apply()` が、原作のマウスの事象として
                     * Term へ入れる（そのとき命令待ちでなければ捨てる）。ここでは状態を見ない。
                     *
                     * 同じメッセージの先に並んでいた生のキーは、この列の**前**へ積んでおく（`legacy_only` は
                     * 下でまとめて積むので、そのままだとクリックが先に出てしまう）。
                     */
                    if (!legacy_only.events.empty()) {
                        presentation::append_input_event_keys(legacy_only, keys);
                        legacy_only.events.clear();
                    }
                    const int click_mods = (event.shift ? AN_MAP_CLICK_SHIFT : 0) | (event.ctrl ? AN_MAP_CLICK_CTRL : 0)
                        | (event.alt ? AN_MAP_CLICK_ALT : 0);
                    keys.push_back(AN_MAP_CLICK_PACK(event.grid_x, event.grid_y, event.button, click_mods));
                } else if (event.e == "text") {
                    /*
                     * 修-39（F-angband-01）: 自由文字入力は UTF-8 を符号位置に直して積む
                     * （`append_text_event_codepoints()`）。同じメッセージの先に並んでいた生のキーは、
                     * この列の**前**へ積んでおく（`map_click` と同じ。`legacy_only` は下でまとめて積む）。
                     */
                    if (!legacy_only.events.empty()) {
                        presentation::append_input_event_keys(legacy_only, keys);
                        legacy_only.events.clear();
                    }
                    append_text_event_codepoints(event.text, keys);
                } else {
                    legacy_only.events.push_back(event);
                }
            }
            if (!legacy_only.events.empty()) {
                presentation::append_input_event_keys(legacy_only, keys);
            }
            std::lock_guard<std::mutex> lock(g_inbox_mutex);
            if (arm_drop_all) {
                g_drop_all_armed.store(true); // 予約と `d` は同じ区間で入れる（an_drop_all_idle() の掃除と入れ違いにならない）。
            }
            for (const int key : keys) {
                g_key_queue.push_back(key);
            }
            continue;
        }
        if (type == "ui_state") {
            /*
             * 日本語化（設計 §4）。最初の 1 通の `lang` だけを見る（起動列で言語を決める）。
             * ほかに使う欄はサブパネルの種類だけ（選び直すたびに届くので毎回受け取る）。
             * 遊んでいる途中の言語の切り替えは画面側が `lang-restart` で起こし直す。
             */
            presentation::UiStateMessage message;
            if (!presentation::decode_ui_state(payload, message, err)) {
                std::fprintf(stderr, "[angband] bad \"ui_state\": %s\n", err.c_str());
                continue;
            }
            if (!g_first_ui_state_seen.load()) {
                {
                    std::lock_guard<std::mutex> lock(g_first_ui_state_mutex);
                    g_first_ui_state_lang = message.lang;
                }
                g_first_ui_state_seen.store(true);
            }
            if (message.has_sub_panel_kinds) {
                std::lock_guard<std::mutex> lock(g_sub_panel_kinds_mutex);
                for (int i = 0; i < kSubPanelCount; ++i) {
                    g_sub_panel_kinds[static_cast<std::size_t>(i)] = message.sub_panel_kinds[i];
                }
            }
            continue;
        }
        if (type == "quit_request") {
            g_quit_requested.store(true);
            std::fprintf(stderr, "[angband] quit_request received\n");
            continue;
        }
        // 未知の種別は黙って捨てる（v1 §2.3）。
    }
}

/*!
 * @name 検証用のキー注入（S2 の HD2D 実機確認のため）
 * @details `silq/adapter/sq_main.cpp` の `ScriptKeys`（変愚の
 * `HENGBAND_SDL2_INJECT_KEYS`。`presentation/term/sdl_null_term.cpp`）と同じ書式を
 * こちらの環境変数名（`ANGBAND_INJECT_KEYS` / `ANGBAND_INJECT_FILE`）で読む。
 * HD2D は画面側からコアへ直接キーを送らない（`--core-path=` で起こしたときも
 * プロトコルの `input_event` を経由する）ので、**スクリプトを流す手立てが無いと
 * 実機の確認が人手でしか進められない**。
 *
 * | 綴り | 意味 |
 * |---|---|
 * | `\r` `\n` `\e` `\t` `\s` | Enter / 改行 / ESC / Tab / 空白 |
 * | `\^A`〜`\^Z` | Ctrl+A〜Ctrl+Z |
 * | `\xHH` | 生のバイト |
 * | `\.` | キーを積まずに約 0.25 秒待つ（`-more-` や生成の後で使う） |
 * | `\\` | `\` そのもの |
 * @{
 */
class ScriptKeys {
public:
    void load()
    {
        if (const char *const inline_script = std::getenv("ANGBAND_INJECT_KEYS")) {
            this->parse(inline_script);
        }
        if (const char *const path = std::getenv("ANGBAND_INJECT_FILE")) {
            std::FILE *fp = std::fopen(path, "rb");
            if (fp == nullptr) {
                std::fprintf(stderr, "[angband] inject: cannot open %s\n", path);
            } else {
                std::string body;
                char buf[1024];
                while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
                    std::string line = buf;
                    const auto hash = line.find('#');
                    if (hash != std::string::npos) {
                        line.erase(hash);
                    }
                    body += line;
                }
                std::fclose(fp);
                this->parse(body);
            }
        }
        if (!this->actions_.empty()) {
            std::fprintf(stderr, "[angband] inject: %u action(s) scripted\n",
                static_cast<unsigned>(this->actions_.size()));
        }
    }

    //! 次の 1 個。@return 積むキー、または 0（待ちの最中／打ち止め）
    int next()
    {
        const auto now = portable::tick_ms();
        if (now < this->wait_until_ms_) {
            return 0;
        }
        while (this->cursor_ < this->actions_.size()) {
            const Action &action = this->actions_[this->cursor_++];
            if (action.is_wait) {
                this->wait_until_ms_ = now + 250;
                return 0;
            }
            return action.key;
        }
        return 0;
    }

private:
    struct Action {
        bool is_wait{false};
        int key{0};
    };

    void parse(const std::string &text)
    {
        for (std::size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if ((c == ' ') || (c == '\r') || (c == '\n') || (c == '\t')) {
                continue; // 素の空白・改行は読み飛ばす（見た目の区切りだけ）。
            }
            if ((c == '\\') && (i + 1 < text.size())) {
                const char esc = text[++i];
                switch (esc) {
                case 'r': this->actions_.push_back({false, '\r'}); break;
                case 'n': this->actions_.push_back({false, '\n'}); break;
                case 'e': this->actions_.push_back({false, 27}); break;
                case 't': this->actions_.push_back({false, '\t'}); break;
                case 's': this->actions_.push_back({false, ' '}); break;
                case '.': this->actions_.push_back({true, 0}); break;
                case '\\': this->actions_.push_back({false, '\\'}); break;
                case '^':
                    if (i + 1 < text.size()) {
                        const char ctrl = text[++i];
                        this->actions_.push_back({false, (std::toupper(static_cast<unsigned char>(ctrl)) - 'A') + 1});
                    }
                    break;
                case 'x':
                    if (i + 2 < text.size()) {
                        const std::string hex = text.substr(i + 1, 2);
                        i += 2;
                        this->actions_.push_back({false, std::stoi(hex, nullptr, 16)});
                    }
                    break;
                default:
                    this->actions_.push_back({false, static_cast<unsigned char>(esc)});
                    break;
                }
                continue;
            }
            this->actions_.push_back({false, static_cast<unsigned char>(c)});
        }
    }

    std::vector<Action> actions_;
    std::size_t cursor_{0};
    std::uint64_t wait_until_ms_{0};
};
/*! @} */

ScriptKeys g_script;

int host_next_key()
{
    {
        std::lock_guard<std::mutex> lock(g_inbox_mutex);
        if (!g_key_queue.empty()) {
            const int key = g_key_queue.front();
            g_key_queue.pop_front();
            return key;
        }
    }
    return g_script.next();
}

void host_drop_keys()
{
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    g_key_queue.clear();
}

void host_sleep_ms(int ms)
{
    portable::sleep_ms(ms);
}

/*!
 * @brief `an_host_hooks::shutdown_requested` の実体。
 * @return 0=無し／1=穏やか（`quit_request`）／2=強制（stdin の EOF）
 * @details 穏やかな要求は**読み出した時点で旗を下ろす**（`exchange`）。
 * 下ろさないと、`inkey_flag` が偽で拒まれ続ける間、10ms ごとに同じ要求を
 * 送り続けることになり、`an_shutdown_and_quit()` の plog が延々と出る
 * （S1 の見直しで実測。stdin を閉じて 6 秒で 694 行）。
 * `g_ui_gone`（強制）は下ろさない——コアは確実に終了する経路に入るので、
 * 二重に読まれても実害が無い。
 */
int host_shutdown_requested()
{
    if (g_ui_gone.load()) {
        return 2;
    }
    if (g_quit_requested.exchange(false)) {
        return 1;
    }
    return 0;
}

/* ================================================================ frame（S1 は Term の写しだけ） */

std::uint64_t g_frame_id = 0;
bool g_has_last_frame = false;
std::string g_last_frame_key;

} // namespace（一旦閉じる。見直しの指摘 8: `extern "C"` の定義を無名名前空間の中に
  // 置くと、C++11 以降は無名名前空間のメンバが内部結合になる規則が `extern "C"` より
  // 優先され、実際には他の翻訳単位（`an_map_shim.c`）から呼べない内部シンボルに
  // なってしまう。`g_frame_id` はこの無名名前空間の中で宣言されているが、無名
  // 名前空間の定義は「その場所に `using namespace <一意な名前>;` を差し込む」のと
  // 同じ意味を持つので、外（ここ）から無限定名で引き続き参照できる。

/*!
 * @brief AH-23 の検査専用の読み取り口（`an_map_shim.c` から C 連結で呼ぶ）。
 * `build_frame()` が今組んでいる（＝直前に払い出した）`frame_id` を返す——
 * `ANGBAND_DEBUG_RNG` が立っているときだけ `an_debug_dump_rng_state()` が
 * これを一緒に出す。1 回の `build_frame()` の中で `an_map_cell()` が複数回
 * 呼ばれても同じ値になるので、検査は「同じ `frame_id` の中で `state_i` が
 * 変わっていないか」を見られる（`build_frame()` の外で本当のターンが進んで
 * `state_i` が変わることはあり得るので、`frame_id` を跨いだ比較はしない）。
 * 通常運用では誰も呼ばない。
 */
extern "C" unsigned long long an_debug_current_frame_id(void)
{
    return static_cast<unsigned long long>(g_frame_id);
}

/*!
 * @brief AH-59: 「全部落とす」の予約を 1 回だけ消費する（`an_ui_shim.c` の `an_wrapped_get_quantity()` が呼ぶ。
 * ゲームスレッド）。立っていれば下ろして 1 を返す。
 */
extern "C" int an_drop_all_take(void)
{
    return g_drop_all_armed.exchange(false) ? 1 : 0;
}

/*!
 * @brief AH-59: 命令待ちで入力が何も無いのに残っている「全部落とす」の予約を捨てる（`an_null_term.c` の
 * `an_term_event()` が、待つ回で何も読まなかったときに呼ぶ。ゲームスレッド）。
 * @details 予約は `d` を積む受信スレッドの区間と同じロックの中で立つので、ここでも同じロックを取り、
 * **待ち行列が空のときだけ**下ろす（呼び手の確認と入れ違いに `d` が積まれていれば残す）。
 * 対象を選ばず ESC・1 個の山だった予約は、数量の問いに至らず、ここへ戻ってきて捨てられる。
 */
extern "C" void an_drop_all_idle(void)
{
    std::lock_guard<std::mutex> lock(g_inbox_mutex);
    if (g_key_queue.empty()) {
        g_drop_all_armed.store(false);
    }
}

namespace { // 再び開く（同じ無名名前空間。以降はこれまでどおり）

std::string frame_compare_key(std::string payload)
{
    const std::string marker = "\"frame_id\":";
    const auto pos = payload.find(marker);
    if (pos == std::string::npos) {
        return payload;
    }
    auto end = pos + marker.size();
    while ((end < payload.size()) && (payload[end] >= '0') && (payload[end] <= '9')) {
        ++end;
    }
    payload.erase(pos + marker.size(), end - (pos + marker.size()));
    return payload;
}

//! `tilework/tile_mapping.csv` の名寄せ表。`run_protocol()` の頭で 1 度だけ読む。
an::TileMapping g_tile_mapping;
bool g_tile_mapping_loaded = false;

bool g_startup_tables_sent = false;

/*!
 * @brief `cmds_all[]`（`ui-game.c:329-350`）から `PadCommandsMessage` を組む
 * （設計 §6.3「コマンド一覧」）。
 * @details AH-17: 以前は「Angband には
 * `rogue_like_commands` に当たる切替オプションが無い」と誤って決めつけ、
 * `current_keymap` を常に `"original"` にしていた——実際には `list-options.h` に
 * `rogue_like_commands` があり、`ui-*.c` の各所がこれで `KEYMAP_MODE_ORIG`/
 * `KEYMAP_MODE_ROGUE` を切り替えている。画面側（`hd2d/app/run_parts.cpp`）は
 * `current_keymap` の値だけを見て `seq_original`/`seq_rogue`（下の 2 行）の
 * どちらを送るか決めるので、常に `"original"` のままだと roguelike keyset を
 * 入れているのに常に原作既定の並び（`key[0]`）を送り、割り当てが違う
 * コマンドが走っていた（`an_rogue_like_commands()`。`an_pad_shim.c`）。
 * `seq_original` = `key[0]`、`seq_rogue` = `key[1]`（design のとおり。同じキー表を
 * 両方へ積む Sil-Q とは違い、Angband の `key[1]` は素の「第二の割り当て」
 * なのでそのまま渡す）。
 */
presentation::PadCommandsMessage build_pad_commands()
{
    presentation::PadCommandsMessage message;
    message.current_keymap = (an_rogue_like_commands() != 0) ? "rogue" : "original";

    static an_pad_command raw[128];
    const int n = an_pad_commands(raw, static_cast<int>(sizeof(raw) / sizeof(raw[0])));

    int next_id = 1;
    for (int i = 0; i < n; ++i) {
        presentation::PadCommandWireEntry wire;
        wire.id = next_id++;
        wire.command = raw[i].key1;
        // 日本語（設計 §8 の最後の行）。分類名と名前は `E:` で引く。画面側が綴りで見る
        // 分類（`Inventory`／`持ち物一覧`。`hd2d/app/input_controller.cpp`）は Angband の
        // 分類に無いので、訳しても開く位置（先頭の分類）は英語と同じ。
        wire.group_utf8 = raw[i].group ? ui_text(raw[i].group) : "";
        wire.label_utf8 = raw[i].label ? ui_text(raw[i].label) : "";
        wire.seq_original.push_back(raw[i].key1);
        wire.seq_rogue.push_back(raw[i].key2 ? raw[i].key2 : raw[i].key1);
        message.entries.push_back(std::move(wire));
    }
    /*
     * 繰り返し回数（`0`。原作 `textui_process_command()` の `case '0'`、標準・roguelike の両方の
     * キー配置で同じキー）。原作の表 `cmds_all[]` の外にあるので、メニューにも割り当てにも
     * 出ていなかった（決めたこと 2026-10-02「angband は繰り返し回数だけ対応しよう」。
     * Rogue 系は 137d2e9a23 で足してある）。押した後の数値の窓と `Command: ` は AH-51 の作り。
     * 既存の id を動かさないよう**末尾**に足す。
     */
    {
        presentation::PadCommandWireEntry wire;
        wire.id = next_id++;
        wire.command = '0';
        wire.group_utf8 = ui_text("Action commands");
        wire.label_utf8 = ui_text("Set repeat count");
        wire.seq_original.push_back('0');
        wire.seq_rogue.push_back('0');
        message.entries.push_back(std::move(wire));
    }
    return message;
}

/*!
 * @brief AH-59: 持ち物の窓の操作の表。コマンド一覧の表（`build_pad_commands()`）に、持ち物の窓だけの
 * 「全部置く」（原作の右クリックメニューの Drop All）を足す。
 * @details 足すのはこの窓へ渡す写しだけで、画面側へ送る `pad_commands`（コマンドメニュー・パッドの
 * 割り当て）は今までどおり——一覧の id はずれない。「全部置く」のキー列は `d`（アイテムを置く）と同じで、
 * 頭に原作へ送らない印（`kDropAllMark`）を付ける。印があるキー列は 2 個以上になるので、画面側の
 * 操作のホットキー（キー列が 1 個のときだけ付く）は `d` と重ならない。予約の運び方は `g_drop_all_armed`。
 */
presentation::PadCommandsMessage build_inventory_commands()
{
    presentation::PadCommandsMessage table = build_pad_commands();
    const presentation::PadCommandWireEntry *drop = nullptr;
    bool taken = false;
    for (const auto &entry : table.entries) {
        if (entry.command == AN_INV_CMD_DROP_ALL) {
            taken = true; // 原作のコマンドの鍵と重なった（起きないはず）。足さない。
        }
        if (!drop && (entry.command == 'd')) {
            drop = &entry;
        }
    }
    if (drop && !taken) {
        presentation::PadCommandWireEntry wire;
        wire.id = static_cast<int>(table.entries.size()) + 1;
        wire.command = AN_INV_CMD_DROP_ALL;
        wire.group_utf8 = drop->group_utf8;
        wire.label_utf8 = ui_text("Drop All");
        wire.seq_original.push_back(kDropAllMark);
        wire.seq_original.insert(wire.seq_original.end(), drop->seq_original.begin(), drop->seq_original.end());
        wire.seq_rogue.push_back(kDropAllMark);
        wire.seq_rogue.insert(wire.seq_rogue.end(), drop->seq_rogue.begin(), drop->seq_rogue.end());
        table.entries.push_back(std::move(wire));
    }
    return table;
}

/*!
 * @brief 起動時の表を 1 回だけ送る（設計 §6.2「目録」・§6.3「コマンド一覧」。
 * `silq/adapter/sq_main.cpp` の `send_startup_tables()` と同じ役目）。
 * @details `sub_panel_kinds` は S3 でもまだ中身が無いので**空のまま送る**
 * ——それでも送らないと画面側は前のコアの表を持ち越したままになる
 * （`hd2d/net/core_link.cpp` の `dispatch()` は表が届くたびに全置換する作りで、
 * 「送らない」を「前のコアと同じ」とは解釈しない）。空でも困らないことは
 * HD2D 実機で確かめてある（サブパネルの節が単に空で出るだけ）。
 */
void send_startup_tables()
{
    if (g_startup_tables_sent) {
        return;
    }
    g_startup_tables_sent = true;

    const presentation::PadCommandsMessage pad = build_pad_commands();
    std::fprintf(stderr, "[angband] pad_commands: %zu entr%s\n", pad.entries.size(),
        (pad.entries.size() == 1) ? "y" : "ies");
    (void)send_message("pad_commands", presentation::encode_pad_commands(pad));

    //! 画面側の既定の 5 種（Rogue 系と同じ）。中身は fill_legacy_sub_panels() が埋めた欄から画面が描く。
    // 言語は起動列で決まっている（`apply_language()`）。ゲーム中の切り替えは画面側が起こし直すので、送り直しは要らない。
    const presentation::SubPanelKindsMessage sub_panels = presentation::rogue_sub_panel_kinds(an::lang_code() != "ja");
    (void)send_message("sub_panel_kinds", presentation::encode_sub_panel_kinds(sub_panels));

    if (g_tile_mapping_loaded && !g_tile_mapping.message().assets.empty()) {
        auto manifest = g_tile_mapping.message();
        manifest.root = portable::path_to_utf8(manifest.root); // パスは ANSI で持つ。JSON に載せるときだけ UTF-8 にする（日本語の名前のフォルダで握手が落ちていた。2026-10-06）
        (void)send_message("asset_manifest", presentation::encode_asset_manifest(manifest));
        std::fprintf(stderr, "[angband] asset_manifest: %zu entr%s\n",
            g_tile_mapping.message().assets.size(),
            (g_tile_mapping.message().assets.size() == 1) ? "y" : "ies");
    }
}

//! `source_row` → `frame.menu_term_lines` の添字。空行を詰めた後の対応づけ
//! （プロンプト・選択肢の `line_index` を引くのに使う。設計 §6.3）。
int find_mirror_line_index(const GameFrame &frame, int source_row)
{
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].source_row == source_row) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/*!
 * @brief いま開いているメニューの実際の枠（`an_menu_active_region()`。設計 §6.3 の
 * 見直しで発見した「詳細欄に元の Term 行の残りが混ざる」不具合の直し）。
 * @details Angband の `struct menu` は**自分の `active`（region.row/col/width/
 * page_rows）の中だけ**を描き直す（`ui-menu.c` の `menu_calc_size()`/
 * `display_menu_row()`）。この枠の外側は、いま開いているメニューが 1 度も
 * 上書きしていない——メニューを開く前の地図・左状態列（持ち物一覧などの
 * **行**の箱の外）や、店の「Command for X」のような小さな浮動メニューが
 * `region_erase_bordered()` で自分の枠 + 1 桁だけを消して、**枠の右**（前の
 * 店の一覧の値段など、もっと横に長い残り）は消さないまま——という 2 通りの
 * 残骸が、そのまま詳細欄（残りの列）として読まれてしまっていた（見直しで
 * 実機確認：持ち物一覧で "Human"/"Rookie Warrior"/"LEVEL: 1" の 3 行、
 * 店の購入確認で前の商品一覧の値段が残った）。
 *
 * `menu->active` は `region_calculate()` を経て絶対座標に解決済み——
 * `struct menu` をゼロ初期化のまま使う全面表示のメニュー（`menu_layout()` を
 * 呼ばない場合）でも画面いっぱいの矩形へ解決されるので、「切る」と「切らない
 * （全面表示）」を同じ経路で扱える（呼び手が別に場合分けしなくてよい）。
 * @return 有効な枠が無ければ偽（呼び手は「切らない」へ倣う）。
 */
struct MenuActiveBox {
    bool valid{false};
    int row{0};
    int col{0};
    int width{0};
    int page_rows{0};
};

MenuActiveBox menu_active_box()
{
    MenuActiveBox box;
    // AH-42: 店の中では、説明（「調べる」'l'・「持ち物を調べる」'I' の
    // `textui_textblock_show()`）が店の一覧の上に重なって出る。`struct menu` を開かないので
    // 覚えているメニューは店の一覧のまま——そちらの箱を先に使うと、説明ではなく一覧の
    // 行が写る。説明の箱がいまの Term とまだ一致する間は、そちらを先に使う。
    if ((an_menu_store_kind() != 0)
        && (an_textblock_active_region(&box.row, &box.col, &box.width, &box.page_rows) != 0)) {
        box.valid = true;
        return box;
    }
    box.valid = an_menu_active_region(&box.row, &box.col, &box.width, &box.page_rows) != 0;
    if (!box.valid) {
        // AH-24: A1/A2（`struct menu`）が無い「生テキストの画面」
        // （`textui_textblock_show()`。設計 §4 A7）は、記録した先頭行の写しが
        // いまの Term とまだ一致するときだけこちらを使う——一致しなければ
        // （別の画面へ移った）`an_textblock_active_region()` 自身が 0 を返す。
        box.valid = an_textblock_active_region(&box.row, &box.col, &box.width, &box.page_rows) != 0;
    }
    return box;
}

//! 行 `y` を写すときの左端。0 行目（見出し・プロンプト）は枠にかかわらず常に
//! 切らない——`fill_menu_choices()`/`fill_prompts()` がそこを見出し・プロンプト文
//! として読むため。
int box_left_for_row(const MenuActiveBox &box, int y)
{
    if ((y == 0) || !box.valid) {
        return 0;
    }
    return box.col;
}

/*!
 * @brief A1/A2 の記録が、いまの Term にもう対応していないなら捨てる。
 * @details 「応答待ちです」で止まる不具合の 2 回目の再発（見直しで特定）の
 * 根本の直し。A1/A2 は `struct menu` を使う画面でしか呼ばれない——原作の
 * birth の能力値割り振り画面（`ui-birth.c` の `point_based_command()`）の
 * ように `struct menu` を使わず `Term_gotoxy()` + 生の `inkey_ex()` で直接描く
 * 画面へ移ると、`an_menu_begin()` が一度も呼ばれないまま**前の画面の記録**
 * （選択肢の行・`an_menu_active_region()` の枠の両方）が残り続ける。
 * `fill_menu_mirror()`/`fill_menu_choices()` がこれを読む**前**に、記録した
 * 「選ぶ字」が今の Term に実際に描かれているかを確かめ、**1 つも一致しなければ
 * 前の画面の古い記録**と判じて空にする（`an_menu_discard_stale()`）——これで
 * 選択肢だけでなく、詳細欄の切り取り枠（`an_menu_active_region()`）も
 * 同時に無効になり、次のフレームからは「メニューが無い」画面として扱われる。
 * AH-11: 以前は「選ぶ字の無い行（`row.sel == 0`。
 * カーソルだけで選ぶ行。知識画面など）は判じられないので、1 件でもあれば疑わしいと
 * 決めつけない」としていたが、これでは**選ぶ字を持たない一覧しか出さない画面**
 * （知識・`V` の説明・クイックスタート前後）で古い記録が絶対に discard されず、
 * 偽の選択肢や前の画面の残骸（切り取られた枠）がそのまま次の画へ出ていた。
 * `an_menu_row_drawn()` が描画直後に取っている行内容の写し（`row.snapshot*`。
 * `an_shim.h` の註記）を、いまの Term の同じ位置と突き合わせれば `sel == 0` の
 * 行でも判じられる——写しが無い行（`snapshot_len<=0`。列が無い等）だけ、
 * 従来どおり `sel` の字で判じる。
 */
void discard_stale_menu_if_needed()
{
    const int count = an_menu_choice_count();
    if (count <= 0) {
        return;
    }
    bool plausible = false;
    for (int i = 0; (i < count) && !plausible; ++i) {
        an_menu_choice_row row{};
        if (!an_menu_choice_at(i, &row)) {
            continue;
        }
        if (row.snapshot_len > 0) {
            // 日本語（設計 §3.2）: 写しは「桁 `min(width, 31)` を 32 バイトの器へ」取った
            // もの（`an_bootstrap.c`）。以前は写しの**バイト数**を桁数として読み直して
            // いた（英語では同じ値）。全角が混じると食い違うので、取ったときと同じ桁・
            // 同じ器で読み直す。
            char text[sizeof(row.snapshot)];
            const int want = std::min(row.width, static_cast<int>(sizeof(row.snapshot)) - 1);
            const int n = an_term_row_cols(row.row, row.col, want, text, static_cast<int>(sizeof(text)));
            if ((n == row.snapshot_len) && (std::memcmp(text, row.snapshot, static_cast<std::size_t>(n)) == 0)) {
                plausible = true;
            }
            continue;
        }
        if (row.sel == 0) {
            plausible = true; // 写しも選ぶ字も無い行は判じられないので、残す側へ倒す。
            break;
        }
        char text[1024];
        const int n = an_term_row(row.row, text, static_cast<int>(sizeof(text)));
        const int key_pos = row.col - 3;
        if ((n > 0) && (key_pos >= 0) && (key_pos < n)
            && (static_cast<unsigned char>(text[key_pos]) == static_cast<unsigned char>(row.sel))) {
            plausible = true;
        }
    }
    if (!plausible) {
        std::fprintf(stderr,
            "[angband] discarding a stale menu record (%d row(s); none matched the current term "
            "-- the screen changed without an_menu_begin() firing)\n",
            count);
        an_menu_discard_stale();
    }
}

/*!
 * @brief Term の写し（メニュー系の画）を `menu_term_lines` へ積む。
 * @details 設計 §6.2「icky 中の Term 全文ミラー（`menu_open` 時のみ Bridge が充填）」
 * に合わせ、呼ぶのは `frame.menu_open` が真のときだけ（`build_frame()` が分岐する）。
 * **空行は詰める**（設計 §6.3「`line_index` は `menu_term_lines` の添字へ直す（空行を
 * 詰めているので Term 行と一致しない）」）——選択肢・プロンプトの `line_index` も
 * ここで積んだ添字を指すので、詰めないと両方が食い違う。
 */
void fill_menu_mirror(GameFrame &frame)
{
    int cols = 0;
    int rows = 0;
    (void)an_term_size(&cols, &rows);

    const MenuActiveBox box = menu_active_box();
    const int box_min_row = box.valid ? box.row : -1;
    const int box_max_row = box.valid ? (box.row + box.page_rows - 1) : -1;
    {
        static int last_min = -99;
        static int last_max = -99;
        static int last_col = -99;
        static int last_width = -99;
        if ((box_min_row != last_min) || (box_max_row != last_max) || (box.col != last_col)
            || (box.width != last_width)) {
            last_min = box_min_row;
            last_max = box_max_row;
            last_col = box.col;
            last_width = box.width;
            std::fprintf(stderr, "[angband] menu active box: row=%d..%d col=%d width=%d\n", box_min_row,
                box_max_row, box.col, box.width);
        }
    }

    /*
     * AH-12。箱の外（行 0 は除く。上の分岐と同じ条件）の見出し・説明を捨てる前に
     * 別枠へ拾っておく——`fill_menu_mirror()` 自体の切り取りは変えない（AH-11 の
     * 「地図・状態列の混入を防ぐ」注意点をそのまま守る）。店の名前・所持金
     * （`store_display_frame()`。箱の上、行 1〜3）がここに来る。
     *
     * 見直しの指摘（AH-12）: 以前はここだけ「箱の外の全行を全幅で」拾って
     * いたため、地図の上に開くメニュー（持ち物・「Enter」コマンドメニュー等）
     * では箱の左右にある地図の ASCII や、画面下の状態欄・最下行まで
     * `menu_box_header_utf8`（→ `window.subtitle`）へ紛れ込み、AH-24 で
     * 除いたはずの地図混入が別経路から戻っていた。店の見出し（店名・所持金）は
     * **箱のすぐ上**（`box_min_row` の直前まで）に、**箱と同じ列幅**で描かれる
     * ——`store_display_frame()`（原作 `store.c`）は箱の外枠と同じ列に揃えて
     * 描くので、列を箱に合わせて切れば地図・状態列は入らない。さらに、
     * 店以外の画面（`an_menu_store_kind()==0`）では箱の上に見出しは無く、
     * 拾えるものは地図・状態欄しか無いので、店の画面に限って拾う。
     */
    {
        std::string header;
        if (box.valid && (an_menu_store_kind() != 0) && (box_min_row > 1)) {
            char aux_text[1024];
            // AH-40: 見出しは店の一覧の箱（`store_menu_region`。桁 1・4 行目から）の上の
            // 行 1〜3 にある。以前は「いまの箱」の桁と幅で切っていたので、一覧の上に開く
            // 小さな「Command for …」のメニュー（桁 2・幅 12）では見出しが途中で切られ、
            // 全角の途中で割れた字や一覧の 1 行目が見出しに混ざっていた。
            constexpr int kStoreHeaderCol = 1;
            constexpr int kStoreHeaderLastRow = 3;
            const int header_col = kStoreHeaderCol;
            const int header_width = std::max(0, cols - 1 - header_col);
            for (int y = 1; (y < box_min_row) && (y <= kStoreHeaderLastRow); ++y) {
                int n;
                if (header_width > 0) {
                    n = an_term_row_cols(y, header_col, header_width, aux_text,
                        static_cast<int>(sizeof(aux_text)));
                } else {
                    n = 0;
                }
                if (n > 0) {
                    if (!header.empty()) {
                        header += '\n';
                    }
                    header.append(aux_text, static_cast<std::size_t>(n));
                }
            }
        }
        frame.menu_box_header_utf8 = std::move(header);
    }

    char text[1024];
    unsigned char colors[256];
    for (int y = 0; y < rows; ++y) {
        if (box.valid && (y > 0) && ((y < box_min_row) || (y > box_max_row))) {
            continue; // 枠の外（地図・状態列の混入を防ぐ）。
        }
        const int crop = box_left_for_row(box, y);
        const int crop_width = (crop > 0) ? std::min(box.width, cols - crop) : cols;
        int n;
        if (crop > 0) {
            n = an_term_row_cols(y, crop, crop_width, text, static_cast<int>(sizeof(text)));
            if (n > 0) {
                // `an_term_row_cols()` は末尾の空白を詰めない（設計どおり範囲そのまま）ので、
                // ここで詰める（`an_term_row()` の「空行は詰める」と同じ扱いに揃える）。
                while ((n > 0) && (text[n - 1] == ' ')) {
                    --n;
                }
                text[n] = '\0';
            }
        } else {
            n = an_term_row(y, text, static_cast<int>(sizeof(text)));
        }
        if (n <= 0) {
            continue; // 範囲外、または空行（詰める）。
        }
        if (an_term_colors(y, colors, static_cast<int>(sizeof(colors))) < 0) {
            colors[0] = 0;
        }
        TermMirrorLine line;
        line.text_utf8 = text;
        line.color = (crop < cols) ? colors[crop] : 1;
        line.source_row = y;
        frame.menu_term_lines.push_back(std::move(line));
    }

    int cx = 0;
    int cy = 0;
    int cvisible = 0;
    if ((an_term_cursor(&cx, &cy, &cvisible) == AN_OK) && cvisible) {
        const int crop = box_left_for_row(box, cy);
        frame.menu_term_curs_col = cx - crop;
        frame.menu_term_curs_row = find_mirror_line_index(frame, cy); // 添字へ直す。
    }
}

/*!
 * @brief A1/A2 の記録から `menu_choices` を組む（設計 §6.3「選択肢」）。
 * @details `ui-menu.c` の `display_menu_row()` は「行を描いた直後」に A1 を呼ぶので、
 * ここに並んでいるのは**いま Term に出ているメニューの行だけ**（`an_menu_begin()` が
 * 前のメニューの記録を空にしている）。`row`/`col`/`width` は「`%c) ` の 3 桁を除いた
 * ラベルの範囲」（`ui-menu.c` の `display_menu_row()` 本体で `col += 3; width -= 3;`
 * してから渡している）ので、鍵の桁はそこから 3 桁左にある。
 *
 * ## 「応答待ちです」で止まる不具合の 2 回目の再発（見直しで特定）
 * 上の前提「`an_menu_begin()` が前のメニューの記録を空にしている」は、**次に
 * 開く画面が `struct menu` を使わない場合には成り立たない**。原作の birth の
 * 能力値割り振り画面（`ui-birth.c` の `point_based_command()`。得点方式・
 * ロール方式のどちらも同様）は `struct menu` を作らず、`Term_gotoxy()` +
 * `inkey_ex()` の生のループで直接描く——`display_menu_row()`/`menu_refresh()`
 * を一度も通らないので、A1/A2（`an_menu_row_drawn()`/`an_menu_begin()`）が
 * **一度も呼ばれない**。結果、`an_menu_rows[]` は 1 つ前の画面（例：ロール方式
 * 選択メニューの `a) Point-based` / `b) Roll for stats`）の行をそのまま
 * 持ち越す。`find_mirror_line_index()` は**行番号だけ**で今の Term と突き合わせる
 * ため、能力値割り振り画面がたまたま同じ行に文字を描いていると、無関係な
 * 文字列が「選択肢」として復活してしまう——しかも `row.cursor` も前の画面の
 * まま凍結しているので、新 UI 側から見ると「カーソルが指す行」が実際の入力に
 * 追随せず、下矢印を送っても見た目がまったく変わらない。
 *
 * 実機の `--protocol-log=`/stderr で確認: ロール方式選択で `a`（Point-based）を
 * 選んだ直後から、`an_menu_epoch()` が二度と変わらないまま（＝新しい
 * `struct menu` は一度も開かれていない）、下矢印 1 回の `ui_action`
 * （`action=navigate number=2`）が `request_id` を 100 回以上進めながら
 * 際限なく再送され続けていた——**古い記録の選択肢を選ぶたびに Angband 側の
 * カーソルは動く（`point_based_command()` の `dir==2` 分岐）が、新 UI へ返す
 * 選択肢の表示は凍結したままなので、新 UI から見ると要求がまったく効いて
 * いないように映り、際限なく同じ操作を送り直してしまう**。
 *
 * 直しは、**選ぶ字が付いている行だけ**、その字が今の Term の実際の文字と
 * 一致するときに限って選択肢として採用する（一致しなければ前の画面の
 * 古い記録なので黙って捨てる）。`an_menu_begin()` が呼ばれない画面へ古い記録が
 * 漏れ出す経路そのものを塞ぐ、より一般的な直し（例えば `an_menu_begin()` が
 * 呼ばれない全画面切り替えを検知して `an_menu_rows[]` を空にする）は原作へ
 * 新しい手入れを 1 つ足す必要があり、S3 の見直しの範囲を超える。ここでは
 * 既存の A1/A2 の記録の**読み出し側だけ**で確実に効く直しにとどめる。
 */
void fill_menu_choices(GameFrame &frame)
{
    const MenuActiveBox box = menu_active_box();

    const int count = an_menu_choice_count();
    for (int i = 0; i < count; ++i) {
        an_menu_choice_row row{};
        if (!an_menu_choice_at(i, &row)) {
            continue;
        }
        const int line_index = find_mirror_line_index(frame, row.row);
        if (line_index < 0) {
            continue; // Term に出ていない（空行として詰まれた）行は選べない。
        }
        // `fill_menu_mirror()` がこの行の文字列を左端から何桁切ったか（設計 §6.3。
        // `span_begin`/`key_begin` は `menu_term_lines[line_index].text_utf8` への
        // バイト位置なので、Term の絶対桁のままでは切った分だけずれる）。
        const int crop = box_left_for_row(box, row.row);

        MenuChoice choice;
        choice.line_index = line_index;
        // 見直しの指摘 11。以前はここを常に真にしていた（隠れた行が A1 まで
        // 来ないことは事実だが、「隠れてはいないが選べない行」（`valid_row()` が
        // `MN_ROW_INVALID` を返す行。例: 対象外のアイテムを読む・飲むときの一覧）
        // まで選べる扱いにしてしまっていた）。`an_menu_row_drawn()`（`an_bootstrap.c`）
        // が原作の `valid_row(menu, oid)` を呼んだ結果をそのまま使う。
        choice.available = (row.available != 0);
        choice.selected = (row.cursor != 0);
        if (row.sel != 0) {
            // 日本語（設計 §3.2）: `row.col` は Term の**桁**、`span_begin`/`key_begin` は
            // `text_utf8` の**バイト位置**。英語では同じ値だが、全角（3 バイト・2 桁）や
            // 埋め字（0 バイト・1 桁）が混じると食い違うので、桁 → バイトを Term から引く。
            const int key_pos = an_term_cols_to_bytes(row.row, crop, row.col - 3);
            const int span_bytes = an_term_cols_to_bytes(row.row, row.col - 3, row.col + row.width);
            const std::string &line_text = frame.menu_term_lines[line_index].text_utf8;
            // 上の註記のとおり：前の画面の古い記録でないことを、選ぶ字が今の Term に
            // 実際に描かれている文字と一致するかで確かめる。M0 は英語のみ
            // （ASCII）なので、桁とバイト位置は一致する。
            const bool key_matches = (key_pos >= 0)
                && (static_cast<std::size_t>(key_pos) < line_text.size())
                && (static_cast<unsigned char>(line_text[static_cast<std::size_t>(key_pos)])
                    == static_cast<unsigned char>(row.sel));
            if (!key_matches) {
                continue;
            }
            choice.key = static_cast<unsigned char>(row.sel);
            choice.key_begin = key_pos;
            choice.key_len = 1;
            choice.span_begin = key_pos;
            choice.span_len = span_bytes;
            if (row.sel == ' ') {
                /*
                 * AH-57: 選ぶ字が空白の行（自分のマスの文脈メニューの「Other」。原作
                 * `menu_dynamic_add_label(m, "Other", ' ', ...)`）。共用の窓は先頭の空白を刈ってから
                 * 「X) 」の 3 字を外すので、鍵の空白が先に消えて「) Other」と読んでしまう。
                 * 字を自分で切り出して渡す（`native_modern.h` は `label_utf8` があればそれを使う）。
                 */
                const std::size_t label_begin = static_cast<std::size_t>(key_pos) + 3; // 「 ) 」の 3 バイト
                if ((span_bytes > 3) && (label_begin < line_text.size())) {
                    std::string label = line_text.substr(label_begin, static_cast<std::size_t>(span_bytes) - 3);
                    while (!label.empty() && (label.back() == ' ')) {
                        label.pop_back();
                    }
                    choice.label_utf8 = std::move(label);
                }
            }
        } else {
            // 鍵が無い行——設計どおり「カーソルで選んで Enter」に任せる（key=0 のまま）。
            choice.span_begin = an_term_cols_to_bytes(row.row, crop, row.col);
            choice.span_len = an_term_cols_to_bytes(row.row, row.col, row.col + row.width);
        }
        frame.menu_choices.push_back(std::move(choice));
    }
}

/*!
 * @brief 誕生の種族・職業のメニューで、原作が右側に描く説明（能力の修正値・技能・
 * HP・経験値・赤外線視力・特性）を、カーソルのある選択肢の説明（`description_utf8`）
 * として新 UI の右の欄へ運ぶ（の 1）。
 * @details 原作 `ui-birth.c` は種族・職業のメニュー（`race_region`/`class_region`。
 * 行 `TABLE_ROW`=9、桁 2/19、幅 17）の `browse_hook`（`race_help()`/`class_help()`。
 * `ui-birth.c:533`）で、**メニューの箱の右**（桁 `RACE_AUX_COL`=19 /
 * `CLASS_AUX_COL`=36 から右）へ説明を描く。`fill_menu_mirror()` は地図・状態列の
 * 混入を防ぐために箱の外を切り落とす（AH-11/AH-24）ので、この説明は新 UI へ
 * 1 字も届かず、右の欄が空になっていた。
 *
 * `browse_hook` はカーソルが動くたびに描き直すので、その時点の Term の箱の右
 * （同じ行の範囲・箱の右端から画面の右端まで）を読めば、いまカーソルのある
 * 選択肢の説明になる。空行で段落を分け、段落の中の行と、行の中の 2 つ以上の
 * 空白（原作の欄の区切り）は「 / 」でつなぐ——画面側（`modern_content.h`）は
 * 説明の改行 1 つを詰めて 1 段落にするので、区切りを字で残しておく。
 * 他のメニュー・誕生の他の画面は箱の形（行 9・桁 2・幅 17 か 34）で除く。
 */
void fill_birth_browse_description(GameFrame &frame)
{
    if (!frame.pre_game_menu || frame.menu_choices.empty()) {
        return;
    }
    const MenuActiveBox box = menu_active_box();
    constexpr int kTableRow = 9; // ui-birth.c の TABLE_ROW
    constexpr int kRaceCol = 2; // ui-birth.c の RACE_COL
    // 実測（`[angband] menu active box` の記録）: 種族は桁 2・幅 17、職業は桁 2・幅 34
    // （職業の箱は左の種族の一覧の桁から始まる形で記録される）。どちらも箱の右端が
    // `RACE_AUX_COL`=19 / `CLASS_AUX_COL`=36 に当たる。能力値の決め方（幅 68）は説明が無い。
    // 職業の説明が空になった原因（2026-09-28）: 上の「職業は桁 2・幅 34」は、種族と職業の
    // メニューが**同じ回に続けて描かれた最初の 1 フレームだけ**の形（`an_menu_begin()` が
    // 左右に並ぶ 2 つの箱を合わせる。`an_bootstrap.c`）。カーソルを動かすと
    // `menu_select()` は職業のメニューだけを描き直すので、箱は `class_region`
    // （桁 `CLASS_COL`=19・幅 17）そのものになり、桁 2 の判定で外れていた。
    // 見分けは「箱の右端が `RACE_AUX_COL`=19 か `CLASS_AUX_COL`=36」で行う。
    constexpr int kRaceAuxCol = 19;  // ui-birth.c の RACE_AUX_COL
    constexpr int kClassAuxCol = 36; // ui-birth.c の CLASS_AUX_COL（= ROLLER_COL）
    constexpr int kRollerRight = 70; // ROLLER_COL + 34
    constexpr int kQuestionRow = 7;  // ui-birth.c の QUESTION_ROW
    if (!box.valid || (box.row != kTableRow)) {
        return;
    }
    const int box_right = box.col + box.width;
    if (box_right == kRollerRight) {
        // 能力値の決め方（`roller_menu`）は `browse_hook` が NULL で、選択肢ごとの説明を
        // 原作は描かない。描くのはメニュー全体の案内（`menu_question()` が QUESTION_ROW に
        // 黄色で出す `hint`。"Choose how to generate your intrinsic stats. Point-based is
        // recommended."）だけで、これは箱の外なので新 UI へ届いていなかった。原作の要素と
        // して、その案内をどの選択肢の説明にも入れる（カーソルを動かしても消えない）。
        char hint[512];
        const int n = an_term_row(kQuestionRow, hint, static_cast<int>(sizeof(hint)));
        if (n <= 0) {
            return;
        }
        std::string text(hint, static_cast<std::size_t>(n));
        const auto first = text.find_first_not_of(' ');
        if (first == std::string::npos) {
            return;
        }
        text.erase(0, first);
        for (auto &choice : frame.menu_choices) {
            if (choice.description_utf8.empty()) {
                choice.description_utf8 = text;
            }
        }
        return;
    }
    if ((box_right != kRaceAuxCol) && (box_right != kClassAuxCol)) {
        return;
    }
    (void)kRaceCol;
    MenuChoice *selected = nullptr;
    for (auto &choice : frame.menu_choices) {
        if (choice.selected) {
            selected = &choice;
            break;
        }
    }
    if (selected == nullptr) {
        return;
    }
    int cols = 0;
    int rows = 0;
    (void)an_term_size(&cols, &rows);
    const int aux_col = box.col + box.width;
    if (aux_col >= cols) {
        return;
    }
    const int last_row = std::min(rows - 1, box.row + std::max(0, box.page_rows - 1));

    std::string description;
    std::string paragraph;
    const auto flush = [&]() {
        if (paragraph.empty()) {
            return;
        }
        if (!description.empty()) {
            description += "\n\n";
        }
        description += paragraph;
        paragraph.clear();
    };
    char text[1024];
    for (int y = box.row; y <= last_row; ++y) {
        const int n = an_term_row_cols(y, aux_col, cols - aux_col, text, static_cast<int>(sizeof(text)));
        std::string line;
        if (n > 0) {
            // 行の中の 2 つ以上の空白を「 / 」に、1 つの空白はそのまま。前後の空白は捨てる。
            int spaces = 0;
            for (int i = 0; i < n; ++i) {
                if (text[i] == ' ') {
                    ++spaces;
                    continue;
                }
                if (!line.empty() && (spaces > 0)) {
                    // 「腕力:  +0」のように `%+3d` の埋めで空白が 2 つ並ぶ所は欄の区切りではない。
                    line += ((spaces >= 2) && (line.back() != ':')) ? " / " : " ";
                }
                spaces = 0;
                line += text[i];
            }
        }
        if (line.empty()) {
            flush();
            continue;
        }
        if (!paragraph.empty()) {
            paragraph += " / ";
        }
        paragraph += line;
    }
    flush();
    selected->description_utf8 = std::move(description);
}

/*!
 * @brief 我が家（`FEAT_HOME`）が空のとき、店の画面が「押せる物が無い読み物」に
 * 化けて止まって見える不具合の直し。
 * @details 根本原因: `ui-store.c` の店メニューは一覧が 0 件でも
 * `store_display_frame()`（見出し）・`store_redraw()`（"Press '?' for help."・
 * 所持金）を描き、`store_menu_set_selections()` が積んだ `menu->cmd_keys`
 * （`d`/`g`/`?`/ESC ほか）で普通に応答する——`menu_select()`
 * （`ui-menu.c:821`）は `cmd_keys` に載っている字を `menu_handle_keypress()` の
 * 「一覧が空なら読み捨てる」判定（同ファイル `count <= 0: eat = true`）より
 * **先に**振り分けるので、原作としては何も壊れていない。
 *
 * 壊れているのは A1/A2（`an_menu_row_drawn()`）が**一覧の行だけ**を見ていること
 * ——`cmd_keys` はここに来ない。`fill_menu_choices()` はその記録をそのまま
 * `frame.menu_choices` へ写すだけなので、一覧 0 件のときは選択肢も 0 件のまま
 * 新 UI へ届く。`native_modern.h` の `capture()` は「選択肢も無い・カーソルも
 * 無い」画面を最後の手段として `stage="detail"（`continue_key=13`）」へ落とす
 * ——だが Enter はこの店の `cmd_keys` に無いので、原作は
 * `menu_handle_keypress()` の「読み捨て」に入って**何も起きない**。押せる物が
 * 「続ける」（Enter）しか出ていない新 UI では、遊ぶ人はそこから一切先へ
 * 進めない（コアは正常に ESC を待っているだけなので、`--protocol-log=` で見ると
 * 「`menu_open=true`・選択肢 0 のまま止まる」ように見える）。
 *
 * 見出しの文字列（"Home Inventory"/"Store Inventory"）で「いま本当に店の
 * 画面である」ことを確かめる案は**実機で外れた**——`store_menu_region`
 * （`ui-store.c`。`{ 1, 4, -1, -2 }`）で `menu->active` は 4 行目から始まり、
 * 見出し（`store_display_frame()` が書く 1〜3 行目）は**箱の外**にある。
 * `fill_menu_mirror()` は `y==0` の行（行 0。ここでは使っていない）以外、
 * 箱の外の行を丸ごと飛ばす（見直しコメントのとおり「地図・状態列の混入を
 * 防ぐ」ため）ので、見出しは**この店に限らずどの店でも** `menu_term_lines`
 * に一度も出てこない。一覧が 0 件の店は箱の中身（アイテムの行）も無いので、
 * `menu_term_lines` は**丸ごと空**になる——これが気づいたことどおり
 * 「画面が出ず」に見える理由そのもの。
 *
 * ## 決めたこと（続き）: ESC だけでは足りない
 * 一覧 0 件は**最初に我が家へ入ったとき**そのものであり、そここそ「預ける」
 * （我が家なら `d`、店なら `s`。`store_menu_handle()` は両方とも
 * `store_sell(ctx)` を呼ぶので**同じ動作**）が要る場面——タッチしか無い
 * Android/Quest では ESC だけでは物を置けない。
 *
 * `an_menu_cmd_keys()`（`an_bootstrap.c`。`an_menu_begin()` が
 * `menu->cmd_keys` をそのまま持ち回るだけ）を読み、`'d'` が実際に含まれて
 * いるときだけ合成する——決め打ちではなく、原作がいま本当に受け付ける
 * コマンド鍵から作る（決めたどおり）。ラベルは原作の文言をそのまま
 * 使う：`ui-store.c` の `context_menu_store()`（マウスの右クリック相当の
 * 動的メニュー）が同じ 2 択を "Stash"（我が家）/"Sell"（それ以外の店）・
 * "Exit" という語で出しているので、それに合わせる（`an_menu_store_kind()`
 * で我が家かどうかを判じる）。
 *
 * `an_menu_choice_count()==0` かつ `an_menu_active_region()` が有効
 * （＝`an_menu_begin()` が実際に呼ばれた＝`struct menu` を使う本物の画面）
 * であることを前提にする。`discard_stale_menu_if_needed()` の「古い枠」
 * 注意点（`fill_birth_point_based_cursor()` のコメント）は一覧が 1 件以上
 * あるときにしか働かないので、一覧 0 件のまま別の非メニュー画面へ移った
 * 直後の 1 フレームだけ、この箱が古いまま残る余地はある——が、そこで
 * 合成するのは ESC（Angband のほぼ全画面で「戻る／閉じる」）と、
 * `an_menu_store_kind()` が 0（店ではない）なら出さない「預ける」の 2 つ
 * だけなので、たとえ的外れの画面に出ても実害はない。
 */
void fill_empty_store_escape_choice(GameFrame &frame)
{
    if (!frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)) {
        return; // 他の合成（birth ほか）が既に何か積んでいるなら譲る。
    }
    if (an_menu_choice_count() != 0) {
        return; // 一覧に本物の行があるなら、通常の経路で選べている。
    }
    if (!menu_active_box().valid) {
        return; // struct menu が開いていない（`an_menu_begin()` が呼ばれていない）。
    }

    // `line_index` を指す実在の Term 行が無い（見出しは箱の外で写されない・
    // 一覧は 0 件）ので、`native_modern.h` の `capture()`（`window.stage=="choices"`
    // の枝）が `label_utf8` を字幕としてそのまま使う経路に乗せる——
    // `line_index` が範囲外でも `label_utf8` が空でなければ捨てられない
    // （同ファイルの `if ((line_index<0||...) && label_utf8.empty()) continue;`）。
    const int store_kind = an_menu_store_kind(); // 0=店ではない／1=我が家／2=それ以外の店
    const char *const cmd_keys = an_menu_cmd_keys();
    if ((store_kind != 0) && cmd_keys && std::strchr(cmd_keys, 'd')) {
        MenuChoice deposit;
        deposit.key = 'd';
        deposit.available = true;
        deposit.selected = false;
        deposit.line_index = -1;
        deposit.label_utf8 = ui_text((store_kind == 1) ? "Stash" : "Sell");
        frame.menu_choices.push_back(std::move(deposit));
    }

    MenuChoice exit_choice;
    exit_choice.key = 27; // ESCAPE。この店の `cmd_keys` には無いが、`menu_handle_keypress()`
                           // が `count <= 0` の判定より前に ESCAPE を直接見ているので必ず効く。
    exit_choice.available = true;
    exit_choice.selected = false;
    exit_choice.line_index = -1;
    exit_choice.label_utf8 = ui_text("Exit");
    frame.menu_choices.push_back(std::move(exit_choice));
}

/*!
 * @brief AH-06/AH-09: 現在のメニューの `cmd_keys`/`switch_keys`（行に紐付かない
 * 追加コマンド）を、パッド・タッチ向けの常設ボタン（`ModernWindow::actions`）へ
 * 変換する。
 * @details `native_modern.h` の `NativeModernMenu::capture()` は
 * `stage=="choices"||stage=="cursor"` のとき、渡された `declared_actions` を
 * そのままボタンとして足す（`window_action` イベントで `key` を直接送る——
 * `native_key` の「一覧の行にあるキーだけ通す」制限（AH-09 の根拠。同ファイルの
 * `event.action=="native_key"` の分岐）を経由しない）。
 *
 * キーボードは既にこれらのキーを送れている——`hd2d/ui/modern_store_ui.cpp` の
 * `handle_native_key()` は `window.kind=="dialog"`（Angband は常にこれ）なら
 * 行に一致しない生キーも `native_key` として送り、コア側 `native_modern.h` も
 * `stage=="cursor"` では素通しする（`stage=="choices"` のときだけ「一覧の行に
 * あるキーだけ通す」制限に掛かる）。抜けているのは**物理キーボードの無い環境
 * （パッド・タッチ）向けのボタンだけ**なので、ここは見た目を足すだけで
 * キーボードでの振る舞いは変えない——他の 5 コア・Android は
 * `an_menu_cmd_keys()`/`an_menu_switch_keys()` を一切呼ばないので影響しない。
 *
 * `cmd_keys`/`switch_keys` を機械的に 1 文字ずつボタン化すると、同じ動作に
 * 2 つの綴り（'s'/'d' の売る・預ける、'x'/'l' の調べるなど。原作が新旧の
 * キーマップ両方を同時に受け付けるための重複）が両方ボタンとして出てしまう
 * ——ここは**意味ごとに 1 個**にまとめる。載せていないキー（^D/^E の切替・
 * 'p'/'g'/'x' の旧式プロンプト・'{'/'}' の銘・'C'/'~'/'w'/'=' ほか）は
 * Enter で選ぶ通常の手段と重なるだけの物や、AH-06/AH-09 の報告に出ていない
 * ものなので割愛した（遊ぶ人から要望があれば足す）。
 */
std::vector<ModernAction> an_declared_menu_actions(const GameFrame &frame)
{
    std::vector<ModernAction> actions;
    if (!frame.menu_open) {
        return actions;
    }
    const char *const cmd_keys = an_menu_cmd_keys();
    const char *const switch_keys = an_menu_switch_keys();
    auto has_key = [&](char c) {
        return (cmd_keys && std::strchr(cmd_keys, c) != nullptr)
            || (switch_keys && std::strchr(switch_keys, c) != nullptr);
    };
    auto add = [&actions](int key, const char *label) {
        ModernAction action;
        action.id = std::string("cmdkey:") + std::to_string(key);
        action.label = ui_text(label);
        action.enabled = true;
        action.key = key;
        actions.push_back(std::move(action));
    };

    // AH-06: 店の「売る・預ける」。一覧が 0 件のときは
    // `fill_empty_store_escape_choice()` が別の仕組み（`frame.menu_choices`）で
    // 既に出しているので、ここでは一覧が 1 件以上あるとき（AH-06 の本題）
    // だけ足す。ラベルは同関数と同じ判じ方（我が家なら Stash・それ以外は Sell）。
    if (has_key('d') && an_menu_choice_count() != 0) {
        const int store_kind = an_menu_store_kind();
        add('d', (store_kind == 1) ? "Stash" : "Sell");
    }
    // AH-09: アイテム選択画面（`item_menu()`）の一覧切り替え、店の I/e。
    if (has_key('/')) {
        add('/', "Switch source (Inventory / Equipment)");
    }
    if (has_key('-')) {
        add('-', "Floor items");
    }
    if (has_key('|')) {
        add('|', "Quiver");
    }
    if (has_key('I')) {
        add('I', "Inspect");
    }
    if (has_key('e')) {
        add('e', "Equipment");
    }
    if (has_key('i')) {
        add('i', "Inventory");
    }
    // AH-52: 誕生の種族・職業・ロールの選択（`ui-birth.c` の `menu_question()`。cmd_keys は "?=*@^X"）の
    // `*`（いまの項目をランダムに決める）・`@`（残りをランダムで決めて確認へ進む）・`=`（誕生時のオプション）。
    // ロールの選択（得点方式・標準ロール。2 項目）は `allow_random` が偽で `*` を受けないので出さない。
    if (frame.pre_game_menu) {
        if (has_key('*') && (an_menu_choice_count() != 2)) {
            add('*', "Select one at random");
        }
        if (has_key('@')) {
            add('@', "Finish with random choices");
        }
        if (has_key('=')) {
            add('=', "Birth options");
        }
    }
    // AH-62: 誕生の名前入力（`get_character_name()`）の `*`（ランダムな名前）。文字入力の段
    // （`stage=="text"`）の操作として出す。キーボードの無い環境（パッド・タッチ）で押せるようにする。
    if (frame.player_name_input_active && has_key('*') == false) {
        add('*', "Random name");
    }
    // AH-55: モンスター一覧（`[`。`monster_list_show_interactive()`）の `x`（経験値の順の切り替え）。
    // 一覧の 1 行目が「Press 'x' to turn ON/OFF 'sort by exp'」。押すと並べ直して開き直す。
    for (const auto &line : frame.menu_term_lines) {
        if (contains_marker(line.text_utf8, "Press 'x' to turn ON 'sort by exp'")) {
            add('x', "Sort by experience");
            break;
        }
        if (contains_marker(line.text_utf8, "Press 'x' to turn OFF 'sort by exp'")) {
            add('x', "Sort in the standard order");
            break;
        }
    }
    if (has_key('?')) {
        add('?', "Help");
    }
    // AH-60: ヘルプの目次。原作の `show_file()` は目次でも `%`（ファイルへ移動。`Goto File:`）を受けるが、
    // 目次の選択肢は英字の行だけなので、窓は `%` を「この画面では使えないキー」として捨て（`native_key` は
    // 選択肢の段では行にあるキーだけ通す）、操作の一覧にも出なかった。`declared_actions` にすると、キーは
    // `window_action` として届く（`modern_store_ui.cpp` の `handle_native_key()` は操作の一覧のキーを先に見る）。
    if (is_help_index_frame(frame)) {
        add('%', "Go to file");
    }
    return actions;
}

/*!
 * @brief birth の能力値割り振り画面（`ui-birth.c` の `point_based_command()`）専用の
 * カーソル合成。
 * @details この画面は `struct menu` を使わないので A1/A2 が呼ばれず、
 * `fill_menu_choices()` は（`discard_stale_menu_if_needed()` の直しの後は）
 * 空のまま——放っておくと新 UI からは「選択肢の無い読み物」（`stage="detail"`）
 * にしか見えず、遊ぶ人がどの能力値にポイントを振るか選べない。
 *
 * 手本は Sil-Q（`silq/adapter/sq_menu.cpp` の `known_choice_menu()`）が同じ形の
 * 画面（得点方式の能力値・技能の割り振り）を扱っている手口——**その原作の
 * 目印（見出し・脚注の文言）でこの画面だと確かめてから、選ばれている行を
 * 拾って `frame.menu_core_cursor` を合成する**。Sil-Q は選ばれている行を色
 * （`attrs[x]==14`）で拾っているが、Angband の `point_based_command()` は
 * 色を変えず、`Term_gotoxy(COSTS_COL + 4, COSTS_ROW + stat)`（`ui-birth.c`）で
 * 素の Term カーソルを置くだけなので、こちらは `an_term_cursor()` から拾った
 * `frame.menu_term_curs_row` をそのまま使う。
 *
 * 目印文言はこの画面が最下段に出す脚注そのもの（`ui-birth.c`
 * `point_based_start()`）: `"[up/down to move, left/right to modify, 'r' to
 * reset, 'Enter' to accept]"`。`frame.menu_core_cursor.key = 13` は Enter
 * （`ACT_CTX_BIRTH_PTS_ACCEPT`）。上下（`navigate` action、`stage=="cursor"`）は
 * `NativeModernMenu::input()`（`presentation/bridge/native_modern.h`。読むだけ・
 * 変えない）がそのまま `'2'`/`'8'` を積み、`point_based_command()` の
 * `target_dir()` がそれを方向として受ける（`dir==8`/`dir==2` で前後の能力値へ
 * 移動）。増減は `'+'`/`'-'` が `stage=="detail"` 相当の `native_key` 経由で
 * 既にそのまま届く（`event.action == "native_key"` の分岐）ので、ここでは
 * カーソル行の合成だけを足せばよい。
 */
void fill_birth_point_based_cursor(GameFrame &frame)
{
    // 既に別の経路で選択肢/カーソルが埋まっているなら手を出さない。
    if (!frame.pre_game_menu || !frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)) {
        return;
    }
    static const char *const kFooter
        = "[up/down to move, left/right to modify, 'r' to reset, 'Enter' to accept]";
    bool found_footer = false;
    for (const auto &line : frame.menu_term_lines) {
        if (contains_marker(line.text_utf8, kFooter)) {
            found_footer = true;
            break;
        }
    }
    if (!found_footer) {
        return;
    }
    if ((frame.menu_term_curs_row < 0) || (frame.menu_term_curs_row >= static_cast<int>(frame.menu_term_lines.size()))) {
        return;
    }
    MenuChoice cursor;
    cursor.line_index = frame.menu_term_curs_row;
    cursor.key = 13;
    frame.menu_core_cursor = cursor;
    frame.menu_core_cursors.assign(1, cursor);
}

/*!
 * @brief birth の最終確認画面（`ui-birth.c` の `get_confirm_command()`）専用の
 * 選択肢の合成。
 * @details この画面も `struct menu` を使わない raw `inkey()` の画面（脚注
 * `"['ESC' to step back, 'S' to start over, or any other key to continue]"`）。
 * 原作の判定は `'S'/'s'` だけが「最初からやり直す」で、`ESC` は 1 つ前へ戻る、
 * それ以外の**あらゆる**キー（`Enter` を含む）が「続ける（確定）」——この
 * 「それ以外のあらゆるキー」を、共用の新 UI 側（`presentation/bridge/
 * native_modern.h`。読むだけ・変えない）の「かっこ書きの脚注から操作を
 * 取り出す」一般化処理に任せると、脚注中で唯一の「'X' で〜」という体裁を
 * 持つ `'S' to start over` の方を拾ってしまい、決定（Enter）がそちらに
 * 吸われる（遊ぶ人の実機確認: `ui_action` が `action=window_action
 * target=key:83`（'S'）になり、種族選択まで巻き戻った）。
 *
 * 手本は Sil-Q（`silq/adapter/sq_menu.cpp` の `known_choice_menu()`）——
 * 同じ「原作の脚注を機械的に拾わせるのではなく、アダプタが選択肢を
 * 明示する」考え方。`NativeModernMenu::capture()`
 * （`presentation/bridge/native_modern.h`）は `frame.menu_choices` が
 * 埋まっていれば `window.stage = "choices"` にしてそのまま使う（共用の
 * 脚注取り出し処理より**先に**通る）ので、ここで 3 つの選択肢を
 * `key` つきで明示すれば、共用処理の一般化に横取りされない。
 * 既定は先頭の「続ける」。`selected` は立てない（AH-44。立てると画面が
 * 原作のカーソルとして扱い、上下で '2'/'8' を送り続けて止まる）。
 * `native_pregame_selection()`（同じ `native_modern.h`）は `> ` の行と
 * セーブ選択の見出しが無ければ手を出さないので、この画面は横取りされない。
 */
void fill_birth_final_confirm_choices(GameFrame &frame)
{
    if (!frame.pre_game_menu || !frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)) {
        return;
    }
    static const char *const kFooter
        = "['ESC' to step back, 'S' to start over, or any other key to continue]";
    bool found_footer = false;
    for (const auto &line : frame.menu_term_lines) {
        if (contains_marker(line.text_utf8, kFooter)) {
            found_footer = true;
            break;
        }
    }
    if (!found_footer) {
        return;
    }

    auto add_choice = [&frame](int key, const char *label, bool selected) {
        MenuChoice choice;
        choice.line_index = -1; // Term の特定の行を指さない（label_utf8 で表す）。
        choice.key = key;
        choice.label_utf8 = ui_text(label);
        choice.selected = selected;
        choice.available = true;
        frame.menu_choices.push_back(std::move(choice));
    };
    // 原作の判定順（'S'/ESC を先に見る）とは逆に、既定で選ばれているものを
    // 先頭に置く——「続ける」を誤操作しにくい既定にする（設計判断。
    // ゲームのルールは変えていない。原作へ渡すキーは 13/'S'/27 のまま）。
    add_choice(13, "Continue", false); // AH-44: selected は立てない（下の註記）。
    add_choice('S', "Start over", false);
    add_choice(27, "Step back", false);
}

/*!
 * @brief AH-41: 店の一覧を `NativeStoreMenu` へ渡す（手本は `frox/adapter/fc_main.cpp` の同じ所）。
 * @details 行は店の品（tab 0。買う＝'p' → 行の字）と持ち物（tab 1。売る・預ける＝'d' →
 * 原作の持ち物の一覧の字を名前で引く）。一覧そのものが開いているとき（`root_frame`）は、
 * A1 の記録（一覧の各行）を選択肢から外す——行はすべて店の品として出しているうえ、
 * 「Cloak [1,+0]」のような '[' を含む名前が「店の操作」と取り違えられるため。
 */
std::uint16_t an_tile_for_row(int kind_id, int unaware_category)
{
    if (kind_id) {
        char name[128] = {};
        int tval = 0;
        if (an_kind_name_and_tval(kind_id, name, sizeof(name), &tval)) {
            const auto found = g_tile_mapping.lookup_object(name, tval);
            if (found.tile_index) {
                return found.tile_index;
            }
        }
    }
    if (unaware_category) {
        return g_tile_mapping.lookup_unaware_representative(
            static_cast<presentation::UnawareItemCategory>(unaware_category));
    }
    return 0;
}

void capture_store_menu(GameFrame &frame)
{
    native_store_info info{};
    std::vector<ModernRow> rows;
    if (an_native_store_info(&info)) {
        const bool ja = an::lang_code() != "en";
        // AH-42: 原作が店の一覧でいま受け付けるコマンド鍵（`store_menu_set_selections()`）。
        const char *const cmd_keys = an_menu_cmd_keys();
        auto has_key = [cmd_keys](char c) { return cmd_keys && std::strchr(cmd_keys, c) != nullptr; };
        // 店の品を調べる（`store_menu_handle()` の 'l'/'x'。どちらも同じ `store_examine()`）。
        const int examine_key = has_key('l') ? 'l' : (has_key('x') ? 'x' : 0);
        // 売り買いの確認（`store_get_check()`）が行 0 に出ている間は、メニューが店の一覧の
        // ままでも一覧の画ではない（確認は `struct menu` を開かないので、覚えているメニューは
        // 一覧のまま）。一覧として扱わない。
        {
            char row0[256];
            const int n0 = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
            // 行 0 には買った後の「You have 4 Wooden Torches (d).」のような普通のメッセージも
            // 残るので、見るのは確認の脚注だけ（英日とも）。
            if ((n0 > 0) && contains_marker(std::string(row0, static_cast<std::size_t>(n0)), "[ESC, any other key to accept]")) {
                info.root_frame = 0;
            }
        }
        // AH-42: 「調べる」（'l'）・「持ち物を調べる」（'I'）の説明（`textui_textblock_show()`。
        // `struct menu` を開かないので、覚えているメニューは店の一覧のまま）が出ている間も
        // 一覧の画ではない。一覧として扱うと、説明が新 UI に出ないまま原作が何かキーを待つ。
        {
            int tb_row = 0;
            int tb_col = 0;
            int tb_width = 0;
            int tb_rows = 0;
            if (an_textblock_active_region(&tb_row, &tb_col, &tb_width, &tb_rows)) {
                info.root_frame = 0;
                // 選択肢は下に隠れた店の一覧の字（A1 の記録）なので渡さない。説明は
                // 「何かキーで閉じる」だけの画面として新 UI の既定（続ける）に任せる。
                frame.menu_choices.clear();
            }
        }
        for (int i = 0;; ++i) {
            native_store_item raw{};
            if (!an_native_store_item(i, &raw)) {
                break;
            }
            if (!raw.count) {
                continue;
            }
            ModernRow row;
            row.tab = raw.tab;
            row.slot = raw.slot;
            row.selection = raw.selection;
            row.key = raw.key;
            row.hotkey = raw.key;
            row.count = raw.count;
            row.price = raw.price;
            row.price_known = raw.price_known != 0;
            row.available = raw.available != 0;
            row.equipped = raw.location == 1;
            row.label = raw.name;
            row.weight = raw.weight;
            row.reason = raw.reason;
            row.tile = an_tile_for_row(raw.kind_id, raw.unaware_category);
            row.glyph = raw.glyph;
            row.glyph_color = raw.glyph_color;
            row.id = presentation::native_store_id(info.kind, raw, row.label);
            const bool home = info.kind == 2;
            const char *action = raw.tab ? (home ? "deposit" : "sell") : (home ? "take" : "buy");
            const char *label = raw.tab ? (home ? "Stash" : "Sell") : (home ? "Take" : "Buy");
            (void)ja;
            row.actions.push_back({ action, ui_text(label), row.reason, row.available, raw.tab ? info.put_key : info.get_key });
            if (!raw.tab && examine_key) {
                // AH-42: 以前は一覧の行を選ぶと原作の文脈メニュー（買う・調べる）が出ていた。
                // 新 UI の店では行の操作が「買う」だけになったので、「調べる」も行に付ける
                // （'l' → 原作の「Examine which item?」→ 行の字。買うと同じ手順）。
                row.actions.push_back({ "examine", ui_text("Examine"), "", true, examine_key });
            }
            rows.push_back(std::move(row));
        }
        std::string heading;
        {
            // 見出し（店主の名前と店の名前・資金）は `fill_menu_mirror()` が拾った行 1。欄の区切りの長い空白は 2 つに詰める。
            const std::string &src = frame.menu_box_header_utf8;
            const std::string first = src.substr(0, src.find(static_cast<char>(10)));
            int spaces = 0;
            for (char c : first) {
                if (c == ' ') {
                    ++spaces;
                    continue;
                }
                if (!heading.empty() && (spaces > 0)) {
                    heading += (spaces >= 2) ? "  " : " "; // 欄の区切り（2 つ以上）は 2 つに詰める。
                }
                spaces = 0;
                heading += c;
            }
        }
        if (info.root_frame) {
            // AH-42: 一覧の行に紐付かない原作のコマンド（`menu->cmd_keys`）を、パッド・タッチから
            // 押せる行として足す。`NativeStoreMenu` は `frame.menu_choices` から「店の操作」を
            // 作るが、下で選択肢を空にする（一覧の行を店の品と取り違えないため）ので、ここで
            // 出さないと 35ed0e871 より前に `an_declared_menu_actions()` が出していたボタン
            // （?・I・e・i・|）が消える。選ぶ集合はその関数と同じ。
            auto add_command = [&](int key, const char *label, std::string description) {
                ModernRow command;
                command.id = "store:command:" + std::to_string(key);
                command.tab = 0;
                command.slot = info.top;
                command.price_known = false;
                command.label = ui_text(label);
                command.description = std::move(description);
                command.actions.push_back({ "command:" + std::to_string(key), command.label, "", true, key });
                rows.push_back(std::move(command));
            };
            if (has_key('?')) {
                // 原作の `?` はヘルプの段落（`store_display_help()`）を一覧の下に出し入れするだけ。
                // 出ている間は Term の下 3 行（`store_display_recalc()` の `hgt -= 3`）を読み、
                // この行の説明として詳細の欄に出す。
                std::string help;
                int cols = 0;
                int term_rows = 0;
                if (an_menu_store_help_shown() && (an_term_size(&cols, &term_rows) == AN_OK) && (term_rows > 3)) {
                    for (int y = term_rows - 3; y < term_rows; ++y) {
                        char line[512];
                        const int n = an_term_row(y, line, static_cast<int>(sizeof(line)));
                        std::string text = (n > 0) ? std::string(line, static_cast<std::size_t>(n)) : std::string();
                        const auto first = text.find_first_not_of(' ');
                        const auto last = text.find_last_not_of(' ');
                        if (first == std::string::npos) {
                            continue;
                        }
                        if (!help.empty()) {
                            help += ' ';
                        }
                        help += text.substr(first, last - first + 1);
                    }
                }
                add_command('?', "Help", std::move(help));
            }
            if (has_key('I')) {
                add_command('I', "Inspect", {});
            }
            if (has_key('e')) {
                add_command('e', "Equipment", {});
            }
            if (has_key('i')) {
                add_command('i', "Inventory", {});
            }
            if (has_key('|')) {
                add_command('|', "Quiver", {});
            }
            // AH-56: 店の中でも使える持ち物のコマンド（`store_process_command_key()`。銘・装備・外す・無視）。
            // 選ぶと原作のコマンドの流れに入る（アイテムを選ぶ → 実行。銘は文字入力の窓）。
            if (has_key('{')) {
                add_command('{', "Inscribe an object", {});
            }
            if (has_key('}')) {
                add_command('}', "Uninscribe an object", {});
            }
            if (has_key('w')) {
                add_command('w', "Wear/wield an item", {});
            }
            if (has_key('t')) {
                add_command('t', "Take off/unwield an item", {});
            }
            if (has_key('k')) {
                add_command('k', "Ignore an item", {});
            }
        }
        g_store_menu.set_title(info.title);
        g_store_menu.set_heading(heading.empty() ? std::string(info.title) : heading);
        if (info.root_frame) {
            frame.menu_choices.clear();
        }
    }
    g_store_menu.capture(frame, info, std::move(rows));
}

/*!
 * @brief 店の売り買いの最後の確認（`ui-store.c` の `store_get_check()`）の選択肢を合成する
 *
 * @details `store_get_check()` は `get_check()` を通らず、行 0 に
 * `"Buy 3 Rations of Food? [ESC, any other key to accept]"`（行 1 に `Price: 9`）を
 * `prt()` して生の `inkey()` を待つ——ESC と N/n だけが取り消しで、他のキーはすべて決定。
 * `an_get_check_state()` に載らず `struct menu` も無いので、以前は `stage="detail"` に落ち、
 * 本文に店の一覧全体が並び、左の一覧には行 0 の問いだけが「押しても何も起きない行」として
 * 出ていた（実窓で Enter を押しても `ui_action` が 1 つも出ず、買えなかった）。
 * 確認の問いであることを原作の脚注（英日とも `contains_marker()`）で確かめ、
 * 「決定」（'y'）と「取り消し」（ESC）を明示する。説明には問いと値段の行を入れる。
 */
void fill_store_confirm_choices(GameFrame &frame)
{
    if (frame.pre_game_menu) {
        return;
    }
    if (an_menu_store_kind() == 0) {
        return;
    }
    static const char *const kFooter = "[ESC, any other key to accept]";
    char row0[512];
    const int n0 = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
    if ((n0 <= 0) || !contains_marker(std::string(row0, static_cast<std::size_t>(n0)), kFooter)) {
        return;
    }
    // 確認は `struct menu` を開かないので、A1 の記録（店の一覧の行）は画面に残ったまま
    // 選択肢として積まれている。確認の間それらは選べない（原作は次の 1 キーを確認の答えと
    // してしか読まない）ので捨てる——残すと一覧を選ぶ画に見え、押した字が「決定」になる。
    frame.menu_choices.clear();
    frame.menu_core_cursor = MenuChoice{};
    frame.menu_core_cursors.clear();
    // 本文・見出しも問い（行 0）と値段（行 1）だけにする。店の一覧の写しや、一覧の箱の
    // 上から拾った見出し（ここでは値段の行の切れ端）が確認の窓に並ばないように。
    frame.menu_box_header_utf8.clear();
    std::erase_if(frame.menu_term_lines, [](const TermMirrorLine &line) { return line.source_row > 1; });
    std::string description(row0, static_cast<std::size_t>(n0));
    char row1[512];
    const int n1 = an_term_row(1, row1, static_cast<int>(sizeof(row1)));
    if (n1 > 0) {
        description += "\n\n"; // 画面側は改行 1 つを詰めるので段落で分ける。
        description.append(row1, static_cast<std::size_t>(n1));
    }
    auto add_choice = [&](int key, const char *label, bool selected) {
        MenuChoice choice;
        choice.line_index = -1;
        choice.key = key;
        choice.label_utf8 = ui_text(label);
        choice.description_utf8 = description;
        choice.selected = selected;
        choice.available = true;
        frame.menu_choices.push_back(std::move(choice));
    };
    add_choice('y', "Accept", false); // AH-44
    add_choice(27, "Cancel (Esc)", false);
}

/*!
 * @brief birth の履歴確認（`ui-birth.c` の `get_history_command()`）専用の選択肢の合成
 * （2026-09-23。「応答待ち」3 回目の調査で見つけた止まり所）。
 * @details この画面は Term 行 0 に `"Accept character history? [y/n]"` を `prt()` して
 * 素の `inkey()` で読む——**`get_check()` を通らない**ので `an_ui_shim.c` のフックに
 * 掛からず、`frame.prompt`（確認）にならない。何も足さないと新 UI には
 * 「選択肢の無い読み物」（`stage="detail"`）として届き、画面側
 * （`hd2d/ui/modern_store_ui.cpp`）はそれを見出し 1 行の文書として扱って、
 * Enter を「見出しの節を開く（詳細欄へ焦点を移す）」に消費する——実機で
 * 確認: Enter を何度送っても `ui_action` が 1 つも出ず、Space（`native_key`）
 * だけが通った。原作の判定は ESC＝1 つ前へ戻る／`n`＝履歴を編集／それ以外の
 * あらゆるキー（Enter を含む）＝承認。上の最終確認と同じく、選択肢を明示して
 * `stage="choices"` にし、既定（`selected`）を「承認」にする。原作へ渡すキーは
 * `'y'`/`'n'`/`27` で、ゲームのルールは変えていない。
 */
void fill_birth_history_choices(GameFrame &frame)
{
    if (!frame.pre_game_menu || !frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)) {
        return;
    }
    static const char *const kPrompt = "Accept character history? [y/n]";
    bool found = false;
    for (const auto &line : frame.menu_term_lines) {
        if ((line.source_row == 0) && starts_with_marker(line.text_utf8, kPrompt)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }

    auto add_choice = [&frame](int key, const char *label, bool selected) {
        MenuChoice choice;
        choice.line_index = -1;
        choice.key = key;
        choice.label_utf8 = ui_text(label);
        choice.selected = selected;
        choice.available = true;
        frame.menu_choices.push_back(std::move(choice));
    };
    add_choice('y', "Accept", false); // AH-44
    add_choice('n', "Edit history", false);
    add_choice(27, "Step back", false);
}

/*!
 * @brief AH-05: 死後の New Game・死んだキャラのセーブのクイックスタート確認
 * （`ui-birth.c` の `textui_birth_quickstart()`）専用の選択肢の合成。
 * @details `fill_birth_history_choices()` と同じ理由で止まる——素の `inkey()` で
 * 読む画面（`struct menu` も `get_check()` も通らない）なので、A1/A2 のフックにも
 * `an_ui_shim.c` のフックにも掛からず、新 UI には「選択肢の無い読み物」にしか
 * 見えない。ただしこの画面はプロンプトが **Term 最終行**（`Term->hgt - 1`）に
 * 出る点が `fill_birth_history_choices()`（行 0）と違うので、`an_term_size()` で
 * 実際の行数を引いてからその行を読む。原作の判定は 'Y'/'y'＝そのまま使う・
 * 'N'/'n'＝作り直す・'C'/'c'＝名前と生い立ちを変える・'='＝誕生オプション
 * （こちらは選択肢を閉じずに設定画面を開くだけなので、ここでは合成しない——
 * 新 UI からは「作り直す」で足りる。**遊ぶ人が実際に必要とすれば別途足す**）。
 * 既定（`selected`）は「そのまま使う」——Sil-Q の似た確認と同じ考え方
 * （決めたこと 2026-08-23「既定で選ばれているものを誤操作しにくい既定にする」
 * という `fill_birth_final_confirm_choices()` の設計判断を踏襲）。
 */
void fill_birth_quickstart_choices(GameFrame &frame)
{
    if (!frame.pre_game_menu || !frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)) {
        return;
    }
    static const char *const kFooter
        = "['Y': use as is; 'N': redo; 'C': change name/history; '=': set birth options]";
    int cols = 0;
    int rows = 0;
    if (an_term_size(&cols, &rows) != 0 || (rows <= 0)) {
        return;
    }
    char last_row[256];
    const int last_row_len = an_term_row(rows - 1, last_row, static_cast<int>(sizeof(last_row)));
    if (last_row_len <= 0) {
        return;
    }
    const std::string text(last_row, static_cast<std::size_t>(last_row_len));
    if (!contains_marker(text, kFooter)) {
        return;
    }

    auto add_choice = [&frame](int key, const char *label, bool selected) {
        MenuChoice choice;
        choice.line_index = -1; // 特定の Term 行は指さない（見出しは行 0、脚注は最終行で 1 対 1 にならない）。
        choice.key = key;
        choice.label_utf8 = ui_text(label);
        choice.selected = selected;
        choice.available = true;
        frame.menu_choices.push_back(std::move(choice));
    };
    add_choice('y', "Use as is", false); // AH-44
    add_choice('n', "Redo", false);
    add_choice('c', "Change name/history", false);
    add_choice('=', "Birth options", false); // AH-52: 原作の `=`（`do_cmd_options_birth()`）。
}

/*!
 * @brief AH-07: 変身中に別の行動をしたときの「元の姿に戻るか」の選択肢の合成
 * （`player-util.c` の `player_get_resume_normal_shape()`）。
 * @details `get_check()` ではなく `get_char(prompt,"yrn",3,'n')`（`ui-input.c`）を
 * 直接呼ぶ画面なので、`an_ui_shim.c` の `get_check` フックにも `frame.prompt`
 * （`fill_prompts()` の `an_get_check_state()` 経由）にも掛からない。しかも
 * `screen_save()` を挟まないので `an_menu_open()` も偽のまま——`fill_birth_*_choices()`
 * のように `frame.menu_open` の中で呼ぶわけにはいかない（この画面はダンジョンの
 * 通常プレイ中に出る）。`fill_prompts()` と同じく、`menu_open` の真偽に
 * かかわらず**ここから独立して**呼ぶ。
 *
 * 文言は `cmd_verb(cmd->code)`（実行しようとした原コマンドの名 - 例: "read"・
 * "eat"）で変わるので固定文字列とは前方一致させられない。`get_char()` 自身が
 * 末尾に `"[%s] "`（＝`"[yrn] "`）を足すので、その固定の尾で判じる。
 */
void fill_shapechange_revert_choices(GameFrame &frame)
{
    if (!frame.prompt.text_utf8.empty() || !frame.menu_choices.empty()
        || frame.numeric.active || frame.text_input_active
        || (frame.menu_core_cursor.line_index >= 0)) {
        return; // 他の合成が既に何か積んでいるなら譲る。
    }
    // `an_term_row()`（an_null_term.c）は行末の空白を刈るので、末尾に空白を
    // 残さない（見直しの指摘。以前は "[yrn] " の末尾空白のせいで一致しなかった）。
    static const char *const kSuffix = "or (r)eturn to normal? [yrn]";
    /*
     * AH-07 の検査専用フック。`ANGBAND_TEST_FORCE_SHAPECHANGE_ROW0` が
     * 立っているときだけ、実際の Term 行 0 の代わりにこの文字列で判定する
     * （変身画面は狙って再現しづらいので、`an_map_shim.c` の
     * `ANGBAND_TEST_FORCE_IMAGE` と同じ考え方の検査用フックを設けた）。
     * 通常運用では環境変数が無いので何もしない。
     */
    static int test_row0_checked = 0;
    static const char *test_row0_override = nullptr;
    if (!test_row0_checked) {
        test_row0_override = std::getenv("ANGBAND_TEST_FORCE_SHAPECHANGE_ROW0");
        test_row0_checked = 1;
    }
    std::string text;
    if (test_row0_override != nullptr) {
        text = test_row0_override;
    } else {
        char row0[256];
        const int row0_len = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
        if (row0_len <= 0) {
            return;
        }
        text.assign(row0, static_cast<std::size_t>(row0_len));
    }
    if (!contains_marker(text, kSuffix)) {
        return;
    }

    /*
     * `frame.menu_choices`（`fill_birth_*_choices()` が使う仕組み）ではなく
     * `frame.prompt.choices` を使う——`native_modern.h` の `capture()` は
     * `frame.menu_open` が偽だと `menu_choices` を一切見ない（`window.stage`
     * を決める分岐に入る手前の `if (frame.menu_open || ... || modern_prompt || ...)`
     * で弾かれる）。この画面は `menu_open` も `numeric.active` も立たないので、
     * `modern_prompt`（`!frame.prompt.choices.empty()`）でその関門を通す。
     * `stage="confirm"` は `prompt_.choices` を**そのまま数だけ並べる**作りなので
     * （`hd2d/ui/modern_store_ui.cpp` の `prompt_.choices.size()` を見る各所）、
     * 2 択（y/n）専用ではなく 3 択もそのまま乗る。
     */
    frame.prompt.text_utf8 = text;
    frame.prompt.line_index = frame.menu_open ? find_mirror_line_index(frame, 0) : -1;
    // 既定は「変身を続ける」——`get_char()` の fallback（ESC・無効なキー）と同じ
    // 既定へそろえる（`fill_birth_final_confirm_choices()` と同じ「誤操作しにくい
    // 既定」の考え方だが、ここは「原作の既定＝安全側」がそのまま「変えない」）。
    PromptChoice stay;
    stay.label_utf8 = ui_text("Stay changed");
    stay.key = 'n';
    PromptChoice change_and_act;
    change_and_act.label_utf8 = ui_text("Change back and act");
    change_and_act.key = 'y';
    PromptChoice revert_only;
    revert_only.label_utf8 = ui_text("Return to normal only");
    revert_only.key = 'r';
    frame.prompt.choices.push_back(std::move(stay));
    frame.prompt.choices.push_back(std::move(change_and_act));
    frame.prompt.choices.push_back(std::move(revert_only));
}

/*!
 * @brief AH-15: アイテムの山で出る「You see:」
 * （実際の文言は 3 通り。下の `kPrompts`）専用の選択肢の合成。
 * @details `ui-display.c` の `see_floor_items()`（`floor_num > 1` の枝）は
 * `screen_save()` の後、行 0 に見出しを `prt()` して素の `inkey_ex()` で 1 つ読み、
 * **読んだキーをそのまま次のコマンドとして `Term_event_push()` で押し戻す**
 * ——原作は「Enter を含むどんなキーでも続ける」という作り。ところが新 UI の
 * Enter は**決定の専任**で、確認する相手が無いとき（`nothing_to_confirm()`。
 * `hd2d/app/input_controller.cpp`。2026-09-25 に `confirm_opens_command_menu()`
 * から改名——以前はここが真だと Enter がコマンドメニューを開いた）は**何も
 * しない**——コアへ送られない。この一覧を「続ける」つもりで Enter を押しても、
 * 押し戻されたその Enter がコアへ届かず、一覧が進まなくなってしまう。
 *
 * 見出しの文言は `p`（既定 `"see"`）の値で 3 通りに変わる——**普通に持ち物へ
 * 入る空きがあり盲目でもないとき**（いちばんよく踏む場合。既定のまま）は
 * 「You see: 」、持ち物に入り切らないときは「You have no room for the
 * following objects: 」、盲目のときは「You feel something on the floor: 」。
 * 最初の実装では後の 2 つしか見ておらず、いちばん普通に踏む「You see: 」を
 * 見落としていた（`CMD_HOLD`＝足踏み・立ち止まりで 2 個以上のアイテムの上に
 * いれば毎回これ）。
 *
 * 他の合成（`fill_birth_*_choices()`）と同じ手口——Term 行 0 の文言で画面を見分け、
 * 「続ける」を明示の選択肢にして押し戻すキーを ESC（27）に固定する。ESC は
 * 何もしない取消として読まれるだけなので、「続ける」の意味はそのまま保てる
 * （原作のように一覧の内容へ直接コマンドを打ち込む裏技は失うが、時間内で
 * 安全に直せる範囲はここまで）。
 */
void fill_floor_list_continue_choice(GameFrame &frame)
{
    if (!frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)
        || frame.numeric.active || !frame.prompt.text_utf8.empty()) {
        return; // 他の合成が既に何か積んでいるなら譲る。
    }
    // 末尾の空白は `an_term_row()` が詰めるので（設計どおり。`an_null_term.c`
    // の「空行は詰める」と同じ規約）、原作の書式 `"You %s: "` の末尾の空白を
    // 落とした形で持つ。
    static const char *const kPrompts[] = {
        "You see:",
        "You have no room for the following objects:",
        "You feel something on the floor:",
    };
    char row0[256];
    const int row0_len = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
    if (row0_len <= 0) {
        return;
    }
    const std::string text(row0, static_cast<std::size_t>(row0_len));
    bool found = false;
    for (const char *prompt : kPrompts) {
        if (starts_with_marker(text, prompt)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }

    MenuChoice choice;
    choice.line_index = -1; // 一覧の中身の行は Term の実際の並びに任せる（見出しだけ差し替える）。
    choice.key = 27;
    choice.label_utf8 = ui_text("Continue");
    choice.selected = true;
    choice.available = true;
    frame.menu_choices.push_back(std::move(choice));
}

/*!
 * @brief AH-19（前半）: メッセージ履歴（`ui-knowledge.c` の `do_cmd_messages()`。
 * `^P`）を、パッド・タッチから 1 ページずつ繰れるようにする。
 * @details この画面は `struct menu` を使わない素の `inkey_ex()` ループで、
 * ページ送りは原作の中でも `'p'`/`' '`（20 件古い方へ）・`'n'`（20 件新しい方へ）
 * という**通常の文字キー**で受け付けている（矢印・`ARROW_UP` 等は 1 件ずつの
 * 移動）——キーボードなら押せるが、物理キーボードの無いパッド・タッチには
 * 「1 件ずつ」の上下操作しか渡っていない（`native_modern.h` の `navigate` は
 * `stage=="cursor"`/`"choices"` でしか効かず、この画面は選択肢もカーソルも
 * 無いので `stage="detail"` に落ちる——上下スワイプはあっても「20 件飛ばす」
 * ページ送りの手が無かった）。
 *
 * `frame.menu_page_prev_key`/`menu_page_next_key`（K-?。ヘルプの左右ページ送りで
 * 幻想蛮怒が使っているのと同じ仕組み）にこの画面の 'p'/'n' を積むだけでよい
 * ——`native_modern.h` の `capture()` は `window.prev_key`/`next_key` を
 * `stage` に関わらず毎回この 2 つの値で埋めており（上の AH-12 の直しのすぐ下）、
 * `document_page` イベント（パッドの L/R・画面の「前へ／次へ」ボタン）は
 * それをそのまま押し戻す。見出し（`prt(format("Message recall (%d-%d of %d), offset
 * %d", ...), 0, 0)`）で画面を見分ける。
 */
void fill_message_recall_paging(GameFrame &frame)
{
    char row0[256];
    const int row0_len = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
    if (row0_len <= 0) {
        return;
    }
    const std::string text(row0, static_cast<std::size_t>(row0_len));
    if (!starts_with_marker(text, "Message recall (")) {
        return;
    }
    frame.menu_page_prev_key = 'p'; // 20 件古い方へ（原作: 'p' / ' ' / KC_PGUP）。
    frame.menu_page_next_key = 'n'; // 20 件新しい方へ（原作: 'n' / KC_PGDOWN）。
}

/*!
 * @brief AH-55: 行動の履歴（`history_display()`。死亡メニューの `h`・知識の「character history」）と
 * 栄誉の殿堂（`show_scores()`。死亡メニューの `v`・知識の「hall of fame」）のページ送りの手を出す。
 * @details `fill_message_recall_paging()`（AH-19）と同じ手——`menu_page_prev_key`/`menu_page_next_key` に
 * 原作のキーを積むと、画面の「前へ／次へ」・パッドの L/R（`document_page`）がそのキーを送る。
 * 履歴は脚注「p/PgUp で前のページ・n/PgDn で次のページ」（`p` と `n`）。殿堂の原作は「上矢印で前のページ・
 * ほかのどのキーでも次のページ」なので、前へは上矢印（`ARROW_UP` = 0x83）・次へは空白を送る。
 * 脚注は Term の幅で末尾が切れることがあるので、切れない前半の字で見分ける。
 */
void fill_history_and_score_paging(GameFrame &frame)
{
    if (frame.menu_page_prev_key || frame.menu_page_next_key) {
        return;
    }
    for (const auto &line : frame.menu_term_lines) {
        const std::string &text = line.text_utf8;
        if ((text.find("p/PgUp") != std::string::npos) && (text.find("n/PgDn") != std::string::npos)) {
            frame.menu_page_prev_key = 'p'; // 前のページ（原作: 'p' / KC_PGUP）。
            frame.menu_page_next_key = 'n'; // 次のページ（原作: 'n' / ' ' / KC_PGDOWN）。
            return;
        }
        if (contains_marker(text, "[Press ESC to exit, up for prior page, any other key for next page.]")) {
            frame.menu_page_prev_key = 0x83; // 上矢印（`ARROW_UP`。ui-event.h）。
            frame.menu_page_next_key = ' '; // ほかのどのキーでも次のページ。
            return;
        }
    }
}

/*!
 * @brief 知識の 2 列画面（`ui-knowledge.c` の `display_knowledge()`）を、ほかのコアの知識のブラウザと同じ形で渡す。
 * @details 原作はこの画面を 2 つの `struct menu`（左＝分類・右＝名前）で描き、**描いた直後に記録した行**
 * （A1/A2）は**いま動かしている側だけ**になる。しかも写しは箱（2 つの箱の和）へ切り取られ、見出し
 * （`Knowledge - …`・`Group`/`Name`・区切りの線・最下行の `<dir>, ESC`）が落ちる。結果、新しい UI は
 * 分類の列の行しか選べず、右の名前の列は行にならず、下へ送っても原作がスクロールした先へ届かなかった。
 *
 * この画面の見た目は変愚・Sil-Q・Frox などの知識のブラウザ（`cmd4.c` の `browser_cursor`）とそっくりなので、
 * 共通の窓（`modern_browser_content()`）がそのまま読める形にする——Term の全行（見出しから最下行まで）を写し、
 * 選択肢の記録は捨てて、**カーソルの行・列**（動かしている側のメニューのカーソル）を Term のカーソルとして渡す。
 * 十字キー・左右（`4`/`6` が分類と名前の切り替え）は原作のメニューがそのまま処理する。
 * ページ送りは原作のメニューが空白（次のページ。端で先頭へ戻る）だけを持つので、前は申告しない
 * （原作のメニューは端で回り込むので、共通の窓の「端で原作へ上下を送る」仕組みがそのまま働く）。
 * 見た目を変える操作（`v` の記号と色の変更の選び方）は列の形が違うので、今までの写しのまま通す。
 */
void fill_knowledge_browser(GameFrame &frame)
{
    if (frame.pre_game_menu || frame.text_input_active || frame.numeric.active || !frame.prompt.choices.empty()) {
        return;
    }
    int cols = 0;
    int rows = 0;
    (void)an_term_size(&cols, &rows);
    if (rows < 10) {
        return;
    }
    char text[1024];
    int n = an_term_row(2, text, static_cast<int>(sizeof(text)));
    if (n <= 0) {
        return;
    }
    const std::string title(text, static_cast<std::size_t>(n));
    if ((title.rfind("Knowledge - ", 0) != 0) && (title.rfind("知識 - ", 0) != 0)) {
        return;
    }
    n = an_term_row(rows - 1, text, static_cast<int>(sizeof(text)));
    if (n <= 0) {
        return;
    }
    const std::string footer(text, static_cast<std::size_t>(n));
    if ((footer.find("<dir>") == std::string::npos) || (footer.find("ESC") == std::string::npos)
        || (footer.find("ENTER") != std::string::npos)) {
        return; // 記号と色の選び（タイル・文字の選択）は今までの写しのまま。
    }

    // 動かしている側のメニューのカーソル（記録は動かしている側だけ）。
    int cursor_row = -1;
    int cursor_col = 0;
    const int count = an_menu_choice_count();
    for (int i = 0; i < count; ++i) {
        an_menu_choice_row row{};
        if (an_menu_choice_at(i, &row) && (row.cursor != 0)) {
            cursor_row = row.row;
            cursor_col = row.col;
            break;
        }
    }

    std::vector<TermMirrorLine> lines;
    unsigned char colors[256];
    for (int y = 0; y < rows; ++y) {
        n = an_term_row(y, text, static_cast<int>(sizeof(text)));
        if (n <= 0) {
            continue;
        }
        if (an_term_colors(y, colors, static_cast<int>(sizeof(colors))) < 0) {
            colors[0] = 0;
        }
        TermMirrorLine line;
        line.text_utf8.assign(text, static_cast<std::size_t>(n));
        line.color = colors[0];
        line.source_row = y;
        lines.push_back(std::move(line));
    }
    int cursor_line = -1;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].source_row == cursor_row) {
            cursor_line = static_cast<int>(i);
        }
    }
    if (cursor_line < 0) {
        // 動かしている側が空（名前の列に項目が無い分類）。分類の列の先頭を指す。
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].source_row >= 6) {
                cursor_line = static_cast<int>(i);
                cursor_col = 0;
                break;
            }
        }
    }
    if (cursor_line < 0) {
        return;
    }
    frame.menu_term_lines = std::move(lines);
    frame.menu_term_curs_row = cursor_line;
    frame.menu_term_curs_col = cursor_col;
    frame.menu_choices.clear();
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();
    frame.menu_page_prev_key = 0;
    frame.menu_page_next_key = ' '; // 原作のメニューの「次のページ」（端で先頭へ戻る）。前のページのキーは無い。
}

/*!
 * @brief AH-50（前半）: ヘルプ・メッセージ履歴の中の 1 行入力（`/` 検索・`#` 行へ飛ぶ・`%` ファイルへ飛ぶ・
 * `&` 強調・メッセージ履歴の `=`）を文字入力の窓にする。
 * @details 原作は `askfor_aux()` を直接呼ぶので `get_string_hook` を通らず、`text_input_active` が立たなかった
 * （窓の段が `detail` のまま。「Find」「Goto Line」ボタンを押しても文字入力の窓にならず、パッド・タッチ・
 * Android のソフトキーボードで入力できなかった）。問いは Term の最終行（`hgt - 1`）に
 * `Find: `・`Goto Line: `・`Goto File: `・`Show: ` の形で出て、Term のカーソルもその行にあるので、
 * カーソルのある行がこの形なら AH-16（オプションの数値欄）と同じ手で立てる。窓の入力欄は
 * カーソル行（`menu_term_curs_row`）の字を読む。問いの字は日本語にして `prompt.text_utf8` へ載せる
 * （原作の問いは訳されない。`ui_text()` が英語のままなら原文）。
 */
void fill_help_text_input(GameFrame &frame)
{
    if (frame.text_input_active || (frame.menu_term_curs_row < 0)
        || (frame.menu_term_curs_row >= static_cast<int>(frame.menu_term_lines.size()))) {
        return;
    }
    static const char *const kMarkers[] = { "Find:", "Goto Line:", "Goto File:", "Show:" };
    const std::string line = frame.menu_term_lines[static_cast<std::size_t>(frame.menu_term_curs_row)].text_utf8;
    for (const char *marker : kMarkers) {
        const std::string translated = ui_text(marker);
        const std::size_t head = (line.rfind(marker, 0) == 0) ? std::strlen(marker)
            : ((line.rfind(translated, 0) == 0) ? translated.size() : 0);
        if (head > 0) {
            frame.text_input_active = true;
            frame.menu_choices.clear();
            frame.menu_core_cursor = {};
            frame.menu_core_cursors.clear();
            std::string label = translated;
            while (!label.empty() && (label.back() == ' ')) {
                label.pop_back();
            }
            const std::string rest = line.substr(head);
            const auto first_char = rest.find_first_not_of(' ');
            frame.prompt.text_utf8 = (first_char == std::string::npos) ? label : (label + " " + rest.substr(first_char));
            frame.prompt.line_index = -1;
            return;
        }
    }
}

/*!
 * @brief AH-50（後半）: ヘルプの目次（`index.txt`。`.. menu:: [a] ...` のあるファイル）の
 * `(a) Available commands` の行を選択肢として出す。
 * @details 原作は英字 1 キーで下のファイルへ入る（`show_file()` の `menu`）が、選択肢もカーソルも
 * 無いのでパッド・タッチでは入れなかった。目次の行は `    (a) 題名` の形で、最終行が
 * 「[Press a Letter, or ESC to exit.]」（日本語では「[英字で選ぶ・ESC で戻る]」）。この形だけを対象にする
 * （普通のヘルプ本文は最終行が違う）。`selected` は立てない（AH-44。カーソルを立てると上下が原作へ
 * '2'/'8' で送られ、原作は 1 行スクロールするだけでカーソルが動かない）。
 */
void fill_help_index_choices(GameFrame &frame)
{
    if (frame.text_input_active || !is_help_index_frame(frame)) {
        return;
    }
    frame.menu_choices.clear();
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        const auto open = text.find_first_not_of(' ');
        if ((open == std::string::npos) || (text.size() < open + 5) || (text[open] != '(')
            || (text[open + 2] != ')') || (text[open + 1] < 'a') || (text[open + 1] > 'z') || (text[open + 3] != ' ')) {
            continue;
        }
        MenuChoice choice;
        choice.line_index = static_cast<int>(i);
        choice.span_begin = static_cast<int>(open);
        choice.span_len = static_cast<int>(text.size() - open);
        choice.key_begin = static_cast<int>(open) + 1;
        choice.key_len = 1;
        choice.key = text[open + 1];
        choice.label_utf8 = text.substr(open);
        choice.available = true;
        frame.menu_choices.push_back(std::move(choice));
    }
}

/*
 * AH-44: アダプタが合成する複数の選択肢
 * （続ける／受け入れる／振り直す ほか）には `MenuChoice::selected` を**立てない**。
 * `selected` は「原作がカーソルを持っていて、いまここにある」という申告で、
 * `NativeModernMenu::capture()`（`native_modern.h`）はそれを `window.cursor`
 * にし、画面（`hd2d/ui/modern_store_ui.cpp` の `advance()`）は上下の操作を
 * `navigate`（'2'/'8'）として原作へ送り、原作のカーソルが目当ての行へ来るまで
 * 送り続ける。これらの画面は素の `inkey_ex()` 読みで '2'/'8' を無視するので
 * カーソルは永遠に動かず、`navigate` が数百回送られ続けて以後の決定も
 * 振り直しも効かなかった（気づいたこと 2026-09-28「標準のロールができない」）。
 * `selected` が無ければ画面が自分のカーソルで選び、決定で行の `key` を送る。
 * 既定は先頭の行（ここでは必ず「続ける／受け入れる」を先頭に置いている）。
 */

/*!
 * @brief AH-19（後半）: 誕生の能力値ロール画面（`ui-birth.c` の `roller_command()`。
 * 「振り直す」）専用の選択肢の合成。
 * @details `fill_birth_quickstart_choices()` と同じ理由で止まる——`struct menu` も
 * `get_check()` も通らない素の `inkey_ex()` 読みの画面なので、A1/A2 のフックにも
 * `an_ui_shim.c` の `get_check` フックにも掛からない。プロンプトは
 * `prt(prompt, Term->hgt - 1, Term->wid / 2 - promptlen / 2)` で**Term 最終行の
 * 中央**に出る（`fill_birth_quickstart_choices()` と同じ行だが、行頭ではなく
 * 中央寄せなので部分一致で判じる）。原作の判定は Enter＝そのまま使う・
 * `' '`/`'r'`＝振り直す・（2 回目以降のみ）`'p'`＝前の結果に戻す。
 * 既定選択は「そのまま使う」——他の birth 合成と同じ考え方。
 */
void fill_birth_reroll_choices(GameFrame &frame)
{
    if (!frame.pre_game_menu || !frame.menu_choices.empty() || (frame.menu_core_cursor.line_index >= 0)) {
        return;
    }
    int cols = 0;
    int rows = 0;
    if (an_term_size(&cols, &rows) != 0 || (rows <= 0)) {
        return;
    }
    char last_row[256];
    const int last_row_len = an_term_row(rows - 1, last_row, static_cast<int>(sizeof(last_row)));
    if (last_row_len <= 0) {
        return;
    }
    const std::string text(last_row, static_cast<std::size_t>(last_row_len));
    if (!contains_marker(text, "'r' to reroll")) {
        return;
    }
    const bool has_prev = contains_marker(text, "'p' for previous roll");

    auto add_choice = [&frame](int key, const char *label, bool selected) {
        MenuChoice choice;
        choice.line_index = -1; // 中央寄せの脚注 1 行なので特定の Term 行を指さない。
        choice.key = key;
        choice.label_utf8 = ui_text(label);
        choice.selected = selected;
        choice.available = true;
        frame.menu_choices.push_back(std::move(choice));
    };
    add_choice(13, "Accept", false); // AH-44
    add_choice('r', "Reroll", false);
    if (has_prev) {
        add_choice('p', "Previous roll", false);
    }
}

//! AH-51: `Repeat: ` を見たあと `Command: ` を出すまで真（ゲームスレッドだけが読み書きする）。
bool g_repeat_command_wait = false;

/*!
 * @brief AH-10: `menu_open` が偽のまま Term 行 0 に出る「素の 1 キー待ち」の問いを
 * 見える形にする（`get_com()`/`get_com_ex()`/`get_char()`/生の `inkey()` 読み。
 * どれも `get_check_hook` 等のフックを通らないので、`an_ui_shim.c` には掛からない）。
 * @details 対象は課題管理表 AH-10 の 6 つ:
 * - `0`（回数。`textui_get_count()`。`ui-input.c`）— 行 0 が `"Repeat: "` で
 *   始まる。数字だけを編集するので `frame.text_input_active` に乗せる
 *   （Enter は原作側で「確定して次のコマンド待ちへ」に化けるので、
 *   共用の `text_confirm`（生の 0x0D）とかみ合う）。
 * - `L`（地図区画の移動方向。`do_cmd_locate()`。`ui-knowledge.c`）— 行 0 が
 *   `"Map sector"` で始まり `"Direction?"` で終わる。**当初はここも
 *   `text_input_active` に乗せていたが、AH-33 の実機確認（2026-09-25）で
 *   誤りと分かった**——`do_cmd_locate()` は Enter で確定する文字列を
 *   組み立てているのではなく、方向キー 1 つで即座に区画を動かし同じ問いへ
 *   戻るループ（方向でなければベルが鳴るだけ）なので、`text_input_active`
 *   を立てると新 UI が「文字を入力してください。」という**的外れな汎用
 *   テキスト入力ダイアログ**を開いてしまい、方向キーでの移動ができなく
 *   なる（AH-33 のカメラ追従自体は `an_locate_active()`/`camera_detached`
 *   が既に正しく動いている——問題は新 UI 側の壁）。**この問いは選択肢も
 *   専用の面も持たせず、何もしないで抜ける**——`frame.text_input_active`/
 *   `frame.prompt` のどちらも立たなければ `native_modern.h` の
 *   `capture()` はモーダルの窓を一切組まない（`menu_open` も `numeric.active`
 *   も `modern_prompt` も `message_more_pending()` も全部偽のまま）ので、
 *   通常のダンジョン画面がそのまま出続け、方向キーは素の打鍵として
 *   コアへそのまま届く——`do_cmd_locate()` からすれば「新 UI が割り込む
 *   前と同じ」動きになる。詳細は `tools/angband/ah33_locate_camera_smoke.py`
 *   の追補（AH-33 follow-up）を参照。
 * - `/`（記号でモンスターを引く。`do_cmd_query_symbol()`。`ui-knowledge.c`）— 任意の
 *   1 文字を受けるので `presentation::expose_ascii_symbol_prompt()`
 *   （手本は Sil-Q/幻想蛮怒/FroxComposband が使っている同じ共用の器）で
 *   `!`〜`~` を選択肢として合成する。
 * - `)`（画面の保存形式。`do_cmd_save_screen()`。`ui-command.c`）— `get_char()` の
 *   2 択（h/f）。行末が `"[hf] "` になる。
 * - `Q` の `@`（生年退場の最終確認。`textui_cmd_retire()`。`ui-command.c`）と
 *   追放の記号（`effect-handler-general.c`）は既存の共用ロジックへ回す
 *   （`Q` は `prompt_semantics.h` の `analyze_command_prompt()` を Angband の
 *   文言にも掛かるよう広げた。追放は `is_monster_symbol_prompt()` が
 *   "monster race (by symbol)" にそのまま一致する）。
 * @note ここに来る時点で `frame.menu_open` は偽（呼び出し元の `else` 節）——
 * `expose_ascii_symbol_prompt()` は `frame.menu_open` を真に書き換えるが、これは
 * **この関数の後**の `an_declared_menu_actions()`/`g_native_menu.capture()` に
 * 「メニュー相当」として見せるためで、今フレームの `fill_game_frame()` は
 * 既にこの前で呼び終えている（地図・状態列は普段どおり送られる。害は無い——
 * `an_menu_cmd_keys()`/`an_menu_switch_keys()` は実際の `struct menu` が
 * 開いていなければ NULL を返すだけ）。
 */
void fill_raw_row0_free_prompts(GameFrame &frame)
{
    presentation::set_repeat_count_input_active(false); // 立てるのは下の `fill_repeat_count_input()` だけ（AH-51）。
    char row0[256];
    const int n = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
    if (n <= 0) {
        g_repeat_command_wait = false; // AH-51: 行 0 が消えた＝回数の入力は終わった。
        return;
    }
    std::string text(row0, static_cast<std::size_t>(n));
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    if (text.empty()) {
        g_repeat_command_wait = false;
        return;
    }

    if (presentation::is_monster_symbol_prompt(text)) {
        presentation::expose_ascii_symbol_prompt(frame, text);
        return;
    }
    if (starts_with_marker(text, "Enter character to be identified")) {
        presentation::expose_ascii_symbol_prompt(frame, text);
        return;
    }

    const auto command_prompt = presentation::analyze_command_prompt(text);
    if (command_prompt.kind == presentation::CommandPromptKind::VerifyAt) {
        frame.prompt.text_utf8 = text;
        frame.prompt.choices.push_back({ ui_text("Confirm (@)"), '@', 0, 0 });
        frame.prompt.choices.push_back({ ui_text("Cancel (Esc)"), 27, 0, 0 });
        return;
    }

    if (starts_with_marker(text, "Dump as (H)TML or (F)orum text?")) {
        frame.prompt.text_utf8 = text;
        frame.prompt.choices.push_back({ ui_text("HTML"), 'h', 0, 0 });
        frame.prompt.choices.push_back({ ui_text("Forum text"), 'f', 0, 0 });
        return;
    }

    /*
     * AH-51: `0`（反復回数。`textui_get_count()`）。数字だけを 1 キーずつ読む入力なので、他のコアの繰り返し
     * 回数と同じ数値の窓（0〜9999）で出す。決定は共用の `set_number`（退格×5＋数字。
     * `input_event_adapter.cpp`）。原作は Enter のあとに `Command: `（どのコマンドを繰り返すか）を 1 キーで
     * 聞くので、その間は「繰り返すコマンドを選んでください」を出す（選択肢は出さない。パッドの
     * コマンドメニューの決定はそのコマンドのキーとして原作の `Command: ` に届く）。
     * `\`（`Command: `）と `^`（`Control: `）は何も出さないので、`Repeat: ` を見たあとの
     * `Command: ` だけを対象にする。
     */
    if (an_awaiting_command() != 0) {
        /*
         * 原作は ESC で回数（や `Command: `）を取り消すと行 0 を消さずに `continue` するので、次のコマンド待ちに
         * なっても `Repeat: 0` / `Command: ` が Term に残る。コマンド待ち（`inkey_flag` が真）なら古い字。
         */
        if (starts_with_marker(text, "Repeat: ") || is_command_prompt(text)) {
            g_repeat_command_wait = false;
            return;
        }
    }
    if (presentation::fill_repeat_count_input(text, frame.numeric)) {
        frame.numeric.prompt_utf8 = ui_text("Repeat count (0-9999):");
        g_repeat_command_wait = true;
        return;
    }
    if (g_repeat_command_wait && is_command_prompt(text)) {
        frame.prompt.text_utf8 = ui_text("Choose the command to repeat");
        frame.prompt.line_index = -1;
        return;
    }
    g_repeat_command_wait = false;

    // AH-33 follow-up（2026-09-25）: 上のコメントのとおり、`L` の
    // 「Map sector ... Direction?」はテキスト入力ではないので、ここでは
    // 何も立てずに抜ける（新 UI にモーダルを開かせない）。
    if (starts_with_marker(text, "Map sector") && contains_marker(text, "Direction?")) {
        return;
    }

    /*
     * AH-34: モンスターの記憶を一覧で見る
     * （`/` → `^A`（全部）→ `y`/`k`（レベル順/撃破数順）で入る
     * `do_cmd_query_symbol()` の履歴ループ。`ui-knowledge.c`）は、行 0 に
     * モンスターの名前＋`" [(r)ecall, ESC]"`（`lore_title()` の直後に
     * `textblock_append()` で足す）を `textui_textblock_place()`（`_show` では
     * ない——`screen_save()` を挟まないので `menu_open` は偽のまま）で描く。
     * この行はここまでのどの形（`is_monster_symbol_prompt`・`VerifyAt`・
     * `[hf] `・`Repeat: `・`Map sector`）にも一致しないので、フレームが
     * 前回と全く同じ内容のまま `host_present()` の重複除け（digest 比較）に
     * 引っかかり、実機で「数秒フレームが来ない」——選択肢が無いだけで
     * 画面自体は正しく描けているので、`fill_menu_mirror()`/`menu_open` 側の
     * 不具合ではなく、この関数がこの行の形を知らないだけ。
     * `'r'`（記憶の再表示。押すたびにトグル）と `ESC`（一覧を閉じる）を
     * 選択肢として合成する——`'-'`（前）/その他（次）はキーボードでの
     * 操作を妨げないよう選択肢に載せない（ここに来る目的は「選べることを
     * 画面へ伝える」ことで、全キーを尽くすことではない）。
     */
    static const char *const kRecallSuffix = " [(r)ecall, ESC]";
    const std::size_t recall_suffix_len = std::strlen(kRecallSuffix);
    (void)recall_suffix_len;
    if (ends_with_marker(text, kRecallSuffix)) {
        frame.prompt.text_utf8 = text;
        const auto paren = text.rfind("(r)ecall");
        PromptChoice recall;
        recall.label_utf8 = ui_text("Recall (r)");
        recall.key = 'r';
        if (paren != std::string::npos) {
            recall.begin = static_cast<int>(paren) + 1; // "(r)ecall" の 'r'。
            recall.len = 1;
        }
        PromptChoice close;
        close.label_utf8 = ui_text("Close (Esc)");
        close.key = 27;
        frame.prompt.choices.push_back(std::move(recall));
        frame.prompt.choices.push_back(std::move(close));
        return;
    }
}

/*!
 * @brief AH-48: 休憩の問いを選択肢（または数値の窓）で出す。
 * @details 原作の `textui_cmd_rest()` は `&`・`*`・`!`・`1`〜`9999` を受ける（Enter で確定）。
 * 「ターン数を指定」を選ぶまでは 3 つの休憩の種類とターン数の指定を出す。`text_input_active` は立てない
 * （立てると窓が文字入力になり、行 0 が画面へ渡らないので問いも入力中の字も出ない）。
 * 数値の窓の決定は共用の `set_number`（退格＋数字＋Enter。退格は下の受信側が 1 個に丸める）で送る。
 * `PromptChoice` に `selected` は無い（合成した選択肢でカーソルを立てない。AH-44）。
 */
void fill_rest_prompt(GameFrame &frame, const an_text_state &)
{
    g_rest_prompt.store(true);
    if (g_rest_count_mode.load()) {
        frame.numeric.active = true;
        frame.numeric.prompt_utf8 = ui_text("Rest turns (1-9999):");
        frame.numeric.min = 1;
        frame.numeric.max = 9999;
        frame.numeric.value = 10;
        frame.numeric.digits = 4;
        frame.numeric.text_len = 0;
        frame.numeric.line_index = -1;
        return;
    }
    frame.prompt.text_utf8 = ui_text("Rest for how long?");
    frame.prompt.line_index = -1;
    frame.prompt.choices.push_back({ ui_text("Rest as needed (&)"), '&', 0, 0 });
    frame.prompt.choices.push_back({ ui_text("Rest until HP and SP are full (*)"), '*', 0, 0 });
    frame.prompt.choices.push_back({ ui_text("Rest until HP or SP is full (!)"), '!', 0, 0 });
    frame.prompt.choices.push_back({ ui_text("Set the number of turns"), kRestCountKey, 0, 0 });
    // 取り消しの選択肢は出さない（Esc で取り消せる。出すと共用の確認の窓が既定を取り消しにする）。
}

/*!
 * @brief AH-49: 文字入力（`get_string()`。銘・メモ・設定の 1 行・画面の保存のファイル名）の 1 行を画面へ渡す。
 * @details 原作は問いと既定値と入力中の字を Term の行 0 に描く（`askfor_aux()`）が、行 0 は画面側へ
 * 渡らない（送るのは状態列と最下段だけ）ので、文字入力の窓にラベルも既定値も打った字も出なかった。
 * 行 0 をそのまま `prompt.text_utf8`（選択肢なし）に載せる。窓の入力欄は `modern_prompt_text()` でこれを読む。
 * `text_state.prompt`（訳した問い）で始まらない行は入力中の行ではないので使わない。
 */
void fill_text_input_line(GameFrame &frame, const an_text_state &text_state)
{
    char row0[256];
    const int n = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
    if (n <= 0) {
        return;
    }
    const std::string text(row0, static_cast<std::size_t>(n));
    std::string prompt = text_state.prompt;
    while (!prompt.empty() && (prompt.back() == ' ')) {
        prompt.pop_back(); // `an_term_row()` は行末の空白を詰める。
    }
    if (!prompt.empty() && (text.rfind(prompt, 0) != 0)) {
        return;
    }
    frame.prompt.text_utf8 = text;
    frame.prompt.line_index = -1;
}

/*!
 * @brief 確認・数量・文字入力の状態を埋める（設計 §6.3）。
 * @details `frame.menu_open` の真偽にかかわらず呼ぶ——数量・確認は通常のダンジョン
 * 画面（`menu_open` が偽）でも出る（`GameFrame::prompt`/`numeric` のヘッダ註記
 * 「元の画面にプロンプト行が出ている店・建物では line_index、出ていないダンジョンでは
 * -1」）。Term 行 0 に出ているときだけ `line_index` を引く。
 */
void fill_prompts(GameFrame &frame)
{
    an_check_state check{};
    if (an_get_check_state(&check)) {
        frame.prompt.text_utf8 = check.prompt;
        frame.prompt.line_index = frame.menu_open ? find_mirror_line_index(frame, 0) : -1;
        const std::string text = check.prompt;
        const auto bracket = text.find("[y/n]");
        PromptChoice yes;
        yes.label_utf8 = ui_text("Yes");
        yes.key = 'y';
        PromptChoice no;
        no.label_utf8 = ui_text("No");
        no.key = 'n';
        if (bracket != std::string::npos) {
            yes.begin = static_cast<int>(bracket) + 1;
            yes.len = 1;
            no.begin = static_cast<int>(bracket) + 3;
            no.len = 1;
        }
        frame.prompt.choices.push_back(std::move(yes));
        frame.prompt.choices.push_back(std::move(no));
    }

    an_quantity_state quantity{};
    if (an_get_quantity_state(&quantity)) {
        frame.numeric.active = true;
        frame.numeric.prompt_utf8 = quantity.prompt;
        frame.numeric.min = quantity.min;
        frame.numeric.max = quantity.max;
        frame.numeric.line_index = frame.menu_open ? find_mirror_line_index(frame, 0) : -1;
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%d", (quantity.max > 0) ? quantity.max : 1);
        frame.numeric.digits = static_cast<int>(std::strlen(digits));

        /*
         * 見直しの指摘（遊ぶ人に直接効く不具合。最優先で直す）: 以前はここで
         * `value`/`text_len` を埋めておらず、`GameFrame::NumericInput::value`
         * は既定初期化のまま 0 だった。画面側はこれを初期表示に使うので、
         * 店で決定を押しただけで「0 個」＝購入不成立になっていた。
         *
         * 原作の `askfor_aux()`（`ui-input.c`）は `Term_locate()` で
         * 「`prt(prompt, 0, 0)` の直後のカーソル位置」を取り、そこから編集
         * バッファ（既定 "1"）を描く——つまり Term 行 0 は
         * 「プロンプト文字列」＋「編集中の数字」がそのまま連結されている。
         * ここでは Term 行 0 を読み、`quantity.prompt`（このフックが
         * `an_orig_get_quantity()` へ渡したのと同じ文字列。見直しの指摘 10 の
         * 直しで NULL のときも組んである）を先頭から取り除いた残りを、数字の
         * 並びとして読む（原作の初期値 "1" もこの経路でそのまま読める。
         * KTRL('B')/KTRL('D') を送る直し・数字入力の直し（見直しの指摘 3）の
         * 効果も、この読み出しを通じて画面側の表示に正しく反映される）。
         */
        char row0[256];
        const int row0_len = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
        if (row0_len > 0) {
            const std::string text(row0, static_cast<std::size_t>(row0_len));
            const std::string &prompt = quantity.prompt;
            if (!prompt.empty() && (text.rfind(prompt, 0) == 0) && (text.size() > prompt.size())) {
                std::string digits_only;
                for (std::size_t i = prompt.size(); i < text.size(); ++i) {
                    const char c = text[i];
                    if ((c >= '0') && (c <= '9')) {
                        digits_only.push_back(c);
                    } else {
                        break; // 数字が途切れたら終わり（原作は数字以外を編集バッファに許さない）。
                    }
                }
                if (!digits_only.empty()) {
                    frame.numeric.value = std::atoi(digits_only.c_str());
                    frame.numeric.text_len = static_cast<int>(digits_only.size());
                    frame.numeric.value_begin = static_cast<int>(prompt.size());
                    frame.numeric.value_len = static_cast<int>(digits_only.size());
                }
            }
        }
    }

    an_text_state text_state{};
    if (an_get_text_state(&text_state) && text_state.rest) {
        fill_rest_prompt(frame, text_state);
    } else if (an_get_text_state(&text_state)) {
        g_rest_prompt.store(false);
        g_rest_count_mode.store(false);
        frame.text_input_active = true;
        fill_text_input_line(frame, text_state);
    } else {
        g_rest_prompt.store(false);
        g_rest_count_mode.store(false);
    }

    // birth の名前入力は `get_string_hook` を通らない（`ui-birth.c` の
    // `get_character_name()` が `askfor_aux_ext()` を直に呼ぶ。実装時に確認した実物の
    // 動き）ので、Term 行 0 の文言そのもので見分ける（`silq/adapter/sq_frame.cpp` の
    // `player_name_screen` と同じ手）。
    if (frame.menu_open) {
        char row0[256];
        if (an_term_row(0, row0, static_cast<int>(sizeof(row0))) > 0) {
            const std::string text = row0;
            // AH-62: 日本語モードでは行 0 が訳文（`E:` の全文を引いた `J:`）に変わる。頭の一部
            // （"Enter a name for your character"）だけでは `an_tr()` が訳を引けず、英語の綴りしか見ないので、
            // 日本語では名前入力と判らずソフトキーボードが出なかった。カタログの `E:` と同じ全文で見分ける。
            if (starts_with_marker(text, "Enter a name for your character (* for a random name):")) {
                frame.text_input_active = true;
                frame.player_name_input_active = true;
            } else if (starts_with_marker(text, "Accept character history? [y/n]")) {
                /*
                 * AH-14: 「履歴を編集」（'n' で開く
                 * `edit_text()`。`ui-birth.c`）も `get_string_hook` を通らない
                 * raw な画面——ただし名前入力と違い、Term 行 0 を一切書き直さない
                 * （`edit_text()` の `clear_from()`/textblock は `HIST_INSTRUCT_ROW+1`
                 * ＝行 19 から下しか触らない）。そのため編集中もずっと行 0 に
                 * 「Accept character history? [y/n]」が残ったままで、
                 * `fill_birth_history_choices()` の判定（行 0 の文言一致）が
                 * 編集中も毎フレーム引っかかり、Accept/Edit/Step back の選択肢が
                 * 出続けていた——既定選択の Accept（'y'）が Enter を奪い、
                 * 「確定」のつもりの Enter が編集バッファへ文字 'y' を挿すだけに
                 * なって、編集を確定する手段が無かった。
                 *
                 * 行の文言だけでは編集中かどうか見分けが付かないので、Term の
                 * カーソル位置で判じる——`edit_text()` は毎回
                 * `Term_gotoxy(1+x, 19+y)` でカーソルを行 19 以降へ置く
                 * （`ui-birth.c`）。生の Accept プロンプトはカーソルを行 0 に
                 * 残す（`prt()` の直後）か隠すので、行 19 以降に**見える**
                 * カーソルがあるかどうかで確実に区別できる（バッファが空でも
                 * カーソル自体は毎回置き直されるので、文字数に左右されない）。
                 * 一致したら「文字を編集している画面」として扱う——
                 * `frame.text_input_active` を見る共用コード（`native_modern.h`）は
                 * これを `frame.menu_choices` より先に見るので、上で積んだ選択肢が
                 * あっても正しく `stage="text"` になり、Enter は
                 * `text_confirm`（生の 0x0D）として送られる。
                 */
                int cx = 0;
                int cy = 0;
                int cvisible = 0;
                if ((an_term_cursor(&cx, &cy, &cvisible) == AN_OK) && cvisible && (cy >= 19)) {
                    frame.text_input_active = true;
                }
            }
        }
        /*
         * AH-16（前半）: オプション画面の数値の欄（`do_cmd_delay()`/`do_cmd_hp_warn()`/
         * `do_cmd_lazymove_delay()`。`ui-options.c`）は `screen_save()` の中で
         * `askfor_aux(tmp, sizeof(tmp), askfor_aux_numbers)` を**直に**呼ぶ——
         * `get_string_hook`（`an_ui_shim.c`）を経由しない生の編集バッファなので、
         * `an_get_text_state()` には掛からず、`struct menu` も無いので
         * `frame.menu_choices` も空のまま。パッド・タッチには開いていることさえ
         * 見えなかった。
         *
         * この 3 画面は原作の作りが共通で、行 20 が `"Command: "` で始まり、
         * 続く行 21 が `"New "` で始まって `:` を含む（範囲つき「New base delay
         * factor (0-255): 」・範囲無し「New movement delay: 」のどちらも当てはまる。
         * `an_term_row()` は行末の空白を詰めるので、末尾が `": "` ではなく `":"` で
         * 終わる形で読める——空白の有無に頼らず `:` の有無だけで見る）。
         * この形は上の 3 画面にしか出ない——`grep -rn '"New .*: '"'"'` で
         * `ui-options.c` を全数確認済み（2026-09-24）。
         *
         * **`frame.menu_choices` が空とは限らない**——この 3 画面はオプション一覧
         * （`struct menu` の `option_actions[]`）から直接呼ばれる素の `prt()`+
         * `askfor_aux()` 画面で、`screen_save()` は増えても元のオプション一覧の
         * `struct menu` 自体は壊れずに残ったまま（`an_menu_begin()` を新しく
         * 呼ばない）なので、`fill_menu_choices()` は**そのオプション一覧の
         * 14 個の選択肢を読み直し続ける**——`discard_stale_menu_if_needed()`
         * （AH-11）は行ごとの中身を突き合わせる方式で、この画面は行 20/21 の
         * 2 行だけを上書きし、オプション一覧の他の行はそのまま実在する内容として
         * 残るので「食い違う行が無い」＝stale と判じない（AH-11 の対象は
         * *全体が別画面に変わった*場合で、*一部だけ上書きされた*ここは別の形）。
         * 行 20/21 の合致を見つけたら、**古い選択肢を読み替えて**
         * `text_input_active` を優先させる（`native_modern.h` は
         * `frame.text_input_active` を `frame.menu_choices` より先に見るので、
         * 選択肢を残したままでも `stage="text"` になるが、`window.body` に
         * 見えない選択肢の行が混じらないよう明示的に消しておく）。
         *
         * 「キーマップの作成」（`ui_keymap_create()`。同じ `ui-options.c`）は
         * ここでは直せない——**Enter が確定ではなく「Enter キーそのものを
         * キーマップへ記録する」対象**という別の作りで（'=' だけが確定）、
         * 既存の `frame.text_input_active`/`stage="text"` は Enter を
         * `text_confirm`（確定）として扱う契約なので、Enter の意味が逆になる
         * この画面へそのまま被せると壊れる。共用の契約を変えずに直す手段が
         * 今回の時間内で見つからなかったので、未着手のまま次回へ送る。
         */
        if (frame.prompt.choices.empty() && !frame.numeric.active && !frame.text_input_active) {
            char row20[256];
            char row21[256];
            const int n20 = an_term_row(20, row20, static_cast<int>(sizeof(row20)));
            const int n21 = an_term_row(21, row21, static_cast<int>(sizeof(row21)));
            if ((n20 > 0) && (n21 > 0)) {
                const std::string text20(row20, static_cast<std::size_t>(n20));
                const std::string text21(row21, static_cast<std::size_t>(n21));
                if (starts_with_marker(text20, "Command: ") && starts_with_marker(text21, "New ")
                    && (text21.find(':') != std::string::npos)) {
                    frame.text_input_active = true;
                    frame.menu_choices.clear();
                    frame.menu_core_cursor = {};
                    frame.menu_core_cursors.clear();
                }
            }
        }

        /*
         * AH-16（後半。上の見直しの続き）: キーマップの作成/照会/削除
         * （`ui_keymap_create()`/`ui_keymap_query()`/`ui_keymap_remove()`。
         * `ui-options.c`）は行 13 に見出し（`prt(title, 13, 0)`）、行 14 に
         * `"Key: "`（`prt("Key: ", 14, 0)`）を raw に描く——「キーマップの操作」
         * 一覧（`struct menu`）の行の動作として呼ばれるだけで `an_menu_begin()` は
         * 呼ばれないので、上の数値欄と同じ形（`an_menu_active_region()` は
         * 一覧のまま、行 13〜19 だけが上書きされる）。一覧の箱（例
         * `row=2..11`）は行 13〜19 を含まないため、`fill_menu_mirror()` は
         * それらを切り落とし、`an_menu_choice_count()` も一覧の古い選択肢
         * （"Query a keymap" ほか）を返し続ける。
         *
         * `ui_keymap_create()` は Enter を含む**次の 1 打鍵をそのままキーマップへ
         * 記録する**（確定は `=` だけ）——`frame.text_input_active`（Enter を
         * 確定として扱う契約）を被せると意味が逆になる。既に同じ形の契約
         * （Enter も含めて生キーを送る）が `ModernWindow::capture_keys`
         * にあるので（`native_modern.h`。他コアは「トリガーキー:」のカーソル行
         * から立てる）、新しい契約を増やさずここから `frame.capture_keys` を
         * 立てて同じ経路に乗せる（`game_frame.h` の註記のとおり）。
         *
         * 古い一覧の選択肢は捨て（`native_modern.h` の分岐が `stage="detail"` へ
         * 落ちるように）、行 13〜19 を箱の外からでも `menu_term_lines` へ足す
         * （既にミラーに載っている行——一覧が行 11 を超えて重なる余地は無いが
         * 念のため——は二重に足さない）。
         * `translate_captured_special_key()`（`hd2d` 側）は Hengband の
         * PC/AT 表記でキーを送るため、Backspace・矢印キーはこの画面では今のところ
         * 効かない（AH-16 に既知の制約として記録）。
         */
        if (!frame.capture_keys) {
            char row13[256];
            char row14[256];
            const int n13 = an_term_row(13, row13, static_cast<int>(sizeof(row13)));
            const int n14 = an_term_row(14, row14, static_cast<int>(sizeof(row14)));
            if ((n13 > 0) && (n14 > 0)) {
                const std::string text13(row13, static_cast<std::size_t>(n13));
                const std::string text14(row14, static_cast<std::size_t>(n14));
                const bool is_keymap_screen = starts_with_marker(text13, "Create a keymap")
                    || starts_with_marker(text13, "Query a keymap")
                    || starts_with_marker(text13, "Remove a keymap");
                // `an_term_row()` は行末の空白を詰める（設計どおり）ので、`prt("Key: ", 14, 0)`
                // は末尾の空白が落ちて `"Key:"` として読める——上の行 20/21 の直しと同じ注意点
                // （空白の有無に頼らず `:` で見る）。
                if (is_keymap_screen && starts_with_marker(text14, "Key:")) {
                    frame.capture_keys = true;
                    frame.menu_choices.clear();
                    // `MenuChoice::line_index` の既定は `0`（`game_frame.h`）——`-1` ではない
                    // ので、`= {}` だけでは `native_modern.h` の `stage=="cursor"` 判定
                    // （`menu_core_cursor.line_index >= 0`）にそのまま引っかかり、
                    // `capture_keys` を見る `stage=="detail"` の分岐まで届かない。
                    frame.menu_core_cursor = MenuChoice{};
                    frame.menu_core_cursor.line_index = -1;
                    frame.menu_core_cursors.clear();
                    for (int y = 13; y <= 19; ++y) {
                        if (find_mirror_line_index(frame, y) >= 0) {
                            continue; // 既にミラーに載っている。
                        }
                        char text[1024];
                        const int n = an_term_row(y, text, static_cast<int>(sizeof(text)));
                        if (n <= 0) {
                            continue; // 空行（詰める。他の行と同じ扱い）。
                        }
                        unsigned char colors[256];
                        if (an_term_colors(y, colors, static_cast<int>(sizeof(colors))) < 0) {
                            colors[0] = 0;
                        }
                        TermMirrorLine line;
                        line.text_utf8 = text;
                        line.color = colors[0];
                        line.source_row = y;
                        frame.menu_term_lines.push_back(std::move(line));
                    }
                }
            }
        }
    } else if (frame.prompt.choices.empty() && !frame.numeric.active && !frame.text_input_active) {
        /*
         * 遊んでいる最中（`menu_open` 偽）の `-more-`（2026-09-23。「応答待ち」
         * 3 回目の調査で見つけた）。原作の `msg_flush()`（`ui-input.c`）は Term
         * 行 0 に `-more-` を書いて `anykey()` で待つが、このアダプタは遊んでいる
         * 最中は行 0 を画面へ渡していない（`an_frame.cpp` が渡すのは左の状態列と
         * 最下段だけ）。そのため、例えば Ctrl+W（ウィザードモード）の 2 行の
         * メッセージの後の待ちが**画面には何も出ないまま**続き、次の確認
         * （`get_check()`）まで進めない——実機で確認: Ctrl+W の後、フレームは
         * メッセージ 2 行を載せた 1 枚だけで、確認の問いは stderr にも出なかった。
         * 共用の `message_more_pending()`（`presentation/frame/message_more.h`）は
         * 「選択肢の無い `prompt.text_utf8` が `-more-` で終わる」を待ちの合図と
         * 定めているので、その形で渡す（`line_index = -1`＝ミラーには出ていない）。
         *
         * `frame.prompt` へこの合図を渡すこと自体は、`tools/angband/
         * ah15_floor_list_continue_smoke.py`（`send_and_settle()`）のように
         * 「`-more-` を検出したら別のキーで明示的にやり過ごしてから次のコマンドを
         * 送る」呼び手が実在するので**そのまま残す**——ここを消して `frame.prompt`
         * を一切立てずに抜けると、そうした呼び手（実機の新 UI も同じ理屈で
         * 「続ける」ボタンを出したい）が待ちに気付けず、次に送ったキーが
         * `-more-` の解除にだけ使われて本来のコマンドを 1 回分空振りさせてしまう。
         *
         * AH-35: 以前はこの合図（
         * `message_more_pending()` が真になること）を `native_modern.h::capture()`
         * が無条件に「`menu_open` 中と同じ全画面のモーダル（`window.kind="dialog"`）」
         * として組んでいた。変愚・幻想蛮怒・Sil-Q は実際には `menu_open` が真の間
         * しかこの形の `-more-` を出さない（店・知識などの画面の中）ので気付かれ
         * にくかったが、**FroxComposband は通常のダンジョン画面でも** `-more-` を
         * 出す（`frox/adapter/fc_menu.cpp::fill_row0_prompt()` の
         * `fc_awaiting_command()==0` の枝は `menu_open` を見ていない）。Angband も
         * 同様に**通常のダンジョン画面でも**（`menu_open` 偽のまま）この合図を
         * 送るので、生年直後の最初のメッセージが全画面表示に化けて見えていた
         *
         * 直しは `native_modern.h::capture()` 側——`menu_open` も選択肢も
         * ミラーも無い「素の `-more-` だけ」のときは全画面のモーダルを組まず、
         * メッセージはサブパネル（`fill_messages()`。`menu_open` に関わらず毎回
         * 埋まる）と帯（`draw_prompt_bar()`）にだけ出す。検査は
         * `tools/angband/ah35_message_in_panel_smoke.py`。
         */
        char row0[256];
        const int n = an_term_row(0, row0, static_cast<int>(sizeof(row0)));
        bool more_pending = false;
        if (n > 0) {
            const std::string text = row0;
            if (presentation::message_more_suffix(text)) {
                frame.prompt.text_utf8 = text;
                frame.prompt.line_index = -1;
                /*
                 * 決めたこと 2026-09-25。帯（`draw_prompt_bar`。`hd2d/ui/
                 * game_hud.cpp`）はこの `-more-` を全画面へは昇格させず、帯へ
                 * そのまま出す（このファイル冒頭の AH-35 の註）。決定キーで
                 * 進められることは自明ではないので、帯の 2 行目（`footer_utf8`）
                 * へ案内を添える。Angband コアは英語のままなので綴りも英語。
                 */
                frame.prompt.footer_utf8 = ui_text("Enter/A: continue");
                more_pending = true;
            }
        }
        // AH-10。`-more-` ではないと分かった行 0 を、素の 1 キー待ちの問いとして見る。
        if (!more_pending) {
            fill_raw_row0_free_prompts(frame);
        }
    }
}

/*!
 * @brief 修-19: セーブの選択（タイトル）の窓に `content_type="start"` を付ける。
 * @details 原作の `select_savefile()`（`ui-game.c`）は、行 0 に `Select the save to use (movement keys and
 * enter or mouse) or quit` を `prt()` して `menu_select()` を開く。選択肢は「New game」と「Use <セーブ>」の
 * 可変個で、変愚蛮怒の `N`/`L`/`Q` の 3 行（共通部分 `modern_content.h` が `start` と見分ける形）にも
 * Sil-Q の `a`〜`d` にも当たらない。そのため窓は誕生の選択と同じ `character` のままで、画面側
 * （`ModernStoreUi`）が案内を「取消: 戻る」と書き、問い合わせの `phase` も `birth` になっていた。
 * ここでアダプタが見分けて `start` を付ければ、画面側が「取消: 終了」を出し、`phase` も `title` になる
 * （取消は `quit(NULL)` ＝コアの終了で、画面ごと終わる）。
 * 行 0 の文言で見分ける（他の合成と同じ手口。英日の両方）。死亡後に開き直したときの同じ窓も同じ文言。
 * 共通部分は触らない。
 */
void mark_title_window(GameFrame &frame)
{
    if (!frame.pre_game_menu || frame.modern.rows.empty() || (frame.modern.stage != "choices")) {
        return;
    }
    static const char *const kHeading = "Select the save to use (movement keys and enter or mouse) or quit";
    for (const auto &line : frame.menu_term_lines) {
        if ((line.source_row == 0) && starts_with_marker(line.text_utf8, kHeading)) {
            frame.modern.content_type = "start";
            return;
        }
    }
}

/*!
 * @brief `GameFrame` を作る。設計 §6.2 のとおり、`menu_open` かどうかで分岐する
 * （S1 の見直し項目 7）。
 */
GameFrame build_frame()
{
    GameFrame frame;
    frame.frame_id = ++g_frame_id;

    frame.awaiting_command = an_awaiting_command() != 0;
    frame.menu_open = an_menu_open() != 0;
    frame.pre_game_menu = !an_character_generated();

    /*
     * AH-32の直し（2026-09-25）。以前は
     * `an::fill_game_frame()`（マス・ミニマップ・HUD・状態列/最下段・
     * サブパネル・メッセージを埋める。下を参照）を `!frame.menu_open` の
     * ときだけ呼んでいた——メニューを開いている間（店・持ち物・知識の画面
     * など）は `frame.hud`/`frame.cells`/`frame.sub2_lines` 等がこの回の
     * `GameFrame` の既定値（0/空）のまま送られ、HP・SP・所持金が 0 に
     * 見えたり、サブパネルが空になったりしていた（「見直しの指摘 9」で
     * `fill_messages()` だけ個別に呼び直していたのは、まさにこの穴の
     * 症状の 1 つに対する場当たりの手当てだった）。
     *
     * 手本は `gensoband/adapter/gb_frame.cpp`（`fill_hud()`/`fill_map()`/
     * `fill_status_col()`/`fill_sub_panels()` は**すべて `menu_open` に
     * 関わらず毎回呼ぶ**、と明記したコメントがある）・`silq`/`frox` も同じ
     * 作り——5 コアとも同じ 2 段構え（地図・HUD は常に埋める／メニューの
     * 中身だけ `menu_open` の時だけ重ねる）のはずが、Angband だけ
     * 「メニュー中は地図側を丸ごと呼ばない」という別の分岐になっていた。
     * `fill_game_frame()` 自身（`an_frame.cpp`）の `fill_cells()`/`fill_hud()`
     * は `an_map_ready()`/`an_get_hud()` の中で「まだキャラクターが居ない」
     * ときは早期 return するので、生年前の画面で呼んでも安全（1 バイトも
     * 埋まらないだけ）。
     *
     * 画面側の見た目は変えない——`hd2d/ui/game_hud.cpp` の `frame_shows_map()`
     * は `menu_term_lines` が空でない限り（＝メニューを開いている間）
     * 立体を描かず、Term の写しをそのまま出す約束のままなので、地図を
     * 毎回埋めるようにしても「メニュー中に立体の地図が透けて見える」ように
     * なるわけではない（`menu_over_map` を立てる直しは別の話——この行は
     * Angband ではまだ 1 か所も立てていない。AH-32 の対象はあくまで
     * 「メニュー中に HUD/マスの中身が消える／0 になる」ことで、地図の
     * 重ね描き自体は対象外）。HUD・状態列・サブパネル・メッセージは
     * `frame_shows_map()` を経由せず常に描かれる（`draw_game_ui()`）ので、
     * こちらは直ちに直る。
     */
    an::fill_game_frame(frame, g_tile_mapping);
    {
        std::lock_guard<std::mutex> lock(g_sub_panel_kinds_mutex);
        presentation::apply_rogue_sub_panel_kinds(frame, g_sub_panel_kinds);
    }

    if (frame.menu_open) {
        discard_stale_menu_if_needed(); // fill_menu_mirror() の切り取り枠より前に。
        fill_menu_mirror(frame);
        fill_menu_choices(frame);
        /*
         * AH-58。地図の右クリックが開いた文脈メニュー（`context_menu_cave()`・`context_menu_player()`）は、
         * 原作が `region_erase_bordered()` で自分の箱の分だけ消して地図の上へ重ねる小さな箱である。
         * `menu_over_map` の契約（`game_frame.h`）そのもの——`menu_term_lines` は箱の行だけへ刈り込んで
         * ある（`fill_menu_mirror()`）ので、画面側は立体を描き続け、箱をその上（クリックの近く）へ出せる。
         * この旗を立てるのは**その 1 枚のメニューだけ**（`an_menu_is_click_context()`）。
         * 持ち物・店・コマンドメニューなど、ほかの画面は今までどおり全面の写しのまま変わらない。
         */
        frame.menu_over_map = !frame.pre_game_menu && (an_menu_is_click_context() != 0) && menu_active_box().valid;
        fill_birth_browse_description(frame); // 誕生の種族・職業の説明（§21 の 1）。
        fill_empty_store_escape_choice(frame);
        fill_birth_final_confirm_choices(frame);
        fill_store_confirm_choices(frame); // AH-40。
        fill_birth_history_choices(frame);
        fill_birth_quickstart_choices(frame);
        fill_birth_reroll_choices(frame); // AH-19。
        fill_birth_point_based_cursor(frame);
        fill_floor_list_continue_choice(frame); // AH-15。
        fill_message_recall_paging(frame); // AH-19。
        fill_history_and_score_paging(frame); // AH-55。
        fill_knowledge_browser(frame); // 知識の 2 列画面を分類・名前の 2 列の一覧にする（案 A の続き）。
        fill_help_index_choices(frame); // AH-50。
        fill_help_text_input(frame); // AH-50。
    }

    fill_prompts(frame);
    fill_shapechange_revert_choices(frame); // AH-07。menu_open の外でも出る画面。
    // 見直しの指摘 4。受信スレッドの `input()` と同じ `g_native_menu_mutex` で包む
    // ——ロックは呼び手の `host_present()` が関数全体で持っている（待ち行列の
    // 確認から `capture()` までを 1 つの区間にするため。2026-09-23 の直し）。
    // AH-06/AH-09。`an_declared_menu_actions()` は Angband 側でしか呼ばれない
    // ——他の 5 コア・Android の `capture()` 呼び出しは引き続き第 2 引数無しか
    // 自前の一覧のままなので、この行を足しても他コアは 1 バイトも変わらない。
    capture_store_menu(frame); // AH-41。店に居るときだけ frame.modern を埋める。
    g_native_menu.capture(frame, an_declared_menu_actions(frame));
    mark_title_window(frame); // 修-19。`capture()` が組んだ窓を、セーブの選択なら `start` にする。
    /*
     * AH-04。`g_native_menu.capture()` の**後**に呼ぶ（Sil-Q の `sq_main.cpp` と同じ順）
     * ——`NativeInventoryMenu::capture()` は `frame.modern` が積んだ後の状態
     * （`frame.menu_open`・`frame.awaiting_command` など）を見て、根の一覧を
     * 出してよいかを判じるため。
     */
    g_inventory_menu.capture(
        frame,
        [] {
            return presentation::read_native_owned_items(
                an_native_owned_item,
                [](const char *s) { return std::string(s ? s : ""); },
                [](int kind_id, int unaware_category) -> std::uint16_t {
                    if (kind_id) {
                        char name[128] = {};
                        int tval = 0;
                        if (an_kind_name_and_tval(kind_id, name, sizeof(name), &tval)) {
                            const auto found = g_tile_mapping.lookup_object(name, tval);
                            if (found.tile_index) {
                                return found.tile_index;
                            }
                        }
                    }
                    if (unaware_category) {
                        return g_tile_mapping.lookup_unaware_representative(
                            static_cast<presentation::UnawareItemCategory>(unaware_category));
                    }
                    return 0;
                });
        },
        [] { return build_inventory_commands(); }); // AH-59: 持ち物の窓だけ「全部置く」を足した表。

    /*
     * 「応答待ちです」で固まる不具合の切り分け用（見直しで追加）。`an_menu_epoch()`
     * が変わった＝A2（`an_menu_begin`）が新しく呼ばれた＝**原作の側では確かに
     * 次の画面へ進んでいる**ことの証拠。変わらないまま `ui_action` が何度も
     * 届いているなら、原作へ渡すキーの中身か順序がおかしい（このログと
     * `--protocol-log=` の `in`/`ui_action` 行を突き合わせて判じること）。
     */
    static int last_epoch = -1;
    const int epoch = an_menu_epoch();
    if (epoch != last_epoch) {
        last_epoch = epoch;
        const std::string heading = frame.menu_term_lines.empty() ? std::string()
                                                                    : frame.menu_term_lines.front().text_utf8;
        std::fprintf(stderr, "[angband] menu epoch -> %d (heading=\"%s\", modern.stage=\"%s\")\n", epoch,
            heading.c_str(), frame.modern.stage.c_str());
    }

    /*
     * AH-17: `rogue_like_commands` をゲーム中にオプション画面で切り替えたら
     * コマンド一覧の表（`send_startup_tables()` が起動時に 1 度だけ送ったもの）
     * を送り直す——送り直さないと `current_keymap` が古いまま画面側に残り、
     * オプションを切り替えた直後だけ表示と実際のキー配列がずれる
     * （`gensoband/adapter/gb_main.cpp` の `g_last_rogue_like` と同じ手口）。
     */
    static int last_rogue_like = -1;
    const int rogue_like = an_rogue_like_commands();
    if (g_startup_tables_sent && (rogue_like != last_rogue_like) && (last_rogue_like != -1)) {
        const presentation::PadCommandsMessage pad = build_pad_commands();
        (void)send_message("pad_commands", presentation::encode_pad_commands(pad));
    }
    last_rogue_like = rogue_like;

    /*
     * 新 UI の目標選択（設計 §8.5 T1）。`an_wrapped_get_aim_dir()`
     * （`an_targeting.c`）が `hb_target_choose()` の中で待っている間、
     * このコアには「Term へ何か書いた」証拠が無い——原作を一切呼ばないので
     * `an_change_digest()` は変わらない。手本の Sil-Q/Frox と同じく、
     * ここで `frame.targeting` を差し込み、`menu_open`/`awaiting_command`
     * 等の以前の値を上書きする（`native_targeting_runtime.cpp` 自身が
     * 「点選択中は他の入力欄と同時に開かない」契約を守る）。
     */
    presentation::apply_native_targeting_frame(frame);

    return frame;
}

//! 直近の安い digest（`an_change_digest()`）。見直しの指摘 6。
unsigned long long g_last_digest = 0;
bool g_has_last_digest = false;

void host_present()
{
    /*
     * 根本の直し（2026-09-23。「応答待ち」3 回目の調査）。手本は
     * `rogue_legacy/adapter/rl_main.cpp` の `host_present()`——**原作がまだ読んで
     * いないキーが待ち行列にある間はフレームを組まない**。`ui_action` の返事
     * （`request_id`/`result`）を載せたフレームは、原作がそのキーを読んで画を
     * 描き直した**後**に初めて出る、という順序をここで保証する。
     * `g_native_menu_dirty` は下ろさない（待ち行列が空いた次の回に効く）ので、
     * キーを読んでも Term が変わらない操作でも返事のフレームは必ず出る。
     * 受信側（`receive_loop()`）は「翻訳→積む」を同じロックの中で行うので、
     * 「`consumed_` は立っているが待ち行列はまだ空」という瞬間は無い。
     */
    std::lock_guard<std::mutex> menu_lock(g_native_menu_mutex);
    {
        std::lock_guard<std::mutex> inbox_lock(g_inbox_mutex);
        if (!g_key_queue.empty()) {
            return;
        }
    }

    /*
     * 安いしきい値（手本は `silq/adapter/sq_main.cpp` の `host_present()`）。
     * `an_term_event()` は入力待ちの間も 10ms ごとに `present()` を呼ぶので、
     * 変わっていないフレームを毎回組み直して JSON へ直列化するのは
     * 高くつく（見直しの指摘 6。実測 CPU 約28%）。`build_frame()`（=
     * フロア全域の `map_info()` 走査を含む）自体を先に省く。
     *
     * `map_info()` は純粋関数ではない（幻覚中に乱数を引く。
     * `hallucinatory_object`/`hallucinatory_monster`）ので、待機中の無駄な呼び出しを
     * 減らすことは乱数消費も抑える（見直しの指摘 7 と併せて効く）。
     * 下の払い出し済みペイロード比較（v1 §6.4）はそのまま残す——digest は
     * 衝突しうるが、こちらは厳密だから。
     */
    const unsigned long long digest = an_change_digest();
    const bool native_menu_dirty = g_native_menu_dirty.exchange(false);

    /*
     * 「確認の問いが、キーを 2〜3 回送った後にしかフレームに出ない」の見直し用
     *。`an_check`/`an_quantity`/`an_text` の `active` が
     * 0→1 に変わった瞬間と、そのときの安いしきい値の判定（`digest` が変わったか・
     * `native_menu_dirty` か）を stderr へ出す。`--protocol-log=` の
     * `out`/`frame` 行（実際に送れた時刻）と突き合わせれば、「問いは立った
     * のに送れなかった」瞬間が分かる。
     */
    static bool s_last_check_active = false;
    static bool s_last_quantity_active = false;
    static bool s_last_text_active = false;
    an_check_state chk{};
    (void)an_get_check_state(&chk);
    an_quantity_state qty{};
    (void)an_get_quantity_state(&qty);
    an_text_state txt{};
    (void)an_get_text_state(&txt);
    // 魔法・矢弾の効果の記録が溜まっているときは、手番と格子が同じでも組む（汲まれるまで残るので、組まないと届かない）。
    const bool gate_would_skip = !native_menu_dirty && g_has_last_digest && (digest == g_last_digest) && !an::attack_fx_ready();
    if ((chk.active && !s_last_check_active) || (qty.active && !s_last_quantity_active)
        || (txt.active && !s_last_text_active)) {
        const std::string now = portable::iso_utc_now();
        if (chk.active && !s_last_check_active) {
            std::fprintf(stderr,
                "[angband] %s check prompt activated: \"%s\" digest_changed=%d dirty=%d gate_skips=%d\n",
                now.c_str(), chk.prompt, g_has_last_digest ? (digest != g_last_digest) : 1,
                native_menu_dirty ? 1 : 0, gate_would_skip ? 1 : 0);
        }
        if (qty.active && !s_last_quantity_active) {
            std::fprintf(stderr,
                "[angband] %s quantity prompt activated: \"%s\" digest_changed=%d dirty=%d gate_skips=%d\n",
                now.c_str(), qty.prompt, g_has_last_digest ? (digest != g_last_digest) : 1,
                native_menu_dirty ? 1 : 0, gate_would_skip ? 1 : 0);
        }
        if (txt.active && !s_last_text_active) {
            std::fprintf(stderr,
                "[angband] %s text prompt activated: \"%s\" digest_changed=%d dirty=%d gate_skips=%d\n",
                now.c_str(), txt.prompt, g_has_last_digest ? (digest != g_last_digest) : 1,
                native_menu_dirty ? 1 : 0, gate_would_skip ? 1 : 0);
        }
    }
    // 見直しの指摘 5: 以前はここで毎回（10ms ごと）出していたため、3 秒で 196 行になっていた。
    // 状態が変わった（＝新しく SKIPPED になった）ときだけ出す。
    static bool s_was_skipping = false;
    if (gate_would_skip && (chk.active || qty.active || txt.active)) {
        if (!s_was_skipping) {
            std::fprintf(stderr, "[angband] %s host_present: SKIPPED while a prompt is active (digest unchanged)\n",
                portable::iso_utc_now().c_str());
        }
        s_was_skipping = true;
    } else {
        s_was_skipping = false;
    }
    s_last_check_active = chk.active != 0;
    s_last_quantity_active = qty.active != 0;
    s_last_text_active = txt.active != 0;

    if (gate_would_skip) {
        return;
    }
    g_last_digest = digest;
    g_has_last_digest = true;

    GameFrame frame = build_frame();
    const std::string wire = presentation::encode_game_frame(frame);
    std::string key = frame_compare_key(wire);
    if (g_has_last_frame && (key == g_last_frame_key)) {
        return; // v1 §6.4：前回と同じ内容は送らない。
    }
    g_last_frame_key = std::move(key);
    g_has_last_frame = true;
    (void)send_message("frame", wire);
}

/* ================================================================ 起動の下拵え */

//! 自 exe のあるディレクトリを基準に 1 段ずつ降りる。
std::string under_exe(const char *a, const char *b)
{
    const std::string base = portable::exe_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + a + portable::kPathSep + b;
}

//! 実行時データの置き場（設計 §2）。`<exe_dir>/angband/lib`。
std::string angband_lib_dir()
{
    return under_exe("angband", "lib");
}

//! セーブ・遊ぶ人設定の置き場（設計 §2）。`portable::data_dir/angband`。
std::string angband_data_dir()
{
    const std::string base = portable::data_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + "angband";
}

//! 名寄せ表（設計 §6.2）。`tilework/` はリポジトリ直下（`angband/` の外）にある共有物。
std::string tile_mapping_csv_path()
{
    const std::string base = portable::exe_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + "tilework" + portable::kPathSep + "tile_mapping.csv";
}

//! アイテムの分類表（1b 直し）。`lib/` はリポジトリ直下（`angband/` の外）にある共有物
//! （変愚のほかのコアと共用。Angband 専用のものは持たない）。
std::string baseitem_jsonc_path()
{
    const std::string base = portable::exe_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + "lib" + portable::kPathSep + "edit" + portable::kPathSep + "BaseitemDefinitions.jsonc";
}

//! M2（設計 §8.6）: 変愚の絵を流用できない Angband 固有の実体の追加名寄せ表。
//! `tools/angband/an_build_tile_todo.py --write` → `tools/angband/an_gen_tiles.py`
//! が書く（生成物）。
std::string angband_tile_mapping_csv_path()
{
    const std::string base = portable::exe_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + "angband" + portable::kPathSep + "tilework" + portable::kPathSep + "mapping.csv";
}

//! M2: Angband 固有のボクセル板の置き場（`hello_ack.asset_roots.slab` で申告する）。
std::string angband_slab_asset_root()
{
    const std::string base = portable::exe_dir();
    if (base.empty()) {
        return std::string();
    }
    return base + "angband" + portable::kPathSep + "assets" + portable::kPathSep + "slab";
}

void print_usage()
{
    std::fprintf(stderr,
        "AngbandCore (%s) speaks the core protocol v%d.\n"
        "\n"
        "  usage: AngbandCore.exe %s [--lib=<dir>] [--data=<dir>]\n"
        "                          [--lang=ja|en] [--report-untranslated=<path>] [--resume]\n"
        "         AngbandCore.exe --selftest [--lang=ja]\n"
        "\n"
        "  --selftest   read lib, run init_angband(), drive the savefile-selection\n"
        "               menu with a single ESC (no host hooks are installed) to\n"
        "               confirm the A1/A2 menu hooks fire, then print the version.\n"
        "  --lib=       use this lib directory (default: <exe dir>\\angband\\lib).\n"
        "  --data=      use this data directory (default: <data dir>\\angband).\n"
        "  --lang=      core language. without it the first \"ui_state\" decides\n"
        "               . default: english.\n"
        "  --report-untranslated=<path>  append Term strings the japanese catalog\n"
        "               did not have, as E:/J: pairs (only with --lang=ja).\n"
        "  --resume     open the character the previous run saved and skip the\n"
        "               savefile menu (the frontend adds it when it relaunches the\n"
        "               core to change language; section 4).\n"
        "  --protocol-log=<path>  record every sent/received message as JSON\n"
        "               Lines (v1 section 11.2). Off by default.\n"
        "\n"
        "This executable is not meant to be started by hand. The frontend\n"
        "launches it as a child process and speaks the core protocol over\n"
        "stdin/stdout.\n",
        an_core_version(), presentation::kProtocolVersion, kUiProtocolArg.data());
    std::fflush(stderr);
}

/*!
 * @brief 訳文の置き場。`<lib_dir>/../lang/<lang>`（設計 §2 の配置。`angband/lang/ja`）。
 */
std::string lang_dir_for(const std::string &lib_dir, const std::string &lang)
{
    std::string base = lib_dir;
    while (!base.empty() && ((base.back() == '/') || (base.back() == '\\'))) {
        base.pop_back();
    }
    const std::size_t cut = base.find_last_of("/\\");
    if (cut == std::string::npos) {
        return std::string("lang") + portable::kPathSep + lang;
    }
    return base.substr(0, cut + 1) + "lang" + portable::kPathSep + lang;
}

/*!
 * @brief 言語を決めてカタログを読む（設計 §4）。**`an_bootstrap()` より前に 1 度だけ。**
 * @details 英語（`lang` が `ja` 以外）なら何も読まず、日本語層は寝たまま（制約 1）。
 * 読み込みで弾いた件（変換指定の並びが英日で違う訳ほか）は stderr に全部出す。
 */
void apply_language(const std::string &lib_dir, const std::string &lang, const std::string &report_path)
{
    const std::string dir = lang_dir_for(lib_dir, lang.empty() ? std::string("en") : lang);
    const an::LangLoadReport rep = an::lang_init(lang, dir, report_path);
    if (!rep.enabled) {
        std::fprintf(stderr, "[angband] language = en (the japanese layer is off)\n");
        std::fflush(stderr);
        return;
    }
    std::fprintf(stderr, "[angband] language = ja, catalog = %s (%d entr%s, %d name(s))\n", dir.c_str(),
        rep.entries, (rep.entries == 1) ? "y" : "ies", rep.names);
    for (const std::string &e : rep.errors) {
        std::fprintf(stderr, "[angband] catalog: %s\n", e.c_str());
    }
    std::fflush(stderr);
}

/*!
 * @brief `--selftest` の `prompt labels` の段（設計 §8 の検査）。
 * @details 原作の文言で画面を見分ける所（`starts_with_marker()` ほか）を、**英語と
 * （日本語層が起きていれば）日本語の両方の綴り**で呼んで真になることを見る。
 * わざと綴りを壊した行で偽になることも 1 度見る（`translation-breaks-ascii-screen-scrapers`）。
 * 4 か所: 履歴の確認・名前の入力（行 0 の先頭一致）・確認の `[y/n]`（訳文にも残る）・
 * 数量の問い（`Quantity (0-%d, *=all): ` を書式の段で訳した字が行 0 の頭にある）。
 */
bool selftest_prompt_labels()
{
    struct Case {
        const char *en;
        bool prefix;
    };
    static const Case kCases[] = {
        { "Accept character history? [y/n]", true },
        { "Enter a name for your character (* for a random name):", true },
    };
    bool ok = true;
    int checked = 0;
    for (const Case &c : kCases) {
        const std::string en_line = std::string(c.en) + " (row 0)";
        const std::string ja_line = std::string(an_tr(c.en)) + " (row 0)";
        const bool en_ok = starts_with_marker(en_line, c.en);
        const bool ja_ok = starts_with_marker(ja_line, c.en);
        const bool broken = starts_with_marker(std::string("X") + en_line, c.en);
        checked += 2;
        if (!en_ok || !ja_ok || broken) {
            std::fprintf(stderr, "[angband] **FAIL** prompt labels: \"%s\" en=%d ja=%d broken=%d\n", c.en, en_ok,
                ja_ok, broken);
            ok = false;
        }
    }
    {
        // 設計 §20。画面に出る行（原作の字。日本語では書式・Term の段で E: の訳になる）と、
        // その行を見分ける目印。英語の行と訳した行の両方で目印が当たることを見る。
        struct LineCase {
            const char *line;
            const char *marker;
            bool prefix;
        };
        static const LineCase kLines[] = {
            { "Command: Base Delay Factor", "Command: ", true },
            { "New base delay factor (0-255): ", "New ", true },
            { "Command: Hitpoint Warning", "Command: ", true },
            { "New hitpoint warning (0-9): ", "New ", true },
            { "Command: Movement Delay Factor", "Command: ", true },
            { "New movement delay: ", "New ", true },
            { "Key:", "Key:", true },
            { "Map sector [%d,%d], which is%s your sector.  Direction?", "Map sector", true },
            { "Map sector [%d,%d], which is%s your sector.  Direction?", "Direction?", false },
            { "['r' to reroll", "'r' to reroll", false },
            { ", 'p' for previous roll", "'p' for previous roll", false },
            { "[up/down to move, left/right to modify, 'r' to reset, 'Enter' to accept]",
                "[up/down to move, left/right to modify, 'r' to reset, 'Enter' to accept]", false },
            { "['ESC' to step back, 'S' to start over, or any other key to continue]",
                "['ESC' to step back, 'S' to start over, or any other key to continue]", false },
            { "['Y': use as is; 'N': redo; 'C': change name/history; '=': set birth options]",
                "['Y': use as is; 'N': redo; 'C': change name/history; '=': set birth options]", false },
        };
        for (const LineCase &c : kLines) {
            const std::string en_line = c.line;
            const std::string ja_line = an_tr(c.line);
            auto hit = [&c](const std::string &s) {
                return c.prefix ? starts_with_marker(s, c.marker) : contains_marker(s, c.marker);
            };
            const bool en_ok = hit(en_line);
            const bool ja_ok = hit(ja_line);
            const bool broken = c.prefix && starts_with_marker(std::string("X") + en_line, c.marker);
            checked += 2;
            if (!en_ok || !ja_ok || broken) {
                std::fprintf(stderr, "[angband] **FAIL** prompt labels: \"%s\" / \"%s\" en=%d ja=%d broken=%d\n",
                    c.line, c.marker, en_ok, ja_ok, broken);
                ok = false;
            }
        }
    }
    {
        // `[y/n]` は訳文にも残す（設計 §5・§8）。`fill_prompts()` はこれで「はい／いいえ」の桁を取る。
        const char *const kCheck = "Are you sure? ";
        const std::string ja = an_tr(kCheck);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s[y/n] ", ja.c_str());
        const bool has_yn = std::string(buf).find("[y/n]") != std::string::npos;
        checked++;
        if (!has_yn) {
            std::fprintf(stderr, "[angband] **FAIL** prompt labels: [y/n] missing in \"%s\"\n", buf);
            ok = false;
        }
    }
    {
        // 数量の問いは書式（`vstrnfmt()` のフック #2）で訳されて行 0 に出る。
        // `fill_prompts()` は `quantity.prompt`（同じ字）で行 0 の先頭一致を取る。
        const char *const kQty = "Quantity (0-%d, *=all): ";
        char want[160];
        std::snprintf(want, sizeof(want), an_tr(kQty), 5);
        const std::string row0 = std::string(want) + "1";
        const bool qty_ok = (row0.rfind(want, 0) == 0);
        checked++;
        if (!qty_ok) {
            std::fprintf(stderr, "[angband] **FAIL** prompt labels: quantity prompt \"%s\"\n", want);
            ok = false;
        }
    }
    std::fprintf(stderr, "[angband] prompt labels: %s (%d check(s), lang=%s)\n", ok ? "OK" : "FAIL", checked,
        an::lang_code().c_str());
    std::fflush(stderr);
    return ok;
}

/*!
 * @brief `--selftest` の `text codepoints` の段（修-39）。
 * @details `append_text_event_codepoints()` が UTF-8 を符号位置 1 個にし、特別キーと衝突する値
 * （U+0080〜U+009F）・壊れた並び・16 ビットに収まらない字を積まないことを見る。
 */
bool selftest_text_codepoints()
{
    struct Case {
        const char *name;
        std::string input;
        std::vector<int> expected;
    };
    const std::vector<Case> cases = {
        { "ascii", "Xyz", { 'X', 'y', 'z' } },
        { "japanese", "\xE8\xA9\xA6\xE3\x81\x97", { 0x8A66, 0x3057 } }, // 試し
        { "latin-1 (2 byte)", "\xC3\xA9", { 0xE9 } },
        { "mixed", "a\xE8\xA9\xA6" "b", { 'a', 0x8A66, 'b' } },
        { "control U+0090 dropped", "\xC2\x90", {} },
#ifdef _WIN32
        { "supplementary dropped (16-bit wchar_t)", "\xF0\x9F\x98\x80", {} },
#else
        { "supplementary kept", "\xF0\x9F\x98\x80", { 0x1F600 } },
#endif
        { "stray continuation bytes (0x80/0x9C) dropped", "\x80\x9C" "a", { 'a' } },
        { "truncated 3-byte dropped", "\xE8\xA9", {} },
        { "overlong dropped", "\xC0\x80", {} },
        { "surrogate dropped", "\xED\xA0\x80", {} },
    };
    bool ok = true;
    for (const auto &c : cases) {
        std::vector<int> got;
        append_text_event_codepoints(c.input, got);
        if (got != c.expected) {
            std::fprintf(stderr, "[angband] selftest: text codepoints: FAIL - %s (got %zu key(s))\n", c.name, got.size());
            ok = false;
        }
    }
    if (ok) {
        std::fprintf(stderr, "[angband] selftest: text codepoints: ok (%zu cases)\n", cases.size());
    }
    std::fflush(stderr);
    return ok;
}

/*!
 * @brief `--selftest`。起動列を通し、A1/A2 が実際に呼ばれることを確かめ、
 * 通ったことの証拠を出す。設計 §8 S1 の受け入れ。
 * @details フックを差さないので、null term は「ESC を積むだけ」で回る
 * （`an_term_event()` の `!an_hooks_ready` 経路）。`an_run_game()` を 1 度
 * 走らせると、`play_game(GAME_SELECT)` がセーブ選択のメニューを描いてから
 * ESC を受けて `quit(NULL)` へ抜ける——その「描く」の中で A1（`an_menu_row_drawn`）・
 * A2（`an_menu_begin`/`an_menu_end`）が呼ばれるので、`an_menu_hook_calls()` を
 * 前後で比べれば patch が当たって実行されていることの証拠になる
 * （S1 の見直し項目 8「selftest の説明と実装を合わせる」）。
 */
int run_selftest(const std::string &lib_dir, const std::string &data_dir, const std::string &lang)
{
    if (lib_dir.empty() || data_dir.empty()) {
        std::fprintf(stderr, "[angband] cannot resolve the exe/data directory\n");
        return 1;
    }

    std::fprintf(stderr, "[angband] lib = %s\n", lib_dir.c_str());
    std::fprintf(stderr, "[angband] data = %s\n", data_dir.c_str());
    std::fflush(stderr);

    apply_language(lib_dir, lang, std::string());
    if (!selftest_prompt_labels()) {
        return 1;
    }
    if (!selftest_text_codepoints()) {
        return 1;
    }
    if (!an::selftest_death_causes()) {
        return 1;
    }
    if (!an::selftest_term_translations()) {
        return 1;
    }

    int rc = an_term_install(kTermCols, kTermRows);
    if (rc != AN_OK) {
        std::fprintf(stderr, "[angband] an_term_install failed (%d)\n", rc);
        return 1;
    }

    rc = an_bootstrap(lib_dir.c_str(), data_dir.c_str());
    if (rc != AN_OK) {
        std::fprintf(stderr, "[angband] bootstrap failed (%d): %s\n", rc, an_last_error());
        an_term_remove();
        return 1;
    }

    /*
     * 状態のアイコンの表の自己検査。キャラクターは要らないが、
     * `init_angband()` の後（キャラクターシートの項目が読み込まれた後）であること。
     * 環境変数 HENGBAND_STATUS_ICON_TABLE に `hd2d/ui/status_icon_table.inc` の絶対パスがあれば、
     * 送る鍵が全部その表にあることまで見る（無ければ表の中の整合だけ。関門はパスを渡す）。
     */
    {
        char status_err[160] = { 0 };
        int status_checked = 0;
        const int status_failures = an_status_selftest(status_err, static_cast<int>(sizeof(status_err)), &status_checked);
        if (status_failures != 0) {
            std::fprintf(stderr, "[angband] selftest: status icons: FAIL - %d case(s), first: %s\n", status_failures, status_err);
            an_term_remove();
            return 1;
        }
        std::fprintf(stderr,
            "[angband] selftest: status icons: ok (%d keys checked against the icon table, %d timed rows, %d permanent rows)\n",
            status_checked, an_status_timed_count(), an_status_perm_row_count());
    }

    const int hooks_before = an_menu_hook_calls();
    rc = an_run_game(); /* フック無しなので ESC 1 発でセーブ選択メニューを抜けて quit(NULL)。 */
    const int hooks_after = an_menu_hook_calls();
    std::fprintf(stderr, "[angband] an_run_game() returned %d (ESC through the savefile menu)\n", rc);

    int cols = 0;
    int rows = 0;
    (void)an_term_size(&cols, &rows);
    char line[512];
    const int cells = an_term_row(0, line, static_cast<int>(sizeof(line)));
    std::fprintf(stderr, "[angband] term %dx%d, row0(%d) = \"%s\"\n", cols, rows, cells,
        (cells > 0) ? line : "");
    const unsigned long long selftest_digest = an_change_digest();

    an_term_remove();

    if (hooks_after <= hooks_before) {
        std::fprintf(stderr,
            "[angband] **FAIL** A1/A2 menu hooks were not called while drawing the savefile "
            "menu (before=%d after=%d)\n",
            hooks_before, hooks_after);
        std::fflush(stderr);
        return 1;
    }
    std::fprintf(stderr, "[angband] A1/A2 menu hooks: OK (%d call(s) while drawing the savefile menu)\n",
        hooks_after - hooks_before);

    /*
     * 日本語化の受け入れ。
     * `an_term_remove()` の後なので Term は混ざらない（`an_change_digest()` は Term が
     * 無ければ状態の数値だけを混ぜる）——ここでは Term を外す**前**に取った値を出す。
     */
    std::fprintf(stderr, "[angband] digest = %016llx (lang=%s)\n", selftest_digest,
        an::lang_code().c_str());

    /* 受け入れの合図（設計 §8 S1）。 */
    std::fprintf(stderr, "[angband] %s\n", an_core_version());
    std::fflush(stderr);
    return 0;
}

/* ==================================================================== 本体 */

int run_protocol(presentation::TransportHandle in, presentation::TransportHandle out,
    const std::string &lib_dir, const std::string &data_dir, const std::string &lang_arg,
    const std::string &report_path, bool resume)
{
    g_in = in;
    g_out = out;
    if ((g_in == nullptr) || (g_out == nullptr)) {
        std::fprintf(stderr, "[angband] no transport endpoint; cannot speak the protocol\n");
        return 3;
    }

    {
        const std::string csv = tile_mapping_csv_path();
        const std::string asset_root = portable::exe_dir();
        const std::string jsonc = baseitem_jsonc_path();
        const std::string extra_csv = angband_tile_mapping_csv_path();
        g_tile_mapping_loaded = !csv.empty() && g_tile_mapping.load(csv, asset_root, jsonc, extra_csv);
        if (g_tile_mapping_loaded) {
            std::fprintf(stderr, "[angband] tile mapping: %s (%d terrain, %d monster, %d object name(s))\n",
                csv.c_str(), g_tile_mapping.terrain_count(), g_tile_mapping.monster_count(),
                g_tile_mapping.object_count());
        } else {
            /* 致命ではない——絵が字のボクセル板へ落ちるだけ（捏造せずに言う）。 */
            std::fprintf(stderr, "[angband] tile mapping unavailable: %s\n", g_tile_mapping.error().c_str());
        }
    }

    std::string payload;
    std::string err;
    const presentation::TransportResult got = presentation::read_message(g_in, payload, err);
    if (got != presentation::TransportResult::Ok) {
        std::fprintf(stderr, "[angband] failed to read the first message: %s%s%s\n",
            presentation::transport_result_name(got), err.empty() ? "" : " - ", err.c_str());
        return 4;
    }
    const std::string type = presentation::peek_message_type(payload);
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
    std::fprintf(stderr, "[angband] hello from %s %s (protocol %d)\n",
        hello.ui_name.empty() ? "<unnamed ui>" : hello.ui_name.c_str(),
        hello.ui_version.empty() ? "?" : hello.ui_version.c_str(), hello.protocol);

    presentation::HelloAckMessage ack;
    ack.protocol = presentation::kProtocolVersion;
    ack.core_name = "angband";
    ack.core_version = an_core_version();
    /*
     * v1 §3.1 の `features`。リアルタイム進行・音の出来事（`audio`）は今のところ実装が無い
     * （設計の範囲外・`frame.sounds` は空のまま）ので申告しない。申告していない機能の節は
     * 画面側で自然に隠れる。言語切替（`lang-restart`）と `map_click` は下で申告する。
     *
     * `asset_root_graf`（8/16px タイル面）は申告しない——8/16px スタイルは対応していない。
     *
     * `asset_root_slab`（ボクセル板の専用置き場）は**申告する**（M2。2026-09-23）。
     * 変愚の絵を流用できない Angband 固有の実体（`angband/tilework/mapping.csv`）を
     * 用意したので、ui の板探索に `angband/assets/slab` を先に見てもらう
     * （ と同じ仕組み。ui 側は
     * ［申告された置き場 → 既定の置き場］の 2 段で探すので、変愚の絵はいままでどおり
     * 既定の置き場から引ける）。
     */
    ack.asset_root_slab = portable::path_to_utf8(angband_slab_asset_root()); // パスは ANSI で持つ。JSON に載せるときだけ UTF-8 にする（日本語の名前のフォルダで握手が落ちていた。2026-10-06）
    /*
     * 日本語化。意味は「言語を替えるには
     * **このコアを起こし直す**必要がある」——言語は `an_bootstrap()` より前に決まって
     * いなければならない（カタログを読む・名前の表を引く）。画面側
     * （`hd2d_app.cpp` の (d2)）は保存 → 終了 → 起こし直し（`--resume`）を回す。
     */
    ack.features.emplace_back("lang-restart");
    /*
     * AH-57: 地図のマスのクリック・タップを受ける（`input_event` の `e == "map_click"`）。
     * **Angband だけが申告する**——ほかのコアは申告しないので、画面はそのコアへ `map_click` を
     * 送らず、ほかのコアの動きは 1 つも変わらない。
     */
    ack.features.emplace_back("map_click");
    if (!send_message("hello_ack", presentation::encode_hello_ack(ack))) {
        return 5;
    }

    std::thread(receive_loop).detach();

    if (lib_dir.empty() || data_dir.empty()) {
        send_fatal("cannot resolve the exe/data directory");
        return 1;
    }
    std::fprintf(stderr, "[angband] lib = %s\n", lib_dir.c_str());
    std::fprintf(stderr, "[angband] data = %s\n", data_dir.c_str());

    g_script.load(); // 検証用のキー注入（ANGBAND_INJECT_KEYS / ANGBAND_INJECT_FILE）。

    /*
     * --- 言語を決める。**`an_bootstrap` より前でなければ
     * 意味がない。** `--lang=` があればそれが勝つ。無ければ最初の `ui_state` を待つ——
     * 画面側は握手の直後に必ず 1 回送ってくる。送ってこない呼び手（`tools/angband/` の
     * 検査は `ui_state` を送らない）のために上限を置き、時間切れなら英語で立てる。
     * **待った実測を stderr に出す**（Sil-Q と同じ）。
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
            std::lock_guard<std::mutex> lock(g_first_ui_state_mutex);
            lang = g_first_ui_state_lang;
            std::fprintf(stderr, "[angband] first ui_state after %lu ms, lang = %s\n",
                static_cast<unsigned long>(waited), lang.empty() ? "(not declared)" : lang.c_str());
        } else {
            std::fprintf(stderr, "[angband] no ui_state within %lu ms; starting in english\n",
                static_cast<unsigned long>(waited));
        }
    } else {
        std::fprintf(stderr, "[angband] language forced by --lang=%s (not waiting for ui_state)\n", lang.c_str());
    }
    std::fflush(stderr);
    apply_language(lib_dir, lang, report_path);
    const std::string &ui_lang = an::lang_code();

    // 新 UI のカーソル式ダイアログ（設計 §6.3）。言語は上で決めたもの（既定は "en"）。
    g_native_menu.set_enabled(true);
    g_native_menu.set_language(ui_lang);
    g_native_menu.set_title("Angband");
    // AH-04。`profile` は "frox" だけ特別扱いされる（`native_inventory_menu.h` の
    // `inventory-source` 送りキー）ので、それ以外の値なら何でもよい。
    g_inventory_menu.configure(true, ui_lang, "angband");
    // AH-41。profile は "frox"/"genso" 以外なら汎用の振る舞い。
    g_store_menu.set_enabled(true, ui_lang != "en", "angband");
    // 新 UI の目標選択（設計 §8.5 T1）。変換なし（既定の identity。コアの字は元から UTF-8）。
    presentation::configure_native_targeting(true, ui_lang, nullptr);

    int rc = an_term_install(kTermCols, kTermRows);
    if (rc != AN_OK) {
        send_fatal("could not install the headless term");
        return 1;
    }

    an_host_hooks hooks{};
    hooks.present = host_present;
    hooks.next_key = host_next_key;
    hooks.drop_keys = host_drop_keys;
    hooks.sleep_ms = host_sleep_ms;
    hooks.shutdown_requested = host_shutdown_requested;
    an_set_host_hooks(&hooks);

    rc = an_bootstrap(lib_dir.c_str(), data_dir.c_str());
    if (rc != AN_OK) {
        an_set_host_hooks(nullptr);
        if (g_ui_gone.load() || g_quit_requested.load()) {
            std::fprintf(stderr, "[angband] shut down during startup (the ui went away)\n");
            send_exit_once(g_ui_gone.load() ? 1 : 0);
            return 0;
        }
        const std::string reason = an_last_error();
        send_fatal(std::string("bootstrap failed: ") + (reason.empty() ? "(unknown)" : reason));
        an_term_remove();
        return 1;
    }

    /* 起動列が通った直後の画（セーブ選択メニューなど）を 1 回送る。 */
    host_present();

    send_startup_tables();

    /*
     * 言語切り替えの立て直し（`--resume`）なら、セーブ選択を出さずに直前のセーブを開く
     * （`an_bootstrap.c` の `an_take_resume()`）。印が無いときは普通にセーブ選択が出る。
     */
    an_set_resume(resume ? 1 : 0);
    rc = an_run_game();
    std::fprintf(stderr, "[angband] game finished (%d)\n", rc);

    an_set_host_hooks(nullptr);

    if (rc == AN_ERR_CORE) {
        const std::string reason = an_last_error();
        send_fatal(std::string("the core aborted: ") + (reason.empty() ? "(unknown)" : reason));
        return 1;
    }

    const int code = g_ui_gone.load() ? 1 : 0;
    send_exit_once(code);
    return 0;
}

} // namespace

#if !defined(_WIN32)

/*!
 * @brief dlsym 用の C 入口（Android / Quest の多コア形）。
 *
 * @details 画面（`libmain.so`）はコア選択で選ばれた `.so` からこの名前を引く
 * （`platform/android/hd2d_entry_android.cpp` の resolver）。名前はほかのコアと共通
 * ——**同時に開くのは 1 つ**（`RTLD_LOCAL`）なのでぶつからない。
 * `frox/adapter/fc_main.cpp` の同名の包みと同じ形。
 *
 * `lib_dir` / `data_dir` は `angband_lib_dir()` / `angband_data_dir()` が
 * `portable::exe_dir()` / `portable::data_dir()` から引く（Android では内部ストレージの
 * cwd）ので、Windows 専用の `--lib=` / `--data=` 引数と同じ値になる。`forwarded_args` からは
 * `--lang=` と `--resume`（言語切り替えの立て直しで画面側が足す）を拾う（main() と同じ）。
 */
extern "C" __attribute__((visibility("default"))) int hengband_core_entry(presentation::TransportHandle in,
    presentation::TransportHandle out, const std::string &protocol_log_path,
    const std::vector<std::string> &forwarded_args)
{
    std::string lang_arg;
    bool resume = false;
    for (const std::string &arg : forwarded_args) {
        // 日本語化。
        if (arg.rfind(kLangArg, 0) == 0) {
            lang_arg = arg.substr(kLangArg.size());
        }
        // 言語切り替えの立て直し（直前に保存したセーブを自動で開く）。
        if (arg == kResumeArg) {
            resume = true;
        }
    }
    if (!protocol_log_path.empty()) {
        g_log.open(protocol_log_path);
        std::fprintf(stderr, "[angband] protocol log: %s\n", protocol_log_path.c_str());
    }
    const int rc = run_protocol(in, out, angband_lib_dir(), angband_data_dir(), lang_arg, std::string(), resume);
    g_log.close();
    return rc;
}

#endif /* !_WIN32 */

int main(int argc, char **argv)
{
    bool selftest = false;
    bool has_protocol_arg = false;
    std::string lib_dir = angband_lib_dir();
    std::string data_dir = angband_data_dir();
    std::string protocol_log_path;
    std::string lang_arg;
    std::string report_path;
    bool resume = false;

    for (int i = 1; i < argc; i++) {
        const std::string_view opt = argv[i];
        if (opt.rfind(kLangArg, 0) == 0) {
            lang_arg = std::string(opt.substr(kLangArg.size()));
            continue;
        }
        if (opt.rfind(kReportUntranslatedArg, 0) == 0) {
            report_path = std::string(opt.substr(kReportUntranslatedArg.size()));
            continue;
        }
        if (opt == kResumeArg) {
            resume = true;
            continue;
        }
        if (opt == kSelfTestArg) {
            selftest = true;
            continue;
        }
        if (opt == kUiProtocolArg) {
            has_protocol_arg = true;
            continue;
        }
        if (opt.rfind(kLibArg, 0) == 0) {
            lib_dir = std::string(opt.substr(kLibArg.size()));
            continue;
        }
        if (opt.rfind(kDataArg, 0) == 0) {
            data_dir = std::string(opt.substr(kDataArg.size()));
            continue;
        }
        if (opt.rfind(kProtocolLogArg, 0) == 0) {
            protocol_log_path = std::string(opt.substr(kProtocolLogArg.size()));
            continue;
        }
        std::fprintf(stderr, "[angband] unknown argument: %s\n", argv[i]);
        print_usage();
        return 1;
    }

    if (selftest) {
        return run_selftest(lib_dir, data_dir, lang_arg);
    }
    if (!has_protocol_arg) {
        print_usage();
        return 2;
    }

    if (!protocol_log_path.empty()) {
        g_log.open(protocol_log_path);
        std::fprintf(stderr, "[angband] protocol log: %s\n", protocol_log_path.c_str());
    }

    presentation::set_stdio_binary_mode();

    const int rc = run_protocol(presentation::transport_stdin(), presentation::transport_stdout(), lib_dir, data_dir,
        lang_arg, report_path, resume);
    g_log.close();
    /*
     * 見直しの指摘 11。`run_protocol()` の中で起こした受信スレッド
     * （`receive_loop()`。`join` する場所が無いので `detach()` している——
     * Sil-Q・Gensoband・Frox も同じ構造）は、ここで `main()` を素直に
     * `return` すると、ランタイムが `g_native_menu`・`g_key_queue`・`g_log` の
     * ミューテックスなどの静的オブジェクトを破棄する一方でまだ動いている
     * ことがある。`std::_Exit()` はプロセスを即座に終わらせ、静的破壊子も
     * `atexit` も一切走らせない——受信スレッドが何かを読みかけの間に、
     * その先が壊される心配が無くなる（あとは OS がプロセスごと片づける）。
     * 標準出力・標準エラーは直前の `flush` で確実に届けてから抜ける。
     */
    std::fflush(nullptr);
    std::_Exit(rc);
}
