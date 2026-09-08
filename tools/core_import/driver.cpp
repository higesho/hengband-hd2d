#include "app/core_import.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>

// 開発用の検査入口。製品の UI と同じ処理を呼ぶ。
int main(int argc,char **argv) {
    if(argc<6) return 2;
    hd2d::CoreImport importer({argv[1],argv[2],argv[3],argc>7?argv[7]:argv[3],argc>6?argv[6]:"windows"});
    importer.start(argv[4],argv[5]);
    hd2d::CoreImportStatus s;
    int previous=-1;
    do {
        s=importer.status();
        if(argc>8 && std::string(argv[8])=="--cancel" && s.phase=="build") importer.cancel();
        if(s.completed!=previous) {std::cout<<s.phase<<" "<<s.completed<<"/"<<s.total<<std::endl;previous=s.completed;}
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }while(s.running);
    std::cout<<nlohmann::json({{"phase",s.phase},{"message",s.message},{"path",s.core_path},{"succeeded",s.succeeded}}).dump()<<std::endl;
    return s.succeeded?0:s.phase=="cancelled"?3:1;
}
