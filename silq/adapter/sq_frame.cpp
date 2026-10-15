/*!
 * @file sq_frame.cpp
 * @brief `sq_frame.h` の実装。**P2 の範囲**（Term ミラー・HUD・メッセージ）。
 *
 * ## 文字コード（M1 で変わった）
 * M0 では Sil-Q が純 ASCII だったので変換が 1 か所も無かったが、日本語層を
 * 乗せた M1 からは **Term のセルが CP932（SJIS）**になる。境界はこの TU の
 * **3 か所だけ**である（設計 §3.2 の表）:
 *
 * | 場所 | すること |
 * |---|---|
 * | `row_to_text()` | 先行バイトを次のセルと結合して `sjis_to_utf8()` |
 * | `span_to_line()` | 同上。**区画の端で割れたら空白に落とす**（半分だけ運ばない） |
 * | `row_to_runs()` | 同上 |
 *
 * 手本は `gensoband/adapter/gb_frame.cpp:90-106 / :258-285 / :328-359`。
 *
 * **純 ASCII の行は 1 バイトも触らずに通す**（設計 §1 制約 1）。塊に 0x80 以上が
 * 1 つも無ければ変換器を呼ばない——`--lang` 無しのときに M0 と同じ画を出すための
 * 決めであって、速さのための細工ではない。
 *
 * 制御文字は従来どおり空白に潰す——Term には `0x7F` や `0x00` が残る区画があり、
 * そのまま JSON に載せると読み手が驚く。
 *
 * ## 色 span（設計 §4 の term_mirror 行）
 * 1 行 1 色に落とすと**1 行に何色も出る画面が潰れる**（変愚で実際に潰れた。
 * `game_frame.h` の `TermColorSpan` の註記）。属性の変わり目ごとに区間を切り、
 * 行全体を隙間なく覆う。色を 16 に畳むのは `sq_term_row()` の仕事（設計 §4.1）。
 */

#include "sq_frame.h"
#include "bridge/player_identity.h"
#include "bridge/native_targeting_runtime.h"

#include "sq_lang_c.h" //!< `sq_tr`（HUD の名札。SH-08）
#include "sq_manifest.h"
#include "sq_menu.h"
#include "sq_shim.h"
#include "sq_sub_panels.h"
#include "sq_text.h"

#include "frame/cell_feature_bits.h"
#include "bridge/named_attack_fx_glue.h"
#include "bridge/modern_content.h"
#include "bridge/native_save_picker.h"
#include "portable/legacy_os.h"
#include <memory>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::unique_ptr<presentation::NativeSavePicker> sq_picker;

std::string silq_identity_display(const std::string &save_name,const std::string &fallback)
{
    std::string shown=presentation::player_identity_load("silq",save_name,{});
    if(!shown.empty())return shown;
    const auto dot=save_name.find('.');
    if(dot!=std::string::npos && dot+1<save_name.size()) {
        shown=presentation::player_identity_load("silq",save_name.substr(dot+1),{});
        if(!shown.empty())return shown;
    }
    return fallback;
}

void silq_identity_remove(const std::string &save_name)
{
    presentation::player_identity_remove("silq",save_name);
    const auto dot=save_name.find('.');
    if(dot!=std::string::npos && dot+1<save_name.size())
        presentation::player_identity_remove("silq",save_name.substr(dot+1));
}
}

extern "C" int sq_player_identity_display_name(const char *save_name,char *out,int cap)
{
    if(save_name==nullptr||out==nullptr||cap<=0)return 0;
    const std::string shown=silq_identity_display(save_name,save_name);
    const std::string encoded=sq::utf8_to_sjis(shown);
    const std::string &value=encoded.empty()?shown:encoded;
    if(value.empty())return 0;
    std::snprintf(out,static_cast<size_t>(cap),"%s",value.c_str());
    return 1;
}

extern "C" void sq_player_identity_remove_name(const char *save_name)
{
    if(save_name!=nullptr&&*save_name!='\0')silq_identity_remove(save_name);
}

/*
 * Fix 57 (play test 1): the core keeps the real (CP932) player name now.  The savefile name must stay ASCII and unique,
 * so a name with a 0x80+ byte gets "P" + 7 letters/digits from the hash of its UTF-8 form - the same alias that the
 * older side-store (player_identity.h) used as the core name.  A plain ASCII name returns 0 and keeps the original rules.
 */
extern "C" int sq_save_base_name(const char *full_name,char *out,int cap)
{
    if(full_name==nullptr||out==nullptr||cap<=1)return 0;
    bool wide=false;
    for(const unsigned char *q=reinterpret_cast<const unsigned char*>(full_name);*q;++q)if(*q>=0x80){wide=true;break;}
    if(!wide)return 0;
    const std::string alias=presentation::player_identity_core_name(sq::sjis_to_utf8(full_name),8);
    if(alias.empty())return 0;
    std::snprintf(out,static_cast<size_t>(cap),"%s",alias.c_str());
    return 1;
}

extern "C" void sq_save_picker_begin(const char *directory)
{
    const std::string root=directory;
    sq_picker=std::make_unique<presentation::NativeSavePicker>("Sil-Q",sq_lang_enabled()!=0,
        [root](){
            std::vector<std::string> names;
            for(const auto &name:portable::list_files(root)) {
                if(name.empty()||name.front()=='.')continue;
                std::string lower=name;for(char &c:lower)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
                if(lower=="last_slot.txt")continue;
                const auto ext=std::filesystem::path(name).extension().string();
                if(ext==".new"||ext==".old"||ext==".lok"||ext==".sdl2panels"||(ext.size()==4&&ext[1]=='F'&&std::isdigit(static_cast<unsigned char>(ext[2]))&&std::isdigit(static_cast<unsigned char>(ext[3]))))continue;
                names.push_back(name);
            }
            std::sort(names.begin(),names.end());return names;
        },[root](const std::string &name){
            const std::string result=presentation::remove_native_save(root,name);
            if(result.empty())silq_identity_remove(name);
            return result;
        },
        [](const std::string &name){
            return silq_identity_display(name,sq::sjis_to_utf8(name));
        });
    sq_picker->show_load_only();
}
extern "C" void sq_save_picker_draw(void)
{
    if(!sq_picker)return;
    const auto page=sq_picker->page();
    sq_save_picker_clear(sq::utf8_to_sjis(page.title).c_str(),sq::utf8_to_sjis(page.description).c_str());
    for(size_t i=0;i<page.rows.size();++i){const auto &row=page.rows[i];sq_save_picker_row(int(i),row.key,sq::utf8_to_sjis(row.label).c_str(),row.selected?1:0);}
}
extern "C" int sq_save_picker_key(int key,char *selected,int capacity)
{
    if(!sq_picker)return -1;
    const int result=sq_picker->key(key);
    if(result==0&&capacity>0)std::snprintf(selected,capacity,"%s",sq_picker->selected().c_str());
    return result;
}
extern "C" void sq_save_picker_end(void){sq_picker.reset();}

namespace sq {

namespace {

/* ============================================================ 画面側からの申告 */

//! 視界の既定。`ui_state` が来るまでのぶん（HD2D は必ず送ってくる）。P3 で効く。
int g_view_w = 47;
int g_view_h = 22;
bool g_camera_follow_player = false;
//! 視界の上限。桁違いの値が来ても走査量が爆発しないようにする。
constexpr int kMaxView = 200;
const TileManifest *g_manifest = nullptr;

//! 主 Term の最大寸法。`sq_term_row` の受け皿の大きさを決めるだけの上限。
constexpr int kMaxCols = 256;
//! 画面側へ運ぶメッセージの本数。変愚の `presentation_bridge.cpp` と同じ 8。
constexpr int kMessageLines = 8;
//! 1 本のメッセージの上限。
constexpr int kMessageBytes = 1024;

/*!
 * @brief セル 1 個ぶんを空白とみなすか。**定義は下（状態列の節）にある**。
 * @details M1 で `row_to_text()` からも使うようになったので、ここへ前触れを置く。
 * 判じ方を 2 か所に分けると、ミラーと状態列で字が食い違う。
 */
bool cell_is_blank(char c);

/*!
 * @brief コアから受け取った素の文字列（CP932）を UTF-8 にする。
 * @details Term のセルを通らずに `GameFrame` へ渡る文字列——メッセージ・
 * プレイヤ名・現在地——のための道。**0x80 以上が 1 バイトも無ければ変換器を
 * 呼ばない**（設計 §1 制約 1。`--lang` 無しの画を M0 と 1 バイトも変えない）。
 *
 * @note 設計 §3.2 は境界を「3 か所」と書いていたが、実装してみると
 * **Term を通らない文字列が 3 本ある**（`sq_message_text` / `sq_hud_data.name` /
 * `depth_text` と `place_name`）。ここを落とすとメッセージだけ化ける。
 * 設計 §3.2 に追記済み。
 *
 * @note **符号化だけでは足りない（2026-08-21）。** Term を通らないということは
 * **訳のフック（`Term_addstr`）も通らない**ということで、`msg_print("英文")` の
 * 文と `"Surface"` が英語のまま出ていた。訳は `sq_shim.c` の側（`sq_message_text()` と
 * `depth_text` / `place_name`）で当てている——ここは UTF-8 にするだけの道である。
 */
std::string to_utf8(const char *bytes, std::size_t len)
{
    if ((bytes == nullptr) || (len == 0)) {
        return std::string();
    }
    bool wide = false;
    for (std::size_t i = 0; i < len; ++i) {
        if (static_cast<unsigned char>(bytes[i]) >= 0x80) {
            wide = true;
            break;
        }
    }
    if (!wide) {
        return std::string(bytes, len);
    }
    std::string out = sjis_to_utf8(bytes, len);
    if (out.empty()) {
        out.assign(bytes, len); // 変換できない。**落とすよりは化けたまま運ぶ**
    }
    return out;
}

inline std::string to_utf8(const char *z) { return to_utf8(z, (z != nullptr) ? std::strlen(z) : 0); }

/*!
 * @brief セル 1 行（CP932 のバイト＋属性）→ 1 行の UTF-8 ＋色 span。
 * @param cells 文字のバイト列（CP932）
 * @param attrs 属性（**16 に畳んだ後**。`sq_term_row`）
 * @param count セル数
 * @param out_spans 色 span（**UTF-8 のバイト位置**。行全体を隙間なく覆う）
 * @return 行の文字列（UTF-8）
 *
 * @details 設計 §3.2 の 1 か所目。**色 span はバイト位置で持っている**ので、
 * 2 セルが 1 文字（さらに UTF-8 では 3 バイト）になると境界が動く。
 * 塊ごとに変換して、変換後の長さで span を積み直す。
 *
 * 空白の扱い（`cell_is_blank` と同じ規則）は M0 のまま——**制御文字は空白**、
 * ただし **0x80 以上は空白ではない**（M1 でここが変わった）。
 */
std::string row_to_text(const char *cells, const unsigned char *attrs, int count,
    std::vector<TermColorSpan> &out_spans)
{
    out_spans.clear();

    /*
     * 末尾の空白は落とす。**色ごと落とす**ので span も短くなる。
     * 落とさないと、どの行も 80 桁ぶんの空白と span を運ぶことになり、
     * 27 行 × 毎フレームで効いてくる（v1 §10 の「小さく保つ」）。
     *
     * 右から見ても 2 バイト文字を切らない: CP932 の後続バイトは 0x40〜0x7E と
     * 0x80〜0xFC で、**空白（0x20）にも制御文字にもならない**。
     */
    int end = count;
    while ((end > 0) && cell_is_blank(cells[end - 1])) {
        --end;
    }
    if (end <= 0) {
        return std::string();
    }

    std::string text;
    int x = 0;
    while (x < end) {
        const unsigned char attr = attrs[x];

        /* 同じ属性が続くあいだを 1 塊にする。2 バイト文字は 2 セルまとめて取る。 */
        std::string chunk;
        bool wide = false;
        while (x < end) {
            if (attrs[x] != attr) {
                break;
            }
            const auto lead = static_cast<unsigned char>(cells[x]);
            if (is_sjis_lead(lead) && ((x + 1) < end)) {
                /*
                 * 2 セル 1 文字。**属性は先行バイト側を採る**——後続バイトの
                 * セルにも同じ属性が入っているのが普通だが、違っていても
                 * 文字を割ってはいけない。
                 */
                chunk.push_back(cells[x]);
                chunk.push_back(cells[x + 1]);
                x += 2;
                wide = true;
                continue;
            }
            if (is_sjis_lead(lead)) {
                chunk.push_back(' '); // 行の端で割れた。半端なバイトは運ばない
                x += 1;
                continue;
            }
            chunk.push_back(cell_is_blank(cells[x]) ? ' ' : static_cast<char>(lead));
            x += 1;
        }

        /*
         * **純 ASCII の塊は変換器に掛けない**（設計 §1 制約 1）。
         * `--lang` 無しの画を M0 と 1 バイトも変えないための決めである。
         */
        std::string piece = chunk;
        if (wide) {
            piece = sjis_to_utf8(chunk);
            if (piece.empty() && !chunk.empty()) {
                /* 変換できない塊（壊れた符号）。**行を落とさず**空白で埋める。 */
                piece.assign(chunk.size(), ' ');
            }
        }

        TermColorSpan span;
        span.begin = static_cast<int>(text.size());
        span.len = static_cast<int>(piece.size());
        span.color = attr;
        if (span.len > 0) {
            out_spans.push_back(span);
        }
        text += piece;
    }

    return text;
}

void fill_term_mirror(GameFrame &frame)
{
    int cols = 0;
    int rows = 0;
    if (sq_term_size(&cols, &rows) != SQ_OK) {
        return;
    }
    cols = (std::min)(cols, kMaxCols - 1);

    char cells[kMaxCols];
    unsigned char attrs[kMaxCols];

    frame.menu_term_lines.clear();
    frame.menu_term_lines.reserve(static_cast<std::size_t>(rows));

    for (int y = 0; y < rows; ++y) {
        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        const int n = sq_term_row(y, cells, attrs, kMaxCols);
        if (n < 0) {
            continue;
        }

        TermMirrorLine line;
        line.source_row = y;
        line.text_utf8 = row_to_text(cells, attrs, n, line.color_spans);
        /* `color` は `color_spans` が空のときだけ使われる。 */
        line.color = line.color_spans.empty() ? 1 : line.color_spans.front().color;
        frame.menu_term_lines.push_back(std::move(line));
    }

    /*
     * カーソル。**`menu_term_curs_row` は `menu_term_lines` の添字**であって
     * Term の行番号ではない。ここは空行も捨てずに全行を積んでいるので、
     * 両者は一致する。捨てるようにしたら**必ずここも直す**。
     */
    int cx = 0;
    int cy = 0;
    int visible = 0;
    if ((sq_term_cursor(&cx, &cy, &visible) == SQ_OK) && visible && (cy >= 0)
        && (cy < static_cast<int>(frame.menu_term_lines.size()))) {
        frame.menu_term_curs_col = cx;
        frame.menu_term_curs_row = cy;
    } else {
        frame.menu_term_curs_col = -1;
        frame.menu_term_curs_row = -1;
    }
}

void fill_hud(GameFrame &frame)
{
    sq_hud_data data{};
    if (sq_read_hud(&data) != SQ_OK) {
        return;
    }

    const std::string fallback_name = to_utf8(data.name);
    char save_id[256]{};
    (void)sq_current_save_id(save_id, static_cast<int>(sizeof(save_id)));
    /*
     * Fix 57: the core holds the real name (japanese included), so the HUD simply shows it.  The side file kept beside the
     * save is only for the save picker, which cannot read the savefile: keep it equal to the name (rewritten when the name
     * changes).  Characters made by the older side-store have an ASCII alias as the core name; for them the side file is
     * still the display name, but only while the core name is that alias.
     */
    const bool wide_name = std::any_of(fallback_name.begin(), fallback_name.end(), [](unsigned char c) { return c >= 0x80; });
    if (wide_name) {
        frame.hud.name = fallback_name;
        static std::string kept_id, kept_name;
        if (save_id[0] != 0 && (kept_id != save_id || kept_name != fallback_name)) {
            if (presentation::player_identity_load("silq", save_id, {}) != fallback_name)
                (void)presentation::player_identity_store("silq", save_id, fallback_name);
            kept_id = save_id;
            kept_name = fallback_name;
        }
    } else if (save_id[0] != 0) {
        const std::string kept = silq_identity_display(save_id, std::string());
        frame.hud.name = (!kept.empty() && presentation::player_identity_core_name(kept, 8) == fallback_name) ? kept : fallback_name;
    } else {
        frame.hud.name = fallback_name;
    }
    frame.hud.hp = data.hp;
    frame.hud.hp_max = data.hp_max;
    frame.hud.sp = data.sp;
    frame.hud.sp_max = data.sp_max;
    frame.hud.depth = data.depth;

    /*
     * **Sil-Q に無いもの**（設計 §4）。**負の値で「この概念が無い」と言う**
     * （`hud_snapshot.h` の約束。**P4 で 0 から改めた**）:
     * - `gold` … 金銭の概念が無い（`player_type` に `au` が無い）
     * - `level` … プレイヤレベルが無い（経験値を技能に振る）
     *
     * 0 で出していたころは画面に **`LV 0  AU 0`** が並んでいた
     * （`game_hud.cpp` が無条件に組む）。**「0 なら出さない」では直せない**——
     * 変愚では所持金 0 が当たり前にあり、そのとき `AU 0` が消えてしまう。
     * 「無い」と「0」は別の事実なので、コアが別の値で言う。
     */
    frame.hud.gold = -1;
    frame.hud.level = -1;

    /*
     * **`SP` の名札を送る**（SH-08 / W5。`hud_snapshot.h` の `sp_label`）。
     *
     * **Sil-Q に魔力は無い。** あの欄は**歌の力**で、先方の状態列も `Voice` と書く
     * （`xtra1.c` の `prt_voice`）。画面は「魔力」も「声」も知らない造りなので
     * （必守制約 1）、語はここで決めて運ぶ。**訳もここで当てる**——コアは自分の版の
     * 言語を知っているが、画面は知らない。
     *
     * 変愚・幻想蛮怒・短愚蛮怒は**この欄を空のままにする**ので、あちらの画は
     * 1 画素も変わらない（画面側の既定が `SP`）。
     */
    const std::string voice = to_utf8(sq_tr("Voice"));
    frame.hud.sp_label = voice;
    /*
     * `HP` の名札も同じ理由で運ぶ（2026-08-22。遊ぶ人の実機の絵）。
     * **`sp_label` だけ入れたら片方だけ日本語になった**——左の状態列はコアが
     * `生命力` と書くのに、HUD のゲージは `HP` と `声` が並ぶ画になっていた。
     * 語は **Sil-Q の状態列と同じ鍵**（`xtra1.c:442` の `Health`）を引く。
     * `"HP"` ではない——あちらにその字は無く、カタログにも載っていない。
     */
    frame.hud.hp_label = to_utf8(sq_tr("Health"));

    /*
     * `status_line` は変愚の `fill_hud` と**同じ形**にする——画面側が同じ読み方を
     * するので形を割らない。ただし **`LEV` は出さない**（無い数字を 0 で出すと
     * 「レベル 0 のキャラ」に見える）。代わりに未使用の経験値を出す
     * ——Sil-Q でいちばん近い「伸びしろ」の数である。
     */
    char head[64];
    char tail[64];
    std::snprintf(head, sizeof(head), " %d/%d  ", frame.hud.hp, frame.hud.hp_max);
    std::snprintf(tail, sizeof(tail), " %d/%d  EXP %d", frame.hud.sp, frame.hud.sp_max, data.exp);
    //! `SP` はここでも使わない（上の註記）。**組み立ては UTF-8 で**（名札が日本語になる）。
    frame.hud.status_line = frame.hud.hp_label + head + voice + tail;

    /*
     * 現在地は**コアの文言のまま**運ぶ（`prt_depth()` と同じ字面）。
     * Sil-Q には町も荒野も無いので、地名に当たるものは深さだけである。
     */
    frame.hud.right_top_lines.clear();
    if (data.depth_text[0] != '\0') {
        frame.hud.right_top_lines.push_back(to_utf8(data.depth_text));
    }
}

void fill_messages(GameFrame &frame)
{
    const int num = sq_message_count();
    const int take = (std::min)(kMessageLines, (num > 0) ? num : 0);

    frame.messages.clear();
    frame.messages.reserve(static_cast<std::size_t>(take));

    char buf[kMessageBytes];
    for (int age = take - 1; age >= 0; --age) {
        const int n = sq_message_text(age, buf, static_cast<int>(sizeof(buf)));
        if (n <= 0) {
            continue;
        }
        MessageEvent ev{};
        ev.seq = static_cast<uint32_t>(frame.messages.size());
        ev.color = 1;
        ev.text_utf8 = to_utf8(buf, static_cast<std::size_t>(n));
        if (ev.text_utf8.empty()) {
            continue;
        }
        frame.messages.push_back(std::move(ev));
    }
}

/* ==================================== 左の状態列・最下段の帯・階層（P4） */

/*!
 * @name Sil-Q の画面の割り付け（`silq/src/defines.h:679-760`）
 * @details **表示を組み直さず、Term のその区画をそのまま読む**（幻想蛮怒 P4 と同じ方針）。
 * 桁と行は Sil-Q のべた書きで、**変愚（13 桁・帯は横いっぱい）とも
 * 幻想蛮怒（20 桁）とも違う**:
 *
 * | 区画 | 桁 | 行 |
 * |---|---|---|
 * | 左の状態列 | 0〜12（`COL_MAP = 13`） | 1〜`hgt-1` |
 * | 最下段の帯 | **13〜**（空腹 13・盲目 22・混乱 28・朦朧 37・恐怖 49・状態 56・速度 67・地形 72） | `hgt-1` |
 * | 階層 | 0〜11（`prt_depth` の `"%12s"`。右詰め） | `hgt-2`（`NNNN ft`）と `hgt-1`（`min NNNN ft`） |
 *
 * **最下段は「左 13 桁が状態列・残りが帯」で割れている。** 変愚・幻想蛮怒は逆
 * （帯が横いっぱいで、右端だけ階層）なので、そのまま写すと `min NNNN ft` が
 * 帯の先頭に紛れ込む。
 * @{
 */
//! 左の状態列の桁数（`COL_MAP`）。ここから右が地図と帯である。
constexpr int kStatusColCells = 13;
/*! @} */

/*!
 * @name 直前に読めた値
 * @details 全画面の別画面（一覧・鍛冶・キャラクターシート）を開いている間、
 * Term の左端は本文で塗り潰されている。**その画を状態列として読まない**ために、
 * 直前に読めた値を使い回す（幻想蛮怒 `gb_frame.cpp` と同じ手）。
 * @{
 */
std::vector<SubPanelLine> g_status_col_cache;
std::vector<TermTextRun> g_bottom_row_cache;
SubPanelLine g_depth_cache;
/*! @} */

/*!
 * @brief セル 1 個ぶんを空白とみなすか。
 * @details 空白・未書き込み（`\0`）のほかに**制御文字**（`< 0x20` と `0x7F`）も空白にする。
 * 地図の桁が状態列へはみ出す穴は変愚・幻想蛮怒の両方で踏んでいる。
 * `row_to_text()` が同じ規則を使っているので、ミラーと状態列で字が食い違わない。
 *
 * @note **M1 で `>= 0x7F` を `== 0x7F` に改めた**（設計 §3.2）。
 * 0x80 以上は日本語の文字のバイトであって空白ではない。ここを直さないと、
 * 焼いた `lib-ja/edit` の名前がまるごと空白になる。
 * 幻想蛮怒の同名の関数（`gb_frame.cpp`）も同じ形である。
 */
bool cell_is_blank(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u == ' ') || (u < 0x20) || (u == 0x7F);
}

/*!
 * @brief 桁 `[x0, x1)` を 1 行にする。設計 §3.2 の 2 か所目。
 * @param trim_left 行頭の空白も落とすか。**状態列では落とさない**——1 行に複数の項目が
 * 決まった桁で並ぶ（`ROW_EQUIPPY` と `ROW_MEL` はどちらも行 13）ので、左を詰めると
 * 片方が無いときにもう片方が滑る。階層（`"%12s"` の右詰め）だけは両端を落とす。
 * @return 行の色は**最初の非空白**セルの色（`SubPanelLine` は 1 行 1 色）。
 *
 * @details **区画の端で 2 バイト文字が割れたら空白に落とす**（設計 §3.2 の表）。
 * 半分だけ運ぶと画面側で化ける。左の状態列は 13 桁で切っているので、
 * ここは必ず踏む道である。
 */
SubPanelLine span_to_line(const char *cells, const unsigned char *attrs, int n, int x0, int x1,
    bool trim_left)
{
    SubPanelLine out;
    const int begin = (std::max)(0, x0);
    int end = (std::min)(x1, n);

    while ((end > begin) && cell_is_blank(cells[end - 1])) {
        --end;
    }
    if (end <= begin) {
        return out;
    }

    std::string sys; // CP932 のまま組み、最後に 1 回だけ変換する
    bool color_found = false;
    bool wide = false;
    for (int x = begin; x < end; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if (!color_found && !cell_is_blank(cells[x])) {
            out.color = attrs[x];
            color_found = true;
        }
        if (is_sjis_lead(lead) && ((x + 1) < end)) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (is_sjis_lead(lead)) {
            sys.push_back(' '); // 区画の端で割れた。**半端なバイトは運ばない**
            continue;
        }
        sys.push_back(cell_is_blank(cells[x]) ? ' ' : cells[x]);
    }

    if (trim_left) {
        const std::size_t head = sys.find_first_not_of(' ');
        sys = (head == std::string::npos) ? std::string() : sys.substr(head);
    }

    if (!wide) {
        out.text_utf8 = std::move(sys); // 純 ASCII。**1 バイトも触らない**
        return out;
    }
    out.text_utf8 = sjis_to_utf8(sys);
    if (out.text_utf8.empty() && !sys.empty()) {
        out.text_utf8.assign(sys.size(), ' ');
    }
    return out;
}

/*!
 * @brief 最下段の帯を**同じ色が続く区間ごと**に切って桁とともに積む（K-32）。
 * @details この行は桁と色の両方に意味がある——空腹 13・盲目 22・混乱 28・朦朧 37・
 * 恐怖 49・状態 56・速度 67・地形 72 が**決まった桁**に出る。1 行 1 色に潰すと両方落ちる。
 * @param x0 ここから左は積まない（左 13 桁は状態列＝`min NNNN ft` なので帯に混ぜない）
 * @note 桁は **Term のまま**運ぶ（`bottom_row_cols` も Term の幅のまま）。
 * 13 桁ぶん詰めて左へ寄せることもできるが、そうすると「帯の桁」と「コアの桁」が
 * ずれて、後で数え直す人が必ず間違える。左の 16% が空くのは
 * **コアがそこに帯を書いていない**という事実そのものである。
 */
void row_to_runs(const char *cells, const unsigned char *attrs, int n, int x0,
    std::vector<TermTextRun> &out)
{
    out.clear();
    const int begin = (std::max)(0, x0);

    std::string sys; // CP932 のまま溜め、run を閉じるときに 1 回だけ変換する
    bool wide = false;
    int run_col = 0;
    unsigned char run_color = 1;

    const auto flush = [&]() {
        while (!sys.empty() && (sys.back() == ' ')) {
            sys.pop_back();
        }
        if (sys.empty()) {
            wide = false;
            return;
        }
        TermTextRun run;
        run.col = run_col;
        run.color = run_color;
        if (wide) {
            run.text_utf8 = sjis_to_utf8(sys);
            if (run.text_utf8.empty()) {
                run.text_utf8.assign(sys.size(), ' ');
            }
        } else {
            run.text_utf8 = sys; // 純 ASCII。**1 バイトも触らない**
        }
        out.push_back(std::move(run));
        sys.clear();
        wide = false;
    };

    for (int x = begin; x < n; ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        const unsigned char color = attrs[x];

        if (cell_is_blank(cells[x])) {
            /* 空白 2 桁以上で区切る（1 桁の空白は語の間なので run に含める）。 */
            if (!sys.empty() && ((x + 1) < n) && cell_is_blank(cells[x + 1])) {
                flush();
                continue;
            }
            if (!sys.empty()) {
                sys.push_back(' ');
            }
            continue;
        }

        if (sys.empty()) {
            run_col = x;
            run_color = color;
        } else if (color != run_color) {
            flush();
            run_col = x;
            run_color = color;
        }

        if (is_sjis_lead(lead) && ((x + 1) < n)) {
            /* 2 セル 1 文字。**属性は先行バイト側**を採る（文字を割らない）。 */
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (is_sjis_lead(lead)) {
            break; // 行の端で割れた。半端なバイトは運ばない
        }
        sys.push_back(cells[x]);
    }
    flush();
}

/*
 * ==== 状態のアイコン（`GameFrame::status_effects`）====
 *
 * 範囲:
 *  - 原作の最下段（`xtra1.c` の `prt_frame_extra()`）の状態：速さ・朦朧・空腹・盲目・混乱・恐怖・毒・切り傷・
 *    幻惑・鍛冶・矢作り・隠密行動。
 *  - メッセージで知らされる一時的な状態：幻覚・一時的な能力値の上昇・透明視・耐火・耐冷・耐毒・知覚の上昇・
 *    激怒・魔法の暗黒。
 *  - 歌は**曲ごと**の鍵（`sq_song_*`）。主旋律と副旋律（織り合わせ）を歌うときは 2 つ並べる。
 *  - 常時の効果は、C 画面の Flags のページの**既知の旗**だけ（`sq_read_status()` の `perm_mask`。未鑑定は出さない）。
 *  - 出さないもの：地形の注意書き（落とし穴・蜘蛛の巣・陽だまり）・休息・繰り返しの回数・残りターン。
 *
 * 名前の字はコアの言語で書く（`sq_lang_enabled()`）。原作が最下段へ出す字（`messages.ja.txt` の `xtra1.c` の項）と
 * 同じ語にそろえ、歌の名前は原作の `b_name`（日本語層が起きていれば日本語）から引く。鍵は画面側の表
 * （`hd2d/ui/status_icon_table.inc`）にある名前だけ。表に絵が無い物は送らない（`status_selftest()` が見る）。
 */

//! 日本語層が起きていれば `ja`、でなければ `en`。**UTF-8 にして返す**（修-37）。
//! 呼び手が渡す字面は、このアダプタの実行文字集合（既定は ACP＝CP932）のバイト列なので、
//! 線（`label_utf8`）へ入れる前にここで 1 度だけ `to_utf8()` を通す。英語は ASCII なので素通し。
//! 歌の名前（`song_label()`）はすでに UTF-8 にしてあるので、これを通さない。
inline std::string ja_en(const char *ja, const char *en) { return to_utf8((sq_lang_enabled() != 0) ? ja : en); }

void add_status(std::vector<StatusEffectView> &out, const char *key, const std::string &label, StatusEffectKind kind)
{
    StatusEffectView view{};
    view.key = key;
    view.label_utf8 = label;
    view.kind = kind;
    out.push_back(std::move(view));
}

//! 歌の鍵。原作の `SNG_ELBERETH` から `SNG_MASTERY` の順（`defines.h`）。
const char *const kSongKeys[] = {
    "sq_song_elbereth", "sq_song_challenge", "sq_song_delvings", "sq_song_freedom", "sq_song_silence", "sq_song_staunching",
    "sq_song_thresholds", "sq_song_trees", "sq_song_slaying", "sq_song_staying", "sq_song_lorien", "sq_song_mastery",
};
constexpr int kSongCount = static_cast<int>(sizeof(kSongKeys) / sizeof(kSongKeys[0]));

//! 歌の名前。英語は `Song of ` を落とす（最下段の `prt_song()` と同じ。日本語は元から付かない）。
std::string song_label(const char *name)
{
    std::string text = to_utf8(name);
    static const char kPrefix[] = "Song of ";
    if ((sq_lang_enabled() == 0) && (text.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0)) {
        text.erase(0, sizeof(kPrefix) - 1);
    }
    return text;
}

//! 常時の効果の表の 1 行。`bit` は `sq_status_state::perm_mask` の桁（`SQ_PERM_*`）。
struct PermRow {
    int bit;
    const char *key;
    const char *ja;
    const char *en;
};

const PermRow kPermRows[] = {
    { SQ_PERM_SUST_STR, "sust_str", "腕力の維持", "Sustain Str" },
    { SQ_PERM_SUST_DEX, "sust_dex", "敏捷の維持", "Sustain Dex" },
    { SQ_PERM_SUST_CON, "sust_con", "耐久の維持", "Sustain Con" },
    { SQ_PERM_SUST_GRA, "sust_wis", "恩寵の維持", "Sustain Gra" }, // 恩寵（Grace）に当たる維持の絵は無いので、賢さの維持の絵を使う
    { SQ_PERM_RES_COLD, "res_cold", "冷気耐性", "Resist Cold" },
    { SQ_PERM_RES_FIRE, "res_fire", "火耐性", "Resist Fire" },
    { SQ_PERM_RES_ELEC, "res_elec", "電撃耐性", "Resist Elec" },
    { SQ_PERM_RES_POIS, "res_pois", "毒耐性", "Resist Poison" },
    { SQ_PERM_RES_FEAR, "res_fear", "恐怖耐性", "Resist Fear" },
    { SQ_PERM_RES_BLIND, "res_blind", "盲目耐性", "Resist Blind" },
    { SQ_PERM_RES_CONFU, "res_conf", "混乱耐性", "Resist Conf" },
    { SQ_PERM_RES_STUN, "res_stun", "朦朧耐性", "Resist Stun" },
    { SQ_PERM_SLOW_DIGEST, "slow_digest", "腹持ち", "Sustenance" },
    { SQ_PERM_LIGHT, "lite", "明かり", "Light" },
    { SQ_PERM_REGEN, "regeneration", "傷の塞がり", "Regeneration" },
    { SQ_PERM_SEE_INVIS, "senseunseen", "透明を見る", "See Invisible" },
    { SQ_PERM_FREE_ACT, "free_act", "動きの自由", "Free Action" },
    { SQ_PERM_SLAY_ORC, "slay_orc", "オーク殺し", "Slay Orc" },
    { SQ_PERM_SLAY_TROLL, "slay_troll", "トロル殺し", "Slay Troll" },
    { SQ_PERM_SLAY_WOLF, "slay_animal", "狼殺し", "Slay Wolf" }, // 狼・蜘蛛の絵は無いので、動物倍打の絵を使う
    { SQ_PERM_SLAY_SPIDER, "slay_animal", "蜘蛛殺し", "Slay Spider" },
    { SQ_PERM_SLAY_UNDEAD, "slay_undead", "不死者殺し", "Slay Undead" },
    { SQ_PERM_SLAY_RAUKO, "slay_demon", "魔霊殺し", "Slay Rauko" }, // 魔霊（Rauko）はデーモンに当たる
    { SQ_PERM_SLAY_DRAGON, "slay_dragon", "竜殺し", "Slay Dragon" },
    { SQ_PERM_SKILL_MEL, "blows", "打撃上昇", "Melee Up" },
    { SQ_PERM_SKILL_ARC, "xtra_shots", "射撃上昇", "Archery Up" },
    { SQ_PERM_SKILL_STL, "stealth", "隠密上昇", "Stealth Up" },
    { SQ_PERM_SKILL_PER, "search", "知覚上昇", "Perception Up" },
    { SQ_PERM_SKILL_WIL, "magic_mastery", "意志上昇", "Will Up" },
};

//! 材料（`sq_read_status()`）→ アイコンの並び。並びは (1) 速さ (2) 最下段 (3) 歌 (4) 知らされる状態 (5) 常時の効果。
std::vector<StatusEffectView> build_status_effects(const sq_status_state &st)
{
    constexpr StatusEffectKind Good = StatusEffectKind::Good;
    constexpr StatusEffectKind Bad = StatusEffectKind::Bad;
    constexpr StatusEffectKind Special = StatusEffectKind::Special;
    std::vector<StatusEffectView> out;
    if (st.in_game == 0) {
        return out;
    }

    // (1) 速さ。原作は「Fast」「Slow」の字だけで数字を出さない（効果量は空）。
    if (st.speed_stage > 0) {
        add_status(out, "haste", ja_en("加速", "Fast"), Good);
    } else if (st.speed_stage < 0) {
        add_status(out, "slow", ja_en("鈍足", "Slow"), Bad);
    }

    // (2) 最下段。朦朧 → 空腹 → 盲目・混乱・恐怖・毒・切り傷 → 幻惑・鍛冶・矢作り・隠密行動。
    switch (st.stun_stage) {
    case 1: add_status(out, "stun", ja_en("よろけ", "Stun"), Bad); break;
    case 2: add_status(out, "stun", ja_en("朦朧", "Heavy stun"), Bad); break;
    case 3: add_status(out, "stun", ja_en("昏倒", "Knocked out"), Bad); break;
    default: break;
    }
    switch (st.hunger_stage) {
    case 1: add_status(out, "hungry", ja_en("空腹", "Hungry"), Bad); break;
    case 2: add_status(out, "hungry", ja_en("衰弱", "Weak"), Bad); break;
    case 3: add_status(out, "hungry", ja_en("飢餓", "Starving"), Bad); break;
    case 4: add_status(out, "hungry", ja_en("満腹", "Full"), Bad); break;
    default: break;
    }
    if (st.blind > 0) add_status(out, "blind", ja_en("盲目", "Blind"), Bad);
    if (st.confused > 0) add_status(out, "confused", ja_en("混乱", "Confused"), Bad);
    if (st.afraid > 0) add_status(out, "afraid", ja_en("恐怖", "Afraid"), Bad);
    if (st.poisoned > 0) add_status(out, "poisoned", ja_en("毒", "Poisoned"), Bad);
    if (st.cut_stage == 1) add_status(out, "cut", ja_en("出血", "Bleeding"), Bad);
    if (st.cut_stage >= 2) add_status(out, "cut", ja_en("致命傷", "Mortal wound"), Bad);
    if (st.entranced > 0) add_status(out, "paralyzed", ja_en("幻惑", "Entranced"), Bad);
    if (st.smithing > 0) add_status(out, "sq_smithing", ja_en("鍛冶中", "Smithing"), Special);
    if (st.fletching > 0) add_status(out, "sq_fletching", ja_en("矢作り", "Fletching"), Special);
    if (st.stealth_mode != 0) add_status(out, "stealth", ja_en("隠密行動", "Stealth"), Good);

    // (3) 歌。主旋律（1 曲目）と副旋律（織り合わせの 2 曲目）。
    if ((st.song1 >= 0) && (st.song1 < kSongCount)) add_status(out, kSongKeys[st.song1], song_label(st.song1_name), Good);
    if ((st.song2 >= 0) && (st.song2 < kSongCount)) add_status(out, kSongKeys[st.song2], song_label(st.song2_name), Good);

    // (4) メッセージで知らされる一時的な状態。
    if (st.image > 0) add_status(out, "hallucination", ja_en("幻覚", "Hallucinating"), Bad);
    static const char *const kStatJa[] = { "腕力上昇", "敏捷上昇", "耐久上昇", "恩寵上昇" };
    static const char *const kStatEn[] = { "Str Up", "Dex Up", "Con Up", "Gra Up" };
    for (int i = 0; i < 4; ++i) {
        if (st.tmp_stat[i] > 0) add_status(out, "buildup", ja_en(kStatJa[i], kStatEn[i]), Good);
    }
    if (st.tim_invis > 0) add_status(out, "senseunseen", ja_en("透明視", "See Invisible"), Good);
    if (st.oppose_fire > 0) add_status(out, "res_fire", ja_en("耐火", "Resist Fire"), Good);
    if (st.oppose_cold > 0) add_status(out, "res_cold", ja_en("耐冷", "Resist Cold"), Good);
    if (st.oppose_pois > 0) add_status(out, "res_pois", ja_en("耐毒", "Resist Poison"), Good);
    if (st.tmp_per > 0) add_status(out, "sq_per_up", ja_en("知覚上昇", "Perception Up"), Good);
    if (st.rage > 0) add_status(out, "berserk", ja_en("激怒", "Rage"), Good);
    if (st.darkened > 0) add_status(out, "sq_darkened", ja_en("魔法の暗黒", "Darkened"), Bad);

    // (5) 常時の効果（既知の旗だけ）。
    for (const PermRow &row : kPermRows) {
        if ((st.perm_mask & (1u << row.bit)) != 0u) {
            add_status(out, row.key, ja_en(row.ja, row.en), StatusEffectKind::Permanent);
        }
    }
    return out;
}

void fill_status_effects(GameFrame &frame)
{
    sq_status_state st;
    (void)sq_read_status(&st);
    frame.status_effects = build_status_effects(st);
}

/*!
 * @brief 左の状態列（K-28）・最下段の帯（K-32）・階層（K-33）をまとめて写す。
 * @details 読む相手の判断（別画面なら直前の値）が同じなので 1 か所に置く。
 */
void fill_status_col(GameFrame &frame)
{
    frame.status_col_lines.clear();
    frame.bottom_row_runs.clear();
    frame.bottom_row_cols = 0;
    frame.depth = SubPanelLine{};

    const int screen = sq_screen_flags();
    if (((screen & SQ_SCREEN_GENERATED) == 0) || ((screen & SQ_SCREEN_DUNGEON) == 0)
        || ((screen & SQ_SCREEN_DEAD) != 0)) {
        /*
         * @ がまだ無い（タイトル・誕生）／もう無い（死亡）。Term の左端は本文なので
         * 読まない。**古い値も捨てる**——墓碑の画を状態列として拾わないため。
         */
        g_status_col_cache.clear();
        g_bottom_row_cache.clear();
        g_depth_cache = SubPanelLine{};
        return;
    }

    int cols = 0;
    int rows = 0;
    if (sq_term_size(&cols, &rows) != SQ_OK) {
        return;
    }
    cols = (std::min)(cols, kMaxCols - 1);
    if ((cols <= kStatusColCells) || (rows < 4)) {
        return;
    }
    frame.bottom_row_cols = cols;

    //! 階層が書かれている 2 行（`ROW_DEPTH` / `ROW_MIN_DEPTH`）。
    const int depth_row = rows - 2;
    const int min_depth_row = rows - 1;

    if ((screen & (SQ_SCREEN_ICKY | SQ_SCREEN_XTRA)) == 0) {
        char cells[kMaxCols];
        unsigned char attrs[kMaxCols];

        std::vector<SubPanelLine> lines;
        for (int y = 1; y <= min_depth_row; ++y) {
            if (y == depth_row) {
                /*
                 * 階層は**右下の 1 か所だけ**に出す約束（`game_frame.h` の `depth`）。
                 * ここは空行にして**行の並びを崩さない**——落とすと下の
                 * `min NNNN ft` が 1 行ぶり上がって、他の項目と目が合わなくなる。
                 */
                lines.push_back(SubPanelLine{});
                continue;
            }
            std::memset(cells, ' ', sizeof(cells));
            std::memset(attrs, 1, sizeof(attrs));
            const int n = sq_term_row(y, cells, attrs, kMaxCols);
            if (n < 0) {
                continue;
            }
            lines.push_back(span_to_line(cells, attrs, n, 0, kStatusColCells, false));
        }
        while (!lines.empty() && lines.back().text_utf8.empty()) {
            lines.pop_back();
        }
        frame.status_col_lines = std::move(lines);

        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        int n = sq_term_row(depth_row, cells, attrs, kMaxCols);
        if (n > 0) {
            /* `"%12s"` の右詰め。両端を落として `50 ft` だけにする。 */
            frame.depth = span_to_line(cells, attrs, n, 0, kStatusColCells, true);
        }

        std::memset(cells, ' ', sizeof(cells));
        std::memset(attrs, 1, sizeof(attrs));
        n = sq_term_row(min_depth_row, cells, attrs, kMaxCols);
        if (n > 0) {
            row_to_runs(cells, attrs, n, kStatusColCells, frame.bottom_row_runs);
        }

        g_status_col_cache = frame.status_col_lines;
        g_bottom_row_cache = frame.bottom_row_runs;
        g_depth_cache = frame.depth;
        return;
    }

    frame.status_col_lines = g_status_col_cache;
    frame.bottom_row_runs = g_bottom_row_cache;
    frame.depth = g_depth_cache;
}

/* ==================================================================== 地図（P3） */

/*!
 * @brief `SQ_FEAT_*`（C の境界）→ `CELL_FEAT_*`（画面側）。
 * @details **表をここ 1 か所に置く。** 値を揃えて素通しにすると、どちらかの並びが
 * 変わったときに黙って別の意味になる（壁が扉になる類の壊れ方で、絵を見るまで分からない）。
 *
 * Sil-Q にしか無いビット（`SQ_FEAT_CHASM` / `SQ_FEAT_FORGE`）は**落とす**——
 * 画面側に受け皿が無い。深淵は `PASSABLE` が立っているので床として通る（設計 §4.2）。
 */
uint16_t translate_feature_flags(unsigned short bits)
{
    static const struct {
        unsigned short from;
        uint16_t to;
    } kTable[] = {
        { SQ_FEAT_WALL, CELL_FEAT_WALL },
        { SQ_FEAT_DOOR, CELL_FEAT_DOOR },
        { SQ_FEAT_DOOR_OPEN, CELL_FEAT_DOOR_OPEN },
        { SQ_FEAT_STAIRS, CELL_FEAT_STAIRS },
        { SQ_FEAT_PERMANENT, CELL_FEAT_PERMANENT },
        { SQ_FEAT_RUBBLE, CELL_FEAT_RUBBLE },
        { SQ_FEAT_GLOW, CELL_FEAT_GLOW },
        { SQ_FEAT_PLAYER, CELL_FEAT_PLAYER },
        { SQ_FEAT_KNOWN, CELL_FEAT_KNOWN },
        { SQ_FEAT_PASSABLE, CELL_FEAT_PASSABLE },
        { SQ_FEAT_ROOM, CELL_FEAT_ROOM },
        { SQ_FEAT_GLOWING, CELL_FEAT_GLOWING },
        { SQ_FEAT_SHAFT, CELL_FEAT_SHAFT },
    };

    uint16_t out = 0;
    for (const auto &row : kTable) {
        if ((bits & row.from) != 0) {
            out |= row.to;
        }
    }
    return out;
}

/*! @brief `SQ_MM_*` → `MinimapKind`。値を揃えて素通しにしない理由は上と同じ。 */
uint8_t translate_minimap_kind(unsigned char kind)
{
    switch (kind) {
    case SQ_MM_FLOOR:
        return static_cast<uint8_t>(MinimapKind::Floor);
    case SQ_MM_WALL:
        return static_cast<uint8_t>(MinimapKind::Wall);
    case SQ_MM_DOOR:
        return static_cast<uint8_t>(MinimapKind::Door);
    case SQ_MM_STAIRS:
        return static_cast<uint8_t>(MinimapKind::Stairs);
    case SQ_MM_ITEM:
        return static_cast<uint8_t>(MinimapKind::Item);
    case SQ_MM_MONSTER:
        return static_cast<uint8_t>(MinimapKind::Monster);
    case SQ_MM_PLAYER:
        return static_cast<uint8_t>(MinimapKind::Player);
    default:
        return static_cast<uint8_t>(MinimapKind::Unknown);
    }
}

/*!
 * @brief そのマスの絵を決める（設計 §5）。
 * @details 優先は **@ → 見えている敵 → 落ちている品 → 地形**で、変愚の
 * `presentation_bridge.cpp::resolve_tile_index()` と同じ順である（画面側は
 * 1 マスに 1 枚しか立てないので、順番を変えると敵が床で塗り潰される）。
 *
 * **実体が居て絵が無いときは 0 を返す**（`sq_manifest.h` の「索引 0 のまま」）。
 * 画面側のアスキー実体が `ascii_fallback` から文字の板を立てて拾う。
 *
 * > **かつてここは地形の索引を返していた**（2026-08-21 に直した）。`tile_index` を
 * > 読むのは画面側の `entity_view.cpp` **だけ**で、そこは「実体が居るマス」しか見ない
 * > ——地形は `terrain_id` から別に描かれる。だから地形の索引を返すと、画面側はそれを
 * > **実体の板の番号として** `slab_library` へ持って行く。地形に板（`.vox`）は無いので
 * > 必ず外れ、何も描かれないまま `警告: タイル欠け` が出る。チュートリアルの床の覚え書き
 * > （`~`）がまるごと消えていたのがこれである。
 * >
 * > 名前がたまたま板と一致すれば、今度は**地形の絵が実体として立つ**——無いより悪い。
 * > 0 を返すことと嘘の索引を返すことは違う。
 *
 * @param src そのマス
 * @param is_player @ の居るマスか（位置は `fill_map` が知っている）
 * @param[out] under 実体を立てたときに**足元へ敷く地形**の索引。実体が無ければ 0
 */
/*!
 * @brief **Sil-Q の feat を、画面側が読む地形番号へ名寄せする**（2026-08-22）。
 *
 * ## なぜ要るのか
 * 立体の細別は `PrefabLibrary::rule_for_terrain(terrain_id)` が
 * `assets/voxel/terrain_prefabs.jsonc` を**番号で**引いて決める。あの表は
 * **変愚の番号**でできている。Sil-Q の feat をそのまま送っていたので、
 * 番号が偶然かぶったマスに**まるで別の地形**が立っていた——実測 35 件:
 *
 * | Sil-Q | 出ていたもの |
 * |---|---|
 * | 9 陽だまり | 上り階段（変愚の `QUEST_EXIT`）…… 気づいたことの出どころ |
 * | 6〜8 護符の扉 | 上り階段・下り階段・クエストの入口 |
 * | 65〜79 鍛冶場 | 「パターン」の床と、町の店 6 軒 |
 * | **80〜83 階段と縦坑** | 闇市場・我が家・書店・深い水 |
 *
 * 扉（32〜47）・花崗岩（56〜59）・瓦礫（49）・石英（51）は番号が揃っているので
 * 素通しで正しく出ていた。**当たっていたマスだけが壊れて見えていた**わけである。
 *
 * ## 表をここに書かない理由
 * 名寄せの正は `silq/tilework/terrain_map.csv` である（`voxel_id` 欄）。
 * 幻想蛮怒は同じことを C の表でやっている（`gb_frame.cpp` の `align_terrain_id`）が、
 * こちらは**倒し先の表がすでにファイルにある**——2 本目を C で持つと、
 * 片方だけ直したときに黙って食い違う（`sq_manifest.h` の「表を 2 つ持たない」）。
 *
 * @param feat Sil-Q の feat 番号（`f_info[].mimic` 解決後）
 * @return 画面へ送る番号。目録が差さっていなければ feat のまま（従来どおり）
 */
uint16_t align_terrain_id(uint16_t feat)
{
    if (g_manifest == nullptr) {
        return feat;
    }
    return g_manifest->voxel_terrain(static_cast<int>(feat), feat);
}

uint16_t resolve_tile_index(const sq_map_cell &src, bool is_player, uint16_t &under)
{
    under = 0;
    if (g_manifest == nullptr) {
        return 0;
    }
    const uint16_t terrain = g_manifest->lookup_terrain(static_cast<int>(src.terrain_id));

    if (is_player) {
        int race = 0;
        if ((sq_player_race(&race) == SQ_OK)) {
            if (const uint16_t pt = g_manifest->lookup_player(race)) {
                under = terrain;
                return pt;
            }
        }
    }
    /* `monster_id` は `ml`（見えている）ときだけ入る——記憶の敵は描かない。 */
    if (src.monster_id != 0) {
        if (const uint16_t mt = g_manifest->lookup_monster(static_cast<int>(src.monster_id))) {
            under = terrain;
            return mt;
        }
    }
    /* 品は**いま照明があるとき**だけ（記憶のマスに品の絵を残さない。変愚と同じ）。 */
    if ((src.object_id != 0) && (src.light_level >= 2)) {
        /*
         * 正体の分からないアイテム。`object_unaware_category`
         * は `sq_shim.c` の `sq_unaware_category()` が既に判定済み（0 = 対象外）。
         */
        if (src.object_unaware_category != 0) {
            const auto category = static_cast<presentation::UnawareItemCategory>(src.object_unaware_category);
            if (const uint16_t ut = g_manifest->lookup_unaware_representative(category)) {
                under = terrain;
                return ut;
            }
        }
        if (const uint16_t ot = g_manifest->lookup_object(static_cast<int>(src.object_id))) {
            under = terrain;
            return ot;
        }
    }
    /*
     * ここへ来たということは、**このマスに実体が居るなら、その実体の絵は無い**。
     * そのときは 0 を返す（上の註記）。判定は画面側の `entity_view.cpp` が
     * 「実体が居る」と見る条件と**同じ 3 つ**にする——片方だけ増えると、また
     * 「画面は実体だと思っているのにコアは地形を渡す」に戻る。
     */
    if (is_player || (src.monster_id != 0) || (src.object_id != 0)) {
        return 0;
    }
    return terrain;
}

void fill_map(GameFrame &frame)
{
    int floor_w = 0;
    int floor_h = 0;
    if (sq_floor_size(&floor_w, &floor_h) != SQ_OK) {
        return; // 階がまだ無い（タイトル・誕生画面）
    }

    int px = 0;
    int py = 0;
    if (sq_player_pos(&px, &py) != SQ_OK) {
        return;
    }
    frame.player_gx = px;
    frame.player_gy = py;

    const int view_w = (std::min)((std::max)(g_view_w, 1), kMaxView);
    const int view_h = (std::min)((std::max)(g_view_h, 1), kMaxView);
    frame.view_w = view_w;
    frame.view_h = view_h;

    int ox = px - (view_w / 2);
    int oy = py - (view_h / 2);
    /*
     * **コアが @ から離れた所を見ているなら、そちらへ窓を移す**
     * （気づいたこと 2026-08-26「地図を動かすモードに入るもキー入力をしても画面変化なし」）。
     *
     * `L`（`do_cmd_locate`。`cmd3.c:1966`）は `p_ptr->wy` / `wx` を 1 画面ぶんずつ動かすだけで、
     * **@ は動かない**。画面側は @ を中心に窓を切っているので、そのままでは
     * 押しても絵が 1 画素も変わらなかった。
     *
     * **見分けは「@ が区画の外に出たか」で足りる。** ふつうに歩いている間は
     * コアが `PU_PANEL` で区画を @ に追わせているので、@ は必ず区画の中に居る。
     * `L` を取り消せば同じ仕組みで戻るので、**後始末も要らない**。
     */
    frame.camera_detached = (sq_locate_active() != 0);
    if (frame.camera_detached) {
        /*
         * **`L`（地図を動かす）の最中は、コアの区画へ窓を移す**
         * （気づいたこと 2026-08-26「地図を動かすモードに入るもキー入力をしても画面変化なし」）。
         *
         * `do_cmd_locate()` が動かすのは `p_ptr->wy` / `wx` だけで **@ は動かない**。
         * 画面側は @ を中心に窓を切っているので、そのままでは 1 画素も変わらなかった。
         *
         * **「@ が区画の外に出たか」で見分けようとして外した**（同日）。狭い階では
         * たまたま立ったが、**広い階では 1 画面ぶん動かしても @ は区画の中に残る**。
         * 命令そのものが旗を立てるようにして、ここはそれだけを見る。
         */
        int panel_x = 0;
        int panel_y = 0;
        int panel_w = 0;
        int panel_h = 0;
        if ((sq_panel_view(&panel_x, &panel_y, &panel_w, &panel_h) == SQ_OK) && (panel_w > 0)
            && (panel_h > 0)) {
            /*
             * **区画の真ん中へ置くが、階の端では端へ寄せる**（気づいたこと 2026-08-26
             * 「キャラはマップ中ごろにいるので画面に移っていない左側の踏破済み箇所はある」）。
             *
             * こちらの窓（45 マス）はコアの区画（66 マス）より**狭い**ので、真ん中へ
             * 置くと区画の左右が 10 マスずつ隠れる。区画が階の端まで来ているときは、
             * その端こそ見たい所である。**端では端へ寄せる。**
             */
            ox = panel_x + ((panel_w - view_w) / 2);
            oy = panel_y + ((panel_h - view_h) / 2);
            if (panel_x <= 0) {
                ox = 0;
            } else if ((panel_x + panel_w) >= floor_w) {
                ox = (std::max)(0, floor_w - view_w);
            }
            if (panel_y <= 0) {
                oy = 0;
            } else if ((panel_y + panel_h) >= floor_h) {
                oy = (std::max)(0, floor_h - view_h);
            }
            static int said_x = -9999;
            static int said_y = -9999;
            if ((panel_x != said_x) || (panel_y != said_y)) {
                said_x = panel_x;
                said_y = panel_y;
                std::fprintf(stderr,
                    "[silq] 地図を動かす: 区画 (%d,%d) %dx%d / @ (%d,%d) / 窓 %dx%d -> (%d,%d)\n",
                    panel_x, panel_y, panel_w, panel_h, px, py, view_w, view_h, ox, oy);
            }
        }
    }

    frame.cam_x = ox + (view_w / 2);
    frame.cam_y = oy + (view_h / 2);

    std::vector<sq_map_cell> raw(static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h));
    const int n = sq_read_map(ox, oy, view_w, view_h, raw.data(), static_cast<int>(raw.size()));
    if (n <= 0) {
        return;
    }

    frame.cells.reserve(static_cast<std::size_t>(n));
    /*
     * プレイヤの光で照らされた（暗黒側なら暗くされた）マスのビット列（`LightingState::player_lit`）。
     * `cells` と同じ並び・1 マス 1 ビット・下位ビットから。**送るかどうかは光源の種類が決める**
     * ので、ここでは作るだけで、`fill_floor_and_lighting()` が種類を見て確定する（光が無ければ捨てる）。
     */
    std::vector<std::uint8_t> player_lit((static_cast<std::size_t>(n) + 7u) / 8u, 0u);
    for (int i = 0; i < n; ++i) {
        const sq_map_cell &src = raw[static_cast<std::size_t>(i)];
        if (src.player_lit != 0) {
            player_lit[static_cast<std::size_t>(i) >> 3] = static_cast<std::uint8_t>(
                player_lit[static_cast<std::size_t>(i) >> 3] | (1u << (static_cast<unsigned>(i) & 7u)));
        }
        MapCellView cell{};
        cell.gx = src.gx;
        cell.gy = src.gy;
        //! **番号を名寄せしてから送る**（`align_terrain_id`。素通しは 35 件が別物になる）。
        cell.terrain_id = align_terrain_id(src.terrain_id);
        cell.feature_flags = translate_feature_flags(src.feature_flags);
        cell.monster_id = src.monster_id;
        cell.monster_slot = src.monster_slot;
        cell.object_id = src.object_id;
        cell.light_level = src.light_level;
        cell.fg_color = src.fg_color;
        cell.bg_color = src.bg_color;
        cell.ascii_fallback = src.ascii;
        {
            /* @ の居るマスかどうかは位置で見る（`sq_map_cell` は @ を持たない）。 */
            const bool is_player = (src.gx == static_cast<short>(px)) && (src.gy == static_cast<short>(py));
            uint16_t under = 0;
            cell.tile_index = resolve_tile_index(src, is_player, under);
            cell.under_tile_index = under;
        }
        /* `graf_*` は -1 固定（設計 §4 の表）。 */
        frame.cells.push_back(cell);
    }
    frame.lighting.player_lit = std::move(player_lit);

    /*
     * 一過性の重ね書き（SQ-2。`sq_shim.h` の `sq_read_overlay()`）。
     * **`raw` をそのまま材料にする**——`map_info()` は純粋関数ではないので、
     * 突き合わせのために呼び直すと 1 フレームに 2 度乱数を引くことになる。
     */
    if (!frame.menu_open) {
        /*
         * **ミラーが前に出ている画面では採らない。** 一覧・鍛冶・墓碑のあいだ、Term の
         * 地図区画には本文が載っているので、突き合わせると全面が「食い違い」になる
         * （実測: 死亡の墓碑で 198 件出た）。判定は `menu_open` 1 か所に任せる。
         */
        std::vector<sq_overlay_cell> over(raw.size());
        const int m = sq_read_overlay(raw.data(), n, over.data(), static_cast<int>(over.size()));
        if (m > 0) {
            frame.map_overlay.reserve(static_cast<std::size_t>(m));
            for (int i = 0; i < m; ++i) {
                MapOverlayCell cell{};
                cell.gx = over[static_cast<std::size_t>(i)].gx;
                cell.gy = over[static_cast<std::size_t>(i)].gy;
                cell.ascii = over[static_cast<std::size_t>(i)].ascii;
                cell.color = over[static_cast<std::size_t>(i)].color;
                frame.map_overlay.push_back(cell);
            }
        }
    }

    /*
     * 敵の警戒度と照準（SQ-1。`sq_shim.h` の `sq_read_alerts()` / `sq_read_target()`）。
     * **`menu_open` では採らない**——リングは地図の上の飾りで、ミラーが前に出ている間は
     * 出しても意味が無い（重ね書きと同じ判断を 1 か所に揃える）。
     */
    if (!frame.menu_open) {
        //! 見えている敵は視界ぶんしか居ないので、受け皿は `cells` と同じ大きさで足りる。
        std::vector<sq_alert_cell> alerts(raw.size());
        const int m = sq_read_alerts(alerts.data(), static_cast<int>(alerts.size()));
        for (int i = 0; i < m; ++i) {
            MonsterAlertCell cell{};
            cell.gx = alerts[static_cast<std::size_t>(i)].gx;
            cell.gy = alerts[static_cast<std::size_t>(i)].gy;
            cell.level = alerts[static_cast<std::size_t>(i)].level;
            frame.monster_alerts.push_back(cell);
        }

        int tx = 0;
        int ty = 0;
        if (sq_read_target(&tx, &ty) == SQ_OK) {
            frame.target_gx = static_cast<int16_t>(tx);
            frame.target_gy = static_cast<int16_t>(ty);
        }
    }

    /* ミニマップ（階の全域）。`cells` は視界ぶんしかないので別に運ぶ。 */
    std::vector<unsigned char> kinds(static_cast<std::size_t>(floor_w) * static_cast<std::size_t>(floor_h));
    const int written = sq_read_minimap(kinds.data(), static_cast<int>(kinds.size()));
    if (written == static_cast<int>(kinds.size())) {
        frame.minimap.width = floor_w;
        frame.minimap.height = floor_h;
        frame.minimap.player_gx = px;
        frame.minimap.player_gy = py;
        frame.minimap.kinds.resize(kinds.size());
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            frame.minimap.kinds[i] = translate_minimap_kind(kinds[i]);
        }
    }
}

/*!
 * @brief 階の素性と光（`FloorIdentity` / `LightingState`）。
 * @details HD2D は `floor` で見た目の種を安定させ（同じ階へ戻ると同じ絵）、
 * `lighting` で方向光を作る。**空だと階の意匠が出入りのたびに入れ替わる。**
 */
void fill_floor_and_lighting(GameFrame &frame)
{
    sq_floor_info info{};
    if (sq_read_floor_info(&info) != SQ_OK) {
        frame.lighting.player_lit.clear(); // 光の種類が分からないなら、照らされたマスも送らない
        return;
    }

    /*
     * Sil-Q に世界地図は無い。ダンジョンは 1 本きりなので `dungeon_id` は 0 のまま、
     * 階の見分けは `dun_level` だけで足りる。`generated_turn` も **0 のまま**にする
     * ——Sil-Q の階に「作られた turn」を持つ欄が無い。0 に倒すと「同じ階は毎回同じ絵」
     * になる（潜り直すと作り直されるのに前の見た目が出る、という穴はあるが、
     * 毎フレーム入れ替わるより害が小さい。幻想蛮怒と同じ判断）。
     */
    frame.floor.dungeon_id = 0;
    frame.floor.dun_level = info.dun_level;
    frame.floor.kind = info.kind;
    frame.floor.town_id = 0;
    frame.floor.wild_mode = false;
    if (info.place_name[0] != '\0') {
        frame.floor.place_name_utf8 = to_utf8(info.place_name);
    }

    /*
     * **昼夜が無い。** Sil-Q は地下しか無く、時刻の概念を持たない
     * （`extract_day_hour_min()` に当たるものが無い）。真夜中・夜として出し、
     * 明かりはプレイヤの光源だけにする——`day_minute` を昼に倒すと、
     * HD2D が地下に太陽光の方向光を差し込む。
     */
    frame.lighting.day_minute = 0;
    frame.lighting.daytime = false;
    frame.lighting.light_radius = info.light_radius;
    /*
     * 光の届く範囲。
     * 種類は `SQ_LIGHT_*`（`LightingState::light_kind` と同じ値。5 = 暗黒側）。
     * 光が無い（0）なら照らされたマスも送らない（画面側は従来どおり点光源の減衰だけで描く）。
     */
    frame.lighting.light_kind = static_cast<std::uint8_t>(info.light_kind);
    if (info.light_kind == SQ_LIGHT_NONE) {
        frame.lighting.player_lit.clear();
    }
}

/*!
 * @brief 周囲の内訳を `frame.surroundings` へ写す。
 * @details 数えるのは C 側（`sq_read_surroundings`）——地形の意味を知っているのは
 * `cave_feat` を読めるあちらだけである。ここは写すだけ。
 *
 * **Sil-Q で埋まるのは壁と瓦礫の 2 つだけ**である（草も水も木も溶岩も存在しない）。
 * 残りは 0 のままで、画面側はその層を鳴らさない。**`radius` が 0 なら
 * 「数えていない」**（ゲーム前・階が無い）で、そのときは層を 1 つも使わない
 * （`presentation/frame/game_frame.h` の `SurroundingsView::valid`）。
 */
void fill_surroundings(GameFrame &frame)
{
    frame.surroundings = SurroundingsView{};

    sq_surroundings s{};
    if (sq_read_surroundings(&s) != SQ_OK) {
        return;
    }
    auto &out = frame.surroundings;
    out.rock = s.rock;
    out.wall = s.wall;
    out.radius = s.radius;
    out.counted = s.counted;
}

/*!
 * @brief 溜まっている音を汲んで `frame.sounds` へ移す。
 * @details **汲んだら消える**（`sq_sound_take`）。音は状態ではなく一度きりの出来事で、
 * 載ったその 1 枚を逃すと二度と来ない。
 *
 * `heard_*` と `path` は載せない——いまは音の道のりを数えていないので 0 のままにする。
 * 画面側は 0 を「計算していない」と読んで実際のマスと直線距離へ落ちる
 * （`presentation/frame/sound_event.h` の約束）。**コア名の分岐は生えない。**
 * 騒音の場（`cave_cost[FLOW_PLAYER_NOISE]`）から道のりを載せるのは P4 の仕事である。
 */
void fill_sounds(GameFrame &frame)
{
    sq_sound_event raw[32];
    const int n = sq_sound_take(raw, 32);

    frame.sounds.clear();
    if (n <= 0) {
        return;
    }
    frame.sounds.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const sq_sound_event &e = raw[i];
        if (e.name[0] == '\0') {
            continue;
        }
        SoundEvent s{};
        s.name = e.name;
        s.y = static_cast<std::int16_t>(e.y);
        s.x = static_cast<std::int16_t>(e.x);
        frame.sounds.push_back(std::move(s));
    }
}

/*!
 * @brief 溜まっている魔法・矢の効果を `frame.projections`・`frame.missiles` へ移す（`named_attack_fx.h`）。**汲んだら空になる**。
 * @details 矢は 1 歩ごとに仮置きされ、命令待ち（`sq_awaiting_command()`＝動きの終わり）で確定する。
 * 入力待ちの 10ms ごとのフレームで、飛行中の 1 発が途中で割れないようにするため。
 */
void fill_attack_fx(GameFrame &frame)
{
    attack_fx::glue::take_into(frame, sq_awaiting_command() != 0);
}

} // namespace

void set_view_size(int w, int h)
{
    if ((w > 0) && (h > 0)) {
        g_view_w = w;
        g_view_h = h;
    }
}

void set_camera_follow_player(bool follow)
{
    g_camera_follow_player = follow;
}

void set_tile_manifest(const TileManifest *manifest)
{
    g_manifest = manifest;
}

int status_selftest(char *err, int cap, int *checked)
{
    int failures = 0;
    if (checked != nullptr) {
        *checked = 0;
    }
    const auto fail = [&](const std::string &what) {
        ++failures;
        if ((failures == 1) && (err != nullptr) && (cap > 0)) {
            std::snprintf(err, static_cast<std::size_t>(cap), "%s", what.c_str());
        }
    };

    // 画面側の表（環境変数に絶対パスがあるときだけ読む。関門が渡す）。
    std::set<std::string> table;
    if (const char *path = std::getenv("HENGBAND_STATUS_ICON_TABLE")) {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t open = line.find("{ \"");
            if (open == std::string::npos) continue;
            const std::size_t close = line.find('"', open + 3);
            if (close != std::string::npos) table.insert(line.substr(open + 3, close - (open + 3)));
        }
    }
    const auto check_key = [&](const std::string &key) {
        if (table.empty()) return;
        if (checked != nullptr) ++*checked;
        if (table.count(key) == 0) fail("key not in the icon table: " + key);
    };

    // 常時の効果の表が、旗の桁を欠けなく 1 度ずつ覆っている（表の桁と `SQ_PERM_*` の個数が合う）。
    unsigned int covered = 0;
    for (const PermRow &row : kPermRows) {
        if ((row.bit < 0) || (row.bit >= SQ_PERM_COUNT)) fail(std::string("perm row out of range: ") + row.key);
        covered |= 1u << row.bit;
        check_key(row.key);
    }
    for (int bit = 0; bit < SQ_PERM_COUNT; ++bit) {
        if ((covered & (1u << bit)) == 0u) fail("perm bit without a row: " + std::to_string(bit));
    }
    for (const char *key : kSongKeys) check_key(key);

    // 材料 → アイコン。実機で作りにくい組み合わせも含めて、1 つずつ照合する。
    const auto base = []() {
        sq_status_state st;
        std::memset(&st, 0, sizeof(st));
        st.in_game = 1;
        st.song1 = -1;
        st.song2 = -1;
        return st;
    };
    const auto describe = [](const std::vector<StatusEffectView> &list) {
        std::string text;
        for (const StatusEffectView &e : list) {
            text += e.key + ":" + std::to_string(static_cast<int>(e.kind)) + " ";
        }
        return text;
    };
    const auto label_is_utf8 = [](const std::string &text) {
        for (std::size_t i = 0; i < text.size();) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const std::size_t n = (c < 0x80) ? 1 : ((c >= 0xC2) && (c <= 0xDF)) ? 2 : ((c >= 0xE0) && (c <= 0xEF)) ? 3 : ((c >= 0xF0) && (c <= 0xF4)) ? 4 : 0;
            if ((n == 0) || (i + n > text.size())) return false;
            for (std::size_t k = 1; k < n; ++k) {
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
            }
            i += n;
        }
        return true;
    };
    const auto expect = [&](const char *what, const sq_status_state &st, const std::string &want) {
        const std::vector<StatusEffectView> got = build_status_effects(st);
        const std::string text = describe(got);
        if (text != want) {
            fail(std::string(what) + ": want [" + want + "] got [" + text + "]");
        }
        for (const StatusEffectView &e : got) {
            check_key(e.key);
            if (e.label_utf8.empty()) fail(std::string(what) + ": empty label for " + e.key);
            // 名前が UTF-8 として正しい（修-37。CP932 のまま入ると不正なバイト列になる）。日本語層が起きているときは、
            // 歌（原作の名前を `song_label()` が通す。このテストでは ASCII の仮の名前）以外は非 ASCII を含む。
            if (!label_is_utf8(e.label_utf8)) fail(std::string(what) + ": label is not valid UTF-8 for " + e.key);
            const bool ascii_only = std::all_of(e.label_utf8.begin(), e.label_utf8.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
            if ((sq_lang_enabled() != 0) && ascii_only && (e.key.compare(0, 8, "sq_song_") != 0)) {
                fail(std::string(what) + ": label is not Japanese for " + e.key);
            }
        }
    };

    expect("nothing", base(), "");
    {
        sq_status_state st = base();
        st.in_game = 0;
        st.blind = 1;
        st.perm_mask = ~0u;
        expect("not in game", st, "");
    }
    { sq_status_state st = base(); st.speed_stage = 1; expect("fast", st, "haste:0 "); }
    { sq_status_state st = base(); st.speed_stage = -1; expect("slow", st, "slow:1 "); }
    { sq_status_state st = base(); st.stun_stage = 1; expect("stun", st, "stun:1 "); }
    { sq_status_state st = base(); st.stun_stage = 3; expect("knocked out", st, "stun:1 "); }
    for (int stage = 1; stage <= 4; ++stage) {
        sq_status_state st = base();
        st.hunger_stage = stage;
        expect("hunger", st, "hungry:1 ");
    }
    { sq_status_state st = base(); st.blind = 5; expect("blind", st, "blind:1 "); }
    { sq_status_state st = base(); st.confused = 5; expect("confused", st, "confused:1 "); }
    { sq_status_state st = base(); st.afraid = 5; expect("afraid", st, "afraid:1 "); }
    { sq_status_state st = base(); st.poisoned = 5; expect("poisoned", st, "poisoned:1 "); }
    { sq_status_state st = base(); st.cut_stage = 1; expect("bleeding", st, "cut:1 "); }
    { sq_status_state st = base(); st.cut_stage = 2; expect("mortal wound", st, "cut:1 "); }
    { sq_status_state st = base(); st.entranced = 3; expect("entranced", st, "paralyzed:1 "); }
    { sq_status_state st = base(); st.smithing = 9; expect("smithing", st, "sq_smithing:3 "); }
    { sq_status_state st = base(); st.fletching = 9; expect("fletching", st, "sq_fletching:3 "); }
    { sq_status_state st = base(); st.stealth_mode = 1; expect("stealth mode", st, "stealth:0 "); }
    { sq_status_state st = base(); st.image = 9; expect("hallucination", st, "hallucination:1 "); }
    { sq_status_state st = base(); st.tmp_stat[0] = 3; st.tmp_stat[3] = 3; expect("stat up", st, "buildup:0 buildup:0 "); }
    { sq_status_state st = base(); st.tim_invis = 9; expect("see invisible", st, "senseunseen:0 "); }
    { sq_status_state st = base(); st.oppose_fire = 9; st.oppose_cold = 9; st.oppose_pois = 9; expect("oppose", st, "res_fire:0 res_cold:0 res_pois:0 "); }
    { sq_status_state st = base(); st.tmp_per = 9; expect("perception", st, "sq_per_up:0 "); }
    { sq_status_state st = base(); st.rage = 9; expect("rage", st, "berserk:0 "); }
    { sq_status_state st = base(); st.darkened = 9; expect("darkened", st, "sq_darkened:1 "); }
    for (int song = 0; song < kSongCount; ++song) {
        sq_status_state st = base();
        st.song1 = song;
        std::snprintf(st.song1_name, sizeof(st.song1_name), "Song of Test");
        expect("song", st, std::string(kSongKeys[song]) + ":0 ");
    }
    {
        sq_status_state st = base();
        st.song1 = 0;
        st.song2 = 7;
        std::snprintf(st.song1_name, sizeof(st.song1_name), "Song of A");
        std::snprintf(st.song2_name, sizeof(st.song2_name), "Song of B");
        expect("woven themes", st, "sq_song_elbereth:0 sq_song_trees:0 ");
    }
    for (const PermRow &row : kPermRows) {
        sq_status_state st = base();
        st.perm_mask = 1u << row.bit;
        expect("permanent", st, std::string(row.key) + ":4 ");
    }
    if ((checked != nullptr) && !table.empty() && (*checked == 0)) {
        fail("no key was compared with the icon table");
    }
    return failures;
}

int status_perm_row_count()
{
    return static_cast<int>(sizeof(kPermRows) / sizeof(kPermRows[0]));
}

GameFrame capture_frame(unsigned long long frame_id)
{
    GameFrame frame;
    frame.frame_id = frame_id;

    /*
     * メニューの判定。
     *
     * - `menu_open` … 「Term ミラーを前に出す」。全画面の別画面（一覧・鍛冶・
     *   キャラクターシート）と、@ がまだ／もう無い場面。
     * - `pre_game_menu` … 「本文を全面に出す」。@ がまだ無い／死んだとき。
     *
     * @ が出来る前は `menu_open` を必ず立てる。ここを落とすと
     * **`initial_menu()` が画面に 1 文字も出ないまま固まる**（設計 §3.3。
     * タイトルは Sil-Q 自身が Term に描くので、ミラーが出口である）。
     *
     * `((screen & SQ_SCREEN_DUNGEON) == 0)` が効くので、階が載っている間
     * （＝ゲーム中）はミラーが止まり、地図が出る。**この条件を落とすと
     * 立体が 1 枚も描かれない**（幻想蛮怒 P3 で踏んだ穴と同じ形）。
     */
    const int screen = sq_screen_flags();
    const bool generated = (screen & SQ_SCREEN_GENERATED) != 0;
    const bool dead = (screen & SQ_SCREEN_DEAD) != 0;
    frame.menu_open = !generated || dead || ((screen & (SQ_SCREEN_ICKY | SQ_SCREEN_XTRA)) != 0)
        || ((screen & SQ_SCREEN_DUNGEON) == 0);
    frame.pre_game_menu = !generated || dead;
    frame.game_over = dead; //!< 死亡・引退のあとの画面。題と phase を誕生と分ける（修-10）。
    presentation::apply_native_targeting_frame(frame);

    if(dead && generated && sq_native_look_map_active()){frame.menu_open=false;frame.pre_game_menu=false;frame.game_over=false;}

    /*
     * `title_screen` は「タイトル画を敷いてよい間」。Sil-Q には
     * `display_introduction()` と `initial_menu()` があるが、宣言口が無いので
     * **@ が出来るまで**をそれとみなす（幻想蛮怒と同じ扱い）。
     */
    frame.title_screen = !generated;

    /*
     * Term ミラーは **`menu_open` のときだけ**積む。
     * **毎フレーム積んではいけない**——画面側の `frame_shows_map()` は
     * `menu_term_lines.empty()` だけを見ており、積んであると「いま地図ではない」と
     * 判断して**立体を 1 枚も描かず** Term の写しを窓に出す。
     */
    if (frame.menu_open) {
        fill_term_mirror(frame);
        //! 品選びの一覧をカーソルで押せるようにする（M0.5 ①。`sq_menu.h`）。
        (void)fill_menu_choices(frame);
    }
    /*
     * **`menu_open` に関わらず毎フレーム。**`[y/n]` と個数入力は `screen_save()` を
     * 通らない＝ icky にならないので、Term ミラーには 1 行も出てこない
     * （M0.5 §1.5 の「いちばん踏みやすい罠」）。行 0 を直に読む。
     */
    {
        std::vector<MenuChoice> choices;int row=0,column=0,selected=0,key=0;const char *label=nullptr;
        for(int index=0;sq_save_menu_entry(index,&row,&column,&selected,&key,&label);++index) {
            MenuChoice choice;choice.line_index=-1;choice.key=key;choice.selected=selected!=0;choice.description_utf8=sjis_to_utf8(std::string(sq_save_menu_description()));
            choice.label_utf8=sjis_to_utf8(std::string(label));
            for(size_t i=0;i<frame.menu_term_lines.size();++i)if(frame.menu_term_lines[i].source_row==row) {
                choice.line_index=int(i);choice.span_begin=int(presentation::modern_column_byte(frame.menu_term_lines[i].text_utf8,column));
                choice.span_len=int(frame.menu_term_lines[i].text_utf8.size())-choice.span_begin;
                // A second save column is a separate choice.
                const auto gap=frame.menu_term_lines[i].text_utf8.find("  ",choice.span_begin);
                if(gap!=std::string::npos)choice.span_len=int(gap)-choice.span_begin;
                break;
            }
            if(choice.selected){frame.menu_core_cursor=choice;frame.menu_core_cursors.assign(1,choice);}
            choices.push_back(std::move(choice));
        }
        if(!choices.empty())frame.menu_choices=std::move(choices);
    }
    fill_row0_prompt(frame);

    fill_hud(frame);
    fill_messages(frame);
    fill_status_effects(frame); //!<。名前は原作の最下段の字・歌は `b_name`
    fill_attack_fx(frame); //!< 
    fill_status_col(frame);
    fill_map(frame);
    fill_floor_and_lighting(frame);
    /* 効果音。汲んだら消える。 */
    fill_sounds(frame);
    /* 周囲の地形の内訳（環境音の層）。 */
    fill_surroundings(frame);
    /*
     * サブパネル 7 枚（M0.5 ②。`sq_sub_panels.h`）。**種類を当てるのもここ**——
     * Term の活性を掻き回す操作は capture の 1 か所に閉じ込めてある。
     */
    fill_sub_panels(frame);
    /*
     * 文字入力（W1）。コアが**自由文字入力の待ち**に入っている間だけ立つ
     * ——`util.c` の `askfor_aux()` と `askfor_name()` を `sq_text_input_set` で
     * 囲んである（`term_get_string()` は前者を呼ぶので自動で覆われる）。
     *
     * **これが無いと IME が切れないまま遊ぶことになる。** 画面側は旗が偽に落ちた
     * ときに `SDL_StopTextInput()` を呼ぶ（`hd2d_app.cpp:13198`）ので、旗を
     * 一度も立てないと**その枝が一度も成立しない**——一度変換が始まると以後の
     * 打鍵が合成へ吸われる（SH-07）。非 ASCII を送る門も同じ旗である（SH-31）。
     */
    frame.text_input_active = (sq_text_input_active() != 0);
    if (frame.text_input_active && frame.menu_open) {
        bool saw_enter_name = false;
        bool saw_tab_random = false;
        for (const auto &line : frame.menu_term_lines) {
            // These key labels deliberately stay ASCII in both English and
            // Japanese Sil-Q.  Matching the translated prose itself here is
            // unsafe because the adapter source is transcoded to CP932 for
            // legacy builds before compilation.
            if (line.text_utf8.find("Enter ") != std::string::npos) saw_enter_name = true;
            if (line.text_utf8.find("Tab ") != std::string::npos) saw_tab_random = true;
        }
        const bool player_name_screen = saw_enter_name && saw_tab_random;
        frame.player_name_input_active = player_name_screen;
        // Fix 57: the name is edited by the core itself now (askfor_name() takes japanese), so no side-store edit is started.
        // Characters made by the older side-store are still read: see the HUD name above and the save picker.
    }
    /*
     * **いま次の命令を待っているか**（2026-08-23。`sq_awaiting_command()` の註記）。
     * 画面側はこの旗が立っているときだけ A を「コマンドメニューを開く」に使う。
     * 立てないと A は今までどおり決定（`\r`）のままで、Sil-Q では何も起きない。
     */
    frame.awaiting_command = (sq_awaiting_command() != 0);

    /* The top command menu and a child prompt must never own the same input.
     * Re-check the fully interpreted frame rather than relying only on the
     * legacy inkey flag. */
    if (frame.menu_open || frame.pre_game_menu || frame.text_input_active
        || frame.numeric.active || !frame.prompt.choices.empty()) {
        frame.awaiting_command = false;
    }

    presentation::apply_native_targeting_frame(frame);
    return frame;
}

bool attack_fx_ready()
{
    return attack_fx::glue::ready(sq_awaiting_command() != 0);
}

} // namespace sq

/* ==================================================================== 魔法・投射物の効果（C から呼ばれる入口。`sq_shim.c`） */

extern "C" void sq_named_fx_projection(int origin, int shape, const char *attr, int rad, int src_y, int src_x, int impact_y,
    int impact_x, int flags, const int *path, int path_n, const int *area, int area_n)
{
    attack_fx::glue::projection(&attack_fx::silq_look_name, origin, shape, attr, rad, src_y, src_x, impact_y, impact_x, flags,
        path, path_n, area, area_n);
}

extern "C" void sq_named_fx_missile_step(int first, int kind, int item_id, const char *brand, int src_y, int src_x, int y, int x)
{
    attack_fx::glue::missile_step(first, kind, item_id, brand, src_y, src_x, y, x);
}

extern "C" void sq_named_fx_missile_hit(void)
{
    attack_fx::glue::missile_hit();
}
