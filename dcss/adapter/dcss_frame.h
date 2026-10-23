// DCSS コア: 原作（ウェブタイル版）が出す JSON を受けて状態を持ち、HD2D のフレームに組む層。
// プロトコル v1 の枠（長さ付きの JSON・握手）は dcss_main.cpp、ここは「JSON → GameFrame」だけ。
// 原作のヘッダは引かない（原作の .cc とは別の TU。C++20）。
#pragma once

#include "frame/game_frame.h"

#include <string>
#include <vector>

namespace dcss_frame {

//! 原作が出した JSON 1 メッセージ（末尾の改行は含まない。先頭が '*' のものはサーバ宛の制御メッセージ）を取り込む。ゲームのスレッドから。
void ingest(const std::string &json_line);

//! いまの状態から GameFrame を組む。ゲームのスレッドから。
void build(GameFrame &frame);

//! 地図の上の印（怪物の状態のアイコン・態度・強さの旗。段 3）を画面の部品へ送る JSON。何も無ければ空。ゲームのスレッドから。
std::string marks_json();

//! 画面側が選んだ表示（ui_state）。どのスレッドからでもよい。
void set_view_cells(int w, int h);
void set_language(const std::string &lang);
//! 画面が `dcss-ui` を挙げているか。真のとき、窓（メニュー・説明・文字画面）の字と選択肢は画面の部品が自分で描くので、フレームには載せない
//! （窓が開いている・文字入力中などの旗だけを載せる）。ゲームに入る前に 1 度だけ呼ぶ。
void set_ui_offered(bool offered);

//! 受信スレッドが入力を翻訳するための、いまの入力の場面。最後に組んだフレームの時点のもの。
enum class InputScene : int {
    Command = 0,  //!< 地図で次の命令を待っている（方向は vi キー）
    Window = 1,   //!< メニュー・説明の窓・文字画面（方向は矢印キー）
    Text = 2,     //!< 1 行の文字入力
    Other = 3,    //!< 狙い・方向の問い・-more- など（方向は vi キー）
};
InputScene input_scene();
//! 文字入力の最中なら、入力した字を表示のために覚える（原作は入力欄の中身を送ってこない）。受信スレッドから。
void note_text_key(int keycode);

//! キャラクターが決まっていれば名前（保存して終わる前に控え、`--resume` で開き直すのに使う）。
std::string player_name();
//! ゲームの外へ出たとき（死亡・終了の理由）の控え。空なら無い。
std::string exit_reason();
//! 死亡・勝利などでゲームが終わったか（exit_reason が dead / won / quit のとき）。
bool game_ended();

//! 怪物・アイテム・プレイヤーの板（M2。dcss_slab.h）。ゲームのスレッドから。
//! 画像ページ（tools/dcss/dcss_tile_atlas.py の出力）の置き場と、作った板を書く置き場を渡す。読めなければ false（怪物・アイテムは字で出る）。
bool init_slabs(const std::string &atlas_dir, const std::string &slab_dir);
//! 目録（asset_manifest）の 1 行。板の名前（.vox の拡張子なしのファイル名）が画面の探す名前になる。
struct AssetEntry {
    int index = 0; //!< tile_index
    char kind = 'R'; //!< 'P' プレイヤー・'R' 怪物・'K' アイテム
    int id = 0;
    std::string name;
};
//! 前の呼び出しのあとに板が増えていれば、目録の全部を `all` に入れて true（画面は全置換で受ける）。板のファイルは書き終えてある。
bool take_new_assets(std::vector<AssetEntry> &all);
std::size_t asset_count();
//! 検査用の入口（dcss_main.cpp の main が `--dcss-slab-bake` / `--dcss-slab-layers` で呼ぶ）。ゲームは起こさない。
int slab_tool(int argc, char **argv);

} // namespace dcss_frame
