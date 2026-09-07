/*!
 * @file game_pad.h
 * @brief ゲームパッド（P8）— 十字／スティックで歩き、ボタンでコマンドを出す。
 *
 * ## 落とすと必ず壊れるもの（既存 UI の K-36 で分かっている）
 * | # | 約束 | 落とすとどうなるか |
 * |---|---|---|
 * | 1 | **窓が入力の焦点を持っていないときは方向を出さない** | 裏に回した窓のパッドでプレイヤが歩き続ける |
 * | 2 | **焦点が戻った／パッドを挿した直後は、一度中立を見るまで受け付けない** | 「握ったまま」の状態が復帰の瞬間に走り出す |
 * | 3 | **スティックの死区は出入りで別のしきい値にする**（ヒステリシス） | 境目でがたつき、1 回のつもりが数回入る |
 * | 4 | **方向を入れ替えたときもリピートの間隔を置く** | 斜めへ倒す途中の一瞬が 1 歩になる |
 * | 5 | **トリガは軸。立ち上がりでだけ発火させる** | 押しっぱなしで連射になる |
 *
 * ## 割り当て
 * 方向・決定（A）・取消（B）・機能メニュー（Back）は**固定**。それ以外の 9 個は
 * コアのコマンドへ割り当てられる。コマンドの一覧と名前と**キー列**は
 * コアが `pad_commands` で送ってくる（v1 §8）ので、こちらは表を持たない。
 *
 * @note 旧 2D UI（削除済み）と
 * **同じことができる**ようにしてある。実装は引かない（必守制約 6）。
 * あちらは `KeyQueue` に積む作りで、こちらはワイヤの出来事を積む作りなので形が違う。
 */
#pragma once

#include "frame/protocol_messages.h"

#include <cstdint>
#include <string>
#include <vector>

union SDL_Event;

namespace hd2d {

//! 割り当てられるパッドの入力。**方向は含まない**（保持と連射の扱いが違う）。
enum class PadInput {
    A,
    B,
    X,
    Y,
    LeftShoulder,
    RightShoulder,
    LeftTrigger,
    RightTrigger,
    Back,
    Start,
    LeftStick,
    RightStick,
    Count,
};

constexpr int kPadInputCount = static_cast<int>(PadInput::Count);

//! 画面と cfg に出す綴り（`A` `LB` `LT` …）。
const char *pad_input_name(PadInput input);
//! 綴りから読む。読めたら true。
bool parse_pad_input(const std::string &name, PadInput &out);

/*!
 * @brief 固定で意味を持つ入力か（割り当ての対象にしない）。
 * @details A＝決定・B＝取消・Back＝機能メニュー。**メニューの中では B と Back を
 * 最初に見る**（既存 UI の K-4。取消を割り当て可能にすると、メニューから出られなくなる）。
 */
bool pad_input_is_fixed(PadInput input);

/*!
 * @name 同時押しの修飾（2026-08-14 に決めた）
 *
 * @details 「割り当てられるキーを増やしたい」。**十字をボタンにするのではなく、
 * LB／RB を押しながらの同時押しで層を増やす**ことにした（2026-08-14 に決めた）。
 * 十字を割り当ての対象にすると、方向の経路——ヒステリシス・連射・再武装・焦点という
 * 「落とすと必ず壊れる」約束が全部あるところ——を作り替えることになり、
 * しかも**十字で歩く手段が戻せなくなる**。同時押しは何も奪わずに枠だけ増やせる。
 *
 * 値はコアのマクロトリガーの修飾文字（`lib/pref/pref-xxx.prf` の
 * `T:&x#:CSA:control-:shift-:alt-` の `C` と `A`）に対応する。**新しい概念ではなく、
 * `\x1f` の次に 1 バイト足すだけ**である（`\[control-Pad_X]` と読める形で表示・保存される）。
 * @{
 */
constexpr int kPadModCtrl = 1; //!< LB を押しながら（`control-`）
constexpr int kPadModAlt = 2; //!< RB を押しながら（`alt-`）
//! 層の数（無し・C・A・C+A）。表示用の配列の寸法に使う。
constexpr int kPadModLayerCount = 4;

//! 修飾に使うボタンか（LB / RB）。**固定**なので利用者は変えられない。
bool pad_input_is_modifier(PadInput input);
//! そのボタンが表す修飾。修飾でなければ 0。
int pad_modifier_of(PadInput input);
//! cfg に書く綴り（`""` / `"C"` / `"A"` / `"CA"`）。層 0 は空。
const char *pad_mods_tag(int mods);
//! 同じ綴りから読む。読めなければ -1（＝知らない綴り。呼ぶ側は黙って飛ばす）。
int parse_pad_mods(const std::string &tag);
//! 画面に出す綴り（`X` / `LB＋X` / `LB＋RB＋X`）。**割り当ての一覧と帯で同じものを使う。**
std::string pad_chord_display_name(PadInput input, int mods);
/*! @} */

/*!
 * @brief 押し 1 回ぶん。**修飾を一緒に運ぶ**（同時押しかどうかは押した瞬間に決まる）。
 */
struct PadPress {
    PadInput input{ PadInput::Count };
    int mods{ 0 }; //!< `kPadModCtrl` 等の論理和。0 = 単独
};

/*!
 * @brief 同時押しの状態機械。**実機のパッドとバーチャルパッドが同じものを使う。**
 * @details 入口ごとに書くと片方だけ直された状態に必ずなる（既存 UI の K-18）。
 *
 * 約束は 2 つだけ。
 * 1. **修飾を押した瞬間には何も起きない。**離したときに、その間に他のボタンを
 *    押していなければ「単独押し」として初めて発火する（2026-08-14 に決めた。
 *    LB／RB は階段の上り下りを持っているので、単独でも使えないと困る）。
 * 2. **修飾を押している間の他のボタンは同時押し**になり、その修飾は「使われた」
 *    印が付く（＝離しても単独発火しない）。修飾どうしを重ねたときも同じで、
 *    LB＋RB は `control-alt-` の層になる。
 */
class PadChord {
public:
    /*!
     * @brief ボタンが押された。
     * @param[out] mods_out 修飾でないときだけ、そのとき押されていた修飾が入る。
     * @return **修飾ボタンなら true**（呼ぶ側は押しを積まない）。
     */
    bool press(PadInput input, int &mods_out);
    /*!
     * @brief ボタンが離された。
     * @param[out] out 単独押しとして今こそ積むべきボタン。
     * @return 積むべきなら true。
     */
    bool release(PadInput input, PadInput &out);
    //! いま押されている修飾（`kPadModCtrl` 等の論理和）。
    int mask() const;
    //! 全部忘れる（焦点を失ったとき・メニューを開いたとき）。
    void forget();

private:
    bool held_[2]{}; //!< 0 = LB / 1 = RB
    bool used_[2]{}; //!< 同時押しに使われた（＝単独ではなくなった）
};

/*!
 * @name マクロのトリガーとしてのパッド（2026-08-14 に決めた）
 *
 * @details 「マクロのトリガーにパッドのキーを入れられないか」。**入れられる。**
 * コアはマクロのトリガーを `\x1f` ＋修飾＋`x`＋走査符号 2 桁＋`\r` という列で受け取り、
 * **その列にマクロが登録されていなければ黙って飲み込む**
 * （`src/io/input-key-acceptor.cpp:275`。`UNIT_SEPARATOR` を見ると `parse_under` が立ち、
 * 末尾の `\r`（≦32）まで捨てる）。本物の端末が未登録のファンクションキーを押された
 * ときと同じ振る舞いで、**空振りしても誤コマンドにならない**。
 * だからパッドのボタンにも走査符号を 1 つずつ与えて、その列を送ればよい。
 *
 * 送るのは**割り当ての無いボタンだけ**（2026-08-14 に決めた）。
 * 割り当て済みのボタンは今までどおりその操作を送る——マクロを乗せたければ
 * 先に割り当てを外す。「押したのに割り当てと違うものが走る」を作らないための線引きである。
 *
 * 名前（`\[Pad_X]`）は `lib/pref/pref-xxx.prf` の `T:` 行が与える。
 * **符号はあちらとここで対になっている。片方だけ変えないこと。**
 * @{
 */

//! そのボタンのマクロトリガー走査符号。**0 = トリガーを持たない**（A・B・Back）。
int pad_input_trigger_scancode(PadInput input);

/*!
 * @brief そのボタンの「押した」を表すキー列（`\x1f x HH \r`）。持たなければ空。
 * @details 綴りは `presentation/bridge/input_event_adapter.cpp` の `fkey` 展開と同じ
 * （あちらが F キーで作っている列の、走査符号だけがパッドのものになったもの）。
 */
std::vector<int> pad_input_trigger_sequence(PadInput input, int mods = 0);

/*!
 * @brief ボタンごと・層ごとの「マクロが乗っているか」。
 * @details コアから届いたトリガー一覧（`macro_triggers`。v1 §8.4）を、こちらの
 * ボタンの綴りと突き合わせて 1 回だけ畳んだもの。描くたびに突き合わせないのは、
 * 一覧を描く所が 3 か所あって、それぞれで畳むと**片方だけ直された状態**になるため。
 * 層（`mods`）ごとに持つのは、**修飾を押している間だけ裏の割り当てを見せる**ため
 * ——同時押しは覚えていないと使えないので、画面に出せないと死ライブラリする。
 */
struct PadMacroFlags {
    bool has[kPadModLayerCount][kPadInputCount]{};

    bool operator()(PadInput input, int mods = 0) const
    {
        const int index = static_cast<int>(input);
        if ((index < 0) || (index >= kPadInputCount)) {
            return false;
        }
        if ((mods < 0) || (mods >= kPadModLayerCount)) {
            return false;
        }
        return this->has[mods][index];
    }
};

//! コアから届いたトリガー一覧を畳む。**押したときの判定とは独立**（あちらは登録の有無を見ない）。
PadMacroFlags make_pad_macro_flags(const std::vector<std::vector<int>> &macro_triggers);
/*! @} */

//! コアのコマンド 1 件（`pad_commands` で届く。v1 §8）。
struct PadCommand {
    int id{ 0 };
    /*!
     * @brief オリジナル配列の Angband コマンド 1 文字（`g` = 拾う、`>` = 階段を降りる …）。
     * @details **既定の割り当てを書けるのはこの欄のおかげ**である。`id` は表の並び順から
     * 出る番号なので、コアの版が動くと別のコマンドを指しうる。「拾う」を名指ししたければ
     * `'g'` で引くしかない（`apply_default_pad_binds()`）。
     */
    int command{ 0 };
    std::string group_utf8;
    std::string label_utf8;
    //! いま有効なキー配列での解決結果。**空 = 解決不能**（割り当てても出せない）。
    std::vector<int> keys;
    /*!
     * @brief 分類の中の**小分類**（v1 の追補。2026-08-23）。**空なら小分類は無い**。
     * @details コマンドメニューを 3 段にするためだけの欄で、割り当ての一覧は見ない
     * （あちらは分類の見出しだけで足りる）。詳しくは
     * `presentation/frame/protocol_messages.h` の同名の欄。
     *
     * @note **並びの最後に置いてある。**この構造体は検査のあちこちで
     * `PadCommand{ id, cmd, 分類, 名札, キー }` と括弧で組まれており、
     * 途中に足すと全部の 5 番目が小分類として読まれる（`keys` が文字列に化けて
     * 組めなくなる）。足すなら末尾。
     */
    std::string subgroup_utf8;
};

/*!
 * @brief SDL 以外から来る「いまのパッドの状態」（VR の Touch コントローラ）。
 * @details。**XR の actions をここへ流し込むと、
 * ヒステリシス・連射・再武装・同時押しの割り当てが全部そのまま効く**——
 * それがこの口を作った理由である。XR 側に同じ仕掛けを書くと、必ず片方だけ直る。
 *
 * 状態（押されているか）で渡すこと。縁（押した瞬間）は `GamePad` が出す。
 */
struct ExternalPadState {
    //! 添字は `PadInput`。トリガ（LT/RT）も真偽で渡す（しきい値は渡す側で通す）。
    bool button[kPadInputCount]{};
    /*!
     * @name スティック（−1〜+1）
     * @details 左は歩く向き（**下が正**。画面の南）。右は視点。
     * @{
     */
    float left_x{ 0.f };
    float left_y{ 0.f };
    float right_x{ 0.f };
    float right_y{ 0.f };
    /*! @} */
};

class GamePad {
public:
    //! SDL のイベントを 1 つ渡す。パッドのものなら食って true。
    bool on_event(const SDL_Event &event);
    void close();

    /*!
     * @brief SDL 以外の入力（VR の Touch）の状態を流し込む。
     * @details 毎フレーム呼ぶ。縁の検出・同時押し・ヒステリシスは中でやるので、
     * 呼ぶ側は「いま押されているか」だけを渡す。**繋がっていない間は呼ばないこと**
     * （呼ぶと `connected()` が真になる）。
     */
    void feed_external(const ExternalPadState &state);

    bool connected() const { return (this->controller_ != nullptr) || this->external_connected_; }
    const std::string &name() const { return this->name_; }

    /*!
     * @brief 方向を 1 回ぶん取り出す（初回押下とリピート）。
     * @param now_ms `SDL_GetTicks`。
     * @param focused 窓が入力の焦点を持っているか（約束 1）。
     * @param walk_repeat_ms **0 以外なら「歩き」の歩調**（ミリ秒）。1 歩目のあとの
     *   待ちを置かず、この間隔で等間隔に出す。
     * @return 出たら true。`dx`/`dy` は -1..1。
     *
     * @details 既定（0）は「1 歩目 → 260ms 待つ → 90ms ごと」。メニューのカーソルには
     * これでよいが、**歩くときにその待ちが入ると 1 歩ごとに止まって見える**
     * （一人称 VR では酔いに直結する。2026-08-14 に気づいた）。
     */
    bool poll_direction(std::uint32_t now_ms, bool focused, int &dx, int &dy, int walk_repeat_ms = 0);

    /*!
     * @brief 押し 1 回ぶんを取り出す。無ければ false。
     * @details **修飾（LB/RB）は押した瞬間には出てこない**（`PadChord` の約束 1）。
     * 単独押しだったと確定する「離した瞬間」に、修飾なしの押しとして出る。
     */
    bool take_pressed(PadPress &out);
    //! いま押されている修飾。表示（同時押しの層の見せ方）に使う。
    int chord_mask() const { return this->chord_.mask(); }

    /*!
     * @name 右スティックの傾き（−1〜+1）。死区の内側は 0
     *
     * @details **視点を回すためだけ**の口。左スティックと違って「1 回ぶんの向き」ではなく
     * 連続値を返す（回転は速度で効かせるため）。繋がっていない・中立なら 0 なので、
     * 呼ぶ側は条件を書かなくてよい。
     *
     * | | 見下ろし（本線） | 一人称 |
     * |---|---|---|
     * | 横（`x`。右が正） | — | 方位を回す |
     * | 縦（`y`。**下が正**） | **見下ろし角を変える**（2026-08-11 に決めた） | 仰角・俯角 |
     * @{
     */
    float right_stick_x() const;
    float right_stick_y() const;
    /*! @} */

    //! 「握ったまま」を全部忘れる（焦点を失ったとき・メニューを開いたとき）。
    void forget_hold();

private:
    void open(int joystick_index);
    //! いまの十字とスティックから向きを出す（ヒステリシスつき）。
    void current_direction(int &dx, int &dy) const;

    void *controller_{ nullptr }; //!< `SDL_GameController*`（ヘッダに SDL を持ち込まない）
    std::int32_t joystick_id_{ -1 };
    std::string name_;

    //! いま押している向き（0,0 なら中立）。
    int held_dx_{ 0 };
    int held_dy_{ 0 };
    std::uint32_t next_repeat_ms_{ 0 };
    //! **一度中立を見るまで受け付けない**（約束 2）。
    bool rearm_pending_{ true };

    //! 左スティックのラッチ（-1 / 0 / +1）。死区の出入りにヒステリシスを持たせる（約束 3）。
    int stick_x_{ 0 };
    int stick_y_{ 0 };
    //! 十字の押下状態。
    bool dpad_[4]{ false, false, false, false };
    //! トリガの押下状態（軸なので立ち上がりでだけ発火させる。約束 5）。
    bool trigger_down_[2]{ false, false };

    std::vector<PadPress> pressed_;
    PadChord chord_; //!< 同時押しの状態機械（バーチャルパッドと同じもの）

    /*!
     * @name SDL 以外の入力（VR の Touch）
     * @details 縁を取るために前回の状態を覚える。右スティックは**実機のパッドが
     * 繋がっていればそちらを優先**する（机の上のパッドを挿したまま被れる）。
     * @{
     */
    bool external_connected_{ false };
    bool external_prev_[kPadInputCount]{};
    float external_right_x_{ 0.f };
    float external_right_y_{ 0.f };
    /*! @} */
};

/*!
 * @brief パッドのボタンに操作を割り当てた表。
 * @details 値は**操作の番号**（`ui/key_binds.h`）。正 = コアのコマンド `id`、
 * 負 = この exe 自身の操作、**0 は割り当て無し**。
 *
 * @note 持ち方は「入力 → 操作」のままだが、**画面は「操作 → 入力」で見せる**
 * （2026-08-11 に決めた）。持ち方まで裏返すと cfg の綴りが変わって
 * 既存の `pad_bind=` が読めなくなるので、引く向きだけを `input_for()` で足してある。
 */
struct PadBinds {
    /*!
     * @brief 層 × ボタンの割り当て。**添字は `[修飾][ボタン]`。**
     * @details 層 0（`command[0][i]`）が単独押しで、これが従来の持ち方そのもの。
     * 同時押し（2026-08-14 に決めた「同時押しも割り当て対象に含められる？」）を
     * 足すにあたり、**特例を作らず次元を 1 つ増やした**——「同時押しは必ずマクロ」と
     * いう例外を残すと、押したときの規則が単独と同時で 2 本になる。
     * いまは **(ボタン, 層) の組がそれぞれ枠で、空いている枠がマクロのトリガー**という
     * 1 本の規則で全部が説明できる。
     */
    int command[kPadModLayerCount][kPadInputCount]{};

    //! この操作を持っている入力と層。無ければ `input == PadInput::Count`。
    PadPress input_for(int action) const;
    /*!
     * @brief 割り当てる。
     * @details **同じ操作を 2 つの枠に持たせない**（先客からは外す）。
     * 固定の入力（`pad_input_is_fixed`）は**層 0 でだけ**断る——A / B / Back が固定なのは
     * 決定・取消・メニューという「単独押しの役」のためで、層の中では空いている
     * （2026-08-14 に気づいた）。修飾そのもの（LB/RB）は逆で、**層 0 でだけ**受け付ける
     * ——`LB＋LB` は押しようがない。
     * @return 割り当てられたら true。
     */
    bool assign(int action, PadInput input, int mods = 0);
    //! この操作の割り当てを消す（どの層にあっても消える）。
    void clear(int action);

    //! `X:12,C+Y:34` の形へ（cfg に書く）。**層 0 は綴りを付けない**（既存の cfg と同じ形）。
    std::string to_line() const;
    //! 同じ形から読む。読めた分だけ入れる。**`+` の無い綴りは層 0**（前の版の cfg がそのまま読める）。
    void parse(const std::string &line);
};

/*!
 * @brief 既定の割り当てを入れる（2026-08-11 に決めた）。
 *
 * ```
 *   X       拾う                 g
 *   Y       投射物を撃つ         f
 *   R1(RB)  階段を降りる         >
 *   L1(LB)  階段を上る           <
 *   R2(RT)  掘る                 T
 *   L2(LT)  探す                 s   ← **`S` ではない**（下の注記）
 *   R3      一人称の視界を水平に戻す
 *   L3      **一人称の入切**（2026-08-11 に決めた。F7 と同じ操作）
 *   Select(Back)  機能メニュー   ← **固定**なのでここでは触らない
 * ```
 *
 * @param commands コアから届いたコマンド表。**空なら核のコマンドは入れられない**
 *   （握手の直後はまだ来ていない）。R3 だけは表を要らないので必ず入る。
 * @return 1 つでも入れたら true。
 *
 * @details **`id` ではなく `command`（Angband のコマンド 1 文字）で引く。**`id` は
 * コアの `menu_info` の並び順から出る番号なので、上流を取り込んで表が動くと
 * 「拾う」のつもりが別のコマンドになる。文字なら意味が変わらない。
 * 見つからないコマンドは**黙って飛ばす**（版によっては無いこともある）。
 *
 * @note **`s` と `S` は別のコマンドである**（2026-08-11 に直した）。
 * `src/cmd-io/cmd-menu-content-table.cpp` の表で
 * `s` = **探す**（1 回探索する）／`S` = **探索モードの ON/OFF** となっている。
 * LT に `S` を入れて「探す」と書いていたので、押すと歩くたびに自動で探す状態が
 * 切り替わっていた。**綴りが似ているだけの別物**なので、文字で引くこの作りでも
 * 表を 1 度は読んで確かめること。
 */
bool apply_default_pad_binds(PadBinds &binds, const std::vector<PadCommand> &commands,
    const std::string &core_name);

/*!
 * @brief コアのコマンド `id` をコアへの出来事へ変える。
 * @param commands コアから届いたコマンド表。
 * @param[out] out ここへ積む。**キー列は順序どおり**に積む（1 コマンドが複数キーのことがある）。
 * @return 積んだら true（割り当てが無い・解決できないときは false）。
 *
 * @note **キーボードから来ても パッドから来ても同じここを通る。**入口ごとに書くと、
 * 片方だけ直された状態に必ずなる（既存 UI の K-18 と同じ話）。
 */
bool emit_core_command(int command_id, const std::vector<PadCommand> &commands,
    presentation::InputEventsMessage &out);

/*!
 * @brief 割り当ての無いボタンを「マクロのトリガーを押した」としてコアへ送る。
 * @return 送ったら true。トリガーを持たないボタン（A・B・Back）なら false。
 * @details 送るのは `keyseq`（生のキー列）。トリガーの先頭は `\x1f` で、
 * `key` の `char` は印字 ASCII に限られているため運べない
 * （`presentation/frame/protocol_messages.h` の `bytes` の注）。
 * @note **登録が無ければコアが黙って飲む**ので、呼ぶ側は「マクロがあるか」を
 * 気にしなくてよい（`pad_input_trigger_scancode` の注）。
 */
bool emit_pad_macro_trigger(PadInput input, int mods, presentation::InputEventsMessage &out);

/*!
 * @brief 最下段に出す「全ボタンの割り当て」の項目（`A:決定` `X:拾う` の形）。
 *
 * @details 旧 2D UI（削除済み）にあったものを
 * この exe にも置く（2026-08-11 に決めた「すべてのコントローラーキーの割り当てを表示」）。
 *
 * **未割り当ても並べる。**割り当てが無いことも利用者が知りたい情報であり、
 * 「出ていない」と「割り当てが無い」は画面から見分けられなければならない。
 * 折り返しは呼び出し側（字送りを知っているのは描画側だけ）。
 */
std::vector<std::string> build_pad_bind_items(const PadBinds &binds, const std::vector<PadCommand> &commands,
    const PadMacroFlags &macros, int chord_mask);

} // namespace hd2d
