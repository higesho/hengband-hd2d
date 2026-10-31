/*
 * NetHack のコアの「専用の画面」（features: nethack-ui）の、コアの側の実装。
 * プロトコルの追補は プロトコル v1 の仕様 §8.7。
 *
 * このファイルは nh_main.cpp の無名名前空間の中に #include して取り込む（nh_main.cpp の送信・待ちの関数と状態を直に使うため。
 * Linux の組み立て表〔platform/linux/CMakeLists.txt〕に .cpp を増やさずに済む）。単独ではコンパイルしない。
 *
 * 画面と申告し合ったとき（hello の features と hello_ack の features の両方に nethack-ui）だけ、次を行う。
 *  - 原作の画面の関数（nh_winport.c が素の値に直して nh_host_ui_* などで渡す）を {"t":"nethack_ui","k":…} の知らせにして送る。
 *  - 要求（menu・text・ask・select・rip）には番号 r を付けて送り、{"t":"nethack_reply","r":…,"a":…} の返事を待つ。
 *    古い要求への返事（r が違う）は捨てる。窓が開いている間に来たキーは捨てる（画面の窓が受ける）。
 *  - --More--（原作の GUI 版 mhmsgwnd.c の more_prompt_check と同じ規則）で、メッセージの窓の行数と桁数（返事の msgwin）を使って止める。
 * 申告が無いときは何も起きない（共通の画面向けの frame だけ）。
 *
 * ゲームを動かすスレッドだけが状態を書く。受信スレッドは返事を列に積むだけ（g_input_mutex で守る）。
 */

using Json = nlohmann::json;

//! features の名前（hello と hello_ack の両方に挙がったときだけ有効）。
constexpr const char *kFeatureUi = "nethack-ui";

//! 画面と nethack-ui を申告し合ったか。
std::atomic<bool> g_ui_active{ false };
//! 画面が知らせたメッセージの窓の見える行数と桁数（返事の msgwin）。知らせが来る前の既定は原作の GUI 版の既定（4 行）と 80 桁。
std::atomic<int> g_msgwin_rows{ 4 };
std::atomic<int> g_msgwin_cols{ 80 };

//! 要求の番号。1 から振る（0 は「番号なし」）。ゲームを動かすスレッドだけが進める。
long g_request_seq = 0;

//! 画面から届いた返事（msgwin・pos 以外）。g_input_mutex で守る。
struct UiReply {
    long r{ 0 };
    Json body;
};
std::deque<UiReply> g_ui_replies;

//! 画面から届いた地図のクリック（pos の返事）。g_input_mutex で守る。
struct UiPos {
    int x{ 0 };
    int y{ 0 };
    int mod{ 1 };
};
std::deque<UiPos> g_ui_pos;

//! 画面から届いた地図のマスの説明の問い合わせ（nethack_query q=hover）。g_input_mutex で守る。最新の 1 件だけ残す。
struct UiHover {
    int x{ 0 };
    int y{ 0 };
    bool pending{ false };
};
UiHover g_ui_hover;

//! 原作の NHW_PERMINVENT（持ち物の常設の窓の種類）。
constexpr int kNhwPermInvent = 6;
//! 墓標の窓の種類（win の type。原作には無い。outrip が text の窓を rip に替える）。
constexpr int kNhwRip = 100;

// nh_main.cpp の関数（この後に定義されている）
void present(bool force);
int handle_shutdown();
int wait_key_impl(int mode, UiPos *pos);
void push_history(const std::string &text);
void trim_incomplete_utf8(std::string &text);

constexpr int BUFSZ_UI_HOVER = 256;

bool ui_on() { return g_ui_active.load(); }

//! nethack_ui の知らせを 1 通送る。壊れた UTF-8 は置き換える（JSON を落とさない）。
bool ui_send(Json message)
{
    message["t"] = "nethack_ui";
    return send_message(message.dump(-1, ' ', false, Json::error_handler_t::replace));
}

//! 画面へ出す字（壊れた UTF-8 と制御文字を直す）。
std::string ui_text(const char *text) { return nethack::clean_utf8(text); }

//! キーを待つ理由（wait の reason）。
const char *ui_wait_reason(int mode)
{
    switch (mode) {
    case NH_KEY_COMMAND: return "command";
    case NH_KEY_POS: return "pos";
    case NH_KEY_MORE: return "more";
    default: return "key";
    }
}

void ui_send_wait(const char *reason, long r, const char *text = nullptr)
{
    Json m;
    m["k"] = "wait";
    m["reason"] = reason;
    if (r != 0) m["r"] = r;
    if (text != nullptr && *text != '\0') m["text"] = text;
    (void)ui_send(std::move(m));
}

//! 地図のマス (x, y) の説明（原作の autodescribe と同じ文）を hover の知らせで送る。ゲームのスレッドの入力待ちの中で呼ぶ。
void ui_answer_hover(int x, int y)
{
    char buf[BUFSZ_UI_HOVER] = {};
    Json m;
    m["k"] = "hover";
    m["x"] = x;
    m["y"] = y;
    m["text"] = (nh_describe_cell(x, y, buf, static_cast<int>(sizeof(buf))) != 0) ? ui_text(buf) : std::string();
    (void)ui_send(std::move(m));
}

// ---- 表示の桁（--More-- の折り返しの数え方）----

//! UTF-8 の 1 文字を読んで、符号位置を返す。i は次の位置へ進む。
unsigned ui_next_cp(const std::string &s, std::size_t &i)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    unsigned cp = c;
    if (c >= 0xF0) {
        len = 4;
        cp = c & 0x07;
    } else if (c >= 0xE0) {
        len = 3;
        cp = c & 0x0F;
    } else if (c >= 0xC0) {
        len = 2;
        cp = c & 0x1F;
    }
    for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    return cp;
}

//! 全角（端末の桁で 2）か。nh_frame.cpp の display_cols と同じ区切り。
bool ui_is_wide(unsigned cp) { return cp >= 0x2E80 && !(cp >= 0xFF61 && cp <= 0xFF9F); }

/*!
 * 文字列を width 桁で折り返したときの行数。原作の GUI 版は DrawText(DT_WORDBREAK) で数えている。
 * 同じにはならない（フォントの幅が違う）ので、近似: 半角の単語は空白で折り返し、全角は 1 字ごとに折り返せる。
 * 長すぎる単語は字ごとに折り返す。行の終わりの空白はぶら下げる（行数を増やさない）。
 */
int ui_wrapped_lines(const std::string &text, int width)
{
    if (width < 1) width = 1;
    int lines = 1;
    int col = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            ++lines;
            col = 0;
            ++i;
            continue;
        }
        if (c == ' ') {
            if (col < width) ++col;
            ++i;
            continue;
        }
        std::size_t probe = i;
        const unsigned first = ui_next_cp(text, probe);
        if (ui_is_wide(first)) {
            if (col + 2 > width && col > 0) {
                ++lines;
                col = 0;
            }
            col += 2;
            i = probe;
            continue;
        }
        // 半角の単語（空白・改行・全角の手前まで）
        int word = 0;
        std::size_t end = i;
        while (end < text.size()) {
            const unsigned char d = static_cast<unsigned char>(text[end]);
            if (d == ' ' || d == '\n') break;
            std::size_t next = end;
            if (ui_is_wide(ui_next_cp(text, next))) break;
            end = next;
            ++word;
        }
        if (col + word <= width) {
            col += word;
        } else if (word <= width) {
            ++lines;
            col = word;
        } else {
            // 1 行に入らない長さの単語: 字ごとに折り返す
            for (int k = 0; k < word; ++k) {
                if (col + 1 > width) {
                    ++lines;
                    col = 0;
                }
                ++col;
            }
        }
        i = end;
    }
    return lines;
}

// ---- メッセージの窓の模型（原作の GUI 版 mhmsgwnd.c の NHMessageWindow と同じ状態）----

//! 原作の GUI 版の window_text[] の最大の長さ（TBUFSZ）。
constexpr std::size_t kMsgMaxText = 300;

struct MsgLine {
    int attr{ 0 };
    std::string text;
};

struct MsgWin {
    //! 最後が一番新しい行。いつも 1 行以上ある。
    std::deque<MsgLine> lines = std::deque<MsgLine>(1);
    int not_seen{ 0 };       //!< lines_not_seen: 前の --More-- か命令の始めから、まだ見せていない行
    int last_turn{ 0 };      //!< lines_last_turn
    bool nevermore{ false }; //!< Esc で以後の --More-- を出さない
    // 日本語版の ATR_NOHISTORY の行（履歴に積まない一時の行）
    bool nohistory{ false };
    MsgLine saved;
    int saved_last_turn{ 0 };
    int saved_not_seen{ 0 };
};
MsgWin g_msg;

const char *ui_more_text() { return kBuildJa ? "--続き--" : "--More--"; }

std::string ui_strip_newline(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return text;
}

void msg_end_nohistory()
{
    if (!g_msg.nohistory) return;
    g_msg.lines.back() = g_msg.saved;
    g_msg.last_turn = g_msg.saved_last_turn;
    g_msg.not_seen = g_msg.saved_not_seen;
    g_msg.nohistory = false;
    g_msg.saved = MsgLine{};
}

void msg_begin_nohistory()
{
    if (g_msg.nohistory) return;
    g_msg.saved = g_msg.lines.back();
    g_msg.saved_last_turn = g_msg.last_turn;
    g_msg.saved_not_seen = g_msg.not_seen;
    g_msg.nohistory = true;
    g_msg.lines.back().text.clear();
}

//! clear_nhwindow(WIN_MESSAGE)（MSNH_MSG_CLEAR_WINDOW）。命令の始めごとに呼ばれる。
void msg_clear()
{
    msg_end_nohistory();
    g_msg.last_turn = 0;
    g_msg.not_seen = 0;
    g_msg.nevermore = false;
}

//! 原作の GUI 版の can_append_text: 最後の行に「  」で続けて 1 行に収まるか。
bool msg_can_append(int attr, const std::string &text)
{
    const MsgLine &last = g_msg.lines.back();
    if (g_msg.not_seen == 0) return false; // 命令の始め
    if (last.attr != attr) return false;
    if (!last.text.empty() && last.text.back() == '\n') return false;
    const std::string more = ui_more_text();
    if (last.text.size() + 2 + text.size() + more.size() >= kMsgMaxText) return false;
    const std::string tmp = ui_strip_newline(last.text + "  " + text) + more;
    const int width = std::max(1, g_msgwin_cols.load() - 2); // 左に 2 桁の余白（LINE_PADDING_LEFT）
    return ui_wrapped_lines(tmp, width) == 1;
}

/*!
 * 原作の GUI 版の more_prompt_check: まだ見せていない行が、窓の見える行数に収まらなければ --More--。
 * 日本語版（NetHackJP）の同じ関数は、折り返した高さを remaining_height から引く行が抜けている
 * （DrawText の戻り値を捨てている）ため、「履歴の行数に達したとき」以外は偽を返す。
 * 日本語版でも英語版と同じ数え方にする（決めたこと 2026-10-28「続きの件は英語と同等に直しとこう」。
 * D18）。原作どおりにすると、日本語版では --続き-- がほとんど出ないため。
 */
constexpr bool kMoreSubtractsHeight = true;

bool msg_more_needed(int msg_lines)
{
    if (g_msg.not_seen == 0) return false;
    if (g_msg.not_seen >= msg_lines) return true; // 履歴の行数に達した: いつも --More--
    const int width = std::max(1, g_msgwin_cols.load() - 2);
    int remaining = std::max(1, g_msgwin_rows.load());
    for (int i = 0; i < g_msg.not_seen; ++i) {
        const int at = static_cast<int>(g_msg.lines.size()) - 1 - i;
        if (at < 0) break;
        std::string text = ui_strip_newline(g_msg.lines[static_cast<std::size_t>(at)].text);
        if (i == 0) text += ui_more_text();
        const int height = ui_wrapped_lines(text, width);
        if (kMoreSubtractsHeight) remaining -= height;
        if (remaining <= 0) break;
    }
    return remaining <= 0;
}

//! --More-- を出して、スペース・Enter・Esc を待つ（Esc は以後のそのターンの --More-- を出さない）。
void msg_do_more()
{
    for (;;) {
        const int key = wait_key_impl(NH_KEY_MORE, nullptr);
        if (key == ' ' || key == '\r' || key == '\n') break;
        if (key == 0x1B) {
            g_msg.nevermore = true;
            break;
        }
        // ほかのキーは捨てて、もう一度待つ
    }
    g_msg.not_seen = 0;
}

/*!
 * putstr(WIN_MESSAGE)。メッセージの窓の模型を更新し、必要なら --More-- で止まってから、putstr の知らせを送る。
 * id が負（raw_print）のときは模型を使わず送るだけ。
 */
void ui_message_putstr(int id, int attr, const std::string &text, int msg_lines)
{
    if (id >= 0) {
        const bool nohistory = (attr & 32) != 0; // ATR_NOHISTORY
        if (!nohistory) msg_end_nohistory();
        if (nohistory) {
            msg_begin_nohistory();
            g_msg.lines.back().attr = attr & ~(32 | 16);
            g_msg.lines.back().text = text.substr(0, kMsgMaxText);
        } else if (msg_can_append(attr, text)) {
            g_msg.lines.back().text += "  " + text;
            if (g_msg.lines.back().text.size() > kMsgMaxText) g_msg.lines.back().text.resize(kMsgMaxText);
        } else {
            if (!g_msg.nevermore && msg_more_needed(msg_lines)) msg_do_more();
            // 最後の行が空でなければ、1 行ずらして新しい行にする
            const std::string &prev = g_msg.lines.back().text;
            const bool blank = prev.find_first_not_of(" \t\r\n") == std::string::npos;
            if (!blank) {
                g_msg.lines.emplace_back();
                while (static_cast<int>(g_msg.lines.size()) > std::max(msg_lines, 1)) g_msg.lines.pop_front();
            }
            g_msg.lines.back().attr = attr;
            g_msg.lines.back().text = text.substr(0, kMsgMaxText);
            ++g_msg.not_seen;
            ++g_msg.last_turn;
        }
    }
    Json m;
    m["k"] = "putstr";
    m["id"] = id;
    const int shown_attr = attr & ~32;
    if (shown_attr != 0) m["attr"] = shown_attr;
    if ((attr & 32) != 0) m["nohistory"] = true;
    m["text"] = text;
    (void)ui_send(std::move(m));
}

// ---- 返事を待つ ----

/*!
 * 要求 r の返事を待つ。true: 来た（body に入る）。false: 画面が居なくなった・終了を求められた（原作の hangup へ入った）。
 * 古い要求への返事は捨てる。待っている間に届いたキーは捨てる（窓が開いている間のキーは画面の窓が受ける）。
 */
bool ui_wait_reply(long r, Json &body)
{
    std::unique_lock<std::mutex> lock(g_input_mutex);
    for (;;) {
        while (!g_ui_replies.empty()) {
            UiReply reply = std::move(g_ui_replies.front());
            g_ui_replies.pop_front();
            if (reply.r == r) {
                body = std::move(reply.body);
                return true;
            }
        }
        g_keys.clear();
        g_ui_pos.clear();
        if (g_hangup_called.load()) {
            lock.unlock();
            (void)handle_shutdown();
            return false;
        }
        if (g_ui_gone.load() || g_quit_requested.load()) {
            lock.unlock();
            (void)handle_shutdown();
            return false;
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

/*!
 * 要求を送って返事を待つ。送る前に frame を送り（地図を最新にする）、送った後に wait を送る。
 * 戻り値と body は ui_wait_reply と同じ。r は呼び出し側が ui_new_request() で取った番号で、message["r"] に入れておくこと。
 */
long ui_new_request() { return ++g_request_seq; }

bool ui_request(Json message, const char *reason, long r, Json &body)
{
    g_view.key_mode = NH_KEY_OTHER;
    g_moves_ok.store(false);
    if (g_hangup_called.load()) {
        (void)handle_shutdown();
        return false;
    }
    present(false);
    (void)ui_send(std::move(message));
    ui_send_wait(reason, r);
    return ui_wait_reply(r, body);
}

// ---- メニュー ----

//! 項目の字（selector）を決める。字が無い選べる項目には a〜z A〜Z を順に振る（使われている字は飛ばす）。
std::vector<int> ui_assign_selectors(const nh_menu_item *items, int count, int how)
{
    std::vector<int> sel(static_cast<std::size_t>(count), 0);
    bool used[256] = {};
    for (int i = 0; i < count; ++i) {
        sel[static_cast<std::size_t>(i)] = static_cast<unsigned char>(items[i].selector);
        if (items[i].selectable != 0 && how != NH_PICK_NONE && sel[static_cast<std::size_t>(i)] != 0) used[sel[static_cast<std::size_t>(i)]] = true;
    }
    static const char kLetters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::size_t next = 0;
    for (int i = 0; i < count; ++i) {
        if (items[i].selectable == 0 || how == NH_PICK_NONE || sel[static_cast<std::size_t>(i)] != 0) continue;
        while (kLetters[next] != '\0' && used[static_cast<unsigned char>(kLetters[next])]) ++next;
        if (kLetters[next] == '\0') break;
        sel[static_cast<std::size_t>(i)] = static_cast<unsigned char>(kLetters[next]);
        used[sel[static_cast<std::size_t>(i)]] = true;
    }
    return sel;
}

std::string ui_char_string(int ch)
{
    if (ch <= 0 || ch > 255) return std::string();
    return std::string(1, static_cast<char>(ch));
}

Json ui_item_json(const nh_menu_item &item, long index, int selector)
{
    Json j;
    const bool selectable = item.selectable != 0;
    j["i"] = selectable ? index + 1 : 0; // 0 は見出し・説明の行
    if (selector != 0 && selectable) j["ch"] = ui_char_string(selector); // 見るだけの窓でも、原作が付けた字（持ち物の字）は送る
    if (item.group != 0) j["gch"] = ui_char_string(static_cast<unsigned char>(item.group));
    if (item.attr != 0) j["attr"] = item.attr;
    j["clr"] = item.color;
    if (selectable && item.preselected != 0) j["sel"] = true;
    j["tile"] = -1; // 絵の一覧は別の担当が作る。作られるまでは -1
    j["glyph"] = item.glyph;
    j["text"] = ui_text(item.text);
    return j;
}

/*!
 * メニュー（start_menu〜select_menu）。menu の知らせを送り、返事を待つ。
 * 返事: menu（sel: [{i, n}]。i は項目の番号〔1 から〕、n は個数で -1 は全部）・dismiss（見るだけの窓を閉じた）・cancel。
 * 戻り値は nh_host_menu と同じ（選ばれた数。取り消しは -1）。
 */
int ui_menu(int win_id, int win_type, int how, const char *prompt, const nh_menu_item *items, int count, int *chosen, long *counts)
{
    const std::vector<int> selectors = ui_assign_selectors(items, count, how);
    Json m;
    m["k"] = "menu";
    m["id"] = win_id;
    m["prompt"] = ui_text(prompt);
    m["how"] = how;
    Json list = Json::array();
    for (int i = 0; i < count; ++i) list.push_back(ui_item_json(items[i], i, selectors[static_cast<std::size_t>(i)]));
    m["items"] = std::move(list);
    if (win_type == kNhwPermInvent) {
        // 持ち物の常設の窓（返事は要らない。r は 0）
        (void)ui_send(std::move(m));
        return 0;
    }
    const long r = ui_new_request();
    m["r"] = r;
    Json reply;
    if (!ui_request(std::move(m), "menu", r, reply)) return -1;
    std::string a;
    try {
        a = reply.value("a", std::string());
    } catch (...) {
    }
    if (a == "cancel") return how == NH_PICK_NONE ? 0 : -1;
    if (how == NH_PICK_NONE || a != "menu") return 0;
    int n = 0;
    std::vector<bool> taken(static_cast<std::size_t>(count), false);
    try {
        const Json &sel = reply["sel"];
        if (sel.is_array()) {
            for (const Json &entry : sel) {
                if (!entry.is_object()) continue;
                const long i = entry.value("i", 0L);
                const long index = i - 1;
                if (index < 0 || index >= count || items[index].selectable == 0 || taken[static_cast<std::size_t>(index)]) continue;
                taken[static_cast<std::size_t>(index)] = true;
                if (chosen != nullptr) chosen[n] = static_cast<int>(index);
                if (counts != nullptr) {
                    const long c = entry.value("n", -1L);
                    counts[n] = c > 0 ? c : -1;
                }
                ++n;
                if (how == NH_PICK_ONE) break;
            }
        }
    } catch (...) {
    }
    return n;
}

//! text（テキストの窓）か rip（墓標）。閉じられるまで待つ。lines は nh_menu_item の text と attr を使う。
void ui_text_window(const char *kind, int win_id, const nh_menu_item *lines, int count, const Json *rip)
{
    Json m;
    m["k"] = kind;
    m["id"] = win_id;
    if (rip != nullptr) {
        for (auto it = rip->begin(); it != rip->end(); ++it) m[it.key()] = it.value();
    }
    Json list = Json::array();
    for (int i = 0; i < count; ++i) {
        Json line;
        if (lines[i].attr != 0) line["attr"] = lines[i].attr;
        line["text"] = ui_text(lines[i].text);
        list.push_back(std::move(line));
    }
    m["lines"] = std::move(list);
    const long r = ui_new_request();
    m["r"] = r;
    Json reply;
    (void)ui_request(std::move(m), "menu", r, reply);
    // 返事は dismiss か cancel のどちらでも閉じた扱い
}

// ---- yn ----

/*!
 * yn_function の押されたキーから答えを決める（原作の tty と同じ規則。キーの経路の nh_host_yn と同じ）。
 * true: 決まった（result に入る）。false: 選べないキー（もう一度待つ）。
 */
bool ui_resolve_yn(int key, const char *choices, char def, char &result)
{
    const bool has_choices = choices != nullptr && *choices != '\0';
    if (!has_choices) {
        result = static_cast<char>(key); // 何でも受ける（原作の tty も同じ）
        return true;
    }
    if (key == 0x1B) {
        if (std::strchr(choices, 'q') != nullptr) result = 'q';
        else if (std::strchr(choices, 'n') != nullptr) result = 'n';
        else result = def;
        return true;
    }
    if (key == ' ' || key == '\r' || key == '\n') {
        result = def;
        return true;
    }
    const int lower = (key >= 'A' && key <= 'Z') ? key - 'A' + 'a' : key;
    const int upper = (key >= 'a' && key <= 'z') ? key - 'a' + 'A' : key;
    if (key > 0 && std::strchr(choices, key) != nullptr) {
        result = static_cast<char>(key);
        return true;
    }
    if (lower > 0 && std::strchr(choices, lower) != nullptr) {
        result = static_cast<char>(lower);
        return true;
    }
    if (upper > 0 && std::strchr(choices, upper) != nullptr) {
        result = static_cast<char>(upper);
        return true;
    }
    return false;
}

char ui_yn(const char *query, const char *choices, char def)
{
    // 原作の GUI 版は問いをメッセージの窓に出し、その前にメッセージの窓を消す（clear_nhwindow）。見せ済みの扱いにする
    msg_clear();
    const bool has_choices = choices != nullptr && *choices != '\0';
    const std::string shown = ui_text(query);
    Json m;
    m["k"] = "ask";
    m["kind"] = "yn";
    m["query"] = shown;
    if (has_choices) {
        std::string visible = choices;
        const std::size_t esc = visible.find('\033');
        if (esc != std::string::npos) visible.resize(esc); // ESC より後ろは表示しない（原作の GUI 版と同じ）
        m["choices"] = ui_text(visible.c_str());
    }
    if (def != '\0') m["def"] = std::string(1, def);
    char result = def;
    const long r = ui_new_request();
    m["r"] = r;
    Json reply;
    bool got = ui_request(std::move(m), "ask", r, reply);
    while (got) {
        std::string a;
        int key = 0x1B;
        try {
            a = reply.value("a", std::string());
            if (a == "yn") key = reply.value("key", 0x1B);
        } catch (...) {
        }
        // yn と cancel 以外の返事、選べないキーは捨てて、同じ要求への返事をもう一度待つ（新しい要求は出さない）
        if ((a == "yn" || a == "cancel") && ui_resolve_yn(key, choices, def, result)) break;
        got = ui_wait_reply(r, reply);
    }
    if (!got) result = def;
    g_msg.not_seen = 0; // 問いが済んだ（MSNH_MSG_CARET を消すと lines_not_seen が 0 になる）
    if (g_view.phase == NH_PHASE_GAME && result >= 0x21 && result < 0x7F) push_history(shown + " " + result);
    return result;
}

// ---- 文字入力 ----

int ui_ask_text(int kind, const char *query, char *buf, int bufsize, const char *const *cmds, int ncmds)
{
    const char *kind_name = kind == NH_ASK_NAME ? "name" : (kind == NH_ASK_EXT_CMD ? "ext_cmd" : "getlin");
    Json m;
    m["k"] = "ask";
    m["kind"] = kind_name;
    m["query"] = ui_text(query);
    if (kind == NH_ASK_EXT_CMD && cmds != nullptr) {
        Json list = Json::array();
        for (int i = 0; i < ncmds; ++i) list.push_back(ui_text(cmds[i]));
        m["cmds"] = std::move(list);
    }
    const long r = ui_new_request();
    m["r"] = r;
    Json reply;
    if (!ui_request(std::move(m), "ask", r, reply)) return 0;
    std::string a;
    std::string text;
    try {
        a = reply.value("a", std::string());
        if (a == "text") text = reply.value("text", std::string());
    } catch (...) {
        a.clear();
    }
    if (a != "text") return 0; // cancel など
    // 改行などの制御文字は取り除く。UTF-8 が壊れていれば直す
    std::string clean = nethack::clean_utf8(text.c_str());
    const std::size_t limit = bufsize > 1 ? static_cast<std::size_t>(bufsize - 1) : 0;
    if (clean.size() > limit) clean.resize(limit);
    trim_incomplete_utf8(clean);
    if (buf != nullptr && bufsize > 0) std::snprintf(buf, static_cast<std::size_t>(bufsize), "%s", clean.c_str());
    if (kind != NH_ASK_NAME && g_view.phase == NH_PHASE_GAME) push_history(ui_text(query) + " " + clean);
    return 1;
}

// ---- キャラクタ作成 ----

int ui_select(const nh_select_table *t, int result[4])
{
    Json m;
    m["k"] = "select";
    Json roles = Json::array();
    for (int i = 0; i < t->nroles; ++i) {
        Json j;
        j["i"] = i;
        j["name"] = ui_text(t->role_name[i]);
        if (t->role_name_f[i] != nullptr) j["name_f"] = ui_text(t->role_name_f[i]);
        roles.push_back(std::move(j));
    }
    m["roles"] = std::move(roles);
    Json races = Json::array();
    for (int i = 0; i < t->nraces; ++i) {
        Json j;
        j["i"] = i;
        j["name"] = ui_text(t->race_name[i]);
        j["adj"] = ui_text(t->race_adj[i]);
        races.push_back(std::move(j));
    }
    m["races"] = std::move(races);
    Json genders = Json::array();
    for (int i = 0; i < t->ngenders; ++i) {
        Json j;
        j["i"] = i;
        j["name"] = ui_text(t->gender_name[i]);
        genders.push_back(std::move(j));
    }
    m["genders"] = std::move(genders);
    Json aligns = Json::array();
    for (int i = 0; i < t->naligns; ++i) {
        Json j;
        j["i"] = i;
        j["name"] = ui_text(t->align_name[i]);
        aligns.push_back(std::move(j));
    }
    m["aligns"] = std::move(aligns);
    Json ok = Json::array();
    for (int i = 0; i < t->ncombos; ++i) {
        ok.push_back(Json::array({ t->combos[i][0], t->combos[i][1], t->combos[i][2], t->combos[i][3] }));
    }
    m["ok"] = std::move(ok);
    m["name"] = ui_text(t->name);
    Json pick;
    pick["role"] = t->pick[0];
    pick["race"] = t->pick[1];
    pick["gender"] = t->pick[2];
    pick["align"] = t->pick[3];
    m["pick"] = std::move(pick);

    const long r = ui_new_request();
    m["r"] = r;
    Json reply;
    if (!ui_request(std::move(m), "ask", r, reply)) return 0;
    std::string a;
    try {
        a = reply.value("a", std::string());
    } catch (...) {
    }
    result[0] = result[1] = result[2] = result[3] = -1;
    if (a == "quit" || a == "cancel") return 0;
    if (a == "select") {
        try {
            result[0] = reply.value("role", -1);
            result[1] = reply.value("race", -1);
            result[2] = reply.value("gender", -1);
            result[3] = reply.value("align", -1);
        } catch (...) {
        }
        return 1;
    }
    // random（おまかせ）か、読めない返事: 全部おまかせ
    return 1;
}

// ---- ステータス ----

struct UiStatusField {
    std::string text;
    int color{ 8 };
    int attr{ 1 };
    int pct{ -1 };
    bool set{ false };
    bool dirty{ false };
};
UiStatusField g_ui_status[NH_BL_MAX];
struct UiCond {
    std::string name;
    int color{ 8 };
    int attr{ 1 };
};
std::vector<UiCond> g_ui_conds;
bool g_ui_conds_set = false;
bool g_ui_conds_dirty = false;
