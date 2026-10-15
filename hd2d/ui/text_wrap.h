#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
namespace hd2d {
namespace text_wrap_detail {
// UTF-8 の 1 字を読む（壊れたバイトは 1 バイト 1 字として扱う）。
inline char32_t decode_at(std::string_view s,size_t i,size_t *len=nullptr) {
    const auto b=[&](size_t k){return static_cast<unsigned char>(s[k]);};
    const unsigned char c=b(i);size_t n=1;char32_t cp=c;
    if(c>=0xF0&&i+3<s.size()){n=4;cp=((c&0x07u)<<18)|((b(i+1)&0x3Fu)<<12)|((b(i+2)&0x3Fu)<<6)|(b(i+3)&0x3Fu);}
    else if(c>=0xE0&&i+2<s.size()){n=3;cp=((c&0x0Fu)<<12)|((b(i+1)&0x3Fu)<<6)|(b(i+2)&0x3Fu);}
    else if(c>=0xC0&&i+1<s.size()){n=2;cp=((c&0x1Fu)<<6)|(b(i+1)&0x3Fu);}
    if(len)*len=n;return cp;
}
// i の直前の字の頭のバイト位置（i>0 のとき）。
inline size_t prev_start(std::string_view s,size_t i) {
    size_t k=i-1;while(k>0&&(static_cast<unsigned char>(s[k])&0xc0)==0x80)--k;return k;
}
// 全角の文字（CJK・かな・全角の約物）。この境では空白を探さずに折ってよい。
inline bool is_wide(char32_t cp){return cp>=0x2E80&&!(cp>=0xFF61&&cp<=0xFF9F);}
// 行頭禁則（行の頭に来てはいけない字）。
inline bool head_ng(char32_t cp) {
    switch(cp) {
    case U'、':case U'。':case U'，':case U'．':case U'）':case U'」':case U'』':case U'】':case U'〕':
    case U'〉':case U'》':case U'］':case U'｝':case U'〙':case U'〗':case U'！':case U'？':case U'：':case U'；':
    case U'ー':case U'・':case U'…':case U'‥':case U'～':case U'々':case U'ゝ':case U'ゞ':case U'ヽ':case U'ヾ':
    case U'ぁ':case U'ぃ':case U'ぅ':case U'ぇ':case U'ぉ':case U'っ':case U'ゃ':case U'ゅ':case U'ょ':case U'ゎ':
    case U'ァ':case U'ィ':case U'ゥ':case U'ェ':case U'ォ':case U'ッ':case U'ャ':case U'ュ':case U'ョ':case U'ヮ':
    case U'ヵ':case U'ヶ':
        return true;
    default:return false;
    }
}
// 行末禁則（行の終わりに来てはいけない字）。
inline bool tail_ng(char32_t cp) {
    switch(cp) {
    case U'「':case U'『':case U'（':case U'【':case U'〔':case U'〈':case U'《':case U'［':case U'｛':case U'〘':case U'〖':
        return true;
    default:return false;
    }
}
}
// Keep explicit paragraph breaks. Prefer word boundaries, falling back to a
// UTF-8 boundary for a single word that cannot fit in the available width.
// 全角の境で折るときは空白を探さず、禁則（行頭・行末）を守るため折る位置を前へ送る
// （はみ出させない。送った字は次の行の頭へ回る）。英語の語の境の折り方は変えない。
template<class Fit>
std::vector<std::string> wrap_ui_text(std::string_view input,int width,Fit fit) {
    using namespace text_wrap_detail;
    std::vector<std::string> lines;size_t start=0;
    while(start<input.size()) {
        const size_t end=input.find('\n',start);
        auto rest=input.substr(start,end==std::string_view::npos?input.size()-start:end-start);
        if(rest.empty())lines.emplace_back();
        while(!rest.empty()) {
            size_t n=std::min(rest.size(),fit(rest,std::max(1,width)));
            if(n==0) {n=1;while(n<rest.size()&&(static_cast<unsigned char>(rest[n])&0xc0)==0x80)++n;}
            if(n<rest.size()&&rest[n]!=' '&&rest[n]!='\t') {
                const bool wide_break=is_wide(decode_at(rest,n))||is_wide(decode_at(rest,prev_start(rest,n)));
                if(!wide_break) {
                    const auto space=rest.substr(0,n).find_last_of(" \t");
                    if(space!=std::string_view::npos&&space>0)n=space;
                } else {
                    // 送り: 次の行の頭が行頭禁則、またはこの行の終わりが行末禁則なら 1 字ずつ前で折る（最大 2 字）。
                    for(int guard=0;guard<2;++guard) {
                        const size_t p=prev_start(rest,n);
                        if(p==0)break;
                        if(head_ng(decode_at(rest,n))||tail_ng(decode_at(rest,p)))n=p;else break;
                    }
                }
            }
            lines.emplace_back(rest.substr(0,n));rest.remove_prefix(n);
            while(!rest.empty()&&(rest.front()==' '||rest.front()=='\t'))rest.remove_prefix(1);
        }
        if(end==std::string_view::npos)break;start=end+1;
    }
    return lines;
}
}
