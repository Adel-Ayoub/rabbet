#include "rabbet/serialize/AtomicWrite.h"

#include <fstream>
#include <system_error>

namespace rb {

bool writeFileAtomically(const std::filesystem::path& path, const std::string& text) {
    // Guards process failures: a save that cannot complete leaves the target's bytes
    // untouched. Power loss is out of scope; there is no fsync, so the rename may be
    // journaled before the temp's data reaches disk.
    std::error_code ec;
    // Resolve first so a symlinked target is written through, not replaced by a file.
    std::filesystem::path target = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        target = path;
    }
    // rename(2) needs only directory permission and would strip a target's write
    // protection; keep the direct write's refusal instead.
    const std::filesystem::file_status status = std::filesystem::status(target, ec);
    if (!ec && std::filesystem::is_regular_file(status)) {
        using std::filesystem::perms;
        const perms writable = perms::owner_write | perms::group_write | perms::others_write;
        if ((status.permissions() & writable) == perms::none) {
            return false;
        }
    }
    std::filesystem::path temp = target;
    temp += ".tmp";
    // A stale temp is ours to reclaim. Removing it first also stops a planted temp
    // symlink from redirecting the write into the target itself.
    std::filesystem::remove(temp, ec);
    std::ofstream out(temp);
    if (!out) {
        return false;
    }
    out << text;
    // Close before judging: write-back errors on network mounts surface at close,
    // after a clean flush.
    out.close();
    if (out.fail()) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        return false;
    }
    return true;
}

} // namespace rb
