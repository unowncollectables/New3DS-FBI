#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "settings.h"
#include "selfupdate.h"
#include "ftp.h"
#include "extras.h"
#include "savebackup.h"
#include "sysinfo.h"
#include "task/uitask.h"
#include "../core/core.h"

/* Everyday menu (kid-friendly labels). */
static list_item mm_sd = {"My SD card files", COLOR_TEXT, files_open_sd};
static list_item mm_saves = {"Back up my saves", COLOR_TEXT, savebackup_open};
static list_item mm_sysinfo = {"About my 3DS", COLOR_TEXT, sysinfo_open};
static list_item mm_titles = {"My installed games", COLOR_TEXT, titles_open};
static list_item mm_pending = {"Unfinished installs", COLOR_TEXT, pendingtitles_open};
static list_item mm_tickets = {"Game tickets", COLOR_TEXT, tickets_open};
static list_item mm_extsave = {"Extra game saves", COLOR_TEXT, extsavedata_open};
static list_item mm_remote = {"Install from the internet", COLOR_TEXT, remoteinstall_open};
static list_item mm_batch = {"Install games from SD (/cias)", COLOR_TEXT, batchcias_open};
static list_item mm_ports = {"Update my Pokemon ports", COLOR_TEXT, portupdater_open};
static list_item mm_update = {"Update this app", COLOR_TEXT, update_open};
static list_item mm_history = {"Recent installs", COLOR_TEXT, extras_history_open};
static list_item mm_settings = {"Settings", COLOR_TEXT, settings_open};
static list_item mm_advanced = {"Advanced…", COLOR_WARNING, NULL};
static list_item mm_about = {"About / Credits", COLOR_HINT, NULL};

/* Advanced submenu — technical names kept. */
static list_item adv_ctr_nand = {"CTR NAND", COLOR_TEXT, files_open_ctr_nand};
static list_item adv_twl_nand = {"TWL NAND", COLOR_TEXT, files_open_twl_nand};
static list_item adv_twl_photo = {"TWL Photo", COLOR_TEXT, files_open_twl_photo};
static list_item adv_twl_sound = {"TWL Sound", COLOR_TEXT, files_open_twl_sound};
static list_item adv_dump_nand = {"Dump NAND", COLOR_TEXT, dumpnand_open};
static list_item adv_syssave = {"System Save Data", COLOR_TEXT, systemsavedata_open};
static list_item adv_ftp = {"Send files from your PC (FTP)", COLOR_TEXT, ftp_open};
static list_item adv_leftovers = {"Find leftover tickets / installs", COLOR_TEXT, leftovers_open};

static const char* mainmenu_tooltip(list_item* selected) {
    if(selected == &mm_sd) {
        return "Browse files on your SD card. Copy, move, rename, delete, or install a .cia (asks first).";
    }
    if(selected == &mm_saves) {
        return "Back up game saves to the SD card, or restore an older backup (always asks first).";
    }
    if(selected == &mm_sysinfo) {
        return "See model, system version, battery, SD space, and network info. Read-only.";
    }
    if(selected == &mm_titles) {
        return "See games and apps already installed on this system.";
    }
    if(selected == &mm_pending) {
        return "Finish or clear installs that did not complete.";
    }
    if(selected == &mm_tickets) {
        return "Tickets are little keys games need. Most people leave these alone.";
    }
    if(selected == &mm_extsave) {
        return "Browse extra save data some games store outside the title.";
    }
    if(selected == &mm_remote) {
        return "Install a game from a link, QR code, or GitHub. Safe titles only.";
    }
    if(selected == &mm_batch) {
        return "Install every game CIA waiting in the /cias folder on your SD card.";
    }
    if(selected == &mm_ports) {
        return "Update your SoulSilver or Platinum port from a CIA you built (no internet).";
    }
    if(selected == &mm_update) {
        if(selfupdate_is_available()) {
            return "A newer New3DS FBI is ready. Open this to read the notes and install it.";
        }
        return "Check for a newer New3DS FBI, read the notes, and install when you want.";
    }
    if(selected == &mm_history) {
        return "See games you installed recently with this app.";
    }
    if(selected == &mm_settings) {
        return "Turn options on or off, like update checks and Advanced tools.";
    }
    if(selected == &mm_advanced) {
        return "Expert tools that can change system files. Only open if you understand them.";
    }
    if(selected == &mm_about) {
        return "Credits — New3DS FBI is built on FBI by Steveice10.";
    }
    return "Choose an item with A.";
}

static const char* advanced_tooltip(list_item* selected) {
    if(selected == &adv_ctr_nand) {
        return "WARNING: System files live here. Changing them can break the console.";
    }
    if(selected == &adv_twl_nand) {
        return "WARNING: DS-mode system area. Changing it can break DS features.";
    }
    if(selected == &adv_twl_photo) {
        return "Nintendo DS Camera photo folder on the system.";
    }
    if(selected == &adv_twl_sound) {
        return "Nintendo DS Sound folder on the system.";
    }
    if(selected == &adv_dump_nand) {
        return "WARNING: Makes a huge copy of the whole system chip to your SD card.";
    }
    if(selected == &adv_syssave) {
        return "WARNING: System save data. Editing it can break system apps.";
    }
    if(selected == &adv_ftp) {
        return "Share your SD card with a computer over Wi-Fi. Easy copy of files. Home Wi-Fi only.";
    }
    if(selected == &adv_leftovers) {
        return "Find tickets or unfinished installs with no game. You choose what to delete.";
    }
    return "Advanced tool — be careful.";
}

static void about_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    (void) data;
    const char* tip =
        "New3DS FBI is built on FBI by Steveice10.\n\n"
        "Thank you Steveice10 — this app would not exist without that work.";
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.5f, 0.5f, x2 - x1 - 28.0f);
    float y = y1 + 16.0f;
    screen_draw_rect(x1 + 8.0f, y - 8.0f, (x2 - x1) - 16.0f, h + 16.0f, COLOR_PANEL, true);
    screen_draw_string_wrap(tip, x1 + 14.0f, y, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 14.0f);

    const char* credit = "Based on FBI by Steveice10";
    screen_get_string_size(&w, &h, credit, 0.45f, 0.45f);
    screen_draw_string(credit, x1 + (x2 - x1 - w) / 2, y2 - h - 6.0f, 0.45f, 0.45f, COLOR_HINT, true);
}

static void about_update(ui_view* view, void* data, float* progress, char* text) {
    (void) data;
    (void) progress;
    if(text != NULL) {
        text[0] = '\0';
    }
    if(hidKeysDown() & (KEY_B | KEY_A | KEY_START)) {
        ui_pop();
        info_destroy(view);
    }
}

static void about_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    (void) data;
    const float pad = 10.0f;
    float y = y1 + pad;
    float wrap = x2 - pad;

    char line[160];
    float w, h;

    snprintf(line, sizeof(line), "New3DS FBI  v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    screen_get_string_size(&w, &h, line, 0.6f, 0.6f);
    screen_draw_string(line, x1 + (x2 - x1 - w) / 2, y, 0.6f, 0.6f, COLOR_TEXT, true);
    y += h + 10.0f;

    const char* body =
        "Built on FBI by Steveice10\n"
        "(github.com/Steveice10/FBI)\n\n"
        "This fork is for New 2DS / New 3DS only.\n"
        "Safer installs, local Pokemon port updater,\n"
        "quiet self-update, and easy FTP.\n"
        "By unowncollectables.\n\n"
        "LICENSE / copyright for FBI remain\n"
        "Steveice10's. See LICENSE.txt.";
    screen_get_string_size_wrap(&w, &h, body, 0.48f, 0.48f, (x2 - x1) - pad * 2);
    screen_draw_string_wrap(body, x1 + pad, y, 0.48f, 0.48f, COLOR_TEXT, true, wrap);

    const char* hint = "A / B = back";
    screen_get_string_size(&w, &h, hint, 0.45f, 0.45f);
    screen_draw_string(hint, x1 + (x2 - x1 - w) / 2, y2 - h - 8.0f, 0.45f, 0.45f, COLOR_HINT, true);
}

void about_open(void) {
    ui_view* view = info_display("About / Credits", "A / B = back", false, NULL, about_update, about_draw_top);
    if(view != NULL) {
        view->drawBottom = about_draw_bottom;
    }
}

static void mainmenu_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) view;
    (void) data;

    u32 logoWidth = 0;
    u32 logoHeight = 0;
    screen_get_texture_size(&logoWidth, &logoHeight, TEXTURE_LOGO);

    /* Smaller logo so tooltip has room. */
    float maxLogoH = (y2 - y1) * 0.42f;
    float scale = 1.0f;
    if(logoHeight > 0 && logoHeight > maxLogoH) {
        scale = maxLogoH / (float) logoHeight;
    }
    float drawW = logoWidth * scale;
    float drawH = logoHeight * scale;
    float logoX = x1 + (x2 - x1 - drawW) / 2;
    float logoY = y1 + 6.0f;
    screen_draw_texture(TEXTURE_LOGO, logoX, logoY, drawW, drawH);

    if(selfupdate_is_available()) {
        char badge[64];
        snprintf(badge, sizeof(badge), "Update available (%s)", selfupdate_display_tag());
        float bw, bh;
        screen_get_string_size(&bw, &bh, badge, 0.45f, 0.45f);
        screen_draw_string(badge, x1 + (x2 - x1 - bw) / 2, logoY + drawH + 4.0f, 0.45f, 0.45f, COLOR_SUCCESS, true);
    }

    const char* tip = mainmenu_tooltip(selected);
    float tw, th;
    screen_get_string_size_wrap(&tw, &th, tip, 0.5f, 0.5f, x2 - x1 - 28.0f);
    float tipY = logoY + drawH + (selfupdate_is_available() ? 22.0f : 10.0f);
    if(tipY + th > y2 - 28.0f) {
        tipY = y2 - 28.0f - th;
        if(tipY < y1 + 4.0f) tipY = y1 + 4.0f;
    }
    screen_draw_string_wrap(tip, x1 + 14.0f, tipY, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 14.0f);

    const char* credit = "Based on FBI by Steveice10";
    float cw, ch;
    screen_get_string_size(&cw, &ch, credit, 0.45f, 0.45f);
    screen_draw_string(credit, x1 + (x2 - x1 - cw) / 2, y2 - ch - 6.0f, 0.45f, 0.45f, COLOR_HINT, true);
}

static void mainmenu_rebuild(linked_list* items) {
    linked_list_clear(items);

    linked_list_add(items, &mm_sd);
    linked_list_add(items, &mm_saves);
    linked_list_add(items, &mm_sysinfo);
    linked_list_add(items, &mm_titles);
    linked_list_add(items, &mm_pending);
    linked_list_add(items, &mm_tickets);
    linked_list_add(items, &mm_extsave);
    linked_list_add(items, &mm_remote);
    linked_list_add(items, &mm_batch);
    linked_list_add(items, &mm_ports);
    linked_list_add(items, &mm_update);
    linked_list_add(items, &mm_history);
    linked_list_add(items, &mm_settings);
    if(settings_show_advanced()) {
        linked_list_add(items, &mm_advanced);
    }
    linked_list_add(items, &mm_about);
}

static void advanced_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) view;
    (void) data;
    const char* tip = advanced_tooltip(selected);
    float tw, th;
    screen_get_string_size_wrap(&tw, &th, tip, 0.5f, 0.5f, x2 - x1 - 28.0f);
    float tipY = y1 + (y2 - y1 - th) / 2.0f;
    if(tipY < y1 + 8.0f) tipY = y1 + 8.0f;
    u32 color = COLOR_WARNING;
    if(selected == &adv_twl_photo || selected == &adv_twl_sound) {
        color = COLOR_TEXT;
    }
    screen_draw_string_wrap(tip, x1 + 14.0f, tipY, 0.5f, 0.5f, color, true, x2 - 14.0f);

    const char* credit = "Based on FBI by Steveice10";
    float cw, ch;
    screen_get_string_size(&cw, &ch, credit, 0.45f, 0.45f);
    screen_draw_string(credit, x1 + (x2 - x1 - cw) / 2, y2 - ch - 6.0f, 0.45f, 0.45f, COLOR_HINT, true);
}

static void advanced_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    (void) data;
    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected->data != NULL) {
            ((void(*)()) selected->data)();
            return;
        }
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &adv_ftp);
        linked_list_add(items, &adv_leftovers);
        linked_list_add(items, &adv_ctr_nand);
        linked_list_add(items, &adv_twl_nand);
        linked_list_add(items, &adv_twl_photo);
        linked_list_add(items, &adv_twl_sound);
        linked_list_add(items, &adv_dump_nand);
        linked_list_add(items, &adv_syssave);
    }
}

static void advanced_open(void) {
    list_display("Advanced", "A = choose, B = back", NULL, advanced_update, advanced_draw_top);
}

static void mainmenu_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    (void) data;

    if(hidKeysDown() & KEY_START) {
        ui_pop();
        list_destroy(view);
        return;
    }

    /* Keep Update label subtle when a newer build exists. */
    if(selfupdate_is_available()) {
        snprintf(mm_update.name, LIST_ITEM_NAME_MAX, "Update this app (%s)", selfupdate_display_tag());
        mm_update.color = COLOR_SUCCESS;
    } else {
        snprintf(mm_update.name, LIST_ITEM_NAME_MAX, "Update this app");
        mm_update.color = COLOR_TEXT;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected == &mm_about) {
            about_open();
            return;
        }
        if(selected == &mm_advanced) {
            advanced_open();
            return;
        }
        if(selected->data != NULL) {
            ((void(*)()) selected->data)();
            return;
        }
    }

    /* Rebuild when advanced visibility changes. */
    static bool lastAdv = false;
    bool adv = settings_show_advanced();
    if(linked_list_size(items) == 0 || adv != lastAdv) {
        mainmenu_rebuild(items);
        lastAdv = adv;
    }
}

void mainmenu_open(void) {
    resources_load();
    settings_init();
    selfupdate_init();
    selfupdate_start_background_check();

    list_display("New3DS FBI", "A = choose, START = exit", NULL, mainmenu_update, mainmenu_draw_top);
}
