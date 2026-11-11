#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "task/uitask.h"
#include "../core/core.h"
#include "extras.h"

/* Local-only port CIAs. Never download / never contact the network here.
   Primary: sdmc:/cias/  (Pokemon 3DS Builder deploy path: soulsilver3ds.cia / heartgold3ds.cia / platinum3ds.cia)
   Secondary: sdmc:/fbi/ports/ (optional manual drop folder) */
#define PORTS_PATH_PRIMARY   "/cias"
#define PORTS_PATH_SECONDARY "/fbi/ports"

#define PORT_TITLE_SOULSILVER 0x000400000F7D4000ULL
#define PORT_TITLE_PLATINUM   0x000400000F7D3D00ULL
#define PORT_TITLE_HEARTGOLD  0x000400000F7D4100ULL

typedef struct {
    const char* name;
    u64 titleId;
} port_known_title;

static const port_known_title port_known_titles[] = {
    {"SoulSilver", PORT_TITLE_SOULSILVER},
    {"Platinum",   PORT_TITLE_PLATINUM},
    {"HeartGold",  PORT_TITLE_HEARTGOLD},
};

typedef struct {
    FS_Archive archive;
    linked_list contents; /* list_item* with file_info* for matching port CIAs */
    data_op_data installInfo;
    list_item* updatingItem;
} port_updater_data;

static const char* port_name_for_title(u64 titleId) {
    for(u32 i = 0; i < sizeof(port_known_titles) / sizeof(port_known_titles[0]); i++) {
        if(port_known_titles[i].titleId == titleId) {
            return port_known_titles[i].name;
        }
    }

    return NULL;
}

static bool port_is_known_title(u64 titleId) {
    return port_name_for_title(titleId) != NULL;
}

static void port_format_version(char* out, size_t outSize, u16 version) {
    snprintf(out, outSize, "%u.%u.%u",
             (unsigned) ((version >> 10) & 0x3F),
             (unsigned) ((version >> 4) & 0x3F),
             (unsigned) (version & 0xF));
}

static bool port_get_installed_version(u64 titleId, u16* versionOut, bool* installedOut) {
    *installedOut = false;
    *versionOut = 0;

    AM_TitleEntry entry;
    if(R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &titleId, &entry))) {
        *installedOut = true;
        *versionOut = entry.version;
        return true;
    }

    return false;
}

/* Short, plain label that fits the 320px bottom screen. Details are on the top screen. */
static void port_build_item_label(list_item* item, file_info* info) {
    const char* portName = port_name_for_title(info->ciaInfo.titleId);
    if(portName == NULL) {
        portName = "Port";
    }

    bool installed = false;
    u16 installedVersion = 0;
    port_get_installed_version(info->ciaInfo.titleId, &installedVersion, &installed);

    char fileVer[24];
    char instVer[24];
    port_format_version(fileVer, sizeof(fileVer), info->ciaInfo.version);
    port_format_version(instVer, sizeof(instVer), installedVersion);

    bool inPorts = strncmp(info->path, PORTS_PATH_PRIMARY "/", strlen(PORTS_PATH_PRIMARY) + 1) != 0;
    const char* where = inPorts ? " (fbi/ports)" : "";

    if(!info->ciaInfo.loaded) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s%s", portName, where);
        item->color = COLOR_TEXT;
    } else if(!installed) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s: not installed yet (%s)%s", portName, fileVer, where);
        item->color = COLOR_WARNING;
    } else if(info->ciaInfo.version > installedVersion) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s: update ready %s -> %s%s", portName, instVer, fileVer, where);
        item->color = COLOR_ACCENT;
    } else if(info->ciaInfo.version < installedVersion) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s: file is OLDER %s -> %s%s", portName, instVer, fileVer, where);
        item->color = COLOR_WARNING;
    } else {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s: up to date (%s)%s", portName, instVer, where);
        item->color = COLOR_SUCCESS;
    }
}

static void port_refresh_labels(linked_list* list) {
    linked_list_iter iter;
    linked_list_iterate(list, &iter);
    while(linked_list_iter_has_next(&iter)) {
        list_item* item = (list_item*) linked_list_iter_next(&iter);
        port_build_item_label(item, (file_info*) item->data);
    }
}

static void port_updater_free_data(port_updater_data* data) {
    if(data == NULL) {
        return;
    }

    task_clear_files(&data->contents);
    linked_list_destroy(&data->contents);

    if(data->archive != 0) {
        fs_close_archive(data->archive);
        data->archive = 0;
    }

    free(data);
}

static void port_updater_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    port_updater_data* portData = (port_updater_data*) data;

    if(portData->updatingItem != NULL) {
        task_draw_file_info(view, portData->updatingItem->data, x1, y1, x2, y2);
        return;
    }

    if(linked_list_size(&portData->contents) > 0) {
        list_item* first = (list_item*) linked_list_get(&portData->contents, 0);
        if(first != NULL) {
            task_draw_file_info(view, first->data, x1, y1, x2, y2);
        }
    }
}

/* ---- install (overwrite only; never delete title; no network) ---- */

static Result port_is_src_directory(void* data, u32 index, bool* isDirectory) {
    *isDirectory = false;
    return 0;
}

static Result port_make_dst_directory(void* data, u32 index) {
    return 0;
}

static Result port_open_src(void* data, u32 index, u32* handle) {
    port_updater_data* portData = (port_updater_data*) data;
    file_info* info = (file_info*) portData->updatingItem->data;

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

static Result port_close_src(void* data, u32 index, bool succeeded, u32 handle) {
    return FSFILE_Close(handle);
}

static Result port_get_src_size(void* data, u32 handle, u64* size) {
    return FSFILE_GetSize(handle, size);
}

static Result port_read_src(void* data, u32 handle, u32* bytesRead, void* buffer, u64 offset, u32 size) {
    return FSFILE_Read(handle, bytesRead, offset, buffer, size);
}

static Result port_open_dst(void* data, u32 index, void* initialReadBlock, u64 size, u32* handle) {
    port_updater_data* portData = (port_updater_data*) data;
    file_info* info = (file_info*) portData->updatingItem->data;

    if(*(u16*) initialReadBlock != 0x2020) {
        return R_APP_BAD_DATA;
    }

    u64 titleId = 0;
    Result res = 0;
    if(R_FAILED(res = cia_get_title_id(&titleId, (u8*) initialReadBlock, portData->installInfo.bufferSize))) {
        return R_APP_BAD_DATA;
    }

    /* Strict: only the two known port title IDs, and still on the SD allowlist. */
    if(!port_is_known_title(titleId) || !fs_is_remote_install_allowed(titleId)) {
        return R_APP_SAFETY_REFUSED;
    }

    info->ciaInfo.titleId = titleId;
    info->ciaInfo.loaded = true;

    /* Overwrite in place — never AM_DeleteTitle. Saves stay with the title. */
    return AM_StartCiaInstallOverwrite(handle, MEDIATYPE_SD);
}

static Result port_close_dst(void* data, u32 index, bool succeeded, u32 handle) {
    if(succeeded) {
        /* No seed download / no network from this path. */
        return AM_FinishCiaInstall(handle);
    }

    return AM_CancelCIAInstall(handle);
}

static Result port_write_dst(void* data, u32 handle, u32* bytesWritten, void* buffer, u64 offset, u32 size) {
    return FSFILE_Write(handle, bytesWritten, offset, buffer, size, 0);
}

static Result port_suspend(void* data, u32 index) {
    return 0;
}

static Result port_restore(void* data, u32 index) {
    return 0;
}

static bool port_install_error(void* data, u32 index, Result res, ui_view** errorView) {
    if(res == R_APP_SAFETY_REFUSED) {
        *errorView = error_display_res(data, port_updater_draw_top, res,
                                       "Blocked: this file is not\none of your ports (SoulSilver,\nHeartGold or Platinum).\n\nOnly those can be updated here.");
    } else if(res == R_APP_BAD_DATA) {
        *errorView = error_display_res(data, port_updater_draw_top, res,
                                       "This CIA file looks broken\nor incomplete. Build or copy\nit again. Nothing was changed.");
    } else {
        *errorView = error_display_res(data, port_updater_draw_top, res,
                                       "The update did not finish.\nYour game and save were\nnot changed.");
    }

    return false;
}

static void port_install_update(ui_view* view, void* data, float* progress, char* text) {
    port_updater_data* portData = (port_updater_data*) data;

    if(portData->installInfo.finished) {
        ui_pop();
        info_destroy(view);

        extras_install_end();

        if(R_SUCCEEDED(portData->installInfo.result)) {
            file_info* doneInfo = (file_info*) portData->updatingItem->data;
            extras_history_append(doneInfo != NULL ? doneInfo->name : "Port", "OK");
            prompt_display_notify("All done!",
                                  "Your port is updated.\nYour save was kept.",
                                  COLOR_SUCCESS, portData, port_updater_draw_top, NULL);
        }

        /* Show the new installed version right away (no need to press X). */
        port_refresh_labels(&portData->contents);
        portData->updatingItem = NULL;
        return;
    }

    if((hidKeysDown() & KEY_B) && !portData->installInfo.finished) {
        svcSignalEvent(portData->installInfo.cancelEvent);
    }

    *progress = portData->installInfo.currTotal != 0
                ? (float) ((double) portData->installInfo.currProcessed / (double) portData->installInfo.currTotal)
                : 0;

    file_info* info = (file_info*) portData->updatingItem->data;
    snprintf(text, PROGRESS_TEXT_MAX,
             "Updating %s\n%.2f %s / %.2f %s\n%.2f %s/s, ETA %s",
             port_name_for_title(info->ciaInfo.titleId) != NULL
                 ? port_name_for_title(info->ciaInfo.titleId) : info->name,
             ui_get_display_size(portData->installInfo.currProcessed),
             ui_get_display_size_units(portData->installInfo.currProcessed),
             ui_get_display_size(portData->installInfo.currTotal),
             ui_get_display_size_units(portData->installInfo.currTotal),
             ui_get_display_size(portData->installInfo.bytesPerSecond),
             ui_get_display_size_units(portData->installInfo.bytesPerSecond),
             ui_get_display_eta(portData->installInfo.estimatedRemainingSeconds));
}

static void port_start_install(port_updater_data* data, list_item* item);

static void port_confirm_onresponse(ui_view* view, void* data, u32 response) {
    port_updater_data* portData = (port_updater_data*) data;

    if(response == PROMPT_YES && portData->updatingItem != NULL) {
        port_start_install(portData, portData->updatingItem);
    } else {
        portData->updatingItem = NULL;
    }
}

static void port_start_install(port_updater_data* data, list_item* item) {
    data->updatingItem = item;

    data->installInfo.data = data;
    data->installInfo.op = DATAOP_COPY;
    data->installInfo.bufferSize = 2 * 1024 * 1024;
    data->installInfo.copyEmpty = false;
    data->installInfo.continueOnError = false;

    data->installInfo.total = 1;
    data->installInfo.processed = 0;

    data->installInfo.isSrcDirectory = port_is_src_directory;
    data->installInfo.makeDstDirectory = port_make_dst_directory;
    data->installInfo.openSrc = port_open_src;
    data->installInfo.closeSrc = port_close_src;
    data->installInfo.getSrcSize = port_get_src_size;
    data->installInfo.readSrc = port_read_src;
    data->installInfo.openDst = port_open_dst;
    data->installInfo.closeDst = port_close_dst;
    data->installInfo.writeDst = port_write_dst;
    data->installInfo.suspend = port_suspend;
    data->installInfo.restore = port_restore;
    data->installInfo.error = port_install_error;

    data->installInfo.finished = false;
    data->installInfo.result = 0;
    data->installInfo.cancelEvent = 0;

    if(!extras_battery_ok_for_long_job()) {
        prompt_display_notify("Battery low",
                              "Battery looks low and the console\nis not charging.\n\nPlug in, then try again.",
                              COLOR_WARNING, NULL, NULL, NULL);
        data->updatingItem = NULL;
        return;
    }

    extras_install_begin();
    Result res = task_data_op(&data->installInfo);
    if(R_FAILED(res)) {
        extras_install_end();
        error_display_res(data, port_updater_draw_top, res, "Could not start the update.");
        data->updatingItem = NULL;
        return;
    }

    info_display("Updating port…", "B = cancel (nothing changes)", true, data, port_install_update, port_updater_draw_top);
}

static void port_ask_update(port_updater_data* data, list_item* item) {
    file_info* info = (file_info*) item->data;
    const char* name = port_name_for_title(info->ciaInfo.titleId);
    if(name == NULL) {
        name = info->name;
    }

    data->updatingItem = item;

    bool installed = false;
    u16 installedVersion = 0;
    port_get_installed_version(info->ciaInfo.titleId, &installedVersion, &installed);
    bool older = installed && info->ciaInfo.loaded && info->ciaInfo.version < installedVersion;

    char msg[320];
    snprintf(msg, sizeof(msg),
             "Update %s using this file?\n%.40s\n\n"
             "%s"
             "Your save is kept. The old version\n"
             "is not deleted first. If it fails,\n"
             "nothing changes. No internet used.\n\n"
             "Yes = update\nNo = cancel",
             name, info->name,
             older ? "WARNING: this file is OLDER than\nwhat is installed now.\n\n" : "");

    /* Requires explicit Yes before AM overwrite. */
    prompt_display_yes_no(older ? "Install older version?" : "Update port?", msg, older ? COLOR_WARNING : COLOR_TEXT, data, port_updater_draw_top, port_confirm_onresponse);
}

/* ---- list UI ---- */

static void port_list_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    port_updater_data* portData = (port_updater_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        port_updater_free_data(portData);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        port_ask_update(portData, selected);
        return;
    }

    if(hidKeysDown() & KEY_X) {
        /* Refresh labels (installed versions may have changed). */
        port_refresh_labels(items);
    }

    if(linked_list_size(items) == 0) {
        linked_list_iter iter;
        linked_list_iterate(&portData->contents, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            port_build_item_label(item, (file_info*) item->data);
            linked_list_add(items, item);
        }
    }
}

static void port_list_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    if(selected != NULL) {
        task_draw_file_info(view, selected->data, x1, y1, x2, y2);
    } else {
        port_updater_draw_top(view, data, x1, y1, x2, y2);
    }
}

static void port_list_open(port_updater_data* data) {
    list_display("Update my Pokemon ports", "A = update, X = refresh, B = back", data, port_list_update, port_list_draw_top);
}

/* ---- scan (primary /cias, then /fbi/ports) ---- */

typedef struct {
    port_updater_data* portData;
    populate_files_data popData;
    u32 pathIndex; /* 0 = /cias, 1 = /fbi/ports */
    const char* paths[2];
    bool cancelled;
} port_loading_data;

static void port_filter_known_only(port_updater_data* data) {
    linked_list kept;
    linked_list_init(&kept);

    linked_list_iter iter;
    linked_list_iterate(&data->contents, &iter);
    while(linked_list_iter_has_next(&iter)) {
        list_item* item = (list_item*) linked_list_iter_next(&iter);
        file_info* info = (file_info*) item->data;

        bool keep = info->isCia && info->ciaInfo.loaded
                    && port_is_known_title(info->ciaInfo.titleId)
                    && fs_is_remote_install_allowed(info->ciaInfo.titleId);

        linked_list_iter_remove(&iter);
        if(keep) {
            linked_list_add(&kept, item);
        } else {
            task_free_file(item);
        }
    }

    linked_list_destroy(&data->contents);
    data->contents = kept;
}

static void port_append_scan_results(port_updater_data* data, linked_list* scanned) {
    if(scanned == NULL) {
        return;
    }

    linked_list_iter iter;
    linked_list_iterate(scanned, &iter);
    while(linked_list_iter_has_next(&iter)) {
        list_item* item = (list_item*) linked_list_iter_next(&iter);
        linked_list_add(&data->contents, item);
        linked_list_iter_remove(&iter);
    }

    linked_list_destroy(scanned);
    free(scanned);
}

static void port_discard_scan_list(linked_list* scanned) {
    if(scanned == NULL) {
        return;
    }

    task_clear_files(scanned);
    linked_list_destroy(scanned);
    free(scanned);
}

static bool port_start_path_scan(port_loading_data* loadingData);

static void port_loading_update(ui_view* view, void* data, float* progress, char* text) {
    port_loading_data* loadingData = (port_loading_data*) data;

    if(loadingData->popData.finished) {
        if(R_FAILED(loadingData->popData.result)) {
            /* Soft-fail empty/missing folders; keep going to the other path. */
            port_discard_scan_list(loadingData->popData.items);
        } else {
            port_append_scan_results(loadingData->portData, loadingData->popData.items);
        }
        loadingData->popData.items = NULL;

        loadingData->pathIndex++;
        if(loadingData->pathIndex < 2 && !loadingData->cancelled) {
            if(port_start_path_scan(loadingData)) {
                snprintf(text, PROGRESS_TEXT_MAX, "Looking in %s…\n(on your SD card only)",
                         loadingData->paths[loadingData->pathIndex]);
                return;
            }
        }

        ui_pop();
        info_destroy(view);

        port_filter_known_only(loadingData->portData);

        if(loadingData->cancelled) {
            port_updater_free_data(loadingData->portData);
        } else if(linked_list_size(&loadingData->portData->contents) == 0) {
            prompt_display_notify("No ports found",
                                  "No SoulSilver, HeartGold or\nPlatinum port files were found.\n\n"
                                  "Put your built .cia file in the\n"
                                  "/cias folder (or /fbi/ports)\n"
                                  "on the SD card, then try again.\n\n"
                                  "Nothing is downloaded.",
                                  COLOR_TEXT, NULL, NULL, NULL);
            port_updater_free_data(loadingData->portData);
        } else {
            port_list_open(loadingData->portData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        loadingData->cancelled = true;
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "Looking in %s…\n(on your SD card only)",
             loadingData->paths[loadingData->pathIndex]);
}

static bool port_start_path_scan(port_loading_data* loadingData) {
    linked_list* items = (linked_list*) calloc(1, sizeof(linked_list));
    if(items == NULL) {
        return false;
    }

    linked_list_init(items);

    memset(&loadingData->popData, 0, sizeof(loadingData->popData));
    loadingData->popData.items = items;
    loadingData->popData.archive = loadingData->portData->archive;
    snprintf(loadingData->popData.path, FILE_PATH_MAX, "%s/", loadingData->paths[loadingData->pathIndex]);
    loadingData->popData.recursive = false;
    loadingData->popData.includeBase = false;
    loadingData->popData.meta = true;
    loadingData->popData.filter = fs_filter_cias;
    loadingData->popData.filterData = NULL;

    Result listRes = task_populate_files(&loadingData->popData);
    if(R_FAILED(listRes)) {
        linked_list_destroy(items);
        free(items);
        loadingData->popData.items = NULL;
        loadingData->popData.finished = true;
        loadingData->popData.result = listRes;
        return true; /* treat as finished empty so update advances */
    }

    return true;
}

void portupdater_open() {
    port_updater_data* data = (port_updater_data*) calloc(1, sizeof(port_updater_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate port updater data.");
        return;
    }

    linked_list_init(&data->contents);

    Result res = 0;
    if(R_FAILED(res = fs_open_archive(&data->archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        error_display_res(NULL, NULL, res, "Could not open the SD card.");
        port_updater_free_data(data);
        return;
    }

    /* Ensure both drop folders exist (builder uses /cias). */
    fs_ensure_dir(data->archive, PORTS_PATH_PRIMARY "/");
    fs_ensure_dir(data->archive, "/fbi/");
    fs_ensure_dir(data->archive, PORTS_PATH_SECONDARY "/");

    port_loading_data* loadingData = (port_loading_data*) calloc(1, sizeof(port_loading_data));
    if(loadingData == NULL) {
        error_display(NULL, NULL, "Failed to allocate loading data.");
        port_updater_free_data(data);
        return;
    }

    loadingData->portData = data;
    loadingData->pathIndex = 0;
    loadingData->paths[0] = PORTS_PATH_PRIMARY;
    loadingData->paths[1] = PORTS_PATH_SECONDARY;

    if(!port_start_path_scan(loadingData)) {
        error_display(NULL, NULL, "Could not look for port files.");
        free(loadingData);
        port_updater_free_data(data);
        return;
    }

    info_display("Update my ports", "B = cancel", false, loadingData, port_loading_update, NULL);
}
