#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"

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
        return R_APP_BAD_DATA;
    }

    /* Keep meta in sync for UI. */
    info->ciaInfo.titleId = titleId;
    info->ciaInfo.loaded = true;

    bool n3ds = false;
    if(R_SUCCEEDED(APT_CheckNew3DS(&n3ds)) && !n3ds && ((titleId >> 28) & 0xF) == 2) {
        ui_view* view = prompt_display_yes_no("Confirmation", "Title is intended for New 3DS systems.\nContinue?", COLOR_TEXT, data, batch_cias_draw_top, batch_cias_n3ds_onresponse);
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
    batchData->failed++;

    file_info* info = (file_info*) ((list_item*) linked_list_get(&batchData->selected, index))->data;
    const char* name = info != NULL ? info->name : "?";

    if(res == R_APP_BAD_DATA) {
        *errorView = error_display_res(data, batch_cias_draw_top, res,
                                       "Skipped unsafe/invalid CIA:\n%.40s", name);
    } else {
        *errorView = error_display_res(data, batch_cias_draw_top, res,
                                       "Failed (continuing batch):\n%.40s", name);
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
    char buf[256];
    snprintf(buf, sizeof(buf),
             "Succeeded: %lu\nFailed: %lu\nSkipped (unsafe): %lu\nTotal attempted: %lu",
             data->succeeded, data->failed, data->skippedUnsafe, data->installInfo.total);

    prompt_display_notify("Batch Summary", buf, COLOR_TEXT, NULL, NULL, NULL);
}

static void batch_cias_install_update(ui_view* view, void* data, float* progress, char* text) {
    batch_cias_data* batchData = (batch_cias_data*) data;

    if(batchData->installInfo.finished) {
        if(batchData->deleteAfter) {
            FSUSER_ControlArchive(batchData->archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
        }

        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(batchData->installInfo.result) || batchData->installInfo.continueOnError) {
            /* Count successes as total processed minus failed (skipped unsafe already removed). */
            u32 attempted = batchData->installInfo.total;
            if(attempted >= batchData->failed) {
                batchData->succeeded = attempted - batchData->failed;
            }
        }

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
    snprintf(msg, sizeof(msg), "Install %u CIA(s) from sdmc:%s?\nDelete after each success: %s",
             linked_list_size(&batchData->selected),
             BATCH_CIAS_PATH,
             batchData->deleteAfter ? "YES" : "no");

    prompt_display_yes_no("Confirmation", msg, COLOR_TEXT, batchData, batch_cias_draw_top, batch_cias_confirm_install_onresponse);
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
        snprintf(buf, sizeof(buf), "Nothing safe to install.\nSkipped unsafe: %lu", (unsigned long) data->skippedUnsafe);
        prompt_display_notify("Batch CIAs", buf, COLOR_TEXT, NULL, NULL, NULL);
        batch_cias_free_data(data);
        return;
    }

    /* Delete-after defaults OFF: ask explicitly; Yes enables deletion. */
    prompt_display_yes_no("Delete after install?",
                          "Delete each CIA from /cias after it\ninstalls successfully?\n\nRecommended: No",
                          COLOR_TEXT, data, NULL, batch_cias_ask_delete_onresponse);
}

static void batch_cias_start_install(batch_cias_data* data) {
    data->succeeded = 0;
    data->failed = 0;

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

    Result res = task_data_op(&data->installInfo);
    if(R_SUCCEEDED(res)) {
        info_display("Batch Installing CIAs", "Press B to cancel.", true, data, batch_cias_install_update, batch_cias_draw_top);
    } else {
        error_display_res(NULL, NULL, res, "Failed to start batch install.");
        batch_cias_free_data(data);
    }
}

/* ---- selection UI ---- */

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
            if(item->color == COLOR_ENABLED) {
                /* Move ownership of selected file items into selected list */
                linked_list_add(&batchData->selected, item);
            }
        }

        /* Remove selected from contents so free won't double-free; leave unselected in contents */
        linked_list_iterate(&batchData->contents, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            if(item->color == COLOR_ENABLED) {
                linked_list_iter_remove(&iter);
            }
        }

        ui_pop();
        list_destroy(view);

        if(linked_list_size(&batchData->selected) == 0) {
            prompt_display_notify("Batch CIAs", "No CIAs selected.", COLOR_TEXT, NULL, NULL, NULL);
            batch_cias_free_data(batchData);
            return;
        }

        batch_cias_prepare_selected_and_ask(batchData);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        selected->color = selected->color == COLOR_ENABLED ? COLOR_DISABLED : COLOR_ENABLED;
    }

    if(linked_list_size(items) == 0) {
        linked_list_iter iter;
        linked_list_iterate(&batchData->contents, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            item->color = COLOR_DISABLED;
            linked_list_add(items, item);
        }
    }
}

static void batch_cias_select_open(batch_cias_data* data) {
    list_display("Select CIAs (A: Toggle, X: Install)", "B: Cancel", data, batch_cias_select_update, NULL);
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

static list_item batch_action_all = {"Install all", COLOR_TEXT, NULL};
static list_item batch_action_pick = {"Select which to install", COLOR_TEXT, NULL};

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

    char title[64];
    snprintf(title, sizeof(title), "Batch CIAs (%u found)", linked_list_size(&data->contents));
    list_display(title, "A: Select, B: Cancel", menuData, batch_cias_menu_update, NULL);
}

/* ---- loading /cias ---- */

typedef struct {
    batch_cias_data* batchData;
    populate_files_data popData;
} batch_cias_loading_data;

static void batch_cias_loading_update(ui_view* view, void* data, float* progress, char* text) {
    batch_cias_loading_data* loadingData = (batch_cias_loading_data*) data;

    if(loadingData->popData.finished) {
        ui_pop();
        info_destroy(view);

        if(R_FAILED(loadingData->popData.result)) {
            error_display_res(NULL, NULL, loadingData->popData.result, "Failed to list sdmc:/cias.");
            batch_cias_free_data(loadingData->batchData);
        } else if(linked_list_size(&loadingData->batchData->contents) == 0) {
            prompt_display_notify("Batch CIAs", "No .cia files in sdmc:/cias.\nCreate the folder and copy CIAs there.", COLOR_TEXT, NULL, NULL, NULL);
            batch_cias_free_data(loadingData->batchData);
        } else {
            batch_cias_menu_open(loadingData->batchData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "Scanning sdmc:%s ...", BATCH_CIAS_PATH);
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
        error_display_res(NULL, NULL, res, "Failed to open SDMC.");
        batch_cias_free_data(data);
        return;
    }

    if(R_FAILED(res = batch_cias_ensure_dir(data->archive))) {
        error_display_res(NULL, NULL, res, "Failed to create sdmc:/cias.");
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
        error_display_res(NULL, NULL, listRes, "Failed to start CIA scan.");
        free(loadingData);
        batch_cias_free_data(data);
        return;
    }

    info_display("Batch CIAs", "Press B to cancel.", false, loadingData, batch_cias_loading_update, NULL);
}
