/*!
 * @file core_link.cpp
 * @brief `core_link.h` の実装。
 *
 * Windows は**子プロセス**（`CreateProcess`＋パイプ 3 本）、非 Windows（Android）は
 * **同一プロセスのコアスレッド**。
 * どちらも Impl・送受信・受信スレッドの振り分けは**共通**で、OS 依存は
 * 「起こし方」「生死の見方」「畳み方」の 3 点に閉じている。
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <ctime>
#include <poll.h>
#include <unistd.h>
#endif

#include "net/core_link.h"

#include "frame/frame_codec.h" //!< 捨てるフレームから `sounds` だけを拾う（v1 §6.4 の救済）
#include "frame/protocol_transport.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

namespace hd2d {

namespace {

//! 握手だけを待つ上限（v1 §3.1）。`quit_request` 後の猶予とは別物。
constexpr unsigned kHandshakeTimeoutMs = 30000;
//! 「stdin を閉じたのでコアが緊急セーブする」を待つ上限（`force_quit` の 1 段目）。
constexpr unsigned kPanicSaveGraceMs = 15000;

#if defined(_WIN32)

void close_handle(HANDLE &handle)
{
    if ((handle != nullptr) && (handle != INVALID_HANDLE_VALUE)) {
        ::CloseHandle(handle);
    }
    handle = nullptr;
}

//! コマンドラインへ 1 引数を足す（空白か `"` を含むものだけ引用符で包む）。
void append_arg(std::string &command, const std::string &arg)
{
    command += ' ';
    if ((arg.find(' ') == std::string::npos) && (arg.find('"') == std::string::npos)) {
        command += arg;
        return;
    }
    command += '"';
    for (const char c : arg) {
        if (c == '"') {
            command += '\\';
        }
        command += c;
    }
    command += '"';
}

std::filesystem::path exe_directory()
{
    char buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0) {
        return std::filesystem::path();
    }
    return std::filesystem::path(std::string(buf, len)).parent_path();
}

#else /* !_WIN32 */

//! 端点（`transport_from_fd` が fd + 1 を詰めた void*）を閉じる。
void close_endpoint(presentation::TransportHandle &handle)
{
    if (handle != nullptr) {
        const int fd = static_cast<int>(reinterpret_cast<intptr_t>(handle)) - 1;
        (void)::close(fd);
        handle = nullptr;
    }
}

#endif /* _WIN32 */

/*!
 * @brief 会話ログ（v1 §11.2）。**`core_main.cpp` と同じ 1 行の形**。
 * @details `{"time","dir","t","len","payload"}`。書式を揃えてあるのは、2 本のログを
 * 時刻で突き合わせて読むためなので、1 文字も変えない。
 */
class ProtocolLog {
public:
    void open(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb");
        if (this->fp_ == nullptr) {
            std::fprintf(stderr, "[hd2d] could not open the protocol log: %s\n", path.c_str());
        }
    }

    void record(const char *dir, const std::string &type, const std::string &payload)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->fp_ == nullptr) {
            return;
        }
        std::string line = "{\"time\":\"";
        line += iso_now();
        line += "\",\"dir\":\"";
        line += dir;
        line += "\",\"t\":\"";
        append_escaped(line, type);
        line += "\",\"len\":";
        line += std::to_string(payload.size());
        line += ",\"payload\":\"";
        append_escaped(line, payload);
        line += "\"}\n";
        (void)std::fwrite(line.data(), 1, line.size(), this->fp_);
        (void)std::fflush(this->fp_);
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->fp_ != nullptr) {
            std::fclose(this->fp_);
            this->fp_ = nullptr;
        }
    }

private:
    static std::string iso_now()
    {
#if defined(_WIN32)
        SYSTEMTIME st{};
        ::GetSystemTime(&st);
        char buf[40]{};
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return std::string(buf);
#else
        const auto now = std::chrono::system_clock::now();
        const std::time_t sec = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
        (void)gmtime_r(&sec, &tm);
        char buf[40]{};
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
        return std::string(buf);
#endif
    }

    static void append_escaped(std::string &out, const std::string &text)
    {
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(byte));
                    out += buf;
                } else {
                    out += c;
                }
                break;
            }
        }
    }

    std::mutex mutex_;
    std::FILE *fp_{ nullptr };
};

#if defined(_WIN32)
/*!
 * @brief コアの stderr を素通ししつつ、全文をログへ、直近数百行を手元へ残す（v1 §13-4）。
 * @details 窓だけ見ている利用者に「コンソールから起動してください」と言うのは落ちた後では
 * 手遅れなので、**理由そのものを持ってこられる**ようにしておく。
 * @note 非 Windows（1 プロセス）ではコアの stderr は自分の stderr そのもの
 * （Android は logcat へ流れる）なので、この中継は要らない。
 */
class CoreStderrRelay {
public:
    static constexpr std::size_t kTailLines = 200;

    void open_log()
    {
        char dir[MAX_PATH]{};
        const DWORD len = ::GetTempPathA(MAX_PATH, dir);
        std::string path = ((len > 0) && (len < MAX_PATH)) ? std::string(dir, len) : std::string();
        path += "hengband-hd2d-core-stderr.log";
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->fp_ = std::fopen(path.c_str(), "wb"); //!< "wb" = 毎起動 truncate（溜め込まない）
        if (this->fp_ != nullptr) {
            this->log_path_ = std::move(path);
        }
    }

    std::string log_path()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        return this->log_path_;
    }

    //! パイプが EOF になるまで読み続ける。**専用スレッドから呼ぶこと。**
    void run(HANDLE pipe)
    {
        char buf[4096];
        std::string partial;
        for (;;) {
            DWORD read = 0;
            if ((::ReadFile(pipe, buf, sizeof(buf), &read, nullptr) == FALSE) || (read == 0)) {
                break;
            }
            (void)std::fwrite(buf, 1, read, stderr);
            (void)std::fflush(stderr);
            {
                std::lock_guard<std::mutex> lock(this->mutex_);
                if (this->fp_ != nullptr) {
                    (void)std::fwrite(buf, 1, read, this->fp_);
                    (void)std::fflush(this->fp_);
                }
            }
            for (DWORD i = 0; i < read; ++i) {
                const char c = buf[i];
                if (c == '\n') {
                    this->push_line(std::move(partial));
                    partial.clear();
                } else if (c != '\r') {
                    partial.push_back(c);
                }
            }
        }
        if (!partial.empty()) {
            this->push_line(std::move(partial));
        }
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->fp_ != nullptr) {
            std::fclose(this->fp_);
            this->fp_ = nullptr;
        }
    }

    std::string tail_text(std::size_t count)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        std::string out;
        const std::size_t from = (this->tail_.size() > count) ? (this->tail_.size() - count) : 0;
        for (std::size_t i = from; i < this->tail_.size(); ++i) {
            out += this->tail_[i];
            out += '\n';
        }
        return out;
    }

private:
    void push_line(std::string line)
    {
        if (line.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->tail_.push_back(std::move(line));
        while (this->tail_.size() > kTailLines) {
            this->tail_.pop_front();
        }
    }

    std::mutex mutex_;
    std::deque<std::string> tail_;
    std::FILE *fp_{ nullptr };
    std::string log_path_;
};
#endif /* _WIN32 */

} // namespace

/* ============================================================================ */

//! OS の持ち物ぜんぶ。ヘッダに `windows.h` を持ち込まないための隠し場所。
struct CoreLink::Impl {
    presentation::TransportHandle to_core{ nullptr }; //!< ui が書く側（コアの stdin 相当）
    presentation::TransportHandle from_core{ nullptr }; //!< ui が読む側（コアの stdout 相当）
#if defined(_WIN32)
    HANDLE process{ nullptr };
    HANDLE thread{ nullptr };
    CoreStderrRelay stderr_relay;
#endif

    std::mutex send_mutex;
    ProtocolLog log;

    std::atomic<bool> core_gone{ false };
    std::atomic<bool> exit_received{ false };
    std::atomic<int> exit_code{ 0 };
    std::atomic<bool> fatal_received{ false };

    std::mutex inbox_mutex;
    std::string latest_frame; //!< 最新値スロット（溜めない。v1 §6.4）
    bool has_new_frame{ false };
    /*!
     * @brief 捨てたフレームに `teleport_fx.burst` が立っていた。
     * @details v1 §6.4 は「`burst` は立ち上がり 1 フレームの縁なので合体で潰すな」と
     * **送信側**に課している。受け手が最新値スロットで古いフレームを捨てるとその縁が
     * 消えるので、捨てるときに拾って次へ持ち越す。
     */
    bool dropped_burst{ false };
    /*!
     * @brief 捨てたフレームに載っていた効果音（v1 §6.4 の救済。`dropped_burst` と同じ理由）。
     * @details 効果音は**そのフレームにしか載らない一度きりの縁**である。最新値スロットで
     * 古いフレームを捨てると鳴らずに消える——1 回のキーでコアが何枚もフレームを出すのは
     * 普通なので、**ほとんどの音が消える**（2026-08-21 に気づいた
     * 「効果音がたまにしかならなくなった」）。捨てる前にここへ移しておく。
     */
    std::vector<SoundEvent> carried_sounds;
    std::deque<std::string> table_payloads;
    std::string fatal_reason;


    /*!
     * @brief 受け取った 1 通を種別で振り分ける。
     * @details 受信の環からも**再生モードからも**ここを通す。写して 2 か所に
     * すると、表を足したとき片方だけ直して「届いているのに誰も読まない」に
     * なる（この関数の中の註記にあるとおり、実際に踏んだ罠である）。
     */
    void dispatch(std::string payload)
    {
        std::string err;
        const std::string type = presentation::peek_message_type(payload);

        if (type == "frame") {
            std::lock_guard<std::mutex> lock(this->inbox_mutex);
            if (this->has_new_frame) {
                if (this->latest_frame.find("\"burst\":true") != std::string::npos) {
                    this->dropped_burst = true;
                }
                /*
                 * **捨てるフレームの効果音を拾う。**鍵の有無で先に篩ってから解く
                 * （フレームは大きく、音が載っているのは稀なので、毎回解くのは高い）。
                 */
                if (this->latest_frame.find("\"sounds\":") != std::string::npos) {
                    std::string sound_err;
                    if (!presentation::decode_frame_sounds(this->latest_frame, this->carried_sounds, sound_err)) {
                        std::fprintf(stderr, "[hd2d] 捨てたフレームの音を読めません: %s\n", sound_err.c_str());
                    }
                }
            }
            this->latest_frame = std::move(payload);
            this->has_new_frame = true;
            return;
        }
        //! **表を足したらここにも足す。**忘れると届いているのに誰も読まない（実際に踏んだ）。
        if ((type == "pad_commands") || (type == "sub_panel_kinds") || (type == "asset_manifest")
            || (type == "macro_triggers")) {
            std::lock_guard<std::mutex> lock(this->inbox_mutex);
            this->table_payloads.push_back(std::move(payload));
            return;
        }
        if (type == "exit") {
            presentation::ExitMessage bye;
            if (presentation::decode_exit(payload, bye, err)) {
                this->exit_code.store(bye.code);
            } else {
                std::fprintf(stderr, "[hd2d] bad \"exit\": %s\n", err.c_str());
            }
            this->exit_received.store(true);
            return; // コアはこの後畳む。EOF まで読み続けてよい
        }
        if (type == "fatal") {
            presentation::FatalMessage fatal;
            if (presentation::decode_fatal(payload, fatal, err)) {
                std::lock_guard<std::mutex> lock(this->inbox_mutex);
                this->fatal_reason = fatal.reason;
            }
            this->fatal_received.store(true);
            return;
        }
        // 未知の種別は黙って捨てる（v1 §2.3）。
    }


    /* ------------------------------------------------- 再生モード（`HD2D_REPLAY_LOG`）*/

    /*!
     * @brief 記録した会話から配る（コアを起こさない）。
     *
     * @details `run()` を作り直すとき、**同じ入力を必ず同じ順で与える**ための仕掛け。
     * 本物のコアは別プロセスで実時間に動くので、どの周までに何通届くかが走らせるたびに
     * 変わる。ここでは `--protocol-log=` で採った記録を読み、**1 周につき 1 通**配る。
     *
     * 本物の走りと同じ配られ方にはならない（本物は 1 周に何通も来ることがある）。
     * それでよい——ここで欲しいのは**再生どうしが必ず一致すること**であって、
     * 本物の再現ではない。
     */
    bool replay{ false };
    //! 記録から拾ったコア発の中身（種別は中身から引けるので持たない）。
    std::vector<std::string> replay_in;
    //! 次に配る番号。
    std::size_t replay_at{ 0 };
    //! 配り終えたあと、絵が落ち着くまで回す周の数。
    static constexpr int kReplayGraceLoops = 120;
    int replay_grace{ 0 };
    //! 記録にあった `hello_ack` の中身。
    std::string replay_ack;

    /*!
     * @brief 会話の記録（JSON Lines）を読み、コア発の中身だけを採る。
     * @details 1 行は `{"time":...,"dir":"in","t":"frame","len":N,"payload":"..."}`。
     * `payload` は JSON の字として入っているので、そこだけを解いて元へ戻す。
     */
    bool load_replay(const std::string &path, std::string &err)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            err = "会話の記録を開けません: " + path;
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            if (line.find("\"dir\":\"in\"") == std::string::npos) {
                continue; //!< 画面が送ったぶんは配らない
            }
            const std::size_t key = line.find("\"payload\":\"");
            if (key == std::string::npos) {
                continue;
            }
            //! `payload` の字を解く（`\"` と `\\` だけ戻せばよい。中身は JSON なので制御文字は入らない）。
            std::string body;
            for (std::size_t i = key + 11; i < line.size(); ++i) {
                const char c = line[i];
                if (c == '\\') {
                    if ((i + 1) >= line.size()) {
                        break;
                    }
                    const char n = line[i + 1];
                    ++i;
                    if (n == 'n') {
                        body.push_back('\n');
                    } else if (n == 't') {
                        body.push_back('\t');
                    } else if (n == 'r') {
                        body.push_back('\r');
                    } else if (n == 'u') {
                        i += 4; //!< 使っていないので読み飛ばす
                    } else {
                        body.push_back(n);
                    }
                    continue;
                }
                if (c == '"') {
                    break; //!< 中身の終わり
                }
                body.push_back(c);
            }
            if (body.empty()) {
                continue;
            }
            const std::string kind = presentation::peek_message_type(body);
            if (kind == "hello_ack") {
                this->replay_ack = body;
                continue; //!< 握手で使う。環では配らない
            }
            /*
             * **`exit` と `fatal` は配らない。**記録の末尾に載っているのは
             * 「撮り終えて画面を畳んだ」という**記録した側の都合**であって、
             * 遊びの中身ではない。配ると再生がそこで終わってしまい、
             * 最後のフレームを描く前に畳んで**世界の絵ではなく途中の絵**を撮る
             * （実際にそうなって、絵が 1 枚も撮れなかった）。
             * 終わりは `replay_step()` の猶予が決める。
             */
            if ((kind == "exit") || (kind == "fatal")) {
                continue;
            }
            this->replay_in.push_back(std::move(body));
        }
        if (this->replay_ack.empty()) {
            err = "記録に hello_ack がありません: " + path;
            return false;
        }
        std::fprintf(stderr, "[hd2d] 再生: %s から %zu 通（コア発）\n",
            path.c_str(), this->replay_in.size());
        return true;
    }

    /*!
     * @brief 1 周ぶん配る。配り終えても**すぐには終わらせない**。
     *
     * @details 最後の 1 通を配ったその周では、まだ描き終わっていない。すぐ
     * 「コアが終わった」に倒すと、**最後のフレームを 1 度も描かないまま**畳んで
     * しまう（実際にそうなって、世界の絵ではなく題名画面を撮っていた）。
     * 配り終えてから `kReplayGraceLoops` 周ぶん回して、絵が落ち着いてから終わる。
     */
    void replay_step()
    {
        if (!this->replay) {
            return;
        }
        if (this->replay_at < this->replay_in.size()) {
            this->dispatch(this->replay_in[this->replay_at++]);
            return;
        }
        if (this->replay_grace < kReplayGraceLoops) {
            ++this->replay_grace;
            return;
        }
        if (!this->exit_received.load()) {
            std::fprintf(stderr, "[hd2d] 再生: 記録を配り終えました（%zu 通）\n", this->replay_at);
            this->exit_received.store(true);
        }
    }

    //! 1 メッセージ送る。失敗したら「コア消滅」に倒す（v1 §9.2）。
    bool send(const char *type, const std::string &payload)
    {
        std::lock_guard<std::mutex> lock(this->send_mutex);
        //! 再生モードでは送り先が居ない。**記録には残す**——画面が何を送ったかが
        //! 突き合わせの当のものだから。送れたことにして先へ進める。
        if (this->replay) {
            this->log.record("out", type, payload);
            return true;
        }
        if (this->to_core == nullptr) {
            return false;
        }
        std::string err;
        if (!presentation::write_message(this->to_core, payload, err)) {
            std::fprintf(stderr, "[hd2d] send \"%s\" failed: %s\n", type, err.c_str());
            this->core_gone.store(true);
            return false;
        }
        this->log.record("out", type, payload);
        return true;
    }

    void receive_loop()
    {
        for (;;) {
            std::string payload;
            std::string err;
            const auto result = presentation::read_message(this->from_core, payload, err);
            if (result != presentation::TransportResult::Ok) {
                if (result != presentation::TransportResult::Eof) {
                    std::fprintf(stderr, "[hd2d] receive stopped: %s%s%s\n",
                        presentation::transport_result_name(result), err.empty() ? "" : " - ", err.c_str());
                }
                this->core_gone.store(true);
                return;
            }

            const std::string type = presentation::peek_message_type(payload);
            this->log.record("in", type, payload);
            this->dispatch(std::move(payload));
        }
    }
};

/* ============================================================================ */

CoreLink::~CoreLink()
{
    this->shutdown();
}

#if !defined(_WIN32)

/*!
 * @brief 非 Windows 版の `start`。子プロセスの代わりに**コアスレッド**を起こす。
 * @details パイプ 2 組（ui→core / core→ui）を作り、`options.core_thread_main`
 * （platform 層が差した `hengband_core_protocol_main`）へコア側の端を渡す。
 * 「stdin を閉じる＝ui 消滅の合図」（v1 §9.2）は fd でも同じに保たれる。
 */
bool CoreLink::start(const CoreLinkOptions &options, std::string &err)
{
    this->impl_ = new Impl();
    Impl &impl = *this->impl_;

    if (!options.protocol_log_path.empty()) {
        impl.log.open(options.protocol_log_path);
    }

    if (options.core_thread_main == nullptr) {
        err = "コアスレッドの入口が結線されていません（platform 層の設定漏れ）。";
        return false;
    }

    int ui_to_core[2]{ -1, -1 };
    int core_to_ui[2]{ -1, -1 };
    if (::pipe(ui_to_core) != 0) {
        err = "コアとの通信路（パイプ）を作れませんでした。";
        return false;
    }
    if (::pipe(core_to_ui) != 0) {
        err = "コアとの通信路（パイプ）を作れませんでした。";
        (void)::close(ui_to_core[0]);
        (void)::close(ui_to_core[1]);
        return false;
    }

    impl.to_core = presentation::transport_from_fd(ui_to_core[1]);
    impl.from_core = presentation::transport_from_fd(core_to_ui[0]);
    const presentation::TransportHandle core_in = presentation::transport_from_fd(ui_to_core[0]);
    const presentation::TransportHandle core_out = presentation::transport_from_fd(core_to_ui[1]);

    /*
     * コアスレッド。detach する: 正常終了は `quit()` → `std::exit()`（プロセスごと畳む。
     * ）なので、join する機会がそもそも無い。
     */
    const auto entry = options.core_thread_main;
    const std::string core_log = options.core_protocol_log_path;
    const std::vector<std::string> forwarded = options.forwarded_args;
    std::thread([entry, core_in, core_out, core_log, forwarded]() {
        const int rc = entry(core_in, core_out, core_log, forwarded);
        // 来るとすれば握手前の異常だけ（正常系は exit で戻らない）。理由は stderr に出ている。
        std::fprintf(stderr, "[hd2d] the core thread returned early (%d)\n", rc);
    }).detach();
    std::fprintf(stderr, "[hd2d] started the in-process core thread\n");
    return true;
}

#else /* _WIN32 */

bool CoreLink::start(const CoreLinkOptions &options, std::string &err)
{
    this->impl_ = new Impl();
    Impl &impl = *this->impl_;

    if (!options.protocol_log_path.empty()) {
        impl.log.open(options.protocol_log_path);
    }

    /*
     * **再生モード。**`HD2D_REPLAY_LOG` に会話の記録を渡すと、コアを起こさずに
     * その記録から配る（`Impl::load_replay` / `Impl::replay_step`）。
     * `run()` を作り直すとき、同じ入力を必ず同じ順で与えるための仕掛けである。
     * 実物のコアは別プロセスで実時間に動くので、どの周までに何通届くかが
     * 走らせるたびに変わり、前後の突き合わせにならない。
     */
    if (const char *const replay_path = std::getenv("HD2D_REPLAY_LOG")) {
        if (replay_path[0] != '\0') {
            impl.replay = true;
            if (!impl.load_replay(replay_path, err)) {
                return false;
            }
            return true; //!< コアは起こさない
        }
    }

    impl.stderr_relay.open_log();

    std::filesystem::path core_path = options.core_path;
    if (core_path.empty()) {
        core_path = exe_directory() / "HengbandCore.exe";
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(core_path, ec)) {
        /*
         * **名前を決め打ちしない**。
         * コアは選べるようになったので、`HengbandCore.exe` と書くと
         * 幻想蛮怒を選んで失敗したときに嘘の名前が出る。実際に探した道を出す。
         */
        err = "ゲームコアが見つかりません:\n" + core_path.string();
        return false;
    }

    // 無名パイプ 3 本。子へ渡す側だけ継承可にし、親が持つ側は継承を落とす
    // （落とさないと子が自分の書き込み端を掴んだままになり、EOF が来ない）。
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdout_write = nullptr;
    HANDLE child_stderr_write = nullptr;
    HANDLE core_stderr_read = nullptr;
    const bool pipes_ok = (::CreatePipe(&child_stdin_read, &impl.to_core, &sa, 0) != FALSE)
        && (::CreatePipe(&impl.from_core, &child_stdout_write, &sa, 0) != FALSE)
        && (::CreatePipe(&core_stderr_read, &child_stderr_write, &sa, 0) != FALSE);
    if (!pipes_ok) {
        err = "コアとの通信路（パイプ）を作れませんでした。";
        close_handle(child_stdin_read);
        close_handle(child_stdout_write);
        close_handle(child_stderr_write);
        close_handle(core_stderr_read);
        return false;
    }
    ::SetHandleInformation(impl.to_core, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(impl.from_core, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(core_stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stderr_write;

    std::string command = "\"" + core_path.string() + "\" --ui-protocol=stdio";
    if (!options.core_protocol_log_path.empty()) {
        append_arg(command, "--protocol-log=" + options.core_protocol_log_path);
    }
    for (const auto &arg : options.forwarded_args) {
        append_arg(command, arg);
    }
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');

    PROCESS_INFORMATION pi{};
    const BOOL started = ::CreateProcessA(
        core_path.string().c_str(),
        mutable_command.data(),
        nullptr, nullptr,
        TRUE, //!< ハンドル継承（パイプを渡すため）
        CREATE_NO_WINDOW, //!< コンソール窓を出さない
        nullptr, //!< 環境は継承（INJECT_FILE / BOT_JSON がそのままコアへ届く）
        nullptr, //!< cwd も継承（`bot-state.jsonl` などの相対パスを壊さない）
        &si, &pi);
    // 子に渡した端は親では要らない。持ち続けると子が死んでも EOF にならず永久に待つ。
    close_handle(child_stdin_read);
    close_handle(child_stdout_write);
    close_handle(child_stderr_write);
    if (started == FALSE) {
        err = "ゲームコアを起動できませんでした:\n" + core_path.string();
        close_handle(core_stderr_read);
        return false;
    }
    impl.process = pi.hProcess;
    impl.thread = pi.hThread;
    std::fprintf(stderr, "[hd2d] started %s (pid %lu)\n",
        core_path.string().c_str(), static_cast<unsigned long>(pi.dwProcessId));

    // stderr の中継。detach する: 終わりは子の消滅＝パイプの EOF で、こちらから止める
    // 手段も理由も無い（読み残しを作らないほうが大事）。
    CoreStderrRelay *const relay = &impl.stderr_relay;
    std::thread([relay, core_stderr_read]() { relay->run(core_stderr_read); }).detach();
    return true;
}

#endif /* _WIN32 */

bool CoreLink::handshake(std::string &err)
{
    Impl &impl = *this->impl_;

    //! 再生モードでは記録にあった `hello_ack` をそのまま使う（コアは居ない）。
    if (impl.replay) {
        if (!presentation::decode_hello_ack(impl.replay_ack, this->ack_, err)) {
            err = "記録の hello_ack を読めません: " + err;
            return false;
        }
        return true;
    }

    presentation::HelloMessage hello;
    hello.protocol = presentation::kProtocolVersion;
    hello.ui_name = "hengband-voxel-hd2d";
    hello.ui_version = "0.1.0";
    if (!impl.send("hello", presentation::encode_hello(hello))) {
        err = "コアへ hello を送れませんでした。";
        return false;
    }

#if defined(_WIN32)
    const ULONGLONG deadline = ::GetTickCount64() + kHandshakeTimeoutMs;
#else
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHandshakeTimeoutMs);
#endif
    for (;;) {
#if defined(_WIN32)
        DWORD avail = 0;
        if (::PeekNamedPipe(impl.from_core, nullptr, 0, nullptr, &avail, nullptr) == FALSE) {
            err = "握手の途中でゲームコアがパイプを閉じました。";
            return false;
        }
        if (avail == 0) {
            if (::WaitForSingleObject(impl.process, 0) == WAIT_OBJECT_0) {
                err = "握手が終わる前にゲームコアが終了しました。";
                return false;
            }
            if (::GetTickCount64() >= deadline) {
                err = "ゲームコアが時間内に握手へ応じませんでした。";
                return false;
            }
            ::Sleep(5);
            continue;
        }
#else
        // poll で「読めるようになるまで」待つ。POLLHUP（コア側が閉じた）は
        // そのまま読みに行けば read_message が Eof を返すので、ここでは区別しない。
        struct pollfd pf {};
        pf.fd = static_cast<int>(reinterpret_cast<intptr_t>(impl.from_core)) - 1;
        pf.events = POLLIN;
        const int rv = ::poll(&pf, 1, 50);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            err = "握手の途中でパイプの監視に失敗しました。";
            return false;
        }
        if (rv == 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                err = "ゲームコアが時間内に握手へ応じませんでした。";
                return false;
            }
            continue;
        }
#endif

        std::string payload;
        std::string read_err;
        const auto got = presentation::read_message(impl.from_core, payload, read_err);
        if (got != presentation::TransportResult::Ok) {
            err = std::string("hello_ack を読めませんでした: ") + presentation::transport_result_name(got);
            return false;
        }
        const std::string type = presentation::peek_message_type(payload);
        impl.log.record("in", type, payload);
        if (type == "fatal") {
            presentation::FatalMessage fatal;
            (void)presentation::decode_fatal(payload, fatal, read_err);
            err = "ゲームコアが続行不能を報告しました:\n" + fatal.reason;
            return false;
        }
        if (type != "hello_ack") {
            // 握手の前に他の種別は来ない（v1 §3.1）。§2.3 の「黙って捨てる」は
            // 握手の**後**の規則なので、ここでは異常として扱う。
            err = "ゲームコアが握手に応じませんでした（受け取った種別: \"" + type + "\"）。";
            return false;
        }
        if (!presentation::decode_hello_ack(payload, this->ack_, read_err)) {
            err = "hello_ack を解釈できませんでした: " + read_err;
            return false;
        }
        std::fprintf(stderr, "[hd2d] hello_ack: protocol=%d core=\"%s\" %s graf=%s\n",
            this->ack_.protocol, this->ack_.core_name.c_str(), this->ack_.core_version.c_str(),
            this->ack_.asset_root_graf.empty() ? "<not declared>" : this->ack_.asset_root_graf.c_str());
        if (this->ack_.protocol != presentation::kProtocolVersion) {
            char buf[192]{};
            std::snprintf(buf, sizeof(buf),
                "プロトコルの版が食い違っています（コア %d / このフロントエンド %d）。",
                this->ack_.protocol, presentation::kProtocolVersion);
            err = buf;
            return false;
        }
        return true;
    }
}

void CoreLink::begin_receiving()
{
    Impl *const impl = this->impl_;
    //! 再生モードには受信の環が要らない（配るのは `replay_step()` が主スレッドで行う）。
    //! **別スレッドを立てないことが肝**——立てると配られる順が OS の都合で揺れる。
    if (impl->replay) {
        return;
    }
    // detach する: 終了経路はパイプを閉じて抜けるだけなので join する場所が無い。
    std::thread([impl]() { impl->receive_loop(); }).detach();
}

void CoreLink::replay_step()
{
    this->impl_->replay_step();
}

bool CoreLink::replaying() const
{
    return this->impl_->replay;
}

bool CoreLink::take_latest_frame(std::string &wire, bool &carried_burst, std::vector<SoundEvent> &carried_sounds)
{
    Impl &impl = *this->impl_;
    std::lock_guard<std::mutex> lock(impl.inbox_mutex);
    if (!impl.has_new_frame) {
        return false;
    }
    wire = std::move(impl.latest_frame);
    impl.latest_frame.clear();
    impl.has_new_frame = false;
    carried_burst = impl.dropped_burst;
    impl.dropped_burst = false;
    carried_sounds.swap(impl.carried_sounds);
    impl.carried_sounds.clear();
    return true;
}

std::vector<std::string> CoreLink::take_table_payloads()
{
    Impl &impl = *this->impl_;
    std::deque<std::string> taken;
    {
        std::lock_guard<std::mutex> lock(impl.inbox_mutex);
        taken.swap(impl.table_payloads);
    }
    return std::vector<std::string>(std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end()));
}

bool CoreLink::send_ui_state(const presentation::UiStateMessage &message)
{
    return this->impl_->send("ui_state", presentation::encode_ui_state(message));
}

bool CoreLink::send_input_events(const presentation::InputEventsMessage &message)
{
    return this->impl_->send("input_event", presentation::encode_input_events(message));
}

bool CoreLink::send_keys(const presentation::KeysMessage &message)
{
    return this->impl_->send("keys", presentation::encode_keys(message));
}

bool CoreLink::send_quit_request()
{
    return this->impl_->send("quit_request", presentation::encode_quit_request(presentation::QuitRequestMessage{}));
}

bool CoreLink::exit_received() const
{
    return this->impl_->exit_received.load();
}

int CoreLink::exit_code() const
{
    return this->impl_->exit_code.load();
}

bool CoreLink::fatal_received() const
{
    return this->impl_->fatal_received.load();
}

std::string CoreLink::fatal_reason()
{
    std::lock_guard<std::mutex> lock(this->impl_->inbox_mutex);
    return this->impl_->fatal_reason;
}

bool CoreLink::core_gone() const
{
    return this->impl_->core_gone.load();
}

bool CoreLink::child_exited() const
{
    /*
     * 再生モードにはコアのプロセスが無い。**「居ない＝死んだ」と答えてはいけない**
     * ——`run()` はそれを見て「ゲームコアが予期せず停止しました」で畳んでしまう。
     * 記録を配り終えたときに `replay_step()` が `exit_received` を立てるので、
     * 終わりはそちらで分かる。
     */
    if (this->impl_->replay) {
        return this->impl_->exit_received.load();
    }
#if defined(_WIN32)
    const HANDLE process = this->impl_->process;
    if (process == nullptr) {
        return true;
    }
    return ::WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
#else
    /*
     * コアはスレッドなので「プロセスの生死」は無い。コアが自分から畳む経路は
     * `quit()` → `std::exit()`＝アプリごと終わる（この関数が呼ばれる機会も消える）。
     * 観測できる「消えた」はパイプの EOF（core_gone）と exit メッセージだけ。
     */
    return this->impl_->core_gone.load() || this->impl_->exit_received.load();
#endif
}

std::string CoreLink::core_stderr_tail(std::size_t lines)
{
#if defined(_WIN32)
    return this->impl_->stderr_relay.tail_text(lines);
#else
    // 1 プロセスなのでコアの stderr ＝ 自分の stderr（Android は logcat）。中継は無い。
    (void)lines;
    return std::string();
#endif
}

std::string CoreLink::core_stderr_log_path()
{
#if defined(_WIN32)
    return this->impl_->stderr_relay.log_path();
#else
    return std::string();
#endif
}

void CoreLink::close_core_stdin()
{
    Impl &impl = *this->impl_;
    std::lock_guard<std::mutex> lock(impl.send_mutex);
#if defined(_WIN32)
    close_handle(impl.to_core);
#else
    close_endpoint(impl.to_core);
#endif
}

bool CoreLink::wait_for_core_exit(unsigned timeout_ms)
{
#if defined(_WIN32)
    if (this->impl_->process == nullptr) {
        return true;
    }
    return ::WaitForSingleObject(this->impl_->process, static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
#else
    /*
     * コアスレッドの「畳んだ」を待つ。正常系はコアが `std::exit()` するので
     * ここへ戻ることすら無い（プロセスごと終わっている）。戻ってくるのは
     * 「exit を送ってから畳むまで」の僅かな間だけなので、緩い輪番で足りる。
     */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (this->impl_->core_gone.load() || this->impl_->exit_received.load()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return this->impl_->core_gone.load() || this->impl_->exit_received.load();
#endif
}

void CoreLink::force_quit()
{
    std::fprintf(stderr, "[hd2d] the user asked to end the game; closing the core's stdin first (panic save)\n");
    this->close_core_stdin();
    if (this->wait_for_core_exit(kPanicSaveGraceMs)) {
        return;
    }
#if defined(_WIN32)
    std::fprintf(stderr, "[hd2d] the core is still alive after the panic-save grace period; terminating it\n");
    if (this->impl_->process != nullptr) {
        ::TerminateProcess(this->impl_->process, 1);
        (void)::WaitForSingleObject(this->impl_->process, 5000);
    }
#else
    /*
     * スレッドは殺せない（殺すとロックや stdio が壊れたまま残る）。ここへ来るのは
     * 「stdin を閉じたのにコアが緊急セーブを終えられない」異常だけで、この後
     * プロセスごと終わる流れなので、待ち切れなかった旨だけ残して呼び出し側に返す。
     */
    std::fprintf(stderr, "[hd2d] the core thread did not finish the panic save in time; giving up the wait\n");
#endif
}

void CoreLink::shutdown()
{
    if (this->impl_ == nullptr) {
        return;
    }
    Impl &impl = *this->impl_;
    // 書き込み端を閉じる＝コアから見て stdin が EOF（v1 §9.2 の「ui が死んだ」と同じ合図）。
    this->close_core_stdin();
    /*
     * ---- **相手が本当に終わるまで待つ**（2026-08-28。気づいたこと 2 件の根）----
     *
     * ここは長く「**プロセスを畳む直前にしか呼ばない**」前提だった。ところが
     * `run()` は**畳まずに返って入り直す**ようになっている:
     *
     * | 戻り値 | いつ | 足された日 |
     * |---|---|---|
     * | `kRunRestart` | 遊び終えてコア選択へ戻す | 2026-08-23 |
     * | `kRunRelaunchCore` | 言語を替えて同じコアを起こし直す（追補 A2） | 2026-08-27 |
     *
     * `CoreLink` は `run()` の局所なので、返る所で `~CoreLink()` → ここへ来る。
     * 待たずに返ると**古いコアが生きたまま次のコアが立つ**。2 本は同じ
     * `lib/save/<slot>` と `last_slot.txt` を掴むので、**古い側の緊急セーブと
     * 新しい側の読み込みがぶつかる**——気づいたこと
     * 「英語版にするとコアが落ちる」（`--resume` で読む先を古い側が書いている）
     * 「ゲームを終了したさいにアプリを落とさずにコアを再度選択するとコアが起動しない」
     * （握手に応じない／セーブを開けない）の両方がこれで説明が付く。
     *
     * **`exit` を受け取っていても待つ。** コアは `exit` を送ってから最後の保存を
     * するので、届いた時点ではまだ書いている。
     *
     * 待ちは `force_quit()` の 1 段目と同じ猶予。終わらなければ**殺す**
     * ——次のコアを立てるほうが大事で、そのまま置くと二重起動になる。
     */
    if (!this->wait_for_core_exit(kPanicSaveGraceMs)) {
        std::fprintf(stderr,
            "[hd2d] the core did not exit within the grace period; terminating it before the next one starts\n");
#if defined(_WIN32)
        if (impl.process != nullptr) {
            ::TerminateProcess(impl.process, 1);
            (void)::WaitForSingleObject(impl.process, 5000);
        }
#endif
    }
    /*
     * 読み取り端（`from_core`）は**閉じない**。受信スレッドは detach してあり、
     * まだ読みの最中かもしれない。閉じるとその最中のハンドル／fd を引き抜くことになり、
     * 値が再利用されれば別のものを読みに行く。
     * **相手はもう死んでいる**ので、書き手の消えたパイプは読み切って終わる。
     */
#if defined(_WIN32)
    close_handle(impl.thread);
    close_handle(impl.process);
#endif
    impl.log.close();
    // Impl そのものも delete しない（受信スレッドがまだ触りうる）。
    // 起こし直しのたびに 1 個ずつ漏れるが、遊び 1 回につき 1 個である。
    this->impl_ = nullptr;
}

} // namespace hd2d
