#include "app/source_reconstruction.h"
#include "portable/cp932.h"
#include "portable/cp932_table.h"
#include <picosha2/picosha2.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace hd2d {
namespace {
using Json=nlohmann::json;
constexpr std::size_t max_file=64u*1024u*1024u, max_total=1024u*1024u*1024u;
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
std::string digest(const std::string &s) { return picosha2::hash256_hex_string(s); }
bool relative_path(const std::string &p) {
    if(p.empty() || p.size()>512 || p.front()=='/' || p.back()=='/' || p.find_first_of("\\:\0",0,3)!=std::string::npos)return false;
    std::size_t start=0;
    while(start<p.size()) {
        const auto end=p.find('/',start);const auto piece=p.substr(start,end==std::string::npos?end:end-start);
        if(piece.empty() || piece=="." || piece=="..")return false;
        if(end==std::string::npos)break;start=end+1;
    }
    return true;
}
std::size_t number(const Json &v) {
    require(v.is_number_unsigned() || (v.is_number_integer() && v.get<std::int64_t>()>=0),"Expected nonnegative source offset");
    auto n=v.get<std::uint64_t>();require(n<=std::numeric_limits<std::size_t>::max(),"Source offset overflow");return static_cast<std::size_t>(n);
}
void append_utf8(std::string &out,std::uint32_t c) {
    if(c<0x80)out+=static_cast<char>(c);
    else if(c<0x800) {out+=static_cast<char>(0xc0|(c>>6));out+=static_cast<char>(0x80|(c&63));}
    else {out+=static_cast<char>(0xe0|(c>>12));out+=static_cast<char>(0x80|((c>>6)&63));out+=static_cast<char>(0x80|(c&63));}
}
bool lead(unsigned char c) {return (c>=0x81 && c<=0x9f) || (c>=0xe0 && c<=0xfc);}
bool trail(unsigned char c) {return (c>=0x40 && c<=0x7e) || (c>=0x80 && c<=0xfc);}
// Match Python's standard CP932 codec, including replacement of invalid input.
std::string comment_utf8(std::string_view bytes) {
    std::string out;
    for(std::size_t i=0;i<bytes.size();) {
        const auto c=static_cast<unsigned char>(bytes[i]);std::uint32_t u=0xfffd;std::size_t take=1;
        if(c<0x80 || c==0x80)u=c;
        else if(c>=0xa1 && c<=0xdf)u=0xff61+c-0xa1;
        else if(c==0xa0)u=0xf8f0;
        else if(c>=0xfd)u=0xf8f1+c-0xfd;
        else if(lead(c) && i+1<bytes.size() && trail(static_cast<unsigned char>(bytes[i+1]))) {
            const std::uint32_t code=(std::uint32_t(c)<<8)|static_cast<unsigned char>(bytes[i+1]);
            const auto *end=portable::kSjisToUcs+std::size(portable::kSjisToUcs);
            const auto *it=std::lower_bound(portable::kSjisToUcs,end,code,[](const portable::Cp932Entry &e,std::uint32_t key){return e.sjis<key;});
            if(it!=end && it->sjis==code){u=it->ucs;take=2;}
        }
        append_utf8(out,u);i+=take;
    }
    return out;
}
std::string cp932_c(const std::string &src) {
    require(src.find('\0')==std::string::npos,"NUL byte in C source");
    std::string out,pending;char state='N';std::size_t i=0;
    const auto flush=[&]{out+=comment_utf8(pending);pending.clear();};
    const auto octal=[&](unsigned char c){out+='\\';out+=static_cast<char>('0'+((c>>6)&7));out+=static_cast<char>('0'+((c>>3)&7));out+=static_cast<char>('0'+(c&7));};
    while(i<src.size()) {
        auto c=static_cast<unsigned char>(src[i]);
        if(state=='N' || state=='L' || state=='B') {
            if(c>=0x80) {
                std::size_t n=lead(c) && i+1<src.size() && trail(static_cast<unsigned char>(src[i+1]))?2:1;
                pending.append(src,i,n);i+=n;continue;
            }
            flush();
        }
        if(state=='N') {
            if(c=='/' && i+1<src.size() && (src[i+1]=='/' || src[i+1]=='*')) {state=src[i+1]=='/'?'L':'B';out.append(src,i,2);i+=2;continue;}
            if(c=='"')state='S';else if(c=='\'')state='C';
            out+=src[i++];continue;
        }
        if(state=='L' || state=='B') {
            if(state=='L' && c=='\n')state='N';
            else if(state=='B' && c=='*' && i+1<src.size() && src[i+1]=='/') {out+="*/";i+=2;state='N';continue;}
            out+=src[i++];continue;
        }
        if(lead(c) && i+1<src.size() && trail(static_cast<unsigned char>(src[i+1]))) {octal(c);octal(static_cast<unsigned char>(src[i+1]));i+=2;continue;}
        if(c>=0x80){octal(c);++i;continue;}
        if(c=='\\' && i+1<src.size()){out.append(src,i,2);i+=2;continue;}
        if((state=='S' && c=='"') || (state=='C' && c=='\''))state='N';
        out+=src[i++];
    }
    flush();require(out.size()<=max_file,"Converted source exceeds file limit");return out;
}
}
std::string canonical_source(std::string data) {
    if(data.find('\0')!=std::string::npos)return data;
    if(data.compare(0,3,"\xef\xbb\xbf")==0)data.erase(0,3);
    std::size_t dst=0;
    for(std::size_t i=0;i<data.size();++i)if(data[i]!='\r' || i+1>=data.size() || data[i+1]!='\n')data[dst++]=data[i];
    data.resize(dst);return data;
}
std::string transform_source(const std::string &data,const std::string &kind) {
    if(kind=="raw")return data;
    if(kind=="cp932-c")return cp932_c(data);
    throw std::runtime_error("Unsupported source transform: "+kind);
}
std::map<std::string,std::string> reconstruct_sources(const Json &recipe,const SourceReader &read,
    const std::function<void()> &cancel,const SourceProgress &progress) {
    require(recipe.at("schema")==2,"Unsupported source recipe version; rebuild the import kit");
    const auto &defs=recipe.at("inputs"), &adds=recipe.at("additions"), &files=recipe.at("files");
    require(defs.is_object() && adds.is_object() && files.is_object(),"Invalid source recipe objects");
    require(defs.size()<=10000 && adds.size()<=50000 && files.size()<=10000,"Source recipe count limit exceeded");
    std::map<std::string,std::string> inputs,additions,output;std::size_t total=0;
    for(auto it=defs.begin();it!=defs.end();++it) {
        cancel();const auto &v=it.value();const auto archive=v.at("archive").get<std::string>(),path=v.at("path").get<std::string>();
        require(relative_path(archive) && archive.find('/')==std::string::npos && relative_path(path),"Unsafe source input path");
        auto data=canonical_source(read(archive,path));
        require(data.size()<=max_file,"Source input exceeds file limit");
        if(digest(data)!=v.at("sha256").get<std::string>())throw std::runtime_error("Unsupported or modified source version: "+path);
        data=transform_source(data,v.at("transform").get<std::string>());
        require(digest(data)==v.at("transformed_sha256").get<std::string>(),"Source transform checksum mismatch");
        total+=data.size();require(total<=max_total,"Source input total limit exceeded");inputs.emplace(it.key(),std::move(data));
    }
    total=0;
    for(auto it=adds.begin();it!=adds.end();++it) {
        cancel();const auto &v=it.value();const auto encoding=v.at("encoding").get<std::string>();std::string data;
        if(encoding=="bytes") {
            const auto &values=v.at("values");require(values.is_array() && values.size()<=16,"Invalid literal syntax bytes");
            for(const auto &value:values){auto n=number(value);require(n<=255,"Invalid literal byte");data+=static_cast<char>(n);}
        } else {
            data=v.at("text").get<std::string>();
            if(encoding=="cp932") {auto converted=portable::utf8_to_cp932(data);require(data.empty() || !converted.empty(),"Cannot encode source addition");data=std::move(converted);}
            else require(encoding=="utf8","Unsupported addition encoding");
        }
        require(data.size()<=max_file && digest(data)==it.key(),"Source addition checksum mismatch");
        total+=data.size();require(total<=max_total,"Source additions total limit exceeded");additions.emplace(it.key(),std::move(data));
    }
    total=0;int done=0;
    for(auto it=files.begin();it!=files.end();++it) {
        cancel();require(relative_path(it.key()),"Unsafe reconstructed file path");const auto &ops=it.value().at("operations");require(ops.is_array() && ops.size()<=200000,"Source operation limit exceeded");std::string data;
        for(const auto &op:ops) {
            cancel();require(op.is_array() && !op.empty(),"Invalid source operation");const auto kind=op[0].get<std::string>();
            if(kind=="copy" && op.size()==4) {
                const auto &from=inputs.at(op[1].get<std::string>());auto at=number(op[2]),count=number(op[3]);
                require(at<=from.size() && count<=from.size()-at,"Source range exceeds input");
                require(count<=max_file-data.size(),"Reconstructed file exceeds limit");data.append(from,at,count);
            } else if(kind=="add" && op.size()==2) {
                const auto &part=additions.at(op[1].get<std::string>());require(part.size()<=max_file-data.size(),"Reconstructed file exceeds limit");data+=part;
            } else throw std::runtime_error("Unsupported source operation");
        }
        const auto newline=it.value().value("newline",std::string("keep"));
        if(newline=="crlf") {
            const auto count=static_cast<std::size_t>(std::count(data.begin(),data.end(),'\n'));
            require(count<=max_file-data.size(),"Expanded newlines exceed file limit");
            std::string expanded;expanded.reserve(data.size()+count);
            for(char c:data){if(c=='\n')expanded+='\r';expanded+=c;}
            data=std::move(expanded);
        } else require(newline=="keep","Unsupported output newline transform");
        if(it.value().value("bom",false)) {
            require(data.size()<=max_file-3,"BOM exceeds file limit");data.insert(0,"\xef\xbb\xbf");
        }
        require(digest(data)==it.value().at("sha256").get<std::string>(),"Reconstructed source checksum mismatch");
        total+=data.size();require(total<=max_total,"Reconstructed source total limit exceeded");output.emplace(it.key(),std::move(data));
        if(progress)progress(it.key(),++done,static_cast<int>(files.size()));
    }
    return output;
}
}
