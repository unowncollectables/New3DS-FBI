#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "task/uitask.h"
#include "../core/core.h"

/* Local-only port CIAs. Never download / never contact the network here.
   Primary: sdmc:/cias/  (Pokemon 3DS Builder deploy path: soulsilver3ds.cia / platinum3ds.cia)
   Secondary: sdmc:/fbi/ports/ (optional manual drop folder) */
#define PORTS_PATH_PRIMARY   "/cias"
#define PORTS_PATH_SECONDARY "/fbi/ports"

#define PORT_TITLE_SOULSILVER 0x000400000F7D4000ULL
#define PORT_TITLE_PLATINUM   0x000400000F7D3D00ULL

typedef struct {
    const char* name;
    u64 titleId;
} port_known_title;

static const port_known_title port_known_titles[] = {
    {"SoulSilver", PORT_TITLE_SOULSILVER},
    {"Platinum",   PORT_TITLE_PLATINUM},
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
    snprintf(out, outSize, "%u (%u.%u.%u)",
             (unsigned) version,
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

static void port_build_item_label(list_item* item, file_info* info) {
    const char* portName = port_name_for_title(info->ciaInfo.titleId);
    if(portName == NULL) {
        portName = "Port";
    }

    char ciaVer[96];
    char installedVer[48];
    bool installed = false;
    u16 installedVersion = 0;

    /* Builder embeds SS3DS_VERSION (e.g. "run 59") in SMDH publisher; also CIA title version. */
    if(info->ciaInfo.loaded && info->ciaInfo.hasMeta && !string_is_empty(info->ciaInfo.meta.publisher)) {
        char titleVer[40];
        port_format_version(titleVer, sizeof(titleVer), info->ciaInfo.version);
        snprintf(ciaVer, sizeof(ciaVer), "%s / %s", info->ciaInfo.meta.publisher, titleVer);
    } else if(info->ciaInfo.loaded) {
        port_format_version(ciaVer, sizeof(ciaVer), info->ciaInfo.version);
    } else {
        snprintf(ciaVer, sizeof(ciaVer), "size %.1f %s",
                 ui_get_display_size(info->size),
                 ui_get_display_size_units(info->size));
    }

    port_get_installed_version(info->ciaInfo.titleId, &installedVersion, &installed);
    if(installed) {
        port_format_version(installedVer, sizeof(installedVer), installedVersion);
    } else {
        snprintf(installedVer, sizeof(installedVer), "not installed");
    }

    char where[24];
    if(strncmp(info->path, PORTS_PATH_PRIMARY "/", strlen(PORTS_PATH_PRIMARY) + 1) == 0) {
        snprintf(where, sizeof(where), "cias");
    } else {
        snprintf(where, sizeof(where), "fbi/ports");
    }

    snprintf(item->name, LIST_ITEM_NAME_MAX, "%s [%s] | installed %s -> CIA %s",
             portName, where, installedVer, ciaVer);

    if(!installed) {
        item->color = COLOR_WARNING;
    } else if(info->ciaInfo.loaded && info->ciaInfo.version != installedVersion) {
        item->color = COLOR_ACCENT;
    } else if(info->ciaInfo.loaded && info->ciaInfo.version == installedVersion) {
        item->color = COLOR_SUCCESS;
    } else {
        item->color = COLOR_TEXT;
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
        return R_APP_BAD_DATA;
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
    if(res == R_APP_BAD_DATA) {
        *errorView = error_display_res(data, port_updater_draw_top, res,
                                       "Safety refusal.\n\nOnly SoulSilver / Platinum\nport CIAs in sdmc:/cias/\n(or sdmc:/fbi/ports/)\nare accepted. System/NAND/\nfirmware titles are never installed.");
    } else {
        *errorView = error_display_res(data, port_updater_draw_top, res,
                                       "Port update failed.\nOverwrite was cancelled;\nprevious title/save untouched.");
    }

    return false;
}

static void port_install_update(ui_view* view, void* data, float* progress, char* text) {
    port_updater_data* portData = (port_updater_data*) data;

    if(portData->installInfo.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(portData->installInfo.result)) {
            prompt_display_notify("Update my ports",
                                  "Overwrite install finished.\nSave data was preserved.",
                                  COLOR_SUCCESS, portData, port_updater_draw_top, NULL);
        }

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

    Result res = task_data_op(&data->installInfo);
    if(R_FAILED(res)) {
        error_display_res(data, port_updater_draw_top, res, "Failed to start port update.");
        data->updatingItem = NULL;
        return;
    }

    info_display("Update my ports", "B: Cancel (safe rollback)", true, data, port_install_update, port_updater_draw_top);
}

static void port_ask_update(port_updater_data* data, list_item* item) {
    file_info* info = (file_info*) item->data;
    const char* name = port_name_for_title(info->ciaInfo.titleId);
    if(name == NULL) {
        name = info->name;
    }

    data->updatingItem = item;

    char msg[320];
    snprintf(msg, sizeof(msg),
             "Update %s from local CIA?\n\n"
             "Uses overwrite install (AM).\n"
             "Does NOT delete the title first.\n"
             "Save data is preserved.\n"
             "On failure, install is cancelled\n"
             "and the previous title stays.\n\n"
             "File: %.40s\n"
             "100%% local — no download.",
             name, info->name);

    /* Requires explicit Yes before AM overwrite. */
    prompt_display_yes_no("Update my ports", msg, COLOR_TEXT, data, port_updater_draw_top, port_confirm_onresponse);
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
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            port_build_item_label(item, (file_info*) item->data);
        }
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
        if(loadingData->pathIndex < 2) {
            if(port_start_path_scan(loadingData)) {
                snprintf(text, PROGRESS_TEXT_MAX, "Scanning sdmc:%s ...\n(local only, no network)",
                         loadingData->paths[loadingData->pathIndex]);
                return;
            }
        }

        ui_pop();
        info_destroy(view);

        port_filter_known_only(loadingData->portData);

        if(linked_list_size(&loadingData->portData->contents) == 0) {
            prompt_display_notify("Update my ports",
                                  "No SoulSilver / Platinum CIAs found.\n\n"
                                  "Pokemon 3DS Builder copies them to:\n"
                                  "  sdmc:/cias/soulsilver3ds.cia\n"
                                  "  sdmc:/cias/platinum3ds.cia\n\n"
                                  "Optional: also sdmc:/fbi/ports/\n\n"
                                  "Matched by title ID only.\n"
                                  "Nothing is downloaded — local only.",
                                  COLOR_TEXT, NULL, NULL, NULL);
            port_updater_free_data(loadingData->portData);
        } else {
            port_list_open(loadingData->portData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "Scanning sdmc:%s ...\n(local only, no network)",
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
        error_display_res(NULL, NULL, res, "Failed to open SDMC.");
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
        error_display(NULL, NULL, "Failed to start ports scan.");
        free(loadingData);
        port_updater_free_data(data);
        return;
    }

    info_display("Update my ports", "B = cancel", false, loadingData, port_loading_update, NULL);
}
