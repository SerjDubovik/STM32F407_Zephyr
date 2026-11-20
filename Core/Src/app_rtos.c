#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <string.h>
#include "app_rtos.h"
#include "board.h"
#include "sd_browser.h"
#include "app_lvgl.h"
#include "touchscreen.h"

static struct {
    K_KERNEL_STACK_MEMBER(protocol, 16384);
    K_KERNEL_STACK_MEMBER(display, 16384);
} stacks __ccm_noinit_section;
static struct k_thread protocol, display;
K_MUTEX_DEFINE(devices);
K_SEM_DEFINE(protocol_event, 0, 1);

void AppRtos_LockDevices(void) { k_mutex_lock(&devices, K_FOREVER); }
void AppRtos_UnlockDevices(void) { k_mutex_unlock(&devices); }
void AppRtos_NotifyProtocolFromISR(void) { k_sem_give(&protocol_event); }
void AppRtos_WaitForProtocolEvent(uint32_t ms) { k_sem_take(&protocol_event, K_MSEC(ms)); }

static void run_display(void *a, void *b, void *c)
{
    for (;;) {
        AppRtos_LockDevices();
        AppLvgl_Poll(Touchscreen_OwnsDisplay());
        AppRtos_UnlockDevices();
        k_msleep(1);
    }
}

static void run_protocol(void *a, void *b, void *c)
{
    Board_Init();
    k_thread_start(&display);
    for (;;) {
        if (!SD_Browser_Poll()) AppRtos_WaitForProtocolEvent(25);
    }
}

void AppRtos_SetDisplayPaused(bool paused)
{
    /* Protocol holds devices, so Display is outside LVGL and FatFs. */
    if (paused) k_thread_suspend(&display);
    else k_thread_resume(&display);
}

static void find_idle(const struct k_thread *thread, void *data)
{
    if (k_thread_priority_get((k_tid_t)thread) == K_IDLE_PRIO) {
        *(k_tid_t *)data = (k_tid_t)thread;
    }
}

static const char *browser_state(k_tid_t thread)
{
    if (thread == k_current_get()) return "running";
    char buffer[64];
    const char *state = k_thread_state_str(thread, buffer, sizeof(buffer));
    if (strstr(state, "suspended")) return "suspended";
    if (strstr(state, "pending") || strstr(state, "sleeping")) return "blocked";
    return "ready";
}

unsigned AppRtos_ListTasks(AppTaskInfo info[APP_TASK_COUNT])
{
    k_tid_t threads[] = { &protocol, &display, NULL };
    const char *names[] = { "Protocol", "Display", "IDLE" };
    k_thread_foreach(find_idle, &threads[2]);
    unsigned count = threads[2] ? 3 : 2;
    for (unsigned i = 0; i < count; ++i) {
        k_thread_runtime_stats_t stats = {0};
        size_t unused = 0;
        k_thread_runtime_stats_get(threads[i], &stats);
        k_thread_stack_space_get(threads[i], &unused);
        info[i] = (AppTaskInfo){
            .name = names[i], .state = browser_state(threads[i]),
            .priority = k_thread_priority_get(threads[i]), .stack_free_bytes = unused,
            .runtime_us = (uint32_t)k_cyc_to_us_floor64(stats.execution_cycles),
            .controllable = threads[i] == &display
        };
    }
    return count;
}

int main(void)
{
    k_thread_create(&display, stacks.display, K_KERNEL_STACK_SIZEOF(stacks.display),
                    run_display, NULL, NULL, NULL, 6, 0, K_FOREVER);
    k_thread_name_set(&display, "Display");
    k_thread_create(&protocol, stacks.protocol, K_KERNEL_STACK_SIZEOF(stacks.protocol),
                    run_protocol, NULL, NULL, NULL, 4, 0, K_FOREVER);
    k_thread_name_set(&protocol, "Protocol");
    k_thread_start(&protocol);
    return 0;
}
