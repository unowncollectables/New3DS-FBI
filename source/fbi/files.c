#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"
#include "pathprotect.h"

static list_item rename_opt = {"Rename", COLOR_TEXT, action_rename};
static list_item copy = {"Copy", COLOR_TEXT, NULL};
static list_item cut_move = {"Cut (move)", COLOR_TEXT, NULL};
static list_item paste = {"Paste here", COLOR_TEXT, action_paste_contents};
static list_item delete_marked = {"Delete marked items", COLOR_DANGER, NULL};

static list_item delete_file = {"Delete", COLOR_DANGER, action_delete_file};

static list_item install_cia = {"Install this CIA", COLOR_SUCCESS, action_install_cia};
static list_item install_and_delete_cia = {"Install CIA and delete file", COLOR_TEXT, action_install_cia_delete};

static list_item install_ticket = {"Install ticket", COLOR_TEXT, action_install_ticket};
static list_item install_and_delete_ticket = {"Install and delete ticket", COLOR_TEXT, action_install_ticket_delete};

static list_item delete_dir = {"Delete folder", COLOR_DANGER, action_delete_dir};
static list_item copy_all_contents = {"Copy everything inside", COLOR_TEXT, NULL};
static list_item delete_all_contents = {"Delete everything inside", COLOR_DANGER, action_delete_dir_contents};
static list_item new_folder = {"Make a new folder", COLOR_TEXT, action_new_folder};

static list_item install_all_cias = {"Install all CIAs", COLOR_TEXT, action_install_cias};
static list_item install_and_delete_all_cias = {"Install and delete all CIAs", COLOR_TEXT, action_install_cias_delete};
static list_item delete_all_cias = {"Delete all CIAs", COLOR_TEXT, action_delete_dir_cias};

static list_item install_all_tickets = {"Install all tickets", COLOR_TEXT, action_install_tickets};
static list_item install_and_delete_all_tickets = {"Install and delete all tickets", COLOR_TEXT, action_install_tickets_delete};
static list_item delete_all_tickets = {"Delete all tickets", COLOR_TEXT, action_delete_dir_tickets};

typedef struct {
    populate_files_data populateData;

    bool populated;

    FS_ArchiveID archiveId;
    FS_Path archivePath;
    FS_Archive archive;

    bool showHidden;
    bool showDirectories;
    bool showFiles;
    bool showCias;
    bool showTickets;

    char currDir[FILE_PATH_MAX];

    bool isSdmc;
} files_data;

typedef struct {
    linked_list* items;
    list_item* selected;
    files_data* parent;

    bool containsCias;
    bool containsTickets;
} files_action_data;

static bool files_filter(void* data, const char* name, u32 attributes) {
    files_data* listData = (files_data*) data;

    if((attributes & FS_ATTRIBUTE_HIDDEN) != 0 && !listData->showHidden) {
        return false;
    }

    if((attributes & FS_ATTRIBUTE_DIRECTORY) != 0) {
        return listData->showDirectories;
    } else {
        if((fs_filter_cias(NULL, name, attributes) && !listData->showCias) || (fs_filter_tickets(NULL, name, attributes) && !listData->showTickets)) {
            return false;
        }

        return listData->showFiles;
    }
}

static void files_action_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    task_draw_file_info(view, ((files_action_data*) data)->selected->data, x1, y1, x2, y2);
}

static void files_refresh_mark_visual(list_item* item) {
    file_info* info = (file_info*) item->data;
    if(info == NULL) return;
    if(strncmp(item->name, "<current directory>", LIST_ITEM_NAME_MAX) == 0
       || strncmp(item->name, "<current file>", LIST_ITEM_NAME_MAX) == 0) {
        return;
    }
    if(info->marked) {
        char buf[LIST_ITEM_NAME_MAX];
        if(strncmp(item->name, "[*] ", 4) != 0) {
            snprintf(buf, sizeof(buf), "[*] %s", info->name);
            string_copy(item->name, buf, LIST_ITEM_NAME_MAX);
        }
        item->color = COLOR_SUCCESS;
    } else {
        string_copy(item->name, info->name, LIST_ITEM_NAME_MAX);
        item->color = (info->attributes & FS_ATTRIBUTE_DIRECTORY) ? COLOR_DIRECTORY : COLOR_FILE;
    }
}

typedef struct {
    linked_list* items;
    files_data* parent;
    u32 count;
    u32 index;
    list_item** marked;
} files_marked_delete_ctx;

static void files_marked_delete_run(ui_view* view, void* data, float* progress, char* text);

static void files_marked_delete_onresponse(ui_view* view, void* data, u32 response) {
    (void) view;
    files_marked_delete_ctx* ctx = (files_marked_delete_ctx*) data;
    if(response != PROMPT_YES) {
        free(ctx->marked);
        free(ctx);
        return;
    }
    info_display("Deleting marked", "Please wait…", true, ctx, files_marked_delete_run, NULL);
}

static void files_marked_delete_run(ui_view* view, void* data, float* progress, char* text) {
    files_marked_delete_ctx* ctx = (files_marked_delete_ctx*) data;
    Result res = 0;

    while(ctx->index < ctx->count && R_SUCCEEDED(res)) {
        list_item* li = ctx->marked[ctx->index];
        file_info* fi = (file_info*) li->data;
        snprintf(text, PROGRESS_TEXT_MAX, "Deleting\n%s", fi->name);
        *progress = ctx->count ? (float) ctx->index / (float) ctx->count : 1.0f;

        if(path_is_critical_sd(fi->path)) {
            /* Skip critical paths in batch — user must delete those one-by-one with the danger prompt. */
            ctx->index++;
            continue;
        }

        FS_Path* fp = fs_make_path_utf8(fi->path);
        if(fp == NULL) {
            res = R_APP_OUT_OF_MEMORY;
            break;
        }
        if(fi->attributes & FS_ATTRIBUTE_DIRECTORY) {
            res = FSUSER_DeleteDirectoryRecursively(fi->archive, *fp);
        } else {
            res = FSUSER_DeleteFile(fi->archive, *fp);
        }
        fs_free_path_utf8(fp);

        if(R_SUCCEEDED(res)) {
            /* Remove from list view */
            linked_list_iter it;
            linked_list_iterate(ctx->items, &it);
            while(linked_list_iter_has_next(&it)) {
                list_item* cur = (list_item*) linked_list_iter_next(&it);
                if(cur == li) {
                    task_free_file(cur);
                    linked_list_iter_remove(&it);
                    break;
                }
            }
        }
        ctx->index++;
    }

    FSUSER_ControlArchive(ctx->parent->archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);

    ui_pop();
    info_destroy(view);

    if(R_SUCCEEDED(res)) {
        prompt_display_notify("Done!", "Marked items deleted.", COLOR_TEXT, NULL, NULL, NULL);
    } else {
        error_display_res(NULL, NULL, res, "Could not delete some marked items.");
    }
    free(ctx->marked);
    free(ctx);
}

static void files_start_marked_delete(linked_list* items, files_data* parent) {
    u32 count = 0;
    linked_list_iter it;
    linked_list_iterate(items, &it);
    while(linked_list_iter_has_next(&it)) {
        list_item* li = (list_item*) linked_list_iter_next(&it);
        file_info* fi = li->data;
        if(fi != NULL && fi->marked) count++;
    }
    if(count == 0) {
        prompt_display_notify("Nothing marked", "Press Y on a file to mark it\n(look for [*] ).", COLOR_TEXT, NULL, NULL, NULL);
        return;
    }

    files_marked_delete_ctx* ctx = calloc(1, sizeof(files_marked_delete_ctx));
    if(ctx == NULL) return;
    ctx->items = items;
    ctx->parent = parent;
    ctx->count = count;
    ctx->marked = calloc(count, sizeof(list_item*));
    if(ctx->marked == NULL) {
        free(ctx);
        return;
    }
    u32 i = 0;
    linked_list_iterate(items, &it);
    while(linked_list_iter_has_next(&it) && i < count) {
        list_item* li = (list_item*) linked_list_iter_next(&it);
        file_info* fi = li->data;
        if(fi != NULL && fi->marked) {
            ctx->marked[i++] = li;
        }
    }

    char msg[280];
    snprintf(msg, sizeof(msg),
             "Delete %lu marked item(s)?\n\n"
             "Important paths (luma, Nintendo 3DS,\n"
             "boot.firm) are skipped here — delete\n"
             "those one at a time.\n\n"
             "Yes = delete\nNo = cancel",
             (unsigned long) count);
    prompt_display_yes_no("Delete marked?", msg, COLOR_WARNING, ctx, NULL, files_marked_delete_onresponse);
}

static void files_action_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    files_action_data* actionData = (files_action_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        free(data);
        return;
    }

    if(selected != NULL && (selected->data != NULL || selected == &copy || selected == &copy_all_contents || selected == &cut_move || selected == &delete_marked) && (selectedTouched || (hidKeysDown() & KEY_A))) {
        void* action = selected->data;

        ui_pop();
        list_destroy(view);

        if(selected == &copy || selected == &copy_all_contents || selected == &cut_move) {
            file_info* info = (file_info*) actionData->selected->data;
            Result res = 0;
            if(R_SUCCEEDED(res = clipboard_set_contents(actionData->parent->archive, info->path, selected == &copy_all_contents))) {
                clipboard_set_move(selected == &cut_move);
                const char* msg;
                if(selected == &cut_move) {
                    msg = "Ready to move.\nOpen another folder and choose\nPaste here.";
                } else if(selected == &copy_all_contents) {
                    msg = "Folder contents copied.\nOpen another folder and Paste here.";
                } else if(info->attributes & FS_ATTRIBUTE_DIRECTORY) {
                    msg = "Folder copied.\nOpen another folder and Paste here.";
                } else {
                    msg = "File copied.\nOpen another folder and Paste here.";
                }
                prompt_display_notify(selected == &cut_move ? "Cut!" : "Copied!", msg, COLOR_TEXT, info, task_draw_file_info, NULL);
            } else {
                error_display_res(info, task_draw_file_info, res, "Could not copy to clipboard.");
            }
        } else if(selected == &delete_marked) {
            files_start_marked_delete(actionData->items, actionData->parent);
        } else if(selected == &install_all_cias || selected == &install_and_delete_all_cias || selected == &install_all_tickets || selected == &install_and_delete_all_tickets) {
            void (*filteredAction)(linked_list*, list_item*, bool (*)(void*, const char*, u32), void*) = action;
            filteredAction(actionData->items, actionData->selected, files_filter, actionData->parent);
        } else {
            void (*normalAction)(linked_list*, list_item*) = action;
            normalAction(actionData->items, actionData->selected);
        }

        free(data);
        return;
    }

    if(linked_list_size(items) == 0) {
        file_info* info = (file_info*) actionData->selected->data;
        bool anyMarked = false;
        linked_list_iter it;
        linked_list_iterate(actionData->items, &it);
        while(linked_list_iter_has_next(&it)) {
            file_info* fi = ((list_item*) linked_list_iter_next(&it))->data;
            if(fi != NULL && fi->marked) { anyMarked = true; break; }
        }

        if(anyMarked) {
            linked_list_add(items, &delete_marked);
        }

        if(info->attributes & FS_ATTRIBUTE_DIRECTORY) {
            if(actionData->containsCias) {
                linked_list_add(items, &install_all_cias);
                linked_list_add(items, &install_and_delete_all_cias);
                linked_list_add(items, &delete_all_cias);
            }
            if(actionData->containsTickets) {
                linked_list_add(items, &install_all_tickets);
                linked_list_add(items, &install_and_delete_all_tickets);
                linked_list_add(items, &delete_all_tickets);
            }
            linked_list_add(items, &copy_all_contents);
            linked_list_add(items, &delete_all_contents);
            linked_list_add(items, &new_folder);
            linked_list_add(items, &delete_dir);
        } else {
            if(info->isCia) {
                linked_list_add(items, &install_cia);
                linked_list_add(items, &install_and_delete_cia);
            }
            if(info->isTicket) {
                linked_list_add(items, &install_ticket);
                linked_list_add(items, &install_and_delete_ticket);
            }
            linked_list_add(items, &delete_file);
        }

        linked_list_add(items, &rename_opt);
        linked_list_add(items, &copy);
        linked_list_add(items, &cut_move);
        linked_list_add(items, &paste);
    }
}

static void files_action_open(linked_list* items, list_item* selected, files_data* parent) {
    files_action_data* data = (files_action_data*) calloc(1, sizeof(files_action_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate files action data.");

        return;
    }

    data->items = items;
    data->selected = selected;
    data->parent = parent;

    data->containsCias = false;
    data->containsTickets = false;

    linked_list_iter iter;
    linked_list_iterate(data->items, &iter);

    while(linked_list_iter_has_next(&iter)) {
        file_info* info = (file_info*) ((list_item*) linked_list_iter_next(&iter))->data;

        if(info->isCia) {
            data->containsCias = true;
        } else if(info->isTicket) {
            data->containsTickets = true;
        }
    }

    list_display((((file_info*) selected->data)->attributes & FS_ATTRIBUTE_DIRECTORY) ? "Folder options" : "File options", "A = choose, B = back", data, files_action_update, files_action_draw_top);
}

static void files_options_add_entry(linked_list* items, const char* name, bool* val) {
    list_item* item = (list_item*) calloc(1, sizeof(list_item));
    if(item != NULL) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s", name);
        item->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
        item->data = val;

        linked_list_add(items, item);
    }
}

static void files_options_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    files_data* listData = (files_data*) data;

    if(hidKeysDown() & KEY_B) {
        linked_list_iter iter;
        linked_list_iterate(items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }

        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        bool* val = (bool*) selected->data;
        *val = !(*val);

        selected->color = *val ? COLOR_ENABLED : COLOR_DISABLED;

        listData->populated = false;
    }

    if(linked_list_size(items) == 0) {
        files_options_add_entry(items, "Show hidden", &listData->showHidden);
        files_options_add_entry(items, "Show directories", &listData->showDirectories);
        files_options_add_entry(items, "Show files", &listData->showFiles);
        files_options_add_entry(items, "Show CIAs", &listData->showCias);
        files_options_add_entry(items, "Show tickets", &listData->showTickets);
    }
}

static void files_options_open(files_data* data) {
    list_display("View options", "A = toggle, B = back", data, files_options_update, NULL);
}

static void files_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    if(selected != NULL && selected->data != NULL) {
        task_draw_file_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void files_repopulate(files_data* listData, linked_list* items) {
    if(!listData->populateData.finished) {
        svcSignalEvent(listData->populateData.cancelEvent);
        while(!listData->populateData.finished) {
            svcSleepThread(1000000);
        }
    }

    listData->populateData.items = items;
    listData->populateData.archive = listData->archive;
    string_copy(listData->populateData.path, listData->currDir, FILE_PATH_MAX);

    Result res = task_populate_files(&listData->populateData);
    if(R_FAILED(res)) {
        error_display_res(NULL, NULL, res, "Failed to initiate file list population.");
    }

    listData->populated = true;
}

static void files_navigate(files_data* listData, linked_list* items, const char* path) {
    string_copy(listData->currDir, path, FILE_PATH_MAX);

    listData->populated = false;
}

static void files_free_data(files_data* data) {
    if(!data->populateData.finished) {
        svcSignalEvent(data->populateData.cancelEvent);
        while(!data->populateData.finished) {
            svcSleepThread(1000000);
        }
    }

    if(data->archive != 0) {
        fs_close_archive(data->archive);
        data->archive = 0;
    }

    if(data->archivePath.data != NULL) {
        free((void*) data->archivePath.data);
        data->archivePath.data = NULL;
    }

    free(data);
}

static void files_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    files_data* listData = (files_data*) data;

    if(listData->populated) {
        // Detect whether the current directory was renamed by an action.
        list_item* currDirItem = linked_list_get(items, 0);
        if(currDirItem != NULL && strncmp(listData->currDir, ((file_info*) currDirItem->data)->path, FILE_PATH_MAX) != 0) {
            string_copy(listData->currDir, ((file_info*) currDirItem->data)->path, FILE_PATH_MAX);
        }
    }

    while(!fs_is_dir(listData->archive, listData->currDir)) {
        char parentDir[FILE_PATH_MAX] = {'\0'};
        string_get_parent_path(parentDir, listData->currDir, FILE_PATH_MAX);

        files_navigate(listData, items, parentDir);
    }

    if(hidKeysDown() & KEY_B) {
        if(strncmp(listData->currDir, "/", FILE_PATH_MAX) == 0) {
            ui_pop();

            files_free_data(listData);

            task_clear_files(items);
            list_destroy(view);

            return;
        } else {
            char parentDir[FILE_PATH_MAX] = {'\0'};
            string_get_parent_path(parentDir, listData->currDir, FILE_PATH_MAX);

            files_navigate(listData, items, parentDir);
        }
    }

    if(hidKeysDown() & KEY_SELECT) {
        files_options_open(listData);
        return;
    }

    if(selected != NULL && selected->data != NULL && (hidKeysDown() & KEY_Y)) {
        file_info* fileInfo = (file_info*) selected->data;
        if(strncmp(selected->name, "<current directory>", LIST_ITEM_NAME_MAX) != 0
           && strncmp(selected->name, "<current file>", LIST_ITEM_NAME_MAX) != 0) {
            fileInfo->marked = !fileInfo->marked;
            files_refresh_mark_visual(selected);
        }
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        file_info* fileInfo = (file_info*) selected->data;
        bool isSpecial = (strncmp(selected->name, "<current directory>", LIST_ITEM_NAME_MAX) == 0)
                         || (strncmp(selected->name, "<current file>", LIST_ITEM_NAME_MAX) == 0);

        if((fileInfo->attributes & FS_ATTRIBUTE_DIRECTORY) && !isSpecial) {
            files_navigate(listData, items, fileInfo->path);
        } else {
            files_action_open(items, selected, listData);
            return;
        }
    }

    if(!listData->populated || (hidKeysDown() & KEY_X)) {
        files_repopulate(listData, items);
    }

    if(listData->populateData.finished && R_FAILED(listData->populateData.result)) {
        error_display_res(NULL, NULL, listData->populateData.result, "Failed to populate file list.");

        listData->populateData.result = 0;
    }
}

void files_open(FS_ArchiveID archiveId, FS_Path archivePath) {
    files_data* data = (files_data*) calloc(1, sizeof(files_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate files data.");

        return;
    }

    data->populateData.recursive = false;
    data->populateData.includeBase = true;
    data->populateData.meta = true;

    data->populateData.filter = files_filter;
    data->populateData.filterData = data;

    data->populateData.finished = true;

    data->populated = false;

    data->showHidden = false;
    data->showDirectories = true;
    data->showFiles = true;
    data->showCias = true;
    data->showTickets = true;

    data->archiveId = archiveId;
    data->archivePath.type = archivePath.type;
    data->archivePath.size = archivePath.size;
    if(archivePath.data != NULL) {
        data->archivePath.data = calloc(1, data->archivePath.size);
        if(data->archivePath.data == NULL) {
            error_display(NULL, NULL, "Failed to allocate files data.");

            files_free_data(data);
            return;
        }

        memcpy((void*) data->archivePath.data, archivePath.data, data->archivePath.size);
    } else {
        data->archivePath.data = NULL;
    }

    snprintf(data->currDir, FILE_PATH_MAX, "/");

    Result res = 0;
    if(R_FAILED(res = fs_open_archive(&data->archive, archiveId, archivePath))) {
        error_display_res(NULL, NULL, res, "Failed to open file listing archive.");

        files_free_data(data);
        return;
    }

    data->isSdmc = (archiveId == ARCHIVE_SDMC);
    list_display(data->isSdmc ? "My SD card files" : "Files",
                 data->isSdmc ? "A = open, Y = mark, B = back, X = refresh, SELECT = options" : "A: Select, B: Back, X: Refresh, Select: Options",
                 data, files_update, files_draw_top);
}

static void files_open_nand_warning_onresponse(ui_view* view, void* data, u32 response) {
    FS_ArchiveID archive = (FS_ArchiveID) data;

    if(response == PROMPT_YES) {
        files_open(archive, fsMakePath(PATH_EMPTY, ""));
    }
}

void files_open_nand_warning(FS_ArchiveID archive) {
    prompt_display_yes_no("Warning", "Changing NAND files is dangerous and\ncan stop the console from working.\nOnly continue if you know exactly\nwhat you are doing.\n\nContinue?", COLOR_WARNING, (void*) archive, NULL, files_open_nand_warning_onresponse);
}

void files_open_sd() {
    files_open(ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
}

void files_open_ctr_nand() {
    files_open_nand_warning(ARCHIVE_NAND_CTR_FS);
}

void files_open_twl_nand() {
    files_open_nand_warning(ARCHIVE_NAND_TWL_FS);
}

void files_open_twl_photo() {
    files_open(ARCHIVE_TWL_PHOTO, fsMakePath(PATH_EMPTY, ""));
}

void files_open_twl_sound() {
    files_open(ARCHIVE_TWL_SOUND, fsMakePath(PATH_EMPTY, ""));
}