/*!
 * @file protocol_messages.h
 * @brief プロトコル v1 の制御メッセージ（hello / hello_ack / fatal / exit）の encode/decode。
 *
 * 基準はプロトコル v1 の §3（握手）と §4（カタログ）。
 * 実装計画は分割の第 4 段の §2。
 *
 * ## 何をここに置き、何を置かないか
 * - `frame` は `frame_codec.h`（`GameFrame` を知る必要があるので別 TU）。
 * - `ui_state` / `keys` / `quit_request` / `pad_commands` / `sub_panel_kinds` は
 *   **Step 4b で追加した**（v1 §7・§4.1・§8・§8.1）。
 * - `exit` は 4a のスタブ core が正常終了を伝えるのに要るので今から入れる。
 *
 * ## 型を借りてこない理由（4b）
 * `pad_commands` は `frame/pad_command_table.h` の `PadCommandEntry` を、
 * `sub_panel_kinds` は `frame/sub_panel_kinds.h` の `SubPanelKindEntry` を
 * そのまま運びたくなるが、**どちらも借りない**。前者は `const char *` を持つ
 * （寿命の持ち主がコア側にしか無い）ためワイヤ型として不適で、後者は
 * 実装がコア側 TU にあるため ui 側から見ると宣言だけの空箱になる。
 * ここでは値型のワイヤ表現を自前に持ち、変換は送信側・受信側が各々で行う。
 *
 * ## 中立性
 * 依存は `<nlohmann/json.hpp>` だけ（`frame_codec.h` 冒頭と同じ規約）。
 * core 側と ui 側の**両方**がこの TU をリンクする。
 *
 * ## 正準形について
 * 制御メッセージは 1 接続あたり数個しか流れず、`frame` のような合体判定（v1 §6.4）が
 * 無いので、**既定値の省略規則は課さない**。ただし空の `features` / 空の `asset_roots` は
 * 書かない（相手が「申告なし」と「空の申告」を区別する必要がないため）。
 */
#pragma once

#include <string>
#include <vector>

namespace presentation {

//! v1 のプロトコル大版（v1 §3.1 の `protocol`）。**一致必須**。
inline constexpr int kProtocolVersion = 1;

//! ui → core（v1 §3.1）。
struct HelloMessage {
    int protocol{ kProtocolVersion };
    std::string ui_name;
    std::string ui_version;
    std::vector<std::string> features;
};

//! core → ui（v1 §3.1）。core が最初に書くメッセージ。
struct HelloAckMessage {
    int protocol{ kProtocolVersion };
    std::string core_name;
    std::string core_version;
    std::vector<std::string> features;
    /*!
     * @brief `asset_roots.graf`（8px/16px タイル面の在り処）の絶対パス。
     * @details 空文字列 = 申告なし（キーごと省略される）。8/16px スタイル非対応の
     * コアは省略してよく、ui は該当スタイルを無効化する（v1 §3.1）。
     * v1 の `asset_roots` は `graf` しか持たない。増えたらここにメンバを足す
     * （未知キーは decode 側が黙って捨てる＝v1 §2.3）。
     */
    std::string asset_root_graf;
    /*!
     * @brief `asset_roots.slab`（ボクセル板の在り処）の絶対パス。
     * @details で足したキー（**追加だけ**なので
     * v1 の互換規則内。版は上げない）。空文字列 = 申告なしで、キーごと省略される。
     * 申告するのは幻想蛮怒コアだけ（`<exe_dir>/gensoband/assets/slab`）。
     * **変愚コアは申告しない＝ui の板探索は完全に従来どおり**である。
     * ui 側（`SlabLibrary`）は［申告された置き場 → 既定の置き場］の 2 段で探す。
     */
    std::string asset_root_slab;
};

//! core → ui（v1 §4.2）。送信後にプロセスを終える。
struct FatalMessage {
    std::string reason;
};

//! core → ui（v1 §4.2）。正常終了の直前に 1 回。
struct ExitMessage {
    int code{ 0 };
};

/*!
 * @brief `ui_state` / `pad_commands` が扱うサブパネルの枚数（`kSubPanelCount` と同じ 7）。
 * @details **2026-08-19 に 5 → 7。版は上げない**（v1 §3.2 の「追加だけ」）。
 * 配列を伸ばしただけで、古い相手とも噛み合う——読む側は
 * `index >= kProtocolSubPanelCount` で打ち切るので、7 枚送っても古いコアは 5 枚で捨て、
 * 5 枚しか来なくても新しいコアの 6・7 枚目は -1（UI 既定）のまま残る。
 */
inline constexpr int kProtocolSubPanelCount = 7;

//! `ui_state.sub_panel_cells[i]`。0 は「申告なし」（core は何もしない）。
struct UiStatePanelCells {
    int cols{ 0 };
    int rows{ 0 };
};

/*!
 * @brief ui → core（v1 §7）。**push-latest**。core は最新の 1 個だけ覚えればよい。
 * @details 既定値は「ui がそのキーを送ってこなかったとき core が使う値」。
 * `view_cells` と `sub_panel_cells` だけは 0 を**申告なし**として扱う
 * （0 桁の Term や 0 マスの視界は意味を持たないので、既定値と欠落を区別する必要が無い）。
 */
struct UiStateMessage {
    //! `Bridge::set_view_size` 相当。0 なら適用しない。
    int view_w{ 0 };
    int view_h{ 0 };
    /*!
     * @brief `Bridge::set_camera_follow_player` 相当。
     * @details v1 §7 の表には無いキー（`camera_follow_player`）。現行の
     * 合成ルートの `before_capture` は `set_view_size` と対で
     * `set_camera_follow_player(app.camera_follows_player())` を呼んでおり、
     * これを落とすと分割版のカメラ挙動が変わる（HD2D 経路でフロア端の丸めが
     * 復活してプレイヤが画面内を泳ぐ）。**追加だけの変更なので版は上げない**（v1 §3.2）。
     * 既定 false は `Bridge::camera_follow_player_` の初期値と同じ。
     */
    bool camera_follow_player{ false };
    //! `presentation::set_sub_panel_term_cells` 相当（v1 §7）。
    UiStatePanelCells sub_panel_cells[kProtocolSubPanelCount]{};
    //! `SdlUiOptions::cursor_mode_enabled`。
    bool cursor_mode{ true };
    /*!
     * @brief `SdlUiOptions::sub_panel_kind[7]`。**-1 = UI 既定**。
     * @details キーごと省略されたときに「全部 UI 既定」で上書きしてしまわないよう、
     * 受け取ったかどうかを `has_sub_panel_kinds` で持つ。
     */
    int sub_panel_kinds[kProtocolSubPanelCount]{ -1, -1, -1, -1, -1, -1, -1 };
    bool has_sub_panel_kinds{ false };
    //! `map_style`（v1 §7）。8/16px のときだけ非 0 ／ `"new"` `"old"`。
    int map_style_graf_px{ 0 };
    std::string map_style_graf_tag{ "ascii" };
    //! `audio`。v1 では音は core 側で鳴る（§1.2）。
    bool sound_on{ true };
    bool music_on{ true };
    int sound_volume_index{ 0 };
    int music_volume_index{ 0 };
    /*!
     * @brief **効果音を画面側が鳴らす**。
     * @details 真なら、コアは鳴らさずに `frame.sounds` へ出来事を載せる。
     * 偽なら従来どおりコアが自分で鳴らす。**移行の途中でも二重に鳴らないための旗**で、
     * `sound_on` とは別である（`sound_on` は「コアが鳴らすか」）。
     */
    bool sound_events{ false };
    //! `bot_json`。core 側のコマンドライン／環境変数指定が優先される（v1 §7 の注）。
    bool bot_json_enabled{ false };
    std::string bot_json_path;
    /*!
     * @brief `lang`。**コア側の言語**（実行時多言語化）。`"ja"` / `"en"`。
     * @details 画面側の言語は元から画面側だけで完結していたが、コアの文言も実行時に
     * 切り替わるようになったので、同じつまみで動かすための道である。
     * **空なら触らない**——コアの既定（環境変数 `HENGBAND_LANG`）をそのまま生かす。
     * 綴りは画面側の `hd2d.cfg` の `lang=` と揃えてある。
     * **追加だけの変更なので版は上げない**（v1 §3.2）。
     */
    std::string lang;
    /*!
     * @name `realtime`
     * @details 実時間の時計を回すのは**コア側**（`RealtimeClock`）で、設定を持つのは
     * **画面側**（`hd2d.cfg`）。ここが両者をつなぐ唯一の道である。
     * **追加だけの変更なので版は上げない**（v1 §3.2）。既定は `RealtimeClock` の初期値と同じ。
     * @{
     */
    bool realtime_enabled{ false };
    //! 1 行動あたりの秒数の表への添字（`Hd2dSettings::realtime_seconds_per_turn`）。
    int realtime_speed_index{ 4 };
    //! 小窓の裏でも世界を進めるか（利用者の好み。設計書 §7）。
    bool realtime_prompt_live{ true };
    //! 速さの振れ幅の段（設計書 §4-2）。
    int realtime_self_span_index{ 1 };
    /*! @} */
};

/*!
 * @brief ui → core（v1 §4.1）。翻訳済みキーコード列。順序保存。
 * @details 値域は 1〜255。`int` で運ぶのは
 * `KeyQueue`（`frame/ui_seam.h`）が `std::vector<int>` だからで、
 * 範囲外の値は decode 側が捨てる（0 は「キーなし」、256 以上はコア側で
 * `char` に詰められて別のキーに化けるため、黙って通してはいけない）。
 */
struct KeysMessage {
    std::vector<int> keys;
};

//! ui → core（v1 §4.1）。ペイロードは空。
struct QuitRequestMessage {
};

/*!
 * @brief `pad_commands.entries[]` 1 件（v1 §8）。
 * @details `frame/pad_command_table.h` の `PadCommandEntry` に
 * 「オリジナル配列／ローグライク配列それぞれの解決結果」を足したもの。
 * **空配列 = 解決不能**（現行 K-2 の規則を値として運ぶ）。
 */
struct PadCommandWireEntry {
    int id{ 0 };
    int command{ 0 };
    std::string group_utf8;
    /*!
     * @brief 分類の中の**小分類**（v1 の追補。2026-08-23。**空なら小分類は無い**）。
     * @details コマンドメニューをもう 1 段深くするために足した（決めたこと
     * 「行動メニューをグループ化しもう 1 階層深くして」）。Sil-Q の `Action` は
     * 23 件あって、分類へ潜っても 1 枚に収まらなかった。
     *
     * **送らないコアの画は 1 行も変わらない。**画面側は「その分類に小分類が 1 つでも
     * あるか」で段の数を決めるので（`hd2d/ui/feature_menu.cpp` の `command_rows()`）、
     * 空のまま届く変愚蛮怒・幻想蛮怒は今までどおり 2 段のままである。
     *
     * 同じ分類の中で**混ざってもよい**（小分類のある命令は潜った先、無い命令は
     * その場に並ぶ）。並びはコアが送ってきた順のまま。
     */
    std::string subgroup_utf8;
    std::string label_utf8;
    std::vector<int> seq_original;
    std::vector<int> seq_rogue;
};

//! core → ui（v1 §8）。初期化完了時に 1 回＋キー配列切替時に再送。
struct PadCommandsMessage {
    //! `"original"` | `"rogue"`（コアの `rogue_like_commands`）。
    std::string current_keymap{ "original" };
    std::vector<PadCommandWireEntry> entries;
};

/*!
 * @brief core → ui。いま登録されているマクロのトリガー（v1 §8.4）。
 * @details **ui が「このボタンにマクロが乗っているか」を知るための唯一の道**である。
 * どのバイト列がどのボタンかを知っているのは ui の側（`hd2d/ui/game_pad.cpp` の
 * `pad_input_trigger_sequence`）なので、コアは**登録されている列をそのまま並べる**だけで、
 * パッドのことは何も知らない。この分け方だと、ボタンの符号を変えてもコアは触らずに済む。
 * @note `pad_commands` と同時に 1 回＋**マクロが増減したら再送**。
 * トリガーは短いので（F キーやパッドで 5 バイト）そのまま運ぶ。
 */
struct MacroTriggersMessage {
    //! 1 件 = 1 つのトリガーのバイト列（1〜255）。
    std::vector<std::vector<int>> patterns;
};

//! `sub_panel_kinds.entries[]` 1 件（v1 §8.1）。先頭は必ず `flag = -1`。
struct SubPanelKindWireEntry {
    int flag{ -1 };
    std::string label_utf8;
};

//! core → ui（v1 §8.1）。pad_commands と同時に 1 回。
struct SubPanelKindsMessage {
    std::vector<SubPanelKindWireEntry> entries;
};

/*!
 * @brief `asset_manifest.assets[]` 1 件（v1 §8.2）。
 * @details `official_tile_table.h` の `OfficialTileEntry` のワイヤ表現。`kind` を
 * 1 文字の文字列で運ぶのは JSON に char が無いため（decode 側は先頭 1 文字だけ見る）。
 */
struct AssetManifestWireEntry {
    int index{ 0 }; //!< tile_index（1..65535。0 は未登録なので運ばない）
    std::string kind; //!< "P" | "R" | "K" | "F"
    int id{ 0 }; //!< terrain_id / monrace_id / bi_id（P は 0 固定）
    std::string path; //!< PNG の相対パス
};

//! `asset_manifest.aliases[]` 1 件（v1 §8.2）。既存 index への別 ID 対応（新 index を作らない）。
struct AssetManifestAliasWireEntry {
    std::string kind; //!< 別名側の種別（共有元と異なることがある。擬態など）
    int id{ 0 };
    int index{ 0 }; //!< 共有元の tile_index
};

/*!
 * @brief core → ui（v1 §8.2）。pad_commands / sub_panel_kinds と同時に 1 回。再送なし。
 * @details タイル供給の逆転（憲章 §5.1、Phase 2 P2-1）。ui はこの目録だけでタイルを
 * 引き、official_tile_table を同梱しない。受けるまで・目録に無い index は
 * プレースホルダ絵で描く（落ちない）。
 */
struct AssetManifestMessage {
    //! 相対 path の基準ディレクトリ（絶対パス）。空 = ui の cwd 基準。
    std::string root;
    std::vector<AssetManifestWireEntry> assets;
    std::vector<AssetManifestAliasWireEntry> aliases;
};

/*!
 * @brief `input_event.events[]` 1 件（v1 §8.3、Phase 2 P2-2a）。
 * @details 種別タグ `e` と、種別ごとに使うメンバだけが意味を持つ平坦な入れ物
 * （他のワイヤ型と同じく union は使わない）。decode 側が値域検査を済ませるので、
 * 受け手（アダプタ）は検査なしで読んでよい。
 */
struct InputEventWire {
    std::string e; //!< "move" | "cancel" | "confirm" | "answer" | "key" | "text" | "fkey" | "set_number" | "keyseq"
    int dx{ 0 }; //!< move: -1..1
    int dy{ 0 }; //!< move: -1..1
    std::string value; //!< answer: "yes" | "no"
    std::string chr; //!< key: 印字 ASCII 1 文字（"char" キーのワイヤ表現）
    std::string name; //!< key: "tab" | "delete" | "backspace"
    bool ctrl{ false }; //!< key / fkey
    bool shift{ false }; //!< fkey
    bool alt{ false }; //!< fkey
    std::string text; //!< text: UTF-8 文字列
    int n{ 0 }; //!< fkey: 1..12
    long long number{ 0 }; //!< set_number: 値（"value" キー。answer の value とはキー名が同じでも型で区別）
    int digits{ 1 }; //!< set_number: 0 詰め桁数（省略時 1 = 詰めない）
    /*!
     * @brief keyseq: コアへそのまま積むバイト列（1〜255）。
     * @details **ほかのイベントのように意味へ翻訳しない、生の列**である。要るのは
     * マクロのトリガーを押すため。トリガーは `\x1f` で始まる形（F キー）を取ることがあり、
     * `key` イベントでは運べない（`chr` は印字 ASCII、Ctrl は英字だけ）。
     * 翻訳の余地が無い＝**コアが受け取るキーが送り手の意図そのもの**なので、
     * マクロのように「この列を押したことにしたい」用途にはこちらが正しい。
     * @note 通常の操作に使ってはいけない。方向やコマンドは今までどおり意味で送ること
     * （生のキーで送ると、キー配列の違いを送り手が背負うことになる）。
     */
    std::vector<int> bytes;
};

//! ui → core（v1 §8.3）。`keys` の後継。v1 では keys と共存し、到着順に消費される。
struct InputEventsMessage {
    std::vector<InputEventWire> events;
};

/*!
 * @brief ペイロードの `"t"` だけを取り出す（v1 §2.1 の必須キー）。
 * @return 種別名。JSON として壊れている・オブジェクトでない・`"t"` が無い／文字列でない
 *   ときは**空文字列**。
 * @details 未知種別を黙って捨てる（v1 §2.3）ための入口。ここで種別を見てから
 * 対応する `decode_*` を呼ぶ。JSON を 2 回舐めることになるが、制御メッセージは
 * 数が少なく、`frame` は種別を見た時点で `frame_codec` へ回るので実害は無い。
 */
std::string peek_message_type(const std::string &json_text);

std::string encode_hello(const HelloMessage &message);
std::string encode_hello_ack(const HelloAckMessage &message);
std::string encode_fatal(const FatalMessage &message);
std::string encode_exit(const ExitMessage &message);
std::string encode_ui_state(const UiStateMessage &message);
std::string encode_keys(const KeysMessage &message);
std::string encode_quit_request(const QuitRequestMessage &message);
std::string encode_pad_commands(const PadCommandsMessage &message);
std::string encode_sub_panel_kinds(const SubPanelKindsMessage &message);
std::string encode_macro_triggers(const MacroTriggersMessage &message);
std::string encode_asset_manifest(const AssetManifestMessage &message);
std::string encode_input_events(const InputEventsMessage &message);

/*!
 * @brief `encode_hello` の逆。
 * @param[out] out 成功時のみ書き換わる（失敗時は触らない）。
 * @param[out] err 失敗した理由。
 * @return 成功したか。JSON 破損・`"t"` 不一致・必須キーの型違いは失敗。
 * @note **未知キーは黙って無視する**（v1 §2.3）。
 */
bool decode_hello(const std::string &json_text, HelloMessage &out, std::string &err);
bool decode_hello_ack(const std::string &json_text, HelloAckMessage &out, std::string &err);
bool decode_fatal(const std::string &json_text, FatalMessage &out, std::string &err);
bool decode_exit(const std::string &json_text, ExitMessage &out, std::string &err);
bool decode_ui_state(const std::string &json_text, UiStateMessage &out, std::string &err);
bool decode_keys(const std::string &json_text, KeysMessage &out, std::string &err);
bool decode_quit_request(const std::string &json_text, QuitRequestMessage &out, std::string &err);
bool decode_pad_commands(const std::string &json_text, PadCommandsMessage &out, std::string &err);
bool decode_sub_panel_kinds(const std::string &json_text, SubPanelKindsMessage &out, std::string &err);
bool decode_macro_triggers(const std::string &json_text, MacroTriggersMessage &out, std::string &err);
bool decode_asset_manifest(const std::string &json_text, AssetManifestMessage &out, std::string &err);
bool decode_input_events(const std::string &json_text, InputEventsMessage &out, std::string &err);

} // namespace presentation
