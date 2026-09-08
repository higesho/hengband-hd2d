#include "app/core_import.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>
#include <picosha2/picosha2.h>
#include <zlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace hd2d {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
std::string path_text(const fs::path &p) { auto u = p.generic_u8string(); return std::string(reinterpret_cast<const char *>(u.data()), u.size()); }

class ImportLock {
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int handle_{-1};
#endif
public:
    explicit ImportLock(const fs::path &path) {
        fs::create_directories(path.parent_path());
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Another import is running, or import storage is not writable");
#else
        handle_ = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (handle_ < 0) throw std::runtime_error("Import storage is not writable");
        if (::flock(handle_, LOCK_EX | LOCK_NB) != 0) { ::close(handle_); handle_ = -1; throw std::runtime_error("Another import is running"); }
#endif
    }
    ~ImportLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (handle_ >= 0) ::close(handle_);
#endif
    }
};

std::string read_file(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read: " + path_text(path));
    std::string result;
    char buffer[65536];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const auto count = static_cast<std::size_t>(in.gcount());
        if (count > 512ULL * 1024 * 1024 - result.size()) throw std::runtime_error("Import file exceeds 512 MiB");
        result.append(buffer, count);
    }
    if (!in.eof()) throw std::runtime_error("Cannot finish reading import file");
    return result;
}

void write_file(const fs::path &path, const std::string &data)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("Cannot write: " + path_text(path));
}

std::string sha(const std::string &data) { return picosha2::hash256_hex_string(data); }

std::string canonical(std::string data)
{
    if (data.find('\0') != std::string::npos) return data;
    if (data.compare(0, 3, "\xef\xbb\xbf") == 0) data.erase(0, 3);
    std::string out;
    out.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n') continue;
        out += data[i];
    }
    return out;
}

bool safe_name(const std::string &name)
{
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos
        || name.find(':') != std::string::npos || name.find('\0') != std::string::npos) return false;
    for (const auto &part : fs::u8path(name)) if (part == ".." || part == ".") return false;
    return true;
}

uint16_t u16(const std::string &s, std::size_t p)
{
    if (p + 2 > s.size()) throw std::runtime_error("Truncated ZIP");
    return static_cast<unsigned char>(s[p]) | (static_cast<unsigned char>(s[p + 1]) << 8);
}
uint32_t u32(const std::string &s, std::size_t p) { return u16(s, p) | (uint32_t(u16(s, p + 2)) << 16); }

// ZIP をメモリー上で読む。パス・重複・CRC・展開サイズを検証し、外部へ直接展開しない。
class SourceZip {
    struct Entry { uint32_t offset, compressed, size, crc; uint16_t method; };
    std::string data_;
    std::map<std::string, Entry> files_;
public:
    explicit SourceZip(const fs::path &path)
    {
        if (fs::file_size(path) > 512ULL * 1024 * 1024) throw std::runtime_error("Source ZIP exceeds 512 MiB");
        data_ = read_file(path);
        if (data_.size() < 22) throw std::runtime_error("Invalid ZIP");
        std::size_t end = data_.size() - 22;
        const auto lower = end > 65535 ? end - 65535 : 0;
        while (u32(data_, end) != 0x06054b50 || end + 22 + u16(data_, end + 20) != data_.size()) {
            if (end == lower) throw std::runtime_error("Invalid ZIP directory");
            --end;
        }
        if (u16(data_, end + 4) || u16(data_, end + 6) || u16(data_, end + 8) != u16(data_, end + 10))
            throw std::runtime_error("Split ZIP is not supported");
        const auto count = u16(data_, end + 10);
        if (count == 65535) throw std::runtime_error("ZIP64 is not supported");
        std::size_t pos = u32(data_, end + 16);
        const auto directory_end = pos + u32(data_, end + 12);
        if (directory_end > end) throw std::runtime_error("Invalid ZIP directory bounds");
        uint64_t total = 0;
        std::set<std::string> names;
        for (unsigned i = 0; i < count; ++i) {
            if (u32(data_, pos) != 0x02014b50) throw std::runtime_error("Invalid ZIP entry");
            auto length = u16(data_, pos + 28);
            auto next = pos + 46 + length + u16(data_, pos + 30) + u16(data_, pos + 32);
            if (next > directory_end) throw std::runtime_error("Invalid ZIP entry bounds");
            auto name = data_.substr(pos + 46, length);
            if (!safe_name(name)) throw std::runtime_error("Unsafe ZIP path");
            auto lower_name = name;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return char(c >= 'A' && c <= 'Z' ? c + 32 : c); });
            if (!names.insert(lower_name).second) throw std::runtime_error("Duplicate ZIP path");
            if ((u32(data_, pos + 38) >> 16 & 0170000) == 0120000) throw std::runtime_error("ZIP links are not supported");
            if (u16(data_, pos + 8) & 1) throw std::runtime_error("Encrypted ZIP is not supported");
            Entry e{u32(data_, pos + 42), u32(data_, pos + 20), u32(data_, pos + 24), u32(data_, pos + 16), u16(data_, pos + 10)};
            total += e.size;
            if (e.size > 64 * 1024 * 1024 || total > 1024ULL * 1024 * 1024) throw std::runtime_error("ZIP expanded size exceeds limit");
            if (e.method != 0 && e.method != 8) throw std::runtime_error("Unsupported ZIP compression");
            if (name.back() != '/') files_.emplace(name, e);
            pos = next;
        }
        if (pos != directory_end) throw std::runtime_error("ZIP directory size mismatch");
    }
    std::string prefix(const std::string &relative) const
    {
        std::string found;
        bool have = false;
        for (const auto &pair : files_) {
            const auto &name = pair.first;
            if (name == relative || (name.size() > relative.size() && name.compare(name.size() - relative.size(), relative.size(), relative) == 0 && name[name.size() - relative.size() - 1] == '/')) {
                if (have) throw std::runtime_error("More than one source tree in ZIP");
                found = name.substr(0, name.size() - relative.size()); have = true;
            }
        }
        if (!have) throw std::runtime_error("The ZIP does not contain the expected source tree");
        return found;
    }
    std::string get(const std::string &name) const
    {
        auto it = files_.find(name);
        if (it == files_.end()) throw std::runtime_error("Missing source file: " + name);
        const auto &e = it->second;
        if (u32(data_, e.offset) != 0x04034b50) throw std::runtime_error("Invalid ZIP local header");
        auto start = uint64_t(e.offset) + 30 + u16(data_, e.offset + 26) + u16(data_, e.offset + 28);
        if (start + e.compressed > data_.size()) throw std::runtime_error("Truncated ZIP data");
        std::string out;
        if (e.method == 0) out = data_.substr(static_cast<std::size_t>(start), e.compressed);
        else {
            out.resize(e.size ? e.size : 1);
            z_stream z{};
            z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data_.data() + start)); z.avail_in = e.compressed;
            z.next_out = reinterpret_cast<Bytef *>(out.data()); z.avail_out = static_cast<uInt>(out.size());
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) throw std::runtime_error("ZIP inflater failed");
            auto rc = inflate(&z, Z_FINISH);
            const auto produced = z.total_out; const auto consumed = z.total_in;
            inflateEnd(&z);
            if (rc != Z_STREAM_END || produced != e.size || consumed != e.compressed) throw std::runtime_error("Invalid compressed ZIP data");
            out.resize(e.size);
        }
        if (out.size() != e.size || crc32(0, reinterpret_cast<const Bytef *>(out.data()), static_cast<uInt>(out.size())) != e.crc) throw std::runtime_error("ZIP checksum mismatch");
        return out;
    }
};

std::string unhex(const std::string &s)
{
    if (s.size() % 2) throw std::runtime_error("Invalid patch encoding");
    std::string out;
    const auto digit = [](char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; throw std::runtime_error("Invalid patch digit"); };
    for (std::size_t i = 0; i < s.size(); i += 2) out += char(digit(s[i]) * 16 + digit(s[i + 1]));
    return out;
}

std::string patch(const std::string &original, const Json &edits)
{
    std::string out; std::size_t cursor = 0;
    for (const auto &edit : edits) {
        auto start = edit[0].get<std::size_t>(), count = edit[1].get<std::size_t>();
        if (start < cursor || start > original.size() || count > original.size() - start) throw std::runtime_error("Invalid patch offset");
        out.append(original, cursor, start - cursor); out += unhex(edit[2]); cursor = start + count;
    }
    out.append(original, cursor, original.size() - cursor);
    return out;
}

std::string substitute(std::string value, const std::map<std::string, std::string> &values)
{
    for (const auto &p : values) {
        std::size_t at = 0;
        while ((at = value.find(p.first, at)) != std::string::npos) { value.replace(at, p.first.size(), p.second); at += p.second.size(); }
    }
    return value;
}

int run_compiler(const std::vector<std::string> &args, const fs::path &log, const std::atomic<bool> &cancel)
{
#ifdef _WIN32
    const auto wide = [](const std::string &s) { return fs::u8path(s).wstring(); };
    std::wstring command;
    for (const auto &arg : args) {
        command += L'"'; unsigned slashes = 0;
        for (wchar_t c : wide(arg)) {
            if (c == L'\\') { ++slashes; continue; }
            command.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0; command += c;
        }
        command.append(slashes * 2, L'\\'); command += L"\" ";
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE file = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open compiler log");
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { CloseHandle(file); if (job) CloseHandle(job); throw std::runtime_error("Cannot create compiler job"); }
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = si.hStdError = file;
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    si.hStdInput = input;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(wide(args.front()).c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        CloseHandle(input); CloseHandle(file); CloseHandle(job); throw std::runtime_error("Cannot start bundled compiler");
    }
    CloseHandle(input); CloseHandle(file);
    if (!AssignProcessToJobObject(job, pi.hProcess)) { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job); throw std::runtime_error("Cannot manage compiler process"); }
    ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (cancel || std::chrono::steady_clock::now() > deadline) { TerminateJobObject(job, 1); WaitForSingleObject(pi.hProcess, INFINITE); break; }
    }
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(job); return static_cast<int>(code);
#else
    std::vector<char *> argv; for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str())); argv.push_back(nullptr);
    const auto file = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (file < 0) throw std::runtime_error("Cannot open compiler log");
    const auto pid = ::fork();
    if (pid == 0) {
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (::getppid() == 1) ::_exit(127);
        ::setpgid(0, 0); ::dup2(file, STDOUT_FILENO); ::dup2(file, STDERR_FILENO); ::close(file);
        ::execv(argv[0], argv.data()); ::_exit(127);
    }
    ::close(file);
    if (pid < 0) throw std::runtime_error("Cannot start bundled compiler");
    ::setpgid(pid, pid);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    int status = 0;
    for (;;) {
        const auto waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0 && errno != EINTR) return 1;
        if (cancel || std::chrono::steady_clock::now() > deadline) { ::kill(-pid, SIGKILL); ::waitpid(pid, &status, 0); return 1; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}
} // namespace

struct CoreImport::Impl {
    CoreImportConfig config;
    mutable std::mutex mutex;
    CoreImportStatus current;
    std::atomic<bool> cancelled{false};
    std::thread worker;
    explicit Impl(CoreImportConfig c) : config(std::move(c)) {}
    void report(std::string phase, std::string message, int completed = 0, int total = 0) {
        std::lock_guard<std::mutex> lock(mutex);
        if (phase == current.phase && total == current.total) completed = std::max(completed, current.completed);
        current.phase = std::move(phase); current.message = std::move(message); current.completed = completed; current.total = total;
    }
    void check_cancel() const { if (cancelled) throw std::runtime_error("Cancelled"); }
    void execute(const fs::path &zip_path, const std::string &id) {
        fs::path work;
        try {
            ImportLock lock(config.storage / "import.lock");
            const auto registry_path = config.storage / "registry.json";
            Json registry = Json::object();
            try {
                if (fs::exists(registry_path)) registry = Json::parse(read_file(registry_path));
                if (!registry.is_object()) throw std::runtime_error("Expected an object");
            } catch (const std::exception &) {
                throw std::runtime_error("Core registry is damaged: " + path_text(registry_path));
            }
            auto catalog = Json::parse(read_file(config.kit / "catalog.json"));
            const auto item = catalog.at("targets").at(id);
            if (!safe_name(id)) throw std::runtime_error("Invalid core ID");
            const auto recipe_name = item.at("recipe").get<std::string>();
            if (!safe_name(recipe_name)) throw std::runtime_error("Invalid recipe path");
            auto bytes = read_file(config.kit / recipe_name);
            if (sha(bytes) != item.at("sha256").get<std::string>()) throw std::runtime_error("Import recipe checksum mismatch");
            auto recipe = Json::parse(bytes);
            if (recipe.at("platform") != config.platform) throw std::runtime_error("Import compiler platform mismatch");
            if (!fs::is_regular_file(config.compiler)) throw std::runtime_error("Bundled compiler is missing. Reinstall the import-enabled package.");
            report("verify", "Checking import support files");
            if (catalog.contains("sdk_sha256")) {
                for (auto it = catalog["sdk_sha256"].begin(); it != catalog["sdk_sha256"].end(); ++it) {
                    check_cancel();
                    if (!safe_name(it.key()) || sha(read_file(config.kit / it.key())) != it.value().get<std::string>()) throw std::runtime_error("Import support file checksum mismatch: " + it.key());
                }
            }
            report("verify", "Checking source ZIP");
            SourceZip zip(zip_path);
            const auto prefix = zip.prefix(recipe.at("probe"));
            auto session = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            work = config.storage / "work" / (id + "-" + session);
            fs::create_directories(work.parent_path());
            if (!fs::create_directory(work)) throw std::runtime_error("Cannot create import workspace");
            const auto sources = work / "source";
            int completed = 0;
            for (auto it = recipe["files"].begin(); it != recipe["files"].end(); ++it) {
                check_cancel();
                if (!safe_name(it.key())) throw std::runtime_error("Unsafe recipe path");
                const auto &spec = it.value();
                std::string original;
                if (!spec["input"].is_null()) {
                    const auto input = spec["input"].get<std::string>();
                    if (!safe_name(input)) throw std::runtime_error("Unsafe source path");
                    original = canonical(zip.get(prefix + input));
                    if (sha(original) != spec["input_sha256"].get<std::string>()) throw std::runtime_error("Unsupported or modified source version: " + input);
                }
                auto result = patch(original, spec["edits"]);
                if (sha(result) != spec["sha256"].get<std::string>()) throw std::runtime_error("Patched source checksum mismatch: " + it.key());
                write_file(sources / fs::u8path(it.key()), result);
                report("verify", it.key(), ++completed, static_cast<int>(recipe["files"].size()));
            }
            const auto output_name = recipe.at("output").get<std::string>();
            if (!safe_name(output_name) || fs::u8path(output_name).has_parent_path()) throw std::runtime_error("Invalid core output name");
            const auto output = work / output_name;
            std::map<std::string, std::string> values{{"{source}", path_text(sources)}, {"{kit}", path_text(config.kit)}, {"{work}", path_text(work)}, {"{output}", path_text(output)}, {"{compiler}", path_text(config.compiler)}, {"{linker}", path_text(config.linker)}, {"{native}", path_text(config.compiler.parent_path())}};
            if (recipe.contains("responses")) {
                for (auto it = recipe["responses"].begin(); it != recipe["responses"].end(); ++it) {
                    if (!safe_name(it.key())) throw std::runtime_error("Unsafe response path");
                    write_file(work / it.key(), substitute(it.value(), values));
                }
            }
            const auto &commands = recipe["commands"];
            if (commands.empty()) throw std::runtime_error("Empty build recipe");
            const auto compile_count = commands.size() - 1;
            std::atomic<std::size_t> next{0}, finished{0};
            std::atomic<bool> failed{false};
            std::mutex failure_mutex;
            std::string failure;
            auto execute_command = [&](std::size_t index) {
                std::vector<std::string> args;
                for (const auto &arg : commands[index]) args.push_back(substitute(arg, values));
                if (args.empty() || (args.front() != path_text(config.compiler) && args.front() != path_text(config.linker))) throw std::runtime_error("Invalid compiler command");
                if (run_compiler(args, work / "build.log", cancelled) != 0) throw std::runtime_error("Build failed. Log: " + path_text(work / "build.log"));
            };
            auto compile = [&] {
                try {
                    while (!failed && !cancelled) {
                        const auto index = next.fetch_add(1);
                        if (index >= compile_count) break;
                        execute_command(index);
                        report("build", "Compiling", static_cast<int>(++finished), static_cast<int>(commands.size()));
                    }
                } catch (const std::exception &e) { failed = true; std::lock_guard<std::mutex> lock(failure_mutex); failure = e.what(); }
            };
            report("build", "Compiling", 0, static_cast<int>(commands.size()));
            std::vector<std::thread> workers;
            const int concurrency = config.platform == "windows" ? 4 : 2;
            for (int i = 0; i < concurrency; ++i) workers.emplace_back(compile);
            for (auto &thread : workers) thread.join();
            check_cancel();
            if (failed) throw std::runtime_error(failure);
            report("build", "Linking", static_cast<int>(compile_count), static_cast<int>(commands.size()));
            execute_command(compile_count);
            check_cancel();
            auto binary = read_file(output);
            const bool valid = config.platform == "windows" ? binary.compare(0, 2, "MZ") == 0 : binary.compare(0, 4, "\x7f" "ELF") == 0;
            if (!valid || binary.size() < 4096) throw std::runtime_error("Compiler output is not a core executable");
            if (config.platform == "windows") {
                const auto pe = u32(binary, 0x3c);
                if (u32(binary, pe) != 0x4550 || u16(binary, pe + 4) != 0x14c || (u16(binary, pe + 22) & 0x2000)) throw std::runtime_error("Expected a Windows x86 core executable");
            } else if (binary[4] != 2 || binary[5] != 1 || u16(binary, 16) != 3 || u16(binary, 18) != 183) {
                throw std::runtime_error("Expected an Android arm64 shared library");
            }
            report("install", "Registering core");
            const auto destination = config.storage / "installed" / (id + "-" + session);
            fs::create_directories(destination.parent_path());
            if (!fs::create_directory(destination)) throw std::runtime_error("Cannot create core installation directory");
            const auto installed = destination / output.filename();
            fs::rename(output, installed);
            fs::permissions(installed, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
            registry[id] = {{"path", path_text(installed)}, {"name", item.at("name")}, {"version", item.at("version")}, {"sha256", sha(binary)}};
            const auto temp = config.storage / "registry.json.tmp";
            write_file(temp, registry.dump(2));
#ifdef _WIN32
            if (!MoveFileExW(temp.c_str(), registry_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot register imported core");
#else
            fs::rename(temp, registry_path);
#endif
            { std::lock_guard<std::mutex> lock(mutex); current.succeeded = true; current.core_path = path_text(installed); }
            report("complete", "Core imported");
        } catch (const std::exception &e) { report(cancelled ? "cancelled" : "error", e.what()); }
        // 自分で作った作業ディレクトリだけを掃除する。入力 ZIP とログは削除しない。
        try { if (!work.empty()) {
            std::error_code ec;
            const auto parent = fs::weakly_canonical(config.storage / "work", ec);
            if (!ec && fs::weakly_canonical(work, ec).parent_path() == parent && !ec) {
                fs::remove_all(work / "source", ec);
                ec.clear();
                for (const auto &entry : fs::directory_iterator(work, ec)) {
                    if (entry.path().extension() == ".o" || entry.path().filename() == "link.rsp") fs::remove(entry.path(), ec);
                }
            }
        } } catch (const std::exception &) { /* 作業用ファイルが残っても、登録結果は変えない。 */ }
        std::lock_guard<std::mutex> lock(mutex); current.running = false;
    }
};

CoreImport::CoreImport(CoreImportConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
CoreImport::~CoreImport() { cancel(); if (impl_->worker.joinable()) impl_->worker.join(); }
void CoreImport::cancel() { impl_->cancelled = true; }
CoreImportStatus CoreImport::status() const { std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->current; }
std::vector<ImportTarget> CoreImport::targets() const {
    std::vector<ImportTarget> result;
    auto catalog = Json::parse(read_file(impl_->config.kit / "catalog.json"));
    for (auto it = catalog.at("targets").begin(); it != catalog.at("targets").end(); ++it) result.push_back({it.key(), it.value().at("name"), it.value().at("version")});
    return result;
}
void CoreImport::start(const fs::path &zip, const std::string &target) {
    if (status().running) throw std::runtime_error("An import is already running");
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->cancelled = false;
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->current = {}; impl_->current.running = true; }
    impl_->worker = std::thread([this, zip, target] { impl_->execute(zip, target); });
}
} // namespace hd2d
