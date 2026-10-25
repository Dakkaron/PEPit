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
#include <cstdio>
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
    if (initialized_) {
        seedFactoryData();
    }
    return initialized_;
}

void SD_MMC_Class::end() {
    // No cleanup needed on Linux (files are closed individually)
}

// ============================================================
// Factory data seeding
//
// Real PEPit devices ship with a provisioned SD card: /systemConfig.ini,
// /profiles.ini, /games/, /gfx/. When those files are missing the firmware
// halts on a fullscreen error display (checkFailWithMessage) and can never
// boot. A fresh emulated data directory is the equivalent of a blank SD
// card, so seed minimal factory data. Existing files are never overwritten,
// so user-provided data always wins. Graphics are generated as simple
// solid-color 24-bit BMPs (the formats the firmware's BMP loader accepts).
// ============================================================

static bool pathExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

/** Write a file (creating parent dirs) only if it does not exist yet. */
static bool seedFileIfMissing(const std::string& rootDir, const std::string& relPath, const std::string& contents) {
    std::string fullPath = rootDir + relPath;
    if (pathExists(fullPath)) return false;

    size_t slash = fullPath.find_last_of('/');
    if (slash != std::string::npos) {
        std::string parent = fullPath.substr(0, slash);
        if (!parent.empty() && !pathExists(parent)) {
            mkdirs(parent, 0755);
        }
    }

    FILE* f = fopen(fullPath.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "[PEPitCompat] Failed to seed factory file: %s\n", fullPath.c_str());
        return false;
    }
    bool ok = (fwrite(contents.data(), 1, contents.size(), f) == contents.size());
    fclose(f);
    if (!ok) {
        fprintf(stderr, "[PEPitCompat] Incomplete factory file: %s\n", fullPath.c_str());
        return false;
    }
    return true;
}

/** Build a solid-color 24-bit uncompressed BMP (the layout the firmware's loadBmpAnim accepts). */
static std::string makeBmp24(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    int rowSize = (w * 3 + 3) & ~3;
    int dataSize = rowSize * h;
    std::string out;
    out.reserve(54 + dataSize);
    auto le32 = [](uint32_t v) {
        return std::string(1, char(v & 0xFF)) + std::string(1, char((v >> 8) & 0xFF)) +
               std::string(1, char((v >> 16) & 0xFF)) + std::string(1, char((v >> 24) & 0xFF));
    };
    auto le16 = [](uint16_t v) {
        return std::string(1, char(v & 0xFF)) + std::string(1, char((v >> 8) & 0xFF));
    };
    out += "BM";
    out += le32(14u + 40u + (uint32_t)dataSize);
    out += le32(0);              // reserved
    out += le32(54);             // pixel data offset
    out += le32(40);             // BITMAPINFOHEADER size
    out += le32((uint32_t)w);
    out += le32((uint32_t)h);
    out += le16(1);              // color planes (firmware requires 1)
    out += le16(24);             // bits per pixel (firmware requires 24 for compression 0)
    out += le32(0);              // no compression
    out += le32((uint32_t)dataSize);
    out += le32(2835);           // horizontal resolution
    out += le32(2835);           // vertical resolution
    out += le32(0);              // palette size
    out += le32(0);              // important colors
    std::string row(rowSize, '\0');
    for (int x = 0; x < w; x++) {
        row[x * 3 + 0] = (char)b;
        row[x * 3 + 1] = (char)g;
        row[x * 3 + 2] = (char)r;
    }
    for (int y = 0; y < h; y++) out += row;
    return out;
}

void SD_MMC_Class::seedFactoryData() {
    int seeded = 0;
    auto seed = [&](const char* relPath, const std::string& contents) {
        if (seedFileIfMissing(rootDir_, relPath, contents)) seeded++;
    };

    // /systemConfig.ini — readSystemConfig(). `logExecutions` is the only key
    // read into errorMessage (a missing key = fatal fail loop), but include
    // every key the firmware reads. WiFi left empty => no NTP / no update
    // download attempted; simulate* off => input comes from the (shimmed)
    // pressure sensor / joystick, so MCP tools drive gameplay.
    seed("systemConfig.ini",
        "[system]\n"
        "wifiSSID=\n"
        "wifiPassword=\n"
        "wifiSSID2=\n"
        "wifiPassword2=\n"
        "wifiSSID3=\n"
        "wifiPassword3=\n"
        "touchScreenSensitivity=50\n"
        "simulateTrampoline=0\n"
        "simulateBlowing=0\n"
        "simulateInhalation=0\n"
        "debugLogBlowPressure=0\n"
        "debugLogTrampoline=0\n"
        "logExecutions=0\n"
        "timezoneOffset=0\n"
        "leftHandMode=1\n"
        "manometerMode=0\n");

    // /profiles.ini — readProfileData(). name/imagePath/cycles and all
    // task_* keys of the given task type are mandatory.
    seed("profiles.ini",
        "[profile_0]\n"
        "name=PEPit Training\n"
        "imagePath=/images/profile_0.bmp\n"
        "cycles=1\n"
        "task_0_type=pepLong\n"
        "task_0_minStrength=10\n"
        "task_0_targetStrength=30\n"
        "task_0_repetitions=3\n"
        "task_0_time=2000\n");

    // /prefs_monster.dat — game prefs (key:type:value lines, see shim/prefs.cpp).
    // The monster game self-loads its player from this (loadPlayerMonsterId),
    // so seed it to monster_1 — the only monster in the factory game data.
    seed("prefs_monster.dat",
        "playerMonsterId:int:1\n");

    seed("/images/profile_0.bmp", makeBmp24(30, 30, 120, 80, 180));

    // /games/monster — readGameConfig() (name/template/prefsNamespace are
    // mandatory). maxMonsterCount > monsterCount so monsterLevels[id] is in
    // bounds; [monster_1] needs name/imagePath/basicMonster; attack sections
    // need attackFunction.
    seed("games/monster/gameconfig.ini",
        "[game]\n"
        "name=Monster Catcher\n"
        "template=monster\n"
        "prefsNamespace=monster\n"
        "maxMonsterCount=2\n"
        "monsterCount=1\n"
        "\n"
        "[monster_1]\n"
        "name=Testling\n"
        // NOTE: relative to the game directory — the game code joins
        // monsterCatcherGamePath + imagePath, so an absolute path here
        // would be double-prefixed (e.g. /games/monster//games/monster/...).
        "imagePath=gfx/monsters/monster_1\n"
        "basicMonster=True\n"
        "safariRarity=1\n"
        "attackIdentifier=scratch\n"
        "\n"
        "[attack_scratch]\n"
        "animFrames=2\n"
        "imagePath=gfx/attacks/scratch.bmp\n"
        "attackFunction=scratch\n"
        "\n"
        "[attack_catch_ball]\n"
        "animFrames=2\n"
        "imagePath=gfx/attacks/catch_ball.bmp\n"
        "attackFunction=catch\n"
        "\n"
        "[attack_rareCandy]\n"
        "animFrames=1\n"
        "imagePath=gfx/attacks/rareCandy.bmp\n"
        "attackFunction=rareCandy\n");
    seed("games/monster/logo.bmp", makeBmp24(30, 30, 180, 120, 40));
    // anim_front: single BMP containing the stacked animation frames
    // (loadBmpAnim splits height by animFrames). 128px = 2 frames of 64px.
    seed("games/monster/gfx/monsters/monster_1/anim_front.bmp", makeBmp24(64, 128, 210, 130, 50));
    // back: single frame (drawn while the player blows, long-blow game).
    seed("games/monster/gfx/monsters/monster_1/back.bmp", makeBmp24(64, 64, 180, 100, 40));
    seed("games/monster/gfx/trainer/brendan.bmp", makeBmp24(64, 256, 70, 170, 70));
    seed("games/monster/gfx/attacks/scratch.bmp", makeBmp24(32, 64, 230, 230, 60));
    seed("games/monster/gfx/attacks/catch_ball.bmp", makeBmp24(32, 64, 240, 240, 240));
    seed("games/monster/gfx/attacks/rareCandy.bmp", makeBmp24(32, 32, 240, 120, 240));
    seed("games/monster/gfx/safari/Bush1_1.bmp", makeBmp24(124, 96, 30, 110, 30));
    seed("games/monster/gfx/safari/Bush1_2.bmp", makeBmp24(104, 75, 30, 110, 30));
    seed("games/monster/gfx/safari/Bush1_3.bmp", makeBmp24(75, 58, 30, 110, 30));
    seed("games/monster/gfx/safari/Bush1_4.bmp", makeBmp24(59, 40, 30, 110, 30));
    seed("games/monster/gfx/interface/ball_caught_indicator.bmp", makeBmp24(7, 7, 60, 220, 60));
    // gfx/win must exist and contain at least one file — the win screen path
    // lookup errors out (fail loop) when the directory is missing/empty.
    seed("games/monster/gfx/win/win1.bmp", makeBmp24(320, 240, 25, 85, 25));

    // /gfx — splash + system status icons (drawSystemStats) + selection-page
    // menu icons (refreshAndDrawMenuSprite) + update overlay.
    seed("gfx/splash.bmp", makeBmp24(320, 240, 20, 30, 75));
    seed("gfx/battery_low.bmp", makeBmp24(34, 20, 200, 40, 40));
    seed("gfx/battery_half.bmp", makeBmp24(34, 20, 220, 180, 40));
    seed("gfx/battery_full.bmp", makeBmp24(34, 20, 60, 200, 60));
    seed("gfx/battery_charging.bmp", makeBmp24(34, 20, 140, 240, 140));
    seed("gfx/wifi.bmp", makeBmp24(34, 20, 60, 120, 220));
    seed("gfx/nowifi.bmp", makeBmp24(34, 20, 110, 110, 110));
    seed("gfx/progressionmenu.bmp", makeBmp24(16, 16, 200, 200, 200));
    seed("gfx/executionlist.bmp", makeBmp24(16, 16, 200, 200, 200));
    seed("gfx/systemupdate.bmp", makeBmp24(16, 16, 220, 220, 120));

    if (seeded > 0) {
        printf("[PEPitCompat] Seeded %d missing factory data file(s) in %s\n", seeded, rootDir_.c_str());
    }
    fflush(stdout);
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
        // The arduino-esp32 core's FILE_APPEND (O_WRONLY|O_CREAT|O_APPEND)
        // always creates a missing file; POSIX "a" matches, so no "r" fallback.
        fmode = "a";
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
    if (ferror(fp_)) return false;
    // The arduino-esp32 core reports the exact byte count; feof() only trips
    // after a failed read, which would let while(available()) loops read one
    // phantom 0 byte past EOF.
    long pos = ftell(fp_);
    if (pos < 0) return false;
    fseek(fp_, 0, SEEK_END);
    long size = ftell(fp_);
    fseek(fp_, pos, SEEK_SET);
    return size > pos;
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
