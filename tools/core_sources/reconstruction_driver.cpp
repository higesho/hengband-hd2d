#include "app/source_reconstruction.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <picosha2/picosha2.h>
namespace fs=std::filesystem;
std::string read(const fs::path &p) {
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open test input");
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
int main(int argc,char **argv) {
    try {
        if(argc==5 && std::string(argv[1])=="--transform") {
            auto out=hd2d::transform_source(read(fs::u8path(argv[3])),argv[2]);
            std::ofstream f(fs::u8path(argv[4]),std::ios::binary);f.write(out.data(),out.size());if(!f)throw std::runtime_error("Cannot write transform result");return 0;
        }
        if(argc!=3)return 2;
        auto spec=nlohmann::json::parse(read(fs::u8path(argv[1])));const auto root=fs::weakly_canonical(fs::u8path(argv[2]));
        auto files=hd2d::reconstruct_sources(spec,[&](const std::string &archive,const std::string &path) {
            return read(root/fs::u8path(archive)/fs::u8path(path));
        },[]{});
        nlohmann::json hashes=nlohmann::json::object();
        for(const auto &[name,data]:files)hashes[name]=picosha2::hash256_hex_string(data);
        std::cout<<nlohmann::json({{"files",files.size()},{"hashes",hashes}}).dump()<<'\n';return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
