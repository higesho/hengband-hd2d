#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hd2d {

struct ImportTarget {
    std::string id, name, version;
};

struct CoreImportStatus {
    std::string phase, message, core_path;
    int completed{0}, total{0};
    bool running{false}, succeeded{false};
};

struct CoreImportConfig {
    std::filesystem::path kit, storage, compiler, linker;
    std::string platform;
};

// ZIP 由来のコマンドは実行しない。同梱された定義とコンパイラーだけを使用する。
class CoreImport {
public:
    explicit CoreImport(CoreImportConfig config);
    ~CoreImport();
    CoreImport(const CoreImport &) = delete;
    CoreImport &operator=(const CoreImport &) = delete;
    std::vector<ImportTarget> targets() const;
    void start(const std::filesystem::path &zip, const std::string &target);
    void cancel();
    CoreImportStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hd2d
