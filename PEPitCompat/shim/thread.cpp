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

/**
 * Create a new task pinned to a core (core pinning ignored on Linux).
 * @param taskFunction Task entry point function
 * @param name Task name (logged for debugging, not used)
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
    (void)name;       // Not used on Linux

    if (!taskFunction) return -1; // errCOULD_NOT_CREATE

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
