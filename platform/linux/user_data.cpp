#include "user_data.h"
#include <json/json.h>
#include <picosha2/picosha2.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace hd2d::linux_platform {
namespace {
namespace fs=std::filesystem;
std::string read_file(const fs::path &p) {
    std::ifstream f(p,std::ios::binary);
    if (!f) throw std::runtime_error("ファイルを読めません: "+p.string());
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
std::string sha(const std::string &bytes) {return picosha2::hash256_hex_string(bytes);}
Json::Value parse(const std::string &bytes) {
    Json::CharReaderBuilder builder;builder["rejectDupKeys"]=true;builder["failIfExtra"]=true;
    Json::Value value;std::string err;
    const auto reader=std::unique_ptr<Json::CharReader>(builder.newCharReader());
    if (!reader->parse(bytes.data(),bytes.data()+bytes.size(),&value,&err)) throw std::runtime_error("配布データの目録を読めません: "+err);
    return value;
}
fs::path relative(const std::string &name) {
    const fs::path p(name);
    if (p.empty() || p.is_absolute()) throw std::runtime_error("目録の相対パスが不正です");
    for (const auto &part:p) if (part==".." || part==".") throw std::runtime_error("目録のパスが不正です");
    return p;
}
void ensure_directory(const fs::path &root,const fs::path &rel) {
    fs::path path=root;
    for (const auto &part:rel) {
        path/=part;
        if (fs::is_symlink(fs::symlink_status(path))) throw std::runtime_error("保存先にリンクされたフォルダーがあります: "+path.string());
        fs::create_directory(path);
        if (!fs::is_directory(path)) throw std::runtime_error("保存先がフォルダーではありません: "+path.string());
    }
}
void atomic_write(const fs::path &p,const std::string &bytes) {
    std::string temp=p.string()+".tmp-XXXXXX";
    const int fd=::mkstemp(temp.data());
    if (fd<0) throw std::runtime_error("一時ファイルを作れません: "+p.string());
    std::size_t done=0;bool ok=true;
    while (done<bytes.size()) {
        const auto n=::write(fd,bytes.data()+done,bytes.size()-done);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) {ok=false;break;}
        done+=static_cast<std::size_t>(n);
    }
    if (::fsync(fd)!=0) ok=false;
    if (::close(fd)!=0) ok=false;
    if (!ok) {::unlink(temp.c_str());throw std::runtime_error("保存できません: "+p.string());}
    std::error_code ec;fs::rename(temp,p,ec);
    if (ec) {::unlink(temp.c_str());throw std::runtime_error("保存ファイルを置き換えられません: "+p.string());}
}
struct Lock {
    int fd{-1};
    explicit Lock(const fs::path &p) {
        fd=::open(p.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
        if (fd<0 || ::flock(fd,LOCK_EX)!=0) throw std::runtime_error("データの更新を開始できません: "+p.string());
    }
    ~Lock(){if(fd>=0){::flock(fd,LOCK_UN);::close(fd);}}
};
}

bool prepare_user_data(std::string &error) {
    try {
        fs::path assets;
        if (const char *p=std::getenv("HENGBAND_ASSET_ROOT");p&&*p) assets=fs::absolute(p);
        else assets=fs::read_symlink("/proc/self/exe").parent_path();
        fs::path data;
        if (const char *p=std::getenv("HENGBAND_DATA_ROOT");p&&*p) data=fs::absolute(p);
        else if (const char *p=std::getenv("XDG_DATA_HOME");p&&*p&&fs::path(p).is_absolute()) data=fs::path(p)/"hengband-hd2d";
        else if (const char *p=std::getenv("HOME");p&&*p) data=fs::path(p)/".local/share/hengband-hd2d";
        else throw std::runtime_error("保存先を決められません。HOME または HENGBAND_DATA_ROOT を設定してください。");
        assets=fs::weakly_canonical(assets);data=fs::weakly_canonical(data);
        if (assets==data) throw std::runtime_error("配布データと保存先には別のフォルダーを指定してください。");
        const std::string manifest_bytes=read_file(assets/"linux-data.json");
        const auto manifest=parse(manifest_bytes);
        if (manifest["schema"].asInt()!=1 || !manifest["copies"].isObject() || !manifest["links"].isArray() || !manifest["directories"].isArray())
            throw std::runtime_error("配布データの目録の形式が不正です。");
        fs::create_directories(data);
        Lock lock(data/".data-update.lock");
        Json::Value previous(Json::objectValue);
        const auto state_path=data/".packaged-data.json";
        if (fs::exists(state_path)) previous=parse(read_file(state_path));
        for (const auto &entry:manifest["directories"]) ensure_directory(data,relative(entry.asString()));
        for (const auto &name:manifest["copies"].getMemberNames()) {
            const auto rel=relative(name);const auto target=data/rel;
            ensure_directory(data,rel.parent_path());
            const auto status=fs::symlink_status(target);
            if (fs::is_symlink(status)) throw std::runtime_error("保存先にリンクされたファイルがあります: "+target.string());
            const std::string wanted=manifest["copies"][name].asString();
            if (fs::exists(status)) {
                if (!fs::is_regular_file(status)) throw std::runtime_error("保存先がファイルではありません: "+target.string());
                if (previous["copies"][name].asString()==wanted) continue;
                const auto current=sha(read_file(target));
                if (current==wanted) continue;
                // Preserve files changed by the player or generated by a game core.
                if (!previous["copies"].isMember(name) || current!=previous["copies"][name].asString()) continue;
            }
            const auto bytes=read_file(assets/rel);
            if (sha(bytes)!=wanted) throw std::runtime_error("配布ファイルのハッシュが一致しません: "+name);
            atomic_write(target,bytes);
        }
        for (const auto &entry:manifest["links"]) {
            const auto rel=relative(entry.asString());const auto target=data/rel;
            const auto source=assets/rel;
            if (!fs::exists(source)) throw std::runtime_error("配布ファイルがありません: "+source.string());
            ensure_directory(data,rel.parent_path());
            const auto status=fs::symlink_status(target);
            if (fs::is_symlink(status)) {
                if (fs::read_symlink(target)==source) continue;
                fs::path temp=target.string()+".link-"+std::to_string(::getpid());
                fs::create_symlink(source,temp);fs::rename(temp,target);
            } else if (!fs::exists(status)) fs::create_symlink(source,target);
            // An existing real file or directory belongs to the player; keep it.
        }
        Json::Value state(Json::objectValue);state["copies"]=manifest["copies"];state["manifest_sha256"]=sha(manifest_bytes);
        Json::StreamWriterBuilder writer;writer["indentation"]="";
        atomic_write(state_path,Json::writeString(writer,state));
        if (::setenv("HENGBAND_DATA_ROOT",data.c_str(),1)!=0) throw std::runtime_error("保存先を設定できません。");
        fs::current_path(data);
        return true;
    } catch(const std::exception &e) {error=e.what();return false;}
}
} // namespace hd2d::linux_platform
