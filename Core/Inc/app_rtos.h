#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const char *state;
    unsigned priority;
    unsigned stack_free_bytes;
    uint32_t runtime_us;
    bool controllable;
} AppTaskInfo;

/* Two application tasks and the Zephyr idle thread. */
#define APP_TASK_COUNT 3U

void AppRtos_NotifyProtocolFromISR(void);
void AppRtos_WaitForProtocolEvent(uint32_t timeout_ms);
void AppRtos_LockDevices(void);
void AppRtos_UnlockDevices(void);
unsigned AppRtos_ListTasks(AppTaskInfo info[APP_TASK_COUNT]);
void AppRtos_SetDisplayPaused(bool paused);

#endif
