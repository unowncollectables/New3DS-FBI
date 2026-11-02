#include <malloc.h>
#include <stdio.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "../core/core.h"

static list_item sd = {"SD", COLOR_TEXT, files_open_sd};
static list_item ctr_nand = {"CTR NAND", COLOR_TEXT, files_open_ctr_nand};
static list_item twl_nand = {"TWL NAND", COLOR_TEXT, files_open_twl_nand};
static list_item twl_photo = {"TWL Photo", COLOR_TEXT, files_open_twl_photo};
static list_item twl_sound = {"TWL Sound", COLOR_TEXT, files_open_twl_sound};
static list_item dump_nand = {"Dump NAND", COLOR_TEXT, dumpnand_open};
static list_item titles = {"Titles", COLOR_TEXT, titles_open};
static list_item pending_titles = {"Pending Titles", COLOR_TEXT, pendingtitles_open};
static list_item tickets = {"Tickets", COLOR_TEXT, tickets_open};
static list_item ext_save_data = {"Ext Save Data", COLOR_TEXT, extsavedata_open};
static list_item system_save_data = {"System Save Data", COLOR_TEXT, systemsavedata_open};
static list_item remote_install = {"Remote Install", COLOR_TEXT, remoteinstall_open};
static list_item batch_cias = {"Batch CIAs (/cias)", COLOR_TEXT, batchcias_open};
static list_item port_updater = {"Update my ports", COLOR_TEXT, portupdater_open};
static list_item update = {"Update", COLOR_TEXT, update_open};
static list_item about = {"About / Credits", COLOR_TEXT, NULL};

static void mainmenu_about(void) {
    prompt_display_notify(
        "About New3DS FBI",
        "New3DS FBI is built on FBI\n"
        "by Steveice10.\n\n"
        "Upstream:\n"
        "github.com/Steveice10/FBI\n\n"
        "This fork: New 2DS/3DS only,\n"
        "local port updater, safety\n"
        "guards. By unowncollectables.\n\n"
        "LICENSE / copyright for FBI\n"
        "remain Steveice10's — see\n"
        "LICENSE.txt in the repo.",
        COLOR_TEXT, NULL, NULL, NULL);
}

static void mainmenu_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    u32 logoWidth;
    u32 logoHeight;
    screen_get_texture_size(&logoWidth, &logoHeight, TEXTURE_LOGO);

    float logoX = x1 + (x2 - x1 - logoWidth) / 2;
    float logoY = y1 + (y2 - y1 - logoHeight) / 2;
    screen_draw_texture(TEXTURE_LOGO, logoX, logoY, logoWidth, logoHeight);

    const char* credit = "Based on FBI by Steveice10";
    float cw, ch;
    screen_get_string_size(&cw, &ch, credit, 0.45f, 0.45f);
    screen_draw_string(credit, x1 + (x2 - x1 - cw) / 2, y2 - ch - 6.0f, 0.45f, 0.45f, COLOR_HINT, true);
}

static void mainmenu_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    if(hidKeysDown() & KEY_START) {
        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && (selectedTouched || hidKeysDown() & KEY_A)) {
        if(selected == &about) {
            mainmenu_about();
            return;
        }

        if(selected->data != NULL) {
            ((void(*)()) selected->data)();
            return;
        }
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &sd);
        linked_list_add(items, &ctr_nand);
        linked_list_add(items, &twl_nand);
        linked_list_add(items, &twl_photo);
        linked_list_add(items, &twl_sound);
        linked_list_add(items, &dump_nand);
        linked_list_add(items, &titles);
        linked_list_add(items, &pending_titles);
        linked_list_add(items, &tickets);
        linked_list_add(items, &ext_save_data);
        linked_list_add(items, &system_save_data);
        linked_list_add(items, &remote_install);
        linked_list_add(items, &batch_cias);
        linked_list_add(items, &port_updater);
        linked_list_add(items, &update);
        linked_list_add(items, &about);
    }
}

void mainmenu_open() {
    resources_load();

    list_display("New3DS FBI", "A: Select, START: Exit", NULL, mainmenu_update, mainmenu_draw_top);
}
