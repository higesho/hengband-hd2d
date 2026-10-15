#pragma once

#include "portable/legacy_os.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace presentation {

/*
 * UI/adapter-owned player identity.
 *
 * Old cores are deliberately left with their original byte-oriented player
 * name and save-file rules.  A UTF-8 display name lives beside them in app
 * data and is keyed by core + save id.  This keeps old save formats intact and
 * lets multiple cores share one policy for Japanese player names.
 */
struct PlayerIdentitySnapshot {
    std::string core_key;
    std::string save_id;
    std::string display_name_utf8;
    std::string core_name_ascii;
    std::string edit_name_utf8;
    bool edit_active{ false };
    bool edit_touched{ false };
};

namespace player_identity_detail {

inline std::mutex mutex;
inline PlayerIdentitySnapshot active;

inline std::uint64_t fnv1a(std::string_view text)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

inline std::string metadata_path(std::string_view core_key, std::string_view save_id)
{
    std::string root = portable::data_dir();
    if (root.empty() || core_key.empty() || save_id.empty()) return {};
    std::filesystem::path dir(root);
    dir /= "player-identities";
    dir /= std::string(core_key);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};
    std::ostringstream filename;
    filename << std::hex << std::setfill('0') << std::setw(16) << fnv1a(save_id) << ".name";
    return (dir / filename.str()).string();
}

inline bool printable_ascii_name(std::string_view text)
{
    if (text.empty()) return false;
    for (const unsigned char c : text) {
        if (c < 0x20 || c > 0x7e) return false;
    }
    return true;
}

inline std::string sanitize_ascii(std::string_view text, std::size_t max_len)
{
    std::string out;
    for (const unsigned char c : text) {
        if (out.size() >= max_len) break;
        if (std::isalnum(c)) out.push_back(static_cast<char>(c));
        else if ((c == ' ' || c == '-' || c == '_') && !out.empty()) out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

inline void erase_last_utf8(std::string &text)
{
    if (text.empty()) return;
    std::size_t pos = text.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xc0u) == 0x80u) --pos;
    text.erase(pos);
}

} // namespace player_identity_detail

inline std::string player_identity_core_name(std::string_view display_name_utf8, std::size_t max_len = 8)
{
    using namespace player_identity_detail;
    if (printable_ascii_name(display_name_utf8)) {
        std::string ascii = sanitize_ascii(display_name_utf8, max_len);
        if (!ascii.empty()) return ascii;
    }
    static constexpr char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    std::uint64_t value = fnv1a(display_name_utf8);
    std::string out = "P";
    while (out.size() < max_len) {
        out.push_back(alphabet[value & 31u]);
        value >>= 5;
    }
    return out;
}

inline std::string player_identity_load(std::string_view core_key, std::string_view save_id,
    std::string_view fallback = {})
{
    const std::string path = player_identity_detail::metadata_path(core_key, save_id);
    if (!path.empty()) {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        if (in && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) return line;
        }
    }
    return std::string(fallback);
}

inline bool player_identity_store(std::string_view core_key, std::string_view save_id,
    std::string_view display_name_utf8)
{
    if (display_name_utf8.empty()) return false;
    const std::string path = player_identity_detail::metadata_path(core_key, save_id);
    if (path.empty()) return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(display_name_utf8.data(), static_cast<std::streamsize>(display_name_utf8.size()));
        out.put('\n');
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

inline void player_identity_activate(std::string core_key, std::string save_id,
    std::string fallback_display = {}, std::string fallback_core = {})
{
    PlayerIdentitySnapshot next;
    next.core_key = std::move(core_key);
    next.save_id = std::move(save_id);
    next.display_name_utf8 = player_identity_load(next.core_key, next.save_id, fallback_display);
    next.core_name_ascii = fallback_core.empty()
        ? player_identity_core_name(next.display_name_utf8.empty() ? fallback_display : next.display_name_utf8)
        : std::move(fallback_core);
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    player_identity_detail::active = std::move(next);
}

inline void player_identity_begin_edit(std::string core_key, std::string save_id,
    std::string_view seed_display, std::string_view seed_core = {})
{
    PlayerIdentitySnapshot next;
    next.core_key = std::move(core_key);
    next.save_id = std::move(save_id);
    next.display_name_utf8 = player_identity_load(next.core_key, next.save_id, seed_display);
    next.core_name_ascii = seed_core.empty() ? player_identity_core_name(seed_display) : std::string(seed_core);
    next.edit_active = true;
    next.edit_touched = false;
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    player_identity_detail::active = std::move(next);
}

inline void player_identity_append_edit(std::string_view utf8)
{
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    if (!player_identity_detail::active.edit_active) return;
    if (!player_identity_detail::active.edit_touched) {
        player_identity_detail::active.edit_name_utf8.clear();
        player_identity_detail::active.edit_touched = true;
    }
    player_identity_detail::active.edit_name_utf8.append(utf8);
}

inline void player_identity_backspace_edit()
{
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    if (player_identity_detail::active.edit_active && player_identity_detail::active.edit_touched)
        player_identity_detail::erase_last_utf8(player_identity_detail::active.edit_name_utf8);
}

inline void player_identity_clear_edit()
{
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    if (player_identity_detail::active.edit_active) {
        player_identity_detail::active.edit_touched = true;
        player_identity_detail::active.edit_name_utf8.clear();
    }
}

inline void player_identity_cancel_edit()
{
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    player_identity_detail::active.edit_name_utf8.clear();
    player_identity_detail::active.edit_active = false;
    player_identity_detail::active.edit_touched = false;
}

inline PlayerIdentitySnapshot player_identity_snapshot()
{
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    return player_identity_detail::active;
}

inline PlayerIdentitySnapshot player_identity_commit_edit(std::size_t core_name_max = 8,
    std::string save_id_override = {})
{
    std::lock_guard<std::mutex> lock(player_identity_detail::mutex);
    auto &id = player_identity_detail::active;
    if (id.edit_touched && !id.edit_name_utf8.empty()) id.display_name_utf8 = id.edit_name_utf8;
    id.core_name_ascii = player_identity_core_name(id.display_name_utf8, core_name_max);
    if (!save_id_override.empty()) id.save_id = std::move(save_id_override);
    if (id.save_id.empty()) id.save_id = id.core_name_ascii;
    id.edit_active = false;
    id.edit_touched = false;
    id.edit_name_utf8.clear();
    (void)player_identity_store(id.core_key, id.save_id, id.display_name_utf8);
    return id;
}

inline void player_identity_remove(std::string_view core_key, std::string_view save_id)
{
    const std::string path = player_identity_detail::metadata_path(core_key, save_id);
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace presentation
