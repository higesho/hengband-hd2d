/*!
 * @file sdl_game_bootstrap.cpp
 * @brief SDL 入口本置換のコア起動シーケンス（既存 public API のみ）
 *
 * GDI 版は init 後に File>New / File>Open 待ち。SDL 入口に File メニューは無いため、
 * Term 上のオープニング（news）＋ [N]新規 / [L]ロード 選択を挟んでから play_game する。
 * ロード時のセーブ選択も OS ファイルダイアログではなく Term 内メニューで行う。
 */
#include "bootstrap/sdl_game_bootstrap.h"

#include "audio/sdl_win_audio.h"
#include "bridge/pad_command_bridge.h"
#include "bridge/presentation_bridge.h"
#include "frame/pad_command_table.h"
#include "frame/sdl_ui_options.h"
#include "term/sdl_null_term.h"
#include "term/sdl_sub_window_terms.h"

#include "core/game-play.h"
#include "game-option/runtime-arguments.h"
#include "io/files-util.h"
#include "io/input-key-acceptor.h"
#include "io/signal-handlers.h"
#include "main/angband-initializer.h"
#include "main-win/main-win-define.h"
#include "player/process-name.h"
#include "system/angband.h"
#include "system/angband-system.h"
#include "system/player-type-definition.h"
#include "term/gameterm.h"
#include "term/screen-processor.h"
#include "term/term-color-types.h"
#include "term/z-term.h"
#include "term/z-util.h"
#include "term/z-form.h"
#include "util/angband-files.h"
#include "util/int-char-converter.h"
#include "util/string-processor.h"
#include "util/stack-trace.h"
#include "world/world.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace presentation {

namespace {

/*!
 * @brief `quit()` 時の後始末フック（GDI 資源は保持していないため最小）。
 * @details 後始末そのものは無いが、**「誰が・なぜ終わらせたか」を必ず残す**。
 * `quit("")` は静かに `exit(0)` するので、記録が無いと「起動直後に落ちた」ようにしか
 * 見えない（Android のエミュレータで実際にこれで調査が止まった）。
 * 呼び出し元は `util::StackTrace` で取る（Windows・Android とも実装がある）。
 */
void sdl_quit_hook(std::string_view reason)
{
    std::fprintf(stderr, "[quit] reason=\"%.*s\"\n",
        static_cast<int>(reason.size()), reason.data());
    std::fprintf(stderr, "[quit] backtrace:\n%s", util::StackTrace().dump().c_str());
    std::fflush(stderr);
}

//! オープニングの選択肢（縦メニュー。並びがそのまま表示順・カーソル位置）。
enum class OpeningChoice : int {
    NewGame = 0,
    LoadGame,
    Quit,
    Count
};

/*!
 * @brief オープニング 1 項目分。`letter` は**従来から残している文字キー**（N / L / Q）。
 * @details 表示は ASCII のみ（この TU の制約。下の draw_opening_screen の注意を参照）。
 */
struct OpeningEntry {
    char letter;
    const char *label;
};

const OpeningEntry kOpeningEntries[static_cast<int>(OpeningChoice::Count)] = {
    { 'N', "New Game" },
    { 'L', "Load" },
    { 'Q', "Quit" },
};

//! news を描く行数。ここから下は選択メニューに使う（24 行 Term の下 5 行）。
constexpr int kOpeningNewsRows = 19;
/*!
 * @brief 版の文字列を出す行（K-45）。
 * @details `news_j.txt` は行 2 と行 4 が `*****` の帯で、**その間（行 3）は空**。
 * コアの `put_title()`（`src/main/angband-initializer.cpp:110`）がそこへ版を書くので、
 * 帯の中に題が入った形が本来のタイトル画面である。SDL2 のオープニングは
 * `init_angband` より前に出るため、同じことを自分で行う必要がある。
 */
constexpr int kOpeningVersionRow = 3;
//! 操作説明の行（sdl_pick_savefile と同じく、項目のすぐ上に置く）。
constexpr int kOpeningHintRow = kOpeningNewsRows;
//! 選択肢の先頭行。
constexpr int kOpeningTopRow = kOpeningHintRow + 1;
static_assert(kOpeningTopRow + static_cast<int>(OpeningChoice::Count) <= MAIN_TERM_MIN_ROWS,
    "オープニングの選択肢が Term の下端をはみ出している");

/*!
 * @brief news_j.txt / news.txt ＋ 新規／ロード／終了の縦メニューを Term に描画する。
 * @param cursor いま選んでいる項目（OpeningChoice）。その行に `>` を付ける。
 * @details 注意: この TU に日本語リテラルを置かない
 * （UTF-8 バイトが iskanji 誤判定→string_view 終端参照で Debug Assert になる）。
 * **ここで足す文字列も ASCII だけ**。日本語は news ファイル側に任せる。
 */
void draw_opening_screen(int cursor)
{
    term_clear();
    const auto path_news = path_build(ANGBAND_DIR_FILE, _("news_j.txt", "news.txt"));
    auto *fp = angband_fopen(path_news, FileOpenMode::READ);
    if (fp) {
        int i = 0;
        while (true) {
            const auto buf = angband_fgets(fp);
            if (!buf) {
                break;
            }
            if (i >= kOpeningNewsRows) {
                break;
            }
            term_putstr(0, i++, -1, TERM_WHITE, *buf);
        }
        angband_fclose(fp);
    }

    /*
     * K-45: `*****` の帯の間へ版を出す（`put_title()` と同じ位置・同じ中央寄せ）。
     * 文字列はコアが組むので**この TU に日本語リテラルは無い**（上の注意を守っている）。
     */
    const auto title = AngbandSystem::get_instance().build_version_expression(VersionExpression::FULL);
    const auto title_col = (title.length() <= MAIN_TERM_MIN_COLS)
        ? static_cast<int>((MAIN_TERM_MIN_COLS - title.length()) / 2)
        : 0;
    prt(title, kOpeningVersionRow, title_col);

    prt("Up/Down or 8/2  Enter=select  Esc=quit", kOpeningHintRow, 2);
    for (int i = 0; i < static_cast<int>(OpeningChoice::Count); ++i) {
        const auto &entry = kOpeningEntries[i];
        const char mark = (i == cursor) ? '>' : ' ';
        const auto line = format(" %c %c) %s", mark, entry.letter, entry.label);
        c_prt((i == cursor) ? TERM_L_BLUE : TERM_WHITE, line, kOpeningTopRow + i, 2);
    }
    term_fresh();
}

//! 拡張子なしのプレイヤセーブのみ（*.F## フロア一時・delete.me 等は除外）。
std::vector<std::filesystem::path> list_player_savefiles()
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::exists(ANGBAND_DIR_SAVE, ec) || !std::filesystem::is_directory(ANGBAND_DIR_SAVE, ec)) {
        return out;
    }
    for (const auto &entry : std::filesystem::directory_iterator(ANGBAND_DIR_SAVE, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        const auto name = entry.path().filename().string();
        if (name.empty() || name == "delete.me" || name.starts_with("Makefile")) {
            continue;
        }
        /*
         * 補助ファイルを弾く。**「`.` を含むから除外」にしてはいけない。**
         * Linux は `SAVEFILE_USE_UID`（`src/system/h-config.h:60`）でセーブ名が
         * `<uid>.<キャラクター名>`（例 `1000.Shisy`）になるため、その判定だと
         * **プレイヤセーブごと消えてロード一覧に出てこない**（実際にそうなった）。
         * Windows のセーブが拡張子なしなのはたまたまで、拠り所にはできない。
         *
         * 弾くべきものは名指しできる：
         *   .F00〜.F99  … フロア一時   （`src/floor/floor-save.cpp:31`）
         *   .new / .old … 書き込み中の一時（`src/save/save.cpp:299` および 307。
         *                  hoge.new へ書く → hoge を hoge.old へ → hoge.new を hoge へ）
         *   .sdl2panels … サブパネル構成（`presentation/term/sdl_sub_window_terms.cpp:162`）
         */
        const auto ext = entry.path().extension().string();
        const auto is_floor_temp = (ext.size() == 4) && (ext[1] == 'F')
            && (std::isdigit(static_cast<unsigned char>(ext[2])) != 0)
            && (std::isdigit(static_cast<unsigned char>(ext[3])) != 0);
        if (is_floor_temp || (ext == ".new") || (ext == ".old") || (ext == ".sdl2panels")) {
            continue;
        }
        out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end(), [](const std::filesystem::path &a, const std::filesystem::path &b) {
        return a.filename().string() < b.filename().string();
    });
    return out;
}

/*!
 * @brief 一覧で選んだセーブを採用する（`savefile` と `savefile_base` を揃えて設定）。
 *
 * **`savefile` だけ差し替えてはいけない。** プレイ記録の名前は `savefile_base` から
 * 決まる（`src/io/write-diary.cpp:38`）が、コア側でそれが更新されるのは
 * `is_modified || savefile_base.empty()` のときだけ（`src/player/process-name.cpp:108`）。
 * ロード時の呼び出し（`src/core/game-play.cpp:173`）は `is_new_savefile` が false なので
 * `is_modified` が立たず、**空でなければ何もしない**。
 *
 * GDI 版（`src/main-win.cpp`）はロード前に `process_player_name` を呼ばないので
 * `savefile_base` が空のまま `play_game` に入り、そこで正しく埋まる。ところが SDL2 UI は
 * セーブ選択より前に `process_player_name(p_ptr, true)`（`run_sdl_game` の手順 2）を
 * 呼んで既定名で埋めてしまうため、ここで揃えないと**選んだセーブと記録の名前がずれる**
 * （`coon` を開いても `playrecord-PLAYER.txt` に書かれる。Windows・Linux とも実測）。
 *
 * @note `split[1]` を素で使わないこと。`SAVEFILE_USE_UID` は `<uid>.<名前>` を前提に
 * するが、Windows 版が作ったセーブ（`coon` 等）には `.` が無く要素が 1 個しかない。
 * コア側（`process-name.cpp:113`）はこの守りが無い。
 *
 * @note **求め方はコア（オリジナル）の規則をそのまま写している。**守りを足した以外は同じ。
 * その結果、同じセーブでもプレイ記録の名前が環境で分かれる
 * （Linux は `SAVEFILE_USE_UID` があるので `playrecord-Shisy.txt`、
 * Windows・Android は無いので `playrecord-1000.Shisy.txt`）。
 * **これは不揃いではなく、オリジナル通りの挙動。**揃えようとして独自規則を入れないこと。
 */
void adopt_savefile(const std::filesystem::path &path)
{
    savefile = path;
    const auto name = savefile.filename().string();
#ifdef SAVEFILE_USE_UID
    const auto split = str_split(name, '.');
    savefile_base = (split.size() > 1) ? split[1] : split[0];
#else
    savefile_base = name;
#endif
}

/*!
 * @brief セーブ 1 つに**付随する**ファイル（本体は含めない）。
 *
 * @details 消すときに置き去りにしないためのもの。`list_player_savefiles()` が
 * 一覧から弾いている 4 種類と**同じ顔ぶれ**である（あちらが弾く＝これが付随物）。
 *
 *   `.F00`〜`.F99` … フロア一時（`src/floor/floor-save.cpp` の `get_saved_floor_name`。
 *                     綴りは `<セーブ>.F##` なので、本体を消すと迷子になる）
 *   `.new` / `.old` … 書き込み中の残骸
 *   `.sdl2panels`   … 小窓の配置
 *
 * **同じ綴りで始まるだけのファイルを巻き込まない。**`PLAYER` を消すときに
 * `PLAYER2` が消えては困るので、`<名前>` そのものに拡張子が付いたものだけを見る。
 */
std::vector<std::filesystem::path> savefile_companions(const std::filesystem::path &save)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    const auto dir = save.parent_path();
    const auto stem = save.filename().string();
    if (!std::filesystem::is_directory(dir, ec)) {
        return out;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        const auto name = entry.path().filename().string();
        if ((name.size() <= stem.size()) || (name.compare(0, stem.size(), stem) != 0)
            || (name[stem.size()] != '.')) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        const auto is_floor_temp = (ext.size() == 4) && (ext[1] == 'F')
            && (std::isdigit(static_cast<unsigned char>(ext[2])) != 0)
            && (std::isdigit(static_cast<unsigned char>(ext[3])) != 0);
        if (is_floor_temp || (ext == ".new") || (ext == ".old") || (ext == ".sdl2panels")) {
            out.push_back(entry.path());
        }
    }
    return out;
}

/*!
 * @brief セーブとその付随ファイルを消す（2026-08-14 に決めた）。
 * @param[out] err 消せなかったときの理由（ASCII。この TU の約束）。
 * @return 消せたら true。**1 つでも消せなければ false**（半端に消えた状態を黙認しない）。
 *
 * @details 触るのは `lib/save/` の中で**その名前を持つファイルだけ**である。
 * プレイ記録（`lib/user/playrecord-*.txt`）は消さない——あれはセーブではなく
 * 遊んだ記録で、キャラクターを消しても残したい人が居る（消したい人は自分で消せる）。
 */
bool remove_savefile(const std::filesystem::path &save, std::string &err)
{
    std::error_code ec;
    bool ok = true;
    for (const auto &companion : savefile_companions(save)) {
        if (!std::filesystem::remove(companion, ec) || ec) {
            ok = false;
            err = "cannot delete " + companion.filename().string();
        }
    }
    if (!std::filesystem::remove(save, ec) || ec) {
        return (void)(err = "cannot delete " + save.filename().string()), false;
    }
    return ok;
}

/*!
 * @brief 「本当に消すか」を**カーソルで**訊く。
 * @return true = 消す。
 *
 * @details `y/n` で訊かない。**この画面はパッドと指でも通れなければならない**
 * （タイトルの一覧はそう作ってある）ので、文字キーを押せない入口でも答えられる
 * 2 択にする。既定は「消さない」——取り返しがつかない側を既定にしない。
 */
bool confirm_delete_savefile(const std::filesystem::path &save)
{
    int cursor = 0; //!< 0 = Keep（既定）／1 = Delete
    for (;;) {
        term_clear();
        prt("=== Delete Save ===", 1, 2);
        prt(format("This will delete: %s", save.filename().string().c_str()), 3, 2);
        prt("The save and its floor files will be removed. This cannot be undone.", 4, 2);
        /*
         * **印は行の綴りに混ぜて 1 回で書く。**別に `prt` で上書きすると、あちらは
         * 行末まで消してから書くので**選択肢の字が消える**（実際にそうなり、
         * 「> だけが出ている確認画面」になった）。
         */
        c_prt((cursor == 0) ? TERM_L_BLUE : TERM_WHITE,
            format(" %c No, keep it", (cursor == 0) ? '>' : ' '), 6, 2);
        c_prt((cursor == 1) ? TERM_L_RED : TERM_WHITE,
            format(" %c Yes, delete it", (cursor == 1) ? '>' : ' '), 7, 2);
        prt("Up/Down or 8/2  Enter=select  Esc=cancel", 9, 2);
        term_fresh();

        const char key = inkey();
        if ((key == ESCAPE) || (key == 'n') || (key == 'N')) {
            return false;
        }
        if ((key == '8') || (key == 'k') || (key == 'K') || (key == '2') || (key == 'j') || (key == 'J')) {
            cursor = 1 - cursor;
            continue;
        }
        if ((key == '\r') || (key == '\n') || (key == ' ')) {
            return cursor == 1;
        }
        if ((key == 'y') || (key == 'Y')) {
            return true;
        }
    }
}

/*!
 * @brief Term 内セーブ一覧から選択する（OS ファイルダイアログは使わない）。
 * @return 選択して savefile 設定したら true。キャンセル／空なら false。
 */
bool sdl_pick_savefile()
{
    auto files = list_player_savefiles();

    constexpr int kListTop = 3;
    constexpr int kVisible = 16;
    int cursor = 0;
    int scroll = 0;
    /*
     * 削除の待ち（2026-08-14 に決めた「セーブデータを UI から削除出来るように」）。
     *
     * **読み込みは 1 押しのまま**にする（毎回「読む？消す？」と訊かれるのは煩い）。
     * 一覧の末尾に入口を 1 つ置き、そこを選んだときだけ**次に選んだセーブを消す**。
     * 入口を行にしてあるのは、この画面が**パッドと指でも通れる**必要があるため
     * ——文字キーを押せない入口では「D で削除」は押せない。
     */
    bool deleting = false;

    for (;;) {
        if (files.empty()) {
            term_clear();
            prt("=== Load Game ===", 1, 2);
            prt("No save files found in lib/save/", 3, 2);
            prt("[ press any key ]", 5, 2);
            term_fresh();
            (void)inkey();
            return false;
        }
        cursor = (std::max)(0, (std::min)(cursor, static_cast<int>(files.size())));
        if (cursor < scroll) {
            scroll = cursor;
        }
        if (cursor >= scroll + kVisible) {
            scroll = cursor - kVisible + 1;
        }

        term_clear();
        prt(deleting ? "=== Delete Save ===" : "=== Load Game ===", 1, 2);
        prt(deleting ? "Pick the save to delete   Enter=delete  Esc=back"
                     : "Up/Down or 8/2  Enter=load  Esc=cancel",
            2, 2);

        const int n = static_cast<int>(files.size());
        //! 一覧の**末尾に削除の入口**を 1 行だけ足す（消す相手を選んでいる間は出さない）。
        const int rows = deleting ? n : (n + 1);
        for (int row = 0; row < kVisible; ++row) {
            const int idx = scroll + row;
            if (idx >= rows) {
                break;
            }
            if (idx == n) {
                //! 入口。**赤**にするのは、押した先が取り返しのつかない道だからである。
                c_prt((idx == cursor) ? TERM_L_RED : TERM_WHITE,
                    format(" %c    * Delete a save file *", (idx == cursor) ? '>' : ' '),
                    kListTop + row, 2);
                break;
            }
            const auto name = files[static_cast<size_t>(idx)].filename().string();
            const char mark = (idx == cursor) ? '>' : ' ';
            const char letter = (idx < 26) ? static_cast<char>('a' + idx) : ' ';
            const auto line = format(" %c %c) %s", mark, letter, name.c_str());
            const auto colour = (idx == cursor) ? (deleting ? TERM_L_RED : TERM_L_BLUE) : TERM_WHITE;
            c_prt(colour, line, kListTop + row, 2);
        }

        if (scroll > 0) {
            prt("^ more", kListTop - 1, 60);
        }
        if (scroll + kVisible < rows) {
            prt("v more", kListTop + kVisible, 60);
        }

        term_fresh();
        const char key = inkey();

        if (key == ESCAPE || key == 'Q' || key == 'q') {
            if (deleting) {
                deleting = false; //!< 消す待ちをやめるだけ。一覧からは降りない
                continue;
            }
            return false;
        }
        if (key == '\r' || key == '\n' || key == ' ') {
            if (cursor == n) {
                deleting = true; //!< 入口を選んだ。次に選んだセーブを消す
                cursor = 0;
                continue;
            }
            const auto picked = files[static_cast<size_t>(cursor)];
            if (!deleting) {
                adopt_savefile(picked);
                return true;
            }
            deleting = false;
            if (!confirm_delete_savefile(picked)) {
                continue;
            }
            std::string err;
            if (!remove_savefile(picked, err)) {
                //! **消せなかったことを黙らせない**（消えたつもりで一覧から消える、を防ぐ）。
                term_clear();
                prt("=== Delete Save ===", 1, 2);
                prt(err.c_str(), 3, 2);
                prt("[ press any key ]", 5, 2);
                term_fresh();
                (void)inkey();
            }
            files = list_player_savefiles();
            cursor = 0;
            scroll = 0;
            continue;
        }
        if (key == '8' || key == 'k' || key == 'K' || key == '-') {
            if (cursor > 0) {
                --cursor;
            }
            continue;
        }
        if (key == '2' || key == 'j' || key == 'J' || key == '+') {
            if (cursor + 1 < rows) {
                ++cursor;
            }
            continue;
        }
        if (key == '9' || key == '>') {
            cursor = (std::min)(rows - 1, cursor + kVisible);
            continue;
        }
        if (key == '7' || key == '<') {
            cursor = (std::max)(0, cursor - kVisible);
            continue;
        }
        /*
         * 文字キーで直接選ぶ道。**消す待ちの間はカーソルを動かすだけ**にする
         * ——`a` を押した瞬間に消えるのは危ない（決定で改めて訊く）。
         */
        int letter_idx = -1;
        if ((key >= 'a') && (key <= 'z')) {
            letter_idx = key - 'a';
        } else if ((key >= 'A') && (key <= 'Z')) {
            letter_idx = key - 'A';
        }
        if ((letter_idx >= 0) && (letter_idx < n)) {
            if (deleting) {
                cursor = letter_idx;
                continue;
            }
            adopt_savefile(files[static_cast<size_t>(letter_idx)]);
            return true;
        }
    }
}

/*!
 * @brief オープニング表示のうえ新規／ロードを**カーソル移動で**選択する。
 * @return true=新規開始、false=ロード（savefile 設定済み）
 *
 * @details 操作系は同ファイルの `sdl_pick_savefile()` に揃えてある。
 *   - 上下: `8` / `2`（テンキー）と**矢印キー**。矢印は画面側が
 *     `'8'` / `'2'` へ翻訳して `KeyQueue` へ積むので、ここには数字として届く。
 *   - 決定: `Enter`（`'\r'`）／`Space`
 *   - 終了: `ESC`
 *   - **従来の N / L / Q の文字キーもそのまま効く**（押した項目を直接実行する）。
 * **パッドでも動く**: 画面側が十字を `'1'..'9'`、決定を `'\r'`、取消を `0x1B` へ
 * 翻訳して積むため、上の分岐がそのまま噛み合う。
 */
bool sdl_choose_new_or_load()
{
    /*
     * K-47: タイトル画を敷くのは**起動からこの関数を抜けるまで**。
     * 立てる側は既定（`g_title_screen` の初期値）が受け持つ。データ初期化
     * （`init_angband`）はこの関数より前に走り、その間も `データの初期化中...` が
     * 画面に出ているので、そこも同じ絵の上に出したい。ここで初めて立てると
     * 「処理が終わってから絵が出る」ように見える。
     * ここの役目は**抜けるときに必ず降ろす**こと（birth のロール結果の裏へ持ち越さない）。
     * `sdl_pick_savefile()` もここから呼ばれるので、セーブ選択の裏にも同じ絵が残る
     * （オープニングの続きなので意図どおり）。
     */
    struct TitleScreenScope {
        TitleScreenScope() { presentation::set_title_screen(true); }
        ~TitleScreenScope() { presentation::set_title_screen(false); }
    } title_scope;

    // Bridge が Term ミラーを MainMap に出す条件（menu_open）を満たす。
    auto &world = AngbandWorld::get_instance();
    const byte prev_icky = world.character_icky_depth;
    if (world.character_icky_depth == 0) {
        world.character_icky_depth = 1;
    }

    constexpr int kChoiceCount = static_cast<int>(OpeningChoice::Count);
    int cursor = static_cast<int>(OpeningChoice::NewGame);
    bool new_game = true;

    //! 1 項目を実行する。@return true=選択確定（ループを抜ける）／false=選び直し。
    const auto decide = [&](int index) {
        switch (static_cast<OpeningChoice>(index)) {
        case OpeningChoice::NewGame:
            savefile.clear();
            new_game = true;
            return true;
        case OpeningChoice::LoadGame:
            if (sdl_pick_savefile()) {
                new_game = false;
                return true;
            }
            return false; // セーブ選択をキャンセル → オープニングへ戻る
        case OpeningChoice::Quit:
        default:
            world.character_icky_depth = prev_icky;
            quit("");
            return false; // quit() から戻ってきたときの保険（選び直し）
        }
    };

    for (;;) {
        draw_opening_screen(cursor);
        const char key = inkey();

        if (key == ESCAPE) {
            world.character_icky_depth = prev_icky;
            quit("");
            continue;
        }
        if (key == '8' || key == 'k' || key == 'K') {
            cursor = (cursor + kChoiceCount - 1) % kChoiceCount;
            continue;
        }
        if (key == '2' || key == 'j' || key == 'J') {
            cursor = (cursor + 1) % kChoiceCount;
            continue;
        }
        if (key == '\r' || key == '\n' || key == ' ') {
            if (decide(cursor)) {
                break;
            }
            continue;
        }

        // 従来の文字キー（N / L / Q）。カーソルもその項目へ合わせてから実行する。
        int letter_index = -1;
        for (int i = 0; i < kChoiceCount; ++i) {
            const char upper = kOpeningEntries[i].letter;
            const char lower = static_cast<char>(upper - 'A' + 'a');
            if (key == upper || key == lower) {
                letter_index = i;
                break;
            }
        }
        if (letter_index >= 0) {
            cursor = letter_index;
            if (decide(cursor)) {
                break;
            }
        }
    }

    world.character_icky_depth = prev_icky;
    return new_game;
}

/*!
 * @brief `--bot-json-output[=path]`／`HENGBAND_BOT_JSON` を読む（K-49）。
 * @details コアの入口（`platform/windows/core_main.cpp`）は
 * `main-win/commandline-win.cpp` を通らないため、ここで自前に採る。
 * 環境変数はスクリプト（`tools/sdl2_verify`）から入れやすいようにした口で、
 * `1` なら既定のパス、それ以外の文字列は**出力先のパスそのもの**とみなす。
 */
//! K-49: コマンドライン／環境変数で JSON 出力が指定されたか（設定より優先）。
bool s_bot_json_forced = false;

void parse_bot_json_output_args()
{
#ifdef _WIN32
    // `__argc` / `__argv` は MSVC の CRT が用意する口（WinMain には引数が来ないため）。
    // Android・Unix にはそもそもコマンドラインを渡す経路が無いので環境変数だけ見る。
    constexpr std::string_view name = "--bot-json-output";
    for (int i = 1; i < __argc; ++i) {
        const std::string_view opt = __argv[i];
        if (opt == name) {
            s_bot_json_forced = true;
            continue;
        }
        if ((opt.size() > name.size()) && opt.starts_with(name) && (opt[name.size()] == '=')) {
            s_bot_json_forced = true;
            if (const auto path = opt.substr(name.size() + 1); !path.empty()) {
                arg_bot_json_output_path = std::string(path);
            }
        }
    }
#endif

    const char *const env = std::getenv("HENGBAND_BOT_JSON");
    if ((env == nullptr) || (env[0] == '\0') || (env[0] == '0')) {
        return;
    }
    s_bot_json_forced = true;
    if (std::string_view(env) != "1") {
        arg_bot_json_output_path = env;
    }
}

} // namespace

bool bot_json_forced()
{
    return s_bot_json_forced;
}

int run_sdl_game(const std::filesystem::path &lib_path, Bridge &bridge, const UiSeam &seam)
{
    // 0. ボット用 JSON 出力の指定を採る（コアの入力待ちで書き出される）。
    parse_bot_json_output_args();

    // 1. パス初期化（GDI init_windows は呼ばない。init_file_paths は公開 API）。
    init_file_paths(lib_path);
#ifdef TANGBAND
    /*
     * 短愚蛮怒はデータ・記録系だけ tangband/lib/ に分離する（tangband/UPSTREAM.md）。
     * pref・file・help・xtra 等は変愚側 lib/ を共有する。data/ は edit/ から生成される
     * .raw の置き場なので、edit を分けるなら必ず一緒に分けること。
     */
    const auto tang_lib = lib_path.parent_path() / "tangband" / "lib";
    ANGBAND_DIR_EDIT = tang_lib / "edit";
    ANGBAND_DIR_HELP = tang_lib / "help";
    ANGBAND_DIR_DATA = tang_lib / "data";
    ANGBAND_DIR_SAVE = tang_lib / "save";
    ANGBAND_DIR_APEX = tang_lib / "apex";
    ANGBAND_DIR_BONE = tang_lib / "bone";
    ANGBAND_DIR_USER = tang_lib / "user";
#endif

    // 2. プレイヤ名の整形（パス用意。開始選択は後段）。
    process_player_name(p_ptr, true);

    // 3. quit フック（最小）。
    quit_aux = sdl_quit_hook;

    // 4. null term を angband_terms[0] に設置・activate（画素なし）。
    static SdlNullTerm null_term;
    null_term.install(p_ptr, &bridge, seam);

    // 4b. K-26: サブパネル用の画素なし Term を angband_terms[1..5] に設置する。
    //     コアの `window_stuff()` はここに置いた Term へサブウインドウを描くようになる
    //     （どの種類を描くかは `g_window_flags`。Bridge が毎フレーム設定から写す）。
    install_sub_window_terms();

    // 4c. コアの自由文字入力（`askfor`）に入ったことを UI が知れるようにする。
    //     ソフトキーボードしか無い環境（Android）で、名前入力などのときだけ
    //     キーボードを出すための信号。**画面の見た目から推測しない。**
    install_text_input_hook();

    // 5. シグナル初期化（公開 API）。
    signals_init();

    // 6. コア初期化。GDI 版と同じ TermCenteredOffsetSetter を使う。
    //    ※ music.cfg 読込は QuestList/DungeonList を参照するため、必ずこの後で行う
    //      （空 map で rbegin() すると Debug アサート: cannot decrement begin iterator）。
    {
        TermCenteredOffsetSetter tcos(MAIN_TERM_MIN_COLS, MAIN_TERM_MIN_ROWS);
        init_angband(p_ptr, false);
    }

    // 6b. パッドへ割り当てられるコマンド表（`menu_info` の平坦化）とキーマップ逆引き。
    //     **必ず init_angband の後**。逆引きが見る keymap_actions_map は
    //     init_angband 内の process_pref_file("pref.prf") で初めて埋まるため
    //     （src/main/angband-initializer.cpp:238）。先に登録すると「表はあるが
    //     逆引きが常に恒等」という、動いて見えて誤動作する状態になる（B-01 と同型の起動順の罠）。
    register_pad_command_table();

    // 6c. パッド割り当ての自己検査（旧 2D UI が実体を登録していた）。
    //     コマンド表と逆引きが揃ったここでしか回せない（keymap は init_angband 依存）。
    //     未登録なら -1 が返るだけで素通りする。
    if (const int smoke_rc = run_pad_bind_smoke_hook(); smoke_rc >= 0) {
        return smoke_rc;
    }

    // 6d. 効果音／BGM（既存 main-win waveOut/MCI + sound.cfg/music.cfg）。
    sdl_win_audio_init(lib_path);
    set_sdl_ui_options_apply_hook([]() { sdl_win_audio_apply_options(); });

    // 7. オープニング＋新規／ロード選択（GDI の File メニュー待ちに相当）。
    //    以前の「セーブ無し＝即 new_game」は選択画面を飛ばすため廃止。
    const bool new_game = sdl_choose_new_or_load();

    // 8. ゲーム本線。
    play_game(p_ptr, new_game, false);

    sdl_win_audio_shutdown();
    quit("");
    return 0;
}

} // namespace presentation
