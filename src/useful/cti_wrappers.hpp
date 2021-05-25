/******************************************************************************\
 * cti_wrappers.hpp - A header file for utility wrappers. This is for helper
 *                    wrappers to C-style allocation and error handling routines.
 *
 * Copyright 2019-2020 Hewlett Packard Enterprise Development LP.
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/
#pragma once

// cti frontend definitions
#include "cti_defs.h"

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>

#include "useful/cti_useful.h"
#include "ld_val/ld_val.h"

namespace cti {

// there is an std::make_unique<T> which constructs a unique_ptr of type T from its arguments.
// however, there is no equivalent that accepts a custom destructor function. normally, one would
// have to explicitly provide the types of T and its destructor function:
//     std::unique_ptr<T, decltype(&destructor)>{new T{}, destructor}
// this is a helper function to perform this deduction:
//     take_pointer_ownership(new T{}, destructor)
// for example:
//     auto const cstr = take_pointer_ownership(strdup(...), std::free);
template <typename T, typename Destr>
inline static auto
take_pointer_ownership(T*&& expiring, Destr&& destructor) -> std::unique_ptr<T, decltype(&destructor)>
{
    // type of Destr&& is deduced at the same time as Destr -> universal reference
    static_assert(!std::is_rvalue_reference<decltype(destructor)>::value);

    // type of T is deduced from T* first, then parameter as T*&& -> rvalue reference
    static_assert(std::is_rvalue_reference<decltype(expiring)>::value);

    return std::unique_ptr<T, decltype(&destructor)>
    { std::move(expiring) // then we take ownership of the expiring raw pointer
    , destructor          // and merely capture a reference to the destructor
    };
}

// Return value of environment variable, or default string if unset
inline static auto getenvOrDefault(char const* env_var, char const* default_value)
{
    if (char const* env_value = ::getenv(env_var)) {
        return env_value;
    }
    return default_value;
};

/* cstring wrappers */
namespace cstr {
    // lifted asprintf
    template <typename... Args>
    static inline std::string asprintf(char const* const formatCStr, Args&&... args) {
        char *rawResult = nullptr;
        if (::asprintf(&rawResult, formatCStr, std::forward<Args>(args)...) < 0) {
            throw std::runtime_error("asprintf failed.");
        }
        auto const result = take_pointer_ownership(std::move(rawResult), std::free);
        return std::string(result.get());
    }

    // lifted mkdtemp
    static inline std::string mkdtemp(std::string const& pathTemplate) {
        auto rawPathTemplate = take_pointer_ownership(strdup(pathTemplate.c_str()), std::free);
        if (::mkdtemp(rawPathTemplate.get())) {
            return std::string(rawPathTemplate.get());
        } else {
            throw std::runtime_error("mkdtemp failed on " + pathTemplate);
        }
    }

    // lifted gethostname
    static inline std::string gethostname() {
        char buf[HOST_NAME_MAX + 1];
        if (::gethostname(buf, HOST_NAME_MAX) < 0) {
            throw std::runtime_error("gethostname failed");
        }
        return std::string{buf};
    }

    // lifted readlink
    static inline std::string readlink(std::string const& path) {
        char buf[PATH_MAX + 1];
        if (::readlink(path.c_str(), buf, PATH_MAX) < 0) {
            throw std::runtime_error("readlink failed");
        }
        return std::string{buf};
    }

    // lifted basename
    static inline std::string basename(std::string const& path) {
        auto rawPath = take_pointer_ownership(strdup(path.c_str()), std::free);
        if (auto const baseName = ::basename(rawPath.get())) {
            return std::string(baseName);
        } else {
            throw std::runtime_error("basename failed on " + path);
        }
    }

    // lifted dirname
    static inline std::string dirname(std::string const& path) {
        auto rawPath = take_pointer_ownership(strdup(path.c_str()), std::free);
        if (auto const dirName = ::dirname(rawPath.get())) {
            return std::string(dirName);
        } else {
            throw std::runtime_error("dirname failed on " + path);
        }
    }

    // lifted getcwd
    static inline std::string getcwd() {
        char buf[PATH_MAX + 1];
        if (auto const cwd = ::getcwd(buf, PATH_MAX)) {
            return std::string{cwd};
        }

        throw std::runtime_error("getcwd failed: " + std::string{strerror(errno)});
    }
} /* namespace cti::cstr */

namespace file {
    // open a file path and return a unique FILE* or nullptr
    static inline auto try_open(std::string const& path, char const* mode) ->
        std::unique_ptr<FILE, decltype(&std::fclose)>
    {
        return take_pointer_ownership(fopen(path.c_str(), mode), std::fclose);
    }

    // open a file path and return a unique FILE* or throw
    static inline auto open(std::string const& path, char const* mode) ->
        std::unique_ptr<FILE, decltype(&std::fclose)>
    {
        if (auto ufp = try_open(path, mode)) {
            return ufp;
        }
        throw std::runtime_error("failed to open path " + path);
    }

    // fread from an open FILE* and conduct error handling
    static inline size_t read(void *ptr, size_t size, size_t nmemb, FILE* fp)
    {
        errno = 0;
        size_t ret = fread(ptr, size, nmemb, fp);
        // check for file read error
        if(ferror(fp)) {
            throw std::runtime_error(std::string{"Error in reading from file: "} + strerror(errno));
        }
        return ret;
    }

    // write a POD to file
    template <typename T>
    static inline void writeT(FILE* fp, T const& data)
    {
        static_assert(std::is_pod<T>::value, "type cannot be written bytewise to file");
        if (fwrite(&data, sizeof(T), 1, fp) != 1) {
            throw std::runtime_error("failed to write to file");
        }
    }

    // read a POD from file
    template <typename T>
    static inline T readT(FILE* fp)
    {
        static_assert(std::is_pod<T>::value, "type cannot be read bytewise from file");
        T data;
        if (fread(&data, sizeof(T), 1, fp) != 1) {
            throw std::runtime_error("failed to read from file");
        }
        return data;
    }
} /* namespace cti::file */

namespace dir {
    namespace {
        using CloseDirFn = int(*)(DIR*);
    }

    // open a directory path and return a unique DIR* or nullptr
    static inline auto try_open(std::string const& path) ->
        std::unique_ptr<DIR, CloseDirFn>
    {
        return take_pointer_ownership(opendir(path.c_str()), closedir);
    }

    // open a directory and return a unique DIR* or throw
    static inline auto open(std::string const& path) ->
        std::unique_ptr<DIR, CloseDirFn>
    {
        if (auto udp = try_open(path)) {
            return udp;
        }
        throw std::runtime_error("failed to open directory " + path);
    }
} /* namespace cti::dir */

/*
** Class to manage c style file descriptors. Ensures closure on destruction.
*/
class fd_handle {
private:
    int m_fd;
public:
    // Default constructor
    fd_handle()
    : m_fd{-1}
    { }
    // Default constructor with fd
    fd_handle(int fd)
    : m_fd{fd}
    {
        if (fd < 0) { throw std::runtime_error("File descriptor creation failed."); }
    }
    // Delete copy constructor
    fd_handle(const fd_handle&) = delete;
    fd_handle& operator=(const fd_handle&) = delete;
    // Move constructor
    fd_handle(fd_handle&& old)
    {
        m_fd = old.m_fd;
        old.m_fd = -1;
    }
    fd_handle& operator=(fd_handle&& other)
    {
        m_fd = other.m_fd;
        other.m_fd = -1;
        return *this;
    }
    // custom destructor
    ~fd_handle()
    {
        if (m_fd >= 0 ) close(m_fd);
    }
    // getter
    int fd() { return m_fd; }
};

struct dir_handle {
    static constexpr auto mode755 = int{S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH};

    std::string m_path;
    dir_handle(std::string const& path, int mode = mode755)
        : m_path{path}
    {
        if (::mkdir(m_path.c_str(), mode)) {
            throw std::runtime_error("mkdir " + m_path + " failed: " + strerror(errno));
        }
    }

    ~dir_handle()
    {
        if (::rmdir(m_path.c_str())) {
            fprintf(stderr, "warning: rmdir %s failed: %s\n", m_path.c_str(), strerror(errno));
        }
    }
};

struct softlink_handle {
    std::string m_linkPath;
    softlink_handle(std::string const& fromPath, std::string const& toPath)
        : m_linkPath{toPath}
    {
        if (::symlink(fromPath.c_str(), toPath.c_str())) {
            throw std::runtime_error("link " + fromPath + " -> " + toPath + " failed: " + strerror(errno));
        }
    }
    ~softlink_handle()
    {
        if (::unlink(m_linkPath.c_str())) {
            fprintf(stderr, "unlink %s failed: %s\n", m_linkPath.c_str(), strerror(errno));
        }
    }
};

template <typename T>
static void free_ptr_list(T* head) {
    auto elem = head;
    while (*elem != nullptr) {
        free(*elem);
        elem++;
    }
    free(head);
}

/* ld_val wrappers */
namespace ld_val {
    static inline auto getFileDependencies(const std::string& filePath, const std::string& ldAuditPath) ->
        std::unique_ptr<char*, decltype(&free_ptr_list<char*>)>
    {
        auto dependencyArray =  _cti_ld_val(filePath.c_str(), ldAuditPath.c_str());
        return take_pointer_ownership(std::move(dependencyArray), free_ptr_list<char*>);
    }
} /* namespace cti::ld_val */

/* cti_useful wrappers */
static inline std::string
findPath(std::string const& fileName) {
    if (auto fullPath = take_pointer_ownership(_cti_pathFind(fileName.c_str(), nullptr), std::free)) {
        return std::string{fullPath.get()};
    } else { // _cti_pathFind failed with nullptr result
        throw std::runtime_error(fileName + ": Could not locate in PATH.");
    }
}

static inline std::string
findLib(std::string const& fileName) {
    if (auto fullPath = take_pointer_ownership(_cti_libFind(fileName.c_str()), std::free)) {
        return std::string{fullPath.get()};
    } else { // _cti_libFind failed with nullptr result
        throw std::runtime_error(fileName + ": Could not locate in LD_LIBRARY_PATH or system location.");
    }
}

static inline std::string
getNameFromPath(std::string const& filePath) {
    if (auto realName = take_pointer_ownership(_cti_pathToName(filePath.c_str()), std::free)) {
        return std::string{realName.get()};
    } else { // _cti_pathToName failed with nullptr result
        throw std::runtime_error("Could not convert the fullname to realname.");
    }
}

static inline std::string
getRealPath(std::string const& filePath) {
    if (auto realPath = take_pointer_ownership(realpath(filePath.c_str(), nullptr), std::free)) {
        return std::string{realPath.get()};
    } else { // realpath failed with nullptr result
        throw std::runtime_error("realpath failed.");
    }
}

// Test if a directory has the specified permissions
static inline bool
dirHasPerms(char const* dirPath, int const perms)
{
    struct stat st;
    return dirPath != nullptr
        && !stat(dirPath, &st) // make sure this directory exists
        && S_ISDIR(st.st_mode) // make sure it is a directory
        && !access(dirPath, perms); // check that the directory has the desired permissions
}

// Test if a file has the specified permissions
static inline bool
fileHasPerms(char const* filePath, int const perms)
{
    struct stat st;
    return filePath != nullptr
        && !stat(filePath, &st) // make sure this directory exists
        && S_ISREG(st.st_mode)  // make sure it is a regular file
        && !access(filePath, perms); // check that the file has the desired permissions
}

// Test if a socket has the specified permissions
static inline bool
socketHasPerms(char const* socketPath, int const perms)
{
    struct stat st;
    return socketPath != nullptr
        && !stat(socketPath, &st) // make sure this path exists
        && S_ISSOCK(st.st_mode)  // make sure it is a socet
        && !access(socketPath, perms); // check that the file has the desired permissions
}

// Test if a file exists
static inline bool
pathExists(char const* filePath)
{
    struct stat st;
    return !stat(filePath, &st);
}

static inline bool
isSameFile(const std::string& filePath, const std::string& candidatePath) {
    // todo: could do something with file hashing?
    return !(filePath.compare(candidatePath));
}

// verify read/execute permissions of the given path, throw if inaccessible
static inline std::string
accessiblePath(std::string const& path) {
        if (!access(path.c_str(), R_OK | X_OK)) {
            return path;
        }
        throw std::runtime_error("path inacessible: " + path);
}

// Verify that a fd has write permissions
static inline bool
canWriteFd(int const fd)
{
    errno = 0;
    int accessFlags = fcntl(fd, F_GETFL) & O_ACCMODE;
    if (errno != 0) {
        return false;
    }
    return (accessFlags & O_RDWR) || (accessFlags & O_WRONLY);
}

// generate a temporary file and remove it on destruction
class temp_file_handle
{
private:
    std::unique_ptr<char, decltype(&::free)> m_path;

public:
    temp_file_handle(std::string const& templ)
        : m_path{strdup(templ.c_str()), ::free}
    {
        // use template to generate filename
        mktemp(m_path.get());
        if (m_path.get()[0] == '\0') {
            throw std::runtime_error("mktemp failed");
        }
    }

    temp_file_handle(temp_file_handle&& moved)
        : m_path{std::move(moved.m_path)}
    {
        moved.m_path.reset();
    }

    ~temp_file_handle()
    {
        // TODO: Log the warning if this fails.
        if( m_path ) {
            remove(m_path.get());
        }
    }

    char const* get() const { return m_path.get(); }
};

// Read passwd file and resize buffer as needed
static inline auto
getpwuid(uid_t const uid)
{
    auto pwd = passwd{};
    auto pwd_buf = std::vector<char>{};

    size_t buf_len = 4096;
    long rl = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (rl != -1) {
        buf_len = static_cast<size_t>(rl);
    }

    // Resize the vector
    pwd_buf.resize(buf_len);

    // Get the password file
    struct passwd *result = nullptr;
    if (getpwuid_r(uid,
                   &pwd,
                   pwd_buf.data(),
                   pwd_buf.size(),
                   &result)) {
        throw std::runtime_error("getpwuid_r failed: " + std::string{strerror(errno)});
    }

    // Ensure we obtained a result
    if (result == nullptr) {
        throw std::runtime_error("password file entry not found for uid " + std::to_string(uid));
    }

    return std::make_pair(std::move(pwd), std::move(pwd_buf));
}

} /* namespace cti */
