/*!
 * @file sq_pad_commands.cpp
 * @brief `sq_pad_commands.h` の実装。**根拠はコード**（推測で足さないこと）。
 *
 * ## どこから採ったか
 * キーの正は `silq/src/dungeon.c` の `process_command()`（:762〜）の `case` と、
 * `silq/lib/pref/pref.prf` の keymap 定義である。
 *
 * | 動作 | キー | 出どころ |
 * |---|---|---|
 * | 移動 8 方向 | `1`〜`9`（`5` を除く） | `pref.prf:45-60`（`;<数字>`＝歩くへ写る。keyset 0/1 とも同じ） |
 * | その場で待つ | `z` | `dungeon.c:934`（`do_cmd_hold`） |
 * | 上り階段 | `<` | `dungeon.c:965` |
 * | 下り階段 | `>` | `dungeon.c:976` |
 * | 拾う | `g` | `dungeon.c:949` |
 * | 休憩 | `Z` | `dungeon.c:941-942`（`%` も同じ） |
 * | 歌を変える | `s` | `dungeon.c:864`（`do_cmd_change_song`。Sil-Q 固有） |
 * | 忍び足の切替 | `S` | `dungeon.c:958`（`do_cmd_toggle_stealth`。Sil-Q 固有） |
 * | 技能・特技 | `Tab` | `dungeon.c:871`（`do_cmd_ability_screen`） |
 * | 鍛冶 | `0` | `dungeon.c:878`（`do_cmd_smithing_screen`。Sil-Q 固有） |
 * | 持ち物 | `i` | `dungeon.c:857` |
 * | 装備 | `e` | `dungeon.c:850` |
 * | 装備する | `w` | `dungeon.c:822` |
 * | 射る | `f` | `dungeon.c:1051` |
 * | 投げる | `t` | `dungeon.c:1065` |
 * | 掘る | `T` | `dungeon.c:911` |
 * | 全体図 | `M` | `dungeon.c:1102` |
 * | セーブ | `Ctrl-S`（0x13） | `dungeon.c:1250` |
 *
 * ### W3 で足したぶん
 *
 * | 動作 | キー | 出どころ（`case` の行） |
 * |---|---|---|
 * | 調べる | `x` | `dungeon.c:888`（`do_cmd_observe`） |
 * | 何かする（方向つき） | `/` | `dungeon.c:904`（`do_cmd_alter`） |
 * | 位置を交換 | `X` | `dungeon.c:1015`（`do_cmd_exchange`。Sil-Q 固有） |
 * | 矢を作る | `-` | `dungeon.c:1021`（`do_cmd_fletchery`。Sil-Q 固有） |
 * | 杖を使う | `a` | `dungeon.c:1037`（`do_cmd_activate_staff`） |
 * | 食べる | `E` | `dungeon.c:1044`（`do_cmd_eat_food`） |
 * | 楽器を鳴らす | `p` | `dungeon.c:1079`（`do_cmd_play_instrument`。Sil-Q 固有） |
 * | 飲む | `q` | `dungeon.c:1086`（`do_cmd_quaff_potion`） |
 * | 人物表 | `@` | `dungeon.c:1159`（`do_cmd_character_sheet`。**技能を買う口**） |
 * | 敵の一覧 | `[` | `dungeon.c:1291`（`do_cmd_view_monsters`） |
 * | 品の一覧 | `]` | `dungeon.c:1297`（`do_cmd_view_objects`） |
 *
 * ### SH-32 で足したぶん（2026-08-22。W3 の続き）
 *
 * | 動作 | キー | 出どころ（`case` の行） |
 * |---|---|---|
 * | 外す | `r` | `dungeon.c:829`（`do_cmd_takeoff`） |
 * | 落とす | `d` | `dungeon.c:836`（`do_cmd_drop`） |
 * | 閉じる | `c` | `dungeon.c:994`（`do_cmd_close`。**護符の扉を作る唯一の道**） |
 * | 銘を刻む | `{` | `dungeon.c:1030`（`do_cmd_inscribe`。自由文字入力を通る） |
 * | 第 2 矢筒で射る | `F` | `dungeon.c:1058`（`do_cmd_fire(2)`。SH-15） |
 * | 使う | `u` | `dungeon.c:1093`（`do_cmd_use_item`） |
 * | 見る | `l` | `dungeon.c:1116`（`do_cmd_look`） |
 * | ヘルプ | `?` | `dungeon.c:1132`（`do_cmd_help`） |
 * | 主メニュー | `m` | `dungeon.c:1139`（`do_cmd_main_menu`） |
 * | 知識 | `~` | `dungeon.c:1278`（`do_cmd_knowledge`） |
 *
 * ### 追補 ④ で足したぶん
 *
 * リリース判定（同 §11.2）で「**どこからも届かない**」と数えた 4 件である。
 * キーボードがあれば押せるが、パッドとコマンドメニューだけで遊ぶ機体（Android・Quest・
 * 触りだけの機体）には道が無かった。**既定の割り当ては後で詰める**（決めたこと）。
 *
 * | 動作 | キー | 出どころ（`case` の行） |
 * |---|---|---|
 * | 走る | `.` | `dungeon.c:927`（`do_cmd_run`） |
 * | 壊す | `k` | `dungeon.c:843`（`do_cmd_destroy`） |
 * | 狙いを任せて投げる | `Ctrl-T`（0x14） | `dungeon.c:1072`（`do_cmd_throw(TRUE)`） |
 * | 地図を動かす | `L` | `dungeon.c:1109`（`do_cmd_locate`） |
 *
 * ## 名札の文字コード
 * 名札は **UTF-8 で載せる**（v1）。この TU の日本語リテラルは execution-charset が
 * ACP なので **CP932 で焼かれる**——だから**名札は ASCII で書く**（設計 §3.1・
 * 要件 R2「日本語モードでも英語を出しておく」）。日本語にするのは M1（翻訳）の仕事で、
 * そのときは変換器を通すか、UI 側の辞書に載せる。
 *
 * ### 訳を当てた（M1 の追補 A4。2026-08-22）
 * 上の「M1 の仕事」が P0〜P6 では**届かなかった**。段が名前・メッセージ・説明・画面・
 * ヘルプを回るあいだ、この表だけ英語で残り、キー設定の画で
 * **`A:決定` と `X:Smithing` が並ぶ**ことになっていた（気づいたこと）。
 *
 * 採ったのは「変換器を通す」ほう。**表は ASCII のまま**にして、載せる直前に
 * `tr_utf8()` でカタログを引く。**UI 側の辞書に載せる案は採れない**——
 * 必守制約 1「UI にコア固有の知識を持ち込まない」に触れる。
 *
 * 混ざって見えていた理由もここにある: 画面側の動作（決定・取消…）は
 * `feature_menu.cpp:663` の `ui_action_label()` から `assets/lang/*.json` を引くが、
 * コアが送る名札は `:681` でそのまま出る。**変愚と幻想蛮怒でこれが起きない**のは、
 * あちらが名札をコアの `menu_info` から実行時に採っていて、その版の言語がそのまま
 * 出るからである。ベタ書きの表を持っているのは Sil-Q だけだった。
 */

#include "sq_pad_commands.h"

#include "sq_lang_c.h"
#include "sq_text.h"

#include <cstring>
#include <string>
#include <vector>

namespace sq {

namespace {

struct PadSeed {
    /*!
     * @brief 一覧での番号。**表の並び順から採らない**（W3。2026-08-22）。
     *
     * @details 画面側は割り当てを `pad_bind=<ボタン>:<id>` で cfg に**焼き込む**
     * （`hd2d/ui/game_pad.h` の `PadBinds`）。番号を並び順から採ると、
     * **表の途中に 1 行足しただけで以後の割り当てが全部ずれる**——利用者の
     * 「持ち物」のボタンが黙って「調べる」になる。あちらの註記も
     * 「`id` は表の並び順から出る番号なので、コアの版が動くと別のコマンドを指しうる」と
     * 断ってあるが、**こちらが番号を据えれば起きない**。
     *
     * だから **番号は書いたら動かさない**。並べ替えは自由、追加は最大値の次から。
     */
    int id;
    int command; //!< コマンド文字（既定の割り当てはこちらで引く。`apply_default_pad_binds`）
    const char *group;
    /*!
     * @brief 分類の中の**小分類**（2026-08-23。空なら小分類なし）。
     * @details 「行動メニューをグループ化しもう 1 階層深くして」と決めた。
     * `Action` だけが 23 件あって、分類へ潜っても 1 枚に収まらなかった。
     * **並べるのは画面側の仕事**で、こちらは名前を付けるだけである
     * （`hd2d/ui/feature_menu.cpp` の `command_rows()`）。
     *
     * **小分類の行が並ぶ順は、この表に最初に出てきた順**である。だから
     * `Action` の行は小分類ごとにまとめて置いてある——ばらばらに置くと、
     * 画面に出る順が表の見た目と食い違う。
     */
    const char *subgroup;
    const char *label;
    int key;
    /*!
     * @brief 2 打目（0 なら 1 打で終わり）。
     *
     * @details **マニュアルのためだけに足した**（2026-08-23。気づいたこと
     * 「追加した機能は全てコントローラー単体で操作可能か？」）。`?` の画で `m` を
     * 押すとマニュアルが開くが、`?` の画でコアが待っているのは `inkey()` であって
     * 命令ではない——`frame.awaiting_command` が下りるので、**決定ではコマンド
     * メニューが開かない**（`sq_awaiting_command()` の註）。`?` と `m` を続けて
     * 送る 1 つの命令にすれば、コントローラだけで届く。
     *
     * 線の上では `seq_original` が**2 要素の並び**になるだけで、プロトコルは不変
     * （`PadCommandWireEntry::seq_original` はもともと配列である）。
     */
    int key2;
};

/*!
 * @brief 名札をカタログで引いて **UTF-8** で返す（追補 A4。頭の註記を見よ）。
 * @param ascii 表に書いてある英語の名札。これが**そのまま鍵**である
 * @return 訳文（UTF-8）。訳が無ければ `ascii` のまま
 *
 * @details `sq_tr()` が返すのは **CP932** なので、線に載せる前に直す。
 *
 * **日本語層が寝ているときは 1 バイトも変わらない**——`sq_tr()` が引数をそのまま
 * 返し、非 ASCII が無いので変換器も呼ばない（必守制約 1）。
 *
 * ここで日本語リテラルを直に書かないのは、**この TU の焼かれ方が平台で違う**ため
 * である（Windows は ACP＝CP932、Android は UTF-8）。どちらで組んでも同じ字を
 * 出すには、カタログという 1 本の道を通すのが確実である。
 */
std::string tr_utf8(const char *ascii)
{
    const char *const translated = sq_tr(ascii);
    if (translated == nullptr) {
        return std::string();
    }
    for (const char *p = translated; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80u) {
            return sjis_to_utf8(translated, std::strlen(translated));
        }
    }
    return std::string(translated); //!< 英語のまま（訳が無いか、日本語層が寝ている）
}

//! M0 の最小表（設計 §3）。**足すときはこの表だけ**を触る。
const PadSeed kSeeds[] = {
    { 1, '8', "Move", "", "North", '8' },
    { 2, '2', "Move", "", "South", '2' },
    { 3, '4', "Move", "", "West", '4' },
    { 4, '6', "Move", "", "East", '6' },
    { 5, '7', "Move", "", "Northwest", '7' },
    { 6, '9', "Move", "", "Northeast", '9' },
    { 7, '1', "Move", "", "Southwest", '1' },
    { 8, '3', "Move", "", "Southeast", '3' },
    { 9, 'z', "Move", "", "Stay", 'z' },
    { 10, '<', "Move", "", "Up staircase", '<' },
    { 11, '>', "Move", "", "Down staircase", '>' },
    /*
     * ---------------------------------------------------------------- Action
     *
     * **小分類ごとにまとめて置いてある**（2026-08-23。`PadSeed::subgroup`）。
     * 23 件を 1 枚に並べると送りっぱなしになるので、画面側はこの名前で
     * もう 1 段潜らせる（「行動メニューをグループ化しもう 1 階層深くして」と決めた）。
     *
     * どの命令がどこから来たか（M0 の最小表 / W3 / SH-32）は**頭の 3 つの表**にある。
     * 並べ替えても `id` は動かしていないので、**既存の `pad_bind=` はそのまま効く**
     * （番号を並び順から採らないのはこのためである。`PadSeed::id`）。
     */
    { 17, 'f', "Action", "Combat", "Fire", 'f' },
    { 41, 'F', "Action", "Combat", "Fire second quiver", 'F' },
    { 18, 't', "Action", "Combat", "Throw", 't' },
    /*
     * **狙いを任せて投げる**（`^T`。`dungeon.c:1072` の `do_cmd_throw(TRUE)`）。
     * `t` との違いは的を選ばないところで、乱戦でいちばん要る形である。
     * 制御キーなのでキーボードからは打てるが、**触りだけの機体には道が無かった**。
     */
    { 47, 0x14, "Action", "Combat", "Throw at nearest", 0x14 },
    { 13, 'Z', "Action", "Bearing", "Rest", 'Z' },
    { 14, 'S', "Action", "Bearing", "Stealth mode", 'S' },
    { 15, 's', "Action", "Bearing", "Change song", 's' },
    /*
     * **走る**（`.`。`dungeon.c:927` の `do_cmd_run`）。方向を続けて訊く。
     * 1 マスずつ押すのと結果は同じだが、長い通路で押す回数が 20 分の 1 になる。
     */
    { 48, '.', "Action", "Bearing", "Run", '.' },
    { 16, 'T', "Action", "Surroundings", "Tunnel", 'T' },
    //! `/` は方向を続けて訊く（`do_cmd_alter`）。`T`（掘る）と同じ作りなので画面も同じ。
    { 31, '/', "Action", "Surroundings", "Alter", '/' },
    /*
     * **`c`（閉じる）が要る。** 歩いて扉に当たれば**開く**のは自動だが（`cmd1.c:4749`）、
     * 閉じるほうに自動の道は無い。そして**護符の扉は「境の歌を歌いながら閉めた扉」で
     * しか生まれない**（`cmd2.c:1562`）ので、これが無いと触りだけの機体は SH-21 の
     * 3 色に**一生出会えない**。
     */
    { 37, 'c', "Action", "Surroundings", "Close", 'c' },
    { 32, 'X', "Action", "Surroundings", "Exchange places", 'X' },
    { 12, 'g', "Action", "Carrying", "Pick up", 'g' },
    //! `w`（装備する）だけあって `r`（外す）・`d`（落とす）が無いと、袋がいっぱいの所で手詰まる。
    { 19, 'w', "Action", "Carrying", "Wield", 'w' },
    { 38, 'r', "Action", "Carrying", "Take off", 'r' },
    { 39, 'd', "Action", "Carrying", "Drop", 'd' },
    //! `{`（銘）は**自由文字入力**を通る。W1 の旗が入ったので今は打てる。
    { 43, '{', "Action", "Carrying", "Inscribe", '{' },
    /*
     * **壊す**（`k`。`dungeon.c:843` の `do_cmd_destroy`）。落とすのとは違う——
     * 袋がいっぱいの所で要らない品を消せるのはこちらだけである。
     */
    { 49, 'k', "Action", "Carrying", "Destroy", 'k' },
    { 40, 'u', "Action", "Consuming", "Use item", 'u' },
    { 29, 'a', "Action", "Consuming", "Use staff", 'a' },
    { 27, 'q', "Action", "Consuming", "Quaff", 'q' },
    { 28, 'E', "Action", "Consuming", "Eat", 'E' },
    { 30, 'p', "Action", "Consuming", "Play instrument", 'p' },
    { 33, '-', "Action", "Consuming", "Fletchery", '-' },
    { 26, 'x', "Action", "Inspecting", "Examine", 'x' },
    { 42, 'l', "Action", "Inspecting", "Look", 'l' },
    /*
     * ------------------------------------------------------------------ Info
     *
     * こちらは 10 件なので**小分類は付けていない**（2 列なら 5 段で収まる）。
     * 段を深くするのは収まらないときだけでよい——深いほど遠くなる。
     */
    { 20, 'i', "Info", "", "Inventory", 'i' },
    { 21, 'e', "Info", "", "Equipment", 'e' },
    { 22, '	', "Info", "", "Abilities", '	' },
    { 23, '0', "Info", "", "Smithing", '0' },
    //! **技能を買う唯一の入口**（`@` → `i`。SH-01）。これが無いと触りだけでは伸ばせない。
    { 34, '@', "Info", "", "Character sheet", '@' },
    { 35, '[', "Info", "", "Monster list", '[' },
    { 36, ']', "Info", "", "Object list", ']' },
    { 24, 'M', "Info", "", "Level map", 'M' },
    /*
     * **地図を動かす**（`L`。`dungeon.c:1109` の `do_cmd_locate`）。`M` が階全体を
     * 1 枚に縮めるのに対し、こちらは**等倍のまま画面 1 枚ぶんずつ**動かす。
     * 遠くの敵や扉を数えるときはこちらでないと読めない。
     */
    { 50, 'L', "Info", "", "Scroll map", 'L' },
    { 44, '~', "Info", "", "Knowledge", '~' },
    { 45, '?', "Info", "", "Help", '?' },
    /*
     * **マニュアル**（`?` → `m`）。追補 ③ で英語にも出るようになったので、
     * 触りだけの機体からも 1 つの命令で開けるようにする。
     *
     * `m` だけを送っても駄目である——地図の上の `m` は主メニューで、
     * マニュアルになるのは `?` の画に居るときだけ（フック #17）。
     */
    { 51, '?', "Info", "", "Manual", '?', 'm' },
    /*
     * **主メニュー。**`m` の 1 件で人物表・設定・全体図・知識の画 5 枚・地形の記憶・
     * マクロ・色・履歴・版まで届く（`cmd4.c:6338-6366` の綴り）。触りだけの機体では
     * **ここが最後の逃げ道**になるので、表に無いと痛い。
     */
    { 46, 'm', "System", "", "Main menu", 'm' },
    { 25, 0x13, "System", "", "Save", 0x13 },
};

} // namespace

presentation::PadCommandsMessage build_pad_commands()
{
    presentation::PadCommandsMessage message;
    /* Sil-Q に配列の切り替えは無い（`sq_pad_commands.h` の注記）。 */
    message.current_keymap = "original";

    for (const PadSeed &seed : kSeeds) {
        presentation::PadCommandWireEntry wire;
        //! **番号は表が持っている**（並び順から採らない。`PadSeed::id` の註記）。
        wire.id = seed.id;
        wire.command = seed.command;
        //! 追補 A4。**表は ASCII のまま**で、載せる直前に引く（頭の註記）。
        wire.group_utf8 = tr_utf8(seed.group);
        /*
         * 小分類（2026-08-23）。**空のまま送る行がある**ので、引く前に見る——
         * `sq_tr("")` は空文字をそのまま返すが、無い鍵をカタログに問い合わせるのは
         * 「訳が抜けている」と見分けがつかない。
         */
        wire.subgroup_utf8 = ((seed.subgroup != nullptr) && (seed.subgroup[0] != '\0'))
            ? tr_utf8(seed.subgroup)
            : std::string();
        wire.label_utf8 = tr_utf8(seed.label);
        wire.seq_original.push_back(seed.key);
        wire.seq_rogue.push_back(seed.key);
        if (seed.key2 != 0) {
            //! 2 打目（マニュアルだけ）。**Sil-Q に配列の切り替えは無い**ので両方へ同じ物を積む。
            wire.seq_original.push_back(seed.key2);
            wire.seq_rogue.push_back(seed.key2);
        }
        message.entries.push_back(std::move(wire));
    }

    return message;
}

} // namespace sq
