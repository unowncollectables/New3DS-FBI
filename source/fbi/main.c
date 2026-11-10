#include <sys/iosupport.h>
#include <malloc.h>

#include <3ds.h>

#include "../core/clipboard.h"
#include "../core/error.h"
#include "../core/fs.h"
#include "../core/screen.h"
#include "../core/task/task.h"
#include "../core/ui/ui.h"
#include "section.h"
#include "settings.h"
#include "selfupdate.h"
#include "ftp.h"
#include "extras.h"
#include "jukebox.h"
#include <jansson.h>
#include "../core/http.h"
#include "task/uitask.h"

#define CURRENT_KPROCESS (*(void**) 0xFFFF9004)

#define KPROCESS_PID_OFFSET_OLD (0xB4)
#define KPROCESS_PID_OFFSET_NEW (0xBC)

static bool backdoor_ran = false;
static bool n3ds = false;
static u32 old_pid = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
static __attribute__((naked)) Result svcGlobalBackdoor(s32 (*callback)()) {
    asm volatile(
            "svc 0x30\n"
            "bx lr"
    );
}
#pragma GCC diagnostic pop

static s32 patch_pid_kernel() {
    u32 *pidPtr = (u32*) (CURRENT_KPROCESS + (n3ds ? KPROCESS_PID_OFFSET_NEW : KPROCESS_PID_OFFSET_OLD));

    old_pid = *pidPtr;
    *pidPtr = 0;

    backdoor_ran = true;
    return 0;
}

static s32 restore_pid_kernel() {
    u32 *pidPtr = (u32*) (CURRENT_KPROCESS + (n3ds ? KPROCESS_PID_OFFSET_NEW : KPROCESS_PID_OFFSET_OLD));

    *pidPtr = old_pid;

    backdoor_ran = true;
    return 0;
}

static bool attempt_patch_pid() {
    backdoor_ran = false;
    APT_CheckNew3DS(&n3ds);

    svcGlobalBackdoor(patch_pid_kernel);
    srvExit();
    srvInit();
    svcGlobalBackdoor(restore_pid_kernel);

    return backdoor_ran;
}

static void (*exit_funcs[16])()= {NULL};
static u32 exit_func_count = 0;

static void* soc_buffer = NULL;
static bool soc_inited = false;

static void soc_exit_wrapper() {
    /* Only hand the buffer back to the heap once the network module has
     * really let go of it. Freeing it while still shared is what can take
     * the console down on a later launch. */
    if(soc_inited && R_SUCCEEDED(socExit())) {
        soc_inited = false;
        if(soc_buffer != NULL) {
            free(soc_buffer);
            soc_buffer = NULL;
        }
    }
}

void cleanup_services() {
    /* Shut services down in the reverse order they were started. */
    while(exit_func_count > 0) {
        exit_func_count--;
        if(exit_funcs[exit_func_count] != NULL) {
            exit_funcs[exit_func_count]();
            exit_funcs[exit_func_count] = NULL;
        }
    }

    /* Never initialised (or init failed before socInit took it): safe to free. */
    if(!soc_inited && soc_buffer != NULL) {
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

static Result soc_init_wrapper() {
    Result res = socInit(soc_buffer, 0x200000);
    soc_inited = R_SUCCEEDED(res);
    return res;
}

#define INIT_SERVICE(initStatement, exitFunc) (R_SUCCEEDED(res = (initStatement)) && (exit_funcs[exit_func_count++] = (exitFunc)))

Result init_services() {
    Result res = 0;

    if(soc_buffer == NULL) {
        soc_buffer = memalign(0x1000, 0x200000);
    }

    if(soc_buffer != NULL) {
        Handle tempAM = 0;
        if(R_SUCCEEDED(res = srvGetServiceHandle(&tempAM, "am:net"))) {
            svcCloseHandle(tempAM);

            if(INIT_SERVICE(amInit(), amExit)
               && INIT_SERVICE(cfguInit(), cfguExit)
               && INIT_SERVICE(acInit(), acExit)
               && INIT_SERVICE(ptmuInit(), ptmuExit)
               && INIT_SERVICE(pxiDevInit(), pxiDevExit)
               && INIT_SERVICE(httpcInit(0), httpcExit)
               && INIT_SERVICE(soc_init_wrapper(), soc_exit_wrapper));
        }
    } else {
        res = R_APP_OUT_OF_MEMORY;
    }

    if(R_FAILED(res)) {
        cleanup_services();
    }

    return res;
}

static u32 old_time_limit = UINT32_MAX;

void init() {
    gfxInitDefault();

    bool isNew3DS = false;
    APT_CheckNew3DS(&isNew3DS);
    if(!isNew3DS) {
        error_panic("New3DS FBI requires a New 2DS XL / New 3DS (XL).\nOld 3DS / 2DS models are not supported.\nBased on FBI by Steveice10.");
        return;
    }

    Result romfsRes = romfsInit();
    if(R_FAILED(romfsRes)) {
        error_panic("Failed to mount RomFS: %08lX", romfsRes);
        return;
    }

    if(R_FAILED(init_services())) {
        if(!attempt_patch_pid()) {
            error_panic("Kernel backdoor not installed.\nPlease run a kernel exploit and try again.");
            return;
        }

        Result initRes = init_services();
        if(R_FAILED(initRes)) {
            error_panic("Failed to initialize services: %08lX", initRes);
            return;
        }
    }

    osSetSpeedupEnable(true);

    APT_GetAppCpuTimeLimit(&old_time_limit);
    Result cpuRes = APT_SetAppCpuTimeLimit(30);
    if(R_FAILED(cpuRes)) {
        error_panic("Failed to set syscore CPU time limit: %08lX", cpuRes);
        return;
    }

    AM_InitializeExternalTitleDatabase(false);

    http_init();

    screen_init();
    ui_init();
    task_init();
    settings_init();
    selfupdate_init();
    ftp_init();
    extras_init_lazy();
}

void cleanup() {
    /* Stop every background worker BEFORE any service goes away. */
    jukebox_shutdown();
    ftp_exit();
    selfupdate_exit();
    http_abort_all();

    clipboard_clear();

    settings_exit();
    task_exit();
    ui_exit();
    screen_exit();

    if(old_time_limit != UINT32_MAX) {
        APT_SetAppCpuTimeLimit(old_time_limit);
    }

    osSetSpeedupEnable(false);

    cleanup_services();

    romfsExit();

    gfxExit();
}

int main(int argc, const char* argv[]) {
    if(argc > 0 && envIsHomebrew()) {
        fs_set_3dsx_path(argv[0]);
    }

    init();

    mainmenu_open();
    while(aptMainLoop() && ui_update());

    cleanup();

    return 0;
}