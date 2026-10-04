/*
 * PEPitCompat — FS Shim (File class and namespace)
 * 
 * Provides the File class used by SD_MMC for file operations.
 * Also provides fs::File namespace alias for gfxHandler BMP loading.
 */

#ifndef FS_H
#define FS_H

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <dirent.h>
#include <string>

// Forward declaration of String (defined in Arduino.h)
class String;

// File mode constants (from ESP32 SD_MMC)
#define FILE_READ 0x0
#define FILE_WRITE 0x1
#define FILE_APPEND 0x2

// File class — wraps POSIX file operations for SD_MMC compatibility
class File {
public:
    // Default constructor — creates an invalid/closed file
    File();
    
    // Destructor — closes file if open
    ~File();
    
    // Move constructor and assignment (for SD_MMC.open() return value)
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    
    // Delete copy constructor and assignment (File is non-copyable)
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    // Read operations
    uint8_t read();                                    // Read single byte
    size_t read(uint8_t* buffer, size_t len);          // Read bytes into buffer
    void readBytes(char* buffer, size_t len);          // Read bytes as chars (for INI parsing)
    size_t readBytesUntil(char term, char* buffer, size_t len);  // Read until terminator (for prefs parsing)
    
    // Write operations
    size_t write(uint8_t data);                        // Write single byte
    size_t write(const uint8_t* buffer, size_t len);   // Write bytes from buffer
    size_t print(const char* text);                    // Print string (no newline)
    size_t print(const String& text);                  // Print Arduino String
    size_t print(int num);                             // Print integer
    size_t print(uint32_t num);                        // Print unsigned int
    size_t println(const char* text);                  // Print string with newline
    size_t println(const String& text);                // Print Arduino String + newline
    size_t println(int num);                           // Print integer + newline
    size_t println(uint32_t num);                      // Print unsigned int + newline
    
    // File position and status
    bool available();                                  // More data available to read?
    void seek(uint32_t pos);                           // Seek to absolute position
    uint32_t size();                                   // Get file size in bytes
    
    // File info
    const char* name() const;                          // Get filename (basename)
    String path() const;                               // Get full path
    
    // Directory operations
    bool isDirectory();                                // Check if this is a directory
    File openNextFile();                               // Get next entry in directory
    
    // Close file/directory
    void close();
    
    // Boolean conversion (for if statements: if (file) { ... })
    explicit operator bool() const;

    // Constructors for SD_MMC::open() — creates File from path and handle
    // virtualPath is the app-facing path (e.g. "/games"), resolvedPath is the real filesystem path
    File(const std::string& virtualPath, const std::string& resolvedPath, FILE* fp);  // For regular files
    File(const std::string& virtualPath, const std::string& resolvedPath, DIR* dir);  // For directories

private:
    FILE* fp_ = nullptr;                               // File handle (for regular files)
    DIR* dir_ = nullptr;                                // Directory handle (for directories)
    std::string virtualPath_;                           // App-facing path (e.g. "/games") for path() method
    std::string resolvedPath_;                          // Real filesystem path (symlinks resolved) for I/O
    bool isDir_ = false;                                // True if this is a directory
    
    // Helper to get basename from path (renamed to avoid POSIX basename() collision)
    static std::string getBasename(const std::string& path);
};

// fs namespace — provides File alias for gfxHandler BMP loading
namespace fs {
    using File = ::File;
}

#endif // FS_H
