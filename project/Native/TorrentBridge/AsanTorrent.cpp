#include "AsanTorrent.h"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace lt = libtorrent;
constexpr std::int64_t maxFileBytes = 8LL * 1024 * 1024 * 1024;
constexpr std::uintmax_t freeSpaceReserve = 256ULL * 1024 * 1024;

std::string quoted(std::string const& input) {
    static char const hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : input) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
char* allocated(std::string const& input) {
    auto* out = static_cast<char*>(std::malloc(input.size() + 1));
    if (out) std::memcpy(out, input.c_str(), input.size() + 1);
    return out;
}
char* failure(char const* message) { return allocated("{\"error\":" + quoted(message) + "}"); }
bool validUUID(std::string const& value) {
    if (value.size() != 36) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (value[i] != '-') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}
std::string uuidKey(char const* raw) {
    if (!raw || !validUUID(raw)) throw std::runtime_error("Invalid download identifier");
    std::string key(raw);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}
bool inside(fs::path const& root, fs::path const& path) {
    auto r = root.begin(), p = path.begin();
    for (; r != root.end(); ++r, ++p) if (p == path.end() || *r != *p) return false;
    return p != path.end();
}
void assertNoSymlinks(fs::path const& directory) {
    if (fs::is_symlink(fs::symlink_status(directory))) throw std::runtime_error("Unsafe download directory");
    if (!fs::exists(directory)) return;
    for (auto const& entry : fs::recursive_directory_iterator(directory)) {
        if (fs::is_symlink(entry.symlink_status())) throw std::runtime_error("Symbolic links are not allowed in downloads");
    }
}
void validateRelative(std::string const& name) {
    if (name.empty() || name.size() > 1024 || name.find('\0') != std::string::npos || name.find('\\') != std::string::npos)
        throw std::runtime_error("Unsafe torrent file path");
    fs::path path(name);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) throw std::runtime_error("Absolute torrent file path rejected");
    for (auto const& component : path) {
        auto const part = component.string();
        if (part.empty() || part == "." || part == ".." || part.size() > 255 || part.find(':') != std::string::npos)
            throw std::runtime_error("Unsafe torrent path component");
    }
}
struct Entry {
    lt::torrent_handle handle;
    fs::path directory;
    int selected = -1;
    bool completedPaused = false;
    bool removing = false;
    bool deleteConfirmed = false;
    lt::info_hash_t hashes;
};
bool sameTorrent(lt::info_hash_t const& lhs, lt::info_hash_t const& rhs) {
    // A v1-only magnet may acquire the v2 hash after hybrid metadata arrives.
    return (lhs.has_v1() && rhs.has_v1() && lhs.v1 == rhs.v1)
        || (lhs.has_v2() && rhs.has_v2() && lhs.v2 == rhs.v2);
}
struct Engine {
    fs::path root;
    std::unique_ptr<lt::session> session;
    std::unordered_map<std::string, Entry> entries;
    explicit Engine(char const* raw) {
        if (!raw || !*raw || !fs::path(raw).is_absolute()) throw std::runtime_error("An absolute cache root is required");
        fs::create_directories(raw);
        root = fs::canonical(raw);
        if (root == root.root_path()) throw std::runtime_error("Invalid cache root");
        lt::settings_pack settings;
        settings.set_int(lt::settings_pack::connections_limit, 32);
        settings.set_int(lt::settings_pack::max_queued_disk_bytes, 4 * 1024 * 1024);
        settings.set_int(lt::settings_pack::max_metadata_size, 2 * 1024 * 1024);
        settings.set_int(lt::settings_pack::alert_queue_size, 128);
        settings.set_int(lt::settings_pack::stop_tracker_timeout, 1);
        settings.set_int(lt::settings_pack::alert_mask, static_cast<int>(static_cast<std::uint32_t>(lt::alert_category::error | lt::alert_category::storage | lt::alert_category::status)));
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0,[::]:0");
        session = std::make_unique<lt::session>(lt::session_params(settings));
    }
    Entry& entry(char const* raw) {
        auto const it = entries.find(uuidKey(raw));
        if (it == entries.end()) throw std::runtime_error("Download is not loaded");
        return it->second;
    }
    void checkDirectory(Entry const& item) const {
        if (!inside(root, item.directory) || item.directory.parent_path() != root || !validUUID(item.directory.filename().string()))
            throw std::runtime_error("Download directory escaped its cache root");
        assertNoSymlinks(item.directory);
        if (fs::weakly_canonical(item.directory) != item.directory) throw std::runtime_error("Download directory changed location");
    }
    void drainAlerts() {
        std::vector<lt::alert*> alerts;
        session->pop_alerts(&alerts);
        for (auto const* alert : alerts) {
            if (auto const* deleted = lt::alert_cast<lt::torrent_deleted_alert>(alert)) {
                for (auto& pair : entries) if (pair.second.removing && sameTorrent(pair.second.hashes, deleted->info_hashes)) pair.second.deleteConfirmed = true;
            }
        }
    }
};
Engine& engine(void* raw) {
    if (!raw) throw std::runtime_error("Torrent engine is unavailable");
    return *static_cast<Engine*>(raw);
}
std::shared_ptr<lt::torrent_info const> metadata(Entry& item) {
    auto info = item.handle.torrent_file();
    if (!info) return {};
    auto const& files = info->files();
    if (files.num_files() > 4096) { item.handle.pause(); throw std::runtime_error("Torrent contains too many files"); }
    std::unordered_set<std::string> seen;
    for (auto index : files.file_range()) {
        auto const flags = files.file_flags(index);
        if (flags & lt::file_storage::flag_symlink) { item.handle.pause(); throw std::runtime_error("Torrent symbolic links are not supported"); }
        if (flags & lt::file_storage::flag_pad_file) continue;
        auto const name = files.file_path(index);
        try { validateRelative(name); }
        catch (...) { item.handle.pause(); throw; }
        std::string folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(folded).second) { item.handle.pause(); throw std::runtime_error("Torrent contains colliding file paths"); }
        if (!inside(item.directory, (item.directory / name).lexically_normal())) { item.handle.pause(); throw std::runtime_error("Torrent file escaped its directory"); }
    }
    return info;
}
std::string snapshot(Engine& owner, Entry& item) {
    if (item.removing) throw std::runtime_error("Download removal is pending");
    owner.drainAlerts();
    owner.checkDirectory(item);
    auto const info = metadata(item);
    auto const status = item.handle.status(lt::torrent_handle::query_name);
    if (status.errc) throw std::runtime_error(status.errc.message());
    std::int64_t total = 0, done = 0;
    bool verified = false;
    std::ostringstream filesJSON;
    filesJSON << '[';
    if (info) {
        bool first = true;
        auto const& files = info->files();
        for (auto index : files.file_range()) {
            if (files.file_flags(index) & lt::file_storage::flag_pad_file) continue;
            if (!first) filesJSON << ',';
            first = false;
            filesJSON << "{\"index\":" << static_cast<int>(index) << ",\"name\":" << quoted(files.file_path(index)) << ",\"size\":" << files.file_size(index) << '}';
        }
        if (item.selected >= 0 && item.selected < files.num_files()) {
            auto const idx = lt::file_index_t{item.selected};
            total = files.file_size(idx);
            auto const progress = item.handle.file_progress(lt::torrent_handle::piece_granularity);
            if (static_cast<std::size_t>(item.selected) < progress.size()) done = std::min(total, progress[static_cast<std::size_t>(item.selected)]);
            auto const priorities = item.handle.get_file_priorities();
            bool applied = priorities.size() == static_cast<std::size_t>(files.num_files());
            for (std::size_t i = 0; applied && i < priorities.size(); ++i)
                applied = priorities[i] == (i == static_cast<std::size_t>(item.selected) ? lt::default_priority : lt::dont_download);
            // Piece-granularity progress counts only hash-verified data. Requiring
            // applied priorities and wanted bytes prevents the previous metadata-
            // only state (wanted=0, is_finished=true) from becoming a false success.
            verified = applied && total > 0 && done == total && status.total_wanted >= total && status.is_finished
                && status.state != lt::torrent_status::checking_files && status.state != lt::torrent_status::checking_resume_data;
        }
    }
    filesJSON << ']';
    if (verified && !item.completedPaused) { item.handle.pause(); item.completedPaused = true; }
    std::ostringstream out;
    out << "{\"name\":" << quoted(status.name) << ",\"hasMetadata\":" << (info ? "true" : "false")
        << ",\"files\":" << filesJSON.str() << ",\"selectedIndex\":";
    if (item.selected < 0) out << "null"; else out << item.selected;
    out << ",\"downloadedBytes\":" << done << ",\"totalBytes\":" << total
        << ",\"isVerifiedComplete\":" << (verified ? "true" : "false")
        << ",\"isPaused\":" << ((item.completedPaused || bool(status.flags & lt::torrent_flags::paused)) ? "true" : "false")
        << ",\"peers\":" << status.num_peers << ",\"downloadRate\":" << status.download_payload_rate << '}';
    return out.str();
}
template <class Action> char* protect(Action action) noexcept {
    try { return allocated(action()); }
    catch (std::exception const& error) { try { return failure(error.what()); } catch (...) { return nullptr; } }
    catch (...) { try { return failure("Torrent engine failed"); } catch (...) { return nullptr; } }
}
} // namespace

extern "C" void* asan_torrent_create(char const* root_path) {
    try { return new Engine(root_path); } catch (...) { return nullptr; }
}
extern "C" char* asan_torrent_add(void* raw, char const* uuid, char const* magnet) {
    return protect([&] {
        auto& owner = engine(raw);
        auto const key = uuidKey(uuid);
        auto const existing = owner.entries.find(key);
        if (existing != owner.entries.end()) return snapshot(owner, existing->second);
        if (!magnet || std::strlen(magnet) > 16384 || std::strncmp(magnet, "magnet:?", 8) != 0) throw std::runtime_error("A valid magnet link is required");
        if (owner.entries.size() >= 8) throw std::runtime_error("Too many loaded downloads");
        lt::error_code error;
        auto params = lt::parse_magnet_uri(magnet, error);
        if (error) throw std::runtime_error("Invalid magnet link");
        Entry item;
        item.directory = owner.root / key;
        owner.checkDirectory(item);
        fs::create_directories(item.directory);
        params.save_path = item.directory.string();
        params.storage_mode = lt::storage_mode_sparse;
        params.flags |= lt::torrent_flags::default_dont_download | lt::torrent_flags::upload_mode | lt::torrent_flags::duplicate_is_error;
        params.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused | lt::torrent_flags::seed_mode);
        // A magnet may carry selected-file priorities. They must never override
        // the app's explicit size check and file selection gate.
        params.file_priorities.clear();
        item.handle = owner.session->add_torrent(params, error);
        if (error) throw std::runtime_error(error.message());
        item.hashes = item.handle.info_hashes();
        auto const inserted = owner.entries.emplace(key, std::move(item));
        return snapshot(owner, inserted.first->second);
    });
}
extern "C" char* asan_torrent_snapshot(void* raw, char const* uuid) {
    return protect([&] { auto& owner = engine(raw); return snapshot(owner, owner.entry(uuid)); });
}
extern "C" char* asan_torrent_command(void* raw, char const* uuid, char const* command, int index) {
    return protect([&] {
        auto& owner = engine(raw);
        auto& item = owner.entry(uuid);
        owner.checkDirectory(item);
        std::string const verb = command ? command : "";
        if (verb == "remove") {
            if (!item.removing) {
                item.handle.pause();
                item.removing = true;
                owner.session->remove_torrent(item.handle, lt::session_handle::delete_files);
            }
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (!item.deleteConfirmed && std::chrono::steady_clock::now() < deadline) {
                owner.session->wait_for_alert(std::chrono::milliseconds(100));
                owner.drainAlerts();
            }
            if (!item.deleteConfirmed) throw std::runtime_error("Download is stopping; retry removal shortly");
            owner.checkDirectory(item);
            fs::remove_all(item.directory);
            owner.entries.erase(uuidKey(uuid));
            return std::string("{\"removed\":true}");
        }
        if (item.removing) throw std::runtime_error("Download removal is pending");
        if (verb == "select") {
            auto const info = metadata(item);
            if (!info) throw std::runtime_error("Torrent metadata is still loading");
            auto const& files = info->files();
            if (index < 0 || index >= files.num_files()) throw std::runtime_error("Invalid file selection");
            auto const chosen = lt::file_index_t{index};
            if (files.file_flags(chosen) & (lt::file_storage::flag_symlink | lt::file_storage::flag_pad_file)) throw std::runtime_error("This torrent entry cannot be downloaded");
            auto const size = files.file_size(chosen);
            if (size <= 0 || size > maxFileBytes) throw std::runtime_error("Choose a file smaller than 8 GiB");
            if (item.selected >= 0 && item.selected != index) throw std::runtime_error("Remove this download before choosing a different file");
            // Sparse files advertise the full logical size before downloading.
            // Credit only allocated filesystem blocks for an existing regular
            // owned file; never credit persisted UI progress or st_size alone.
            std::uintmax_t allocatedCredit = 0;
            auto const selectedPath = item.directory / files.file_path(chosen);
            struct stat existing{};
            if (::lstat(selectedPath.c_str(), &existing) == 0 && S_ISREG(existing.st_mode) && existing.st_blocks > 0)
                allocatedCredit = std::min(static_cast<std::uintmax_t>(size), static_cast<std::uintmax_t>(existing.st_blocks) * 512U);
            auto const needed = static_cast<std::uintmax_t>(size) - allocatedCredit + freeSpaceReserve;
            if (fs::space(item.directory).available < needed)
                throw std::runtime_error("Not enough free space for this download");
            std::vector<lt::download_priority_t> priorities(static_cast<std::size_t>(files.num_files()), lt::dont_download);
            priorities[static_cast<std::size_t>(index)] = lt::default_priority;
            item.handle.prioritize_files(priorities);
            // A synchronous read is queued after the priority update on the
            // libtorrent session thread; completion additionally checks these
            // priorities in snapshot, so pending disk updates fail closed.
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            auto applied = item.handle.get_file_priorities();
            while (applied != priorities && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                applied = item.handle.get_file_priorities();
            }
            if (applied != priorities) throw std::runtime_error("File priorities have not been applied yet");
            item.selected = index;
            item.completedPaused = false;
            item.handle.unset_flags(lt::torrent_flags::upload_mode);
            item.handle.resume();
        } else if (verb == "pause") item.handle.pause();
        else if (verb == "resume") {
            if (item.selected < 0) throw std::runtime_error("Choose a file before resuming");
            if (!item.completedPaused) item.handle.resume();
        } else throw std::runtime_error("Unknown torrent command");
        return snapshot(owner, item);
    });
}
extern "C" void asan_torrent_free_string(char* value) { std::free(value); }
extern "C" void asan_torrent_destroy(void* raw) {
    try { delete static_cast<Engine*>(raw); } catch (...) { /* Never unwind across the Swift boundary. */ }
}
