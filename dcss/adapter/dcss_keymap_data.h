// DCSS コア: 画面の部品（dcss-ui）の「操作の割り当て」（HD2D の機能メニュー）が読み書きする、キーボードの割り当て（原作のキーマップ）。D33。
//
// libhd2d.cpp だけが取り込む（原作のヘッダを引く TU。`dcss_tabs_data.h`・`dcss_pad_data.h` の後に取り込むこと）。原作のソースは書き換えない。
//
// ## 何を持つか（技術の判断）
// キーボードの割り当ては**原作のキーマップ**（ゲームの中の `~` の画面が編集する `macro.txt` の `K:` の行）をそのまま使う。画面の側に別の表を持たない。
// 理由: (1) `~` の画面と食い違わない（保存先が 1 つ）。(2) 割り当てを変えていない人の操作は 1 キーも変わらない（`macro.txt` に行が増えるだけ。既定のキーはそのまま効く）。
// (3) 遊びの結果は変わらない（キーマップは「そのキーを押したら、このキーを押したことにする」だけで、コマンドの処理には触れない）。
// 画面の側でキーをコマンドへ置き換える案は、`~` の画面で足したキーマップと二重になって食い違うので採らない。
//
// ## 書き方
//   - 「キー X でコマンド C」 = `K:X` → `A:<C の既定のキー>`（既定のキーがあり、そのキーで C が引けるとき。`~` の画面で読める）。無いときは `A:\{-C の番号}`（キーマップを通らないコマンドのキー）。
//   - 「元に戻す」 = `K:X` → `A:X`（恒等。原作には個別に消す公開関数が無く、`read_rc_file_macro` が足すだけなので、同じキーへ戻す行にする。遊びは割り当てる前と同じ）。
//   - 書くのは原作の `read_rc_file_macro`（rc の `macros += K X o` と同じ入口。`macro.txt` にもその場で保存する）だけ。
//   - 読むのは `macro_save`（遊びの途中で `~` の画面が変えた分も書き出す）のあと `macro.txt` の `K:`（文脈 0）の行。キーが 1 つ・動作が 1 つで、動作が割り当てられるコマンド 1 つを指すものだけを数える。
//
// 呼び出しはゲームのスレッド（`dcss_adapter_handle_ui_line`。画面の要求 `hd2d_keymap`）。
#pragma once

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "macro.h"
#include "state.h"

namespace dcss_keymap_data {

static bool int_field(const std::string &line, const char *name, long &value)
{
    const std::string pat = std::string("\"") + name + "\":";
    const std::size_t at = line.find(pat);
    if (at == std::string::npos)
        return false;
    const char *p = line.c_str() + at + pat.size();
    char *end = nullptr;
    value = std::strtol(p, &end, 10);
    return end != p;
}

static std::string str_field(const std::string &line, const char *name)
{
    const std::string pat = std::string("\"") + name + "\":\"";
    const std::size_t at = line.find(pat);
    if (at == std::string::npos)
        return std::string();
    const std::size_t from = at + pat.size();
    const std::size_t to = line.find('"', from);
    return to == std::string::npos ? std::string() : line.substr(from, to - from);
}

//! キーのコードを、`read_rc_file_macro` の引数（空白で区切る）に書ける綴りにする。
static std::string key_text(int key)
{
    if (key > 32 && key < 127 && key != '\\')
        return std::string(1, static_cast<char>(key));
    if (key == '\\')
        return "\\\\";
    return "\\{" + std::to_string(key) + "}";
}

struct Entry
{
    int key;
    int cmd;
};

//! `macro.txt` の `K:`（文脈 0）の行のうち、キー 1 つ → 割り当てられるコマンド 1 つを指すもの。
static std::vector<Entry> read_maps()
{
    if (crawl_state.unsaved_macros)
        macro_save(); // `~` の画面で変えて、まだ書き出していない分も読む
    std::vector<Entry> out;
    std::ifstream f(macro_file_name());
    std::string line;
    std::string keytext;
    bool have = false;
    while (std::getline(f, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.compare(0, 2, "K:") == 0)
        {
            keytext = line.substr(2);
            have = true;
        }
        else if (line.size() >= 3 && line[0] == 'K' && line[2] == ':')
            have = false; // 窓・狙いなど別の文脈
        else if (line.compare(0, 2, "M:") == 0)
            have = false;
        else if (line.compare(0, 2, "A:") == 0 && have)
        {
            const keyseq key = parse_keyseq(keytext);
            const keyseq action = parse_keyseq(line.substr(2));
            have = false;
            if (key.size() != 1 || action.size() != 1 || key[0] == action[0])
                continue;
            int cmd = 0;
            if (action[0] < 0)
                cmd = -action[0];
            else
                cmd = static_cast<int>(key_to_command(action[0], KMC_DEFAULT));
            if (cmd > CMD_NO_CMD && cmd <= CMD_MAX_NORMAL && dcss_pad_data::bindable(static_cast<command_type>(cmd)))
                out.push_back({ key[0], cmd });
        }
    }
    return out;
}

//! 現在のキーマップの表（`{"t":"dcss_ui","k":"keymap","maps":[[キーのコード,"キーの名前",コマンドの番号],…],"err":"…"}`）。
static std::string build_json(const std::string &err)
{
    dcss_tabs_data::Out o;
    o.raw("{\"t\":\"dcss_ui\",\"k\":\"keymap\",\"maps\":[");
    bool first = true;
    for (const Entry &e : read_maps())
    {
        if (!first)
            o.raw(",");
        first = false;
        o.raw("[");
        o.num(e.key);
        o.raw(",");
        o.str(keycode_to_name(e.key));
        o.raw(",");
        o.num(e.cmd);
        o.raw("]");
    }
    o.raw("],\"err\":");
    o.str(err);
    o.raw("}");
    return o.s;
}

/*!
 * @brief 画面の要求 `{"msg":"hd2d_keymap","op":"list"|"set"|"clear","key":コード,"cmd":番号}` を処理して、返事の JSON を返す。
 * @details `set` = キー `key` でコマンド `cmd`。`clear` = キー `key` を割り当てる前へ戻す。どれも返事は現在の表。
 */
static std::string handle(const std::string &line)
{
    const std::string op = str_field(line, "op");
    std::string err;
    if (op == "set" || op == "clear")
    {
        long key = 0;
        long cmd = 0;
        if (!int_field(line, "key", key) || key <= 0 || key > 0x10ffff)
            err = "key";
        else if (op == "set" && (!int_field(line, "cmd", cmd) || cmd <= CMD_NO_CMD || cmd > CMD_MAX_NORMAL
                                    || !dcss_pad_data::bindable(static_cast<command_type>(cmd))))
            err = "cmd";
        if (err.empty())
        {
            std::string action = key_text(static_cast<int>(key)); // clear = 同じキーへ戻す
            if (op == "set")
            {
                const int own = command_to_key(static_cast<command_type>(cmd));
                if (own > 32 && own < 127 && own != '\\' && key_to_command(own, KMC_DEFAULT) == static_cast<command_type>(cmd))
                    action = key_text(own);
                else
                    action = "\\{-" + std::to_string(cmd) + "}";
            }
            err = read_rc_file_macro("K " + key_text(static_cast<int>(key)) + " " + action);
        }
    }
    return build_json(err);
}

} // namespace dcss_keymap_data
