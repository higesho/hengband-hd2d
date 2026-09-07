/*!
 * @file feature_menu.h
 * @brief 機能メニュー（P8）— F10 で開く、ゲームの外の設定。
 *
 * ## 何を入れて、何を入れないか
 * 2026-08-08 に決めたは「**HD2D 版に要るものだけ**」。
 * `HengbandUi.exe` の機能メニューには、この実行体に**存在しない**ものが並んでいる
 * （SDL2 / HD2D の経路切替・液晶表現・Android のバーチャルパッド・タイルの倍率）。
 * 器だけ残して並べると「押しても何も起きない項目」になるので、入れない。
 *
 * ## 階層にする理由
 * 項目が 30 を超え、**低い窓では下が見切れた**（2026-08-08）。
 * 1 枚に詰めるのをやめ、**分類を選んでから中へ入る**形にした。
 * どの分類も 1 画面に収まるので、窓の高さに関係なく最後の項目まで届く。
 *
 * ```
 * 機能メニュー ─┬─ 言語           ★入口に直接置く値の項目（節ではない）
 *               ├─ 画面           どこに何を置くか。ミニマップの節を持つ
 *               ├─ 絵づくり       どう描くか。効果の節を持つ
 *               ├─ カメラ         どこから見るか
 *               ├─ 一人称         一人称の見え方。出すパネルの節を持つ
 *               ├─ サブパネル     下段の幅・どの枚に何を映すか
 *               ├─ 音             ★コアが `audio` を申告したときだけ
 *               ├─ 操作の割り当て 操作ごとにキーボードとコントローラーを決める
 *               ├─ バーチャルパッド ★Android だけ
 *               ├─ ゲーム進行     ★コアが `realtime` を申告したときだけ
 *               └─ VR             ★VR のある実行体だけ
 * ```
 *
 * ## 分けかたの軸（2026-08-19 の組み替え）
 * 前は「カメラ／画面／サブパネル／ポスト処理／操作の割り当て」の 5 つで、
 * **同じ話が 3 つの節に散っていた**。一人称の設定はカメラに 3・ポスト処理に 1・VR に 3、
 * 画面の節は 14 項目まで膨れて低い窓で危うく、`ポスト処理` には絵の効果でないもの
 * （空間の明るさ・板の向き）が混ざっていた。軸を 3 つに引き直してある:
 *
 * | 軸 | 節 | 中身 |
 * | --- | --- | --- |
 * | どこに置くか | 画面・サブパネル | 窓・作り・パネルの配置 |
 * | どう描くか | 絵づくり（＋効果） | 画調・実体・明るさ・後処理 |
 * | どこから見るか | カメラ・一人称・VR | 視点そのもの |
 *
 * **`一人称の歩調` は VR の節から出した。**`fps_drives_movement()`（`hd2d_app.cpp`）は
 * `first_person.active` しか見ておらず、**平らな画面でも効く**——VR の中にしか
 * 置いていなかったのは誤りである。逆に `1 マスのメートル数` は本当に VR の中だけなので、
 * 一人称の節に置いたうえで**値の欄に「VR のときだけ」と出す**（`TronSky` と同じ流儀）。
 *
 * ## 言語を入口へ置いた理由
 * 前は `画面` の先頭にあった。読めない言語のときに**節をひとつ潜る**必要があり、
 * その 1 手が読めない字の中では重い。入口へ出すと 0 手で届く。
 * **入口は「節の一覧」だけではなくなった**（`root_items()`）。
 *
 * @note **開いた直後のカーソルは最初の節**（`root_item_count()`）に置く。言語の行に
 * 置くと、開いて → を押しただけで言語が変わる——いちばん触ってほしくない項目が
 * いちばん触りやすい所に来る。読めない人は ↑ 1 回（先頭へ回り込む）で届く。
 *
 * **「終了」はもう無い**（2026-08-11 に決めた「HD2D 用メニューの終了は削除して。
 * 通常の終了のみで終わらせる」）。終わらせる道はコアの通常の終了（^X ほか）だけにする。
 * ゲームを保存せずに窓ごと畳む口がメニューに並んでいると、通常の終了と取り違える。
 *
 * ## 割り当ての画面（2026-08-11 に決めた）
 * 「キーごとに操作を割り当てるのではなく、**操作ごとにキーを割り当てる**形に」。
 * 前は「キー割り当て（F1〜F12）」と「パッド（ボタン → コマンド）」の 2 枚だったものを、
 * **1 枚の表**にまとめてある。
 *
 * ```
 *   操作            キーボード      コントローラー
 *   拾う            G               X
 * ```
 *
 * - 行が操作・列がキーボード／コントローラー。**カーソルはマス（行 × 列）を指す**
 * - 項目は 60 を超えるので**画面からはみ出たらスクロール**する
 * - 決定で変更モード。**押したキー／ボタンがそのマスの割り当てになる**
 * - ESC で割り当てを消す（コアの既定のキーへ戻る）
 * - **15 秒未入力で変更モードを降りる**（押すつもりが無くなったときに詰まないように）
 *
 * 予約されているもの（**他の操作へ割り当てられない**）は `ui/key_binds.h` に書いてある。
 * この画面はその判定を**自分では持たない**（2 か所に置くと必ず食い違う）。
 *
 * ## 開いている間はコアへキーを流さない
 * 誤コマンド禁止（既存 UI の K-4 と同じ理由）。メニューの中で押した矢印が
 * ゲームの中で歩く動作になってはいけない。
 */
#pragma once

#include "ui/hd2d_settings.h"
#include "ui/ui_layout.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

class TextOverlay;
class UiPaint;

/*!
 * @brief サブパネルに映せるものの 1 件（コアが `sub_panel_kinds` で送ってくる。v1 §8.1）。
 * @details **番号と名前の対応はコアが持つ。**こちらで表を写すと版がずれるので、
 * 受け取ったものをそのまま並べる。先頭は必ず `flag = -1`（UI 既定）。
 */
struct SubPanelKindChoice {
    int flag{ -1 };
    std::string label_utf8;
};

//! メニューが受け取る入力（`CursorNav` と同じ考え方だが、こちらは取消も食う）。
enum class MenuNav {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Cancel,
};

class FeatureMenu {
public:
    bool is_open() const { return this->open_; }
    /*!
     * @brief コマンドの一覧を出しているか（2026-08-29 に決めた
     * 「コマンドメニュー背景にコアのマップが表示される。消して」）。
     * @details ここだけ**遊んでいる最中に開く**画なので、後ろの地図が透けると
     * 命令の名札と地の絵が重なって読みにくい。呼び手（`hd2d_app.cpp`）が
     * 描く前に幕を敷くために使う。設定の節はそのままにする——あちらは
     * 世界を見ながら値をいじる画なので、地図が見えているほうがよい。
     */
    bool showing_commands() const { return this->open_ && (this->page_ == Page::Commands); }
    void open(const Hd2dSettings &settings);
    void close();

    /*!
     * @brief キーを処理する。**開いている間は常に食う**（コアへ流さない）。
     * @param settings 触った結果をここへ書く。
     */
    void handle(MenuNav nav, Hd2dSettings &settings);

    /*!
     * @name 割り当ての変更モード
     *
     * @details 決定で入り、次に押したキー／ボタンがそのマスの割り当てになる。
     * **ESC は「取消」ではなく「割り当てを消す」**（2026-08-11 に決めた）。
     * 降りる道は「割り当てた」「消した」「15 秒経った」の 3 つで、
     * ESC が消去に取られているぶんを時間で補っている。
     * @{
     */
    /*!
     * @brief 変更モード中に押されたキーを受け取る。
     * @param keycode `SDL_Keycode`。
     * @param mods `kModShift` 等の論理和（`ui/key_binds.h`）。
     * @return 受け取ったら true（変更モードでなければ false）。
     * @details 予約キー（十字・Enter・ESC 以外は `key_is_reserved`）は受け取らず、
     * **変更モードのまま**にする（押し間違いで黙って降りない）。ESC だけは消去として食う。
     */
    bool handle_bind_key(int keycode, int mods, Hd2dSettings &settings);
    //! 変更モード中に押されたパッドのボタンを受け取る。固定のボタンは受け取らない。
    /*!
     * @brief 変更モード中のパッドの押しを割り当てにする。
     * @param mods 同時押しの層（`kPadModCtrl` 等）。**0 なら単独押し**。
     */
    bool handle_bind_pad(PadInput input, int mods, Hd2dSettings &settings);
    //! いま割り当ての入力を待っているか。
    bool waiting_for_key() const { return this->waiting_; }
    //! 変更モードのまま `now_ms` まで来た。**15 秒未入力なら降りる。**毎フレーム呼ぶこと。
    void update(std::uint32_t now_ms);
    /*! @} */

    /*!
     * @brief 実行してほしいコマンドの `id`。**読むと下りる**（1 回きり）。0 = 何も無い。
     *
     * @details 実行そのものはここではしない——`InputEventsMessage` を組んで送るのは
     * `hd2d_app` の仕事で、このパネルは「選ばれた」ことだけを伝える
     * （`take_vpad_edit_request()` と同じ形）。
     */
    int take_command_request()
    {
        const int id = this->command_requested_;
        this->command_requested_ = 0;
        return id;
    }

    //! **コマンドの画面を開いた状態で**開く（`kActionCommandMenu`）。
    void open_commands(const Hd2dSettings &settings);

    /*!
     * @brief いま並んでいる行の名札（`--ui-check` 用）。
     * @details **描くのと同じ `command_rows()` を通す**——検査のために組み直すと、
     * 画に出ている字と違うものを数えることになる（記憶
     * 「検査で描画手順を写さない」と同じ理由）。
     */
    std::vector<std::string> command_labels() const;

    /*!
     * @brief 「ボタン配置」の編集画面を開いてほしいか。**読むと下りる**（1 回きり）。
     * @details メニューはパッドの実体（`VirtualPad`）を知らないので、要求だけ立てて
     * `hd2d_app.cpp` に開かせる。メニューは開いたまま（編集画面が上に被さり、
     * 「戻る」で閉じるとこの画面へ戻ってくる——サブ画面の遷移になる）。
     */
    bool take_vpad_edit_request()
    {
        const bool requested = this->vpad_edit_requested_;
        this->vpad_edit_requested_ = false;
        return requested;
    }

    /*!
     * @brief **VR の入切**を要求されたか。**読むと下りる**（1 回きり）。
     * @details 2026-08-14 に決めた「ゲーム内から VR モードを起動できないか？」。
     * メニューは OpenXR のセッションを知らないので、要求だけ立てて `hd2d_app.cpp` に
     * 立てさせる／畳ませる（`VirtualPad` の配置編集と同じ作り）。
     */
    bool take_vr_toggle_request()
    {
        const bool requested = this->vr_toggle_requested_;
        this->vr_toggle_requested_ = false;
        return requested;
    }

    /*!
     * @brief 見出しに `label` を含む項目へカーソルを合わせる（**その分類へ入る**）。見つかれば true。
     * @details **検査が項目の番号を数えないため**にある。番号で数えていると、
     * 項目を 1 つ足しただけで検査が別の項目を触り、直したはずのものが落ちる（実際に落ちた）。
     */
    bool focus_item(const std::string &label);

    //! サブパネルに映せるものの一覧をコアから受け取る。空でも動く（番号だけ出す）。
    void set_sub_panel_kinds(const std::vector<SubPanelKindChoice> &kinds) { this->kind_choices_ = kinds; }
    /*!
     * @brief 2D アスキー地図で**いま枠に何マス入るか**を教える（値の欄に出すため）。
     * @details 字の大きさを回す人がいちばん知りたいのは px ではなく**マスの数**である
     * （本家の地図領域は 66×22 マスほど）。画面側が実寸を知っているので、毎フレーム渡す。
     */
    void set_ascii_grid(int cols, int rows)
    {
        this->ascii_cols_ = cols;
        this->ascii_rows_ = rows;
    }

    /*!
     * @brief いま画面が縦向きか（`hd2d_app.cpp` が毎フレーム渡す。`h > w`）。
     * @details 「画面の作り」と「ボタン配置」は**縦横で別々**（2026-08-12 に決めた）。
     * メニューは**いま向いている側だけ**を触る——見えている画面と違う側を書き換えると、
     * 「変えたのに何も起きない」になる。
     */
    void set_orientation(bool portrait) { this->portrait_ = portrait; }
    /*!
     * @brief **いま VR で動いているか**（`hd2d_app.cpp` が毎フレーム渡す）。
     * @details 「VR を始める／やめる」の表示と、値の説明の出し分けに使う。
     * **入口の「VR」は Windows なら常に並べる**——ゲーム内から始められるように
     * なった以上、平らな画面のときこそ入口が要る（2026-08-14 に決めた）。
     */
    void set_vr_available(bool on) { this->vr_available_ = on; }

    /*!
     * @brief パッドに割り当てられるコマンドの一覧と、いま繋がっているパッドを渡す。
     * @details 名前も**コアが送ってきたもの**を使う（こちらで表を持つと版がずれる）。
     * 繋がっていないことも画面に出す（**割り当てられるのに反応しない**のは分からない）。
     */
    void set_pad_state(const std::vector<PadCommand> &commands, bool connected, const std::string &name)
    {
        this->pad_commands_ = commands;
        this->pad_connected_ = connected;
        this->pad_name_ = name;
    }

    //! 描く。**`paint` を `text` より先に流すのは呼び出し側。**
    void draw(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings) const;

    /*!
     * @brief 字がパネルからはみ出していないか調べる。**収まっていれば空文字**。
     * @details `text.draw()` は入り切らない字を**黙って積まない**ので、溢れても
     * 落ちず、絵として切れるだけになる（英語にしたら `Full-screen 3D (Landsca` で
     * 途切れた。実機で見た様子 2026-08-15）。人が見るまで気づけないので、
     * 検査から突けるようにここへ出す。**いま選ばれている言語で**測る。
     */
    std::string overflow_report(TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings) const;
    /*!
     * @brief 画面の数（入口・分類・小節の**全部**）。
     * @details `Page` はこのクラスの内側の型なので**一覧そのものは出せない**。
     * 検査は「n 番目の画面を出す」ができればよいので、数と `focus_page()` を渡す。
     * **小節（効果・ミニマップ・出すパネル・ボタン表示設定）まで含める**——前は入口から
     * 入れる分類しか数えておらず、小節は誰も見ていなかった。
     */
    static int page_count();
    /*!
     * @brief `index` 番目の画面を出す（**入口も小節も含む**）。出せたら true。
     * @details 検査が ↓ の回数や決定の順を数えなくて済むようにある。数えていると、
     * 画面を 1 つ足しただけで検査が別の所を見に行く（`focus_item()` と同じ理由）。
     */
    bool focus_page(int index);
    /*!
     * @brief 分類の組み立てがおかしくないか調べる。**問題なければ空文字**。
     * @details `page_of()` に項目を書き忘れると、その項目は**どこにも並ばず**、
     * 元の節が空になる（実際に組み替えのときカメラの 5 つを落とした。2026-08-19）。
     * 画面は普通に描け、はみ出しの検査も画素の検査も通るので、**人が全部の節を
     * 開くまで誰も気づけない**。検査から突けるようにここへ出す。
     */
    static std::string structure_report();
    /*!
     * @brief パネルの大きさと置き場所。
     * @param text 中身を実測するのに使う。**渡さないと従来の 56 桁固定**になる
     * @param settings 値の欄を測るのに要る（項目の値は設定で変わる）
     * @details 幅を桁数で書くと言語を変えた途端に溢れる（`text_overlay.h` の注記）。
     * 実測できるときは実測する。割り当ての一覧（`Page::Binds`）は中身ではなく
     * **地図の枠**で決まるので、どちらでも同じ矩形が出る。
     *
     * **`draw()` と同じ所から作る**（描いたパネルと検査が見るパネルを食い違わせない。
     * 必守制約 3 の親戚）。「操作の割り当てが地図の枠に収まっているか」を
     * `--ui-check` が画素ではなくこの矩形で確かめられるようにするための手段である。
     */
    RectPx panel_rect(const UiLayout &layout, TextOverlay *text = nullptr,
        const Hd2dSettings *settings = nullptr) const;

    /*!
     * @brief 割り当ての一覧の 1 行。
     * @details `action == kActionNone` は**見出し**（カーソルが止まらない）。
     * 固定の操作（移動・決定・取消）は見出しの直後に、値だけ持つ行として並べる。
     */
    struct BindRow {
        int action{ kActionNone };
        std::string label;
        //! 固定行のときだけ中身がある（変えられないので設定を引かない）。
        std::string fixed_keyboard;
        std::string fixed_pad;
        //! 固定行か（見出しでもなく、選べもしない）。
        bool fixed{ false };
    };
    //! 一覧の行を組む。**画面と操作の両方がこれを見る**（並びが 2 か所にあると必ずずれる）。
    std::vector<BindRow> bind_rows() const;

private:
    /*!
     * @brief 分類（＝画面）。`Root` が入口。
     * @note **`Root` にも値の項目がある**（言語。`root_items()`）。入口は節の一覧だけ、
     * という前提でこの列挙を読まないこと。
     */
    enum class Page {
        Root,
        /*!
         * @brief **コマンド**（2026-08-22 に決めた）。コアの命令の一覧を並べて**実行する**。
         *
         * @details ここだけ**設定ではなく操作**である。入口の先頭に置くのは、
         * 触りだけの機体では**ここが全命令への唯一の API**だからである（SH-25 / SH-32）。
         *
         * 行は `Item` ではなく `CommandRow` で並ぶ（`Binds` と同じ理由——コアから
         * 届くので数が動く）。**画面はコア固有の知識を持たない**：並べるのも実行するのも
         * 届いた `pad_commands` が根拠で、変愚（63 件 9 分類。あちらの通常メニューの写し）も
         * Sil-Q（46 件）も同じ 1 本の道を通る。
         */
        Commands,
        Screen,
        /*!
         * @brief **絵づくり**（旧 `Post`。2026-08-19 の組み替え）。
         * @details 「後処理」だけの節ではなくなった——画調・実体・明るさもここにある。
         * 名前を替えたのは、`空間の明るさ`（環境光）と `板の向き`（一人称）が
         * 「ポスト処理」に混ざっていたのが誤りだったからである。
         */
        Look,
        //! 絵づくり ＞ 効果（入切 5 つ＋ぼけの強さ）。親は `Look`。
        Effects,
        Camera,
        /*!
         * @brief **一人称**（2026-08-19 に新設）。
         * @details カメラ 3・ポスト処理 1・VR 3 に散っていた設定をここへ集めた。
         * VR のときだけ効くもの（`FpsCellM` / `FpsPanelsEnter`）も**ここに置く**
         * ——行を出し入れするとカーソルが飛ぶので、効かないことは値の欄で言う。
         */
        FirstPerson,
        SubPanels,
        /*!
         * @brief **音**（2026-08-21 に決めた）。音楽・効果音の入切と音量。
         * @details **コアが `audio` を申告したときだけ**入口に並ぶ（`root_pages()`。
         * `set_core_supports_audio()`）——音を持たないコア（Sil-Q）で並べても、
         * 触った値の行き先が無い。「どこに置くか／どう描くか／どこから見るか」の
         * 3 軸のどれでもないので、独立した節にしてある。
         */
        Sound,
        Binds,
        /*!
         * @brief バーチャルパッド（2026-08-12 に決めた）。**入口に並ぶのは Android だけ**
         * （`root_pages()`。この実行体に無いものは並べない——Windows は cfg でだけ入る）。
         */
        VirtualPad,
        //! バーチャルパッド ＞ ボタン表示設定（14 個の入切）。親は `VirtualPad`。
        VpadButtons,
        /*!
         * @brief 画面 ＞ **ミニマップ**（2026-08-19 に決めた）。親は `Screen`。
         * @details 入口には並べない（`root_pages()` に入れない）——ミニマップは画面の一部で、
         * 分類としては `Screen` の下である。項目が 5 つあるので節にした
         * （`feature_menu.h` の「項目 1 つの節は作らない」の逆側の判断）。
         */
        Minimap,
        //! ゲーム進行。
        Realtime,
        /*!
         * @brief VR（2026-08-14 に決めた
         * 「VR モードの設定メニューが欲しい」）。
         * @details 2026-08-19 の組み替えで、**ここに残るのはジオラマと板だけ**になった
         * （一人称の設定は `FirstPerson` へ移した）。
         */
        Vr,
        //! 一人称 ＞ 出すパネル（部位ごとの入切）。親は `FirstPerson`（**旧 `Vr`**）。
        FpsPanels,
        Count,
    };
    /*!
     * @brief 入口に並べる節。**この実行体・このコアに無いものは並べない。**
     * @note `Realtime` はコアが `realtime` を申告したときだけ並ぶ
     */
    static std::vector<Page> root_pages();

    /*!
     * @brief 項目。**分類ごとにまとまっている**（並びがそのまま画面の並び）。
     * @note `page_items()` はこの列挙を頭から舐めて `page_of()` で振り分けるだけなので、
     * **ここでの並びが画面の並びである**。節を移すときは宣言ごと動かすこと。
     * @note `VpadBtnA`〜 と `VrPanelStatus`〜 は**差で番号を引く**（`vpad_button_index()` /
     * `vr_panel_index()`）ので、あの 2 つの塊だけは並びも連続も崩せない。
     */
    enum class Item {
        /*!
         * @brief 言語（`i18n/lang.h`）。**入口に直接置く**（2026-08-19）。
         * @details 前は `画面` の先頭だった。読めない言語のときに節をひとつ潜るのは、
         * 読めない字の中では重い 1 手である。切り替えは**即座に効く**。
         */
        Language,

        /* ---------------------------------------------------------- 画面 */
        WindowMode,
        Layout,
        MainPanelItem, //!< メインパネルの中身（3D ／ 2D アスキー。§6）
        AsciiPanelSize, //!< 　└ 地図の字の大きさ。上が 2D のときだけ効く
        /*!
         * @brief **状態列の位置**。
         * @details `自動 / 左 / 右`。自動はコアの申告（変愚・短愚・幻想・Sil-Q は左、
         * Frox は右）に従い、左右を選んだら人の選択が勝つ。**5 本すべてのコアで効く**。
         */
        StatusColSideItem,
        Subs,
        MinimapEnter, //!< ミニマップの詳細へ（決定でサブ画面。2026-08-19 に決めた）

        /*!
         * @name 画面 ＞ ミニマップ（`Page::Minimap`）。2026-08-19 に決めた
         * @details 並びがそのまま画面の並び。指示の順（位置・サイズ・縮尺・濃度・方向表示）に揃える。
         * @{
         */
        MinimapCornerItem,
        MinimapSize,
        MinimapCellPx,
        MinimapOpacity,
        MinimapRelative,
        /*! @} */

        /* ------------------------------------------------------ 絵づくり */
        /*!
         * @brief **画調**。
         * @details この節の先頭に置く。以下 2 つはその子で、TRON のときだけ効く。
         */
        SceneLookItem,
        TronSky, //!< 　├ TRON のときの空（真っ黒／時刻に応じる。2026-08-19 に決めた）
        TronFaceGlyph, //!< 　└ ブロックの面の記号
        /*!
         * @brief **面の汚し**（`hd2d/render/surface_wear.h`。2026-08-23 に決めた）。
         * @details 画調の子ではない——**標準でも TRON でも効く**ので、画調の下に並べつつ
         * 値の欄に「TRON のときだけ」とは書かない。
         */
        SurfaceWear,
        /*!
         * @brief **木の葉**（`hd2d/render/leaf_detail.h`。2026-08-23 に決めた）。
         * @details 葉の面にだけ効く。汚しの隣に置く（どちらも面の描き込みの軸）。
         */
        LeafDetail,
        EntityStyleItem, //!< 実体の表現
        EntityGlyphSize, //!< 　└ 実体の字の大きさ。上がアスキーのときだけ効く
        /*!
         * @brief **タイルが無い実体を字で描く**（
         * 決めたこと F5）。板の道で目録に無い実体を `ascii_fallback` の字の板へ落とす。
         * 既定 入。切る手段を残すのは、既存 4 本の絵を変えうる改修だから。
         */
        GlyphFallback,
        Backdrops,
        DungeonLight, //!< 空間の明るさ（**環境光の倍率**。2026-08-15 に決めた）
        EffectsEnter, //!< 効果へ（決定でサブ画面。2026-08-19）

        /*!
         * @name 絵づくり ＞ 効果（`Page::Effects`）
         * @details 入切 5 つ＋ぼけの強さ。**節へ落としたのは数のため**——絵づくりに
         * 平らに並べると 12 行になり、低い窓で下が見切れる（この画面を階層にした元の理由）。
         * @{
         */
        PostFog,
        PostDof,
        PostDofStrength, //!< 　└ 被写界深度の強さ（P10 レビュー 4。0 〜 2.4）
        PostBloom,
        PostGrade,
        PostVignette,
        PostVignetteStrength, //!< 　└ ビネットの強さ（2026-08-23 に決めた。0 〜 0.9）
        PostSepia, //!< セピア調（0 〜 1。LUT へ焼く）
        PostHdr, //!< HDR 風（影が開き・白飛びが戻り・色が締まる。**表示機の HDR 出力ではない**）
        PostExposure, //!< 露出（トーンマップの係数。既定 1.25 ＝ 従来）
        /*! @} */

        /* -------------------------------------------------------- カメラ */
        CameraPitch,
        CameraFov,
        CameraCellPx,
        MoveSmooth, //!< 移動のなめらかさ（P10 レビュー 5）
        Cutaway, //!< 手前の遮蔽を抜く半径。**カメラの話**なので絵の効果からここへ移した

        /*!
         * @name 一人称（`Page::FirstPerson`）。2026-08-19 に 3 つの節から集めた
         * @{
         */
        FpsVerticalLook, //!< 上下視点（2026-08-11 に決めた）
        FpsWallUpper, //!< ダンジョンの壁 2 段目（同 2026-08-11）
        FpsCeiling, //!< ダンジョンの天井（同）
        FpsSlabTurn, //!< 板の向き（自由／8 方向。同 2026-08-15）
        /*!
         * @brief 歩調（1 歩の間隔）。**平らな画面でも効く。**
         * @details 旧 `VrFpsStep`。VR の節にあったが、`fps_drives_movement()` は
         * `first_person.active` しか見ていない——VR の中だけの設定ではなかった。
         */
        FpsStep,
        //! 1 マスのメートル数（自動＝身長から）。**VR のときだけ効く**ので値の欄でそう言う。
        FpsCellM,
        FpsPanelsEnter, //!< 出すパネルへ（決定でサブ画面）。同じく VR のときだけ効く
        /*! @} */

        //! 一人称で出す部位の入切。**並びは `xr::PanelSlot` と同じ**（差で引く）。
        VrPanelStatus,
        VrPanelSub1,
        VrPanelSub2,
        VrPanelSub3,
        VrPanelSub4,
        VrPanelSub5,
        VrPanelMinimap,
        VrPanelMessage,
        VrPanelPrompt,
        VrPanelBottom,

        /*!
         * @name サブパネル（`Page::SubPanels`）。枚数は2026-08-19 に決めた
         * @details 下段 2〜4 枚・右列 1〜3 枚。**行は枚数で増減させない**——
         * いま効かない行は値の欄で「下段 4 枚のときだけ」と言う（`TronSky` と同じ流儀）。
         * 出し入れすると、枚数を変えた瞬間に下の行がずれてカーソルの居場所が飛ぶ。
         * @{
         */
        SubBottomCount,
        SubRightCount,
        SubBottomW1, //!< 　└ 下段 1 の幅（＝ 1 本目の仕切りの位置）
        SubBottomW2,
        SubBottomW3, //!< 下段が 4 枚のときだけ効く
        SubRightH1, //!< 　└ 右列 1 の高さ
        SubRightH2, //!< 右列が 3 枚のときだけ効く
        //! 中身。**添字は場所で固定**（`kSubBottomSlot` / `kSubRightSlot` と同じ並び）。
        SubKind1,
        SubKind2,
        SubKind3,
        SubKind4,
        SubKind5,
        SubKind6,
        SubKind7,
        /*! @} */

        /*!
         * @name 音（`Page::Sound`）。2026-08-21 に決めた
         * @details 並びは指示のとおり「音楽 → 音楽の音量 → 効果音 → 効果音の音量」。
         * 音量の行は**その上の入切の子**なので、入切のすぐ下に置く。
         * @{
         */
        BgmModeItem, //!< BGM に何を流すか
        MusicVolume,
        SoundEnabled,
        SoundVolume,
        /*! @} */

        /* ------------------------------------------ バーチャルパッド */
        VpadShow, //!< バーチャルパッド表示（2026-08-12 に決めた。既定は入）
        VpadOpacity, //!< 表示濃度（同 2026-08-12。25%〜200%）
        VpadButtonsEnter, //!< ボタン表示設定へ（決定でサブ画面へ入る）
        VpadLayoutEdit, //!< ボタン配置（決定で編集画面を要求する。開くのは `hd2d_app.cpp`）
        VpadResetAll, //!< デフォルトに戻す（表示・ボタン表示・配置の全部）

        //! ボタンごとの入切。**並びは `VpadControl` と同じ**（`vpad_button_index()` が差で引く）。
        VpadBtnA,
        VpadBtnB,
        VpadBtnX,
        VpadBtnY,
        VpadBtnR1,
        VpadBtnR2,
        VpadBtnR3,
        VpadBtnL1,
        VpadBtnL2,
        VpadBtnL3,
        VpadBtnStart,
        VpadBtnSelect,
        VpadBtnRS,
        VpadBtnLS,

        //! ゲーム進行。**設定はコアへ送って初めて効く**。
        RealtimeEnabled,
        RealtimeSpeed,
        DamageFlash, //!< 被弾の全面フラッシュ（属性で色が変わる）
        DamageShake, //!< 被弾の画面の揺れ（酔う人向けに別々）
        RealtimePrompt, //!< 小窓（持ち物・足元）の裏でも世界を進めるか
        RealtimeSelfSpan, //!< 速さの振れ幅（はみ出た分は世界の拍へ）

        /*!
         * @name VR（§22）。**実機で詰める値**なので、cfg だけでなくメニューからも回せる
         * @details 一人称の 3 つは `FirstPerson` へ移した（2026-08-19）。ここはジオラマと板だけ。
         * @{
         */
        VrEnter, //!< **VR を始める／やめる**（2026-08-14 に決めた）
        VrTileM, //!< ジオラマの拡大率（1 マスのメートル数）
        VrTableHeight, //!< ジオラマの視点高さ（天板の高さ。自動＝頭の高さから）
        VrPanelSize, //!< 板の字の大きさ（1 桁の見込み角）
        VrPanelLift, //!< 板を見上げる角
        VrPanelFollow, //!< 板が付いてくるまでの遊びの角
        /*!
         * @brief 目の描き先の倍率。
         * @details **効くのは次回起動から**——swapchain はセッションを立てる瞬間にしか
         * 作られず、作り直す手段が無い。だから行の値に「次回起動から」と書く
         * （書かないと「回しても何も変わらない」に見える）。
         */
        VrRenderScale,
        /*! @} */

        Count,
    };

    static const char *page_label(Page page);
    static Page page_of(Item item);
    static const char *item_label(Item item);
    std::string item_value(Item item, const Hd2dSettings &settings) const;
    //! 左右で値を動かす（`delta` は -1 / +1）。動かせない項目は何もしない。
    void adjust(Item item, int delta, Hd2dSettings &settings) const;
    //! `Item::SubKind1`〜`SubKind7` なら 0〜6、そうでなければ -1。
    static int sub_kind_index(Item item);
    //! `Item::SubBottomW1`〜`SubBottomW3` なら 0〜2、そうでなければ -1。
    static int sub_bottom_divider_index(Item item);
    //! `Item::SubRightH1`〜`SubRightH2` なら 0〜1、そうでなければ -1。
    static int sub_right_divider_index(Item item);
    //! `Item::VpadBtnA`〜`VpadBtnLS` なら `VpadControl` の番号、そうでなければ -1。
    static int vpad_button_index(Item item);
    //! `Item::VrPanelStatus`〜`VrPanelBottom` なら `xr::PanelSlot` の番号、そうでなければ -1。
    static int vr_panel_index(Item item);

    /*!
     * @brief `page` に並ぶ項目。**この実行体に無いもの（窓モード・VR の入切）はここで落とす。**
     * @details 落とす判定を呼ぶ側ごとに書くと必ず食い違うので、1 か所にしてある。
     */
    static std::vector<Item> items_of(Page page);
    //! この実行体に**無い**項目か（窓モード・VR の入切）。判定はここ 1 か所だけ。
    static bool item_is_absent(Item item);
    /*!
     * @brief 小節と、その親・入口の項目の対。
     * @details **遷移（取消で親へ戻る）も検査もこの表を見る。**同じ対を 2 か所に書くと、
     * 小節を足したときに片方を書き忘れて「戻ると入口まで飛ぶ」が起きる。
     */
    struct SubPageLink {
        Page child;
        Page parent;
        Item enter;
    };
    static const std::vector<SubPageLink> &sub_page_links();
    //! いまの画面に並ぶ項目。
    std::vector<Item> page_items() const;
    /*!
     * @brief 入口で分類より**上**に並ぶ値の項目（いまは言語だけ）。
     * @details 中身は「`page_of()` が `Page::Root` を返す項目」そのもの。並びを 2 か所に
     * 持つと必ずずれるので、`items_of(Page::Root)` を返すだけにしてある。
     */
    static std::vector<Item> root_items();
    /*!
     * @brief 入口で分類より上に並ぶ**値の項目**の数（いまは言語の 1 つ）。
     * @details 入口のカーソルは「項目 → 分類」の順に並ぶので、`index` 番目の分類は
     * `root_item_count() + index` 行目にある。
     */
    static int root_item_count();

    /*!
     * @name コマンドの一覧（`Page::Commands`）
     * @{
     */
    /*!
     * @brief 一覧の 1 行。**分類の行**か、**小分類の行**か、実行できる命令か、いまは出せない命令。
     *
     * @details **変愚の通常メニューと同じで階層になっている**（2026-08-22 に決めた
     * 「コマンドメニューは変愚のメニューのように各グループ毎に階層化して」）。
     * 1 段目は分類（`group` が入る）、2 段目はその分類の命令（`id` が入る）。
     * 63 件を 1 枚に並べると送りっぱなしになって、どこに何があるか覚えられない。
     *
     * **2026-08-23 に 3 段目を足した**（こう決めた——「行動メニューをグループ化し
     * もう 1 階層深くして」）。分類の中で `subgroup_utf8` が届いている命令は、
     * その名前の行（`subgroup` が入る）へ束ねて、潜った先に並べる。
     * 段の数は**コアが決める**：小分類を送らない分類は今までどおり 2 段のままである。
     */
    struct CommandRow {
        int id{ 0 }; //!< コアの命令の `id`。分類・小分類の行は 0
        std::string label;
        //! 分類の行なら、その分類名（`group_utf8`）。空なら分類の行ではない。
        std::string group;
        //! 小分類の行なら、その小分類名（`subgroup_utf8`）。空なら小分類の行ではない。
        std::string subgroup;
        bool heading{ false }; //!< 選べない案内の行（一覧が届いていないとき）
        //! **いまのキー配列で出せるか。** 偽の行は選べない（押しても何も起きない道を作らない）。
        bool available{ true };
    };
    /*!
     * @brief いまの段の行。
     * @details `command_group_` が空なら**分類の一覧**（1 段目）。分類だけ決まっていれば
     * その中身（2 段目。小分類の行と、小分類を持たない命令が混ざる）。
     * `command_subgroup_` まで決まっていればその小分類の命令（3 段目）。
     */
    std::vector<CommandRow> command_rows() const;
    //! `from` から `dir` 向きに、カーソルが止まれる行を探す。無ければ `from`。
    int next_command_row(const std::vector<CommandRow> &rows, int from, int dir) const;
    /*!
     * @brief 2 列の一覧で**縦**に動く（`from` から `dir` 向きに 2 つずつ）。
     * @details 添字は**行優先**（`i / 2` が段・`i % 2` が列。変愚の
     * `input-key-requester.cpp:401` と同じ並べ方）なので、↓ は +2 である。
     * 止まれないマスに当たったらさらに 2 つ進む。1 周しても無ければ `from`。
     */
    int next_command_row_vertical(const std::vector<CommandRow> &rows, int from, int dir) const;
    //! 2 列の一覧で**横**に動く（同じ段の隣へ。止まれなければ動かない）。変愚と同じ振る舞い。
    int next_command_row_horizontal(const std::vector<CommandRow> &rows, int from, int dir) const;
    //! 一覧を描く。`top_y` は見出しの下（`draw()` が場所を決めてから渡す）。
    void draw_commands(UiPaint &paint, TextOverlay &text, const UiLayout &layout,
        const RectPx &box, int top_y) const;
    /*! @} */

    /*!
     * @name 割り当ての一覧（`Page::Binds`）
     * @{
     */
    //! `from` から `dir` 向きに、カーソルが止まれる行を探す。無ければ `from`。
    int next_bindable_row(const std::vector<BindRow> &rows, int from, int dir) const;
    //! マスの中身（`col` は 0 = キーボード / 1 = コントローラー）。
    std::string bind_cell_value(const BindRow &row, int col, const Hd2dSettings &settings) const;
    //! この頁の中身が要る幅（画素）。**言語で字数も字幅も変わるので実測する。**
    int content_width(TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings) const;
    //! 一覧を描く。`top_y` は見出しの下（`draw()` が場所を決めてから渡す）。
    void draw_binds(UiPaint &paint, TextOverlay &text, const UiLayout &layout, const Hd2dSettings &settings,
        const RectPx &box, int top_y) const;
    //! 変更モードを降りる（割り当てても、消しても、時間切れでも通る 1 か所）。
    void stop_waiting();
    /*! @} */

    std::vector<SubPanelKindChoice> kind_choices_;
    //! 2D アスキー地図で枠に入るマスの数（`set_ascii_grid`）。0 なら分からない。
    int ascii_cols_{ 0 };
    int ascii_rows_{ 0 };
    std::vector<PadCommand> pad_commands_;
    bool pad_connected_{ false };
    std::string pad_name_;

    bool open_{ false };
    Page page_{ Page::Root };
    //! いまの画面でのカーソル位置。**画面を移るたびに 0 へ戻す。**
    int cursor_{ 0 };
    //! 入口へ戻ったときに、さっき入った分類を選んだ状態にしておくための覚え。
    int root_cursor_{ 0 };
    //! 割り当ての一覧での列（0 = キーボード / 1 = コントローラー）。
    int bind_col_{ 0 };
    /*!
     * @brief 一覧の先頭に出している行。
     * @details **`draw()` が窓の高さを知っている唯一の場所**なので、そこで詰め直す。
     * 高さを `handle()` にも持たせると、窓を変えたときに片方だけ古い値で動く。
     */
    mutable int bind_scroll_{ 0 };
    //! `draw()` が実測した「一度に出る行数」。上下のページ送りが使う。
    mutable int bind_visible_rows_{ 1 };

    /*!
     * @name コマンドの一覧の状態（`Page::Commands`）
     * @details 送りの持ち方は `Binds` と同じ（`draw()` だけが窓の高さを知っている）。
     * @{
     */
    mutable int command_scroll_{ 0 };
    mutable int command_visible_rows_{ 1 };
    int command_requested_{ 0 };
    /*!
     * @brief いま開いている分類。**空なら分類の一覧を出している**（2 段のどちらか）。
     * @details 番号ではなく**名前**で持つ。コアの一覧は言語や版で並びが動くので、
     * 番号で覚えると別の分類が開く（`PadSeed::id` を据えたのと同じ理由）。
     */
    std::string command_group_;
    //! 分類の一覧へ戻ったときに、さっき入った分類を選んだ状態にしておくための覚え。
    int command_group_cursor_{ 0 };
    /*!
     * @brief いま開いている**小分類**（3 段目。2026-08-23）。空なら 2 段目までである。
     * @details `command_group_` と同じ理由で**名前**で持つ。
     */
    std::string command_subgroup_;
    //! 小分類から戻ったときの覚え（`command_group_cursor_` と同じ役目）。
    int command_subgroup_cursor_{ 0 };
    /*!
     * @brief **ボタン 1 つで直に開いたか**（`open_commands()`）。
     *
     * @details 取消で行く先がこれで変わる。直に開いたなら**パネルごと閉じる**——
     * 入口（機能メニュー）へ落とすと、コマンドを取り消しただけなのに
     * **設定の画面が立ち上がったように見える**（2026-08-23 に気づいた）。
     * 機能メニューから潜って来たときだけ入口へ戻す。
     */
    bool commands_direct_{ false };
    /*! @} */

    bool vpad_edit_requested_{ false };
    //! いま縦向きか（`set_orientation`）。**覚えるだけで、決めるのは呼び出し側**。
    bool portrait_{ false };
    //! いま VR で動いているか（`set_vr_available`）。表示の出し分けにだけ使う。
    bool vr_available_{ false };
    //! VR の入切を要求されたか（`take_vr_toggle_request`）。
    bool vr_toggle_requested_{ false };

    bool waiting_{ false };
    int wait_action_{ kActionNone }; //!< 変更モードで狙っている操作
    int wait_col_{ 0 }; //!< 変更モードで狙っている列
    std::uint32_t wait_since_ms_{ 0 };
    std::uint32_t wait_now_ms_{ 0 }; //!< `update()` が入れる「いま」。残り秒の表示に使う
    /*!
     * @brief 変更モードで**受け取ったのに割り当てなかった**押しの説明（2026-08-14 に気づいた）。
     *
     * @details 「割り当てで同時押しが入らない」の相談は、**押しが届いていない**のか
     * **届いたのに捨てた**のかで原因がまったく違うのに、画面には何も出ていなかった
     * （キーボードの列を狙っている最中のパッドの押しは黙って捨てている）。
     * 受け取ったものと、なぜ入らなかったかを出す。**画面だけで切り分けられるようにする**
     * ——実機しか持っていない人に「logcat を見て」とは言えない。
     */
    std::string wait_note_;
};

//! 変更モードを降りるまでの未入力時間（ミリ秒）。「15 秒未入力でキャンセル」と決めた。
inline constexpr std::uint32_t kBindWaitTimeoutMs = 15000;

/*!
 * @name コアが持っている機能に合わせて節を出し分ける
 *
 * @details リアルタイム進行は**変愚コアだけ**の機能で、そちらは `hello_ack.features` に
 * `"realtime"` を載せる（v1 §3.1 の互換規則内の追加）。名乗らないコアで
 * 「ゲーム進行」の節を出しても、触った値がどこにも効かない。
 *
 * **画面側にコア名の分岐は書かない**（§1 制約 1）。見るのは申告だけである。
 * 既定を真にしてあるのは、コアを起こさない検査のモード（`--ui-check` ほか）で
 * 節が消えないようにするため——あちらは握手を通らないので、ここは既定のまま走る。
 * `root_pages()` が static なので、`set_vr_available()` のような実体の欄ではなく
 * `ui_paint.h` の `set_panel_transparent()` と同じ形（自由関数）にしてある。
 * @{
 */
void set_core_supports_realtime(bool on);
bool core_supports_realtime();
/*!
 * @brief コアが `audio` を申告したか（音を鳴らせるか）。**偽なら「音」の節を出さない。**
 * @details 変愚・短愚蛮怒（`platform/windows/core_main.cpp`）と幻想蛮怒
 * （`gensoband/adapter/gb_main.cpp`）は鳴らせる。Sil-Q は音の層を持たない。
 * `realtime` と同じ扱いで、既定は真——コアを起こさない検査（`--ui-check`）は
 * 握手を通らないので、ここが偽だと節ごと検査から消える。
 */
void set_core_supports_audio(bool on);
bool core_supports_audio();
/*! @} */

} // namespace hd2d
