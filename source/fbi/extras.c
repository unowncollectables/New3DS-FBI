#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <3ds.h>

#include "extras.h"
#include "resources.h"
#include "section.h"
#include "settings.h"
#include "../core/core.h"

#define HISTORY_PATH "/fbi/history.txt"
#define HISTORY_MAX_LINES 200
#define EXTRAS_CFG_PATH "/fbi/extras.cfg"

static bool g_sound_on_done = true;
static bool g_extras_inited = false;
static bool g_prev_sleep = true;
static u64 g_install_start_ms = 0;
static u32 g_install_elapsed_sec = 0;
static bool g_install_active = false;

static void extras_ensure_dir(void) {
    FS_Archive sdmc = 0;
    if(R_SUCCEEDED(FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        fs_ensure_dir(sdmc, "/fbi/");
        FSUSER_CloseArchive(sdmc);
    }
}

static void extras_load(void) {
    g_sound_on_done = true;
    extras_ensure_dir();
    FILE* fd = fopen("sdmc:" EXTRAS_CFG_PATH, "rb");
    if(fd == NULL) return;
    char line[128];
    while(fgets(line, sizeof(line), fd) != NULL) {
        char key[64], val[64];
        if(sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if(strcasecmp(key, "sound_on_done") == 0) {
                g_sound_on_done = !(val[0] == '0' || strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0);
            }
        }
    }
    fclose(fd);
}

static void extras_save(void) {
    extras_ensure_dir();
    FILE* fd = fopen("sdmc:" EXTRAS_CFG_PATH, "wb");
    if(fd == NULL) return;
    fprintf(fd, "sound_on_done=%d\n", g_sound_on_done ? 1 : 0);
    fclose(fd);
}

void extras_init_lazy(void) {
    if(g_extras_inited) return;
    extras_load();
    g_extras_inited = true;
}

u64 extras_sd_free_bytes(void) {
    FS_ArchiveResource resource = {0};
    if(R_SUCCEEDED(FSUSER_GetArchiveResource(&resource, SYSTEM_MEDIATYPE_SD))) {
        return (u64) resource.freeClusters * (u64) resource.clusterSize;
    }
    return 0;
}

void extras_format_bytes(char* out, size_t outSize, u64 bytes) {
    snprintf(out, outSize, "%.2f %s", ui_get_display_size(bytes), ui_get_display_size_units(bytes));
}

bool extras_sd_has_space(u64 needBytes, u64* haveOut) {
    u64 have = extras_sd_free_bytes();
    if(haveOut) *haveOut = have;
    /* 1 MiB margin for filesystem overhead */
    u64 need = needBytes + 1024ULL * 1024ULL;
    return have >= need;
}

bool extras_battery_ok_for_long_job(void) {
    u8 charging = 0;
    u8 level = 5;
    if(R_SUCCEEDED(PTMU_GetBatteryChargeState(&charging)) && charging) {
        return true;
    }
    if(R_SUCCEEDED(PTMU_GetBatteryLevel(&level))) {
        /* libctru: 0=empty .. 5=full. Warn at 0 or 1. */
        return level >= 2;
    }
    return true;
}

u8 extras_battery_percent_approx(void) {
    u8 level = 5;
    PTMU_GetBatteryLevel(&level);
    return level;
}

void extras_history_append(const char* name, const char* result) {
    extras_ensure_dir();
    FILE* fd = fopen("sdmc:" HISTORY_PATH, "ab");
    if(fd == NULL) return;
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char ts[32];
    if(tm != NULL) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    } else {
        string_copy(ts, "?", sizeof(ts));
    }
    fprintf(fd, "%s | %s | %s\n", ts, name != NULL ? name : "?", result != NULL ? result : "?");
    fclose(fd);
}

bool extras_sound_on_done(void) {
    extras_init_lazy();
    return g_sound_on_done;
}

void extras_set_sound_on_done(bool on) {
    extras_init_lazy();
    g_sound_on_done = on;
    extras_save();
}

void extras_play_done_sound(void) {
    if(!extras_sound_on_done()) return;
    /* Intentionally light: avoid NDSP/CSND init for stability. UI already shows Done! */
}

void extras_install_begin(void) {
    g_prev_sleep = true;
    aptSetSleepAllowed(false);
    g_install_start_ms = osGetTime();
    g_install_elapsed_sec = 0;
    g_install_active = true;
}

void extras_install_end(void) {
    if(g_install_active) {
        u64 now = osGetTime();
        if(now >= g_install_start_ms) {
            g_install_elapsed_sec = (u32) ((now - g_install_start_ms) / 1000ULL);
        }
        aptSetSleepAllowed(g_prev_sleep);
        g_install_active = false;
    }
}

u32 extras_install_elapsed_seconds(void) {
    if(g_install_active) {
        u64 now = osGetTime();
        if(now < g_install_start_ms) return 0;
        return (u32) ((now - g_install_start_ms) / 1000ULL);
    }
    return g_install_elapsed_sec;
}

/* ---- Recent installs UI ---- */
static void history_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) view; (void) data; (void) selected;
    const char* tip = "These are games and apps you installed recently with New3DS FBI.";
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.5f, 0.5f, x2 - x1 - 24.0f);
    screen_draw_string_wrap(tip, x1 + 12.0f, y1 + 24.0f, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 12.0f);
}

static void history_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    (void) data; (void) selected; (void) selectedTouched;
    if(hidKeysDown() & KEY_B) {
        /* free heap items */
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* it = (list_item*) linked_list_iter_next(&iter);
            free(it);
        }
        linked_list_clear(items);
        ui_pop();
        list_destroy(view);
        return;
    }
    if(linked_list_size(items) == 0) {
        FILE* fd = fopen("sdmc:" HISTORY_PATH, "rb");
        if(fd == NULL) {
            list_item* empty = (list_item*) calloc(1, sizeof(list_item));
            if(empty) {
                string_copy(empty->name, "(No installs logged yet)", sizeof(empty->name));
                empty->color = COLOR_HINT;
                linked_list_add(items, empty);
            }
            return;
        }
        /* Read all lines, keep last N */
        char* lines[HISTORY_MAX_LINES];
        int count = 0;
        char buf[384];
        memset(lines, 0, sizeof(lines));
        while(fgets(buf, sizeof(buf), fd) != NULL) {
            size_t len = strlen(buf);
            while(len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
            if(len == 0) continue;
            char* copy = (char*) malloc(len + 1);
            if(!copy) continue;
            memcpy(copy, buf, len + 1);
            if(count < HISTORY_MAX_LINES) {
                lines[count++] = copy;
            } else {
                free(lines[0]);
                memmove(&lines[0], &lines[1], sizeof(char*) * (HISTORY_MAX_LINES - 1));
                lines[HISTORY_MAX_LINES - 1] = copy;
            }
        }
        fclose(fd);
        for(int i = count - 1; i >= 0; i--) {
            list_item* it = (list_item*) calloc(1, sizeof(list_item));
            if(it) {
                string_copy(it->name, lines[i], sizeof(it->name));
                it->color = COLOR_TEXT;
                linked_list_add(items, it);
            }
            free(lines[i]);
        }
        if(count == 0) {
            list_item* empty = (list_item*) calloc(1, sizeof(list_item));
            if(empty) {
                string_copy(empty->name, "(No installs logged yet)", sizeof(empty->name));
                empty->color = COLOR_HINT;
                linked_list_add(items, empty);
            }
        }
    }
}

void extras_history_open(void) {
    list_display("Recent installs", "B = back", NULL, history_update, history_draw_top);
}
