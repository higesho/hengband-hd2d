#pragma once
/*!
 * @file fc_history.h
 * @brief Frox のメッセージ履歴（`Ctrl+P`）の行を、1 メッセージ 1 行にまとめる。
 * @details 原作の `do_cmd_messages()`（`cmd4.c`）は、メッセージを 1 つずつ幅 80 の文書へ流し込む。長いメッセージは行をまたいで
 * 折り返されるが、**折り返しの行とメッセージの切れ目は画面の字だけでは見分けられない**（先頭に印が無い。ターンの区切りの
 * 空行も、メッセージごとには入らない）。新しい UI の窓は 1 行を 1 段落にするので、折り返された 1 つのメッセージが何段落にも割れ
 * （英語）、日本語では段落の判定に使う印が無く 1 つにつながっていた（2026-10-10 の気づいたこと）。
 *
 * ここは原作のメッセージの記録（`msg_get()`。アダプタが読んで渡す）を物差しにして、画面の行がどのメッセージの何番目の行かを
 * 求める。空白を全部落とした字（画面の行を並べたもの `V`・記録を履歴の順に並べたもの `D`）で `V` が `D` のどこにあたるかを探し、
 * 記録のメッセージの先頭にあたる行を新しい段落の始まりとする。窓の最初の行が前のメッセージの続きでも、そのまま 1 つの段落になる。
 * 見つからなければ（記録と画面の字が食い違う）何もしない（呼び出し側は元の行のまま窓を組む）。
 *
 * 原作のソースは触らない。画面の行の写し（`menu_term_lines`）を窓の組み立ての間だけ差し替える使い方で、
 * 送る写しそのものは変えない（`fc_main.cpp` の `host_present()`）。
 */
#include "frame/game_frame.h"
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace fc {
//! 空白（ASCII・タブ・全角の空白）を全部落とす。折り返しで消える空白の有無に左右されないため。
inline std::string history_squash(std::string_view s) {
    std::string out;out.reserve(s.size());
    for(size_t i=0;i<s.size();++i) {
        const unsigned char c=static_cast<unsigned char>(s[i]);
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c==0xE3&&i+2<s.size()&&static_cast<unsigned char>(s[i+1])==0x80&&static_cast<unsigned char>(s[i+2])==0x80){i+=2;continue;} // U+3000
        out.push_back(char(c));
    }
    return out;
}
inline std::string history_trim(std::string_view s) {
    const auto a=s.find_first_not_of(" \t\r\n");if(a==std::string_view::npos)return {};
    return std::string(s.substr(a,s.find_last_not_of(" \t\r\n")-a+1));
}
//! 画面がメッセージ履歴か（1 行目が `[過去のメッセージ …]` / `[Previous Messages, Line …]`）。見出しの添字（無ければ -1）。
inline int history_header_index(const std::vector<TermMirrorLine> &lines) {
    for(size_t i=0;i<lines.size();++i) {
        const auto text=history_trim(lines[i].text_utf8);
        if(text.empty())continue;
        if(!text.starts_with('['))return -1;
        if(text.find(reinterpret_cast<const char*>(u8"過去のメッセージ"))!=std::string::npos||text.find("Previous Messages")!=std::string::npos)return int(i);
        return -1;
    }
    return -1;
}
/*!
 * @brief 折り返された行を 1 メッセージ 1 行にまとめる。
 * @param lines 画面の行（`menu_term_lines`）。まとめられたら書き換える。
 * @param messages 原作のメッセージの記録を、履歴に並ぶ順（**古いものが先**。`do_cmd_messages()` は `msg_count() - 1` から 0 へ流す）に並べたもの。
 *        繰り返しは ` (x2)` を付けたあとの字（`do_cmd_messages()` と同じ）。
 * @return まとめたら true。見出しと脚注が無い・記録と字が合わないときは false（`lines` はそのまま）。
 */
inline bool join_history_lines(std::vector<TermMirrorLine> &lines,const std::vector<std::string> &messages) {
    const int header=history_header_index(lines);
    if(header<0)return false;
    int footer=-1;
    for(size_t i=lines.size();i-->size_t(header)+1;) {
        const auto text=history_trim(lines[i].text_utf8);
        if(text.empty())continue;
        if(text.starts_with('[')&&text.ends_with(']')&&text.find("ESC")!=std::string::npos)footer=int(i);
        break;
    }
    if(footer<0)return false;
    // 画面の字 V と、各行の V の中の始まり
    std::string screen;std::vector<std::pair<int,size_t>> rows; // (行の添字, V の中の始まり)
    for(int i=header+1;i<footer;++i) {
        const auto text=history_squash(lines[size_t(i)].text_utf8);
        if(text.empty())continue;
        rows.push_back({i,screen.size()});screen+=text;
    }
    if(rows.empty()||messages.empty())return false;
    // 記録 D と、メッセージの先頭の位置
    std::string log;std::set<size_t> starts;
    for(const auto &message:messages) {
        const auto text=history_squash(message);
        if(text.empty())continue;
        starts.insert(log.size());log+=text;
    }
    const auto at=log.find(screen);
    if(at==std::string::npos)return false;
    // まとめる
    std::vector<TermMirrorLine> out;
    for(int i=0;i<=header;++i)out.push_back(lines[size_t(i)]);
    size_t next_row=0;
    for(int i=header+1;i<footer;++i) {
        if(next_row>=rows.size()||rows[next_row].first!=i){out.push_back(lines[size_t(i)]);continue;} // 空行はそのまま
        const bool begins=next_row==0||starts.count(at+rows[next_row].second)!=0;
        ++next_row;
        if(begins||out.empty()||out.back().text_utf8.empty()){out.push_back(lines[size_t(i)]);continue;}
        // 前の行の続き: 行の継ぎ目に空白が要るのは、両側が ASCII の文字のときだけ（日本語の折り返しは空白を入れない）
        auto &previous=out.back();
        const auto last=history_trim(previous.text_utf8);
        const auto text=history_trim(lines[size_t(i)].text_utf8);
        const bool both_ascii=!last.empty()&&!text.empty()&&static_cast<unsigned char>(last.back())<128&&static_cast<unsigned char>(text.front())<128;
        previous.text_utf8=last+(both_ascii?" ":"")+text;
        previous.color_spans.clear();
    }
    for(int i=footer;i<int(lines.size());++i)out.push_back(lines[size_t(i)]);
    lines=std::move(out);
    return true;
}
}
