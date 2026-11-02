#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "settings.h"
#include "selfupdate.h"
#include "../core/core.h"

#define SETTINGS_CFG_PATH "/fbi/settings.cfg"

static bool g_show_advanced = false;
static bool g_advanced_warned = false;
static bool g_inited = false;

static void settings_ensure_dir(void) {
    FS_Archive sdmc = 0;
    if(R_SUCCEEDED(FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        fs_ensure_dir(sdmc, "/fbi/");
        FSUSER_CloseArchive(sdmc);
    }
}

static void settings_load(void) {
    g_show_advanced = false;
    g_advanced_warned = false;

    settings_ensure_dir();
    FILE* fd = fopen("sdmc:" SETTINGS_CFG_PATH, "rb");
    if(fd == NULL) {
        return;
    }

    char line[160];
    while(fgets(line, sizeof(line), fd) != NULL) {
        char key[64];
        char val[96];
        if(sscanf(line, "%63[^=]=%95s", key, val) == 2) {
            if(strcasecmp(key, "show_advanced") == 0) {
                g_show_advanced = (val[0] == '1' || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0);
            } else if(strcasecmp(key, "advanced_warned") == 0) {
                g_advanced_warned = (val[0] == '1' || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0);
            }
        }
    }
    fclose(fd);
}

static void settings_save(void) {
    settings_ensure_dir();
    FILE* fd = fopen("sdmc:" SETTINGS_CFG_PATH, "wb");
    if(fd == NULL) {
        return;
    }
    fprintf(fd, "show_advanced=%d\n", g_show_advanced ? 1 : 0);
    fprintf(fd, "advanced_warned=%d\n", g_advanced_warned ? 1 : 0);
    fclose(fd);
}

void settings_init(void) {
    if(g_inited) {
        return;
    }
    settings_load();
    g_inited = true;
}

void settings_exit(void) {
    g_inited = false;
}

bool settings_show_advanced(void) {
    if(!g_inited) {
        settings_init();
    }
    return g_show_advanced;
}

void settings_set_show_advanced(bool show) {
    g_show_advanced = show;
    settings_save();
}

bool settings_advanced_warned(void) {
    return g_advanced_warned;
}

void settings_set_advanced_warned(bool warned) {
    g_advanced_warned = warned;
    settings_save();
}

static list_item s_auto_check = {"Check for app updates: On", COLOR_TEXT, NULL};
static list_item s_advanced = {"Show advanced tools: Off", COLOR_TEXT, NULL};
static list_item s_about_hint = {"About / Credits", COLOR_HINT, NULL};

static void settings_refresh_labels(void) {
    bool autoCheck = selfupdate_check_enabled();
    snprintf(s_auto_check.name, LIST_ITEM_NAME_MAX, "Check for app updates: %s", autoCheck ? "On" : "Off");
    s_auto_check.color = autoCheck ? COLOR_ENABLED : COLOR_DISABLED;

    bool adv = settings_show_advanced();
    snprintf(s_advanced.name, LIST_ITEM_NAME_MAX, "Show advanced tools: %s", adv ? "On" : "Off");
    s_advanced.color = adv ? COLOR_WARNING : COLOR_TEXT;
}

static void settings_advanced_onresponse(ui_view* view, void* data, u32 response) {
    (void) view;
    (void) data;
    if(response == PROMPT_YES) {
        settings_set_show_advanced(true);
        settings_set_advanced_warned(true);
    } else {
        settings_set_show_advanced(false);
    }
}

static void settings_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) view;
    (void) data;

    const char* tip = "Change simple options here.";
    if(selected == &s_auto_check) {
        tip = "When On, the app quietly looks for a newer version when it starts. It never pops up or makes noise.";
    } else if(selected == &s_advanced) {
        tip = "When On, an Advanced folder appears on the main menu with expert tools. Leave this Off unless you know you need it.";
    } else if(selected == &s_about_hint) {
        tip = "See who made this app and credits for FBI by Steveice10.";
    }

    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.5f, 0.5f, x2 - x1 - 24.0f);
    float y = y1 + (y2 - y1 - h) / 2.0f;
    if(y < y1 + 8.0f) y = y1 + 8.0f;
    screen_draw_string_wrap(tip, x1 + 12.0f, y, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 12.0f);

    const char* credit = "Based on FBI by Steveice10";
    screen_get_string_size(&w, &h, credit, 0.45f, 0.45f);
    screen_draw_string(credit, x1 + (x2 - x1 - w) / 2, y2 - h - 8.0f, 0.45f, 0.45f, COLOR_HINT, true);
}

static void settings_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    (void) data;
    settings_refresh_labels();

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected == &s_auto_check) {
            selfupdate_set_check_enabled(!selfupdate_check_enabled());
            if(selfupdate_check_enabled()) {
                selfupdate_start_background_check();
            }
            return;
        }
        if(selected == &s_advanced) {
            if(!settings_show_advanced()) {
                if(!settings_advanced_warned()) {
                    prompt_display_yes_no(
                        "Please read",
                        "Advanced tools can change system files.\n"
                        "A mistake could break games or the console.\n\n"
                        "Only turn this on if a grown-up who knows\n"
                        "3DS homebrew asked you to.\n\n"
                        "Show Advanced tools?",
                        COLOR_WARNING, NULL, NULL, settings_advanced_onresponse);
                } else {
                    settings_set_show_advanced(true);
                }
            } else {
                settings_set_show_advanced(false);
            }
            return;
        }
        if(selected == &s_about_hint) {
            prompt_display_notify(
                "About New3DS FBI",
                "New3DS FBI is built on FBI\n"
                "by Steveice10.\n\n"
                "Upstream:\n"
                "github.com/Steveice10/FBI\n\n"
                "This fork: New 2DS/3DS only.\n"
                "By unowncollectables.\n\n"
                "LICENSE stays Steveice10's.",
                COLOR_TEXT, NULL, NULL, NULL);
            return;
        }
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &s_auto_check);
        linked_list_add(items, &s_advanced);
        linked_list_add(items, &s_about_hint);
    }
}

void settings_open(void) {
    settings_init();
    settings_refresh_labels();
    list_display("Settings", "A = choose, B = back", NULL, settings_update, settings_draw_top);
}
