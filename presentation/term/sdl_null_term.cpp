/*!
 * @file sdl_null_term.cpp
 * @brief null Term フック実装（画素なし・seam 発火）
 */
#include "term/sdl_null_term.h"

#include "audio/sdl_win_audio.h"
#include "bridge/presentation_bridge.h"
#include "core/realtime-clock.h"
#include "core/special-internal-keys.h"
#include "dungeon/dungeon-processor.h"
#include "util/int-char-converter.h" //!< `ESCAPE`（小窓を畳ませるのに使う）
#include "io/input-key-acceptor.h"
#include "io/input-key-requester.h"
#include "locale/character-encoding.h"
#include "system/angband-system.h"
#include "system/player-type-definition.h"
#include "term/gameterm.h"
#include "term/term-color-types.h"
#include "term/z-term.h"
#include "world/world.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace presentation {

namespace {

//! ただ 1 つの null term。フックは C 関数ポインタのため静的ディスパッチする。
SdlNullTerm *s_active_null_term = nullptr;

/*!
 * @brief K-49: 入力追跡ログの書き出し先。指していなければ nullptr（＝ログ無し）。
 */
std::FILE *input_log_file()
{
    static std::FILE *fp = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *const path = std::getenv("HENGBAND_SDL2_INPUT_LOG");
        if ((path != nullptr) && (path[0] != '\0')) {
            fp = std::fopen(path, "a");
        }
    }

    return fp;
}

/*!
 * @brief ログを採るかどうか。**引数を組み立てる前に**問うためにある。
 * @details `input_log` は fp が無ければ黙って戻るが、呼ぶ側が文字列を作る手間は残る。
 * 行を組み立てるのに手間のかかる診断（画面の中身など）はこれで囲うこと。
 */
bool input_log_enabled()
{
    return input_log_file() != nullptr;
}

/*!
 * @brief K-49: 入力追跡ログ（`HENGBAND_SDL2_INPUT_LOG=<path>`）。ui 側と同じファイルへ書く。
 */
void input_log(const char *fmt, ...)
{
    std::FILE *const fp = input_log_file();
    if (fp == nullptr) {
        return;
    }
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(fp, fmt, ap);
    va_end(ap);
    std::fflush(fp);
}

/*!
 * @brief キーをキューの**末尾**へ積む（K-20 / `main-win.cpp:1977` の `term_keypress` と同じ）。
 * @return 積めたら 0。キューが満杯なら 1
 * @details `term_key_push()` は**先頭**へ積む関数（`src/term/z-term.cpp:1770` に
 * "Add a keypress to the FRONT of the queue"）で、1 回の `on_event` で 2 個以上積むと
 * **消費順が逆転する**。K-16 の「積むのは 1 回 1 キー」という制約はこれが原因だった。
 * 本来 UI が積むのは「利用者がその順に押したキー」なので、**FIFO が正しい**。
 * ここを直すと、コマンドメニュー経由の複数キー注入・キーマップ逆引きの多バイト列が
 * そのままの順で通るようになる（K-16 §14.2 の制約は解消）。
 * @note `key_queue` / `key_head` / `key_size` は `z-term.h` の公開メンバなので、
 * コアを改変せずに（必守制約 1）ここから積める。main-win.cpp が同じことをしている。
 */
errr push_key_back(int k)
{
    if (k == 0) {
        return -1; // 非キーは積まない（term_key_push と同じ約束）
    }
    if (game_term == nullptr) {
        return -1;
    }

    game_term->key_queue[game_term->key_head++] = static_cast<char>(k);
    if (game_term->key_head == game_term->key_size) {
        game_term->key_head = 0; // 円環キュー
    }

    // 追い付いた＝満杯。これ以上積むと古いキーを踏むので落とす。
    return (game_term->key_head != game_term->key_tail) ? 0 : 1;
}

/*!
 * @brief IME 由来の UTF-8 バイト列をコアの内部文字コードへ直す（A-T13）。
 * @param keys ui から受け取ったキー列。非 ASCII の連なりをその場で置き換える
 * @details `SDL_TEXTINPUT` の `text` は**常に UTF-8**（ui はそれを 1 バイトずつ積む）。
 * 一方コアの内部文字コードは Windows が Shift-JIS、Android・Unix が EUC-JP で、
 * `askfor()`（`src/core/asking-player.cpp:152`）は先頭バイトを `iskanji()` で見て
 * **2 バイトで 1 文字**として取り込む。UTF-8 のまま積むと「あ」（`E3 81 82`）が
 * `E3 81` の 1 文字＋余った `82` として食われ、豆腐（□）や別の文字になる。
 * ここで直しておけば表示側は素通りでよい（Term ミラーは `sys_to_utf8` で戻すため）。
 * X11 版が XIM の入力に `utf8_to_euc` をかけているのと同じ手当て（`src/main-x11.cpp:1147`）。
 * @note 非 ASCII が来るのは `SDL_TEXTINPUT` だけ。方向・コマンドキーは画面側が ASCII へ
 * 翻訳して積む（`bridge/input_event_adapter.cpp`）ので、ASCII は 1 バイトずつ素通しする。
 */
void convert_text_input_to_system_encoding(KeyQueue &keys)
{
#ifdef JP
    const auto has_non_ascii = std::any_of(keys.begin(), keys.end(), [](int k) { return (k >= 0x80) && (k <= 0xff); });
    if (!has_non_ascii) {
        return; // 通常のプレイでは毎回ここで戻る
    }

    KeyQueue converted;
    converted.reserve(keys.size());
    std::string utf8_run;
    const auto flush_run = [&converted, &utf8_run] {
        if (utf8_run.empty()) {
            return;
        }
        if (const auto sys_str = utf8_to_sys(utf8_run)) {
            for (const auto ch : *sys_str) {
                converted.push_back(static_cast<unsigned char>(ch));
            }
        } else {
            // 内部文字コードに無い文字は**捨てる**。UTF-8 のまま流すと上記のとおり化ける。
            std::fprintf(stderr, "[textinput] utf8 -> system encoding failed (%zu bytes dropped)\n", utf8_run.size());
        }
        utf8_run.clear();
    };

    for (const int key : keys) {
        if ((key >= 0x80) && (key <= 0xff)) {
            utf8_run.push_back(static_cast<char>(static_cast<unsigned char>(key)));
            continue;
        }
        flush_run(); // ASCII で連なりが切れる
        converted.push_back(key);
    }
    flush_run();

    keys.swap(converted);
#else
    (void)keys;
#endif
}

/*!
 * @brief K-38: スクリプトどおりにキーを流す（無人での画面確認・デバッグセーブ作成用）。
 * @details 合成キー注入（`SendInput` / `SendKeys`）では SDL に**印字キーしか届かなかった**
 * （2026-07-29 に 3 通り試して確認。scancode のみ / VK＋scancode / `SendKeys "{ENTER}"`
 * のいずれでも Enter・ESC が無視される）。そのため
 *   - キャラクター作成（ロール確認の `Enter`・名前入力）
 *   - `^A` のデバッグコマンド
 * を外から自動化できず、K-37 で足した 10 画面の実機確認が人力頼みになっていた。
 *
 * **K-48（2026-07-30）で原因が割れて直った**（走査符号 0 のキーメッセージを SDL が
 * 捨てていた。旧 2D UI の入力の繕い）。走っているゲームを外から触るなら
 * `HENGBAND_SDL2_INJECT_KEYS`（`tools/hd2d_verify/playthrough.py` が使う）の方が素直である。
 * ここは**起動の瞬間から決め打ちで流したいとき**（デバッグ用セーブの作成など）のために残す。
 *
 * ここは**コアが入力を待っている間だけ**、スクリプトのキーを 1 個ずつ `push_key_back()` へ流す。
 * 1 回の入力要求につき 1 個しか流さないので、勝手に先走らない。
 *
 * `src/` は触らない（必守制約 1）。既存の `HENGBAND_SDL2_MENUCHOICE_SMOKE` /
 * `HENGBAND_SDL2_MENU_MIRROR_LOG` と同じ、環境変数で入る調査用のオプションである。
 *
 * | 環境変数 | 内容 |
 * |----------|------|
 * | `HENGBAND_SDL2_INJECT_KEYS` | スクリプトそのもの |
 * | `HENGBAND_SDL2_INJECT_FILE` | スクリプトの入ったファイル（長いスクリプトはこちら） |
 *
 * スクリプトの書き方（ファイルは `#` 始まりの行と**空白・改行を全部落とす**ので、
 * 読みやすく改行してよい。空白そのものが要るときは `\s`）:
 *
 * | 綴り | 意味 |
 * |------|------|
 * | `\r` `\n` `\e` `\t` `\s` | Enter / 改行 / ESC / Tab / 空白 |
 * | `\^A`〜`\^Z` | Ctrl+A〜Ctrl+Z（`\^A` がデバッグコマンド） |
 * | `\xHH` | 生のバイト |
 * | `\.` | **キーを積まずに少し待つ**（約 0.25 秒。`-more-` や演出の後で使う） |
 * | `\\` | `\` そのもの |
 * | 上記以外 | その 1 バイトをそのまま |
 */
class ScriptedKeyInjector {
public:
    static ScriptedKeyInjector &get_instance()
    {
        static ScriptedKeyInjector instance;
        return instance;
    }

    /*!
     * @brief スクリプトを 1 コマ進める。
     * @return キーを積んだら true（呼び出し側はそのまま戻ってよい）
     */
    bool feed()
    {
        if (this->wait_loops_ > 0) {
            --this->wait_loops_;
            return false;
        }
        if (this->next_ >= this->keys_.size()) {
            return false; // 打ち止め。以降は人間の操作を待つ
        }

        const int key = this->keys_[this->next_++];
        if (key == kWaitToken) {
            this->wait_loops_ = kWaitLoops;
            return false;
        }
        return push_key_back(key) == 0;
    }

private:
    //! `\.` 1 個で待つ `on_event` のループ回数（1 周 10 ms）。
    static constexpr int kWaitLoops = 25;
    static constexpr int kWaitToken = -1;

    ScriptedKeyInjector()
    {
        if (const char *const inline_script = std::getenv("HENGBAND_SDL2_INJECT_KEYS")) {
            this->parse(inline_script, false);
        }
        const char *const path = std::getenv("HENGBAND_SDL2_INJECT_FILE");
        if (path == nullptr || path[0] == '\0') {
            return;
        }
        FILE *fp = std::fopen(path, "rb");
        if (fp == nullptr) {
            return;
        }
        std::string body;
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
            std::string line = buf;
            // `#` から行末まではコメント。スクリプトの中に `#` を積みたいときは `\x23`。
            if (const size_t hash = line.find('#'); hash != std::string::npos) {
                line.erase(hash);
            }
            body += line;
        }
        std::fclose(fp);
        this->parse(body.c_str(), true);
    }

    //! @param strip_space ファイル由来なら真（空白・改行を落として読みやすく書けるようにする）
    void parse(const char *script, bool strip_space)
    {
        for (size_t i = 0; script[i] != '\0'; ++i) {
            const char c = script[i];
            if (c != '\\') {
                if (strip_space && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
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
            case 'r':
                this->keys_.push_back(0x0D);
                break;
            case 'n':
                this->keys_.push_back(0x0A);
                break;
            case 'e':
                this->keys_.push_back(0x1B);
                break;
            case 't':
                this->keys_.push_back(0x09);
                break;
            case 's':
                this->keys_.push_back(' ');
                break;
            case '.':
                this->keys_.push_back(kWaitToken);
                break;
            case '^': {
                const char ctrl = script[i + 1];
                if (ctrl >= 'A' && ctrl <= 'Z') {
                    this->keys_.push_back(ctrl - 'A' + 1);
                    ++i;
                } else if (ctrl >= 'a' && ctrl <= 'z') {
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
                    if (h >= '0' && h <= '9') {
                        d = h - '0';
                    } else if (h >= 'a' && h <= 'f') {
                        d = h - 'a' + 10;
                    } else if (h >= 'A' && h <= 'F') {
                        d = h - 'A' + 10;
                    }
                    if (d < 0) {
                        break;
                    }
                    value = value * 16 + d;
                    ++i;
                    ++digits;
                }
                if (digits > 0 && value != 0) {
                    this->keys_.push_back(value);
                }
                break;
            }
            default:
                this->keys_.push_back(static_cast<unsigned char>(esc));
                break;
            }
        }
    }

    std::vector<int> keys_;
    size_t next_{ 0 };
    int wait_loops_{ 0 };
};

void hook_init(term_type *) {}
void hook_nuke(term_type *) {}

errr hook_text(TERM_LEN, TERM_LEN, int, TERM_COLOR, std::string_view)
{
    return 0; // 画素を出さない（バッファはコアが保持）
}

errr hook_wipe(TERM_LEN, TERM_LEN, int)
{
    return 0; // 画素を出さない
}

errr hook_curs(TERM_LEN, TERM_LEN)
{
    return 0; // カーソル描画なし
}

errr hook_pict(TERM_LEN, TERM_LEN, int, const TERM_COLOR *, const TermChar *, const TERM_COLOR *, const TermChar *)
{
    return 0; // タイル描画は画面側（Bridge 経由）で行う
}

errr hook_xtra(int n, int v)
{
    if (s_active_null_term == nullptr) {
        return 1;
    }

    switch (n) {
    case TERM_XTRA_FRESH:
        s_active_null_term->on_fresh();
        return 0;
    case TERM_XTRA_CLEAR:
        return 0; // z-term 側でバッファクリア。画素なし
    case TERM_XTRA_EVENT:
        return s_active_null_term->on_event(v);
    case TERM_XTRA_RT_INPUT:
        s_active_null_term->on_rt_input();
        return 0;
    case TERM_XTRA_RT_PACE:
        s_active_null_term->on_rt_pace();
        return 0;
    case TERM_XTRA_FLUSH:
        s_active_null_term->on_flush();
        return 0;
    case TERM_XTRA_DELAY:
        s_active_null_term->on_delay(v);
        return 0;
    case TERM_XTRA_SOUND:
    case TERM_XTRA_MUSIC_BASIC:
    case TERM_XTRA_MUSIC_DUNGEON:
    case TERM_XTRA_MUSIC_QUEST:
    case TERM_XTRA_MUSIC_TOWN:
    case TERM_XTRA_MUSIC_MONSTER:
    case TERM_XTRA_MUSIC_MUTE:
    case TERM_XTRA_SCENE:
    case TERM_XTRA_NOISE: {
        const int audio_rc = sdl_win_audio_term_xtra(n, v);
        if (audio_rc != 1) {
            return audio_rc;
        }
        return 0;
    }
    case TERM_XTRA_SHAPE:
    case TERM_XTRA_BORED:
    case TERM_XTRA_REACT:
    case TERM_XTRA_ALIVE:
    case TERM_XTRA_LEVEL:
        return 0; // 任意アクション。null term では no-op
    default:
        return 1; // 未対応（コアは optional として無視）
    }
}

} // namespace

void SdlNullTerm::install(PlayerType *player, Bridge *bridge, const UiSeam &seam)
{
    player_ = player;
    bridge_ = bridge;
    seam_ = seam;
    s_active_null_term = this;

    term_init(&term_, TERM_DEFAULT_COLS, TERM_DEFAULT_ROWS, 1024);
    term_.attr_blank = TERM_WHITE;
    term_.char_blank = ' ';
    term_.soft_cursor = true;
    term_.always_pict = false;
    term_.always_text = false;

    term_.init_hook = hook_init;
    term_.nuke_hook = hook_nuke;
    term_.text_hook = hook_text;
    term_.wipe_hook = hook_wipe;
    term_.curs_hook = hook_curs;
    term_.pict_hook = hook_pict;
    term_.xtra_hook = hook_xtra;
    term_.data = this;

    angband_terms[0] = &term_;
    term_activate(&term_);
}

void SdlNullTerm::on_fresh()
{
    if (!seam_.present || bridge_ == nullptr) {
        return;
    }
    // H1: capture は null term に残す。set_view_size は platform の before_capture で先行。
    if (seam_.before_capture) {
        seam_.before_capture();
    }
    seam_.present(bridge_->capture(player_));
}

errr SdlNullTerm::on_event(int wait)
{
    KeyQueue keys;

    if (wait != 0) {
        /*
         * リアルタイムの検証用（設計書 §13）。**コアが素の入力待ちへ落ちた瞬間**を残す。
         * リアルタイムが効いているなら、コマンド待ちはここではなく `on_rt_input` に来る。
         * ここに来たということは `can_wait_realtime_command` が偽だったか、小窓に入ったかで、
         * その切り分けに要る材料（行 0 の中身と、判定に使う値）をまとめて出す。
         * @note 2026-08-12 の実測では、これで「被弾の `-more-` で止まっていた」と分かった。
         * `auto_more` は被弾中だけ意図的に効かない（`display-messages.cpp` の
         * `auto_more && !now_damaged`）ので、リアルタイムでは殴られるたびに時計が止まる。
         */
        if (input_log_enabled()) {
            const auto *const p = player_;
            input_log("[core] blocking wait turn=%d energy=%d action=%d run=%d rep=%d new=%d next=%s phase=%d walk=%d rt=%d\n",
                static_cast<int>(AngbandWorld::get_instance().game_turn),
                (p != nullptr) ? static_cast<int>(p->energy_need) : -999,
                (p != nullptr) ? static_cast<int>(p->action) : -1,
                (p != nullptr) ? static_cast<int>(p->running) : -1,
                static_cast<int>(command_rep), static_cast<int>(command_new),
                (inkey_next == nullptr) ? "(null)" : ((*inkey_next == '\0') ? "(empty)" : inkey_next),
                AngbandSystem::get_instance().is_phase_out() ? 1 : 0,
                ((p != nullptr) && p->timewalk) ? 1 : 0,
                RealtimeClock::get_instance().is_enabled() ? 1 : 0);
            if ((this->term_.scr != nullptr) && !this->term_.scr->c.empty()) {
                //! セルは文字を丸ごと持つ（`term/term-char.h`）ので、1 桁ずつ繋ぐ。
                std::string row0;
                for (const auto &cell : this->term_.scr->c[0]) {
                    row0.append(cell.view());
                }
                input_log("[core] row0=[%s]\n", row0.c_str());
            }
        }
        for (;;) {
            // 死亡プロンプト等、inky 待ち中も Term ミラーを再描画する。
            if (seam_.present && bridge_ != nullptr) {
                if (seam_.before_capture) {
                    seam_.before_capture();
                }
                seam_.present(bridge_->capture(player_));
            }
            if (seam_.pump_input) {
                seam_.pump_input(keys);
            }
            if (!keys.empty()) {
                break;
            }
            /*
             * K-38: コアが入力を待っているこの瞬間だけ、スクリプトのキーを 1 個流す。
             * 積んだ時点で戻る（下の for は空の `keys` を回すだけなので同じこと）。
             * 環境変数が無ければスクリプトは空なので、通常のプレイでは何も起きない。
             */
            if (ScriptedKeyInjector::get_instance().feed()) {
                return 0;
            }
            if (seam_.quit_requested && seam_.quit_requested()) {
                keys.push_back(0x1B); // ESC で戻す（クローズ要求のフォールバック）
                break;
            }
            // 小窓の裏で世界を進める（設計書 §7）。許された小窓でだけ働く。
            if (this->rt_step_world_in_prompt()) {
                return 0;
            }
            if (seam_.delay_ms) {
                seam_.delay_ms(10);
            }
        }
    } else {
        if (seam_.pump_input) {
            seam_.pump_input(keys);
        }
        if (keys.empty()) {
            return 1; // 準備できたキーなし
        }
    }

    // A-T13: IME から来た UTF-8 をコアの内部文字コードへ直してから積む。
    convert_text_input_to_system_encoding(keys);

    // K-20: **末尾**へ積む。term_key_push（先頭挿入）で回すと、1 回の pump_input で
    // 2 個以上積んだときに消費順が逆転していた。
    for (const int key : keys) {
        input_log("[core] push 0x%02x (wait=%d)\n", static_cast<unsigned>(key) & 0xffu, wait);
        push_key_back(key);
    }
    return 0;
}

void SdlNullTerm::on_flush()
{
    KeyQueue discard;
    if (seam_.pump_input) {
        seam_.pump_input(discard);
    }
    for (const int key : discard) {
        input_log("[core] FLUSH drop 0x%02x\n", static_cast<unsigned>(key) & 0xffu);
    }
}

void SdlNullTerm::on_delay(int ms)
{
    /*
     * リアルタイムでは待たない（設計書 §7・§8）。ここで寝るのは弾道や爆風を 1 コマずつ
     * 見せるためだが、**その待ちがそのまま世界を止める**。飛道の見せ場は
     * `CombatFeedback` に 1 発 1 個で積んであり、間を作るのは画面側の役目にしてある。
     * 小窓の裏（`is_stepping_world`）では、描いた絵が `screen_load` で消えるので余計に無駄。
     */
    if (RealtimeClock::get_instance().is_stepping_world()) {
        return;
    }

    if (seam_.delay_ms && ms > 0) {
        seam_.delay_ms(ms);
    }
}

/*** リアルタイムモード ***/

namespace {

/*!
 * @brief 単調時計の巻き戻り（約 49 日）に耐える「もう過ぎたか」
 * @details `now >= deadline` と素で書くと、跨いだ瞬間に「まだ来ていない」と判定して
 * 49 日ぶん待つことになる。差を符号付きで見る。
 */
bool rt_time_reached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

//! 締切待ちの 1 周の長さ。刻みが短いときは細かく回す。
int rt_poll_interval_ms(int ms_per_tick)
{
    return std::clamp(ms_per_tick / 4, 1, 10);
}

/*!
 * @brief 先行入力を 1 枠に切り詰める（「先行入力は 1 ターン先まで」と決めた）
 * @details **1 論理キーは 1 文字とは限らない。** F キー・矢印などの特殊キーは
 * `bridge/input_event_adapter.cpp` が `31 ... 13` の連なりを積む。
 * 文字数で切ると連なりの途中で千切れて化けるので、区切りを見て**最後の 1 個**だけ残す。
 */
void rt_keep_last_logical_key(KeyQueue &keys)
{
    if (keys.size() <= 1) {
        return;
    }

    size_t last_begin = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == 31) {
            last_begin = i; // 特殊キーの連なりの先頭
            while ((i < keys.size()) && (keys[i] != 13)) {
                ++i;
            }
            continue;
        }
        last_begin = i;
    }

    keys.erase(keys.begin(), keys.begin() + static_cast<KeyQueue::difference_type>(last_begin));
}

} // namespace

uint32_t SdlNullTerm::rt_now_ms() const
{
    return seam_.now_ms ? seam_.now_ms() : 0u;
}

bool SdlNullTerm::rt_has_pending_key() const
{
    return this->term_.key_head != this->term_.key_tail;
}

bool SdlNullTerm::rt_begin(int &ms_per_tick)
{
    // 時計が束ねられていなければリアルタイムは働かない（＝従来どおりのターン制）。
    if (!seam_.now_ms) {
        return false;
    }

    auto &clock = RealtimeClock::get_instance();
    ms_per_tick = clock.get_ms_per_tick();
    if (clock.consume_resync()) {
        this->rt_next_tick_at_ = this->rt_now_ms() + static_cast<uint32_t>(ms_per_tick);
    }

    return true;
}

void SdlNullTerm::rt_pump_single_key()
{
    if (!seam_.pump_input) {
        return;
    }

    KeyQueue keys;
    seam_.pump_input(keys);
    if (keys.empty()) {
        return;
    }

    convert_text_input_to_system_encoding(keys);

    // 既に 1 枠埋まっているなら捨てる。「1 ターン先まで」なので溜めない。
    if (this->rt_has_pending_key()) {
        for (const int key : keys) {
            input_log("[core] RT drop 0x%02x (buffer full)\n", static_cast<unsigned>(key) & 0xffu);
        }
        return;
    }

    rt_keep_last_logical_key(keys);
    for (const int key : keys) {
        input_log("[core] RT push 0x%02x\n", static_cast<unsigned>(key) & 0xffu);
        push_key_back(key);
    }
}

bool SdlNullTerm::rt_step_world_in_prompt()
{
    auto &clock = RealtimeClock::get_instance();
    if (!clock.is_enabled() || !clock.is_prompt_background_wanted()
        || !clock.is_prompt_background_allowed() || clock.is_stepping_world()) {
        return false;
    }
    if (player_ == nullptr) {
        return false;
    }

    int ms_per_tick = 0;
    if (!this->rt_begin(ms_per_tick)) {
        return false;
    }
    if (!rt_time_reached(this->rt_now_ms(), this->rt_next_tick_at_)) {
        return false; // まだ刻みの境界に来ていない
    }

    /*
     * 歯止め。モンスターの攻撃が `-more-` を出すと、その入力待ちからここへ戻ってくる。
     * 旗を立てておけば、そちらは素の待ちとして振る舞う（設計書 §7・壁 1）。
     * 描画の間（`term_xtra(TERM_XTRA_DELAY)`）も、この旗で飛ばす（壁 4）。
     */
    clock.set_stepping_world(true);
    const auto alive = advance_world_one_tick(player_);
    clock.set_stepping_world(false);

    this->rt_next_tick_at_ += static_cast<uint32_t>(ms_per_tick);
    const auto now = this->rt_now_ms();
    if (rt_time_reached(now, this->rt_next_tick_at_ + static_cast<uint32_t>(ms_per_tick * 3))) {
        this->rt_next_tick_at_ = now + static_cast<uint32_t>(ms_per_tick);
    }

    input_log("[core] RT prompt tick turn=%d alive=%d\n",
        static_cast<int>(AngbandWorld::get_instance().game_turn), alive ? 1 : 0);

    /*
     * 死んだ・階を出た → `ESC` で小窓を畳ませる（壁 3）。`take_hit` は `is_dead` を立てて
     * そのまま戻るだけなので、こちらから畳まないと死体が持ち物を選び続けることになる。
     * 生きている → 「書き直して待ち直せ」の札を積む（決めたこと）。
     */
    push_key_back(alive ? SPECIAL_KEY_REDRAW : ESCAPE);
    return true;
}

void SdlNullTerm::on_rt_input()
{
    int ms_per_tick = 0;
    if (!this->rt_begin(ms_per_tick)) {
        return; // 見送りを立てずに戻る＝コアは従来どおり入力を待つ
    }

    auto &clock = RealtimeClock::get_instance();
    for (;;) {
        if (this->rt_has_pending_key()) {
            return; // 先行入力が入っている。そのまま行動へ
        }

        if (seam_.present && (bridge_ != nullptr)) {
            if (seam_.before_capture) {
                seam_.before_capture();
            }
            seam_.present(bridge_->capture(player_));
        }

        this->rt_pump_single_key();
        if (this->rt_has_pending_key()) {
            return;
        }

        /*
         * スクリプト（K-38）はここでも流す。既定ではスクリプトがあるとリアルタイムは切れる
         * （`apply_realtime_mode`）ので、ここへ来るのは `HENGBAND_SDL2_REALTIME_FORCE=1` で
         * わざと併用したとき、つまり**リアルタイムの検証中**だけである。
         */
        if (ScriptedKeyInjector::get_instance().feed() || this->rt_has_pending_key()) {
            return;
        }

        if (seam_.quit_requested && seam_.quit_requested()) {
            push_key_back(0x1B);
            return;
        }

        if (rt_time_reached(this->rt_now_ms(), this->rt_next_tick_at_)) {
            /*
             * 締切。押しっぱなしの方向があればそれを 1 個だけ効かせる
             * （「ターン切り替え時に押されていたキーが次ターンの入力」）。
             */
            if (seam_.held_dir_key) {
                if (const int key = seam_.held_dir_key(); key != 0) {
                    input_log("[core] RT held dir 0x%02x\n", static_cast<unsigned>(key) & 0xffu);
                    push_key_back(key);
                    return;
                }
            }

            clock.set_holding(true); // 見送り。締切を進めるのは on_rt_pace だけ
            return;
        }

        if (seam_.delay_ms) {
            seam_.delay_ms(rt_poll_interval_ms(ms_per_tick));
        }
    }
}

void SdlNullTerm::on_rt_pace()
{
    int ms_per_tick = 0;
    if (!this->rt_begin(ms_per_tick)) {
        return;
    }

    while (!rt_time_reached(this->rt_now_ms(), this->rt_next_tick_at_)) {
        if (seam_.present && (bridge_ != nullptr)) {
            if (seam_.before_capture) {
                seam_.before_capture();
            }
            seam_.present(bridge_->capture(player_));
        }

        this->rt_pump_single_key(); // この刻みの間に押されたキーが次の行動になる
        if (seam_.quit_requested && seam_.quit_requested()) {
            break;
        }

        if (seam_.delay_ms) {
            seam_.delay_ms(rt_poll_interval_ms(ms_per_tick));
        }
    }

    /*
     * 検証用（設計書 §13-1・§13-2）。`HENGBAND_SDL2_INPUT_LOG` を指したときだけ書く。
     * 「10 秒で何刻み進んだか」「その間に何回行動したか」は、この行と `RT push` を数えれば出る。
     * **加速して行動回数が増えるかを見ないと、見送りが「その場に留まる」に退化していても気づけない。**
     */
    input_log("[core] RT tick turn=%d at=%u\n",
        static_cast<int>(AngbandWorld::get_instance().game_turn), this->rt_now_ms());

    this->rt_next_tick_at_ += static_cast<uint32_t>(ms_per_tick);

    /*
     * 大きく遅れていたら張り直す（設計書 §4 の slack）。
     * 小窓で 30 秒止まったあと、遅れを取り返そうとして早送りが暴発するのを防ぐ。
     * **§6「小窓では時計を止める」はここで効いている。**
     */
    const auto now = this->rt_now_ms();
    if (rt_time_reached(now, this->rt_next_tick_at_ + static_cast<uint32_t>(ms_per_tick * 3))) {
        this->rt_next_tick_at_ = now + static_cast<uint32_t>(ms_per_tick);
    }
}

} // namespace presentation
