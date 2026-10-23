#include "main-unix/unix-user-ids.h"
#include <unistd.h>
#include <clocale>
#include "frame/protocol_transport.h"
#include "../shared/core_protocol_main.h"
#include <csignal>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
extern "C" int hengband_core_entry(presentation::TransportHandle,presentation::TransportHandle,const std::string &,const std::vector<std::string> &);
int main(int argc,char **argv) {
    std::signal(SIGPIPE,SIG_IGN);
    std::setlocale(LC_CTYPE,"C.UTF-8");
    auto &ids=UnixUserIds::get_instance();
    ids.set_user_id(::getuid());
    ids.set_effective_user_id(::geteuid());
    ids.set_effective_group_id(::getegid());
    bool protocol=false;std::string log;std::vector<std::string> args;
    for(int i=1;i<argc;++i){std::string_view value(argv[i]);
        // 自己検査（--text-check・--term-resize-check・--selftest）。Windows の main() と同じ関数（platform/windows/core_main.cpp）。
        int selftest_exit=0;if(hengband_core_run_selftest_option(value,selftest_exit))return selftest_exit;
        args.emplace_back(value);if(value=="--ui-protocol=stdio")protocol=true;if(value.starts_with("--protocol-log="))log=std::string(value.substr(15));}
    if(!protocol){std::fprintf(stderr,"This is a game core. Start HengbandHd2d to play.\n");return 2;}
    return hengband_core_entry(presentation::transport_stdin(),presentation::transport_stdout(),log,args);
}
