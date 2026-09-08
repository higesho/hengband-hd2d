#pragma once
#include <functional>
#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace hd2d {
// The reader returns the requested file from a caller-provided source archive.
using SourceReader = std::function<std::string(const std::string &, const std::string &)>;
using SourceProgress = std::function<void(const std::string &, int, int)>;
std::string canonical_source(std::string data);
std::string transform_source(const std::string &data, const std::string &transform);
std::map<std::string,std::string> reconstruct_sources(const nlohmann::json &recipe,
    const SourceReader &read, const std::function<void()> &cancel, const SourceProgress &progress = {});
}
