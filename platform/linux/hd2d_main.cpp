#include "../shared/desktop_command_line.h"
#include "user_data.h"
#include <clocale>
#include <csignal>
#include <filesystem>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
    std::signal(SIGPIPE, SIG_IGN);
    if (!std::setlocale(LC_CTYPE, "")) std::setlocale(LC_CTYPE, "C.UTF-8");
    const auto original = std::filesystem::current_path();
    std::vector<std::string> args;
    const std::vector<std::string> path_options = {
        "--protocol-log=", "--core-protocol-log=", "--test-input-file=", "--shot=",
        "--model-check=", "--model-view=", "--model-terrain-pack=", "--model-wall=",
        "--model-floor=", "--model-player=", "--voxel-dir=", "--lut-export=", "--town-data="
    };
    for (int i=1; i<argc; ++i) {
        std::string arg(argv[i]);
        if (arg=="--help") {
            std::puts("Hengband HD - desktop Linux\nRun without arguments to choose a game.\n--windowed=WxH  --fullscreen  --core-path=PATH  --gl-probe\nData: $XDG_DATA_HOME/hengband-hd2d (or ~/.local/share/hengband-hd2d).\nHENGBAND_DATA_ROOT overrides the data directory; HENGBAND_ASSET_ROOT overrides packaged assets.");
            return 0;
        }
        for (const auto &prefix : path_options) {
            if (arg.starts_with(prefix) && arg.size()>prefix.size()) {
                const auto path=std::filesystem::path(arg.substr(prefix.size()));
                if (path.is_relative()) arg=prefix+(original/path).lexically_normal().string();
                break;
            }
        }
        if (arg.starts_with("--core-path=")) {
            const auto path=std::filesystem::path(arg.substr(12));
            if (path.is_relative() && path.has_parent_path()) arg="--core-path="+(original/path).lexically_normal().string();
        }
        args.push_back(std::move(arg));
    }
    hd2d::AppOptions options;
    hd2d::desktop::apply_options(args, options);
    if (!options.gl_probe_only) {
        std::string err;
        if (!hd2d::linux_platform::prepare_user_data(err)) {
            std::fprintf(stderr,"Hengband HD: %s\n",err.c_str()); return 1;
        }
    }
    return hd2d::desktop::run(options);
}
