/*
 * PEPitCompat — Display Shim (SDL2 window management)
 * 
 * The real TFT_eSPI library handles all drawing. Our custom processor (TFT_eSDL)
 * writes to a shared pixel buffer. This file manages the SDL2 window/renderer/texture.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Forward declarations — these are implemented in our custom processor (TFT_eSDL.c)
extern "C" {
    void setSDLWindow(SDL_Window* window, SDL_Renderer* renderer);
    void updateSDL(void);
}

// ============================================================
// Global SDL2 state (used by main.cpp and our processor)
// ============================================================

static SDL_Window*    g_sdl_window   = nullptr;
static SDL_Renderer*  g_sdl_renderer = nullptr;

// Called from main.cpp after SDL init
void initDisplay(SDL_Window** window, SDL_Renderer** renderer) {
    fprintf(stderr, "[PEPitCompat] MARK_DISPLAY_INIT\n");
    g_sdl_window = *window;
    g_sdl_renderer = *renderer;
    
    // Pass SDL handles to our processor so it can create texture and render
    setSDLWindow(g_sdl_window, g_sdl_renderer);
}

// Get SDL window (used by touch shim for mouse coordinate mapping)
SDL_Window* getSDLWindow() {
    return g_sdl_window;
}

// Get SDL renderer (used for scaling queries)
SDL_Renderer* getSDLRenderer() {
    return g_sdl_renderer;
}

// Force a display refresh (e.g., after fillScreen on main display)
void displayRefresh() {
    updateSDL();
}

// ============================================================
// Screenshot capture (for MCP server / headless testing)
// ============================================================

// Framebuffer buffer (RGB565) owned by our custom processor (TFT_eSDL.cpp)
extern uint16_t* sdl_tft_buffer;

// System state getter from the original source (systemStateHandler)
extern uint32_t getSystemState();

// ============================================================
// Media output directory for screenshots + recordings
// (--media-dir, default /tmp)
// ============================================================

static char mediaDir_[512] = "/tmp";

void setMediaDir(const char* dir) {
    if (!dir || !*dir) return;
    snprintf(mediaDir_, sizeof(mediaDir_), "%s", dir);
    size_t n = strlen(mediaDir_);
    while (n > 1 && mediaDir_[n - 1] == '/') mediaDir_[--n] = '\0';
}

// Create the media directory on demand (mkdir -p semantics; EEXIST is fine).
static void ensureMediaDir() {
    char path[512];
    snprintf(path, sizeof(path), "%s", mediaDir_);
    for (char* p = path + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(path, 0755); *p = '/'; }
    }
    mkdir(path, 0755);
}

// Write the shot metadata file (timestamp, system state, dimensions) — without framebuffer capture.
// Called at startup and from captureScreenshot.
void writeShotMeta() {
    time_t now = time(nullptr);
    char meta[256];
    snprintf(meta, sizeof(meta),
             "{\"timestamp\":%ld,\"systemState\":%u,\"width\":320,\"height\":240}",
             (long)now, (unsigned)getSystemState());
    ensureMediaDir();
    char tmpPath[560], finalPath[560];
    snprintf(tmpPath, sizeof(tmpPath), "%s/pepit_shot.json.tmp", mediaDir_);
    snprintf(finalPath, sizeof(finalPath), "%s/pepit_shot.json", mediaDir_);
    // Write via temp file + rename so readers never see a partial JSON.
    FILE* mf = fopen(tmpPath, "wb");
    if (mf) {
        fwrite(meta, 1, strlen(meta), mf);
        fclose(mf);
        rename(tmpPath, finalPath);
    }
}

// ============================================================
// Framebuffer -> RGB24 conversion (shared by screenshot + video recording)
// ============================================================

static void fillRGB24FromFramebuffer(uint8_t* out) {
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            uint16_t px = sdl_tft_buffer[y * 320 + x];
            uint32_t r5 = (px >> 11) & 0x1F;
            uint32_t g5 = (px >> 5) & 0x3F;
            uint32_t b5 = px & 0x1F;
            out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            out[1] = (uint8_t)((g5 << 2) | (g5 >> 4));
            out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
            out += 3;
        }
    }
}

// ============================================================
// Screenshot keybinding (--shot-key, default P)
// ============================================================

static int shotKey_ = SDL_SCANCODE_P;

bool captureScreenshot();  // defined below

void setScreenshotKey(int scancode) {
    shotKey_ = scancode;
}

// Edge-latched keystate check — safe to call from every event-pump site
// (main loop, delay(), control-file poller); a held key fires only once.
// Event-driven key edges: instantaneous KeyPress+KeyRelease pairs (synthetic
// events, quick taps) can complete between two GetKeyboardState samples, so
// every SDL event drain must report KEYDOWN here.
static volatile bool shotPressPending_ = false;

void checkScreenshotKey() {
    if (!shotPressPending_) return;
    shotPressPending_ = false;
    if (captureScreenshot()) {
        printf("[PEPitCompat] Screenshot captured (%s/pepit_shot.raw)\n", mediaDir_);
    }
}

// Capture the current framebuffer as raw RGB888 (320x240) + metadata file.
// MCP server reads <mediaDir>/pepit_shot.raw and <mediaDir>/pepit_shot.json.
bool captureScreenshot() {
    static uint8_t* frame = nullptr;
    if (!frame) {
        frame = (uint8_t*)malloc(320 * 240 * 3);
    }
    if (!sdl_tft_buffer || !frame) {
        return false;
    }
    fillRGB24FromFramebuffer(frame);

    ensureMediaDir();
    char tmpPath[560], finalPath[560];
    snprintf(tmpPath, sizeof(tmpPath), "%s/pepit_shot.raw.tmp", mediaDir_);
    snprintf(finalPath, sizeof(finalPath), "%s/pepit_shot.raw", mediaDir_);
    FILE* f = fopen(tmpPath, "wb");
    if (!f) {
        return false;
    }
    bool ok = (fwrite(frame, 1, 320 * 240 * 3, f) == 320 * 240 * 3);
    fclose(f);
    if (!ok) {
        unlink(tmpPath);
        return false;
    }
    rename(tmpPath, finalPath);

    // Write metadata file (timestamp, system state, dimensions)
    writeShotMeta();

    return true;
}

// ============================================================
// Gameplay video recording (--rec-key, default R)
//
// Frames come from the TFT framebuffer (sdl_tft_buffer), never from the SDL
// window, so recording works headless too. Raw RGB24 frames are appended to a
// temporary .raw file on a wall-clock schedule; on stop, ffmpeg transcodes it
// to MP4. A seekable file (not a live pipe) sidesteps rawvideo-demuxer and
// pipe-capacity races that intermittently killed piped recordings. The
// "(recording)" window-title marker is purely cosmetic and never touches the
// framebuffer.
// ============================================================

static const int   REC_FPS         = 30;
static const uint64_t REC_MAX_LAG_US = 1000000;   // longer stalls: resync instead of duplicating a burst

static int    recKey_        = SDL_SCANCODE_R;
static volatile bool recPressPending_ = false;
static FILE*  recRaw_        = nullptr;
static char   recPath_[600]      = {0};
static char   recRawPath_[600]   = {0};
static char   recBaseTitle_[512] = {0};
static uint8_t* recFrame_    = nullptr;
static uint64_t recStartUs_  = 0;
static uint64_t recNextUs_   = 0;
static uint32_t recFrames_   = 0;
// updateRecording() runs on the main-loop thread AND on game-task threads
// (vTaskDelay -> pollEventsAndCheckQuit), so hotkey edges, file writes and
// close/transcode must be serialized or one pump could close the recording
// file while another is mid-fwrite.
static std::recursive_mutex recMutex_;

// Report a keyboard keydown from any SDL event drain to the hotkey handlers.
// SDL's auto-repeat (evt->key.repeat) is filtered so one physical press
// equals one toggle even when held.
void notifyKeyDown(SDL_Event* evt) {
    if (evt->type != SDL_KEYDOWN || evt->key.repeat) return;
    SDL_Scancode sc = evt->key.keysym.scancode;
    if (sc == recKey_) recPressPending_ = true;
    else if (sc == shotKey_) shotPressPending_ = true;
}

static uint64_t nowUs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

static bool ffmpegAvailable() {
    static int cached = -1;
    if (cached < 0) {
        cached = (system("command -v ffmpeg >/dev/null 2>&1") == 0) ? 1 : 0;
    }
    return cached == 1;
}

static void setRecordingTitle(bool active) {
    if (!g_sdl_window) return;
    if (active) {
        const char* title = SDL_GetWindowTitle(g_sdl_window);
        snprintf(recBaseTitle_, sizeof(recBaseTitle_), "%s", title ? title : "PEPitCompat");
        char buf[600];
        snprintf(buf, sizeof(buf), "%s (recording)", recBaseTitle_);
        SDL_SetWindowTitle(g_sdl_window, buf);
    } else if (recBaseTitle_[0]) {
        SDL_SetWindowTitle(g_sdl_window, recBaseTitle_);
        recBaseTitle_[0] = '\0';
    }
}

static void startRecording() {
    std::lock_guard<std::recursive_mutex> lock(recMutex_);
    if (recRaw_) return;
    if (!sdl_tft_buffer) {
        printf("[PEPitCompat] Recording: no framebuffer yet\n");
        return;
    }
    if (!ffmpegAvailable()) {
        printf("[PEPitCompat] Recording needs ffmpeg on PATH\n");
        return;
    }
    if (!recFrame_) {
        recFrame_ = (uint8_t*)malloc(320 * 240 * 3);
        if (!recFrame_) return;
    }

    char stamp[32];
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    ensureMediaDir();
    snprintf(recPath_, sizeof(recPath_), "%s/pepit_rec_%s.mp4", mediaDir_, stamp);
    snprintf(recRawPath_, sizeof(recRawPath_), "%s/pepit_rec_%s.raw", mediaDir_, stamp);

    recRaw_ = fopen(recRawPath_, "wb");
    if (!recRaw_) {
        printf("[PEPitCompat] Recording: cannot open %s\n", recRawPath_);
        recPath_[0] = '\0';
        recRawPath_[0] = '\0';
        return;
    }
    recStartUs_ = recNextUs_ = nowUs();
    recFrames_ = 0;
    setRecordingTitle(true);
    printf("[PEPitCompat] Recording started -> %s\n", recPath_);
    fflush(stdout);
}

static void stopRecording() {
    std::lock_guard<std::recursive_mutex> lock(recMutex_);
    if (!recRaw_) return;
    FILE* raw = recRaw_;
    recRaw_ = nullptr;
    setRecordingTitle(false);

    uint64_t endUs = nowUs();
    fclose(raw);

    bool ok = false;
    if (recFrames_ > 0) {
        // Single-shot transcode of the seekable raw dump; rawvideo demuxing
        // from a plain file has no pipe-capacity or partial-packet races.
        char cmd[1400];
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 "
                 "-s 320x240 -framerate %d -i \"%s\" -c:v libx264 -preset ultrafast "
                 "-crf 20 -pix_fmt yuv420p \"%s\"",
                 REC_FPS, recRawPath_, recPath_);
        int status = system(cmd);
        long bytes = 0;
        struct stat st;
        if (stat(recPath_, &st) == 0) bytes = (long)st.st_size;
        ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && bytes > 1024;
        if (ok) {
            unlink(recRawPath_);
        } else {
            // Keep the raw frame dump so the recording can still be salvaged.
            printf("[PEPitCompat] Transcode failed (ffmpeg status %d), raw frames kept: %s\n",
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1, recRawPath_);
        }
    }
    if (!ok) {
        printf("[PEPitCompat] Recording failed (%u frames captured)\n", recFrames_);
    } else {
        long bytes = 0;
        struct stat st;
        if (stat(recPath_, &st) == 0) bytes = (long)st.st_size;
        printf("[PEPitCompat] Recording saved: %s (%u frames, %.1fs, %ld KB)\n",
               recPath_, recFrames_, (double)(endUs - recStartUs_) / 1e6, bytes / 1024);
    }
    fflush(stdout);
    recPath_[0] = '\0';
    recRawPath_[0] = '\0';
}

void setRecordingKey(int scancode) {
    recKey_ = scancode;
}

void toggleRecording() {
    std::lock_guard<std::recursive_mutex> lock(recMutex_);
    if (recRaw_) stopRecording();
    else startRecording();
}

// Called from every event pump (main loop, delay()/vTaskDelay poller): feeds
// frames on a fixed schedule and applies pending keydown edges.
void updateRecording() {
    std::lock_guard<std::recursive_mutex> lock(recMutex_);
    if (recPressPending_) {
        recPressPending_ = false;
        toggleRecording();
    }

    if (!recRaw_ || !recFrame_) return;
    const uint64_t step = 1000000ull / REC_FPS;
    uint64_t now = nowUs();
    // Longer stalls (blocking game logic) are skipped rather than filled with
    // a burst of duplicate frames.
    if (now > recNextUs_ + REC_MAX_LAG_US) {
        recNextUs_ = now;
    }
    while (recNextUs_ <= now) {
        fillRGB24FromFramebuffer(recFrame_);
        if (fwrite(recFrame_, 1, 320 * 240 * 3, recRaw_) != 320 * 240 * 3) {
            printf("[PEPitCompat] Recording file write failed, stopping\n");
            stopRecording();
            return;
        }
        recFrames_++;
        recNextUs_ += step;
    }
}

// Finalize an active recording on shutdown so the MP4 stays playable.
void stopRecordingIfActive() {
    stopRecording();
}
