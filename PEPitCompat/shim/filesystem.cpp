/*
 * PEPitCompat — Filesystem Shim (SD_MMC + File class)
 * 
 * Replaces ESP32 SD card operations with POSIX file I/O.
 * Root directory is configurable via --data-dir CLI parameter.
 */

#include "SD_MMC.h"
#include "Arduino.h"
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <cstring>
#include <unistd.h>
#include <limits.h>

// ============================================================
// Helper: recursively create parent directories (like mkdir -p)
// ============================================================

static void mkdirs(const std::string& path, mode_t mode) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/') {
            mkdir(current.c_str(), mode);
        }
    }
    mkdir(current.c_str(), mode);
}

// ============================================================
// Helper: resolve a symlink and ensure its target exists
// If path is a symlink, read the link target.  If the target
// doesn't exist (broken symlink) create it as a directory so
// that subsequent stat()/opendir() calls succeed.
// ============================================================

static void ensureSymlinkTarget(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) return;          // path doesn't exist at all
    if (!S_ISLNK(st.st_mode)) return;                     // not a symlink

    // Read the raw link target
    char buf[PATH_MAX];
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (len < 0) return;
    buf[len] = '\0';

    // If the target is relative, resolve it against the symlink's parent
    std::string target = buf;
    if (buf[0] != '/') {
        // dirname may modify its argument, so use a copy
        std::string parent = path;
        char* d = dirname(const_cast<char*>(parent.c_str()));
        target = std::string(d) + "/" + target;
    }

    // Create the target directory (and any missing parents) if it doesn't exist
    struct stat tst;
    if (stat(target.c_str(), &tst) != 0) {
        mkdirs(target, 0755);
    }
}

// ============================================================
// Helper: resolve the full path, following any symlinks along
// the way.  Returns the canonical (resolved) path or an empty
// string on failure.
// ============================================================

static std::string resolvePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved)) {
        return std::string(resolved);
    }
    // realpath fails for non-existent paths.  Try to fix broken symlinks first.
    ensureSymlinkTarget(path);
    if (realpath(path.c_str(), resolved)) {
        return std::string(resolved);
    }
    return "";
}

// ============================================================
// Global SD_MMC instance (for object-style access: SD_MMC.begin())
// ============================================================

SD_MMC_Class SD_MMC;

// ============================================================
// SD_MMC Class Implementation (member function definitions)
// ============================================================

bool SD_MMC_Class::begin(const char* mountPath, bool readOnly) {
    (void)mountPath;  // Mount path ignored on Linux
    (void)readOnly;   // Read-only mode not enforced on Linux
    
    // Check if root directory exists
    struct stat st;
    if (stat(rootDir_.c_str(), &st) != 0) {
        // Try to create the directory if it doesn't exist
        ::mkdir(rootDir_.c_str(), 0755);
    }
    
    initialized_ = (stat(rootDir_.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    return initialized_;
}

void SD_MMC_Class::end() {
    // No cleanup needed on Linux (files are closed individually)
}

void SD_MMC_Class::setPins(uint8_t sclk, uint8_t mosi, uint8_t miso) {
    (void)sclk; (void)mosi; (void)miso;  // Pins not used on Linux
}

File SD_MMC_Class::open(const char* path, uint8_t mode, bool create) {
    // Virtual (app-facing) path: ensure leading / for app-facing path() method
    std::string virtualPath = (path[0] == '/' ? path : std::string("/") + path);
    std::string fullPath = rootDir_ + (path[0] == '/' ? path + 1 : path);

    // Resolve any symlinks in the path so stat/opendir/fopen work correctly
    std::string resolved = resolvePath(fullPath);
    if (!resolved.empty()) {
        fullPath = resolved;
    }

    // CRITICAL: Check if path is a directory BEFORE trying fopen().
    // On some Linux systems, fopen() on a directory may succeed (returning a FILE*),
    // which would cause the File to be created with isDir_=false.
    struct stat st;
    if (stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(fullPath.c_str());
        if (dir) {
            return File(virtualPath, fullPath, dir);  // Directory
        }
    }

    // Not a directory (or doesn't exist) — try as regular file
    const char* fmode = "r";  // Default: read-only
    if (mode == FILE_WRITE) {
        fmode = create ? "w" : "r";  // Create if doesn't exist
    } else if (mode == FILE_APPEND) {
        fmode = create ? "a" : "r";  // Create if doesn't exist
    }

    FILE* fp = fopen(fullPath.c_str(), fmode);
    if (fp) {
        return File(virtualPath, fullPath, fp);  // Regular file
    }

    if (!fp && create && (mode == FILE_WRITE || mode == FILE_APPEND)) {
        // Create parent directories recursively, then try again
        mkdirs(fullPath, 0755);
        fp = fopen(fullPath.c_str(), fmode);
        if (fp) {
            return File(virtualPath, fullPath, fp);  // Regular file
        }
    }

    return File();  // Invalid file (default constructor)
}

bool SD_MMC_Class::exists(const char* path) {
    std::string fullPath = rootDir_ + (path[0] == '/' ? path + 1 : path);
    // Resolve symlinks before checking existence
    std::string resolved = resolvePath(fullPath);
    if (!resolved.empty()) {
        fullPath = resolved;
    }
    return access(fullPath.c_str(), F_OK) == 0;
}

bool SD_MMC_Class::remove(const char* path) {
    std::string fullPath = rootDir_ + (path[0] == '/' ? path + 1 : path);
    std::string resolved = resolvePath(fullPath);
    if (!resolved.empty()) {
        fullPath = resolved;
    }
    return unlink(fullPath.c_str()) == 0;
}

bool SD_MMC_Class::rename(const char* oldPath, const char* newPath) {
    std::string oldFull = rootDir_ + (oldPath[0] == '/' ? oldPath + 1 : oldPath);
    std::string newFull = rootDir_ + (newPath[0] == '/' ? newPath + 1 : newPath);
    std::string oldResolved = resolvePath(oldFull);
    if (!oldResolved.empty()) oldFull = oldResolved;
    std::string newResolved = resolvePath(newFull);
    if (!newResolved.empty()) newFull = newResolved;
    return ::rename(oldFull.c_str(), newFull.c_str()) == 0;
}

bool SD_MMC_Class::mkdir(const char* path) {
    std::string fullPath = rootDir_ + (path[0] == '/' ? path + 1 : path);
    // Recursively create parent directories, then the target directory
    mkdirs(fullPath, 0755);
    struct stat st;
    return stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool SD_MMC_Class::rmdir(const char* path) {
    std::string fullPath = rootDir_ + (path[0] == '/' ? path + 1 : path);
    std::string resolved = resolvePath(fullPath);
    if (!resolved.empty()) {
        fullPath = resolved;
    }
    return ::rmdir(fullPath.c_str()) == 0;
}

uint64_t SD_MMC_Class::cardSize() {
    return 1024ULL * 1024 * 1024;  // Stub: return 1GB
}

void SD_MMC_Class::setRootDir(const std::string& path) {
    rootDir_ = path;
    // Ensure trailing slash for consistent path concatenation
    if (!rootDir_.empty() && rootDir_.back() != '/') {
        rootDir_ += '/';
    }
}

const std::string& SD_MMC_Class::getRootDir() {
    return rootDir_;
}

// ============================================================
// File Class Implementation
// ============================================================

File::File() : fp_(nullptr), dir_(nullptr), isDir_(false) {}

File::~File() {
    close();
}

File::File(File&& other) noexcept 
    : fp_(other.fp_), dir_(other.dir_), virtualPath_(std::move(other.virtualPath_)), 
      resolvedPath_(std::move(other.resolvedPath_)), isDir_(other.isDir_) {
    other.fp_ = nullptr;
    other.dir_ = nullptr;
    other.isDir_ = false;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();  // Close current file/dir
        fp_ = other.fp_;
        dir_ = other.dir_;
        virtualPath_ = std::move(other.virtualPath_);
        resolvedPath_ = std::move(other.resolvedPath_);
        isDir_ = other.isDir_;
        other.fp_ = nullptr;
        other.dir_ = nullptr;
        other.isDir_ = false;
    }
    return *this;
}

// Private constructors for SD_MMC::open() — virtualPath for app-facing path(), resolvedPath for I/O
File::File(const std::string& virtualPath, const std::string& resolvedPath, FILE* fp)
    : fp_(fp), dir_(nullptr), virtualPath_(virtualPath), resolvedPath_(resolvedPath), isDir_(false) {}

File::File(const std::string& virtualPath, const std::string& resolvedPath, DIR* dir)
    : fp_(nullptr), dir_(dir), virtualPath_(virtualPath), resolvedPath_(resolvedPath), isDir_(true) {}

uint8_t File::read() {
    if (!fp_) return 0;
    int c = fgetc(fp_);
    return c == EOF ? 0 : static_cast<uint8_t>(c);
}

size_t File::read(uint8_t* buffer, size_t len) {
    if (!fp_) return 0;
    return fread(buffer, 1, len, fp_);
}

void File::readBytes(char* buffer, size_t len) {
    if (!fp_) return;
    fread(buffer, 1, len, fp_);
}

size_t File::readBytesUntil(char term, char* buffer, size_t len) {
    if (!fp_ || !buffer || len < 1) return 0;
    size_t count = 0;
    while (count < len - 1) {
        int c = fgetc(fp_);
        if (c == EOF || c == term) break;  // terminator consumed but not stored
        buffer[count++] = static_cast<char>(c);
    }
    buffer[count] = '\0';
    return count;
}

size_t File::write(uint8_t data) {
    if (!fp_) return 0;
    return fwrite(&data, 1, 1, fp_);
}

size_t File::write(const uint8_t* buffer, size_t len) {
    if (!fp_) return 0;
    return fwrite(buffer, 1, len, fp_);
}

size_t File::print(const char* text) {
    if (!fp_ || !text) return 0;
    return fwrite(text, 1, strlen(text), fp_);
}

size_t File::println(const char* text) {
    if (!fp_) return 0;
    size_t n = print(text);
    n += fwrite("\n", 1, 1, fp_);
    return n;
}

size_t File::print(const String& text) {
    return print(text.c_str());
}

size_t File::print(int num) {
    if (!fp_) return 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", num);
    return fwrite(buf, 1, strlen(buf), fp_);
}

size_t File::print(uint32_t num) {
    if (!fp_) return 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", num);
    return fwrite(buf, 1, strlen(buf), fp_);
}

size_t File::println(const String& text) {
    size_t n = print(text);
    if (fp_) n += fwrite("\n", 1, 1, fp_);
    return n;
}

size_t File::println(int num) {
    size_t n = print(num);
    if (fp_) n += fwrite("\n", 1, 1, fp_);
    return n;
}

size_t File::println(uint32_t num) {
    size_t n = print(num);
    if (fp_) n += fwrite("\n", 1, 1, fp_);
    return n;
}

bool File::available() {
    if (isDir_) return dir_ != nullptr;  // Directory is "available" while open
    if (!fp_) return false;
    return !feof(fp_) && !ferror(fp_);
}

void File::seek(uint32_t pos) {
    if (!fp_) return;
    fseek(fp_, pos, SEEK_SET);
}

uint32_t File::size() {
    if (!fp_) return 0;
    long current = ftell(fp_);
    fseek(fp_, 0, SEEK_END);
    long size = ftell(fp_);
    fseek(fp_, current, SEEK_SET);  // Restore position
    return static_cast<uint32_t>(size);
}

const char* File::name() const {
    // Store basename in a static buffer (returned as const char*)
    static thread_local std::string nameBuf;
    // Use virtualPath for the app-facing basename (e.g. "games" from "/games")
    if (!virtualPath_.empty()) {
        nameBuf = File::getBasename(virtualPath_);
    } else if (!resolvedPath_.empty()) {
        nameBuf = File::getBasename(resolvedPath_);
    } else {
        nameBuf.clear();
    }
    return nameBuf.c_str();
}

String File::path() const {
    // Return the virtual path (app-facing, e.g. "/games") 
    if (!virtualPath_.empty()) {
        // virtualPath_ is already the app-facing path with leading /
        String result = virtualPath_;
        // Ensure it starts with /
        if (!result.isEmpty() && result.getCharAt(0) != '/') {
            String r = "/";
            r += result;
            return r;
        }
        return result;
    }
    // Fallback: compute from resolved path if virtualPath not set
    if (!resolvedPath_.empty()) {
        size_t pos = resolvedPath_.find(SD_MMC.rootDir_);
        if (pos != std::string::npos) {
            String relative = resolvedPath_.substr(pos + SD_MMC.rootDir_.size());
            if (!relative.isEmpty() && relative.getCharAt(0) != '/') {
                String result = "/";
                result += relative;
                return result;
            }
            return relative;
        }
    }
    return "";
}

bool File::isDirectory() {
    return isDir_;
}

File File::openNextFile() {
    if (!dir_) return File();  // Not a directory or not open
    
    struct dirent* entry = readdir(dir_);
    if (!entry) return File();  // No more entries
    
    // Skip "." and ".."
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        return openNextFile();  // Recurse to get next entry
    }
    
    // Build virtual path for this entry (app-facing, e.g. "/games/mygame")
    std::string virtualPath = virtualPath_ + "/" + entry->d_name;
    if (virtualPath_.empty()) {
        virtualPath = std::string("/") + entry->d_name;  // Fallback if no virtual path set
    }
    
    // Build resolved path for actual I/O operations (use resolvedPath_ as base)
    std::string basePath = !resolvedPath_.empty() ? resolvedPath_ : virtualPath_;
    std::string entryResolved = basePath + "/" + entry->d_name;
    
    // Resolve symlinks so stat/opendir/fopen work on the actual target
    std::string resolved = resolvePath(entryResolved);
    if (!resolved.empty()) {
        entryResolved = resolved;
    }
    
    // Check if it's a directory or file (stat follows symlinks)
    struct stat st;
    if (stat(entryResolved.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            DIR* subdir = opendir(entryResolved.c_str());
            if (subdir) return File(virtualPath, entryResolved, subdir);
        } else {
            FILE* fp = fopen(entryResolved.c_str(), "r");
            if (fp) return File(virtualPath, entryResolved, fp);
        }
    }
    
    return openNextFile();  // Entry couldn't be opened, try next
}

void File::close() {
    if (fp_) { fclose(fp_); fp_ = nullptr; }
    if (dir_) { closedir(dir_); dir_ = nullptr; }
    isDir_ = false;
}

File::operator bool() const {
    return fp_ != nullptr || dir_ != nullptr;
}

std::string File::getBasename(const std::string& path) {
    // Use a copy to avoid modifying the original (basename may modify its argument)
    std::string pathCopy = path;
    char* base = basename(const_cast<char*>(pathCopy.c_str()));
    return std::string(base);
}
