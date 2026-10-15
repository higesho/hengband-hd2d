/*!
 * @file fc_menu.h
 * @brief コアのカーソルとプロンプトの読み取り（M0.5 FH-01。設計 §14.2(b)）。
 *
 * 手本は `gensoband/adapter/gb_menu.{h,cpp}`。**やっていることは文字列の照合だけ**
 * なので判定の理屈はそのまま写せるが、当てる文言は Frox を実読して英語で数え直した
 * （§14.1 の事実 4・5）。M0 の Frox は純 ASCII なので文字コード変換が無く、
 * バイト位置＝桁位置である（M1 で CP932 が入るときは `fc_frame.cpp` と同じ場所へ
 * 変換を通す）。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

namespace fc {
std::vector<ModernAction> birth_menu_actions(const GameFrame &frame);

/*!
 * @brief コアが自前で描いているカーソル `"> "` を Term ミラーから探す。
 * @return 見つかったら true（`frame.menu_core_cursor` / `menu_core_cursors` を埋める）
 * @details 印を書くのはコマンドメニュー（`util.c` の `inkey_from_menu()`）で、
 * 枠 `+----` と組で出る。**枠のある画面に限って**探す——`>` は階段の字でもあり、
 * ミラーには前の画（ASCII の地図）が残っているから、印だけで引くと階段を拾う。
 * `use_menu` の呪文選び（`cmd5.c`）も `"> "` を書くが枠が無いので拾わない
 * ——あちらはコアのカーソルが素の字でミラーに動いて見えるので、遊べはする。
 */
bool fill_core_cursor(GameFrame &frame);

/*!
 * @brief メッセージ行を読んで `frame.prompt` / `frame.numeric` を埋める。
 * @details `[y/n]` と数値入力は `screen_save()` を通らない＝ icky にならないので、
 * Term ミラーには 1 行も出てこない。行を直に読まないと、パッドでは「はい」と
 * 答える手段も個数を入れる手段も無い。
 * **行 0 だけではない**——`get_check()` は `msg_prompt()` 経由で、メッセージの箱
 * （`ui_msg_rect()`。最大 10 行）に出る。長い品名で折り返すと `[y/n]` の尻尾は
 * 行 1〜2 へ落ちるので、行 0〜2 を見る（§14.1 の事実 4）。
 */
void fill_row0_prompt(GameFrame &frame);

/*!
 * @brief 移動の目標のマスを選ぶ問い（`tgt_pt()`）のあいだ、カーソルのマスを 3D の地図へ出す（修-32）。
 * @details `fill_row0_prompt()` で `frame.prompt` が `Travel: ` の帯になっているときだけ、Term のカーソルのマスを
 * 入力を受けない狙いの重ね（`frame.targeting.preview_only` の十字線）として積む。
 * **`apply_native_targeting_menu_frame()` の後に呼ぶ**——あれは重ねだけの `targeting` を毎回空にする。
 */
void fill_travel_reticle(GameFrame &frame);

/*! @brief 文がクエストの説明（`名前 (Level N): 説明文`。日本語は `（レベル N）`）か（修-33）。 */
bool is_quest_message_text(const std::string &text);

/*!
 * @brief 建物の画面で、クエストの説明文がメッセージの箱に出しっぱなしか（修-33）。
 * @details 原作は説明文を出したあとキーを待たずに建物の命令待ちへ戻るので、文は次の命令まで残る。
 */
bool building_quest_message_shown();

/*!
 * @brief 説明文が出ているあいだの決定が、説明文を出した命令の繰り返しか（修-33）。真なら命令は送らず、説明文を閉じるだけにする。
 * @param key 決定が送ろうとしているキー。@param last_key 原作へ最後に渡したキー。
 */
bool repeats_command_under_message(int key, int last_key, bool message_shown);

/*!
 * @brief ミラーの全行から選択肢の札を集めて `frame.menu_choices` へ積む。
 * @return 1 件でも積んだら true
 * @details 札は 3 書式（§14.1 の事実 5）: `a)`（品・呪文・誕生）／`(1)`（オプション
 * の根）／`[b/p/g] Buy`（店の下段ほか doc の命令列。`/` 区切りで複数キー）。
 *
 * **判定は幻想蛮怒より緩い（全行走査）**——あちらは画面ごとに名指しで絞ったが、
 * Frox の doc UI は知らない字を黙って無視するので害が小さい（設計 §14.2(b) の判断。
 * 誤爆が実測されたら名指しの表に絞る）。
 * @note `fill_core_cursor()` が真を返した画面では呼ばない（カーソルが二重になる）。
 * ゲーム前画面の選択肢（`g_pregame_choices`）が既に入っているときも呼ばない。
 */
bool fill_menu_choices(GameFrame &frame);

/*!
 * @brief 個数入力の綴りの検査（`--selftest` から呼ぶ。FH-15）。
 * @return 落ちた件数（0 なら通った）。落ちた中身は stderr へ書く
 *
 * @details **区切りは 2 綴りある**——`get_quantity()` の `(1-5): `
 * （`util.c:3246`）と `msg_input_num()` の **`(1 to 5): `**（`message.c:626`。
 * **店・我が家・建物の個数はこちら**）。片方しか見ていなかったので、店では
 * `frame.numeric` が立たず、決定がカーソル層の選択肢まで流れて品の letter を
 * 送っていた（実機で見つけた（2026-08-24）「数量入力で Enter/A を押すと a が
 * 入り購入可否に進めない」）。
 *
 * **なぜ機械の検査にするか**: `msg_input_num` を出せる場面（店・我が家・建物・
 * 矢筒）へは**新規キャラのボットが届かない**（矢は最初から矢筒の中、店は町の
 * 区画をまたげない——`tools/frox/fc_protocol_driver.py` の `walk_to` の註記）。
 * 綴りだけはここで押さえておく。
 */
int numeric_spelling_selftest();

/*!
 * @brief 命令列の札の読み取りの検査（`--selftest` から呼ぶ）。
 * @return 落ちた件数（0 なら通った）。落ちた中身は stderr へ書く
 *
 * @details 見るのは `[b/p/g] Buy` 形（札 3）の 1 点だけ——**札の頭が日本語でも
 * 拾えるか**である。M1 で店の帯が `[b/p/g] 買う` になったとき、頭を英字だけで
 * 見ていたせいで命令列が 1 つも札にならず、カーソルが品行に落ちて
 * **パッドから店のコマンドが選べなくなっていた**（2026-08-28 に実機で見つけた）。
 *
 * **なぜ機械の検査にするか**: `numeric_spelling_selftest()` と同じ理由で、
 * 店へは新規キャラのボットが届かない（`tools/frox/fc_protocol_driver.py` の
 * `walk_to` の註記）。帯の綴りが訳で変わるたびに実機で踏み直すことになるので、
 * 読み取りの側だけはここで恒久的に押さえる。
 *
 * 弾く側も同じだけ見る——**防具の `[2,+0]` を札にしない**。日本語が続くと
 * 頭の検査は通ってしまうので、担保は角括弧の中身（数字は札のキーにしない）に残る。
 */
int command_label_selftest();

/*!
 * @brief **品選びの窓が重なった画面**の札の絞り込みの検査（フック #37）。
 * @return 落ちた件数（0 なら通った）。落ちた中身は stderr へ書く
 *
 * @details `obj_prompt()` は下の画を消さずに重なるので、店で開くと letter の並びが
 * 2 つ出る——窓の中の持ち物と、下に残った店の在庫である。**上の 1 つだけ**を
 * 札にしなければならない（同じ letter が 2 度出ると、品を選んだつもりで店の品を指す）。
 *
 * **なぜ機械の検査にするか**: この画へ届くには「売れる品を持って店へ入る」まで
 * 遊ぶ必要があり、駆動器では町の区画をまたげないことがある（`fc_menu.h` の
 * `numeric_spelling_selftest()` と同じ事情）。しかも**間違いは静かである**
 * ——札は出るので、押すまで違う品を指していることに気づけない。
 *
 * 2026-08-28 の実装は**ここで 1 度落ちた**。並びが切れた所で `break` したため、
 * その先の行が元の札を持ったまま残り、窓の 2 件のはずが 29 件出ていた。
 */
int obj_prompt_choice_selftest();

//! Rare selector/list grammars added by the command/UI audit.
int audit_semantics_selftest();

/*!
 * @brief **オプションの一覧を、原作のオプションの表から全件にする**（2026-10-10 の決めたこと）。
 *
 * @details 原作 `cmd4.c` の `do_cmd_options_aux()` は、`bottom_opt = Term->hgt - 3` を超える件数を
 * 「下へスクロール」の札で隠し、見える行だけを描く。新しい UI の一覧は Term の写しの見える行から作る
 * （共通の `modern_boolean_settings()`）ので、窓が小さいと 20 件ほどで切れて、後ろの項目
 * （誕生時オプションの `no_selling` など）が一覧に出なかった。
 *
 * 画面の高さは変えない（キーを受ける途中で `Term_resize()` を呼ぶとゲーム本体が落ちた）。代わりに:
 * - `apply()`: 共通の側が作った窓の**行**を、原作の表（`option_info[]`。`fc_options_row()`）の全件に差し替える。
 *   見えている行は共通の側が作った行をそのまま使い、見えていない行だけ表から足す（訳も同じ口 `fc_tr_arg`）。
 *   右の欄（原作の画面を並べた本文）は触らない——原作が自分でスクロールするので、カーソルを動かせば追いかける。
 *   ゲーム中の閲覧のみの頁（`settings_readonly`）には行の選択が無いので、本文のほうを全件にする。
 * - `rewrite_focus()`: 画面がカーソルを動かすたびに送る focus の知らせを、**原作のカーソルを 1 つ動かす上下のキー**
 *   （`native_key` の `2`/`8`）に直す。原作は端で回り込むので、近いほうの向きへ動かす。値は書き換えない
 *   （左右は共通の側が原作の `y`/`n` に直している）。
 */
class OptionsList {
public:
    void apply(GameFrame &frame);
    void rewrite_focus(presentation::InputEventWire &event) const;

private:
    std::vector<std::string> ids_;
    std::string cursor_;
};
OptionsList &options_list();

//! オプションの一覧の検査（`--selftest` から呼ぶ）。落ちた件数。
int options_list_selftest();

/*!
 * @brief **選択メニュー（原作の `menu_choose`）の一覧を、全件・原作の並びで組む**（2026-10-10 の決めたこと）。
 *
 * @details 原作の `menu_choose()`（`menu.c`）は項目を全部 Term の行へ描き、スクロールしない。新しい UI の一覧は
 * Term の写しの行から作る（`fill_menu_choices()`。選択肢は 64 件まで）ので、(1) 項目が Term の高さを超えるとはみ出した項目が一覧に
 * 出ず選べない、(2) 2 列に割るメニュー（突然変異・才能の選択）は行ごとに読まれて並びが a, s, b, t … と混ざる。
 *
 * 呼ぶ側の翻訳単位だけ、組み立ての設定で `menu_choose` を `fc_menu_choose`（`fc_shim.c`）に差し替えてある
 * （`fc_shim.h` の「選択メニュー `menu_choose()` の全件」）。`fc_menu_choose()` はメニューの全件を原作の
 * コールバックから先に取って置き、原作の `menu_choose()` をそのまま呼ぶ。ここは**取った全件でフレームの選択肢を組み直す**。
 * - 項目ごとに、Term の写しの行に見えているもの（共通の側が読んだ選択肢、または `キー) 字` の行の探索）はその行の位置を
 *   使い（右の欄の本文から選択肢の字を抜く処理が行の位置を頼る）、見えていないもの（Term の外へはみ出した項目）は行の無い選択肢にする。
 * - 並びは原作の項目の並び。字は原作が描く字と同じ（`fc_spell_name`）。説明（`MENU_HELP`）は選択肢の説明にする。
 * - 色が `TERM_DARK` の項目は、原作もキーを付けず行を描かない（選べない）ので出さない。
 * - 行に結び付かない補助の選択肢（`C` 人物調書・`?` 閲覧）は項目の後ろに残す。
 * Term に 1 項目も見えていないとき（人物調書の画面など、メニューの画でないとき）は何も変えない。
 * 選ぶと画面側がその項目のキーを送り、原作の入力の処理（`get_com`）がそのまま受ける。
 */
struct MenuChooseItem {
    int key{};            //!< 押すキー（原作が振ったもの。小文字化も済み）
    std::string text;     //!< 表示の字（UTF-8）
    std::string help;     //!< 説明（UTF-8。無ければ空）
};
//! 開いている選択メニューの全件を `fc_menu_choose_item()` から読む（開いていなければ空）。
std::vector<MenuChooseItem> menu_choose_items();
//! 全件で `frame.menu_choices` を組み直す。組み直したら true。
bool menu_choose_apply(GameFrame &frame, const std::vector<MenuChooseItem> &items);
//! `menu_choose_items()` で読んで `menu_choose_apply()` する。
void menu_choose_apply(GameFrame &frame);

//! 選択メニューの検査（`--selftest` から呼ぶ）。落ちた件数。
int menu_choose_selftest();

/*!
 * @brief 店の購入の確認を、決定だけで通せるようにする（初期の選択を「はい」にする）。
 * @details 購入の確認は原作の `msg_prompt("…Really buy %s for %d gp? [y/n]", "ny", PROMPT_YES_NO)`（`shop.c` の `_sell_aux`）。
 * `[y/n]` は小文字なので、共通の側（`modern_content.h`）の既定は「いいえ」になる（`[Y/n]` だけ「はい」）。
 * 品と個数を選んだあとの確認で「いいえ」が初期だと、決定ボタン（A）・Enter を続けて押すと**買わずに店の一覧へ戻り**、
 * 画面は何も言わない（買えないように見える。2026-10-11 の気づいたこと）。売る・譲る・取り置き・品揃えの入れ替えは
 * 取り返しのつかない確認なので「いいえ」のまま。購入の確認だけ「はい」にする（変愚・短愚の店の確認は `[Y/n]` で、もとから「はい」）。
 * 店が開いていて確認の段（`frame.modern.stage == "confirm"`）の文が購入の確認のときだけ変える。
 */
void store_purchase_default_yes(GameFrame &frame);
//! `store_purchase_default_yes()` の検査（`--selftest` から呼ぶ）。落ちた件数。
int store_purchase_default_selftest();

/*!
 * @brief 店の命令の同義キー（`p` `g` `d`）を、店の窓がコアへ送れるか（`--selftest` から呼ぶ）。落ちた件数。
 * @details 原作の店は同じ命令を複数のキーで受ける（`[b/p/g] Buy  [s/d] Sell`。`shop.c`・`home.c`）が、アダプタが申告する選択肢は
 * 各札の先頭の 1 キー（`b`・`s`）だけである。以前は `p` を押すと『この画面では使えないキーです。』と断られた（2026-10-11）。
 * Frox 以外のプロファイルでは同義キーを広げない（他のコアの店は変えない）こともここで見る。
 */
int store_native_key_selftest();


} // namespace fc
