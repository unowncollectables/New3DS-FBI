#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <3ds.h>
#include <jansson.h>

#include "resources.h"
#include "section.h"
#include "selfupdate.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"

#define SELFUPDATE_API_URL "https://api.github.com/repos/unowncollectables/New3DS-FBI/releases/latest"
#define SELFUPDATE_CFG_PATH "/fbi/selfupdate.cfg"
#define SELFUPDATE_CIA_NAME "New3DS-FBI.cia"

typedef struct {
    bool checkEnabled;
    bool available;
    bool checking;
    bool checked;
    char latestTag[SELFUPDATE_TAG_MAX];
    char displayTag[SELFUPDATE_TAG_MAX];
    char body[SELFUPDATE_BODY_MAX];
    char ciaUrl[SELFUPDATE_URL_MAX];
    char skipTag[SELFUPDATE_TAG_MAX];
    int remoteMajor;
    int remoteMinor;
    int remoteMicro;
} selfupdate_state;

static selfupdate_state g_su;
static LightLock g_su_lock;
static bool g_su_inited = false;
static Thread g_su_thread = NULL;

static void selfupdate_ensure_dir(void) {
    FS_Archive sdmc = 0;
    if(R_SUCCEEDED(FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        fs_ensure_dir(sdmc, "/fbi/");
        FSUSER_CloseArchive(sdmc);
    }
}

static void selfupdate_load_cfg(void) {
    g_su.checkEnabled = true;
    g_su.skipTag[0] = '\0';

    selfupdate_ensure_dir();

    FILE* fd = fopen("sdmc:" SELFUPDATE_CFG_PATH, "rb");
    if(fd == NULL) {
        return;
    }

    char line[160];
    while(fgets(line, sizeof(line), fd) != NULL) {
        char key[64];
        char val[96];
        if(sscanf(line, "%63[^=]=%95s", key, val) == 2) {
            if(strcasecmp(key, "check_enabled") == 0) {
                g_su.checkEnabled = !(val[0] == '0' || strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0);
            } else if(strcasecmp(key, "skip_tag") == 0) {
                string_copy(g_su.skipTag, val, sizeof(g_su.skipTag));
            }
        }
    }

    fclose(fd);
}

static void selfupdate_save_cfg(void) {
    selfupdate_ensure_dir();

    FILE* fd = fopen("sdmc:" SELFUPDATE_CFG_PATH, "wb");
    if(fd == NULL) {
        return;
    }

    fprintf(fd, "check_enabled=%d\n", g_su.checkEnabled ? 1 : 0);
    fprintf(fd, "skip_tag=%s\n", g_su.skipTag);
    fclose(fd);
}

/* Parse leading semver from tags like "v3.1.0", "3.1.0-new3ds". */
static bool selfupdate_parse_semver(const char* tag, int* major, int* minor, int* micro) {
    if(tag == NULL || tag[0] == '\0') {
        return false;
    }

    const char* p = tag;
    if(*p == 'v' || *p == 'V') {
        p++;
    }

    int maj = 0, min = 0, mic = 0;
    int n = sscanf(p, "%d.%d.%d", &maj, &min, &mic);
    if(n < 2) {
        return false;
    }

    *major = maj;
    *minor = min;
    *micro = (n >= 3) ? mic : 0;
    return true;
}

static int selfupdate_cmp_semver(int aMaj, int aMin, int aMic, int bMaj, int bMin, int bMic) {
    if(aMaj != bMaj) {
        return (aMaj > bMaj) ? 1 : -1;
    }
    if(aMin != bMin) {
        return (aMin > bMin) ? 1 : -1;
    }
    if(aMic != bMic) {
        return (aMic > bMic) ? 1 : -1;
    }
    return 0;
}

static void selfupdate_apply_json(json_t* json) {
    if(!json_is_object(json)) {
        return;
    }

    json_t* tagName = json_object_get(json, "tag_name");
    if(!json_is_string(tagName)) {
        tagName = json_object_get(json, "name");
    }
    if(!json_is_string(tagName)) {
        return;
    }

    const char* tag = json_string_value(tagName);
    int rMaj = 0, rMin = 0, rMic = 0;
    if(!selfupdate_parse_semver(tag, &rMaj, &rMin, &rMic)) {
        return;
    }

    char ciaUrl[SELFUPDATE_URL_MAX];
    ciaUrl[0] = '\0';

    json_t* assets = json_object_get(json, "assets");
    if(json_is_array(assets)) {
        for(size_t i = 0; i < json_array_size(assets); i++) {
            json_t* val = json_array_get(assets, i);
            if(!json_is_object(val)) {
                continue;
            }
            json_t* assetName = json_object_get(val, "name");
            json_t* assetUrl = json_object_get(val, "browser_download_url");
            if(!json_is_string(assetName) || !json_is_string(assetUrl)) {
                continue;
            }
            if(strcasecmp(json_string_value(assetName), SELFUPDATE_CIA_NAME) == 0) {
                string_copy(ciaUrl, json_string_value(assetUrl), sizeof(ciaUrl));
                break;
            }
        }
    }

    if(ciaUrl[0] == '\0') {
        return;
    }

    char bodyBuf[SELFUPDATE_BODY_MAX];
    bodyBuf[0] = '\0';
    json_t* body = json_object_get(json, "body");
    if(json_is_string(body)) {
        string_copy(bodyBuf, json_string_value(body), sizeof(bodyBuf));
    }

    int newer = selfupdate_cmp_semver(rMaj, rMin, rMic, VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO) > 0;

    LightLock_Lock(&g_su_lock);
    string_copy(g_su.latestTag, tag, sizeof(g_su.latestTag));
    snprintf(g_su.displayTag, sizeof(g_su.displayTag), "v%d.%d.%d", rMaj, rMin, rMic);
    string_copy(g_su.body, bodyBuf, sizeof(g_su.body));
    string_copy(g_su.ciaUrl, ciaUrl, sizeof(g_su.ciaUrl));
    g_su.remoteMajor = rMaj;
    g_su.remoteMinor = rMin;
    g_su.remoteMicro = rMic;

    bool skipped = (g_su.skipTag[0] != '\0' && strcmp(g_su.skipTag, g_su.latestTag) == 0);
    g_su.available = newer && !skipped && g_su.ciaUrl[0] != '\0';
    g_su.checked = true;
    LightLock_Unlock(&g_su_lock);
}

static void selfupdate_check_thread(void* arg) {
    (void) arg;

    json_t* json = NULL;
    /* Silent on any failure (offline, timeout, bad JSON). */
    if(R_SUCCEEDED(http_download_json(SELFUPDATE_API_URL, &json, 48 * 1024))) {
        selfupdate_apply_json(json);
        json_decref(json);
    }

    LightLock_Lock(&g_su_lock);
    g_su.checking = false;
    g_su.checked = true;
    LightLock_Unlock(&g_su_lock);

    g_su_thread = NULL;
}

void selfupdate_init(void) {
    if(g_su_inited) {
        return;
    }

    memset(&g_su, 0, sizeof(g_su));
    LightLock_Init(&g_su_lock);
    selfupdate_load_cfg();
    g_su_inited = true;
}

void selfupdate_exit(void) {
    if(!g_su_inited) {
        return;
    }

    /* Detached threads exit on their own; nothing to join safely here. */
    g_su_inited = false;
}

void selfupdate_start_background_check(void) {
    if(!g_su_inited) {
        selfupdate_init();
    }

    LightLock_Lock(&g_su_lock);
    bool enabled = g_su.checkEnabled;
    bool busy = g_su.checking;
    if(enabled && !busy) {
        g_su.checking = true;
    }
    LightLock_Unlock(&g_su_lock);

    if(!enabled || busy) {
        return;
    }

    g_su_thread = threadCreate(selfupdate_check_thread, NULL, 0x10000, 0x1A, 1, true);
    if(g_su_thread == NULL) {
        LightLock_Lock(&g_su_lock);
        g_su.checking = false;
        LightLock_Unlock(&g_su_lock);
    }
}

bool selfupdate_is_available(void) {
    LightLock_Lock(&g_su_lock);
    bool v = g_su.available;
    LightLock_Unlock(&g_su_lock);
    return v;
}

bool selfupdate_check_enabled(void) {
    LightLock_Lock(&g_su_lock);
    bool v = g_su.checkEnabled;
    LightLock_Unlock(&g_su_lock);
    return v;
}

void selfupdate_set_check_enabled(bool enabled) {
    LightLock_Lock(&g_su_lock);
    g_su.checkEnabled = enabled;
    if(!enabled) {
        g_su.available = false;
    }
    selfupdate_save_cfg();
    LightLock_Unlock(&g_su_lock);
}

const char* selfupdate_latest_tag(void) {
    return g_su.latestTag;
}

const char* selfupdate_display_tag(void) {
    return g_su.displayTag[0] ? g_su.displayTag : g_su.latestTag;
}

const char* selfupdate_body(void) {
    return g_su.body;
}

const char* selfupdate_cia_url(void) {
    return g_su.ciaUrl;
}

void selfupdate_skip_latest(void) {
    LightLock_Lock(&g_su_lock);
    if(g_su.latestTag[0] != '\0') {
        string_copy(g_su.skipTag, g_su.latestTag, sizeof(g_su.skipTag));
        g_su.available = false;
        selfupdate_save_cfg();
    }
    LightLock_Unlock(&g_su_lock);
}

bool selfupdate_is_skipped(void) {
    LightLock_Lock(&g_su_lock);
    bool v = (g_su.skipTag[0] != '\0' && strcmp(g_su.skipTag, g_su.latestTag) == 0);
    LightLock_Unlock(&g_su_lock);
    return v;
}

/* ---- Update screen UI ---- */

static list_item su_install = {"Install update", COLOR_SUCCESS, NULL};
static list_item su_changelog = {"View changelog", COLOR_TEXT, NULL};
static list_item su_toggle = {"Auto-check: On", COLOR_TEXT, NULL};
static list_item su_skip = {"Skip this version", COLOR_HINT, NULL};
static list_item su_refresh = {"Check now", COLOR_TEXT, NULL};

static void selfupdate_refresh_labels(void) {
    bool enabled = selfupdate_check_enabled();
    snprintf(su_toggle.name, LIST_ITEM_NAME_MAX, "Auto-check: %s", enabled ? "On" : "Off");
    su_toggle.color = enabled ? COLOR_ENABLED : COLOR_DISABLED;

    if(selfupdate_is_available()) {
        snprintf(su_install.name, LIST_ITEM_NAME_MAX, "Install %s", selfupdate_display_tag());
        su_install.color = COLOR_SUCCESS;
    } else {
        snprintf(su_install.name, LIST_ITEM_NAME_MAX, "Install update");
        su_install.color = COLOR_HINT;
    }
}

static void selfupdate_finished_all(void* data) {
    (void) data;
    prompt_display_notify(
        "Update installed",
        "New3DS FBI was updated.\nPlease restart the app\n(Home Menu → close → reopen).",
        COLOR_TEXT, NULL, NULL, NULL);
}

static void selfupdate_do_install(void) {
    const char* url = selfupdate_cia_url();
    if(url == NULL || url[0] == '\0') {
        prompt_display_notify("Update", "No CIA URL available.\nUse Check now first.", COLOR_TEXT, NULL, NULL, NULL);
        return;
    }

    char msg[192];
    snprintf(msg, sizeof(msg),
             "Install New3DS FBI %s?\nOverwrites this app only\n(title 0xF83FB). Settings kept.",
             selfupdate_display_tag());

    action_install_url_set_required_title_id(SELFUPDATE_TITLE_ID);
    /* Requires Yes on confirm dialog inside action_install_url. */
    action_install_url(msg, url, NULL, NULL, NULL, selfupdate_finished_all, NULL);
}

static void selfupdate_show_changelog(void) {
    const char* body = selfupdate_body();
    if(body == NULL || body[0] == '\0') {
        prompt_display_notify("Changelog", "No changelog text\nfor this release.", COLOR_TEXT, NULL, NULL, NULL);
        return;
    }

    /* Truncate for the notify dialog. */
    char buf[512];
    string_copy(buf, body, sizeof(buf));
    prompt_display_notify("Changelog", buf, COLOR_TEXT, NULL, NULL, NULL);
}

static void selfupdate_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) view;
    (void) data;
    (void) selected;

    char line[160];
    snprintf(line, sizeof(line), "Running: v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);

    float w, h;
    screen_get_string_size(&w, &h, line, 0.55f, 0.55f);
    float y = y1 + 16.0f;
    screen_draw_string(line, x1 + (x2 - x1 - w) / 2, y, 0.55f, 0.55f, COLOR_TEXT, true);
    y += h + 10.0f;

    if(selfupdate_is_available()) {
        snprintf(line, sizeof(line), "Update available: %s", selfupdate_display_tag());
        screen_get_string_size(&w, &h, line, 0.5f, 0.5f);
        screen_draw_string(line, x1 + (x2 - x1 - w) / 2, y, 0.5f, 0.5f, COLOR_SUCCESS, true);
        y += h + 8.0f;
    } else if(selfupdate_check_enabled()) {
        const char* msg = g_su.checked ? "You are up to date (or offline)." : "Checking in the background…";
        screen_get_string_size(&w, &h, msg, 0.45f, 0.45f);
        screen_draw_string(msg, x1 + (x2 - x1 - w) / 2, y, 0.45f, 0.45f, COLOR_HINT, true);
        y += h + 8.0f;
    } else {
        const char* msg = "Auto-check is off.";
        screen_get_string_size(&w, &h, msg, 0.45f, 0.45f);
        screen_draw_string(msg, x1 + (x2 - x1 - w) / 2, y, 0.45f, 0.45f, COLOR_HINT, true);
        y += h + 8.0f;
    }

    const char* tip = "Silent check on launch.\nNo popups. Install only when you choose.";
    screen_get_string_size_wrap(&w, &h, tip, 0.45f, 0.45f, x2 - x1 - 24.0f);
    screen_draw_string_wrap(tip, x1 + 12.0f, y2 - h - 20.0f, 0.45f, 0.45f, COLOR_HINT, true, x2 - 12.0f);
}

static void selfupdate_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    (void) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        return;
    }

    selfupdate_refresh_labels();

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected == &su_install) {
            if(selfupdate_is_available() || (selfupdate_cia_url()[0] != '\0')) {
                selfupdate_do_install();
            } else {
                prompt_display_notify("Update", "No newer release found.\nTry Check now.", COLOR_TEXT, NULL, NULL, NULL);
            }
            return;
        }
        if(selected == &su_changelog) {
            selfupdate_show_changelog();
            return;
        }
        if(selected == &su_toggle) {
            selfupdate_set_check_enabled(!selfupdate_check_enabled());
            if(selfupdate_check_enabled()) {
                selfupdate_start_background_check();
            }
            return;
        }
        if(selected == &su_skip) {
            if(g_su.latestTag[0] != '\0') {
                selfupdate_skip_latest();
                prompt_display_notify("Skipped", "This version will not\nshow the badge.", COLOR_TEXT, NULL, NULL, NULL);
            }
            return;
        }
        if(selected == &su_refresh) {
            LightLock_Lock(&g_su_lock);
            g_su.checked = false;
            LightLock_Unlock(&g_su_lock);
            selfupdate_start_background_check();
            prompt_display_notify("Checking", "Looking for updates\nin the background…", COLOR_TEXT, NULL, NULL, NULL);
            return;
        }
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &su_install);
        linked_list_add(items, &su_changelog);
        linked_list_add(items, &su_toggle);
        linked_list_add(items, &su_skip);
        linked_list_add(items, &su_refresh);
    }
}

void selfupdate_open(void) {
    selfupdate_refresh_labels();
    if(selfupdate_check_enabled()) {
        selfupdate_start_background_check();
    }
    list_display("Update New3DS FBI", "A: Select, B: Back", NULL, selfupdate_update, selfupdate_draw_top);
}
