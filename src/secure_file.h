#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

namespace cppgram::detail {

// Creates the file if missing with mode 0600 and forces 0600 on an existing one.
// Session files hold auth keys, so they must not be world- or group-readable.
inline void ensure_private_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd >= 0) {
        ::fchmod(fd, 0600);
        ::close(fd);
    }
}

}  // namespace cppgram::detail
