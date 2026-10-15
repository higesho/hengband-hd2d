#pragma once
#include "frame/game_frame.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>

namespace presentation {
// Bootstrap controller shared by cores which have no native title/load menu.
// It reads the real save directory and exposes the same New / Load / Quit flow
// as Hengband. The game continues to own creation, loading and save contents.
class NativeSavePicker {
public:
    struct Entry {int key;std::string label;bool selected;};
    struct Page {std::string title,description;std::vector<Entry> rows;};
    using List=std::function<std::vector<std::string>()>;
    using Remove=std::function<std::string(const std::string&)>;
    using Text=std::function<std::string(const std::string&)>;
    NativeSavePicker(std::string core,bool japanese,List list,Remove remove,Text display)
        :core_(std::move(core)),ja_(japanese),list_(std::move(list)),remove_(std::move(remove)),display_(std::move(display)) {}
    void show_load_only(){mode_=Mode::Load;load_only_=true;cursor_=page_=0;}
    Page page() {
        Page out;out.title=core_;
        if(mode_==Mode::Title) {
            out.rows={{'N',tr(u8"新しく始める","New Game"),false},{'L',tr(u8"セーブデータを読み込む","Load Game"),false},{'Q',tr(u8"終了する","Quit"),false}};
        } else if(mode_==Mode::Confirm) {
            out.title=tr(u8"セーブデータの削除","Delete Save");
            out.description=tr(u8"削除するデータ: ","Save to delete: ")+display_(target_)+"\n"+
                tr(u8"セーブと、そのデータに属する一時ファイルを削除します。削除すると元に戻せません。","The save and its own temporary files will be removed. This cannot be undone.");
            out.rows={{'n',tr(u8"削除しない","No, keep it"),false},{'y',tr(u8"削除する","Yes, delete it"),false}};
        } else if(mode_==Mode::Notice) {
            out.title=tr(u8"セーブデータ","Saved Games");out.description=notice_;
            if(ja_&&notice_.starts_with("The save has a lock file."))out.description=reinterpret_cast<const char*>(u8"このデータは使用中として記録されています。使用しているゲームを終了してから削除してください。");
            out.rows={{'Q',tr(u8"一覧に戻る","Back to saved games"),false}};
        } else {
            out.title=mode_==Mode::Delete?tr(u8"削除するデータを選ぶ","Choose the save to delete"):tr(u8"セーブデータを読み込む","Load Game");
            files_=list_();const int pages=std::max(1,(int(files_.size())+page_size_-1)/page_size_);
            page_=std::clamp(page_,0,pages-1);
            out.description=files_.empty()?tr(u8"セーブデータはありません。","There are no saved games."):
                std::to_string(files_.size())+tr(u8"件  ／  ページ "," saves / page ")+std::to_string(page_+1)+" / "+std::to_string(pages);
            for(int i=page_*page_size_;i<std::min<int>(files_.size(),(page_+1)*page_size_);++i)out.rows.push_back({'a'+i%page_size_,display_(files_[i]),false});
            if(page_>0)out.rows.push_back({'[',tr(u8"前のページ","Previous page"),false});
            if(page_+1<pages)out.rows.push_back({']',tr(u8"次のページ","Next page"),false});
            if(mode_==Mode::Load&&!files_.empty())out.rows.push_back({'D',tr(u8"セーブデータを削除する","Delete a save file"),false});
            out.rows.push_back({'Q',mode_==Mode::Delete?tr(u8"削除をやめる","Stop deleting"):tr(u8"タイトルに戻る","Back to title"),false});
        }
        cursor_=std::clamp(cursor_,0,int(out.rows.size())-1);out.rows[cursor_].selected=true;current_=out;return out;
    }
    // -2 keeps this menu open, -1 quits, 0 loads selected(), 1 creates a game.
    int key(int key) {
        if(key<0)return -1;
        if(key=='8'||key=='2'){cursor_=std::clamp(cursor_+(key=='8'?-1:1),0,int(current_.rows.size())-1);return -2;}
        if(key=='4'||key=='6') {
            if(mode_==Mode::Load||mode_==Mode::Delete)key=key=='4'?'[':']';
            else {cursor_=std::clamp(cursor_+(key=='4'?-1:1),0,int(current_.rows.size())-1);return -2;}
        }
        if(key==27){if(mode_==Mode::Title||(load_only_&&mode_==Mode::Load))return -1;back();return -2;}
        if(key==13||key==10||key==32)key=current_.rows[cursor_].key;
        if(mode_==Mode::Title&&(key=='n'||key=='l'||key=='q'))key=std::toupper(key);
        if(mode_==Mode::Confirm&&(key=='Y'||key=='N'))key=std::tolower(key);
        if(std::none_of(current_.rows.begin(),current_.rows.end(),[&](const auto &r){return r.key==key;}))return -2;
        if(mode_==Mode::Title) {
            if(key=='N')return 1;if(key=='Q')return -1;
            mode_=Mode::Load;cursor_=page_=0;return -2;
        }
        if(mode_==Mode::Confirm) {
            mode_=Mode::Load;cursor_=0;
            if(key=='y') {notice_=remove_(target_);if(!notice_.empty())mode_=Mode::Notice;}
            target_.clear();return -2;
        }
        if(mode_==Mode::Notice){mode_=Mode::Load;cursor_=0;return -2;}
        if(key=='Q'){if(load_only_&&mode_==Mode::Load)return -1;back();return -2;}
        if(key=='D'){mode_=Mode::Delete;cursor_=page_=0;return -2;}
        if(key=='['||key==']'){page_+=key=='['?-1:1;cursor_=0;return -2;}
        const int at=page_*page_size_+key-'a';
        if(at<0||at>=int(files_.size()))return -2;
        target_=files_[at];
        if(mode_==Mode::Load)return 0;
        mode_=Mode::Confirm;cursor_=0;return -2;
    }
    const std::string &selected()const{return target_;}
private:
    enum class Mode {Title,Load,Delete,Confirm,Notice};
    std::string tr(const char8_t *ja,const char *en)const{return ja_?reinterpret_cast<const char*>(ja):en;}
    void back(){mode_=(mode_==Mode::Confirm?Mode::Delete:(mode_==Mode::Delete?Mode::Load:Mode::Title));cursor_=0;}
    std::string core_,target_,notice_;bool ja_,load_only_{false};List list_;Remove remove_;Text display_;
    Mode mode_{Mode::Title};int cursor_{},page_{};static constexpr int page_size_=14;
    std::vector<std::string> files_;Page current_;
};

inline std::string remove_native_save(const std::filesystem::path &directory,const std::string &name) {
    namespace fs=std::filesystem;std::error_code ec;
    if(name.empty()||name=="."||name==".."||name.find_first_of("/\\:")!=std::string::npos)return "Invalid save name.";
    const auto root=fs::weakly_canonical(directory,ec);if(ec)return "Cannot open the save directory.";
    const auto save=root/fs::path(name);
    if(fs::is_symlink(save,ec)||!fs::is_regular_file(save,ec)||ec)return "The selected save is no longer available.";
    if(fs::exists(fs::path(save.string()+".lok"),ec))return "The save has a lock file. Close the game using this save before deleting it.";
    std::vector<fs::path> own;
    for(fs::directory_iterator it(root,ec),end; !ec && it!=end; it.increment(ec)) {
        const auto &entry=*it;
        const auto file=entry.path().filename().string();
        if(file.size()<=name.size()||file.compare(0,name.size(),name)!=0||file[name.size()]!='.')continue;
        const auto suffix=file.substr(name.size());
        const bool floor=suffix.size()==4&&suffix[1]=='F'&&std::isdigit(static_cast<unsigned char>(suffix[2]))&&std::isdigit(static_cast<unsigned char>(suffix[3]));
        if(!floor&&suffix!=".new"&&suffix!=".old"&&suffix!=".sdl2panels")continue;
        if(entry.is_symlink(ec)||!entry.is_regular_file(ec)||ec)return "A save companion is not a regular file.";
        own.push_back(entry.path());
    }
    if(ec)return "Cannot read the save directory.";
    own.push_back(save);
#ifdef _WIN32
    // A read-only file can be renamed on Windows but cannot be removed.
    // Detect it before moving any member of the save.
    for(const auto &path:own) {
        const auto status=fs::status(path,ec);
        if(ec||(status.permissions()&fs::perms::owner_write)==fs::perms::none)return "A save file is read-only and could not be deleted.";
    }
#endif
    fs::path staging;
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(int attempt=0;attempt<64;++attempt) {
        auto candidate=root/(".ui-delete-"+std::to_string(stamp)+"-"+std::to_string(attempt));
        if(fs::create_directory(candidate,ec)){staging=std::move(candidate);break;}
        if(ec)return "Cannot prepare the save deletion.";
    }
    if(staging.empty())return "Cannot prepare the save deletion.";
    std::vector<std::pair<fs::path,fs::path>> moved;
    const auto restore=[&](){
        bool okay=true;std::error_code error;
        for(auto i=moved.rbegin();i!=moved.rend();++i)if(fs::exists(i->second,error)) {
            fs::rename(i->second,i->first,error);if(error)okay=false;
        }
        fs::remove(staging,error);return okay;
    };
    // Stage the exact set first. A locked companion must not cause the main
    // character to disappear while reporting that deletion failed.
    for(const auto &path:own) {
        const auto destination=staging/path.filename();fs::rename(path,destination,ec);
        if(ec) {
            const bool restored=restore();
            return restored?"A save file is in use. No save files were deleted.":"Could not restore all save files after a deletion error. Check "+staging.string();
        }
        moved.emplace_back(path,destination);
    }
    for(const auto &entry:moved)if(!fs::remove(entry.second,ec)||ec) {
        const bool restored=restore();
        return restored?"Could not finish deleting the save. The remaining files were restored.":"Could not finish deleting the save. Check "+staging.string();
    }
    fs::remove(staging,ec);
    if(ec)return "The save was deleted, but its temporary folder could not be removed.";
    for(const char *marker:{"last_slot.txt",".resume"}) {
        const auto path=root/marker;std::ifstream in(path,std::ios::binary);std::string value;std::getline(in,value);in.close();
        while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'))value.pop_back();
        if(value==name){fs::remove(path,ec);if(ec)return "The save was deleted, but its remembered selection could not be cleared.";}
    }
    return {};
}
}
