/*!
 * @file fc_menu.cpp
 * @brief `fc_menu.h` の実装（M0.5 FH-01。設計 §14.2(b)）。
 *
 * 手本は `gensoband/adapter/gb_menu.cpp`。判定の理屈（文字列の照合・
 * 「画面に出る文字列そのもの」で引く）は写し、文言は Frox の実読で差し替えた:
 *
 * | 何 | Frox の綴り | 出どころ |
 * | --- | --- | --- |
 * | はい/いいえ | 末尾 `[y/n]`・`[Y/n]`・`[(O)k/(C)ancel]` | `util.c:3041` / `:3074-3084` |
 * | 個数 | `Quantity (1-%d): `（一般形 `(lo-hi): `） | `util.c:3246` |
 * | 札 1 | ` a) 品名`（品・呪文・誕生・menu.c） | `inv.c` の ` %c)`・`monspell.c:5537` |
 * | 札 2 | `(1) General Options`（オプションの根） | `cmd4.c:2765` の `(%c) %s` |
 * | 札 3 | `[b/p/g] Buy`（doc の命令列。`/` 区切り） | `shop.c:1704` ほか |
 * | カーソル | `"> "`＋枠 `+----` | `util.c` `inkey_from_menu()` |
 *
 * ## 判定の緩さ（設計 §14.2(b) の判断。ここに残す）
 * 幻想蛮怒は「画面ごとに名指し」で絞ったが、**Frox の doc UI は知らない字を黙って
 * 無視する**ので、ミラーが開いている間の全行走査でも害が小さい。M0.5 は全行走査で
 * 入れ、誤爆が実測されたら名指しの表に絞る。プロンプト（`[y/n]`・数値）だけは
 * 従来どおり**画面に出る文字列そのもの**の照合で、緩めていない——行 0 は
 * メッセージ行でもあり、緩い解釈は普通の文言の最中にキーを飲み込む。
 *
 * ## 文字コード
 * M0 の Frox は純 ASCII（`frox/UPSTREAM.md`）なので変換が無く、バイト位置＝桁位置。
 * M1 で CP932 が入るときは `sq_menu.cpp` の「桁は素のセルで見る」を持ち込むこと。
 */

#include "fc_menu.h"
#include "bridge/input_event_adapter.h"
#include "bridge/symbol_prompt.h"
#include "bridge/look_screen_cover.h"
#include "frame/message_more.h"
#include "frame/targeting_codec.h"
#include "bridge/modern_content.h"
#include "bridge/native_store_menu.h"
#include "fc_lang_c.h"
#include "frame/message_more.h"
#include "bridge/modern_content.h"

#include "fc_text.h" //!< CP932 → UTF-8（設計 §3.1）

#include "fc_shim.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/* fc_targeting_menu.inl（狙いの上書きが原作の 1 ファイルに取り込む）が控える、呪文の一覧の行の位置。 */
extern "C" int fc_book_list_count(void);
extern "C" int fc_book_list_row(int index);

namespace fc {

namespace {

//! Term 1 行の受け皿（主 Term は 80 桁だが余裕を取る。`fc_frame.cpp` の `kMaxCols` と同値）。
constexpr int kRowBytes = 256;

//! 積む選択肢の上限（暴走よけ。品は 26 枠＋命令列なので普通は届かない）。
constexpr std::size_t kMenuChoiceMax = 64;

/*!
 * @brief アダプタが自分で書く英語の名札（`[y/n]` の「Yes」など）の日本語訳。
 * @details 原作の字は訳のカタログ（`fc_tr()`）が受け持つが、ここの名札は**アダプタが
 * 組み立てる字**なので、カタログには鍵が無い。日本語層が起きているとき（`fc_lang_enabled()`）だけ、
 * この表で引く。引けなければ英語のまま（設計 §1 制約 4）。
 *
 * 一般のカタログへ `Yes` `No` `Cancel` のような裸の 1 語を入れると、画面の別の字
 * （オプションの値など）まで巻き添えにするので、**別の表**にしてある。
 * 訳は幻想蛮怒（`gb_menu.cpp` の `GB_OPT`）と、カタログの既訳（「放す」「人物調書」）に合わせた。
 */
struct UiLabel {
    const char *en;
    const char8_t *ja;
};

const UiLabel kUiLabels[] = {
    /* 確認（`[y/n]` ほか） */
    { "Yes", u8"はい" },
    { "No", u8"いいえ" },
    { "Auto", u8"以後自動で" },
    { "Always", u8"以後常に" },
    { "Ok", u8"OK" },
    { "Cancel", u8"キャンセル" },
    { "Cancel (Esc)", u8"取り消し (Esc)" },
    { "Confirm", u8"確定" },
    { "Continue", u8"続ける" },
    { "Confirm (@)", u8"確定 (@)" },
    { "Yes, and restart", u8"はい、そして再開" },
    { "Reconstruct elsewhere", u8"別の場所で組み直す" },
    { "View or dump screen", u8"画面を見る／書き出す" },
    { "Decline help", u8"助けを断る" },
    { "Keep Frox", u8"Frox を残す" },
    { "Replace with Frog", u8"Frog で置き換える" },
    { "Import renamed", u8"名前を変えて取り込む" },
    { "Cancel import", u8"取り込みをやめる" },
    { "Evaporate potion", u8"薬を気化させる" },
    { "Evaporate infusion", u8"調合薬を気化させる" },
    { "Show item list", u8"アイテム一覧を表示" },
    { "Hide item list", u8"アイテム一覧を隠す" },
    { "Next item", u8"次のアイテム" },
    { "Take this item", u8"このアイテムを取る" },
    { "All items", u8"全部" },
    { "Create shots", u8"弾を作る" },
    { "Create arrows", u8"矢を作る" },
    { "Create bolts", u8"ボルトを作る" },
    /* 見る（狙い）の帯 */
    { "Look: ", u8"見る: " },
    /* 移動の目標のマスを選ぶ問い（`` ` `` と `^A F`。`tgt_pt()`） */
    { "Travel: ", u8"移動先を選ぶ: " },
    { "Select this square", u8"このマスを選ぶ" },
    { "Next down stairs", u8"次の下り階段" },
    { "Next up stairs", u8"次の上り階段" },
    { "Next monster", u8"次のモンスター" },
    { "Journey targets", u8"目的地の一覧" },
    { "LB/RB: select action  A/Enter: run  D-pad: move cursor  B: cancel", u8"LB/RB: 操作を選ぶ  A/Enter: 実行  十字キー: カーソル移動  B: 取消" },
    { "Recall", u8"思い出を見る" },
    { "Next interesting", u8"次の注目点" },
    { "Previous interesting", u8"前の注目点" },
    { "Recenter on player", u8"自分の位置へ戻す" },
    { "Nearest interesting", u8"最も近い注目点" },
    { "Manual/current mode", u8"手動／現在のモード" },
    { "Select target", u8"ターゲットに選ぶ" },
    { "Targeting help", u8"ターゲットのヘルプ" },
    { "Toggle monster visibility", u8"モンスターの表示切替" },
    { "Set travel destination", u8"移動先に設定" },
    { "LB/RB: select action  A/Enter: run  D-pad: move look cursor", u8"LB/RB: 操作を選ぶ  A/Enter: 実行  十字キー: カーソル移動" },
    { "World Map: ", u8"広域の地図: " },
    { "Look around", u8"見回す" },
    { "LB/RB: select action  A/Enter: run  D-pad: travel", u8"LB/RB: 操作を選ぶ  A/Enter: 実行  十字キー: 移動" },
    /* 呪文・道具・一覧の補助命令（`collect_aux_choices`） */
    { "Use mode", u8"使用モード" },
    { "Browse / describe", u8"閲覧／説明" },
    { "Choose / use", u8"選択／使用" },
    { "Label mode", u8"名札モード" },
    { "Character sheet", u8"人物調書" },
    { "Clear inactive labels", u8"未使用の名札を消去" },
    { "Cycle device type", u8"道具の種類を切り替え" },
    { "Show wands", u8"魔法棒を表示" },
    { "Show staves", u8"杖を表示" },
    { "Show rods", u8"ロッドを表示" },
    { "Exchange two slots", u8"2 つの枠を入れ替え" },
    { "Inscribe a slot", u8"枠に銘を刻む" },
    { "Damage display", u8"ダメージ表示" },
    { "Stop all spells", u8"すべての呪文を止める" },
    { "Recall / inspect monster", u8"モンスターの記憶を見る" },
    { "Probe monster", u8"モンスターを調査" },
    { "Rename selected pet", u8"選択したペットの名前を変える" },
    { "Set current target", u8"ターゲットに設定" },
    { "Travel to selected monster", u8"選択したモンスターへ移動" },
    { "Widen list", u8"一覧を広げる" },
    { "Narrow list", u8"一覧を狭める" },
    { "Help", u8"ヘルプ" },
    { "Toggle ammunition", u8"弾薬の表示切替" },
    { "Toggle identified items", u8"鑑定済みアイテムの表示切替" },
    { "Toggle sensed items", u8"感知したアイテムの表示切替" },
    { "Toggle unsensed items", u8"未感知アイテムの表示切替" },
    { "Toggle stairs", u8"階段の表示切替" },
    { "Toggle unwanted items", u8"不要なアイテムの表示切替" },
    { "Inspect selected item", u8"選択したアイテムを調べる" },
    { "Travel to selected item", u8"選択したアイテムへ移動" },
    { "Rename a pet", u8"ペットの名前を変える" },
    { "Dismiss a pet", u8"ペットを放す" },
    { "Dismiss all unnamed pets", u8"名前の無いペットを全部放す" },
    { "Recall / inspect form", u8"形態の記憶を見る" },
    { "Change information page", u8"情報ページを切り替え" },
    { "Next form group", u8"次の形態グループ" },
    { "Increase bonus amount", u8"ボーナス量を増やす" },
    { "Decrease bonus amount", u8"ボーナス量を減らす" },
    /* 旅の目的地・キー割り当ての画面 */
    { "Go back", u8"戻る" },
    { "Return", u8"戻る" },
    { "Previous custom key", u8"前のキー割り当て" },
    { "Next custom key", u8"次のキー割り当て" },
    { "Switch Replace / Add preview", u8"置換／追加のプレビューを切り替え" },
    { "Toggle all affected / differences only", u8"影響するすべて／差分のみを切り替え" },
    { "Reset macros", u8"マクロを初期化" },
    { "Reset current keyset", u8"現在のキー設定を初期化" },
    { "Reset both keysets", u8"両方のキー設定を初期化" },
    { "Reset everything", u8"すべて初期化" },
    { "Reset everything (default)", u8"すべて初期化（既定）" },
    { "Cancel reset", u8"初期化を取りやめる" },
};

/*!
 * @brief 名札を UTF-8 で返す。日本語層が寝ている、または表に無ければ英語のまま。
 */
std::string ui_label(const char *en)
{
    if (en == nullptr) {
        return std::string();
    }
    if (fc_lang_enabled() != 0) {
        for (const UiLabel &entry : kUiLabels) {
            if (std::strcmp(entry.en, en) == 0) {
                return std::string(reinterpret_cast<const char *>(entry.ja));
            }
        }
    }
    return std::string(en);
}

/*!
 * @brief `<名札> <1 字>` の形の名札（「Inspect / describe a」ほか）を組む。
 * @param en_head 英語の頭（末尾の空白まで。`Inspect / describe `）
 * @param ja_head 日本語の頭（1 字の前）。語順が違うので、頭と後ろを別に持つ
 * @param ja_tail 日本語の後ろ（1 字の後ろ。`を調べる`）
 */
std::string ui_label_with_key(const char *en_head, const char8_t *ja_head, const char8_t *ja_tail, char key)
{
    std::string out;
    if (fc_lang_enabled() != 0) {
        out = reinterpret_cast<const char *>(ja_head);
        out.push_back(key);
        out += reinterpret_cast<const char *>(ja_tail);
        return out;
    }
    out = en_head;
    out.push_back(key);
    return out;
}

//! セル 1 個ぶんを空白とみなすか。**`fc_frame.cpp` の同名の関数と同じ規則**。
bool cell_is_blank(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return (u == ' ') || (u < 0x20) || (u == 0x7F);
}

/*!
 * @brief 主 Term の 1 行を読む（制御文字は空白に倒す）。
 * @details 行 0〜2 は**ミラーが開いていなくても**読める（`fc_term_row()` は
 * `Term->scr` を直に見る）。プロンプトが icky にならない以上、ここが唯一の読み口。
 */
std::string term_row_text(int y,int first=0,int last=kRowBytes)
{
    char cells[kRowBytes];
    std::memset(cells, ' ', sizeof(cells));
    const int n = fc_term_row(y, cells, nullptr, kRowBytes);
    if (n <= 0) {
        return std::string();
    }
    /*
     * CP932 のまま組み、最後に 1 回だけ変換する（設計 §3.2）。
     * **純 ASCII なら 1 バイトも触らない**——英語のときの答えを M0 と変えない。
     *
     * @note ここが返す字は**そのまま `prompt.text_utf8` に載る**。品名が日本語に
     * なると（J3）確認の問い（`Really sell ... [y/n]`）に 2 バイト文字が混ざるので、
     * この 1 か所を通しておかないと帯が化ける。
     * 綴りの照合（`[y/n]` `(1 to 5): `）は ASCII なので、変換しても答えは変わらない。
     */
    std::string sys;
    sys.reserve(static_cast<std::size_t>(n));
    bool wide = false;
    // Crop native cells before conversion: CP932 symbols such as the
    // multiplication sign occupy two cells even outside CJK Unicode ranges.
    for (int x = std::max(0,first); x < std::min(n,last); ++x) {
        const auto lead = static_cast<unsigned char>(cells[x]);
        if(lead>=0x80)wide=true; // Half-width kana also need UTF-8 conversion.
        if (fc::is_sjis_lead(lead) && ((x + 1) < std::min(n,last))) {
            sys.push_back(cells[x]);
            sys.push_back(cells[x + 1]);
            wide = true;
            ++x;
            continue;
        }
        if (fc::is_sjis_lead(lead)) {
            sys.push_back(' '); // 行の端で割れた。**半端なバイトは運ばない**
            continue;
        }
        sys.push_back(cell_is_blank(cells[x]) ? ' ' : cells[x]);
    }

    if (!wide) {
        return sys;
    }
    std::string out = fc::sjis_to_utf8(sys);
    if (out.empty() && !sys.empty()) {
        out.assign(sys.size(), ' ');
    }
    return out;
}

//! 末尾の空白を落とす。プロンプトの照合はどれも「末尾から」なので先に均す。
std::string trim_right(const std::string &raw)
{
    std::string text = raw;
    while (!text.empty() && (text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

bool is_ascii_alpha(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return ((u >= 'a') && (u <= 'z')) || ((u >= 'A') && (u <= 'Z'));
}

bool is_ascii_alnum(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return ((u >= '0') && (u <= '9')) || is_ascii_alpha(c);
}

/*!
 * @brief `] ` の直後に来る**札の頭**として認めてよい字か（札 3 の担保）。
 * @details 英字のほか、**2 バイト文字の先頭バイト**（UTF-8 の 0x80 以上）も認める。
 *
 * M1 で命令列が日本語になり（`[b/p/g] 買う`）、英字だけを見ていたせいで
 * **店の命令列が 1 つも札にならず、カーソルが品行へ落ちていた**
 * （実機で見つけた（2026-08-28）「店のメニューで初めから商品リストにカーソルがあり、
 * コントローラーではコマンドが使えない」）。
 *
 * **数字と記号は認めない。** 防具の `[1] (+2)` を拾わない担保はそこに残る。
 */
bool is_choice_label_head(char c)
{
    return is_ascii_alpha(c) || (static_cast<unsigned char>(c) >= 0x80);
}

/* ================================================ 画面の見分けに使う字（英語と日本語） */

/*!
 * @brief `u8` の字を `text_utf8` と照らせる `const char *` として読む。
 */
const char *utf8_of(const char8_t *text)
{
    return reinterpret_cast<const char *>(text);
}

/*!
 * @brief 画面の字に、原作の英語か、日本語層（`--lang=ja`）が訳した字のどちらかが含まれるか。
 * @param en 原作の英語の字（`frox/src` に書いてある綴り）
 * @param ja_text 訳のカタログ（`frox/lang/ja/ui/messages.ja.txt`）が画面へ出す字（UTF-8）
 * @details 補助の選択肢の見分けは**画面に出る問いの文**で行う。日本語層は問いを訳して画面へ
 * 出すので、英語だけを探すと日本語のときに 1 つも見つからず、ボタンが出なかった。
 * 日本語層が寝ているときは `ja_text` は画面に出ないので、英語の動きは変わらない。
 *
 * 日本語の字はカタログから写した。**書式（`%s`）をまたぐ訳は、またぐ前後の変わらない部分だけ**を使う
 * （`使用する%s（'?' 閲覧 …` なら `'?' 閲覧`）。字が合っているかは、カタログの鍵と突き合わせる検査
 * （`audit_semantics_selftest()` の日本語の問い）と、日本語で実際に画面を出す検査で押さえる。
 */
bool has_text(const std::string &text, const char *en, const char8_t *ja_text)
{
    return (text.find(en) != std::string::npos) || (text.find(utf8_of(ja_text)) != std::string::npos);
}

//! `has_text()` の「頭が一致」版。
bool starts_text(const std::string &text, const char *en, const char8_t *ja_text)
{
    return text.starts_with(en) || text.starts_with(utf8_of(ja_text));
}

/* ==================================================== プロンプト（`[y/n]` と個数） */

/*!
 * @brief 1 行が `y/n` 系のプロンプトなら選択肢へ持ち上げる。
 * @details 表は**画面に出る文字列そのもの**（見出しの表）。`get_check()` の色タグ
 * （`<color:y>[y/n]</color>`）は Term に出ないので、照合は素の `[y/n]` でよい
 * （§14.1 の事実 4）。`[(O)k/(C)ancel]` は rogue 式配列のときだけ出る
 * （`util.c:3066` で外している）が、表に置いておいて害は無い。
 */
bool parse_prompt_line(const std::string &raw, PromptBar &out)
{
    out = PromptBar{};

    const std::string text = presentation::strip_message_repeat_suffix(trim_right(raw));
    if (text.empty()) {
        return false;
    }

    if (text.find("<dir>") != std::string::npos && text.find("q,") != std::string::npos && text.find("p,") != std::string::npos) {
        // 見る・狙うの途中で `r`（モンスターの思い出）などが画面を退避して重なっている間は、帯を作らない（`look_screen_cover.h`）。
        if (presentation::look_screen_cover().covered(fc_screen_depth())) {
            out = PromptBar{};
            return false;
        }
        const auto add=[&](int key,const char *label){PromptChoice c{};c.key=key;c.label_utf8=ui_label(label);if(!out.text_utf8.empty())out.text_utf8+="  ";out.text_utf8+='[';c.begin=static_cast<int>(out.text_utf8.size());out.text_utf8+=c.label_utf8;c.len=static_cast<int>(c.label_utf8.size());out.text_utf8+=']';out.choices.push_back(std::move(c));};
        out.text_utf8=ui_label("Look: ");
        /*
         * 見ている物の説明。行 0 の頭にある「<名前> <状態> (距離 N) [r,/,…]」（敵）や「<名前> [r,/,…]」（アイテム・地形）の
         * 最後の `[` より前を、帯の見出しのすぐ後ろへ残す（`xtra2.c` の `target_set_aux()`。アイテム名にも `[` が付くので最後を取る）。
         * 選択肢だけを出すと、何を見ているか帯のどこにも出なかった（調-13）。続きの待ちの印（`-more-`）は説明に含めない。
         */
        {
            std::string subject;
            if(const std::size_t open=text.rfind('[');open!=std::string::npos)subject=text.substr(0,open);
            const auto trim=[](std::string &s){while(!s.empty()&&s.back()==' ')s.pop_back();std::size_t i=0;while(i<s.size()&&s[i]==' ')++i;s.erase(0,i);};
            trim(subject);
            for(const char *const more:{"-more-",reinterpret_cast<const char *>(u8"-続く-")}){const std::string m(more);if(subject.size()>=m.size()&&subject.compare(subject.size()-m.size(),m.size(),m)==0){subject.erase(subject.size()-m.size());trim(subject);}}
            out.text_utf8+=subject;
        }
        if(text.find('+')!=std::string::npos)add('+',"Next interesting");
        if(text.find('-')!=std::string::npos)add('-',"Previous interesting");
        add('p',"Recenter on player");
        if(text.find("m,")!=std::string::npos)add('m',"Nearest interesting");
        if(text.find("o,")!=std::string::npos)add('o',"Manual/current mode");
        if(text.find("t,")!=std::string::npos)add('t',"Select target");
        if(text.find("?,")!=std::string::npos)add('?',"Targeting help");
        if(text.find("x,")!=std::string::npos)add('x',"Toggle monster visibility");
        if(text.find("j,")!=std::string::npos)add('j',"Set travel destination");
        else if(text.find("(,")!=std::string::npos)add('(',"Set travel destination");
        //! 日本語も英語も `[r,/,…]`（モンスターの思い出・アイテムの調査。`xtra2.c` の `target_set_aux()`）。最後に足す（先頭の「次の注目点」が既定の札のまま変わらないように）。見た先に何も無いときは `[q,p,…]` で `r` が無い。
        if(text.find("[r,")!=std::string::npos)add('r',"Recall");
        out.footer_utf8=ui_label("LB/RB: select action  A/Enter: run  D-pad: move look cursor");
        return !out.choices.empty();
    }

    /*
     * **移動の目標のマスを選ぶ問い**（`` ` `` の移動と、デバッグの `^A F`。`xtra2.c` の `tgt_pt()` に `rng = -1` で入る）。
     * 原作はメッセージ行に説明を出して `inkey()` で待つだけで、狙う `*` の仕組み（`tools/targeting`）には載らない
     * （`rng` が負だと素通し）。画面には何も立たず、十字キーも決定も効かなかった（F-frox-201）。
     * 根本の直し（`tools/targeting/overlay.py`）は聞いていないので、アダプタでメッセージ行の文を問いとして出す。
     * 文は長く、メッセージの箱の幅で折り返される。**頭の行**に `To travel, select a target square`（日本語は
     * `移動するには目標のマスを選んで`）が入るので、下の行から探して頭の行で当たる。
     * 決定（A）はスペース＝このマスへ移動。十字キーは軽い問いのあいだ、そのままコアへ方向として届く
     * （見る・広域の地図と同じ）。取消（B）は Esc。
     */
    if (has_text(text, "To travel, select a target square", u8"移動するには目標のマスを選んで")) {
        const auto add = [&](int key, const char *label) {
            PromptChoice c{};
            c.key = key;
            c.label_utf8 = ui_label(label);
            if (!out.text_utf8.empty()) {
                out.text_utf8 += "  ";
            }
            out.text_utf8 += '[';
            c.begin = static_cast<int>(out.text_utf8.size());
            out.text_utf8 += c.label_utf8;
            c.len = static_cast<int>(c.label_utf8.size());
            out.text_utf8 += ']';
            out.choices.push_back(std::move(c));
        };
        out.text_utf8 = ui_label("Travel: ");
        add(' ', "Select this square");
        add('>', "Next down stairs");
        add('<', "Next up stairs");
        add('*', "Next monster");
        add('x', "Toggle monster visibility");
        add('?', "Journey targets");
        out.footer_utf8 = ui_label("LB/RB: select action  A/Enter: run  D-pad: move cursor  B: cancel");
        return true;
    }

    /*
     * **スペースだけを受ける問い**。原作は `msg_prompt(…, " ", PROMPT_FORCE_CHOICE)` で
     * 待ち、**スペース以外（Enter・決定ボタン・Esc）は全部捨てる**。`-more-` と違って
     * 末尾が `-続く-` ではなく、`[y/n]` のような括弧も無いので、これまでは問いとして
     * 拾えず、画面には何も出なかった——**キーボードの無い端末（Android・ゲームパッド）では
     * スペースを送れないので、操作を受け付けなくなった**（気づいたこと 2026-10-09
     * 「戦闘中に操作を受け付けなくなった」。体力が半分を切ると出る
     * `*** LOW HITPOINT WARNING! ***`〔`effects.c:6921`〕。2026-10-06 の配布物でも同じ）。
     * 決定（A / Enter）が**スペース 1 キー**を送る選択肢として立てる。
     * 文は 4 つ（落馬 `cmd5.c:1709`・指輪 `r_ring.c:2062`・闘技場 `bldg.c:296` も同じ）。
     * 行には直前のメッセージも並ぶので、`***` から後ろだけを問いの文にする。
     */
    if (has_text(text, "Press Space to continue", u8"スペースを押して続ける")
        || has_text(text, "Press the space bar to continue", u8"スペースキーで続行")
        || text.find(utf8_of(u8"続けるにはスペースキーを押すこと")) != std::string::npos
        || text.find(utf8_of(u8"Space を押すと続きます")) != std::string::npos
        // 荒野の待ち伏せ（`wild.c` の `ROOM_AMBUSH`）とブーメラン（`py_throw.c:477`）。原作は素の `inkey()` でスペース以外を捨てるので、
        // これを問いにしないとキーボードの無い端末では抜けられない（2026-10-10 の気づいたこと「荒野の地図が真っ暗で全てのキーが効かない」）。
        || text.find(utf8_of(u8"続けるにはスペースを押してください")) != std::string::npos) {
        const auto stars = text.find("***");
        out.text_utf8 = (stars == std::string::npos) ? text : text.substr(stars);
        out.choices.push_back({ ui_label("Continue"), ' ', 0, 0 });
        return true;
    }
    //! 日本語は `files.c:3421` と `samurai.c:961` の 2 か所で、訳が少し違う（どちらも頭が `確認のため '@' を押し`）。
    if (has_text(text, "Please verify SUICIDE by typing the '@' sign", u8"確認のため '@' を押し")) {
        out.text_utf8 = text;
        out.choices.push_back({ ui_label("Confirm (@)"), '@', 0, 0 });
        out.choices.push_back({ ui_label("Cancel (Esc)"), 27, 0, 0 });
        return true;
    }
    if (text.find("Y to confirm") != std::string::npos && text.find("toggle list") != std::string::npos) {
        out.text_utf8 = text;
        out.choices.push_back({ ui_label("Confirm"), 'Y', 0, 0 });
        out.choices.push_back({ ui_label("Show item list"), 'y', 0, 0 });
        out.choices.push_back({ ui_label("Show item list"), 'u', 0, 0 });
        out.choices.push_back({ ui_label("Show item list"), 'U', 0, 0 });
        out.choices.push_back({ ui_label("Show item list"), '?', 0, 0 });
        out.choices.push_back({ ui_label("Cancel"), 'n', 0, 0 });
        out.choices.push_back({ ui_label("Cancel"), 'N', 0, 0 });
        out.choices.push_back({ ui_label("Cancel"), 27, 0, 0 });
        return true;
    }
    //! 日本語は `引き寄せ：…[*,t,q]`（コロンは全角。`spells3.c:1815`）。
    if (starts_text(text, "Fetch: ", u8"引き寄せ：") && text.find("[*,t,q]") != std::string::npos) {
        out.text_utf8 = text;
        //! 札の字は末尾の `[*,t,q]` の中（品名に `*` や `t` があっても拾わない）。
        const int marker = static_cast<int>(text.rfind("[*,t,q]"));
        out.choices.push_back({ ui_label("Next item"), '*', marker + 1, 1 });
        out.choices.push_back({ ui_label("Take this item"), 't', marker + 3, 1 });
        out.choices.push_back({ ui_label("Cancel"), 'q', marker + 5, 1 });
        return true;
    }
    //! 日本語は `[S] 弾を作りますか？` ほか 3 通り（頭が `[S] 弾`。`archer.c:152`〜`:156`）。
    if (starts_text(text, "Create [S]hots", u8"[S] 弾")) {
        out.text_utf8 = text;
        const auto add = [&](const char *marker, const char *label, int key) {
            const auto at = text.find(marker);
            if (at != std::string::npos) {
                out.choices.push_back({ ui_label(label), key, static_cast<int>(at + 1), 1 });
            }
        };
        add("[S]", "Create shots", 'S');
        add("[A]", "Create arrows", 'A');
        add("[B]", "Create bolts", 'B');
        out.choices.push_back({ ui_label("Cancel"), 27, 0, 0 });
        return true;
    }
    //! 錬金術師の気化（`alky.c:696`）。日本語は `[Q] 薬と [m] 調合薬のどちらを気化させますか？`。`Q` は大文字で書かれるが `PROMPT_DEFAULT`（大文字小文字を区別しない）。
    //! 原作は他のキーを「先頭の選択肢」扱いにするので、取消の選択肢は立てない（Esc が薬の気化になってしまう）。
    if (starts_text(text, "Evaporate [Q] Potion or [m] Infusion?", u8"[Q] 薬と [m] 調合薬")) {
        out.text_utf8 = text;
        const auto at_q = text.find("[Q]");
        const auto at_m = text.find("[m]");
        if ((at_q != std::string::npos) && (at_m != std::string::npos)) {
            out.choices.push_back({ ui_label("Evaporate potion"), 'Q', static_cast<int>(at_q + 1), 1 });
            out.choices.push_back({ ui_label("Evaporate infusion"), 'm', static_cast<int>(at_m + 1), 1 });
            return true;
        }
        out = PromptBar{};
    }

    struct Option {
        const char *label; //!< ASCII。M0 の Frox は英語で立つ（設計 §3.6）
        char key;
    };
    struct Pattern {
        const char *suffix;
        Option options[5]; //!< `key == 0` で終端
        const char8_t *suffix_ja = nullptr; //!< 日本語層の訳が綴りの末尾を変えるときだけ（`[q = はい、そして再開]`）
    };
    static const Pattern kPatterns[] = {
        //! 破壊の確認（`obj.c:1287`）。`A` は「以後自動で壊す」（自動拾い登録）。
        { "[y/n/Auto]", { { "Yes", 'y' }, { "No", 'n' }, { "Auto", 'A' }, { nullptr, 0 } } },
        //! 移動の再開（`dungeon.c:4532`）。`A` は「以後聞かずに再開」。
        { "[y/n/Always]", { { "Yes", 'y' }, { "No", 'n' }, { "Always", 'A' }, { nullptr, 0 } } },
        { "[y/n]", { { "Yes", 'y' }, { "No", 'n' }, { nullptr, 0 } } },
        //! `CHECK_DEFAULT_Y`（`util.c:3079`）。n と Esc 以外はすべて「はい」扱い。
        { "[Y/n]", { { "Yes", 'Y' }, { "No", 'n' }, { nullptr, 0 } } },
        { "[Y,n]", { { "Yes", 'Y' }, { "No", 'n' }, { nullptr, 0 } } },
        { "[(O)k/(C)ancel]", { { "Ok", 'O' }, { "Cancel", 'C' }, { nullptr, 0 } } },
        /*
         * 以下は 2026-10-12 に `frox/src` の `msg_prompt(` 全数（約 70 か所）を表と突き合わせて足した綴り（修-28）。
         * **足したのは原作に書いてある完全な綴りだけ**で、末尾の照合は緩めていない。
         */
        //! 引退・自殺の確認（`files.c:3405`）。`q` は「はい、そして同じキャラで再開」。日本語は訳が `[q = はい、そして再開]` になる。
        //! **「いいえ」を先頭に置く**（原作の `keys` も `"nyq"`）——3 択の確認の既定の選択は先頭なので、素の決定でキャラが死なないようにする。
        { "[y/n] [q = yes, and restart]", { { "No", 'n' }, { "Yes", 'y' }, { "Yes, and restart", 'q' }, { nullptr, 0 } },
            u8"[y/n] [q = はい、そして再開]" },
        //! 既定が「いいえ」の確認（`cmd4.c:2396` の `[y/N]`。`PROMPT_DEFAULT` なので大文字小文字は区別されない）。
        { "[y/N]", { { "Yes", 'y' }, { "No", 'N' }, { nullptr, 0 } } },
        //! 絶滅の確認（`spells2.c:2291` ほか 4 か所の `[Y/N]`）。
        { "[Y/N]", { { "Yes", 'Y' }, { "No", 'N' }, { nullptr, 0 } } },
        //! FrogComposband からの取り込みの問い（`files.c:3083`・`:3088`）。
        { "[F/R/n/c]", { { "Keep Frox", 'F' }, { "Replace with Frog", 'R' }, { "Import renamed", 'n' }, { "Cancel import", 'c' },
            { nullptr, 0 } } },
        { "[F/R/c]", { { "Keep Frox", 'F' }, { "Replace with Frog", 'R' }, { "Cancel import", 'c' }, { nullptr, 0 } } },
    };

    for (const Pattern &pattern : kPatterns) {
        std::string suffix = pattern.suffix;
        const auto matches = [&text](const std::string &tail) {
            return (text.size() >= tail.size()) && (text.compare(text.size() - tail.size(), tail.size(), tail) == 0);
        };
        if (!matches(suffix)) {
            if ((pattern.suffix_ja == nullptr) || !matches(utf8_of(pattern.suffix_ja))) {
                continue;
            }
            suffix = utf8_of(pattern.suffix_ja);
        }
        const std::size_t begin = text.size() - suffix.size();

        PromptBar built;
        bool ok = true;
        for (const Option &option : pattern.options) {
            if (option.key == '\0') {
                break;
            }
            const std::size_t at = text.find(option.key, begin);
            if (at == std::string::npos) {
                ok = false; // 表の書き間違い。積むより出さない方が安全
                break;
            }
            PromptChoice choice;
            choice.label_utf8 = ui_label(option.label);
            choice.key = option.key;
            choice.begin = static_cast<int>(at);
            choice.len = 1;
            built.choices.push_back(std::move(choice));
        }
        if (!ok || (built.choices.size() < 2)) {
            continue;
        }
        built.text_utf8 = text;
        out = std::move(built);
        return true;
    }
    return false;
}

/*!
 * @brief 行 0 が数値入力（`Quantity (1-5): 1`）なら最小・最大・現在値を読む。
 * @details 判定は **`(数字-数字): ` があり、その後ろが数字だけ（または空）**であること。
 * `get_quantity()`（`util.c:3246`）が `Quantity (1-%d): ` を出し、`askfor()` が
 * 編集中の数字をその後ろに書く。呼び手が独自の文言を渡しても `(lo-hi): ` の形なら
 * 同じに当たる。品選びの行 0 は**括弧の中が英字**なので当たらない。
 * 幻想蛮怒に在った `(MAX:n)` の枝は Frox に無いので落とした（設計 §14.2(b)）。
 */
bool parse_numeric_input_line(const std::string &raw, NumericInput &out)
{
    out = NumericInput{};

    const std::string text = presentation::strip_message_repeat_suffix(trim_right(raw));
    if (text.empty()) {
        return false;
    }

    /*
     * Cornucopia's loan/deposit editor predates the generic numeric helpers.
     * It paints a bare `Label: 123` field and reads digits/backspace/Enter via
     * inkey_special().  Model those fields as the same semantic number input;
     * the core remains authoritative for its dynamic cap and clamps every
     * digit as it is received.
     */
    for (const char *prefix : { "Request: ", "Pay back: ", "Withdraw: ",
             "Additional deposit: ", "New deposit: " }) {
        const std::string_view p(prefix);
        if (!text.starts_with(p)) {
            continue;
        }
        const std::string tail = text.substr(p.size());
        if (tail.empty() || !std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return false;
        }
        out.active = true;
        out.prompt_utf8 = std::string(p);
        out.min = 0;
        out.max = 999999999;
        out.value = std::atoi(tail.c_str());
        out.digits = 9;
        out.text_len = static_cast<int>(tail.size());
        out.value_begin = static_cast<int>(p.size());
        out.value_len = static_cast<int>(tail.size());
        return true;
    }

    /*
     * 値の欄は空のこともある（問いが出た直後）。`trim_right` が末尾の空白を
     * 落とすので `): ` が `):`  になっている——**両方見る**（FH-15 の検査が捕まえた）。
     */
    std::size_t close = text.rfind("): ");
    std::size_t tail_at = (close == std::string::npos) ? std::string::npos : (close + 3);
    if (close == std::string::npos) {
        if ((text.size() < 2) || (text.compare(text.size() - 2, 2, "):") != 0)) {
            return false;
        }
        close = text.size() - 2;
        tail_at = text.size();
    }
    const std::size_t open = text.rfind('(', close);
    if (open == std::string::npos) {
        return false;
    }
    const std::string inside = text.substr(open + 1, close - open - 1);
    /*
     * 区切りは **2 綴り**ある（FH-15。実機で見つけた（2026-08-24）
     * 「数量入力で Enter/A を押すと a が入り購入可否に進めない」）:
     *   - `(1-5)`   … `get_quantity()`（`util.c:3246`）
     *   - `(1 to 5)` … `msg_input_num()`（`message.c:626`）。**店の個数はこちら**
     * 片方しか見ていなかったので、店では `frame.numeric` が立たず、決定が
     * カーソル層の**選択肢**まで流れて品の letter（`a`）を送っていた。
     *
     * @warning **この 2 綴りを日本語化しないこと**（M1 J4 で踏みかけた）。
     * カタログで `(%d to %d)` を `(%d〜%d)` に替えると、ここが区切りを
     * 見つけられず `frame.numeric` が立たない——**機械の検査は全部通り、
     * 実機で個数がカーソル入力できなくなる**。括弧の外（`Quantity` の語）は
     * 訳してよい。訳したくなったら、まず**ここの探索を綴りに依らない形へ
     * 直してから**にする。
     */
    std::size_t sep = inside.find(" to ");
    std::size_t sep_len = 4;
    if (sep == std::string::npos) {
        sep = inside.find('-');
        sep_len = 1;
    }
    if ((sep == std::string::npos) || (sep == 0) || (sep + sep_len >= inside.size())) {
        return false;
    }
    const auto all_digits = [](const std::string &s) {
        return !s.empty()
            && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0') && (c <= '9'); });
    };
    const std::string lo = inside.substr(0, sep);
    const std::string hi = inside.substr(sep + sep_len);
    if (!all_digits(lo) || !all_digits(hi)) {
        return false;
    }

    const std::string tail = text.substr(tail_at);
    if (!tail.empty() && !all_digits(tail)) {
        return false; // `): ` の後ろに文字がある＝数値入力ではない
    }

    out.active = true;
    out.prompt_utf8 = text.substr(0, tail_at);
    out.min = std::atoi(lo.c_str());
    out.max = std::atoi(hi.c_str());
    out.value = tail.empty() ? out.min : std::atoi(tail.c_str());
    out.text_len = static_cast<int>(tail.size());
    out.digits = static_cast<int>(hi.size());
    out.value_begin = static_cast<int>(tail_at);
    out.value_len = static_cast<int>(tail.size());
    return true;
}

/* ============================================================ 選択肢の札（3 書式） */

//! 1 つの札。`keys` は `i/e)` のような 2 コマンド札で 2 個になる。
struct ChoiceTokenGroup {
    std::size_t begin{};
    std::vector<std::pair<std::size_t, std::size_t>> key_spans;
    std::vector<int> keys;
    //! `(1)` の書式で読んだ札か（枠を行末まで伸ばすかの判断。`emit_choices_for_line`）。
    bool paren{ false };
    //! `[b/p/g]` の書式で読んだ札か（**店の命令列の目印**。FH-14 の絞り込み）。
    bool bracket{ false };
};

//! `)` の直後として妥当か（空白／行末）。
bool is_choice_token_terminator(const std::string &text, std::size_t pos)
{
    if (pos >= text.size()) {
        return true;
    }
    return text[pos] == ' ';
}

/*!
 * @brief 札 1: ` a)` `ESC)` `Enter)` `Tab)` `*)` `i/e)` `-)`（行頭か空白の直後）。
 * @details `gb_menu.cpp` の `parse_choice_token` から日本語の「スペース)」だけを
 * 落とした写し。`10)` のような 2 桁の札は**わざと**読めない（`1` の次が `)` でない
 * →不成立、`0)` は前が字→開始位置に来ない）——多桁の札を Frox の一覧は使わない。
 *
 * **語キーと記号キーは実機の報告で足した**（FH-12。2026-08-24: 誕生画面の右列
 * `*) Random Name` `?) Help` `=) Options` `Tab) More Info` `Enter) Next Screen`
 * `Esc) Prev Screen` が札にならず、カーソルが左列に閉じ込められて先へ進めなかった）。
 * 綴りは `frox/src` の doc 文字列の全数 grep（ESC 19・Esc 3・RET 10・Enter 1・
 * ENTER・Tab 2・TAB・Space・SPACE・`*` 18・`?` 5・`=` 3）。
 */
bool parse_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    out = ChoiceTokenGroup{};
    out.begin = p;

    const auto accept = [&](std::size_t key_len, int key) {
        if (text.compare(p + key_len, 1, ")") != 0) {
            return false;
        }
        if (!is_choice_token_terminator(text, p + key_len + 1)) {
            return false;
        }
        out.key_spans.emplace_back(p, key_len);
        out.keys.push_back(key);
        return true;
    };

    /*
     * 語のキー。**単字より先に見る**（`RET)` を `R` の札と読まないため）。
     * Enter と Esc は制御キーの値そのもの（0x0D / 0x1B）を積む——画面のカーソル層が
     * Enter は `confirm`・Esc は `cancel`・その他の制御キーは ctrl 付き文字へ
     * 畳んで送り返す（`ui_cursor.cpp` の `handle_choice`）。
     */
    static const struct {
        const char *word;
        int key;
    } kWordKeys[] = {
        { "ESC", 0x1B }, { "Esc", 0x1B },
        { "ENTER", 0x0D }, { "Enter", 0x0D }, { "RET", 0x0D },
        { "SPACE", ' ' }, { "Space", ' ' },
        { "TAB", 0x09 }, { "Tab", 0x09 },
    };
    for (const auto &w : kWordKeys) {
        const std::size_t len = std::strlen(w.word);
        if ((text.compare(p, len, w.word) == 0) && accept(len, w.key)) {
            return true;
        }
    }
    //! `i/e)`: 1 つの札に 2 コマンド。キーはそれぞれ独立に積める。
    if ((p + 2 < text.size()) && is_ascii_alnum(text[p]) && (text[p + 1] == '/')
        && is_ascii_alnum(text[p + 2])) {
        if ((text.compare(p + 3, 1, ")") == 0) && is_choice_token_terminator(text, p + 4)) {
            out.key_spans.emplace_back(p, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p]));
            out.key_spans.emplace_back(p + 2, 1);
            out.keys.push_back(static_cast<unsigned char>(text[p + 2]));
            return true;
        }
        return false;
    }
    if ((p < text.size())
        && (is_ascii_alnum(text[p]) || (text[p] == '-') || (text[p] == '*') || (text[p] == '?')
            || (text[p] == '='))) {
        /* `*` `?` `=` は誕生画面の右列と doc の命令列で使われる記号キー（FH-12）。 */
        return accept(1, static_cast<unsigned char>(text[p]));
    }
    return false;
}

/*!
 * @brief 札 2: `(1)` `(V)`（オプションの根。`cmd4.c:2765` の `(%c) %s`）。
 * @details **見えている字のまま積む。**オプションの根は大文字で描き
 * `tolower` で照合する（`cmd4.c:2786`）ので、大文字のまま送って通る。
 */
bool parse_paren_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    if ((p + 3 > text.size()) || (text[p] != '(')) {
        return false;
    }
    const unsigned char key=static_cast<unsigned char>(text[p+1]);
    /*
     * Frox has a few real menu entries whose command key is punctuation:
     * (_) Auto Pick/Destroy, (@) About Yourself and (}) View all saved notes.
     * Do not accept arbitrary punctuation here: item descriptions such as
     * (+2) also occur in ordinary text and must not become buttons.
     */
    const bool punctuation_choice=(key=='_')||(key=='@')||(key=='}');
    if (!(is_ascii_alnum(static_cast<char>(key)) || punctuation_choice) || (text[p + 2] != ')')) {
        return false;
    }
    if (!is_choice_token_terminator(text, p + 3)) {
        return false;
    }
    out = ChoiceTokenGroup{};
    out.begin = p;
    out.paren = true;
    out.key_spans.emplace_back(p + 1, 1);
    out.keys.push_back(key);
    return true;
}

/*!
 * @brief 札 3 の語（`[q/Esc]` の `Esc` など）。`key < 0` は「押して送れない」
 * ——読み飛ばして同じ札の別のキーに頼る（`[Space/PgDn]` は Space で送る）。
 */
int bracket_word_key(const std::string &text, std::size_t p, std::size_t len)
{
    struct Word {
        const char *word;
        int key;
    };
    static const Word kWords[] = {
        { "Esc", 0x1B },
        { "Enter", '\r' },
        { "Space", ' ' },
        { "Tab", '\t' },
        //! 特殊キー（`SKEY_*`）はプロトコルの keys では送れない。同義のキーに任せる。
        { "PgDn", -1 },
        { "PgUp", -1 },
        { "Home", -1 },
        { "End", -1 },
    };
    for (const Word &w : kWords) {
        if ((std::strlen(w.word) == len) && (text.compare(p, len, w.word) == 0)) {
            return w.key;
        }
    }
    return -2; // 知らない語
}

/*!
 * @brief 札 3: `[b/p/g] Buy`（doc の命令列。`shop.c:1704` ほか）。
 * @details 中身は `/` 区切りで、1 文字のキーか語（`Esc` `Space/PgDn` `-/PgUp`）。
 *
 * **積むのは最初の 1 キーだけ**——`[b/p/g]` の 3 つは同じ命令の同義キーで、
 * 同じ span に選択肢を重ねると左右移動が足踏みする（`fc_main.cpp` のセーブ選択で
 * 踏んだ穴と同じ）。
 *
 * ## 品名の角括弧を拾わない担保
 * 防具の `[1]` や `[3,+5]` も角括弧である。そこで
 * - **数字 1 文字はキーに採らない**（Frox の命令列に数字の角括弧は無い）
 * - `,` や倍数字は語としても不成立
 * - **`]` の直後が「空白＋札の頭」**であること（命令列は必ず `] Buy` の形。
 *   品名では `[1] (+2)` や行末になる）。**札の頭は英字か 2 バイト文字**
 *   （`is_choice_label_head`。日本語の `] 買う` を落としていた）
 */
bool parse_bracket_choice_token(const std::string &text, std::size_t p, ChoiceTokenGroup &out)
{
    if ((p >= text.size()) || (text[p] != '[')) {
        return false;
    }
    const std::size_t close = text.find(']', p + 1);
    if ((close == std::string::npos) || (close == p + 1)) {
        return false;
    }
    if ((close - p - 1) > 16) {
        return false; // 長い角括弧は本文（`[Press Any Key to Continue]` の類）
    }
    if ((close + 2 >= text.size()) || (text[close + 1] != ' ')
        || !is_choice_label_head(text[close + 2])) {
        return false;
    }

    out = ChoiceTokenGroup{};
    out.begin = p;

    std::size_t q = p + 1;
    while (q < close) {
        std::size_t sep = text.find('/', q);
        if ((sep == std::string::npos) || (sep > close)) {
            sep = close;
        }
        const std::size_t len = sep - q;
        if (len == 0) {
            return false;
        }
        int key = -2;
        if (len == 1) {
            const auto c = static_cast<unsigned char>(text[q]);
            if ((c <= 0x20) || (c >= 0x7F) || ((c >= '0') && (c <= '9')) || (c == '[')
                || (c == ']')) {
                return false; // 数字・空白・記号外れはキーの札ではない
            }
            key = c;
        } else {
            key = bracket_word_key(text, q, len);
            if (key == -2) {
                return false; // 知らない語 → 札ではない（品名などの本文）
            }
        }
        if ((key > 0) && out.keys.empty()) {
            out.key_spans.emplace_back(q, len);
            out.keys.push_back(key);
        }
        q = sep + 1;
    }
    out.bracket = !out.keys.empty();
    return !out.keys.empty();
}

/*!
 * @brief `text` の `[from, to)` で、開いたまま閉じていない `(` があるか（括弧の中か）。
 * @details 能力値の選択（`xtra2.c` の `_display_stat_choice()`）は `a) 腕力 (現在      8)` と並ぶ。
 * 括弧の中の `8)` を札と読み、空白 3 つで名札を切ると「腕力 (現在」で切れていた（2026-10-07）。
 */
bool inside_open_paren(const std::string &text, std::size_t from, std::size_t to)
{
    int depth = 0;
    for (std::size_t i = from; (i < to) && (i < text.size()); ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if ((text[i] == ')') && (depth > 0)) {
            --depth;
        }
    }
    return depth > 0;
}

//! 3 書式の札を 1 行から全部読む。開始位置は行頭か空白の直後。
std::vector<ChoiceTokenGroup> scan_choice_tokens(const std::string &text)
{
    std::vector<ChoiceTokenGroup> groups;
    for (std::size_t p = 0; p < text.size(); ++p) {
        if ((p != 0) && (text[p - 1] != ' ')) {
            continue;
        }
        ChoiceTokenGroup g;
        if (parse_bracket_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
            continue;
        }
        if (parse_paren_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
            continue;
        }
        /*
         * **括弧の中の `8)` は札ではない**（能力値の選択の `(現在      8)`。2026-10-07）。
         * 直前の札の頭から数えて、閉じていない `(` があれば読み飛ばす。
         */
        const std::size_t from = groups.empty() ? 0 : groups.back().begin;
        if (inside_open_paren(text, from, p)) {
            continue;
        }
        if (parse_choice_token(text, p, g)) {
            groups.push_back(std::move(g));
        }
    }
    return groups;
}

/*!
 * @brief 枠を敷く範囲の終わり。次の札の直前まで。ただし**空白 3 連で切る**
 * （同じ行の別の桁に値段や重さが並ぶ。`gb_menu.cpp` と同じ理屈）。
 */
std::size_t choice_span_end(
    const std::string &text, const std::vector<ChoiceTokenGroup> &groups, std::size_t index)
{
    const std::size_t begin = groups[index].begin;
    std::size_t end = (index + 1 < groups.size()) ? groups[index + 1].begin : text.size();
    std::size_t gap = text.find("   ", begin);
    /*
     * **括弧の中の空白では切らない**（能力値の選択の `腕力 (現在     17)`。2026-10-07）。
     * 閉じ括弧の後ろから探し直す。
     */
    while ((gap != std::string::npos) && (gap < end) && inside_open_paren(text, begin, gap)) {
        const std::size_t close = text.find(')', gap);
        if ((close == std::string::npos) || (close >= end)) {
            break;
        }
        gap = text.find("   ", close);
    }
    if ((gap != std::string::npos) && (gap < end)) {
        end = gap;
    }
    while ((end > begin) && ((text[end - 1] == ' ') || (text[end - 1] == ','))) {
        --end;
    }
    return end;
}

/*!
 * @brief 枠を**行末まで**伸ばしてよい行か（`(1)` の札しか無い行だけ）。
 * @details オプションの根はラベルが `(1) General Options` と 1 行 1 札で並ぶ。
 * 空白 3 連で切るとラベルの途中で枠が終わる画面があるため。
 */
bool line_spans_whole(const std::vector<ChoiceTokenGroup> &groups)
{
    if (groups.empty()) {
        return false;
    }
    for (const ChoiceTokenGroup &g : groups) {
        if (!g.paren) {
            return false;
        }
    }
    return true;
}

void emit_choices_for_line(
    GameFrame &frame, std::size_t line_index, const std::vector<ChoiceTokenGroup> &groups)
{
    const std::string &text = frame.menu_term_lines[line_index].text_utf8;
    const bool whole = line_spans_whole(groups);
    std::size_t trimmed = text.size();
    while ((trimmed > 0) && (text[trimmed - 1] == ' ')) {
        --trimmed;
    }
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const ChoiceTokenGroup &g = groups[gi];
        const std::size_t span_end = whole ? trimmed : choice_span_end(text, groups, gi);
        for (std::size_t ki = 0; ki < g.keys.size(); ++ki) {
            if (frame.menu_choices.size() >= kMenuChoiceMax) {
                return;
            }
            MenuChoice choice{};
            choice.line_index = static_cast<int>(line_index);
            choice.span_begin = static_cast<int>(g.begin);
            choice.span_len = static_cast<int>(span_end - g.begin);
            choice.key_begin = static_cast<int>(g.key_spans[ki].first);
            choice.key_len = static_cast<int>(g.key_spans[ki].second);
            choice.key = g.keys[ki];
            frame.menu_choices.push_back(choice);
        }
    }
}

//! Term の行番号 → ミラーの添字（`source_row` で引く。無ければ -1）。
int mirror_line_of(const GameFrame &frame, int row)
{
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].source_row == row) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void append_frox_selector_aux_choices(GameFrame &frame)
{
    if (frame.menu_term_lines.empty()) {
        return;
    }
    const auto it = std::find_if(frame.menu_term_lines.begin(), frame.menu_term_lines.end(),
        [](const TermMirrorLine &line) { return line.source_row == 0; });
    if (it == frame.menu_term_lines.end()) {
        return;
    }
    /*
     * 行 0 の問いは**空のこともある**（モンスターの一覧・持ち物の一覧は行 0 を使わない。実機で確かめた
     * 2026-10-07）。以前は空なら先頭で抜けていたので、**行 0 を使わない一覧の補助の選択肢
     * （下の `monster_list` ほか）が英語でも日本語でも 1 つも載らなかった**。行 0 の問いに頼る
     * 照合は空のとき自然に当たらないので、抜けずに先へ進める。
     */
    const std::string &prompt = it->text_utf8;

    const auto add = [&](int key, const char *label) {
        if (std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
                [key](const MenuChoice &choice) { return choice.key == key; })) {
            return;
        }
        if (frame.menu_choices.size() >= kMenuChoiceMax) {
            return;
        }
        MenuChoice choice{};
        choice.line_index = -1;
        choice.key = key;
        choice.key_len = 1;
        choice.label_utf8 = ui_label(label);
        choice.description_utf8 = prompt;
        frame.menu_choices.push_back(std::move(choice));
    };

    /*
     * spells.c::_choose_spell
     *
     * 日本語は `使用する力（'?' 閲覧 '!' 名札）:`（使用）・`閲覧する力（'?' 使用 '!' 名札）:`（閲覧）・
     * `名札を付ける力（'!' 使用 '=' 未使用を消去）:`（名札）。呪文の本の問いは
     * `使用する呪文（'?' 閲覧）:` の形。**英語は `?` の記号そのものだけで通るが、日本語は動詞が
     * 頭に来る**ので、動詞（使用する・閲覧する）と引用符つきの `'?'` の両方を要る。
     */
    {
        const bool en_selector = (prompt.find("Use which ") != std::string::npos || prompt.find("Browse which ") != std::string::npos)
            && prompt.find("Browse") != std::string::npos && prompt.find('?') != std::string::npos;
        const bool ja_browse_head = prompt.find(utf8_of(u8"閲覧する")) != std::string::npos;
        const bool ja_selector = (ja_browse_head || prompt.find(utf8_of(u8"使用する")) != std::string::npos)
            && prompt.find("'?'") != std::string::npos;
        if (en_selector || ja_selector) {
            const bool browsing = (prompt.find("Browse which ") != std::string::npos) || (ja_selector && ja_browse_head);
            add('?', browsing ? "Use mode" : "Browse / describe");
        }
    }
    if (has_text(prompt, "'!' to Label", u8"'!' 名札")) {
        add('!', "Label mode");
    } else if ((prompt.find("Label which ") != std::string::npos && prompt.find("'!' to Use") != std::string::npos)
        || (prompt.find(utf8_of(u8"名札を付ける")) != std::string::npos && prompt.find(utf8_of(u8"'!' 使用")) != std::string::npos)) {
        add('!', "Use mode");
    }
    if (has_text(prompt, "'C' for character sheet", u8"'C' 人物調書")) {
        add('C', "Character sheet");
    }
    if (has_text(prompt, "'=' to wipe inactive", u8"'=' 未使用を消去")) {
        add('=', "Clear inactive labels");
    }

    /* menu.c::_choose（日本語は `（'?' 閲覧）`・`（'?' 選択）`。カタログの `(Type '?' to browse)` ほか） */
    if (has_text(prompt, "Type '?' to browse", u8"'?' 閲覧")) {
        add('?', "Browse / describe");
    } else if (has_text(prompt, "Type '?' to choose", u8"'?' 選択")) {
        add('?', "Choose / use");
    }
    /*
     * doppelganger.c uses title-cased variants of the same prose (`_choose_form_imp()`、`doppelganger.c:293`〜`:294`)。
     * 日本語は `どの種族に化けますか? ('?' で解説)`（解説へ切り替える）・`どの種族を見ますか? ('?' で使用)`（化ける側へ戻す）。
     * 上の `'?' 閲覧`・`'?' 使用` は別の問いの訳（`spells.c` ほか）で、この 2 つには「で」が入るので当たらず、
     * 日本語では `?` が申告されず「この画面では使えないキーです。」と断られていた（修-34。F-frox-503）。
     */
    if (has_text(prompt, "Type '?' to Browse", u8"'?' で解説")) {
        add('?', "Browse / describe");
    } else if (has_text(prompt, "Type '?' to Use", u8"'?' 使用") || (prompt.find(utf8_of(u8"'?' で使用")) != std::string::npos)) {
        add('?', "Choose / use");
    }

    /* magic_eater.c / alky.c / graymage.c custom selectors.  The translated
     * wording varies, but the literal command keys remain in the prompt. */
    const bool magic_eater = prompt.find("'/'") != std::string::npos
        && (prompt.find("'\\'") != std::string::npos || prompt.find("'{'") != std::string::npos
            || has_text(prompt, "Cycle Type", u8"種類を切り替え"));
    if (magic_eater) {
        add('/', "Cycle device type");
        add('w', "Show wands");
        add('s', "Show staves");
        add('r', "Show rods");
        if (prompt.find("'\\'") != std::string::npos) add('\\', "Exchange two slots");
        if (prompt.find("'{'") != std::string::npos) add('{', "Inscribe a slot");

        /* Uppercase slot labels open the item's detailed display.  Expose an
         * explicit action for every visible lowercase slot instead of relying
         * on a physical Shift key. W/S/R/X/Z are reserved by the core. */
        const auto primary = frame.menu_choices;
        for (const MenuChoice &base : primary) {
            if (base.key < 'a' || base.key > 'z') continue;
            const int upper = base.key - 'a' + 'A';
            if (upper == 'W' || upper == 'S' || upper == 'R' || upper == 'X' || upper == 'Z') continue;
            if (std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
                    [upper](const MenuChoice &choice) { return choice.key == upper; })) continue;
            if (frame.menu_choices.size() >= kMenuChoiceMax) break;
            MenuChoice inspect = base;
            inspect.key = upper;
            inspect.key_len = 1;
            inspect.label_utf8 = ui_label_with_key("Inspect / describe ", u8"", u8" を調べる", static_cast<char>(base.key));
            inspect.description_utf8 = prompt;
            frame.menu_choices.push_back(std::move(inspect));
        }
    }
    if (prompt.find("'X'") != std::string::npos && has_text(prompt, "Exchange", u8"入れ替え")) {
        add('x', "Exchange two slots");
        add('X', "Exchange two slots");
    }

    /* hissatsu.c::get_hissatsu_power */
    if (prompt.find("?=") != std::string::npos && has_text(prompt, "ESC=exit", u8"ESC=終了")) {
        add('?', has_text(prompt, "?=Use", u8"?=使用") ? "Use mode" : "Browse / describe");
    }
    if (has_text(prompt, "!=Damage", u8"!=ダメージ")) {
        add('!', "Damage display");
    }

    /* hex.c::stop_hex_spell */
    if (has_text(prompt, "'l' to all", u8"'l' で全部")) {
        add('l', "Stop all spells");
    }

    /*
     * 品選びの窓（`obj_prompt`）の下の問い `どれを拾いますか (* で全部)?`（床・鑑定ほか）。`*` は窓の中の
     * 全部を選ぶ操作で、原作がキーとして受ける。問いは窓の見出し（行 0）ではなく窓の下の行に出るので、
     * 行 0 だけを見ていた以前は新しい UI から押せる選択肢が出なかった（キーボードの無い端末では全部拾えない）。
     */
    if (fc_obj_prompt_active() != 0) {
        for (const TermMirrorLine &line : frame.menu_term_lines) {
            if (has_text(line.text_utf8, "(* for all)", u8"(* で全部)")) {
                add('*', "All items");
                break;
            }
        }
    }

    bool monster_list = false;
    bool probing_list = false;
    bool object_list = false;
    bool pet_normal = false;
    bool pet_dismiss = false;
    bool wipe_preview = false;
    bool mimic_form = false;
    bool skillmaster_choice = false;
    bool skillmaster_spell_list = false;
    bool press_any_key_doc = false;
    int weaponsmith_max_bonus = 0;
    for (const auto &line : frame.menu_term_lines) {
        const std::string &text = line.text_utf8;
        if (has_text(text, "[Press ? for help or ESC to exit]", u8"[? で説明・ESC で戻る]")) monster_list = true;
        if (has_text(text, "[Press P for probing, ? for help or ESC to exit]", u8"[P で調査、? でヘルプ、ESC で終了]")) {
            monster_list = true;
            probing_list = true;
        }
        /*
         * 持ち物の一覧の絞り込みの見出し。日本語は `絞り込み: Ammo|IDentified|…`（頭の `Filter:` だけが訳され、
         * 切り替えの語は英語のまま）。**日本語は頭が 2 桁長く、枠の右端で `Unwanted` が `Unwante` へ切れる**
         * ので、末尾の語は途中（`Unwant`）までで見る。
         */
        if (has_text(text, "Filter: ", u8"絞り込み: ") && text.find("Ammo") != std::string::npos
            && text.find("Unwant") != std::string::npos) object_list = true;
        /*
         * モンスターの思い出・アイテムの調査の窓（`mon_display_instance()` ほか）の末尾。見出しの
         * `名前 : 巨大白ネズミ (r)` の `(r)` を札と読むと、本文の左に「(r)」の 1 行だけの選択肢が出る。
         * 何かキーを押せば閉じる窓なので、行から読んだ札は捨てる（下の一覧と同じ扱い）。
         */
        if (has_text(text, "[Press Any Key to Continue]", u8"[何かキーを押してください]")) press_any_key_doc = true;
        if (has_text(text, "Commands: n) rename pet  d) dismiss pet", u8"n) 名前を付ける  d) 放す")) pet_normal = true;
        if (has_text(text, "Dismiss which pet? ('u' for all unnamed", u8"どのペットを放すか？")) pet_dismiss = true;
        if (text.find("Y to confirm") != std::string::npos && text.find("toggle list") != std::string::npos) wipe_preview = true;
        if (has_text(text, "'?' to recall", u8"'?' で思い出") && has_text(text, "'=' for more info", u8"'=' で詳しく")) mimic_form = true;
        /* `Choose the Skill` は `Choose the Skill Type` の頭でもある（日本語は別の字なので両方）。 */
        if (has_text(text, "Choose the Skill", u8"技能を選ぶ") || text.find(utf8_of(u8"技能の種類を選ぶ")) != std::string::npos) skillmaster_choice = true;
        /* Skillmaster's spell table is the only Frox spell selector whose
         * `Lvl  SP Fail Desc` header starts at column zero with the book name.
         * Gray Mage and the generic spell UIs indent their corresponding
         * headers, so this keeps the uppercase-help alias screen-specific. */
        if (!text.empty() && text.front() != ' ' && has_text(text, "Lvl  SP Fail", u8"Lv   SP 失率")
            && has_text(text, "Desc", u8"解説")) skillmaster_spell_list = true;
        /*
         * 武器鍛冶の「ボーナスの量」の案内（`weaponsmith.c:1953`）。英語は `Type '1' to '3' to adjust the amount of
         * bonus to use.`、日本語は `'1' から '3' を押して使うボーナスの量を決める。`。上限の数字は 2 つ目の `'` の次。
         */
        if (has_text(text, "adjust the amount of bonus to use", u8"使うボーナスの量を決める")) {
            const auto one = text.find("'1'");
            const std::string en_to = "to '";
            const std::string ja_to = utf8_of(u8"から '");
            auto to = (one == std::string::npos) ? std::string::npos : text.find(en_to, one + 3);
            std::size_t to_len = en_to.size();
            if (to == std::string::npos && one != std::string::npos) {
                to = text.find(ja_to, one + 3);
                to_len = ja_to.size();
            }
            if (to != std::string::npos && to + to_len < text.size() && text[to + to_len] >= '1' && text[to + to_len] <= '9') {
                weaponsmith_max_bonus = text[to + to_len] - '0';
            }
        }
    }
    /*
     * モンスターの一覧・アイテムの一覧の行は、キーで選ぶ札ではない（カーソルと `/` `J` で選ぶ）。
     * 行の `(2)`（同じモンスターの数）を札と読んで、`2` を送る札が 1 つ余計に出ていた（実機で確かめた
     * 2026-10-07）。この 2 つの画面だけ、行から読んだ札を捨てて補助の選択肢だけを残す。
     */
    if (monster_list || object_list || press_any_key_doc) {
        frame.menu_choices.erase(std::remove_if(frame.menu_choices.begin(), frame.menu_choices.end(),
                                     [](const MenuChoice &choice) { return choice.line_index >= 0; }),
            frame.menu_choices.end());
    }
    if (monster_list) {
        add('/', "Recall / inspect monster");
        if (probing_list) add('P', "Probe monster");
        add('N', "Rename selected pet");
        add('T', "Set current target");
        add('J', "Travel to selected monster");
        add('+', "Widen list");
        add('-', "Narrow list");
        add('?', "Help");
    }
    if (object_list) {
        add('A', "Toggle ammunition");
        add('D', "Toggle identified items");
        add('E', "Toggle sensed items");
        add('N', "Toggle unsensed items");
        add('S', "Toggle stairs");
        add('U', "Toggle unwanted items");
        add('/', "Inspect selected item");
        add('J', "Travel to selected item");
        add('+', "Widen list");
        add('-', "Narrow list");
        add('?', "Help");
    }
    if (pet_normal) {
        add('n', "Rename a pet");
        add('d', "Dismiss a pet");
    }
    if (pet_dismiss) {
        add('u', "Dismiss all unnamed pets");
    }
    if (wipe_preview) {
        add('Y', "Confirm");
        add('n', "Cancel");
        add('N', "Cancel");
        add('y', "Hide item list");
        add('u', "Hide item list");
        add('U', "Hide item list");
        add('?', "Hide item list");
        add(27, "Cancel");
        add('/', "Inspect selected item");
        add('=', "Widen list");
        add('+', "Widen list");
        add('-', "Narrow list");
    }
    if (mimic_form) {
        add('?', "Recall / inspect form");
        add('=', "Change information page");
        add('\t', "Next form group");
    }
    if (skillmaster_choice || skillmaster_spell_list) {
        const auto primary = frame.menu_choices;
        for (const MenuChoice &base : primary) {
            if (base.key < 'a' || base.key > 'z') continue;
            const int upper = base.key - 'a' + 'A';
            if (std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
                    [upper](const MenuChoice &choice) { return choice.key == upper; })) continue;
            if (frame.menu_choices.size() >= kMenuChoiceMax) break;
            MenuChoice help = base;
            help.key = upper;
            help.key_len = 1;
            help.label_utf8 = ui_label_with_key("Detailed help for ", u8"", u8" の詳しいヘルプ", static_cast<char>(base.key));
            frame.menu_choices.push_back(std::move(help));
        }
    }
    if (weaponsmith_max_bonus > 0) {
        add('+', "Increase bonus amount");
        add('-', "Decrease bonus amount");
        for (int value = 1; value <= weaponsmith_max_bonus; ++value) {
            const std::string label = ui_label_with_key("Set bonus amount to ", u8"ボーナス量を ", u8" にする", static_cast<char>('0' + value));
            add('0' + value, label.c_str());
        }
    }
}

bool collect_journey_choices(GameFrame &frame)
{
    const bool journey = std::any_of(frame.menu_term_lines.begin(), frame.menu_term_lines.end(), [](const TermMirrorLine &line) {
        return has_text(line.text_utf8, "Please choose a journey target:", u8"目的地を選んでください:");
    });
    if (!journey) {
        return false;
    }

    frame.menu_choices.clear();
    const auto push = [&](std::size_t line_index, std::size_t open, std::size_t close,
                          std::size_t span_end, int key, std::size_t key_at, std::string label) {
        if (frame.menu_choices.size() >= kMenuChoiceMax) {
            return;
        }
        MenuChoice choice{};
        choice.line_index = static_cast<int>(line_index);
        choice.span_begin = static_cast<int>(open);
        choice.span_len = static_cast<int>(span_end - open);
        choice.key_begin = static_cast<int>(key_at);
        choice.key_len = (key == 27) ? 3 : 1;
        choice.key = key;
        choice.label_utf8 = std::move(label);
        frame.menu_choices.push_back(std::move(choice));
    };

    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        std::size_t open = 0;
        while ((open = text.find('[', open)) != std::string::npos) {
            const std::size_t close = text.find(']', open + 1);
            if (close == std::string::npos) {
                break;
            }
            const std::string keys = text.substr(open + 1, close - open - 1);
            int key = 0;
            std::size_t key_at = open + 1;
            if (keys == "Esc") {
                key = 27;
            } else if (!keys.empty() && keys.size() <= 3
                && (is_ascii_alpha(keys[0]) || keys[0] == '<' || keys[0] == '>')) {
                key = static_cast<unsigned char>(keys[0]);
            }
            if (key == 0) {
                open = close + 1;
                continue;
            }

            const std::size_t next = text.find('[', close + 1);
            std::size_t span_end = (next == std::string::npos) ? text.size() : next;
            while (span_end > close + 1 && text[span_end - 1] == ' ') {
                --span_end;
            }

            std::string label;
            std::size_t p = close + 1;
            while (p < span_end && text[p] == ' ') ++p;
            if (keys == "Esc") {
                label = ui_label("Go back");
            } else {
                // Journey inserts one live map glyph between the key label and
                // the human-readable destination.  Skip that glyph by finding
                // the first following ASCII space; this also works for UTF-8
                // glyphs because the separator itself remains ASCII.
                const std::size_t glyph_end = text.find(' ', p);
                p = (glyph_end == std::string::npos || glyph_end >= span_end) ? span_end : glyph_end + 1;
                while (p < span_end && text[p] == ' ') ++p;
                label = trim_right(text.substr(p, span_end - p));
                if (label.empty()) label = keys;
            }
            push(i, open, close, span_end, key, key_at, std::move(label));
            open = close + 1;
        }
    }
    return !frame.menu_choices.empty();
}

/*!
 * @brief 死亡の画面（`effects.c` の `_death_dump_screen_prompt()`・`_death_resurrection_prompt()`）の選択肢を、専用の読み方で作る。
 * @return 死亡の画面なら真（`menu_choices` を作り直す）
 * @details 一般の読み方（`scan_choice_tokens()`）は、案内の文の中の `[d] を押すこと。…`・`[s] で静かな死に方の切り替え:` を
 * 「札 + その行の残り」の選択肢にしてしまい、窓の左に文の断片が並んでいた（修-31。F-frox-202・F-frox-305）。
 * 死亡の画面で押せるキーは、**最下段の命令列**（`[d/y] View Screen  [s] Silent  [q] Quick  [w] Cheat Death  [n] Accept Death`。
 * 1 度 Esc を押すと末尾が `[Esc] Accept Death` になる）か、蘇生の問い（`Press [r] …`・`[d/y]`・`[n]`）だけなので、
 * 画面の見分けは**札の組**で行い（言語に依らない）、選択肢はその行だけから作る。案内の文は選択肢にしない。
 */
bool collect_death_screen_choices(GameFrame &frame)
{
    const auto bytes_of = [](const std::string &text, std::size_t at) { return static_cast<int>(at); };

    // (1) 蘇生の問い。題が `***** You have died... *****`（ドットが 3 つ）。ここだけは命令列が無く、案内の文の `[r]`・`[d/y]`・`[n]` が札になる。
    int anchor = -1;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        if (text.find("***** You have died... *****") != std::string::npos || text.find(utf8_of(u8"***** あなたは死んだ... *****")) != std::string::npos) {
            anchor = static_cast<int>(i);
            break;
        }
    }
    if (anchor >= 0) {
        frame.menu_choices.clear();
        const auto add = [&](int key, const char *label, const char *needle) {
            for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
                const std::string &text = frame.menu_term_lines[i].text_utf8;
                const std::size_t at = text.find(needle);
                if (at == std::string::npos) continue;
                MenuChoice choice{};
                choice.line_index = static_cast<int>(i);
                choice.key = key;
                choice.key_begin = bytes_of(text, at + 1);
                choice.key_len = 1;
                choice.span_begin = bytes_of(text, at);
                choice.span_len = static_cast<int>(std::strlen(needle));
                choice.label_utf8 = ui_label(label);
                frame.menu_choices.push_back(std::move(choice));
                return;
            }
        };
        add('r', "Reconstruct elsewhere", "[r]");
        add('d', "View or dump screen", "[d/y]");
        add('n', "Decline help", "[n]");
        return !frame.menu_choices.empty();
    }

    // (2) 命令列。
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        if (text.find("[d/y]") == std::string::npos || text.find("[s]") == std::string::npos || text.find("[q]") == std::string::npos
            || text.find("[w]") == std::string::npos) {
            continue;
        }
        std::vector<MenuChoice> found;
        for (std::size_t open = text.find('['); open != std::string::npos;) {
            const std::size_t close = text.find(']', open);
            if (close == std::string::npos) break;
            const std::string inner = text.substr(open + 1, close - open - 1);
            const std::size_t next = text.find('[', close);
            std::size_t label_begin = close + 1;
            while (label_begin < text.size() && text[label_begin] == ' ') ++label_begin;
            std::size_t label_end = (next == std::string::npos) ? text.size() : next;
            while (label_end > label_begin && text[label_end - 1] == ' ') --label_end;
            int key = 0;
            if (inner == "d/y") key = 'd';
            else if (inner == "Esc") key = 27;
            else if (inner.size() == 1) key = static_cast<unsigned char>(inner[0]);
            if (key != 0 && label_end > label_begin) {
                MenuChoice choice{};
                choice.line_index = static_cast<int>(i);
                choice.key = key;
                choice.key_begin = bytes_of(text, open + 1);
                choice.key_len = (inner == "d/y") ? 1 : static_cast<int>(inner.size());
                choice.span_begin = bytes_of(text, label_begin);
                choice.span_len = static_cast<int>(label_end - label_begin);
                choice.label_utf8 = text.substr(label_begin, label_end - label_begin);
                found.push_back(std::move(choice));
            }
            open = next;
        }
        if (found.size() < 4) continue;
        frame.menu_choices = std::move(found);
        return true;
    }
    return false;
}

bool collect_custom_key_controls(GameFrame &frame)
{
    bool browser = false;
    bool preview = false;
    bool reset = false;
    int anchor = -1;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const std::string &text = frame.menu_term_lines[i].text_utf8;
        if (has_text(text, "Press a listed key to jump to it. ? for help or ESC to return.",
                u8"一覧のキーを押すとそこへ飛びます。")) {
            browser = true;
            anchor = static_cast<int>(i);
        }
        if (has_text(text, "Previewing Replace results.", u8"置き換えた結果を試しに見ている。")
            || has_text(text, "Previewing Add results.", u8"足した結果を試しに見ている。")) {
            browser = true;
            preview = true;
            anchor = static_cast<int>(i);
        }
        if (has_text(text, "Reset (m)acros, current (k)eyset, both (K)eysets, (y)everything, or (n)othing?",
                u8"(m)マクロ、(k)いまのキーセット")) {
            reset = true;
            anchor = static_cast<int>(i);
        }
    }
    if (!browser && !reset) return false;

    frame.menu_choices.clear();
    const auto add = [&](int key, const char *label) {
        if (frame.menu_choices.size() >= kMenuChoiceMax) return;
        MenuChoice choice{};
        choice.line_index = anchor;
        choice.key = key;
        choice.key_len = 1;
        choice.label_utf8 = ui_label(label);
        frame.menu_choices.push_back(std::move(choice));
    };

    if (browser) {
        /* The native browser paints bare horizontal [label] cells, but its
         * complete state space is reachable with 4/6.  Keep those navigation
         * keys semantic rather than pretending the labels are ordinary menu
         * rows (many are named special keys such as Up/F1). */
        add('4', "Previous custom key");
        add('6', "Next custom key");
        add('?', "Help");
        if (preview) {
            add('\t', "Switch Replace / Add preview");
            add('\r', "Toggle all affected / differences only");
        }
        add(27, "Return");
        return true;
    }

    add('m', "Reset macros");
    add('k', "Reset current keyset");
    add('K', "Reset both keysets");
    add('y', "Reset everything");
    add('n', "Cancel reset");
    add('\r', "Reset everything (default)");
    add('?', "Help");
    add(27, "Cancel reset");
    return true;
}

} // namespace

/* ============================================================== コアのカーソル */

/*!
 * @brief 呪文の一覧（枠の無いカーソル式）の `>` を拾う。
 * @details 本の呪文の一覧は枠（`+----`）で囲まれていないので、下の箱の規則では見つからない。
 * 範囲の問い（`*` と ESC を含む。「(呪文 a-h, *=一覧, ESC=中止) …」）が出ている間だけ、
 * 一覧を描いた行（`fc_targeting_menu.inl` の控え）の中で `>` を探す——地図の階段の `>` は拾わない。
 * 見つからないと、画面の別の字（右の欄の「(1)」）を選択肢と取り違え、覚える・調べるの一覧で
 * パッドやマウスでは呪文を選べなかった（遊ぶ人に届いた報告 2026-10-06）。
 */
bool fill_book_list_cursor(GameFrame &frame)
{
    const int count = fc_book_list_count();
    if (count <= 0) {
        return false;
    }
    const auto row0 = std::find_if(frame.menu_term_lines.begin(), frame.menu_term_lines.end(),
        [](const TermMirrorLine &line) { return line.source_row == 0; });
    if ((row0 == frame.menu_term_lines.end()) || (row0->text_utf8.find('*') == std::string::npos)
        || (row0->text_utf8.find("ESC") == std::string::npos)) {
        return false;
    }
    for (int k = 0; k < count; ++k) {
        const int row = fc_book_list_row(k);
        for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
            if (frame.menu_term_lines[i].source_row != row) {
                continue;
            }
            const std::string &text = frame.menu_term_lines[i].text_utf8;
            for (std::size_t p = text.find('>'); p != std::string::npos; p = text.find('>', p + 1)) {
                if (((p != 0) && (text[p - 1] != ' ')) || (p + 1 >= text.size()) || (text[p + 1] != ' ')) {
                    continue;
                }
                const std::size_t name = text.find_first_not_of(' ', p + 1);
                if (name == std::string::npos) {
                    continue;
                }
                std::size_t end = text.size();
                while ((end > name) && (text[end - 1] == ' ')) {
                    --end;
                }
                MenuChoice cursor{ -1, 0, 0, 0, 0, 0 };
                cursor.line_index = static_cast<int>(i);
                cursor.span_begin = static_cast<int>(p);
                cursor.span_len = static_cast<int>(end - p);
                cursor.key_len = 0;
                frame.menu_core_cursor = cursor;
                frame.menu_core_cursors.push_back(cursor);
                return true;
            }
        }
    }
    return false;
}

bool fill_core_cursor(GameFrame &frame)
{
    frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
    frame.menu_core_cursors.clear();

    if (frame.menu_term_lines.empty()) {
        return false;
    }
    if (fill_book_list_cursor(frame)) {
        return true;
    }

    /*
     * 枠 `+----` の行（コマンドメニューの上辺と下辺。`util.c` の
     * `put_str("+----…----+", basey, basex)`）。**枠の無いミラーでは探さない**
     * ——`>` は階段の字でもあり、ミラーには前の画（ASCII の地図）が残っている。
     */
    std::vector<int> frame_rows;
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        if (frame.menu_term_lines[i].text_utf8.find("+----") != std::string::npos) {
            frame_rows.push_back(static_cast<int>(i));
        }
    }
    if (frame_rows.size() < 2) {
        return false;
    }

    static const std::string mark = "> ";
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        /*
         * **箱の中の行だけ**（上と下の両方に枠の行があること）。箱の外は地図の
         * 消し残りで、` > `（暗がりに挟まれた階段）が印と同じ字面になりうる。
         * 箱の中身は `|    |` で塗り直されているので地図は漏れない。
         */
        bool above = false;
        bool below = false;
        for (const int r : frame_rows) {
            above = above || (r < static_cast<int>(i));
            below = below || (r > static_cast<int>(i));
        }
        if (!above || !below) {
            continue;
        }

        const std::string &text = frame.menu_term_lines[i].text_utf8;
        std::size_t p = 0;
        while (p + mark.size() <= text.size()) {
            if (text.compare(p, mark.size(), mark) != 0) {
                ++p;
                continue;
            }
            // 行頭・空白の直後・枠線 `|` の直後だけ（本文中の印を拾わないため）。
            if ((p != 0) && (text[p - 1] != ' ') && (text[p - 1] != '|')) {
                ++p;
                continue;
            }
            // 印だけで終わる行（消し残し）は選択行ではない。
            const std::size_t after = p + mark.size();
            if (text.find_first_not_of(' ', after) == std::string::npos) {
                ++p;
                continue;
            }
            /*
             * 枠の右端は「空白 3 連（隣の列の項目）」か「枠線 `|`」の早い方。
             * 小メニューは親の箱に重ねて描かれる（`basey += 2; basex += 8`）ので、
             * 親の印と子の印が同時に見える。全部積む（`menu_core_cursors`）。
             */
            std::size_t end = text.find("   ", after);
            const std::size_t bar = text.find('|', after);
            if (bar != std::string::npos) {
                end = (end == std::string::npos) ? bar : (std::min)(end, bar);
            }
            if (end == std::string::npos) {
                end = text.size();
            }
            while ((end > after) && (text[end - 1] == ' ')) {
                --end;
            }

            MenuChoice cursor{ -1, 0, 0, 0, 0, 0 };
            cursor.line_index = static_cast<int>(i);
            cursor.span_begin = static_cast<int>(p);
            cursor.span_len = static_cast<int>(end - p);
            cursor.key_len = 0; // コアの画面に「決定で積む 1 文字」は無い
            if (frame.menu_core_cursors.empty()) {
                frame.menu_core_cursor = cursor;
            }
            frame.menu_core_cursors.push_back(cursor);
            p = end; // 次の印はこの枠より右から探す
        }
    }
    return !frame.menu_core_cursors.empty();
}

/* ================================================================== プロンプト */

/*!
 * @brief メッセージの箱の行の並びで、末尾が `-more-`（`-続く-`）の行までの行数。無ければ 0。
 * @details 下から探す（-続く- は箱の最後の行に付く）。その下の行は箱の外（地図）なので捨てる。
 */
std::size_t more_marker_line_count(const std::vector<std::string> &lines)
{
    for (std::size_t i = lines.size(); i > 0; --i) {
        if (presentation::message_more_suffix(lines[i - 1])) {
            return i;
        }
    }
    return 0;
}

/*!
 * @brief 折り返された「スペースだけを受ける問い」を、上下 2 行を合わせて読む。
 * @param upper 上の行（箱の中の素の行）
 * @param lower 下の行
 * @param out 問いが立つときだけ書き込む
 * @details 体力の警告のように長いメッセージの末尾に付く文は、メッセージの箱の幅（`ui_msg_rect()`。
 * 80 桁の端末で 67 桁）で折り返され、`スペースを押して` と `続ける。` のように文の途中で 2 行に割れる。
 * 攻撃者の名前の長さで割れる位置が変わり、割れた回だけ問いが立たず、決定ボタンがスペースを送れずに
 * 戦闘中に操作を受け付けなくなった（2026-10-09。関門の筋書きで間欠的に落ちて判明）。
 * 日本語は字の切れ目、英語は語の切れ目で割れるので、区切り無しと空白 1 つの 2 通りでつなぐ。
 * 他の問いを誤って拾わないよう、スペースだけを受ける問い（選択肢がスペース 1 つ）に限る。
 */
bool parse_wrapped_space_prompt(const std::string &upper, const std::string &lower, PromptBar &out)
{
    const std::string head = trim_right(upper);
    const std::string tail = trim_right(lower);
    if (head.empty() || tail.empty()) {
        return false;
    }
    for (const char *separator : { "", " " }) {
        PromptBar joined;
        if (parse_prompt_line(head + separator + tail, joined) && (joined.choices.size() == 1)
            && (joined.choices[0].key == ' ')) {
            out = std::move(joined);
            return true;
        }
    }
    return false;
}

static void fill_row0_prompt_inner(GameFrame &frame);

/*!
 * @brief 文がクエストの説明（`名前 (Level N): 説明文`）か（修-33）。
 * @details 原作は受けたクエスト・失敗したクエスト・完了の報酬を、どれも `msg_print_for_prompt()` で
 * `<名前> (Level <数>): <説明文>` の形にして出す（`quest.c` の `quest_take()`・`quest_reward()`、`bldg.c` の `castle_quest()`）。
 * 日本語は `（レベル <数>）` になる。
 */
bool is_quest_message_text(const std::string &text)
{
    return text.find("(Level ") != std::string::npos
        || text.find(reinterpret_cast<const char *>(u8"（レベル")) != std::string::npos
        || text.find(reinterpret_cast<const char *>(u8"(レベル")) != std::string::npos;
}

/*!
 * @brief 建物の画面で、クエストの説明文がメッセージの箱に出しっぱなしか（修-33）。
 * @details 原作は説明文を出したあと**キーを待たずに**建物の命令待ちへ戻る（`do_cmd_bldg()` の `inkey()`）。箱の文は次の命令の
 * 処理（`bldg_process_command()` の `msg_line_clear()`）まで残る。建物の画面かは行 23 の「建物を出る」で見る。
 */
bool building_quest_message_shown()
{
    if (fc_msg_line_shown() == 0) {
        return false;
    }
    const std::string exit_row = term_row_text(23);
    if ((exit_row.find("Exit building") == std::string::npos)
        && (exit_row.find(reinterpret_cast<const char *>(u8"建物を出")) == std::string::npos)) {
        return false;
    }
    int box_x = 0;
    int box_y = 0;
    int box_w = 0;
    int box_h = 0;
    if (fc_msg_rect(&box_x, &box_y, &box_w, &box_h) != FC_OK) {
        return false;
    }
    std::string text;
    for (int y = (std::max)(0, box_y); (y < box_y + (std::max)(1, box_h)) && (y < 23); ++y) {
        text += term_row_text(y);
        text += '\n';
    }
    return is_quest_message_text(text);
}

/*!
 * @brief 説明文が出ているあいだの決定が、説明文を出した命令の繰り返しか（修-33）。
 * @param key 画面の決定が原作へ送ろうとしているキー。
 * @param last_key 原作へ最後に渡したキー。
 * @return 真なら、命令を繰り返さず説明文を閉じるだけにする。
 */
bool repeats_command_under_message(int key, int last_key, bool message_shown)
{
    return message_shown && (key != 0) && (key == last_key);
}

/*!
 * @brief 移動の目標のマスを選ぶ問い（`tgt_pt()`）のあいだ、カーソルのマスを 3D の地図に十字線として出す（修-32）。
 * @param has_cell カーソルが地図のマスを指しているか（`fc_term_cursor_cave_cell()` の結果）。
 * @param floor_w 階の幅（`cur_wid`）。画面側の検査（`decode_targeting`）は幅と高さの内側の点しか受けない。
 * @param floor_h 階の高さ（`cur_hgt`）。
 * @details 原作は十字キーのたびに Term のカーソルを動かすだけで地図に何も描かず、3D には動かしているマスが出なかった。
 * 照準のマス（`target_gx/gy`。足元の輪）は Sil-Q のアダプタだけが送り、Frox は送らないので輪は出ない。そこで
 * 画面側が狙いの重ね（`TargetingState`。選択肢の窓の「効果の予告」と同じ**入力を受けない**形 `preview_only`）として描く
 * 十字線のマスに運ぶ。`tools/targeting/` の狙い（`*`）の仕組みには載せない——journey の文字・`?` の一覧・`<` `>` `*` `x` が
 * 原作のまま効くように、入力は原作の `inkey()` のまま。
 * 問いが `Travel: ` の帯でないとき・カーソルが地図の外のときは何もしない。
 */
static void apply_travel_cursor(GameFrame &frame, bool has_cell, int gx, int gy, int floor_w, int floor_h)
{
    if (!has_cell || frame.prompt.text_utf8.empty() || !frame.prompt.text_utf8.starts_with(ui_label("Travel: "))) {
        return;
    }
    if ((floor_w <= gx) || (floor_h <= gy) || (gx < 0) || (gy < 0)) {
        return; //!< 範囲外の点を送ると画面側が丸ごと読み捨てる（前の画のまま止まる）
    }
    TargetingState &t = frame.targeting;
    t = TargetingState{};
    t.preview_only = true;
    t.scope = 1;
    t.revision = 1;
    t.width = floor_w;
    t.height = floor_h;
    t.mode = "point";
    t.kind = "neutral";
    t.selected = TargetingPoint{ gx, gy };
    t.source = t.selected;
    int px = 0;
    int py = 0;
    if ((fc_player_pos(&px, &py) == FC_OK) && (px >= 0) && (py >= 0) && (px < floor_w) && (py < floor_h)) {
        t.source = TargetingPoint{ px, py };
    }
    TargetingMarker marker;
    marker.point = t.selected;
    marker.selected = false; //!< 入力を受けない重ねは「選んだ」印を持てない
    marker.affected = true;
    marker.valid = true;
    t.markers.push_back(marker);
}

void fill_row0_prompt(GameFrame &frame)
{
    const presentation::LookScreenCover::Frame cover_frame(presentation::look_screen_cover()); // 見るの帯を作らなかったフレームで基準を忘れる
    fill_row0_prompt_inner(frame);
}

void fill_travel_reticle(GameFrame &frame)
{
    if (frame.prompt.text_utf8.empty()) { //!< 問いが無いフレームでは Term を読まない
        return;
    }
    int gx = 0;
    int gy = 0;
    int floor_w = 0;
    int floor_h = 0;
    if ((fc_term_cursor_cave_cell(&gx, &gy) != 0) && (fc_floor_size(&floor_w, &floor_h) == FC_OK)) {
        apply_travel_cursor(frame, true, gx, gy, floor_w, floor_h);
    }
}

static void fill_row0_prompt_inner(GameFrame &frame)
{
    frame.prompt = PromptBar{};
    frame.numeric = NumericInput{};
    presentation::set_repeat_count_input_active(false);

    /*
     * 見る・狙うの途中で `r` を押すと、モンスターの思い出（`mon_display_instance()`）が行 1 から重なる。
     * 行 0 は見る側の問い（`[r,/,q,t,p,o,x,j,+,-,?,<dir>]`）のまま残るので、これを軽い帯として出すと
     * 窓の側が帯だけを描いて本文（思い出の字）を出さず、中央が真っ暗になっていた。
     * 末尾が「何かキーを押してください」の窓は押せば閉じるだけなので、問いは立てない（本文の窓にする）。
     */
    if (frame.menu_open) {
        for (const auto &line : frame.menu_term_lines) {
            if (has_text(line.text_utf8, "[Press Any Key to Continue]", u8"[何かキーを押してください]")) {
                return;
            }
        }
    }

    /* The world-map overview owns direction keys: pressing a direction commits
     * travel.  Expose only its non-directional Look action through the same
     * lightweight shoulder prompt used by native Look, instead of letting the
     * full Modern dialog consume the D-pad. */
    for (std::size_t i = 0; i < frame.menu_term_lines.size(); ++i) {
        const auto &text = frame.menu_term_lines[i].text_utf8;
        //! 日本語は `広域の地図: [*/x/l] で見回す  [方向] で移動  …`（`wild.c`。カタログの `World Map:` ほか）。
        //! 日本語は `広域の地図 [*/x/l] …`——見出しの `:` は次の ` [`（桁 10）に上書きされて画面に出ない（実機で確かめた 2026-10-07）。
        if (!has_text(text, "World Map:", u8"広域の地図 [") || !has_text(text, "Look around", u8"で見回す")) continue;
        /*
         * **行 0 がスペースだけを受ける問いなら、広域の地図の帯より先にそれを問いにする**（調-15・F-frox-206）。
         * 荒野の待ち伏せの部屋（`wild.c` の `_build_room()`・`ROOM_AMBUSH`）は、広域の地図の確認画面がまだ画面に残ったまま
         * 〔新しい局所の地図を作っている最中〕行 0 へ「続けるにはスペースを押してください。」を出し、素の `inkey()` でスペースだけを待つ。
         * 下の広域の地図の帯が先に当たっていたので、決定（A）が見回す（`*`）を送ってスペースが届かず、問いも画面に出なかった
         * （61 回の待ち伏せのすべてで「部屋にならない」と見えていた。コアは部屋を作って待っていた）。
         */
        PromptBar space_prompt;
        std::string row0 = term_row_text(0);
        while (!row0.empty() && row0.back() == ' ') row0.pop_back();
        if (!row0.empty() && parse_prompt_line(row0, space_prompt) && (space_prompt.choices.size() == 1)
            && (space_prompt.choices[0].key == ' ')) {
            frame.prompt = std::move(space_prompt);
            frame.prompt.line_index = mirror_line_of(frame, 0);
            //! 下の広域の地図の帯と同じ理由で、画面の行から読んだ選択肢（`[*/x/l]` ほか）は捨てる（カーソル層が決定を食う）。
            frame.menu_choices.clear();
            frame.menu_core_cursors.clear();
            frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
            return;
        }
        frame.prompt.text_utf8 = ui_label("World Map: ");
        const auto at = text.find('*');
        frame.prompt.choices.push_back({ ui_label("Look around"), '*', at == std::string::npos ? 0 : static_cast<int>(at), 1 });
        frame.prompt.footer_utf8 = ui_label("LB/RB: select action  A/Enter: run  D-pad: travel");
        frame.prompt.line_index = static_cast<int>(i);
        /*
         * **画面の行から読んだ選択肢（`[*, x, l] …` と `[>, <] …`）は捨てる。**残すと、画面側のカーソル層が
         * 十字キー・矢印キーを選択肢の選び直しに食い、方向がコアへ 1 つも届かず**広域の移動ができなかった**
         * （プロトコルの記録で、十字キーを押してもコアへ入力が出ないことを確かめた。
         * コアへ直接 `6` を送ると移動に入る）。見回す（`*`）は上の問いの選択肢（LB/RB で選び A で実行）が受け持つ。
         * 共通部分（画面のカーソル層）は直さず、アダプタで取り込む。
         */
        frame.menu_choices.clear();
        frame.menu_core_cursors.clear();
        frame.menu_core_cursor = MenuChoice{ -1, 0, 0, 0, 0, 0 };
        return;
    }

    /*
     * `[y/n]` はメッセージの箱に出る（`get_check()` → `msg_prompt()`）。箱は
     * **最大 10 行 × 幅 MIN(72, wid-13)**（`ui_msg_rect()`。`xtra2.c:3496`）で、
     * 長い前置きが積まれると問いの行が 3 行目より下へ落ちる——wizard の確認
     * （`^A` の初回）で実際に落ちた（2026-08-24）。箱の全行を**下から**試す
     * （メッセージは下の行ほど新しい）。
     *
     * **箱の幅で切ってから照合する**のが要——行 1 以降の右端には状態列の字
     * （`LEVEL` `EXP` …）が残っており、行の素の末尾は `[y/n]` にならない。
     * 切った中身が地形の字で偶然 `[y/n]` に終わることは実質無い（5 字の並び）。
     */
    int term_cols = 0;
    int term_rows = 0;
    (void)fc_term_size(&term_cols, &term_rows);

    /*
     * **箱の場所はコアに聞く**（FH-13）。決め打ちの `MIN(72, wid-13)` で切って
     * いたが、**店・我が家・建物・クエストでは `rect(0,0,80,3)` へ移る**ので、
     * 68 桁目以降に出た `[y/n]` を丸ごと取りこぼしていた（実機で見つけたこと
     * 2026-08-24「y/n の選択がカーソルでできない」）。
     * 聞けないとき（`init_angband()` 前）だけ従来の決め打ちへ落ちる。
     */
    int box_x = 0;
    int box_y = 0;
    int box_w_i = (term_cols > 13) ? (std::min)(72, term_cols - 13) : 72;
    int box_h = 10;
    if (fc_msg_rect(&box_x, &box_y, &box_w_i, &box_h) != FC_OK) {
        box_x = 0;
        box_y = 0;
        box_w_i = (term_cols > 13) ? (std::min)(72, term_cols - 13) : 72;
        box_h = 10;
    }
    /*
     * 高さは**いま使っている行数**なので 0 にもなる。少なくとも 3 行は見る
     * ——`msg_prompt()` は箱へ書き足す途中の行数を返しうるし、長い品名の
     * 折り返しで尻尾が下の行へ落ちる。
     */
    const int begin_row = (std::max)(0, box_x >= 0 ? box_y : 0);
    const int rows = (std::max)(3, box_h);
    const int end_row = (std::min)((term_rows > 0) ? term_rows : begin_row + rows,
        begin_row + rows);
    const std::size_t clip_begin = static_cast<std::size_t>((std::max)(0, box_x));
    const std::size_t clip_end = clip_begin + static_cast<std::size_t>((std::max)(1, box_w_i));

    const auto clipped_row=[&](int y) {
        return term_row_text(y,int(clip_begin),int(clip_end));
    };
    /*
     * 「繰り返し回数」（`0` の後の `Count: 12`。日本語では `回数: 12`）。`request_command()`
     * は `prt(…, 0, 0)` で行 0 の頭へ書き、数字を 1 キーずつ読む。変愚と同じ枠を出す
     * （`input_event_adapter.h`）。
     */
    if (presentation::fill_repeat_count_input(term_row_text(0), frame.numeric)) {
        return;
    }
    // Frox's current message can span the entire message rectangle. Its
    // continuation marker belongs to the final row, not necessarily row zero.
    // Read only this rectangle so a shop's stock or the map cannot leak in.
    if(fc_awaiting_command()==0&&box_h>0) {
        std::vector<std::string> lines;
        for(int y=begin_row;y<end_row;++y) {
            auto text=clipped_row(y);while(!text.empty()&&text.back()==' ')text.pop_back();
            lines.push_back(std::move(text));
        }
        /*
         * **-続く- の行は「箱の最後の行」とは限らない**（2026-10-07）。上で少なくとも 3 行を見るので、
         * 箱が 1〜2 行のときは箱の下の地図の行まで入る。最後の行で見ていたので、地図に字がある
         * 所（町・明るい部屋）では -続く- を見落とし、画面に何も出ないまま原作が -続く- で待った
         * （気づいたこと「レベル 10 の能力値の画面に入る際に操作を受け付けなくなった」。Android）。
         */
        lines.resize(more_marker_line_count(lines));
        if(!lines.empty()) {
            /*
             * **全文を `\n` でつないで渡す**——`menu_open` 真（店・知識などの実在の
             * メニューの中）ではこれが `native_modern.h::capture()` の全画面ダイアログ
             * （`modern_prompt_text()`）へそのまま入り、複数行の箱を丸ごと見せられる。
             *
             * `menu_open` 偽（通常のダンジョン画面）では AH-35 以来ダイアログへは
             * 昇格しない——`draw_prompt_bar()`（`hd2d/ui/game_hud.cpp`）が
             * `frame.prompt.text_utf8` を**1 行の帯**へそのまま出すので、複数行を
             * 混ぜて渡すと `-more-` が末尾以外の行に埋もれて見えなくなる
             * （レビュー担当の指摘 2026-09-25）。帯の側は最終行だけを描くように
             * 直したが、案内（「決定で続ける」）も添えたいので、ここでは
             * `footer_utf8` へ乗せる——**遊んでいる最中だけ**（メニューの中の
             * 複数行ダイアログはダイアログ自身の操作説明を出すのでここでは触らない）。
             */
            for(const auto &line:lines){if(!frame.prompt.text_utf8.empty())frame.prompt.text_utf8+='\n';frame.prompt.text_utf8+=line;}
            if(!frame.menu_open) {
                const bool ja=fc_lang_enabled()!=0;
                frame.prompt.footer_utf8=ja
                    ?reinterpret_cast<const char*>(u8"決定: 続ける　／　n: まとめて読み飛ばす　／　Esc: すべて読み飛ばす")
                    :"Confirm: continue   n: skip this batch   Esc: skip all";
            }
            return;
        }
    }
    PromptBar prompt;
    for (int y = end_row - 1; y >= begin_row; --y) {
        std::string clipped = clipped_row(y);
        if (presentation::is_monster_symbol_prompt(clipped)) {
            presentation::expose_ascii_symbol_prompt(frame, clipped);
            return;
        }
        bool whole_row = false;
        if (!parse_prompt_line(clipped, prompt)) {
            /*
             * **見る（l）の行 0 は、箱の幅より長いことがある**（調-13）。原作は `prt(out_val, 0, 0)` で行 0 の頭から
             * 端末の幅いっぱいまで書くが、上は箱の幅（80 桁の端末で 67 桁）で切っている。敵の行は
             * 「名前 (睡眠中) (距離 N) [r,/,q,t,p,o,x,j,+,-,?,<dir>]」と長く、日本語だと末尾の `<dir>]` が切れて
             * 見るの判定が外れ、帯も窓も消えた（入力待ちが none になる）。行 0 だけは端末の全幅で見直す。
             */
            if (y == 0) {
                std::string whole = term_row_text(0);
                while (!whole.empty() && whole.back() == ' ') whole.pop_back();
                if (whole.find("<dir>") != std::string::npos && parse_prompt_line(whole, prompt)) {
                    whole_row = true;
                }
            }
            //! 文の途中で折り返された、スペースだけを受ける問い（体力の警告）。下の行から見ているので、上の行と合わせる。
            if (!whole_row && ((y <= begin_row) || !parse_wrapped_space_prompt(clipped_row(y - 1), clipped, prompt))) {
                continue;
            }
        }
        /*
         * 桁は**行の頭から数えた位置**で運ぶ（画面は行の字に枠を重ねる）。
         * 箱が右へずれている画面のために、切り落とした分を足し戻す。
         */
        if (clip_begin > 0 && !whole_row) {
            for (PromptChoice &choice : prompt.choices) {
                choice.begin += static_cast<int>(term_row_text(y,0,int(clip_begin)).size());
            }
        }
        frame.prompt = std::move(prompt);
        frame.prompt.line_index = mirror_line_of(frame, y);
        return;
    }

    /*
     * 個数も**箱の全行を下から**見る（FH-15）。`get_quantity()` は
     * `prt(prompt, 0, 0)` なので行 0 だが、**店の個数は `msg_input_num()`**
     * ＝メッセージ行へ書く（`message.c:626`）ので、店の箱（行 0〜2）の
     * どこにでも出る。行 0 だけを見ていたので拾えていなかった。
     */
    NumericInput numeric;
    for (int y = end_row - 1; y >= begin_row; --y) {
        std::string clipped = clipped_row(y);
        if (!parse_numeric_input_line(clipped, numeric)) {
            continue;
        }
        if (clip_begin > 0) {
            numeric.value_begin += static_cast<int>(term_row_text(y,0,int(clip_begin)).size());
        }
        frame.numeric = std::move(numeric);
        frame.numeric.line_index = mirror_line_of(frame, y);
        return;
    }
    if(fc_text_edit_active()!=0&&frame.menu_term_lines.empty()) {
        int cx=0,cy=0;if(fc_term_cursor(&cx,&cy,nullptr)==FC_OK) {
            frame.prompt.text_utf8=clipped_row(cy);frame.menu_term_curs_col=std::max(0,cx-box_x);
        }
    }
}

/*!
 * @brief **連番で続く最初の letter の並び**だけを残す（フック #37 の受け）。
 * @param per_line 行ごとの札（呼び出し側が作ったもの。ここで間引く）
 * @return 1 件でも残ったか
 *
 * @details 品選びの窓（`obj_prompt`）は**下の画を消さずに重なる**ので、
 * 店で開くと画面に letter の並びが 2 つ出る——窓の中の持ち物（`a) b)`）と、
 * 下に残った店の在庫（`a) 〜 o)`）である。**同じ letter が 2 度出る**ので、
 * 素通しにすると「品 a を選んだつもりで店の a を指す」札ができる。
 *
 * 窓は**必ず上に描かれる**（`doc_sync_menu()` が `ui_doc_menu_rect()` の頭から置く）。
 * そこで**上から見て最初に見つかる連番の並び**だけを採る。変愚が店で使っている手と
 * 同じである（`presentation/bridge/presentation_bridge.cpp` の
 * `collect_item_prompt_choices()`。あちらは行 0 の `(a-x)` から `lo` を読むが、
 * Frox の `obj_prompt` は範囲を書かないので `a` から数える）。
 *
 * **角括弧の札は 1 つも残さない。** 下に残っている店の命令列がそれである。
 */
bool keep_first_letter_run(std::vector<std::vector<ChoiceTokenGroup>> &per_line)
{
    int expected = 'a';
    bool started = false;
    bool done = false;
    bool kept = false;
    for (auto &groups : per_line) {
        std::vector<ChoiceTokenGroup> take;
        if (!done) {
            for (const ChoiceTokenGroup &g : groups) {
                if (g.bracket || g.paren || g.keys.empty()) {
                    continue;
                }
                if (g.keys[0] != expected) {
                    continue;
                }
                take.push_back(g);
                started = true;
                kept = true;
                //! `z` の次は `A`（`slot_label()` と同じ並び）。
                expected = (expected == 'z') ? 'A' : (expected + 1);
                break; //!< 1 行から採るのは 1 つだけ
            }
            /*
             * **並びが途切れたら、そこから先は採らない。** 窓の下の在庫がまた
             * `a)` から始まるので、止めないと 2 つ目の並びを拾い直してしまう。
             * ただし**始まる前の行は数えない**（窓の見出しと合計の行が上にある）。
             */
            if (started && take.empty()) {
                done = true;
            }
        }
        /*
         * **採らなかった行は必ず空にする。** ここで `break` して抜けると、
         * 残りの行は元の札を持ったままになり、呼び手がそれを積んでしまう
         * （2026-08-28 に実測で踏んだ。窓の 2 件のはずが 29 件出た）。
         */
        groups = std::move(take);
    }
    return kept;
}

/* ================================================================ 選択肢の走査 */

std::vector<ModernAction> birth_menu_actions(const GameFrame &frame)
{
    std::vector<ModernAction> actions;
    if(!frame.pre_game_menu||frame.text_input_active||frame.numeric.active||!frame.prompt.choices.empty()||frame.menu_choices.empty())return actions;
    const auto trim=[](const std::string &s){auto a=s.find_first_not_of(" \t");return a==std::string::npos?std::string():s.substr(a,s.find_last_not_of(" \t")-a+1);};
    bool summary=false;std::string keys;
    for(const auto &line:frame.menu_term_lines) {
        const auto text=trim(line.text_utf8);
        if(line.source_row==0&&(text.starts_with("Name")||text.starts_with(reinterpret_cast<const char*>(u8"名前"))))summary=true;
    }
    if(!summary)return actions;
    // py_birth.c: race/class/personality/subrace selectors share these four
    // commands even when their native display omits the command band.
    for(const auto &line:frame.menu_term_lines) {
        const auto text=trim(line.text_utf8);
        if(text=="Choose Patron"||text=="Choose Game Speed"||text==reinterpret_cast<const char*>(u8"パトロンを選ぶ")||text==reinterpret_cast<const char*>(u8"ゲーム速度を選ぶ")){keys="\t=";break;}
        if(text=="Choose Your Primary Magic Realm"||text=="Choose Your Secondary Magic Realm"||text==reinterpret_cast<const char*>(u8"第一の魔法領域を選ぶ")||text==reinterpret_cast<const char*>(u8"第二の魔法領域を選ぶ")){keys="=?!";break;}
        if(text.starts_with("Choose ")||text.ends_with(reinterpret_cast<const char*>(u8"を選ぶ")))keys="\t=?!";
    }
    if(keys.empty())return actions;
    const bool ja=fc_lang_enabled()!=0;
    for(char key:keys) {
        const char *en=key=='\t'?"More information":(key=='='?"Birth options":(key=='?'?"Help for this screen":"Getting started"));
        const char8_t *jp=key=='\t'?u8"詳しい情報":(key=='='?u8"初期オプション":(key=='?'?u8"この画面のヘルプ":u8"遊び方の説明"));
        actions.push_back({"key:"+std::to_string(int(key)),ja?reinterpret_cast<const char*>(jp):en,"",true,int(key)});
    }
    return actions;
}

bool fill_menu_choices(GameFrame &frame)
{
    if (!frame.menu_open || frame.menu_term_lines.empty()) {
        return false;
    }

    if (collect_death_screen_choices(frame)) {
        return true;
    }

    if (collect_custom_key_controls(frame)) {
        return true;
    }

    /* Journey rows are `[key] <live-map-glyph> destination`.  They are a
     * dedicated screen grammar: the generic bracket parser intentionally
     * expects the label immediately after `]` and therefore cannot safely be
     * broadened without turning ordinary bracketed prose/stat text into keys. */
    if (collect_journey_choices(frame)) {
        return true;
    }

    std::vector<std::vector<ChoiceTokenGroup>> per_line;
    per_line.reserve(frame.menu_term_lines.size());
    bool has_bracket = false;
    for (const TermMirrorLine &line : frame.menu_term_lines) {
        per_line.push_back(scan_choice_tokens(line.text_utf8));
        for (const ChoiceTokenGroup &g : per_line.back()) {
            has_bracket = has_bracket || g.bracket;
        }
    }

    /*
     * **命令列のある画面で、コアが何も訊いていない間は命令列だけを札にする**
     * （FH-14。2026-08-24 に決めた「コマンド行だけに限る」）。
     *
     * 店の画面は下段が `[b/p/g] Buy …` の命令列で、その上に品行 `a) …` が並ぶ。
     * **待ち受け中に押した letter は「品の指定」ではなく「店のコマンド」**である
     * （`shop.c:1552` の switch。`d)` の品を選ぶつもりで押すと売りの口が開く）。
     * 品行を札にしてよいのは、コアが「どの品を買う?」と**訊いている間だけ**。
     *
     * その見分けは**メッセージ行に中身があるか**で付く——店の待ち受けは
     * `inkey_special()` の**直後**に `msg_line_clear()` する（`shop.c:1541`）ので
     * 待ち受け中は空（`doc_line_count == 0`）、`msg_command()` が問いを書いている
     * 間だけ 1 以上になる（`message.c:462`）。**語や画面名で名指ししない**ので
     * M1 の日本語化でも壊れない。
     *
     * 命令列の無い画面（誕生・オプション・品選びの doc UI）は素通し——
     * `[Press Any Key to Continue]` の類は「`]` の直後が空白＋札の頭」で弾かれる。
     *
     * **逆に、コアが訊いている間は品行だけにする**（2026-08-28。変愚と同じ仕様）。
     * `Buy which item?` の答えは品の letter だけで、命令列の札は 1 つも受け付けない
     * （`shop.c:2318` の `if (cmd < 'a' || cmd > 'z') continue;`）。それでいて
     * `[b/p/g]` `[s/d]` `[x]` の送るキーは `b` `s` `x` ——**どれも a〜z の範囲**なので、
     * 積んだままにすると命令のつもりで選んだ札が**品 b・品 s・品 x を買う**。
     * 変愚は同じ場面を `MenuChoiceScreen::ItemPrompt` で品行だけに絞っている
     * （`presentation/bridge/presentation_bridge.cpp` の `collect_item_prompt_choices`）。
     */
    /*
     * ---- **品選びの窓が開いている間は、窓の中の品だけ**（#37。2026-08-28）----
     *
     * `obj_prompt()` の問いは `msg_line` ではなく doc の窓へ出るので、
     * 下の「メッセージ行に中身があるか」では見分けられない。旗はコアが立てる。
     * 2026-08-28 に気づいた「店の譲る（売る）を選択した際に、売る対象の
     * リストにカーソルが移らない」——`no_selling`（既定 ON）の道はここを通る。
     */
    if (fc_obj_prompt_active() != 0) {
        if (keep_first_letter_run(per_line)) {
            for (std::size_t i = 0; i < per_line.size(); ++i) {
                if (frame.menu_choices.size() >= kMenuChoiceMax) {
                    break;
                }
                if (!per_line[i].empty()) {
                    emit_choices_for_line(frame, i, per_line[i]);
                }
            }
            append_frox_selector_aux_choices(frame);
            return !frame.menu_choices.empty();
        }
        /*
         * 窓は開いているのに連番が 1 つも無い（品が 0 件・呪文の別書式など）。
         * **命令列だけを残す道へは落とさない**——下に残っている店の命令は
         * いま押しても効かないので、素通しにして下の従来の道へ任せる。
         */
    }

    int msg_lines = 0;
    const bool msg_known = (fc_msg_rect(nullptr, nullptr, nullptr, &msg_lines) == FC_OK);
    const bool asking = msg_known && (msg_lines > 0);
    const bool commands_only = has_bracket && msg_known && !asking
        && (fc_obj_prompt_active() == 0);
    const bool items_only = has_bracket && asking;

    for (std::size_t i = 0; i < per_line.size(); ++i) {
        if (frame.menu_choices.size() >= kMenuChoiceMax) {
            break;
        }
        std::vector<ChoiceTokenGroup> groups = std::move(per_line[i]);
        if (commands_only || items_only) {
            const bool keep = commands_only; // 残すのは bracket == keep の札だけ
            groups.erase(std::remove_if(groups.begin(), groups.end(),
                             [keep](const ChoiceTokenGroup &g) { return g.bracket != keep; }),
                groups.end());
            if (groups.empty()) {
                continue;
            }
        }
        emit_choices_for_line(frame, i, groups);
    }
    append_frox_selector_aux_choices(frame);
    return !frame.menu_choices.empty();
}

int audit_semantics_selftest()
{
    int failed = 0;
    const auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::fprintf(stderr, "[frox] selftest: audit semantics FAIL - %s\n", what);
        ++failed;
    };
    const auto line = [](int row, std::string text) {
        TermMirrorLine out;
        out.source_row = row;
        out.text_utf8 = std::move(text);
        return out;
    };
    const auto has_key = [](const GameFrame &frame, int key) {
        return std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
            [key](const MenuChoice &choice) { return choice.key == key; });
    };
    const auto prompt_has_key = [](const PromptBar &prompt, int key) {
        return std::any_of(prompt.choices.begin(), prompt.choices.end(),
            [key](const PromptChoice &choice) { return choice.key == key; });
    };

    for (const int key : {'_', '@', '}'}) {
        ChoiceTokenGroup group;
        std::string text = "(x) command";
        text[1] = static_cast<char>(key);
        check(parse_paren_choice_token(text, 0, group) && !group.keys.empty() && group.keys.front() == key,
            "punctuation parenthesized command");
    }
    {
        ChoiceTokenGroup group;
        check(!parse_paren_choice_token("(+2) item bonus", 0, group), "item bonus is not a command");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0,
            "Use which spell? Browse '?'  '!' to Label  'C' for character sheet  '=' to wipe inactive"));
        append_frox_selector_aux_choices(frame);
        for (const int key : {'?', '!', 'C', '='}) check(has_key(frame, key), "spell selector auxiliary");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Choose technique (?=Browse !=Damage ESC=exit)"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '?') && has_key(frame, '!'), "Hissatsu selector auxiliaries");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Stop which hex? ('l' to all)"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'l'), "Hex stop-all auxiliary");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Hissatsu: ?=Browse  !=Damage  ESC=exit"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '?') && has_key(frame, '!'), "Hissatsu selector auxiliary");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Stop which hex spell? ('l' to all)"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'l'), "Hex stop-all auxiliary");
    }
    {
        GameFrame frame;
        MenuChoice base{};
        base.key = 'a';
        base.label_utf8 = "slot a";
        frame.menu_choices.push_back(base);
        frame.menu_term_lines.push_back(line(0, "Use which device? '/' Cycle Type '\\' '{'"));
        append_frox_selector_aux_choices(frame);
        for (const int key : {'/', 'w', 's', 'r', '\\', '{', 'A'}) check(has_key(frame, key), "device selector auxiliary");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Monster list"));
        frame.menu_term_lines.push_back(line(23, "[Press P for probing, ? for help or ESC to exit]"));
        append_frox_selector_aux_choices(frame);
        for (const int key : {'/', 'P', 'N', 'T', 'J', '+', '-', '?'}) check(has_key(frame, key), "monster list auxiliary");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Press a listed key to jump to it. ? for help or ESC to return."));
        check(collect_custom_key_controls(frame), "custom-key browser recognized");
        for (const int key : std::array<int, 4>{'4', '6', '?', 27}) check(has_key(frame, key), "custom-key browser controls");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Previewing Replace results."));
        check(collect_custom_key_controls(frame), "custom-key preview recognized");
        for (const int key : std::array<int, 6>{'4', '6', '?', '\t', '\r', 27}) check(has_key(frame, key), "custom-key preview controls");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0,
            "Reset (m)acros, current (k)eyset, both (K)eysets, (y)everything, or (n)othing?"));
        check(collect_custom_key_controls(frame), "custom-key reset recognized");
        for (const int key : std::array<int, 8>{'m', 'k', 'K', 'y', 'n', '\r', '?', 27}) check(has_key(frame, key), "custom-key reset controls");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Please choose a journey target:"));
        frame.menu_term_lines.push_back(line(2, "[a] @ First destination  [b] # Second destination  [Esc] Return"));
        check(collect_journey_choices(frame), "journey selector recognized");
        for (const int key : std::array<int, 3>{'a', 'b', 27}) check(has_key(frame, key), "journey selector controls");
    }

    for (const auto &entry : std::array<std::pair<const char *, int>, 5>{
             std::pair<const char *, int>{"Request: 12", 12},
             {"Pay back: 34", 34},
             {"Withdraw: 56", 56},
             {"Additional deposit: 78", 78},
             {"New deposit: 90", 90},
         }) {
        NumericInput numeric;
        check(parse_numeric_input_line(entry.first, numeric) && numeric.active && numeric.value == entry.second,
            "Cornucopia numeric field");
    }
    {
        //! 見る（l）の帯に、見ている物の名前（行 0 の頭の説明）が残る（調-13）。選択肢だけだと何を見ているか分からなかった。
        PromptBar look;
        const auto keeps = [&](const std::string &row, const char *subject) {
            if (!parse_prompt_line(row, look) || look.choices.empty()) return false;
            const std::size_t at = look.text_utf8.find(subject);
            return at != std::string::npos && look.text_utf8.find('[', at + std::strlen(subject)) == static_cast<std::size_t>(look.choices.front().begin) - 1;
        };
        check(keeps("a Giant white mouse (Asleep) (Rng 3) [r,/,x,q,p,o,x,j,+,-,?,<dir>]", "a Giant white mouse (Asleep) (Rng 3)"),
            "look band keeps the monster being looked at (cho-13)");
        check(keeps("You see a Soft Leather Armor [2,+0] [r,/,q,p,o,+,-,?,<dir>]", "a Soft Leather Armor [2,+0]"),
            "look band keeps the item name even with a bracket in it (cho-13)");
        check(keeps("a Cave spider (Rng 2) -more- [r,/,q,p,o,+,-,?,<dir>]", "a Cave spider (Rng 2)")
                && look.text_utf8.find("-more-") == std::string::npos,
            "look band drops the -more- mark (cho-13)");
        check(keeps(utf8_of(u8"ジャイアント・ホワイト・マウス (睡眠中) (距離 3) [r,/,x,q,p,o,x,j,+,-,?,<dir>]"), utf8_of(u8"ジャイアント・ホワイト・マウス (睡眠中) (距離 3)")),
            "ja look band keeps the monster being looked at (cho-13)");
    }
    {
        PromptBar prompt;
        check(parse_prompt_line("Fetch: a useful item [*,t,q]", prompt), "Fetch prompt recognized");
        for (const int key : std::array<int, 3>{'*', 't', 'q'}) check(prompt_has_key(prompt, key), "Fetch prompt controls");
    }
    {
        PromptBar prompt;
        check(parse_prompt_line("Create [S]hots, [A]rrows or [B]olts", prompt), "Archer prompt recognized");
        for (const int key : std::array<int, 4>{'S', 'A', 'B', 27}) check(prompt_has_key(prompt, key), "Archer prompt controls");
    }
    {
        PromptBar prompt;
        check(parse_prompt_line("Please verify SUICIDE by typing the '@' sign", prompt), "raw verification prompt recognized");
        check(prompt_has_key(prompt, '@') && prompt_has_key(prompt, 27), "raw verification controls");
    }
    {
        //! スペースだけを受ける問い（体力の警告ほか）。決定がスペースを送る選択肢を立てる（2026-10-09）。
        for (const std::string &text : std::array<std::string, 5>{
                 "The Novice warrior attacks you: He hits. *** LOW HITPOINT WARNING! *** Press Space to continue.",
                 "*** THROWN FROM MOUNT! *** Press Space to continue.", "Press Space to continue.",
                 "Press the space bar to continue", "...He hits. *** LOW HITPOINT WARNING! *** Press Space to continue" }) {
            PromptBar prompt;
            check(parse_prompt_line(text, prompt), "space-only prompt recognized");
            check(prompt.choices.size() == 1 && prompt_has_key(prompt, ' '), "space-only prompt sends one Space");
            check(text.find("***") == std::string::npos || prompt.text_utf8.rfind("***", 0) == 0, "space-only prompt text starts at the warning");
        }
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(23, "World Map: [*/x/l] Look around  [direction] Travel  [>/< /Esc] Exit to local map"));
        //! 画面の行から読まれた選択肢が残ると、画面側のカーソル層が十字キーを食って移動に入れない（2026-10-09）。
        frame.menu_choices.push_back(MenuChoice{ 0, 12, 20, 13, 1, '*' });
        frame.menu_choices.push_back(MenuChoice{ 0, 60, 30, 61, 1, '>' });
        fill_row0_prompt(frame);
        check(frame.menu_choices.empty(), "world-map overview keeps no screen-line choices (D-pad must reach the core)");
        check(prompt_has_key(frame.prompt, '*'), "world-map Look action");
        check(frame.prompt.footer_utf8.rfind("LB/RB:", 0) == 0, "world-map native-motion prompt");
        check(!prompt_has_key(frame.prompt, '8') && !prompt_has_key(frame.prompt, '2')
            && !prompt_has_key(frame.prompt, '4') && !prompt_has_key(frame.prompt, '6'), "world-map directions stay native");
    }
    {
        //! 移動の目標のマスを選ぶ問い：カーソルのマスを、入力を受けない狙いの重ね（十字線）として運ぶ（修-32）。
        GameFrame travel;
        travel.prompt.text_utf8 = ui_label("Travel: ") + "[x]";
        apply_travel_cursor(travel, true, 12, 7, 100, 50);
        check(travel.targeting.preview_only && !travel.targeting.active && travel.targeting.markers.size() == 1
                && travel.targeting.markers[0].point == TargetingPoint{ 12, 7 } && travel.targeting.selected == TargetingPoint{ 12, 7 },
            "travel prompt carries the cursor cell as a preview-only reticle");
        check(!travel.targeting.markers.empty() && !travel.targeting.markers[0].selected && !travel.targeting.can_confirm, "travel reticle takes no input (no selected marker, no confirm)");
        {
            std::string error;
            TargetingState decoded;
            check(decode_targeting(encode_targeting(travel.targeting), decoded, error), "travel reticle survives the screen-side decoder");
        }
        apply_travel_cursor(travel, true, 13, 7, 100, 50);
        check(travel.targeting.markers.size() == 1 && travel.targeting.markers[0].point == TargetingPoint{ 13, 7 }, "travel reticle follows the D-pad");
        GameFrame outside;
        outside.prompt.text_utf8 = ui_label("Travel: ") + "[x]";
        apply_travel_cursor(outside, false, 12, 7, 100, 50);
        check(!outside.targeting.preview_only && outside.targeting.markers.empty(), "travel cursor outside the map draws nothing");
        apply_travel_cursor(outside, true, 100, 7, 100, 50);
        check(!outside.targeting.preview_only, "travel cursor outside the floor is never sent");
        GameFrame look;
        look.prompt.text_utf8 = ui_label("Look: ") + "[x]";
        apply_travel_cursor(look, true, 12, 7, 100, 50);
        check(!look.targeting.preview_only, "other prompts never carry the travel reticle");
        GameFrame none;
        apply_travel_cursor(none, true, 12, 7, 100, 50);
        check(!none.targeting.preview_only, "no prompt, no reticle");
    }
    {
        //! 建物のクエストの説明文：読み終えた決定が、命令を繰り返さず説明文を閉じる（修-33）。
        check(is_quest_message_text("Orc Camp (Anambar) (Level 20): Roving orcs have been approaching."), "quest message (en)");
        check(is_quest_message_text(utf8_of(u8"オークのキャンプ (アナンバール)（レベル 20）: 荒らし回るオーク")), "quest message (ja)");
        check(!is_quest_message_text("You do not have enough gold."), "other building message is not a quest message");
        check(!is_quest_message_text(utf8_of(u8"あなたは現在のクエストを終了させていません！")), "'not completed yet' notice is not a quest message");
        check(repeats_command_under_message('q', 'q', true), "same key under the message only dismisses it");
        check(!repeats_command_under_message('q', 'q', false), "no message, the command runs");
        check(!repeats_command_under_message(27, 'q', true), "another command (leave the building) runs");
        check(!repeats_command_under_message(0, 0, true), "no key, nothing to swallow");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Filter: Ammo Identified Sensed Unsensed Stairs Unwanted"));
        append_frox_selector_aux_choices(frame);
        for (const int key : std::array<int, 11>{'A','D','E','N','S','U','/','J','+','-','?'})
            check(has_key(frame, key), "object-list auxiliary");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Commands: n) rename pet  d) dismiss pet"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'n') && has_key(frame, 'd'), "pet-list normal controls");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, "Dismiss which pet? ('u' for all unnamed pets)"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'u'), "pet-list dismiss-all control");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Y to confirm, y to toggle list"));
        append_frox_selector_aux_choices(frame);
        for (const int key : std::array<int, 11>{'Y','n','N','y','u','U','?',27,'/','+','-'})
            check(has_key(frame, key), "unwanted-preview controls");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Select a form; '?' to recall, '=' for more info"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '?') && has_key(frame, '=') && has_key(frame, '\t'), "form auxiliary controls");
    }
    {
        GameFrame frame;
        MenuChoice base{};base.key='a';base.label_utf8="skill a";frame.menu_choices.push_back(base);
        frame.menu_term_lines.push_back(line(0, "Choose the Skill"));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'A'), "Skillmaster uppercase help alias");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, "Use '+'/'-' or '1' to '3' to adjust the amount of bonus to use"));
        append_frox_selector_aux_choices(frame);
        for (const int key : std::array<int, 5>{'+','-','1','2','3'}) check(has_key(frame, key), "Weaponsmith amount controls");
    }
    /*
     * 日本語の画面の補助の選択肢（2026-10-07）。**字は実機の日本語の画面から写した**（`--lang=ja` で
     * 実際に出して確かめた）。英語の文だけを探していた間は、日本語で 1 つも載らなかった。
     * 見落としやすい点を 2 つ押さえる:
     *  - 広域の地図の見出しは `広域の地図:` ではなく `広域の地図 [`（次の欠片が `:` を上書きする）。
     *  - モンスターの一覧・持ち物の一覧は行 0 が空。行 0 の問いに頼ると英語でも載らない。
     */
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"使用する力（'?' 閲覧 '!' 名札 'C' 人物調書）:")));
        append_frox_selector_aux_choices(frame);
        for (const int key : {'?', '!', 'C'}) check(has_key(frame, key), "ja power selector auxiliary");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"閲覧する力（'?' 使用 '!' 名札）:")));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '?') && has_key(frame, '!'), "ja power selector (browse) auxiliary");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"名札を付ける力（'!' 使用 '=' 未使用を消去）:")));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '!') && has_key(frame, '='), "ja power selector (label) auxiliary");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"使用する呪文（'?' 閲覧）:")));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '?'), "ja spell selector auxiliary");
    }
    {
        //! 行 0 が空の一覧（英語・日本語とも）。
        for (const auto &footer : std::array<std::string, 2>{
                 std::string("   [Press ? for help or ESC to exit]"), utf8_of(u8"   [? で説明・ESC で戻る]") }) {
            GameFrame frame;
            frame.menu_term_lines.push_back(line(0, ""));
            frame.menu_term_lines.push_back(line(1, "2 monsters"));
            frame.menu_term_lines.push_back(line(4, footer));
            append_frox_selector_aux_choices(frame);
            for (const int key : {'/', 'N', 'T', 'J', '+', '-', '?'}) check(has_key(frame, key), "monster list with an empty row 0");
        }
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, ""));
        frame.menu_term_lines.push_back(line(23, utf8_of(u8"[P で調査、? でヘルプ、ESC で終了]")));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'P') && has_key(frame, '/'), "ja probing monster list auxiliary");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, ""));
        frame.menu_term_lines.push_back(line(1, utf8_of(u8"絞り込み: Ammo|IDentified|SEnsed|UNsensed|Stairs|Unwante")));
        append_frox_selector_aux_choices(frame);
        for (const int key : std::array<int, 11>{'A','D','E','N','S','U','/','J','+','-','?'})
            check(has_key(frame, key), "ja object-list auxiliary with an empty row 0");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"コマンド: n) 名前を付ける  d) 放す  ESC) 戻る")));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'n') && has_key(frame, 'd'), "ja pet-list normal controls");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"どのペットを放すか？（'u' で名前の無いもの全部、Esc で取りやめ）")));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'u'), "ja pet-list dismiss-all control");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(1, utf8_of(u8"['?' で思い出、'=' で詳しく、ESC で取り消し、ENTER で選択]")));
        frame.menu_term_lines.push_back(line(0, ""));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, '?') && has_key(frame, '=') && has_key(frame, '\t'), "ja form auxiliary controls");
    }
    for (const auto &heading : std::array<std::string, 2>{ utf8_of(u8"技能の種類を選ぶ"), utf8_of(u8"技能を選ぶ") }) {
        GameFrame frame;
        MenuChoice base{};base.key='a';base.label_utf8="skill a";frame.menu_choices.push_back(base);
        frame.menu_term_lines.push_back(line(0, ""));
        frame.menu_term_lines.push_back(line(7, heading));
        append_frox_selector_aux_choices(frame);
        check(has_key(frame, 'A'), "ja Skillmaster uppercase help alias");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"'1' から '3' を押して使うボーナスの量を決める。")));
        append_frox_selector_aux_choices(frame);
        for (const int key : std::array<int, 5>{'+','-','1','2','3'}) check(has_key(frame, key), "ja Weaponsmith amount controls");
    }
    {
        /*
         * 化ける姿の一覧（`doppelganger.c:293`〜`:294`）の `?`（修-34。F-frox-503）。化ける側は `('?' で解説)`、解説側は `('?' で使用)` で、
         * 日本語の問いに `'?' 閲覧`・`'?' 使用` が無く `?` が申告されず、画面が「この画面では使えないキーです。」と断っていた。
         */
        for (const char *prompt : { "Mimic which Race? (Type '?' to Browse) ", "Browse which Race? (Type '?' to Use)",
                 utf8_of(u8"どの種族に化けますか? ('?' で解説) "), utf8_of(u8"どの種族を見ますか? ('?' で使用)") }) {
            GameFrame frame;
            frame.menu_term_lines.push_back(line(0, prompt));
            append_frox_selector_aux_choices(frame);
            check(has_key(frame, '?'), "doppelganger form list offers '?'");
        }
        /*
         * 上の日本語の問いは、訳のカタログの行（`J:`）と**同じ字**でなければならない。カタログがあるとき（開発ツリーで流したとき）は、
         * 英語の鍵（`E:`）の次の `J:` 行を読んで、`%s` を種族の訳に、`\s` を空白に直した字で同じ確認をする。
         */
        std::FILE *catalog = std::fopen("frox/lang/ja/ui/messages.ja.txt", "rb");
        if (catalog != nullptr) {
            std::string previous;
            std::vector<std::string> prompts;
            char buffer[2048];
            while (std::fgets(buffer, sizeof(buffer), catalog) != nullptr) {
                std::string text = buffer;
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                    text.pop_back();
                }
                if (text.starts_with("J:") && (previous == "E:Mimic which %s? (Type '?' to Browse)\\s" || previous == "E:Browse which %s? (Type '?' to Use)")) {
                    std::string translated = text.substr(2);
                    for (const char *from : { "%s", "\\s" }) {
                        const std::string needle = from;
                        const std::string with = (needle == "%s") ? std::string(utf8_of(u8"種族")) : std::string(" ");
                        for (std::size_t at = translated.find(needle); at != std::string::npos; at = translated.find(needle, at + with.size())) {
                            translated.replace(at, needle.size(), with);
                        }
                    }
                    prompts.push_back(translated);
                }
                previous = text;
            }
            std::fclose(catalog);
            check(prompts.size() == 2, "doppelganger prompts found in the Japanese catalog");
            for (const std::string &translated : prompts) {
                GameFrame frame;
                frame.menu_term_lines.push_back(line(0, translated));
                append_frox_selector_aux_choices(frame);
                check(has_key(frame, '?'), "doppelganger catalog prompt offers '?'");
            }
        }
    }
    {
        /*
         * 死亡の画面（修-31。F-frox-202・F-frox-305）。
         *
         * (1) 命令列の読み取り。最下段の命令列だけから、画面を見る・静かに・素早く・死を欺く・死を受け入れ（Esc 前は n）の 5 つの選択肢を作り、
         * 案内の文の `[d] を押すこと。…` を選択肢にしない。蘇生の問いは r・d・n の 3 つ。
         */
        for (const bool ja : { false, true }) {
            GameFrame frame;
            frame.menu_open = true;
            frame.menu_term_lines.push_back(line(2, ja ? utf8_of(u8"***** あなたは死んだ *****      死因: テスト") : "***** You have died. *****       Killer: test"));
            frame.menu_term_lines.push_back(line(6, ja ? utf8_of(u8"[d] を押すこと。終わればこの問いかけに自動で戻る。") : "[d] now. You'll automatically return to this dialog afterwards."));
            frame.menu_term_lines.push_back(line(9, ja ? utf8_of(u8"静かな死に方の切り替え         [s]: OFF") : "Toggle silent deaths mode with [s]: OFF"));
            frame.menu_term_lines.push_back(line(19, ja ? utf8_of(u8"  [d/y] 画面を見る   [s] 静かに  [q] 素早く [w] 死を欺く     [n] 死を受け入れる")
                                                         : "  [d/y] View Screen  [s] Silent  [q] Quick  [w] Cheat Death  [n] Accept Death"));
            check(fill_menu_choices(frame), "death screen choices found");
            check(frame.menu_choices.size() == 5, "death screen has exactly 5 choices (prose is not a choice)");
            for (const int key : std::array<int, 5>{ 'd', 's', 'q', 'w', 'n' }) check(has_key(frame, key), "death screen choice keys");
            const auto labelled = [&](int key, const char *label) {
                return std::any_of(frame.menu_choices.begin(), frame.menu_choices.end(),
                    [&](const MenuChoice &c) { return c.key == key && c.label_utf8 == label; });
            };
            if (ja) {
                check(labelled('d', utf8_of(u8"画面を見る")) && labelled('s', utf8_of(u8"静かに")) && labelled('q', utf8_of(u8"素早く"))
                        && labelled('w', utf8_of(u8"死を欺く")) && labelled('n', utf8_of(u8"死を受け入れる")),
                    "ja death screen labels");
            } else {
                check(labelled('d', "View Screen") && labelled('n', "Accept Death"), "death screen labels");
            }
            check(std::none_of(frame.menu_choices.begin(), frame.menu_choices.end(),
                      [](const MenuChoice &c) { return c.label_utf8.find(utf8_of(u8"押すこと")) != std::string::npos; }),
                "death screen: prose fragments are not choices");
        }
        {
            GameFrame frame;
            frame.menu_open = true;
            frame.menu_term_lines.push_back(line(5, "  [d/y] View Screen  [s] Silent  [q] Quick  [w] Cheat Death  [Esc] Accept Death"));
            check(fill_menu_choices(frame) && has_key(frame, 27) && !has_key(frame, 'n'), "death screen after Esc: [Esc] Accept Death");
        }
        {
            GameFrame frame;
            frame.menu_open = true;
            frame.menu_term_lines.push_back(line(1, "***** You have died... *****"));
            frame.menu_term_lines.push_back(line(10, "Press [r] to be reconstructed elsewhere by the Resurrection Machine."));
            frame.menu_term_lines.push_back(line(12, "Press [d/y] to view or dump the current screen. You'll return to this screen"));
            frame.menu_term_lines.push_back(line(15, "Press [n] to decline its help and continue dying."));
            check(fill_menu_choices(frame) && frame.menu_choices.size() == 3 && has_key(frame, 'r') && has_key(frame, 'd') && has_key(frame, 'n'),
                "resurrection prompt: r, d and n");
        }
        /*
         * (2) 桁。原作は**英語の固定桁**にキーの字を塗り直す（`effects.c:56`〜`:130` の `c_put_str(TERM_ORANGE, "w", row + 12, 23)` ほか）ので、
         * 訳の行は「`[` + キーの字」がその桁に来るように桁をそろえる（全角は 2 桁）。1 桁ずれると別の字の上に塗って、全角の途中を壊す
         * （U+0080 が出る）。カタログがあるとき（リポジトリ直下で流したとき）は、英語の原文と訳の両方を検査する。
         * 行は桁 2 から書く。`end` があれば、行の幅（値の ON／OFF を塗る桁）。
         */
        struct Paint {
            const char *english;
            std::vector<std::pair<char, int>> marks;
            int end;
        };
        const std::vector<Paint> paints{
            { "Press [r] to be reconstructed elsewhere by the Resurrection Machine.", { { 'r', 9 } }, 0 },
            { "Press [d/y] to view or dump the current screen. You'll return to this screen", { { 'd', 9 }, { 'y', 11 } }, 0 },
            { "Press [n] to decline its help and continue dying.", { { 'n', 9 } }, 0 },
            { "[d] now. You'll automatically return to this dialog afterwards.", { { 'd', 3 } }, 0 },
            { "Toggle silent deaths mode with [s]:\\s", { { 's', 34 } }, 38 },
            { "Toggle quick restart mode with [q]:\\s", { { 'q', 34 } }, 38 },
            { "To continue to the next death screen, press [n].", { { 'n', 47 } }, 0 },
            { "In that case, press [w] now, to cheat death and resurrect in wizard mode, but", { { 'w', 23 } }, 0 },
            { "[d/y] View Screen  [s] Silent  [q] Quick  [w] Cheat Death  [Esc] Accept Death",
                { { 'd', 3 }, { 'y', 5 }, { 's', 22 }, { 'q', 34 }, { 'w', 45 }, { 'E', 62 } }, 0 },
            { "[d/y] View Screen  [s] Silent  [q] Quick  [w] Cheat Death  [n] Accept Death",
                { { 'd', 3 }, { 'y', 5 }, { 's', 22 }, { 'q', 34 }, { 'w', 45 }, { 'n', 62 } }, 0 },
        };
        const auto cells_of = [](const std::string &text) {
            std::string cells(2, ' '); // 桁 0〜1 は空き
            for (std::size_t at = 0; at < text.size();) {
                const unsigned char c = static_cast<unsigned char>(text[at]);
                if (c < 0x80) {
                    cells.push_back(static_cast<char>(c));
                    ++at;
                } else {
                    const std::size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
                    cells.push_back('\x01'); // 全角の左半分
                    cells.push_back('\x02'); // 右半分
                    at += len;
                }
            }
            return cells;
        };
        std::FILE *death_catalog = std::fopen("frox/lang/ja/ui/messages.ja.txt", "rb");
        if (death_catalog != nullptr) {
            std::map<std::string, std::string> translated;
            std::string previous;
            char buffer[2048];
            while (std::fgets(buffer, sizeof(buffer), death_catalog) != nullptr) {
                std::string text = buffer;
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
                if (text.starts_with("J:") && previous.starts_with("E:")) translated[previous.substr(2)] = text.substr(2);
                previous = text;
            }
            std::fclose(death_catalog);
            const auto unescape = [](std::string text) {
                for (std::size_t at = text.find("\\s"); at != std::string::npos; at = text.find("\\s", at)) text.replace(at, 2, " ");
                return text;
            };
            for (const Paint &paint : paints) {
                const auto found = translated.find(paint.english);
                check(found != translated.end(), "death screen line is in the Japanese catalog");
                for (const bool ja : { false, true }) {
                    if (ja && found == translated.end()) continue;
                    const std::string text = unescape(ja ? found->second : std::string(paint.english));
                    const std::string cells = cells_of(text);
                    for (const auto &mark : paint.marks) {
                        const std::size_t col = static_cast<std::size_t>(mark.second);
                        const bool ok = (col < cells.size()) && (cells[col] == mark.first) && (cells[col - 1] == '[' || cells[col - 1] == '/');
                        check(ok, ja ? "death screen (ja): the painted key letter sits on its column" : "death screen (en): the painted key letter sits on its column");
                    }
                    if (paint.end > 0) {
                        check(static_cast<int>(cells.size()) == paint.end, ja ? "death screen (ja): the line ends where ON/OFF is painted" : "death screen (en): the line ends where ON/OFF is painted");
                    }
                }
            }
        }
    }
    {
        PromptBar prompt;
        check(parse_prompt_line(utf8_of(u8"引き寄せ：松明 [*,t,q]"), prompt), "ja Fetch prompt recognized");
        for (const int key : std::array<int, 3>{'*', 't', 'q'}) check(prompt_has_key(prompt, key), "ja Fetch prompt controls");
        check(parse_prompt_line(utf8_of(u8"引き寄せ：松明（山の 2 個目／3 個）[*,t,q]"), prompt), "ja Fetch pile prompt recognized");
    }
    {
        for (const auto &text : std::array<std::string, 3>{ utf8_of(u8"[S] 弾、[A] 矢、[B] ボルトのどれを作りますか？"),
                 utf8_of(u8"[S] 弾と [A] 矢のどちらを作りますか？"), utf8_of(u8"[S] 弾を作りますか？") }) {
            PromptBar prompt;
            check(parse_prompt_line(text, prompt), "ja Archer prompt recognized");
            check(prompt_has_key(prompt, 'S') && prompt_has_key(prompt, 27), "ja Archer prompt controls");
        }
    }
    {
        //! 日本語は 2 か所で訳が少し違う（`files.c:3421` と `samurai.c:961`）。どちらも頭が同じ。
        for (const auto &text : std::array<std::string, 2>{ utf8_of(u8"確認のため '@' を押してください（Ctrl-E でも可）:"),
                 utf8_of(u8"確認のため '@' を押して下さい。") }) {
            PromptBar prompt;
            check(parse_prompt_line(text, prompt), "ja raw verification prompt recognized");
            check(prompt_has_key(prompt, '@') && prompt_has_key(prompt, 27), "ja raw verification controls");
        }
    }
    {
        /*
         * `msg_prompt(` の全数との突き合わせ（修-28。2026-10-12）。原作 `frox/src` の問いの末尾の綴りを、英語と日本語の両方で
         * 問いとして拾い、**原作が受ける字**（`keys`）がすべて選択肢に並ぶことを確かめる。
         * 引退の確認 `[y/n] [q = yes, and restart]` は、末尾が `[q = …]` で `[y/n]` に合わず、問いが 1 つも立たなかった（F-frox-401）。
         */
        struct Spelling {
            const char *en;
            std::u8string ja;
            std::string keys; //!< 選択肢に並ぶべき字（並びは問わない）
        };
        const std::array<Spelling, 14> spellings{
            Spelling{ "Do you really want to commit suicide? [y/n] [q = yes, and restart]",
                u8"本当に自殺しますか。[y/n] [q = はい、そして再開]", "nyq" },
            { "Really use Potion of Death? [y/n]", u8"死の薬を本当に使いますか？ [y/n]", "yn" },
            { "Really destroy a Dagger? [y/n/Auto]", u8"短刀を本当に壊しますか？ [y/n/Auto]", "ynA" },
            { "Resume travelling? [y/n/Always]", u8"移動を再開しますか？ [y/n/Always]", "ynA" },
            { "Reset all birth options to defaults? [y/n]", u8"誕生時オプションをすべて既定に戻しますか？ [y/n]", "yn" },
            { "Continue? [Y/n]", u8"続けますか？ [Y/n]", "Yn" },
            { "Really remove a Dagger? The entire stack will be permanently destroyed! [Y,n]", u8"短刀を本当に外しますか？ [Y,n]", "Yn" },
            { "No Messages subwindow detected. Suppress main-view messages anyway? [y/N]",
                u8"メッセージ用のサブウィンドウが見つかりません。それでもメイン画面のメッセージを止めますか？ [y/N]", "yN" },
            { "Really genocide Nagas? [Y/N]", u8"ナーガを本当に抹殺しますか？ [Y/N]", "YN" },
            { "Save dump as foo.html? [y/n]", u8"foo.html に保存しますか？ [y/n]", "yn" },
            { "Keep Frox, replace with Frog, import renamed, or cancel import? [F/R/n/c]",
                u8"Frox を残す、Frog で置き換える、名前を変えて取り込む、取り込みをやめる？ [F/R/n/c]", "FRnc" },
            { "Keep Frox, replace with Frog, or cancel import? [F/R/c]", u8"Frox を残す、Frog で置き換える、取り込みをやめる？ [F/R/c]", "FRc" },
            { "Evaporate [Q] Potion or [m] Infusion?", u8"[Q] 薬と [m] 調合薬のどちらを気化させますか？", "Qm" },
            { "Are you sure you want to leave? [Y,n]", u8"本当に出ますか？ [Y,n]", "Yn" },
        };
        for (const Spelling &spelling : spellings) {
            for (const bool ja : { false, true }) {
                const std::string text = ja ? std::string(utf8_of(spelling.ja.c_str())) : std::string(spelling.en);
                PromptBar prompt;
                check(parse_prompt_line(text, prompt), ja ? "msg_prompt sweep (ja): prompt recognized" : "msg_prompt sweep: prompt recognized");
                check(prompt.choices.size() == spelling.keys.size(), "msg_prompt sweep: one choice per key");
                for (const char key : spelling.keys) {
                    check(prompt_has_key(prompt, key), ja ? "msg_prompt sweep (ja): key listed" : "msg_prompt sweep: key listed");
                }
            }
        }
        //! 引退の確認は「いいえ」が先頭（3 択の確認の既定は先頭なので、素の決定でキャラが死なない）。
        PromptBar retire;
        check(parse_prompt_line("Do you really want to commit suicide? [y/n] [q = yes, and restart]", retire) && !retire.choices.empty()
                && retire.choices[0].key == 'n',
            "retire prompt starts with No");
        //! 末尾の綴りが完全でなければ問いにしない（行末の照合を緩めない）。
        PromptBar loose;
        check(!parse_prompt_line("You hear a [q = yes, and restart]", loose), "partial retire spelling is not a prompt");
        check(!parse_prompt_line("Do you really want to commit suicide? [y/n] [q = maybe]", loose), "other 3-way tail is not a prompt");
    }
    {
        for (const std::string &text : std::array<std::string, 5>{
                 utf8_of(u8"農夫マゴットの飼犬『くいつき』の攻撃: それは噛んだ。 *** 体力が危ない！ *** スペースを押して続ける。"),
                 utf8_of(u8"*** 乗騎から投げ落とされた！ *** Space を押すと続きます。"), utf8_of(u8"続けるにはスペースキーを押すこと。"),
                 utf8_of(u8"スペースキーで続行"),
                 //! 荒野の待ち伏せ（`wild.c` の `ROOM_AMBUSH`）とブーメラン（`py_throw.c`）。素の `inkey` でスペースだけを待つ（2026-10-10 の気づいたこと）。
                 utf8_of(u8"待ち伏せされた！ 続けるにはスペースを押してください。") }) {
            PromptBar prompt;
            check(parse_prompt_line(text, prompt), "ja space-only prompt recognized");
            check(prompt.choices.size() == 1 && prompt_has_key(prompt, ' '), "ja space-only prompt sends one Space");
        }
    }
    {
        //! 文の途中で折り返された、スペースだけを受ける問い（2026-10-09 に実機で見つけた。箱の幅 67 桁）。
        PromptBar prompt;
        const std::string upper_ja = utf8_of(u8"それの攻撃: それは噛んだ. *** 体力が危ない！ *** スペースを押して  ");
        const std::string lower_ja = utf8_of(u8"続ける。");
        check(!parse_prompt_line(lower_ja, prompt), "wrapped tail alone is not a prompt");
        check(parse_wrapped_space_prompt(upper_ja, lower_ja, prompt), "ja wrapped space-only prompt recognized");
        check(prompt.choices.size() == 1 && prompt_has_key(prompt, ' ') && prompt.text_utf8.rfind("***", 0) == 0,
              "ja wrapped space-only prompt sends one Space");
        PromptBar en;
        check(!parse_prompt_line("continue.", en), "wrapped english tail alone is not a prompt");
        check(parse_wrapped_space_prompt("The Novice warrior attacks you: He hits. *** LOW HITPOINT WARNING! *** Press Space to",
                  "continue.", en),
              "english wrapped space-only prompt recognized");
        check(en.choices.size() == 1 && prompt_has_key(en, ' '), "english wrapped space-only prompt sends one Space");
        PromptBar other;
        check(!parse_wrapped_space_prompt("You see a door.", "Open it? [y/n]", other), "wrapped helper ignores other prompts");
        check(!parse_wrapped_space_prompt("", "continue.", other), "wrapped helper ignores an empty upper row");
    }
    {
        //! 広域の地図。`広域の地図:` の `:` は次の欠片（` [` ＝桁 10）に上書きされて画面に出ない。
        GameFrame frame;
        frame.menu_term_lines.push_back(line(23, utf8_of(u8"広域の地図 [*/x/l] で見回す  [   方向     ] で移動  [>/</Esc] で近くの地図へ戻る")));
        fill_row0_prompt(frame);
        check(prompt_has_key(frame.prompt, '*'), "ja world-map Look action");
        check(frame.prompt.footer_utf8.rfind("LB/RB:", 0) == 0 || !frame.prompt.footer_utf8.empty(), "ja world-map native-motion prompt");
        check(!prompt_has_key(frame.prompt, '8') && !prompt_has_key(frame.prompt, '2'), "ja world-map directions stay native");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"(m)マクロ、(k)いまのキーセット、(K)両方のキーセット、(y)全部、(n)何もしない？")));
        check(collect_custom_key_controls(frame), "ja custom-key reset recognized");
        for (const int key : std::array<int, 8>{'m', 'k', 'K', 'y', 'n', '\r', '?', 27}) check(has_key(frame, key), "ja custom-key reset controls");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"一覧のキーを押すとそこへ飛びます。? でヘルプ、ESC で戻る。")));
        check(collect_custom_key_controls(frame), "ja custom-key browser recognized");
        frame.menu_choices.clear();
        frame.menu_term_lines.clear();
        frame.menu_term_lines.push_back(line(0, utf8_of(u8"置き換えた結果を試しに見ている。")));
        check(collect_custom_key_controls(frame), "ja custom-key preview recognized");
    }
    {
        GameFrame frame;
        frame.menu_term_lines.push_back(line(0, ""));
        frame.menu_term_lines.push_back(line(2, utf8_of(u8"    目的地を選んでください:")));
        frame.menu_term_lines.push_back(line(4, utf8_of(u8"[a] @ 雑貨屋  [b] # 我が家  [Esc] 戻る")));
        check(collect_journey_choices(frame), "ja journey selector recognized");
        for (const int key : std::array<int, 3>{'a', 'b', 27}) check(has_key(frame, key), "ja journey selector controls");
    }
    /*
     * 能力値の選択（`xtra2.c` の `_display_stat_choice()`。2026-10-07）。括弧の中の `8)` を札にしない・
     * 名札をいまの値の `17)` まで残す。
     */
    {
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line(1, "        Which stat do you want to raise?                       "));
        frame.menu_term_lines.push_back(line(2, "..............        a) Str (cur     17)            .........###"));
        frame.menu_term_lines.push_back(line(3, "~~~~~~~~~~~~~~        b) Int (cur     13)            ........##.."));
        frame.menu_term_lines.push_back(line(4, "~~~~~~~~~~~~~#        c) Wis (cur      8)            .......##:.."));
        frame.menu_term_lines.push_back(line(5, "##############        d) Dex (cur     16)            .#########.."));
        (void)fill_menu_choices(frame);
        check(!has_key(frame, '8'), "stat choice: no token inside parentheses");
        check(has_key(frame, 'a') && has_key(frame, 'b') && has_key(frame, 'c') && has_key(frame, 'd'), "stat choice: a-d");
        const auto a = std::find_if(frame.menu_choices.begin(), frame.menu_choices.end(),
            [](const MenuChoice &choice) { return choice.key == 'a'; });
        if (a != frame.menu_choices.end()) {
            const std::string label = frame.menu_term_lines[static_cast<std::size_t>(a->line_index)].text_utf8.substr(
                static_cast<std::size_t>(a->span_begin), static_cast<std::size_t>(a->span_len));
            check(label.find("17)") != std::string::npos, "stat choice: label keeps the current value");
        }
    }
    /*
     * -続く- の行は箱の最後の行とは限らない（2026-10-07）。箱の下の地図の行が入っても、
     * 末尾が -more- の行までを返す。
     */
    {
        check(more_marker_line_count({ "You have slain the monster.", "Welcome to level 10. -more-",
                  "..........................#.:#:..:#..:.::.:." }) == 2, "more marker above a map row");
        check(more_marker_line_count({ "Welcome to level 10. -more-" }) == 1, "more marker on the only row");
        check(more_marker_line_count({ "You feel something.", "..........#.:#:" }) == 0, "no more marker");
    }

    return failed;
}


/* ======================================== 品選びの窓の札の検査（#37。2026-08-28） */

int obj_prompt_choice_selftest()
{
    /*
     * 店で `s`（売る）を押した実物の画（`fc_protocol_driver.py` で撮った）。
     * 行 0〜4 が `obj_prompt` の窓、行 5 以降が下に残った店、行 24 が命令列である。
     * 日本語は **UTF-8 の 16 進**で書く（`command_label_selftest()` と同じ理由）。
     */
    static const char *const kLines[] = {
        "\xe6\x8c\x81\xe3\x81\xa1\xe7\x89\xa9",       /* 持ち物（窓の見出し） */
        "  a) | \xe5\x89\xa3                     13.0 lb",  /* a) 剣 */
        "  b) { \xe7\x9f\xa2                      5.0 lb",  /* b) 矢 */
        "                                     18.0 lb",        /* 合計（札は無い） */
        "\xe3\x81\xa9\xe3\x82\x8c\xe3\x82\x92\xe5\xa3\xb2\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x99\xe3\x81\x8b\xef\xbc\x9f", /* どれを売りますか？ */
        "    Item Description",
        "  a) ? \xe6\x9c\xac                       3.0 lbs    171", /* 店の a) 本 */
        "  b) ? \xe6\x9c\xac                       3.0 lbs   1710",
        "  c) ~ \xe7\x9f\xa2\xe7\xad\x92 [60]              1.0 lbs     39",
        "[b/p/g] \xe8\xb2\xb7\xe3\x81\x86  [s/d] \xe5\xa3\xb2\xe3\x82\x8b", /* 命令列 */
    };

    std::vector<std::vector<ChoiceTokenGroup>> per_line;
    for (const char *const line : kLines) {
        per_line.push_back(scan_choice_tokens(line));
    }

    int failed = 0;
    if (!keep_first_letter_run(per_line)) {
        std::fprintf(stderr, "[frox] selftest: obj-prompt choices: nothing kept\n");
        return 1;
    }

    //! 残ってよいのは**行 1 の a と行 2 の b だけ**。
    for (std::size_t i = 0; i < per_line.size(); ++i) {
        const std::size_t want = ((i == 1) || (i == 2)) ? 1u : 0u;
        if (per_line[i].size() != want) {
            std::fprintf(stderr,
                "[frox] selftest: obj-prompt choices: line %d kept %d (want %d): '%s'\n",
                (int)i, (int)per_line[i].size(), (int)want, kLines[i]);
            ++failed;
            continue;
        }
        if ((want == 1u) && (per_line[i][0].keys.at(0) != (int)('a' + i - 1))) {
            std::fprintf(stderr, "[frox] selftest: obj-prompt choices: line %d key '%c'\n",
                (int)i, (char)per_line[i][0].keys.at(0));
            ++failed;
        }
    }
    return failed;
}

/* ============================================== 命令列の札の検査（2026-08-28） */

int command_label_selftest()
{
    struct Case {
        const char *line;
        const char *bracket_keys; //!< 拾うべき札 3 のキーを並べたもの
        int plain; //!< 拾うべき札 1 / 札 2（品行）の数
    };
    /*
     * 綴りは実物から採った——英語は `shop.c:1714`、日本語は
     * `frox/lang/ja/ui/messages.ja.txt` の同じ鍵の訳文である。
     * 日本語は **UTF-8 の 16 進で書く**——このファイルは BOM 付き UTF-8 だが、
     * MSVC は狭い文字列を実行時の符号（CP932）へ落とすので、
     * そのまま書くと `text_utf8` と照らし合わなくなる。
     */
    static const Case kCases[] = {
        //! 店の帯の 1 行目（英語）。
        { "[b/p/g] Buy  [s/d] Sell  [x] Examine  [?] Help  [q/Esc] Exit", "bsx?q", 0 },
        //! 同じ行の訳文。`] 買う` を拾えなければパッドから店が使えない。
        { "[b/p/g] \xe8\xb2\xb7\xe3\x81\x86  [s/d] \xe5\xa3\xb2\xe3\x82\x8b  "
          "[x] \xe8\xaa\xbf\xe3\x81\xb9\xe3\x82\x8b  [?] \xe3\x83\x98\xe3\x83\xab\xe3\x83\x97  "
          "[q/Esc] \xe5\x87\xba\xe3\x82\x8b",
            "bsx?q", 0 },
        //! 3 行目の訳文。`Space` と `-` の組も日本語の札を従える。
        { "[Space/PgDn] \xe6\xac\xa1\xe3\x81\xae\xe9\xa0\x81  "
          "[-/PgUp] \xe5\x89\x8d\xe3\x81\xae\xe9\xa0\x81  "
          "[U] \xe4\xb8\x8d\xe8\xa6\x81\xe3\x81\xaa\xe5\x93\x81\xe3\x82\x92\xe9\x9a\xa0\xe3\x81\x99 (12)",
            " -U", 0 },
        //! 防具の `[2,+0]` は札ではない。拾うのは行頭の `a)` だけ。
        { "  a) \xe9\x89\x84\xe3\x81\xae\xe5\xb8\xbd\xe5\xad\x90 [2,+0]  120", "", 1 },
        //! 同じもので、角括弧の後ろが日本語だった場合（頭の検査を通る形）。
        { "  a) \xe9\x89\x84\xe3\x81\xae\xe5\xb8\xbd\xe5\xad\x90 [2,+0] \xe3\x81\xae\xe5\x86\x99\xe3\x81\x97",
            "", 1 },
        //! 本文の角括弧。
        { "[Press Any Key to Continue]", "", 0 },
    };

    int failed = 0;
    for (const Case &c : kCases) {
        const std::vector<ChoiceTokenGroup> groups = scan_choice_tokens(c.line);
        std::string keys;
        int plain = 0;
        for (const ChoiceTokenGroup &g : groups) {
            if (!g.bracket) {
                ++plain;
                continue;
            }
            for (const int key : g.keys) {
                keys.push_back(static_cast<char>(key));
            }
        }
        if (keys != c.bracket_keys) {
            std::fprintf(stderr, "[frox] selftest: command labels '%s' -> '%s' (want '%s')\n",
                c.line, keys.c_str(), c.bracket_keys);
            ++failed;
        }
        if (plain != c.plain) {
            std::fprintf(stderr, "[frox] selftest: command labels '%s' -> %d plain (want %d)\n",
                c.line, plain, c.plain);
            ++failed;
        }
    }
    return failed;
}

/* ================================================ 個数の綴りの検査（FH-15） */

int numeric_spelling_selftest()
{
    struct Case {
        const char *line;
        bool want;
        int min;
        int max;
        int value;
    };
    //! 綴りは実物から採った（`util.c:3246` と `message.c:626`）。
    static const Case kCases[] = {
        { "Quantity (1-5): 1", true, 1, 5, 1 },
        { "Quantity (1 to 5): 1", true, 1, 5, 1 },
        { "Quantity (1 to 21): ", true, 1, 21, 1 },
        { "Quantity (2-40): 12", true, 2, 40, 12 },
        { "Request: 123", true, 0, 999999999, 123 },
        { "Pay back: 456", true, 0, 999999999, 456 },
        { "Withdraw: 789", true, 0, 999999999, 789 },
        { "Additional deposit: 10", true, 0, 999999999, 10 },
        { "New deposit: 20", true, 0, 999999999, 20 },
        //! 札や本文を数値入力と読まないこと。
        { "  a) 6 Wooden Torches (with 1500 turns of light)", false, 0, 0, 0 },
        { "Really sell a Broad Sword for 150 gp? [y/n]", false, 0, 0, 0 },
        { "Buy which item (Esc when done)?", false, 0, 0, 0 },
    };

    int failed = 0;
    for (const Case &c : kCases) {
        NumericInput got;
        const bool ok = parse_numeric_input_line(c.line, got);
        if (ok != c.want) {
            std::fprintf(stderr, "[frox] selftest: numeric %s for '%s'\n",
                ok ? "matched but should not" : "did not match", c.line);
            ++failed;
            continue;
        }
        if (!c.want) {
            continue;
        }
        if ((got.min != c.min) || (got.max != c.max) || (got.value != c.value)) {
            std::fprintf(stderr,
                "[frox] selftest: numeric read %d..%d = %d (want %d..%d = %d) for '%s'\n",
                got.min, got.max, got.value, c.min, c.max, c.value, c.line);
            ++failed;
        }
    }
    return failed;
}

/* ============================================================ オプションの一覧（全件） */

namespace {

struct OptionEntry {
    std::string id;     //!< 共通の側が作る行の id（`option:名前`／名前の無い行は `option:label-ハッシュ`）
    std::string label;  //!< 原作の画面の説明（訳済み・空白をたたんだもの）
    int index{};        //!< 頁の中の番号（`fc_options_value()` の引数）
};

//! 共通の側（`modern_boolean_settings()`）が名前の無い行へ付ける id と同じ式。
std::string options_label_id(const std::string &label)
{
    unsigned long long hash = 1469598103934665603ULL;
    for (unsigned char c : presentation::modern_spaces(label)) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return "label-" + std::to_string(hash);
}

std::vector<OptionEntry> options_page_entries(int page)
{
    std::vector<OptionEntry> out;
    for (int i = 0;; ++i) {
        char ident[64];
        char desc[512];
        if (!fc_options_row(page, i, ident, sizeof(ident), desc, sizeof(desc))) {
            break;
        }
        OptionEntry entry;
        entry.index = i;
        entry.label = presentation::modern_spaces(fc::sjis_to_utf8(std::string(desc)));
        entry.id = std::string("option:") + (ident[0] ? std::string(ident) : options_label_id(entry.label));
        out.push_back(std::move(entry));
    }
    return out;
}

//! `visible`（見えている行の id の並び）が `full` の連続した一部と一致する位置。無ければ -1。
int options_find_run(const std::vector<OptionEntry> &full, const std::vector<std::string> &visible)
{
    if (visible.empty() || (visible.size() > full.size())) {
        return -1;
    }
    for (std::size_t start = 0; start + visible.size() <= full.size(); ++start) {
        bool same = true;
        for (std::size_t j = 0; j < visible.size(); ++j) {
            if (full[start + j].id != visible[j]) {
                same = false;
                break;
            }
        }
        if (same) {
            return static_cast<int>(start);
        }
    }
    return -1;
}

//! 値の字を、共通の側が行の値に出す形（オン・オフ）へ。
std::string options_value_text(const std::string &raw, bool ja)
{
    const std::string value = presentation::modern_trim(raw);
    if ((value == "yes") || (value == "on") || (value == reinterpret_cast<const char *>(u8"はい"))) {
        return ja ? reinterpret_cast<const char *>(u8"オン") : "On";
    }
    if ((value == "no") || (value == "off") || (value == reinterpret_cast<const char *>(u8"いいえ"))) {
        return ja ? reinterpret_cast<const char *>(u8"オフ") : "Off";
    }
    return value;
}

} // namespace

void OptionsList::apply(GameFrame &frame)
{
    ids_.clear();
    cursor_.clear();
    ModernWindow &w = frame.modern;
    const bool editable = (w.content_type == "settings_list") && (w.stage == "cursor");
    const bool readonly = (w.content_type == "settings_readonly");
    if ((!editable && !readonly) || w.rows.empty()) {
        return;
    }
    std::vector<std::string> visible;
    for (const auto &row : w.rows) {
        if (row.id.rfind("@action:", 0) != 0) {
            visible.push_back(row.id);
        }
    }
    // 頁を決める: 見えている行が連続した一部になっている頁。Frox の頁は別の頁の項目を写して並べるので、
    // 候補が 2 つ以上のときは 0 行目の見出しで決める。
    char row0[512] = { 0 };
    (void)fc_term_row(0, row0, nullptr, static_cast<int>(sizeof(row0)));
    int found_page = 0;
    std::vector<OptionEntry> full;
    int candidates = 0;
    bool frox_candidate = false;
    bool any_known = false; //!< 見えている行のどれかが、原作の表のどこかの頁にある
    for (int page = FC_OPTION_PAGE_FIRST; page <= FC_OPTION_PAGE_FROX; ++page) {
        auto entries = options_page_entries(page);
        if (!any_known) {
            any_known = std::any_of(visible.begin(), visible.end(), [&](const std::string &id) {
                return std::any_of(entries.begin(), entries.end(), [&](const OptionEntry &e) { return e.id == id; });
            });
        }
        if (options_find_run(entries, visible) < 0) {
            continue;
        }
        ++candidates;
        if (page == FC_OPTION_PAGE_FROX) {
            frox_candidate = true;
            if (fc_options_title_is_frox_page(row0) != 0) {
                found_page = page;
                full = std::move(entries);
                candidates = 1;
                break;
            }
            continue;
        }
        if (!found_page) {
            found_page = page;
            full = std::move(entries);
        }
    }
    if (!found_page || (candidates - (frox_candidate && (found_page != FC_OPTION_PAGE_FROX) ? 1 : 0) > 1)) {
        // 頁を決められなければ、共通の側の窓のまま（見えている行だけ）。原作の表にある項目が見えているのに頁が決まらないのは
        // 表と画面の食い違い（原作を取り込み直したときなど）なので、1 度だけ知らせる。表に無い一覧（自動セーブ・モガミネータなど）は黙って通す。
        static bool warned = false;
        if (!warned && any_known) {
            warned = true;
            std::fprintf(stderr, "[frox] options list: the visible rows (%zu) match no page of option_info (%d candidates); left as drawn\n",
                visible.size(), candidates);
        }
        return;
    }

    const bool ja = (w.language == "ja");
    if (editable) {
        std::vector<ModernRow> rows;
        const ModernRow *done = nullptr;
        for (const auto &row : w.rows) {
            if (row.id.rfind("@action:", 0) == 0) {
                done = &row;
            }
        }
        for (const auto &entry : full) {
            const auto at = std::find_if(w.rows.begin(), w.rows.end(), [&](const ModernRow &r) { return r.id == entry.id; });
            if (at != w.rows.end()) {
                rows.push_back(*at);
            } else {
                ModernRow row;
                row.id = entry.id;
                row.label = entry.label;
                row.slot = entry.index; // 画面の行ではない（原作が今は描いていない）。通信の決まりで負の値は送れないので頁の中の番号
                row.selection = 0;
                row.price_known = false;
                rows.push_back(std::move(row));
            }
        }
        if (done) {
            rows.push_back(*done);
        }
        w.rows = std::move(rows);
    } else {
        // 閲覧のみ: 行の選択が無い窓。本文の欄を全件にする（見えている行の値・選択は共通の側のまま使う）。
        std::vector<ModernBlock> blocks;
        std::vector<ModernRow> rows;
        for (const auto &entry : full) {
            const auto at = std::find_if(w.rows.begin(), w.rows.end(), [&](const ModernRow &r) { return r.id == entry.id; });
            ModernRow row;
            if (at != w.rows.end()) {
                row = *at;
            } else {
                row.id = entry.id;
                row.label = entry.label;
                row.slot = entry.index;
                row.selection = 0;
                row.price_known = false;
                row.available = false;
            }
            ModernCell cell{ row.label, {}, false };
            cell.target = row.id;
            bool have = false;
            for (const auto &block : w.content) {
                for (const auto &old : block.cells) {
                    if (old.target == row.id) {
                        cell.text = old.text;
                        cell.selected = old.selected;
                        have = true;
                    }
                }
            }
            if (!have) {
                char value[256];
                if (fc_options_value(found_page, entry.index, value, sizeof(value))) {
                    cell.text = options_value_text(fc::sjis_to_utf8(std::string(value)), ja);
                }
            }
            ModernBlock block;
            block.kind = "fields";
            block.cells.push_back(std::move(cell));
            blocks.push_back(std::move(block));
            rows.push_back(std::move(row));
        }
        // 全件を並べたので、「下へスクロール」の札は要らない。説明の段落（自動破壊の頁の黄色の 1 行など）は残す。
        for (auto &block : w.content) {
            if (block.kind != "paragraph") {
                continue;
            }
            const bool marker = std::any_of(block.cells.begin(), block.cells.end(), [](const ModernCell &c) {
                return (c.text.find("scroll") != std::string::npos) || (c.text.find(reinterpret_cast<const char *>(u8"スクロール")) != std::string::npos);
            });
            if (!marker) {
                blocks.push_back(std::move(block));
            }
        }
        w.content = std::move(blocks);
        w.rows = std::move(rows);
    }
    for (const auto &entry : full) {
        ids_.push_back(entry.id);
    }
    cursor_ = w.cursor_target;
}

void OptionsList::rewrite_focus(presentation::InputEventWire &event) const
{
    if ((event.e != "ui_action") || (event.action != "focus") || ids_.empty()) {
        return;
    }
    const auto to = std::find(ids_.begin(), ids_.end(), event.target);
    const auto from = std::find(ids_.begin(), ids_.end(), cursor_);
    if ((to == ids_.end()) || (from == ids_.end()) || (to == from)) {
        return;
    }
    const long n = static_cast<long>(ids_.size());
    const long forward = ((to - from) % n + n) % n;
    const long backward = n - forward;
    event.action = "native_key";
    event.number = (forward <= backward) ? '2' : '8'; // 原作は端で回り込む（`k = (k + 1) % n`）ので、近いほうへ
}

OptionsList &options_list()
{
    static OptionsList list;
    return list;
}

int options_list_selftest()
{
    int failed = 0;
    const auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            ++failed;
            std::fprintf(stderr, "[frox] selftest: options list: FAIL - %s\n", what);
        }
    };
    // 1. どの頁も空でなく、頁の中で id が重ならない（重なると画面の行が取り違えられる）。
    for (int page = FC_OPTION_PAGE_FIRST; page <= FC_OPTION_PAGE_FROX; ++page) {
        const auto entries = options_page_entries(page);
        expect(!entries.empty(), "a page has no options");
        for (std::size_t i = 0; i < entries.size(); ++i) {
            for (std::size_t j = i + 1; j < entries.size(); ++j) {
                if (entries[i].id == entries[j].id) {
                    expect(false, "duplicated option id in a page");
                }
            }
            expect(!entries[i].label.empty(), "an option has no description");
            char value[256];
            expect(fc_options_value(page, entries[i].index, value, sizeof(value)) != 0, "an option has no value text");
        }
    }
    // 2. 誕生時オプションは、窓が小さくて途中までしか描けなくても全件に差し替わる。
    const auto birth = options_page_entries(FC_OPTION_PAGE_BIRTH);
    expect(birth.size() >= 25, "birth options: fewer than 25");
    expect(std::any_of(birth.begin(), birth.end(), [](const OptionEntry &e) { return e.id == "option:no_selling"; }), "birth options: no_selling is missing");
    if (birth.size() >= 12) {
        GameFrame frame;
        frame.modern.content_type = "settings_list";
        frame.modern.stage = "cursor";
        frame.modern.language = "en";
        for (std::size_t i = 0; i < 8; ++i) {          // 先頭の 8 件だけが見えている窓
            ModernRow row;
            row.id = birth[i].id;
            row.label = birth[i].label;
            frame.modern.rows.push_back(std::move(row));
        }
        ModernRow done;
        done.id = "@action:accept-birth-options";
        frame.modern.rows.push_back(done);
        frame.modern.cursor_target = birth[3].id;
        OptionsList list;
        list.apply(frame);
        expect(frame.modern.rows.size() == birth.size() + 1, "apply: the rows are not the full list");
        expect(!frame.modern.rows.empty() && frame.modern.rows.back().id == "@action:accept-birth-options", "apply: the confirm row is not last");
        // 3. focus を 1 キーへ（端は回り込むので近いほうへ）。カーソルは birth[3]。
        const auto key_for = [&](const std::string &target) {
            presentation::InputEventWire event;
            event.e = "ui_action";
            event.action = "focus";
            event.target = target;
            list.rewrite_focus(event);
            return (event.action == "native_key") ? static_cast<int>(event.number) : 0;
        };
        expect(key_for(birth[4].id) == '2', "focus: next row is not '2'");
        expect(key_for(birth[2].id) == '8', "focus: previous row is not '8'");
        expect(key_for(birth[3].id) == 0, "focus: the row under the cursor must not move the cursor");
        expect(key_for(birth.back().id) == '8', "focus: from the 4th row to the last row is not nearer upward (wrap)");
        expect(key_for("option:does_not_exist") == 0, "focus: an unknown row must be left alone");
    }
    return failed;
}

/* ============================================================ 選択メニュー（menu_choose）の全件 */

namespace {

//! 半角の空白とタブを除いた字。項目の字と Term の行の字の照合に使う。
std::string menu_compact(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if ((c != ' ') && (c != '\t')) {
            out.push_back(c);
        }
    }
    return out;
}

//! `got`（Term の行から切り出した字）が `キー) 字` の頭と一致するか。右端で切られた字は短いほうまで比べる。
bool menu_text_matches(const std::string &got, const MenuChooseItem &item)
{
    const std::string expected = std::string(1, static_cast<char>(item.key)) + ")" + menu_compact(item.text);
    const std::string seen = menu_compact(got);
    const std::size_t n = std::min(seen.size(), expected.size());
    return (n >= 2) && (seen.compare(0, n, expected, 0, n) == 0);
}

/*!
 * Term の行から `キー) 字` を探して選択肢の位置を埋める（共通の側が読まなかった画のため）。
 * 行の頭か空白の直後にある `キー) ` で始まり、字が項目の字（右端で切られていれば頭）と合うものだけ。
 */
bool menu_find_in_lines(const std::vector<TermMirrorLine> &lines, const MenuChooseItem &item, MenuChoice &out)
{
    const std::string needle = std::string(1, static_cast<char>(item.key)) + ") ";
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string &line = lines[i].text_utf8;
        for (std::size_t pos = line.find(needle); pos != std::string::npos; pos = line.find(needle, pos + 1)) {
            if ((pos != 0) && (line[pos - 1] != ' ')) {
                continue;
            }
            const std::size_t at = pos + needle.size();
            std::size_t len = 0;
            if (line.compare(at, item.text.size(), item.text) == 0) {
                len = item.text.size();
            } else {
                std::size_t end = line.find("  ", at);
                if (end == std::string::npos) {
                    end = line.size();
                }
                while ((end > at) && (line[end - 1] == ' ')) {
                    --end;
                }
                const std::string part = line.substr(at, end - at);
                if (part.empty() || (item.text.compare(0, part.size(), part) != 0)) {
                    continue;
                }
                len = part.size();
            }
            out.line_index = static_cast<int>(i);
            out.span_begin = static_cast<int>(pos);
            out.span_len = static_cast<int>(needle.size() + len);
            out.key_begin = static_cast<int>(pos);
            out.key_len = 1;
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<MenuChooseItem> menu_choose_items()
{
    std::vector<MenuChooseItem> items;
    const int count = fc_menu_choose_count();
    for (int i = 0; i < count; ++i) {
        int key = 0;
        int attr = 0;
        char text[512];
        char help[3072];
        if (!fc_menu_choose_item(i, &key, &attr, text, static_cast<int>(sizeof(text)), help, static_cast<int>(sizeof(help)))) {
            break;
        }
        if (key == 0) {
            continue; // 原作は色が TERM_DARK の項目のキーを '\0' にして行を描かない。選べない
        }
        MenuChooseItem item;
        item.key = key;
        item.text = presentation::modern_trim(fc::sjis_to_utf8(std::string(text)));
        item.help = fc::sjis_to_utf8(std::string(help));
        items.push_back(std::move(item));
    }
    return items;
}

bool menu_choose_apply(GameFrame &frame, const std::vector<MenuChooseItem> &items)
{
    if (items.empty() || !frame.menu_open || frame.menu_term_lines.empty()) {
        return false;
    }
    const std::vector<TermMirrorLine> &lines = frame.menu_term_lines;
    const std::vector<MenuChoice> parsed = frame.menu_choices;
    std::vector<bool> taken(parsed.size(), false);
    std::vector<MenuChoice> out;
    out.reserve(items.size() + 2);
    int seen = 0;
    for (const MenuChooseItem &item : items) {
        MenuChoice choice;
        bool found = false;
        // 1. 共通の側が Term の写しから読んだ選択肢のうち、同じキー・同じ字のもの（行の位置をそのまま使う）
        for (std::size_t i = 0; (i < parsed.size()) && !found; ++i) {
            const MenuChoice &p = parsed[i];
            if (taken[i] || (p.line_index < 0) || (p.line_index >= static_cast<int>(lines.size())) || (p.key != item.key)) {
                continue;
            }
            const std::string &line = lines[static_cast<std::size_t>(p.line_index)].text_utf8;
            const std::size_t begin = static_cast<std::size_t>(std::max(0, p.span_begin));
            if (begin > line.size()) {
                continue;
            }
            const std::size_t len = (p.span_len > 0) ? static_cast<std::size_t>(p.span_len) : std::string::npos;
            if (!menu_text_matches(line.substr(begin, len), item)) {
                continue;
            }
            choice = p;
            taken[i] = true;
            found = true;
        }
        // 2. 読まれていなければ、Term の行から `キー) 字` を探す
        if (!found) {
            found = menu_find_in_lines(lines, item, choice);
        }
        if (found) {
            ++seen;
        } else {
            choice = MenuChoice{};
            choice.line_index = -1; // Term の外へはみ出した項目。行の無い選択肢
        }
        choice.key = item.key;
        choice.label_utf8 = item.text;
        choice.description_utf8 = item.help;
        choice.available = true;
        choice.reason_utf8.clear();
        out.push_back(std::move(choice));
    }
    if (seen == 0) {
        return false; // メニューの画ではない（人物調書の画面など）。何も変えない
    }
    // 行に結び付かない補助の選択肢（`C` 人物調書・`?` 閲覧）と、項目でないキーの選択肢は項目の後ろに残す
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        if (taken[i]) {
            continue;
        }
        // 行に結び付かない選択肢は残す。行のある選択肢は、項目のキーと同じなら項目の読み違いなので捨てる
        const bool item_key = std::any_of(items.begin(), items.end(), [&](const MenuChooseItem &m) { return m.key == parsed[i].key; });
        if ((parsed[i].line_index < 0) || !item_key) {
            out.push_back(parsed[i]);
        }
    }
    frame.menu_choices = std::move(out);
    return true;
}

void menu_choose_apply(GameFrame &frame)
{
    if (fc_menu_choose_count() <= 0) {
        return;
    }
    (void)menu_choose_apply(frame, menu_choose_items());
}

int menu_choose_selftest()
{
    int failed = fc_menu_choose_selftest(); // 全件の取り方（キーの振り方・TERM_DARK・説明）
    const auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            ++failed;
            std::fprintf(stderr, "[frox] selftest: menu_choose list: FAIL - %s\n", what);
        }
    };
    //! 項目のキー。原作の `get_com` より先に `C`（人物調書）が効くので、`C` は項目のキーに使わない
    static const char kKeys[] = "abcdefghijklmnopqrstuvwxyz0123456789ABDEFGHIJKLMNOPQRSTUVWXYZ";
    const auto make_items = [&](int n) {
        std::vector<MenuChooseItem> items;
        for (int i = 0; i < n; ++i) {
            MenuChooseItem item;
            item.key = kKeys[i];
            item.text = "Item" + std::to_string(i);
            item.help = "Help" + std::to_string(i);
            items.push_back(std::move(item));
        }
        return items;
    };
    const auto line_of = [&](const std::string &text) {
        TermMirrorLine line;
        line.text_utf8 = text;
        return line;
    };
    const auto parsed_choice = [&](int line_index, int key, int begin, const std::string &text) {
        MenuChoice c;
        c.line_index = line_index;
        c.key = key;
        c.key_begin = begin;
        c.key_len = 1;
        c.span_begin = begin;
        c.span_len = static_cast<int>(text.size());
        return c;
    };
    const auto aux_choice = [&](int key) {
        MenuChoice c;
        c.line_index = -1;
        c.key = key;
        c.label_utf8 = "aux";
        return c;
    };

    // 1. 1 列で、Term の高さ（24 行）を超える 60 項目。見えているのは先頭の 23 項目だけ（行 1〜23）。
    {
        const auto items = make_items(60);
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line_of("Choose which one? (Type '?' to browse, 'C' for character sheet)"));
        for (int i = 0; i < 23; ++i) {
            const std::string text = std::string("  ") + static_cast<char>(items[i].key) + ") " + items[i].text;
            frame.menu_term_lines.push_back(line_of(text));
            frame.menu_choices.push_back(parsed_choice(i + 1, items[i].key, 2, text.substr(2)));
        }
        frame.menu_choices.push_back(aux_choice('C'));
        frame.menu_choices.push_back(aux_choice('?'));
        const std::size_t lines_before = frame.menu_term_lines.size();
        expect(menu_choose_apply(frame, items), "overflow: apply returned false");
        expect(frame.menu_choices.size() == 62, "overflow: 60 items + 2 aux are expected");
        bool ordered = frame.menu_choices.size() >= 60;
        for (int i = 0; ordered && (i < 60); ++i) {
            ordered = (frame.menu_choices[i].key == items[i].key) && (frame.menu_choices[i].label_utf8 == items[i].text)
                && (frame.menu_choices[i].description_utf8 == items[i].help);
        }
        expect(ordered, "overflow: the items are not in the original order with their text and help");
        expect(frame.menu_choices.size() >= 60 && frame.menu_choices[22].line_index == 23, "overflow: a visible item must keep its line");
        expect(frame.menu_choices.size() >= 60 && frame.menu_choices[23].line_index < 0 && frame.menu_choices[59].line_index < 0,
            "overflow: an item outside the Term must be a choice without a line");
        expect(frame.menu_choices.size() == 62 && frame.menu_choices[60].key == 'C' && frame.menu_choices[61].key == '?',
            "overflow: the aux choices are not kept after the items");
        expect(frame.menu_term_lines.size() == lines_before, "overflow: the Term mirror must not change");
    }
    // 1b. 同じ画を、本物の読み取り（`fill_menu_choices()`）に通す。読み取りは Term に描かれた項目しか見えない。
    {
        const auto items = make_items(60);
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line_of("Choose which one?"));
        for (int i = 0; i < 23; ++i) {
            frame.menu_term_lines.push_back(line_of(std::string("  ") + static_cast<char>(items[i].key) + ") " + items[i].text));
        }
        (void)fill_menu_choices(frame);
        int read_items = 0;
        for (const MenuChoice &c : frame.menu_choices) {
            if (c.line_index > 0) {
                ++read_items;
            }
        }
        expect(read_items == 23, "real parser: only the 23 drawn items can be read from the Term");
        expect(menu_choose_apply(frame, items), "real parser: apply returned false");
        int listed = 0;
        bool ordered = true;
        for (const MenuChoice &c : frame.menu_choices) {
            if (listed < 60) {
                ordered = ordered && (c.key == items[listed].key) && (c.label_utf8 == items[listed].text);
            }
            ++listed;
        }
        expect(listed >= 60 && ordered, "real parser: the 60 items are not listed in the original order");
        expect(frame.menu_choices.size() >= 60 && frame.menu_choices[22].line_index == 23 && frame.menu_choices[23].line_index < 0,
            "real parser: the 24th item (outside the Term) must be a choice without a line");
    }
    // 2. 2 列のメニュー（原作 `mut_gain_choice`）。共通の側は行ごとに読むので、並びが交互に混ざっている。
    {
        const auto items = make_items(27);
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line_of("Gain which mutation?"));
        for (int r = 0; r < 18; ++r) {
            std::string text = std::string("               ") + static_cast<char>(items[r].key) + ") " + items[r].text;
            const int second = r + 18;
            if (second < 27) {
                text.resize(46, ' ');
                text += std::string(1, static_cast<char>(items[second].key)) + ") " + items[second].text;
            }
            frame.menu_term_lines.push_back(line_of(text));
            frame.menu_choices.push_back(parsed_choice(r + 1, items[r].key, 15, std::string(1, static_cast<char>(items[r].key)) + ") " + items[r].text));
            if (second < 27) {
                frame.menu_choices.push_back(parsed_choice(r + 1, items[second].key, 46, std::string(1, static_cast<char>(items[second].key)) + ") " + items[second].text));
            }
        }
        expect(menu_choose_apply(frame, items), "two columns: apply returned false");
        bool ordered = frame.menu_choices.size() == 27;
        for (int i = 0; ordered && (i < 27); ++i) {
            ordered = (frame.menu_choices[i].key == items[i].key) && (frame.menu_choices[i].line_index >= 1);
        }
        expect(ordered, "two columns: the items are not in the original order (the columns are mixed)");
        expect(frame.menu_choices.size() == 27 && frame.menu_choices[18].span_begin == 46 && frame.menu_choices[18].line_index == 1,
            "two columns: an item of the right column must keep its own span");
    }
    // 3. 共通の側が何も読まなかった画（選択肢が空）でも、`キー) 字` の行を探して位置を埋める。
    {
        const auto items = make_items(5);
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line_of("Choose?"));
        for (int i = 0; i < 5; ++i) {
            frame.menu_term_lines.push_back(line_of(std::string("  ") + static_cast<char>(items[i].key) + ") " + items[i].text));
        }
        expect(menu_choose_apply(frame, items), "search: apply returned false");
        expect(frame.menu_choices.size() == 5 && frame.menu_choices[3].line_index == 4 && frame.menu_choices[3].span_begin == 2,
            "search: the line of an item is not found");
    }
    // 4. 右端で切られた字（行の字が項目の字の頭だけ）も、同じ項目と見る。
    {
        auto items = make_items(2);
        items[1].text = "VeryLongItemName";
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line_of("Choose?"));
        frame.menu_term_lines.push_back(line_of("  a) Item0"));
        frame.menu_term_lines.push_back(line_of("  b) VeryLong"));
        expect(menu_choose_apply(frame, items), "clipped: apply returned false");
        expect(frame.menu_choices.size() == 2 && frame.menu_choices[1].line_index == 2, "clipped: a clipped text must match its item");
        expect(frame.menu_choices.size() == 2 && frame.menu_choices[1].label_utf8 == "VeryLongItemName", "clipped: the label must be the whole text");
    }
    // 5. メニューの画でない（人物調書など）ときは何も変えない。メニューが開いていないときも。
    {
        const auto items = make_items(3);
        GameFrame frame;
        frame.menu_open = true;
        frame.menu_term_lines.push_back(line_of("Name : PLAYER"));
        frame.menu_term_lines.push_back(line_of("Race : Human"));
        frame.menu_choices.push_back(aux_choice('C'));
        expect(!menu_choose_apply(frame, items), "other screen: apply must return false");
        expect(frame.menu_choices.size() == 1 && frame.menu_choices[0].key == 'C', "other screen: the choices must not change");
        GameFrame closed;
        closed.menu_term_lines.push_back(line_of("  a) Item0"));
        expect(!menu_choose_apply(closed, items), "closed: apply must return false");
        expect(!menu_choose_apply(frame, {}), "no items: apply must return false");
    }
    return failed;
}

static bool is_store_purchase_prompt(const std::string &text)
{
    return (text.find("Really buy ") != std::string::npos) || (text.find(reinterpret_cast<const char *>(u8"買いますか")) != std::string::npos);
}

void store_purchase_default_yes(GameFrame &frame)
{
    if (frame.modern.stage != "confirm" || frame.prompt.choices.size() != 2 || !is_store_purchase_prompt(frame.prompt.text_utf8)) {
        return;
    }
    for (size_t i = 0; i < frame.prompt.choices.size(); ++i) {
        const int key = frame.prompt.choices[i].key;
        if (key == 'y' || key == 'Y') {
            frame.modern.default_answer = static_cast<int>(i);
            return;
        }
    }
}

int store_purchase_default_selftest()
{
    int failed = 0;
    const auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            ++failed;
            std::fprintf(stderr, "[frox] selftest: store purchase default: FAIL - %s\n", what);
        }
    };
    const auto make = [&](const char *text) {
        GameFrame frame;
        frame.modern.stage = "confirm";
        frame.modern.default_answer = 1; // 共通の側の既定（[y/n] は「いいえ」）
        frame.prompt.text_utf8 = text;
        PromptChoice yes;
        yes.key = 'y';
        PromptChoice no;
        no.key = 'n';
        frame.prompt.choices.push_back(yes);
        frame.prompt.choices.push_back(no);
        return frame;
    };
    {
        GameFrame frame = make(reinterpret_cast<const char *>(u8"本当に食料×10を 50 ゴールドで買いますか？ [y/n]"));
        store_purchase_default_yes(frame);
        expect(frame.modern.default_answer == 0, "purchase (ja): default must be yes");
    }
    {
        GameFrame frame = make("Really buy Rations for 50 gp? [y/n]");
        store_purchase_default_yes(frame);
        expect(frame.modern.default_answer == 0, "purchase (en): default must be yes");
    }
    {
        GameFrame frame = make(reinterpret_cast<const char *>(u8"本当に食料×10を 50 ゴールドで売りますか？ [y/n]"));
        store_purchase_default_yes(frame);
        expect(frame.modern.default_answer == 1, "sell: default must stay no");
    }
    {
        GameFrame frame = make(reinterpret_cast<const char *>(u8"本当に食料を 50 ゴールドで買いますか？ [y/n]"));
        frame.modern.stage = "browse";
        store_purchase_default_yes(frame);
        expect(frame.modern.default_answer == 1, "not a confirm stage: unchanged");
    }
    return failed;
}

/*!
 * @brief 店の窓のキー入力（native_key）が、同義キーも受けるかの検査。
 */
int store_native_key_selftest()
{
    int failed = 0;
    const auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            ++failed;
            std::fprintf(stderr, "[frox] selftest: store native key: FAIL - %s\n", what);
        }
    };
    //! 雑貨屋の閲覧の画を作り、`key` を native_key として送る。コアへ渡るキー（無ければ 0）を返す。
    const auto press = [&](const char *profile, int key) {
        presentation::NativeStoreMenu menu;
        menu.set_enabled(true, true, profile);
        GameFrame frame;
        frame.menu_open = true;
        const char *const kFooter = "[b/p/g] Buy  [s/d] Sell  [x] Examine  [?] Help  [q/Esc] Exit";
        for (int row = 0; row < 3; ++row) {
            TermMirrorLine line;
            line.source_row = row;
            line.text_utf8 = (row == 2) ? kFooter : "";
            frame.menu_term_lines.push_back(line);
        }
        //! アダプタが申告する選択肢は各札の先頭の 1 キーだけ（`parse_bracket_choice_token`）。
        for (const char *keys : { "bsxq" }) {
            for (const char *k = keys; *k; ++k) {
                MenuChoice choice{};
                choice.line_index = 2;
                choice.key = static_cast<unsigned char>(*k);
                choice.key_len = 1;
                frame.menu_choices.push_back(choice);
            }
        }
        native_store_info info{};
        info.active = 1;
        info.kind = 1;
        info.root_frame = 1; //!< 店の一覧の画（閲覧の段）
        info.get_key = 'p';
        info.put_key = 's';
        info.previous_key = '-';
        info.next_key = ' ';
        menu.capture(frame, info, {});
        presentation::InputEventWire event;
        event.e = "ui_action";
        event.action = "native_key";
        event.number = key;
        event.request_id = 1;
        event.ui_revision = frame.modern.revision;
        std::vector<int> keys;
        menu.input(event, keys);
        return keys.empty() ? 0 : keys.front();
    };
    expect(press("frox", 'b') == 'b', "frox: b (declared) is sent");
    expect(press("frox", 'p') == 'p', "frox: p (purchase) is sent");
    expect(press("frox", 'g') == 'g', "frox: g (purchase synonym) is sent");
    expect(press("frox", 'd') == 'd', "frox: d (sell synonym) is sent");
    expect(press("frox", 's') == 's', "frox: s (sell) is sent");
    expect(press("frox", 'z') == 0, "frox: an undeclared key is still refused");
    expect(press("generic", 'g') == 0, "other cores: synonym keys are not widened");
    return failed;
}

} // namespace fc
