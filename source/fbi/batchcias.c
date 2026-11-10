#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"
#include "extras.h"

#define BATCH_CIAS_PATH "/cias"

typedef struct {
    FS_Archive archive;

    linked_list contents;      /* all CIAs found in /cias */
    linked_list selected;      /* items to install (pointers into contents or copies) */

    bool deleteAfter;
    bool selecting;

    u32 succeeded;
    u32 failed;
    u32 skippedUnsafe;

    volatile bool n3dsContinue;

    char menuTitle[64]; /* list_display keeps the pointer, so it must outlive the view */

    data_op_data installInfo;
} batch_cias_data;

static void batch_cias_n3ds_onresponse(ui_view* view, void* data, u32 response) {
    ((batch_cias_data*) data)->n3dsContinue = response == PROMPT_YES;
}

static void batch_cias_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    batch_cias_data* batchData = (batch_cias_data*) data;

    u32 curr = batchData->installInfo.processed;
    if(curr < batchData->installInfo.total) {
        list_item* item = (list_item*) linked_list_get(&batchData->selected, curr);
        if(item != NULL) {
            task_draw_file_info(view, item->data, x1, y1, x2, y2);
        }
    }
}

static Result batch_cias_is_src_directory(void* data, u32 index, bool* isDirectory) {
    *isDirectory = false;
    return 0;
}

static Result batch_cias_make_dst_directory(void* data, u32 index) {
    return 0;
}

static Result batch_cias_open_src(void* data, u32 index, u32* handle) {
    batch_cias_data* batchData = (batch_cias_data*) data;
    file_info* info = (file_info*) ((list_item*) linked_list_get(&batchData->selected, index))->data;

    Result res = 0;
    FS_Path* fsPath = fs_make_path_utf8(info->path);
    if(fsPath != NULL) {
        res = FSUSER_OpenFile(handle, info->archive, *fsPath, FS_OPEN_READ, 0);
        fs_free_path_utf8(fsPath);
    } else {
        res = R_APP_OUT_OF_MEMORY;
    }

    return res;
}

static Result batch_cias_close_src(void* data, u32 index, bool succeeded, u32 handle) {
    batch_cias_data* batchData = (batch_cias_data*) data;
    file_info* info = (file_info*) ((list_item*) linked_list_get(&batchData->selected, index))->data;

    Result res = FSFILE_Close(handle);
    if(R_SUCCEEDED(res) && batchData->deleteAfter && succeeded) {
        FS_Path* fsPath = fs_make_path_utf8(info->path);
        if(fsPath != NULL) {
            FSUSER_DeleteFile(info->archive, *fsPath);
            fs_free_path_utf8(fsPath);
        } else {
            res = R_APP_OUT_OF_MEMORY;
        }
    }

    return res;
}

static Result batch_cias_get_src_size(void* data, u32 handle, u64* size) {
    return FSFILE_GetSize(handle, size);
}

static Result batch_cias_read_src(void* data, u32 handle, u32* bytesRead, void* buffer, u64 offset, u32 size) {
    return FSFILE_Read(handle, bytesRead, offset, buffer, size);
}

static Result batch_cias_open_dst(void* data, u32 index, void* initialReadBlock, u64 size, u32* handle) {
    batch_cias_data* batchData = (batch_cias_data*) data;
    file_info* info = (file_info*) ((list_item*) linked_list_get(&batchData->selected, index))->data;

    batchData->n3dsContinue = false;

    /* Prefer metadata title ID when loaded; always re-validate from CIA header bytes. */
    u64 titleId = 0;
    Result res = 0;

    if(*(u16*) initialReadBlock != 0x2020) {
        return R_APP_BAD_DATA;
    }

    if(R_FAILED(res = cia_get_title_id(&titleId, (u8*) initialReadBlock, batchData->installInfo.bufferSize))) {
        return R_APP_BAD_DATA;
    }

    if(!fs_is_remote_install_allowed(titleId)) {
        return R_APP_SAFETY_REFUSED;
    }

    /* Keep meta in sync for UI. */
    info->ciaInfo.titleId = titleId;
    info->ciaInfo.loaded = true;

    bool n3ds = false;
    if(R_SUCCEEDED(APT_CheckNew3DS(&n3ds)) && !n3ds && ((titleId >> 28) & 0xF) == 2) {
        ui_view* view = prompt_display_yes_no("New 3DS only", "This is made for New 3DS systems.\nInstall anyway?", COLOR_TEXT, data, batch_cias_draw_top, batch_cias_n3ds_onresponse);
        if(view != NULL) {
            svcWaitSynchronization(view->active, U64_MAX);
        }

        if(!batchData->n3dsContinue) {
            return R_APP_SKIPPED;
        }
    }

    return AM_StartCiaInstallOverwrite(handle, MEDIATYPE_SD);
}

static Result batch_cias_close_dst(void* data, u32 index, bool succeeded, u32 handle) {
    if(succeeded) {
        Result res = AM_FinishCiaInstall(handle);
        if(R_SUCCEEDED(res)) {
            batch_cias_data* batchData = (batch_cias_data*) data;
            file_info* info = (file_info*) ((list_item*) linked_list_get(&batchData->selected, index))->data;
            batchData->succeeded++;
            extras_history_append(info->name, "OK");
            if(info->ciaInfo.loaded) {
                http_download_seed(info->ciaInfo.titleId);
            }
            /* Never AM_InstallFirm. */
        }

        return res;
    }

    return AM_CancelCIAInstall(handle);
}

static Result batch_cias_write_dst(void* data, u32 handle, u32* bytesWritten, void* buffer, u64 offset, u32 size) {
    return FSFILE_Write(handle, bytesWritten, offset, buffer, size, 0);
}

static Result batch_cias_suspend(void* data, u32 index) {
    return 0;
}

static Result batch_cias_restore(void* data, u32 index) {
    return 0;
}

static bool batch_cias_error(void* data, u32 index, Result res, ui_view** errorView) {
    batch_cias_data* batchData = (batch_cias_data*) data;

    list_item* item = (list_item*) linked_list_get(&batchData->selected, index);
    file_info* info = item != NULL ? (file_info*) item->data : NULL;
    const char* name = info != NULL ? info->name : "?";

    if(res == R_APP_SAFETY_REFUSED) {
        batchData->skippedUnsafe++;
        extras_history_append(name, "Blocked (system file)");
        *errorView = error_display_res(data, batch_cias_draw_top, res,
                                       "Skipped to keep your console safe:\n%.40s\n\nOnly games, updates and DLC\nfor the SD card are installed.", name);
    } else {
        batchData->failed++;
        extras_history_append(name, "Failed");
        *errorView = error_display_res(data, batch_cias_draw_top, res,
                                       "Could not install (moving on\nto the next one):\n%.40s", name);
    }

    /* continueOnError: return true to proceed to next file. */
    return true;
}

static void batch_cias_free_data(batch_cias_data* data) {
    if(data == NULL) {
        return;
    }

    task_clear_files(&data->selected);
    linked_list_destroy(&data->selected);

    task_clear_files(&data->contents);
    linked_list_destroy(&data->contents);

    if(data->archive != 0) {
        fs_close_archive(data->archive);
        data->archive = 0;
    }

    free(data);
}

static void batch_cias_show_summary(batch_cias_data* data) {
    u32 total = data->installInfo.total;
    u32 done = data->succeeded + data->failed + data->skippedUnsafe;
    u32 notStarted = total > done ? total - done : 0;

    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "Installed: %lu\nFailed: %lu\nSkipped (system files): %lu",
                     (unsigned long) data->succeeded, (unsigned long) data->failed, (unsigned long) data->skippedUnsafe);
    if(notStarted > 0 && n > 0 && (size_t) n < sizeof(buf)) {
        snprintf(buf + n, sizeof(buf) - n, "\nNot started (cancelled): %lu", (unsigned long) notStarted);
    }

    prompt_display_notify(data->failed == 0 && notStarted == 0 ? "All done!" : "Finished",
                          buf, data->failed == 0 && notStarted == 0 ? COLOR_SUCCESS : COLOR_WARNING, NULL, NULL, NULL);
}

static void batch_cias_install_update(ui_view* view, void* data, float* progress, char* text) {
    batch_cias_data* batchData = (batch_cias_data*) data;

    if(batchData->installInfo.finished) {
        if(batchData->deleteAfter) {
            FSUSER_ControlArchive(batchData->archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
        }

        ui_pop();
        info_destroy(view);

        extras_install_end();

        /* Counts are tallied per CIA as each one finishes or fails. */
        batch_cias_show_summary(batchData);
        batch_cias_free_data(batchData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !batchData->installInfo.finished) {
        svcSignalEvent(batchData->installInfo.cancelEvent);
    }

    *progress = batchData->installInfo.currTotal != 0
        ? (float) ((double) batchData->installInfo.currProcessed / (double) batchData->installInfo.currTotal)
        : 0;

    const char* name = "";
    if(batchData->installInfo.processed < batchData->installInfo.total) {
        file_info* info = (file_info*) ((list_item*) linked_list_get(&batchData->selected, batchData->installInfo.processed))->data;
        if(info != NULL) {
            name = info->name;
        }
    }

    snprintf(text, PROGRESS_TEXT_MAX,
             "%lu / %lu  %s\n%.2f %s / %.2f %s\n%.2f %s/s, ETA %s",
             (unsigned long) (batchData->installInfo.processed + (batchData->installInfo.finished ? 0 : 1)),
             (unsigned long) batchData->installInfo.total,
             name,
             ui_get_display_size(batchData->installInfo.currProcessed),
             ui_get_display_size_units(batchData->installInfo.currProcessed),
             ui_get_display_size(batchData->installInfo.currTotal),
             ui_get_display_size_units(batchData->installInfo.currTotal),
             ui_get_display_size(batchData->installInfo.bytesPerSecond),
             ui_get_display_size_units(batchData->installInfo.bytesPerSecond),
             ui_get_display_eta(batchData->installInfo.estimatedRemainingSeconds));
}

static void batch_cias_start_install(batch_cias_data* data);

/* Proper confirm callback after delete question */
static void batch_cias_confirm_install_onresponse(ui_view* view, void* data, u32 response) {
    batch_cias_data* batchData = (batch_cias_data*) data;

    if(response == PROMPT_YES) {
        batch_cias_start_install(batchData);
    } else {
        batch_cias_free_data(batchData);
    }
}

static void batch_cias_ask_delete_onresponse(ui_view* view, void* data, u32 response) {
    batch_cias_data* batchData = (batch_cias_data*) data;
    batchData->deleteAfter = response == PROMPT_YES;

    char msg[192];
    snprintf(msg, sizeof(msg), "Install %lu game file(s) from the\n/cias folder?\n\nDelete each file after it installs: %s\n\nYes = install\nNo = cancel",
             (unsigned long) linked_list_size(&batchData->selected),
             batchData->deleteAfter ? "Yes" : "No");

    prompt_display_yes_no("Install games?", msg, COLOR_TEXT, batchData, batch_cias_draw_top, batch_cias_confirm_install_onresponse);
}

static void batch_cias_prepare_selected_and_ask(batch_cias_data* data) {
    /* Drop unsafe titles from the install queue up front when meta is known. */
    linked_list safe;
    linked_list_init(&safe);

    linked_list_iter iter;
    linked_list_iterate(&data->selected, &iter);
    while(linked_list_iter_has_next(&iter)) {
        list_item* item = (list_item*) linked_list_iter_next(&iter);
        file_info* info = (file_info*) item->data;

        if(info->isCia && info->ciaInfo.loaded && !fs_is_remote_install_allowed(info->ciaInfo.titleId)) {
            data->skippedUnsafe++;
            linked_list_iter_remove(&iter);
            task_free_file(item);
            continue;
        }

        linked_list_add(&safe, item);
        linked_list_iter_remove(&iter);
    }

    linked_list_destroy(&data->selected);
    data->selected = safe;

    if(linked_list_size(&data->selected) == 0) {
        char buf[160];
        snprintf(buf, sizeof(buf), "Nothing safe to install.\n\nSkipped (system files): %lu\nOnly games, updates and DLC\nfor the SD card are installed.", (unsigned long) data->skippedUnsafe);
        prompt_display_safety_refusal("Nothing to install", buf, NULL, NULL, NULL);
        batch_cias_free_data(data);
        return;
    }

    /* Delete-after defaults OFF: ask explicitly; Yes enables deletion. */
    prompt_display_yes_no("Delete after install?",
                          "Delete each file from /cias after it\ninstalls OK? (Frees SD space.)\n\nYes = delete after install\nNo = keep the files (safer)",
                          COLOR_TEXT, data, NULL, batch_cias_ask_delete_onresponse);
}

static void batch_cias_start_install(batch_cias_data* data) {
    data->succeeded = 0;
    data->failed = 0;

    /* Same safety gates as single installs: enough SD space and battery. */
    u64 need = 0;
    linked_list_iter iter;
    linked_list_iterate(&data->selected, &iter);
    while(linked_list_iter_has_next(&iter)) {
        list_item* it = (list_item*) linked_list_iter_next(&iter);
        file_info* fi = it != NULL ? (file_info*) it->data : NULL;
        if(fi != NULL) {
            need += fi->size;
        }
    }
    u64 have = 0;
    if(!extras_sd_has_space(need, &have)) {
        char msg[256];
        char needS[32], haveS[32];
        extras_format_bytes(needS, sizeof(needS), need);
        extras_format_bytes(haveS, sizeof(haveS), have);
        snprintf(msg, sizeof(msg),
                 "These need about %s.\nYou only have %s free\non the SD card.\n\nPick fewer games or free some space.",
                 needS, haveS);
        prompt_display_notify("Not enough space", msg, COLOR_WARNING, NULL, NULL, NULL);
        batch_cias_free_data(data);
        return;
    }
    if(!extras_battery_ok_for_long_job()) {
        prompt_display_notify("Battery low",
                              "Battery looks low and the console\nis not charging.\n\nPlug in, then try again.",
                              COLOR_WARNING, NULL, NULL, NULL);
        batch_cias_free_data(data);
        return;
    }

    data->installInfo.data = data;
    data->installInfo.op = DATAOP_COPY;
    data->installInfo.bufferSize = 2 * 1024 * 1024;
    data->installInfo.copyEmpty = false;
    data->installInfo.continueOnError = true;

    data->installInfo.isSrcDirectory = batch_cias_is_src_directory;
    data->installInfo.makeDstDirectory = batch_cias_make_dst_directory;
    data->installInfo.openSrc = batch_cias_open_src;
    data->installInfo.closeSrc = batch_cias_close_src;
    data->installInfo.getSrcSize = batch_cias_get_src_size;
    data->installInfo.readSrc = batch_cias_read_src;
    data->installInfo.openDst = batch_cias_open_dst;
    data->installInfo.closeDst = batch_cias_close_dst;
    data->installInfo.writeDst = batch_cias_write_dst;
    data->installInfo.suspend = batch_cias_suspend;
    data->installInfo.restore = batch_cias_restore;
    data->installInfo.error = batch_cias_error;
    data->installInfo.finished = true;

    data->installInfo.total = linked_list_size(&data->selected);
    data->installInfo.processed = data->installInfo.total;

    extras_install_begin();
    Result res = task_data_op(&data->installInfo);
    if(R_SUCCEEDED(res)) {
        info_display("Installing games…", "B = cancel", true, data, batch_cias_install_update, batch_cias_draw_top);
    } else {
        extras_install_end();
        error_display_res(NULL, NULL, res, "Could not start installing.");
        batch_cias_free_data(data);
    }
}

/* ---- selection UI ---- */

/* Show "[x] " / "[ ] " in front of each file name so ticks are obvious. */
static void batch_cias_mark(list_item* item, bool on) {
    file_info* info = (file_info*) item->data;
    item->color = on ? COLOR_ENABLED : COLOR_TEXT;
    snprintf(item->name, LIST_ITEM_NAME_MAX, "%s %s", on ? "[x]" : "[  ]", info != NULL ? info->name : "?");
}

static bool batch_cias_is_marked(list_item* item) {
    return item->color == COLOR_ENABLED;
}

static void batch_cias_select_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    if(selected != NULL && selected->data != NULL) {
        task_draw_file_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void batch_cias_select_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    batch_cias_data* batchData = (batch_cias_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        batch_cias_free_data(batchData);
        return;
    }

    if(hidKeysDown() & KEY_X) {
        /* Install currently highlighted toggles: gather COLOR_ENABLED items */
        linked_list_init(&batchData->selected);

        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            if(batch_cias_is_marked(item)) {
                /* Move ownership of selected file items into selected list */
                linked_list_add(&batchData->selected, item);
            }
        }

        /* Remove selected from contents so free won't double-free; leave unselected in contents */
        linked_list_iterate(&batchData->contents, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            if(batch_cias_is_marked(item)) {
                linked_list_iter_remove(&iter);
            }
        }

        ui_pop();
        list_destroy(view);

        if(linked_list_size(&batchData->selected) == 0) {
            prompt_display_notify("Nothing picked", "No games were ticked.\nPress A on a game to tick it,\nthen press X to install.", COLOR_TEXT, NULL, NULL, NULL);
            batch_cias_free_data(batchData);
            return;
        }

        batch_cias_prepare_selected_and_ask(batchData);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        batch_cias_mark(selected, !batch_cias_is_marked(selected));
    }

    if(hidKeysDown() & KEY_Y) {
        /* Y = tick all / untick all */
        bool anyOff = false;
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            if(!batch_cias_is_marked((list_item*) linked_list_iter_next(&iter))) anyOff = true;
        }
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            batch_cias_mark((list_item*) linked_list_iter_next(&iter), anyOff);
        }
    }

    if(linked_list_size(items) == 0) {
        linked_list_iter iter;
        linked_list_iterate(&batchData->contents, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            batch_cias_mark(item, false);
            linked_list_add(items, item);
        }
    }
}

static void batch_cias_select_open(batch_cias_data* data) {
    list_display("Pick games to install", "A = tick, Y = all, X = install, B = back", data, batch_cias_select_update, batch_cias_select_draw_top);
}

/* ---- action menu after scan ---- */

static void batch_cias_install_all(batch_cias_data* data) {
    linked_list_init(&data->selected);

    linked_list_iter iter;
    linked_list_iterate(&data->contents, &iter);
    while(linked_list_iter_has_next(&iter)) {
        list_item* item = (list_item*) linked_list_iter_next(&iter);
        linked_list_add(&data->selected, item);
        linked_list_iter_remove(&iter);
    }

    batch_cias_prepare_selected_and_ask(data);
}

typedef struct {
    batch_cias_data* batchData;
} batch_cias_menu_data;

static list_item batch_action_all = {"Install all of them", COLOR_TEXT, NULL};
static list_item batch_action_pick = {"Pick which ones to install", COLOR_TEXT, NULL};

static void batch_cias_menu_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    batch_cias_menu_data* menuData = (batch_cias_menu_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        batch_cias_free_data(menuData->batchData);
        free(menuData);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        batch_cias_data* batchData = menuData->batchData;
        bool installAll = selected == &batch_action_all;

        ui_pop();
        list_destroy(view);
        free(menuData);

        if(installAll) {
            batch_cias_install_all(batchData);
        } else {
            batch_cias_select_open(batchData);
        }
        return;
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &batch_action_all);
        linked_list_add(items, &batch_action_pick);
    }
}

static void batch_cias_menu_open(batch_cias_data* data) {
    batch_cias_menu_data* menuData = (batch_cias_menu_data*) calloc(1, sizeof(batch_cias_menu_data));
    if(menuData == NULL) {
        error_display(NULL, NULL, "Failed to allocate batch menu data.");
        batch_cias_free_data(data);
        return;
    }

    menuData->batchData = data;

    snprintf(data->menuTitle, sizeof(data->menuTitle), "Games in /cias (%lu found)", (unsigned long) linked_list_size(&data->contents));
    list_display(data->menuTitle, "A = choose, B = back", menuData, batch_cias_menu_update, NULL);
}

/* ---- loading /cias ---- */

typedef struct {
    batch_cias_data* batchData;
    populate_files_data popData;
    bool cancelled;
} batch_cias_loading_data;

static void batch_cias_loading_update(ui_view* view, void* data, float* progress, char* text) {
    batch_cias_loading_data* loadingData = (batch_cias_loading_data*) data;

    if(loadingData->popData.finished) {
        ui_pop();
        info_destroy(view);

        if(loadingData->cancelled) {
            /* B pressed while scanning: just go back. */
            batch_cias_free_data(loadingData->batchData);
        } else if(R_FAILED(loadingData->popData.result)) {
            error_display_res(NULL, NULL, loadingData->popData.result, "Could not read the /cias folder.");
            batch_cias_free_data(loadingData->batchData);
        } else if(linked_list_size(&loadingData->batchData->contents) == 0) {
            prompt_display_notify("No games found", "There are no .cia files in the\n/cias folder on your SD card.\n\nCopy your .cia files there,\nthen try again.", COLOR_TEXT, NULL, NULL, NULL);
            batch_cias_free_data(loadingData->batchData);
        } else {
            batch_cias_menu_open(loadingData->batchData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        loadingData->cancelled = true;
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "Looking in the /cias folder…");
}

static Result batch_cias_ensure_dir(FS_Archive archive) {
    return fs_ensure_dir(archive, BATCH_CIAS_PATH "/");
}

void batchcias_open() {
    batch_cias_data* data = (batch_cias_data*) calloc(1, sizeof(batch_cias_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate batch CIAs data.");
        return;
    }

    linked_list_init(&data->contents);
    linked_list_init(&data->selected);
    data->deleteAfter = false;

    Result res = 0;
    if(R_FAILED(res = fs_open_archive(&data->archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        error_display_res(NULL, NULL, res, "Could not open the SD card.");
        batch_cias_free_data(data);
        return;
    }

    if(R_FAILED(res = batch_cias_ensure_dir(data->archive))) {
        error_display_res(NULL, NULL, res, "Could not create the /cias folder.");
        batch_cias_free_data(data);
        return;
    }

    batch_cias_loading_data* loadingData = (batch_cias_loading_data*) calloc(1, sizeof(batch_cias_loading_data));
    if(loadingData == NULL) {
        error_display(NULL, NULL, "Failed to allocate loading data.");
        batch_cias_free_data(data);
        return;
    }

    loadingData->batchData = data;
    loadingData->popData.items = &data->contents;
    loadingData->popData.archive = data->archive;
    string_copy(loadingData->popData.path, BATCH_CIAS_PATH "/", FILE_PATH_MAX);
    loadingData->popData.recursive = false;
    loadingData->popData.includeBase = false;
    loadingData->popData.meta = true;
    loadingData->popData.filter = fs_filter_cias;
    loadingData->popData.filterData = NULL;

    Result listRes = task_populate_files(&loadingData->popData);
    if(R_FAILED(listRes)) {
        error_display_res(NULL, NULL, listRes, "Could not look in the /cias folder.");
        free(loadingData);
        batch_cias_free_data(data);
        return;
    }

    info_display("Install games from SD", "B = cancel", false, loadingData, batch_cias_loading_update, NULL);
}
