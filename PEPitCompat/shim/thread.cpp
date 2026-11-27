/*
 * PEPitCompat — FreeRTOS Thread Shim
 * 
 * Replaces ESP32 FreeRTOS tasks with std::thread.
 * Task handles point to a TaskInfo struct containing both the std::thread
 * and its pthread_t, so that xTaskGetCurrentTaskHandle() can return a
 * comparable value for identity checks (e.g. updateHandler's fetchReleaseUrl).
 */

#include "Arduino.h"
#include <thread>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include <pthread.h>

// ============================================================
// Task Info — wraps std::thread + pthread_t for handle comparison
// ============================================================

struct TaskInfo {
    std::thread thread;
    pthread_t   pthreadId;
};

// Global registry of all created tasks, for xTaskGetCurrentTaskHandle lookup.
static std::vector<TaskInfo*> g_tasks;

// Names already refused, so the per-frame creation retries report once.
static std::set<std::string> g_refusedReported;

// The engine3d helper tasks exist to overlap work with the device's second core.
// A host gains nothing from that hand-off, and the SDL renderer belongs to the
// thread that created it, so creation is refused here and engine3d takes its
// single-core paths. PEPIT_COMPAT_ALLOW_3D_TASKS restores the threads for runs
// that must exercise the pipelined protocol.
static bool compatRefusesTask(const char* name) {
    if (getenv("PEPIT_COMPAT_ALLOW_3D_TASKS")) return false;
    return name && (strcmp(name, "w3dblit") == 0 || strcmp(name, "w3dxf") == 0);
}

/**
 * Create a new task pinned to a core (core pinning ignored on Linux).
 * @param taskFunction Task entry point function
 * @param name Task name (checked by compatRefusesTask, used for logging)
 * @param stackSize Stack size in bytes (ignored on Linux)
 * @param parameter Parameter passed to task function
 * @param priority Task priority (ignored on Linux)
 * @param taskHandle Output: handle to created task
 * @param coreID Core to pin task to (ignored on Linux)
 * @return pdPASS (1) on success, errCOULD_NOT_CREATE (-1) on failure
 */
BaseType_t xTaskCreatePinnedToCore(
    void (*taskFunction)(void*),
    const char* name,
    uint32_t stackSize,
    void* parameter,
    UBaseType_t priority,
    TaskHandle_t* taskHandle,
    BaseType_t coreID
) {
    (void)stackSize;  // Unused on Linux
    (void)priority;   // Unused on Linux
    (void)coreID;     // Core pinning not supported

    if (!taskFunction) return -1; // errCOULD_NOT_CREATE

    if (compatRefusesTask(name)) {
        if (g_refusedReported.insert(name).second)
            printf("[PEPitCompat] single-threaded 3D: '%s' not created, engine uses its single-core path\n", name);
        if (taskHandle) *taskHandle = nullptr;
        return -1; // errCOULD_NOT_CREATE
    }

    try {
        auto* taskInfo = new TaskInfo();
        void (*func)(void*) = taskFunction;
        void* param = parameter;
        // Wrap entry point to record pthread_t before calling the actual function.
        taskInfo->thread = std::thread([func, param, taskInfo]() {
            taskInfo->pthreadId = pthread_self();
            func(param);
        });
        g_tasks.push_back(taskInfo);

        if (taskHandle) {
            *taskHandle = reinterpret_cast<TaskHandle_t>(taskInfo);
        }
        return 1; // pdPASS
    } catch (...) {
        if (taskHandle) *taskHandle = nullptr;
        return -1; // errCOULD_NOT_CREATE
    }
}

/**
 * Delete a task (clean up thread resources).
 * @param handle Task handle to delete. NULL = delete current task (self-terminate)
 */
void vTaskDelete(TaskHandle_t handle) {
    if (!handle) {
        // NULL = delete current task. On ESP32 this returns to the scheduler.
        // On Linux, detach our thread so std::thread destructor won't terminate,
        // then exit just this thread.
        pthread_t self = pthread_self();
        for (auto it = g_tasks.begin(); it != g_tasks.end(); ++it) {
            if (pthread_equal((*it)->pthreadId, self)) {
                (*it)->thread.detach();  // Prevent std::terminate on destructor
                g_tasks.erase(it);
                break;
            }
        }
        pthread_exit(nullptr);  // Exit just this thread, not the process
        return;
    }

    auto* info = reinterpret_cast<TaskInfo*>(handle);
    if (info->thread.joinable()) {
        info->thread.join();
    }

    // Remove from global registry
    for (auto it = g_tasks.begin(); it != g_tasks.end(); ++it) {
        if (*it == info) {
            g_tasks.erase(it);
            break;
        }
    }
    delete info;
}

/**
 * Get the handle of the current task (current thread).
 * Returns the TaskInfo* for the calling thread, or nullptr if not a managed task.
 */
TaskHandle_t xTaskGetCurrentTaskHandle() {
    pthread_t self = pthread_self();
    for (auto* info : g_tasks) {
        if (pthread_equal(info->pthreadId, self)) {
            return reinterpret_cast<TaskHandle_t>(info);
        }
    }
    return nullptr;
}

// Note: vTaskDelay is implemented in arduino.cpp (usleep wrapper)
// Note: yield() is implemented in arduino.cpp (1ms usleep)

// ============================================================
// Binary semaphores — FreeRTOS-compatible counting-limited-to-1
// semantics, used by the engine3d pipelined blit.
// ============================================================

#include <mutex>
#include <condition_variable>
#include <chrono>

struct CompatBinarySemaphore {
    std::mutex mtx;
    std::condition_variable cv;
    int count = 0;
};

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return new CompatBinarySemaphore();
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, uint32_t ticks) {
    if (!sem || sem == reinterpret_cast<SemaphoreHandle_t>(1)) return 1; // mutex stub handle
    auto* s = static_cast<CompatBinarySemaphore*>(sem);
    std::unique_lock<std::mutex> lk(s->mtx);
    if (ticks == 0) return s->count > 0 ? (--s->count, 1) : 0;
    if (ticks == portMAX_DELAY) {
        s->cv.wait(lk, [s] { return s->count > 0; });
        --s->count;
        return 1;
    }
    bool ok = s->cv.wait_for(lk, std::chrono::milliseconds(ticks), [s] { return s->count > 0; });
    if (ok) { --s->count; return 1; }
    return 0;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    if (!sem || sem == reinterpret_cast<SemaphoreHandle_t>(1)) return 1; // mutex stub handle
    auto* s = static_cast<CompatBinarySemaphore*>(sem);
    {
        std::lock_guard<std::mutex> lk(s->mtx);
        if (s->count == 0) s->count = 1; // FreeRTOS binary semaphores clamp at 1
    }
    s->cv.notify_one();
    return 1;
}

// ============================================================
// Queue — FIFO of fixed-size POD items (int16_t only, which is what the
// engine3d split-xf claim protocol uses). No blocking sends/receives.
// ============================================================

#include <deque>

struct CompatQueue {
    std::mutex mtx;
    std::deque<uint16_t> items;
    uint32_t length;
};

QueueHandle_t xQueueCreate(uint32_t length, uint32_t itemSize) {
    if (itemSize != sizeof(uint16_t)) return nullptr; // shim covers the engine's int16 claims only
    auto* q = new CompatQueue();
    q->length = length;
    return q;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, uint32_t ticks) {
    (void)ticks; // never blocks in the shim
    if (!queue) return 0;
    auto* q = static_cast<CompatQueue*>(queue);
    std::lock_guard<std::mutex> lk(q->mtx);
    if (q->items.size() >= q->length) return 0;
    uint16_t v;
    memcpy(&v, item, sizeof(v));
    q->items.push_back(v);
    return 1;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* item, uint32_t ticks) {
    (void)ticks; // never blocks in the shim
    if (!queue) return 0;
    auto* q = static_cast<CompatQueue*>(queue);
    std::lock_guard<std::mutex> lk(q->mtx);
    if (q->items.empty()) return 0;
    uint16_t v = q->items.front();
    q->items.pop_front();
    memcpy(item, &v, sizeof(v));
    return 1;
}

BaseType_t xQueueReset(QueueHandle_t queue) {
    if (!queue) return 0;
    auto* q = static_cast<CompatQueue*>(queue);
    std::lock_guard<std::mutex> lk(q->mtx);
    q->items.clear();
    return 1;
}
