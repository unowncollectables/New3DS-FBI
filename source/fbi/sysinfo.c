#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "extras.h"
#include "sysinfo.h"
#include "../core/core.h"

typedef struct {
    char body[1400];
} sysinfo_data;

static const char* sysinfo_model_name(u8 model) {
    switch(model) {
        case CFG_MODEL_3DS: return "Old 3DS";
        case CFG_MODEL_3DSXL: return "Old 3DS XL";
        case CFG_MODEL_N3DS: return "New 3DS";
        case CFG_MODEL_2DS: return "Old 2DS";
        case CFG_MODEL_N3DSXL: return "New 3DS XL";
        case CFG_MODEL_N2DSXL: return "New 2DS XL";
        default: return "Unknown";
    }
}

static const char* sysinfo_region_name(u8 region) {
    switch(region) {
        case CFG_REGION_JPN: return "Japan";
        case CFG_REGION_USA: return "USA";
        case CFG_REGION_EUR: return "Europe";
        case CFG_REGION_AUS: return "Australia";
        case CFG_REGION_CHN: return "China";
        case CFG_REGION_KOR: return "Korea";
        case CFG_REGION_TWN: return "Taiwan";
        default: return "Unknown";
    }
}

static void sysinfo_try_luma(char* out, size_t outSize) {
    out[0] = '\0';
    /* Luma often writes a version string under /luma/ or CONFIG. Read config.ini if present. */
    FILE* f = fopen("sdmc:/luma/config.ini", "rb");
    if(f == NULL) {
        f = fopen("sdmc:/luma/config.cfg", "rb");
    }
    if(f == NULL) {
        string_copy(out, "(not found on SD)", outSize);
        return;
    }
    char line[256];
    while(fgets(line, sizeof(line), f) != NULL) {
        /* Look for a comment or key mentioning version; many builds put nothing useful.
           Fallback: if luma folder exists, say "Luma3DS (folder found)". */
        (void) line;
        break;
    }
    fclose(f);
    string_copy(out, "Luma3DS (config found on SD)", outSize);

    /* Prefer /luma/version.txt if a user/tool left one. */
    FILE* v = fopen("sdmc:/luma/version.txt", "rb");
    if(v != NULL) {
        if(fgets(line, sizeof(line), v) != NULL) {
            size_t n = strlen(line);
            while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            if(n > 0) {
                snprintf(out, outSize, "%s", line);
            }
        }
        fclose(v);
    }
}

static void sysinfo_refresh(sysinfo_data* data) {
    char* b = data->body;
    size_t left = sizeof(data->body);
    int n;

    u8 model = 0xFF;
    CFGU_GetSystemModel(&model);

    u8 region = 0xFF;
    CFGU_SecureInfoGetRegion(&region);

    OS_VersionBin nver = {0}, cver = {0};
    char sysver[64] = "?";
    if(R_SUCCEEDED(osGetSystemVersionDataString(&nver, &cver, sysver, sizeof(sysver)))) {
        /* sysver filled */
    } else {
        snprintf(sysver, sizeof(sysver), "%u.%u.%u-%u",
                 (unsigned) cver.mainver, (unsigned) cver.minor, (unsigned) cver.build, (unsigned) nver.mainver);
    }

    char luma[96];
    sysinfo_try_luma(luma, sizeof(luma));

    u8 charging = 0;
    u8 batLevel = 0;
    u8 batPct = 0;
    PTMU_GetBatteryChargeState(&charging);
    PTMU_GetBatteryLevel(&batLevel);
    /* Prefer MCU percent when available */
    bool havePct = false;
    if(R_SUCCEEDED(mcuHwcInit())) {
        if(R_SUCCEEDED(MCUHWC_GetBatteryLevel(&batPct))) {
            havePct = true;
        }
        mcuHwcExit();
    }

    char batStr[48];
    if(havePct) {
        snprintf(batStr, sizeof(batStr), "%u%%%s", (unsigned) batPct, charging ? " (charging)" : "");
    } else {
        static const char* bars[] = {"empty", "~20%", "~40%", "~60%", "~80%", "full"};
        const char* label = (batLevel <= 5) ? bars[batLevel] : "?";
        snprintf(batStr, sizeof(batStr), "%s%s", label, charging ? " (charging)" : "");
    }

    FS_ArchiveResource sd = {0};
    char sdTotal[32] = "?", sdFree[32] = "?";
    if(R_SUCCEEDED(FSUSER_GetArchiveResource(&sd, SYSTEM_MEDIATYPE_SD))) {
        u64 total = (u64) sd.totalClusters * (u64) sd.clusterSize;
        u64 freeB = (u64) sd.freeClusters * (u64) sd.clusterSize;
        extras_format_bytes(sdTotal, sizeof(sdTotal), total);
        extras_format_bytes(sdFree, sizeof(sdFree), freeB);
    }

    u32 ip = (u32) gethostid();
    char ipStr[32];
    if(ip == 0 || ip == 0xFFFFFFFF) {
        string_copy(ipStr, "(not connected)", sizeof(ipStr));
    } else {
        snprintf(ipStr, sizeof(ipStr), "%lu.%lu.%lu.%lu",
                 (unsigned long) (ip & 0xFF),
                 (unsigned long) ((ip >> 8) & 0xFF),
                 (unsigned long) ((ip >> 16) & 0xFF),
                 (unsigned long) ((ip >> 24) & 0xFF));
    }

    char macStr[32] = "(unknown)";
    u8 mac[6] = {0};
    socklen_t macLen = sizeof(mac);
    if(SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_MAC_ADDRESS, mac, &macLen) == 0) {
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    char memStr[48] = "?";
    {
        char tmp[32];
        u32 freeApp = osGetMemRegionFree(MEMREGION_APPLICATION);
        extras_format_bytes(tmp, sizeof(tmp), freeApp);
        snprintf(memStr, sizeof(memStr), "%s (app region)", tmp);
    }

    n = snprintf(b, left,
        "Model: %s\n"
        "System version: %s\n"
        "Luma: %s\n"
        "Region: %s\n"
        "Battery: %s\n"
        "SD card: %s free / %s total\n"
        "IP address: %s\n"
        "MAC address: %s\n"
        "Free memory: %s\n"
        "\n"
        "(Serial numbers, keys, and friend\n"
        "codes are hidden on purpose.)",
        sysinfo_model_name(model),
        sysver,
        luma,
        sysinfo_region_name(region),
        batStr,
        sdFree, sdTotal,
        ipStr,
        macStr,
        memStr);
    (void) n;
}

static void sysinfo_update(ui_view* view, void* data, float* progress, char* text) {
    (void) progress;
    (void) text;
    sysinfo_data* info = (sysinfo_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        info_destroy(view);
        free(info);
        return;
    }

    if(hidKeysDown() & KEY_X) {
        sysinfo_refresh(info);
    }

    /* Keep body as progress text so bottom screen shows it */
    snprintf(text, PROGRESS_TEXT_MAX, "%s", info->body);
}

static void sysinfo_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    (void) data;
    const char* tip =
        "About my 3DS — read-only info.\n\n"
        "Nothing here can change your system.\n"
        "Press X to refresh. B = back.";
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.5f, 0.5f, x2 - x1 - 20.0f);
    screen_draw_string_wrap(tip, x1 + 10.0f, y1 + 24.0f, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 10.0f);
}

void sysinfo_open(void) {
    sysinfo_data* data = (sysinfo_data*) calloc(1, sizeof(sysinfo_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Out of memory.");
        return;
    }
    extras_init_lazy();
    sysinfo_refresh(data);
    info_display("About my 3DS", "X = refresh, B = back", false, data, sysinfo_update, sysinfo_draw_top);
}
