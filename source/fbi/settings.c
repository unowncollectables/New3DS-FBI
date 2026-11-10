#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "settings.h"
#include "selfupdate.h"
#include "extras.h"
#include "../core/core.h"

#define SETTINGS_CFG_PATH "/fbi/settings.cfg"

static bool g_show_advanced = false;
static bool g_advanced_warned = false;
static bool g_ftp_keep_awake = true;
static bool g_dark_theme = false;
static u32 g_accent = 0; /* Blue */
static char g_ftp_password[FTP_PASSWORD_MAX];
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
    g_ftp_keep_awake = true;
    g_dark_theme = false;
    g_accent = 0;
    g_ftp_password[0] = '\0';

    settings_ensure_dir();
    FILE* fd = fopen("sdmc:" SETTINGS_CFG_PATH, "rb");
    if(fd == NULL) {
        return;
    }

    char line[192];
    while(fgets(line, sizeof(line), fd) != NULL) {
        char key[64];
        char val[128];
        if(sscanf(line, "%63[^=]=%127[^\n]", key, val) == 2) {
            if(strcasecmp(key, "show_advanced") == 0) {
                g_show_advanced = (val[0] == '1' || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0);
            } else if(strcasecmp(key, "advanced_warned") == 0) {
                g_advanced_warned = (val[0] == '1' || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0);
            } else if(strcasecmp(key, "ftp_keep_awake") == 0) {
                g_ftp_keep_awake = !(val[0] == '0' || strcasecmp(val, "false") == 0 || strcasecmp(val, "off") == 0);
            } else if(strcasecmp(key, "ftp_password") == 0) {
                string_copy(g_ftp_password, val, sizeof(g_ftp_password));
            } else if(strcasecmp(key, "dark_theme") == 0 || strcasecmp(key, "theme") == 0) {
                g_dark_theme = (val[0] == '1' || strcasecmp(val, "true") == 0 || strcasecmp(val, "on") == 0
                                || strcasecmp(val, "dark") == 0);
            } else if(strcasecmp(key, "accent") == 0) {
                if(strcasecmp(val, "green") == 0) g_accent = 1;
                else if(strcasecmp(val, "purple") == 0) g_accent = 2;
                else if(strcasecmp(val, "orange") == 0) g_accent = 3;
                else if(val[0] >= '0' && val[0] <= '3') g_accent = (u32) (val[0] - '0');
                else g_accent = 0;
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
    fprintf(fd, "ftp_keep_awake=%d\n", g_ftp_keep_awake ? 1 : 0);
    fprintf(fd, "ftp_password=%s\n", g_ftp_password);
    fprintf(fd, "dark_theme=%d\n", g_dark_theme ? 1 : 0);
    fprintf(fd, "accent=%u\n", (unsigned) g_accent);
    fclose(fd);
}

void settings_init(void) {
    if(g_inited) {
        return;
    }
    settings_load();
    resources_set_dark(g_dark_theme);
    resources_set_accent(g_accent);
    g_inited = true;
}

void settings_exit(void) {
    g_inited = false;
}

bool settings_show_advanced(void) {
    if(!g_inited) settings_init();
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

const char* settings_ftp_password(void) {
    if(!g_inited) settings_init();
    return g_ftp_password;
}

void settings_set_ftp_password(const char* password) {
    if(password == NULL) {
        g_ftp_password[0] = '\0';
    } else {
        string_copy(g_ftp_password, password, sizeof(g_ftp_password));
    }
    settings_save();
}

bool settings_ftp_keep_awake(void) {
    if(!g_inited) settings_init();
    return g_ftp_keep_awake;
}

void settings_set_ftp_keep_awake(bool keep) {
    g_ftp_keep_awake = keep;
    settings_save();
}

bool settings_dark_theme(void) {
    if(!g_inited) settings_init();
    return g_dark_theme;
}

void settings_set_dark_theme(bool dark) {
    g_dark_theme = dark;
    settings_save();
    resources_set_dark(dark);
}

static const char* settings_accent_name(u32 a) {
    switch(a) {
        case 1: return "Green";
        case 2: return "Purple";
        case 3: return "Orange";
        default: return "Blue";
    }
}

u32 settings_accent(void) {
    if(!g_inited) settings_init();
    return g_accent;
}

void settings_set_accent(u32 accent) {
    if(accent > 3) accent = 0;
    g_accent = accent;
    settings_save();
    resources_set_accent(accent);
}

static list_item s_auto_check = {"Check for updates on start: On", COLOR_TEXT, NULL};
static list_item s_advanced = {"Show Advanced tools: Off", COLOR_TEXT, NULL};
static list_item s_ftp_pass = {"FTP password: None", COLOR_TEXT, NULL};
static list_item s_ftp_awake = {"FTP stay awake: On", COLOR_TEXT, NULL};
static list_item s_theme = {"Theme: Light", COLOR_TEXT, NULL};
static list_item s_accent = {"Accent colour: Blue", COLOR_ACCENT, NULL};
static list_item s_sound = {"Show install time: On", COLOR_TEXT, NULL};
static list_item s_about_hint = {"About / Credits", COLOR_HINT, NULL};

static void settings_refresh_labels(void) {
    bool autoCheck = selfupdate_check_enabled();
    snprintf(s_auto_check.name, LIST_ITEM_NAME_MAX, "Check for updates on start: %s", autoCheck ? "On" : "Off");
    s_auto_check.color = autoCheck ? COLOR_ENABLED : COLOR_DISABLED;

    bool adv = settings_show_advanced();
    snprintf(s_advanced.name, LIST_ITEM_NAME_MAX, "Show Advanced tools: %s", adv ? "On" : "Off");
    s_advanced.color = adv ? COLOR_WARNING : COLOR_TEXT;

    if(g_ftp_password[0] == '\0') {
        snprintf(s_ftp_pass.name, LIST_ITEM_NAME_MAX, "FTP password: None");
    } else {
        snprintf(s_ftp_pass.name, LIST_ITEM_NAME_MAX, "FTP password: Set");
    }

    snprintf(s_ftp_awake.name, LIST_ITEM_NAME_MAX, "FTP stay awake: %s",
             g_ftp_keep_awake ? "On" : "Off");
    s_ftp_awake.color = g_ftp_keep_awake ? COLOR_ENABLED : COLOR_DISABLED;

    bool sound = extras_sound_on_done();
    snprintf(s_sound.name, LIST_ITEM_NAME_MAX, "Show install time: %s", sound ? "On" : "Off");
    s_sound.color = sound ? COLOR_ENABLED : COLOR_DISABLED;

    snprintf(s_theme.name, LIST_ITEM_NAME_MAX, "Theme: %s", g_dark_theme ? "Dark" : "Light");
    s_theme.color = g_dark_theme ? COLOR_ACCENT : COLOR_TEXT;

    snprintf(s_accent.name, LIST_ITEM_NAME_MAX, "Accent colour: %s", settings_accent_name(g_accent));
    s_accent.color = COLOR_ACCENT;
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

static void settings_ftp_pass_onresponse(ui_view* view, void* data, SwkbdButton button, const char* response) {
    (void) view;
    (void) data;
    if(button == SWKBD_BUTTON_CONFIRM) {
        settings_set_ftp_password(response);
        if(response != NULL && response[0] != '\0') {
            prompt_display_notify(
                "Password saved",
                "FTP will ask for this password.\n"
                "Still only use FTP on home Wi-Fi.",
                COLOR_TEXT, NULL, NULL, NULL);
        } else {
            prompt_display_notify(
                "No password",
                "Anyone on your Wi-Fi can open\n"
                "the FTP folder. Home Wi-Fi only!",
                COLOR_WARNING, NULL, NULL, NULL);
        }
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
    } else if(selected == &s_ftp_pass) {
        tip = "Optional password for Send files from your PC (FTP). Leave empty for easy use at home. Only use it on your home Wi-Fi.";
    } else if(selected == &s_ftp_awake) {
        tip = "When On, the console will not go to sleep while the Send files from your PC screen is open, so transfers are not cut off.";
    } else if(selected == &s_theme) {
        tip = "Light or Dark look. Changes right away. A custom theme in sdmc:/fbi/theme/ still wins.";
    } else if(selected == &s_accent) {
        tip = "Colour for the top and bottom bars, selected rows, progress bars, and buttons. Try Green!";
    } else if(selected == &s_sound) {
        tip = "When On, the All done! message shows how long the install took. Recent installs are always saved.";
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
        if(selected == &s_ftp_pass) {
            kbd_display("FTP password (empty = none)", g_ftp_password, SWKBD_TYPE_NORMAL, 0, SWKBD_ANYTHING, FTP_PASSWORD_MAX - 1, NULL, settings_ftp_pass_onresponse);
            return;
        }
        if(selected == &s_ftp_awake) {
            settings_set_ftp_keep_awake(!settings_ftp_keep_awake());
            return;
        }
        if(selected == &s_theme) {
            if(resources_sd_color_override()) {
                prompt_display_notify(
                    "Custom theme on SD",
                    "A theme file is on your SD card:\n"
                    "sdmc:/fbi/theme/textcolor.cfg\n\n"
                    "That theme is used instead of\n"
                    "Light / Dark. Remove or rename\n"
                    "the file to use this setting.",
                    COLOR_TEXT, NULL, NULL, NULL);
            } else {
                settings_set_dark_theme(!settings_dark_theme());
            }
            return;
        }
        if(selected == &s_accent) {
            settings_set_accent((settings_accent() + 1) % 4);
            return;
        }
        if(selected == &s_sound) {
            extras_set_sound_on_done(!extras_sound_on_done());
            return;
        }
        if(selected == &s_about_hint) {
            about_open();
            return;
        }
    }

    /* FTP lives in Advanced, so its options only show when Advanced is on. */
    static bool listedAdv = false;
    bool adv = settings_show_advanced();
    if(linked_list_size(items) == 0 || adv != listedAdv) {
        linked_list_clear(items);
        linked_list_add(items, &s_theme);
        linked_list_add(items, &s_accent);
        linked_list_add(items, &s_auto_check);
        linked_list_add(items, &s_sound);
        linked_list_add(items, &s_advanced);
        if(adv) {
            linked_list_add(items, &s_ftp_pass);
            linked_list_add(items, &s_ftp_awake);
        }
        linked_list_add(items, &s_about_hint);
        listedAdv = adv;
    }
}

void settings_open(void) {
    settings_init();
    settings_refresh_labels();
    list_display("Settings", "A = choose, B = back", NULL, settings_update, settings_draw_top);
}
