#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "task/uitask.h"
#include "task/listtitles.h"
#include "../core/core.h"

#define SAVE_ROOT "/fbi/saves"
#define SAVE_PATH_MAX FILE_PATH_MAX

typedef struct {
    populate_titles_data populateData;
    bool populated;
    linked_list* items; /* owned by list view */
} savebackup_data;

typedef struct {
    u64 titleId;
    FS_MediaType mediaType;
    char titleName[128];
    list_item* titleItem;
} save_title_ctx;

typedef struct {
    save_title_ctx title;
    char backupRel[SAVE_PATH_MAX]; /* under /fbi/saves/... */
} save_restore_ctx;

static bool save_is_user_app(u64 titleId) {
    return ((titleId >> 32) & 0xFFFFFFFFULL) == 0x00040000ULL;
}

static void save_sanitize_name(char* out, size_t outSize, const char* in) {
    size_t j = 0;
    if(in == NULL || in[0] == '\0') {
        string_copy(out, "Unknown", outSize);
        return;
    }
    for(size_t i = 0; in[i] != '\0' && j + 1 < outSize && j < 48; i++) {
        unsigned char c = (unsigned char) in[i];
        if(isalnum(c) || c == ' ' || c == '-' || c == '_') {
            out[j++] = (char) c;
        } else if(c < 0x80) {
            out[j++] = '_';
        }
    }
    while(j > 0 && out[j - 1] == ' ') j--;
    out[j] = '\0';
    if(j == 0) {
        string_copy(out, "Unknown", outSize);
    }
}

static Result save_open_title_archive(FS_Archive* out, FS_MediaType media, u64 titleId) {
    u32 path[3] = {(u32) media, (u32) (titleId & 0xFFFFFFFFULL), (u32) ((titleId >> 32) & 0xFFFFFFFFULL)};
    return FSUSER_OpenArchive(out, ARCHIVE_USER_SAVEDATA, fs_make_path_binary(path, sizeof(path)));
}

static Result save_copy_file(FS_Archive srcArch, const char* srcPath, FS_Archive dstArch, const char* dstPath) {
    Result res = 0;
    Handle src = 0;
    Handle dst = 0;
    FS_Path* s = fs_make_path_utf8(srcPath);
    FS_Path* d = fs_make_path_utf8(dstPath);
    if(s == NULL || d == NULL) {
        fs_free_path_utf8(s);
        fs_free_path_utf8(d);
        return R_APP_OUT_OF_MEMORY;
    }

    if(R_FAILED(res = FSUSER_OpenFile(&src, srcArch, *s, FS_OPEN_READ, 0))) {
        fs_free_path_utf8(s);
        fs_free_path_utf8(d);
        return res;
    }

    u64 size = 0;
    FSFILE_GetSize(src, &size);

    /* Create parent already ensured; create/overwrite file */
    FSUSER_DeleteFile(dstArch, *d); /* ignore fail */
    if(R_FAILED(res = FSUSER_OpenFile(&dst, dstArch, *d, FS_OPEN_WRITE | FS_OPEN_CREATE, 0))) {
        FSFILE_Close(src);
        fs_free_path_utf8(s);
        fs_free_path_utf8(d);
        return res;
    }

    if(R_SUCCEEDED(res = FSFILE_SetSize(dst, size))) {
        u8* buf = (u8*) malloc(128 * 1024);
        if(buf == NULL) {
            res = R_APP_OUT_OF_MEMORY;
        } else {
            u64 off = 0;
            while(off < size && R_SUCCEEDED(res)) {
                u32 chunk = (u32) ((size - off) > (128 * 1024) ? (128 * 1024) : (size - off));
                u32 br = 0, bw = 0;
                if(R_FAILED(res = FSFILE_Read(src, &br, off, buf, chunk)) || br == 0) {
                    break;
                }
                if(R_FAILED(res = FSFILE_Write(dst, &bw, off, buf, br, FS_WRITE_FLUSH))) {
                    break;
                }
                off += br;
            }
            free(buf);
        }
    }

    FSFILE_Close(src);
    FSFILE_Close(dst);
    fs_free_path_utf8(s);
    fs_free_path_utf8(d);
    return res;
}

static Result save_copy_dir_recursive(FS_Archive srcArch, const char* srcDir, FS_Archive dstArch, const char* dstDir) {
    Result res = fs_ensure_dir(dstArch, dstDir);
    if(R_FAILED(res)) {
        return res;
    }

    FS_Path* dirPath = fs_make_path_utf8(srcDir);
    if(dirPath == NULL) {
        return R_APP_OUT_OF_MEMORY;
    }

    Handle dirHandle = 0;
    res = FSUSER_OpenDirectory(&dirHandle, srcArch, *dirPath);
    fs_free_path_utf8(dirPath);
    if(R_FAILED(res)) {
        /* Empty / missing save */
        return res;
    }

    while(R_SUCCEEDED(res)) {
        u32 entriesRead = 0;
        FS_DirectoryEntry entry;
        res = FSDIR_Read(dirHandle, &entriesRead, 1, &entry);
        if(R_FAILED(res) || entriesRead == 0) {
            if(entriesRead == 0) {
                res = 0;
            }
            break;
        }

        char name[FILE_NAME_MAX];
        utf16_to_utf8((uint8_t*) name, entry.name, FILE_NAME_MAX - 1);

        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char childSrc[SAVE_PATH_MAX];
        char childDst[SAVE_PATH_MAX];
        if(strcmp(srcDir, "/") == 0) {
            snprintf(childSrc, sizeof(childSrc), "/%s", name);
        } else {
            snprintf(childSrc, sizeof(childSrc), "%s/%s", srcDir, name);
        }
        if(strcmp(dstDir, "/") == 0) {
            snprintf(childDst, sizeof(childDst), "/%s", name);
        } else {
            snprintf(childDst, sizeof(childDst), "%s/%s", dstDir, name);
        }

        if(entry.attributes & FS_ATTRIBUTE_DIRECTORY) {
            if(R_FAILED(res = save_copy_dir_recursive(srcArch, childSrc, dstArch, childDst))) {
                break;
            }
        } else {
            if(R_FAILED(res = save_copy_file(srcArch, childSrc, dstArch, childDst))) {
                break;
            }
        }
    }

    FSDIR_Close(dirHandle);
    return res;
}

static Result save_delete_dir_recursive(FS_Archive arch, const char* dir) {
    Result res = 0;
    FS_Path* dirPath = fs_make_path_utf8(dir);
    if(dirPath == NULL) {
        return R_APP_OUT_OF_MEMORY;
    }

    Handle dirHandle = 0;
    if(R_FAILED(res = FSUSER_OpenDirectory(&dirHandle, arch, *dirPath))) {
        fs_free_path_utf8(dirPath);
        return res;
    }

    while(R_SUCCEEDED(res)) {
        u32 entriesRead = 0;
        FS_DirectoryEntry entry;
        res = FSDIR_Read(dirHandle, &entriesRead, 1, &entry);
        if(R_FAILED(res) || entriesRead == 0) {
            if(entriesRead == 0) res = 0;
            break;
        }
        char name[FILE_NAME_MAX];
        utf16_to_utf8((uint8_t*) name, entry.name, FILE_NAME_MAX - 1);
        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        char child[SAVE_PATH_MAX];
        if(strcmp(dir, "/") == 0) snprintf(child, sizeof(child), "/%s", name);
        else snprintf(child, sizeof(child), "%s/%s", dir, name);

        if(entry.attributes & FS_ATTRIBUTE_DIRECTORY) {
            if(R_FAILED(res = save_delete_dir_recursive(arch, child))) break;
        } else {
            FS_Path* fp = fs_make_path_utf8(child);
            if(fp == NULL) { res = R_APP_OUT_OF_MEMORY; break; }
            res = FSUSER_DeleteFile(arch, *fp);
            fs_free_path_utf8(fp);
            if(R_FAILED(res)) break;
        }
    }

    FSDIR_Close(dirHandle);
    fs_free_path_utf8(dirPath);
    if(R_SUCCEEDED(res) && strcmp(dir, "/") != 0) {
        FS_Path* dp = fs_make_path_utf8(dir);
        if(dp != NULL) {
            res = FSUSER_DeleteDirectory(arch, *dp);
            fs_free_path_utf8(dp);
        }
    }
    return res;
}

static void save_make_backup_folder_name(char* out, size_t outSize, const char* titleName, u64 titleId) {
    char safe[64];
    save_sanitize_name(safe, sizeof(safe), titleName);
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    char ts[32];
    if(tm != NULL) {
        strftime(ts, sizeof(ts), "%Y-%m-%d_%H%M%S", tm);
    } else {
        string_copy(ts, "unknown", sizeof(ts));
    }
    snprintf(out, outSize, "%s/%s [%016llX] COP%s", SAVE_ROOT, safe, (unsigned long long) titleId, ts);
}

static Result save_backup_title(u64 titleId, FS_MediaType media, const char* titleName, char* outRel, size_t outRelSize) {
    FS_Archive saveArch = 0;
    FS_Archive sdmc = 0;
    Result res = save_open_title_archive(&saveArch, media, titleId);
    if(R_FAILED(res)) {
        return res; /* no save */
    }

    if(R_FAILED(res = FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        FSUSER_CloseArchive(saveArch);
        return res;
    }

    fs_ensure_dir(sdmc, "/fbi/");
    fs_ensure_dir(sdmc, SAVE_ROOT);

    char folder[SAVE_PATH_MAX];
    save_make_backup_folder_name(folder, sizeof(folder), titleName, titleId);
    if(outRel != NULL) {
        string_copy(outRel, folder, outRelSize);
    }

    res = save_copy_dir_recursive(saveArch, "/", sdmc, folder);
    if(R_SUCCEEDED(res)) {
        FSUSER_ControlArchive(sdmc, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    } else {
        save_delete_dir_recursive(sdmc, folder); /* best-effort cleanup */
    }

    FSUSER_CloseArchive(saveArch);
    FSUSER_CloseArchive(sdmc);
    return res;
}

static Result save_restore_from_folder(u64 titleId, FS_MediaType media, const char* backupFolder) {
    FS_Archive saveArch = 0;
    FS_Archive sdmc = 0;
    Result res = save_open_title_archive(&saveArch, media, titleId);
    if(R_FAILED(res)) {
        return res;
    }
    if(R_FAILED(res = FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        FSUSER_CloseArchive(saveArch);
        return res;
    }

    /* Wipe current save contents (not the archive itself), then copy backup in. */
    save_delete_dir_recursive(saveArch, "/");
    res = save_copy_dir_recursive(sdmc, backupFolder, saveArch, "/");
    if(R_SUCCEEDED(res)) {
        res = FSUSER_ControlArchive(saveArch, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    }

    FSUSER_CloseArchive(saveArch);
    FSUSER_CloseArchive(sdmc);
    return res;
}


static Result save_backup_extdata(u64 titleId, FS_MediaType media, const char* titleName, const char* parentFolder) {
    /* Optional read-only copy of user extdata into <backup>/extdata/. Safe: never writes extdata. */
    FS_Archive extArch = 0;
    FS_Archive sdmc = 0;
    u32 path[3] = {(u32) media, (u32) (titleId & 0xFFFFFFFFULL), (u32) ((titleId >> 32) & 0xFFFFFFFFULL)};
    Result res = FSUSER_OpenArchive(&extArch, ARCHIVE_EXTDATA, fs_make_path_binary(path, sizeof(path)));
    if(R_FAILED(res)) {
        return res; /* no extdata — fine */
    }
    if(R_FAILED(res = FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        FSUSER_CloseArchive(extArch);
        return res;
    }
    char dest[SAVE_PATH_MAX];
    snprintf(dest, sizeof(dest), "%s/extdata", parentFolder);
    res = save_copy_dir_recursive(extArch, "/", sdmc, dest);
    if(R_SUCCEEDED(res)) {
        FSUSER_ControlArchive(sdmc, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    }
    FSUSER_CloseArchive(extArch);
    FSUSER_CloseArchive(sdmc);
    (void) titleName;
    return res;
}

/* ---- UI: list titles ---- */

static bool save_filter(void* data, u64 titleId, FS_MediaType mediaType) {
    (void) data;
    if(mediaType != MEDIATYPE_SD && mediaType != MEDIATYPE_GAME_CARD) {
        return false;
    }
    return save_is_user_app(titleId);
}

static int save_compare(void* data, const void* p1, const void* p2) {
    (void) data;
    list_item* a = (list_item*) p1;
    list_item* b = (list_item*) p2;
    return strncasecmp(a->name, b->name, LIST_ITEM_NAME_MAX);
}

static void save_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) data;
    if(selected != NULL && selected->data != NULL) {
        task_draw_title_info(view, selected->data, x1, y1, x2, y2);
        return;
    }
    const char* tip =
        "Back up game saves to your SD card, or restore an older backup.\n\n"
        "Restoring always makes a fresh backup of the current save first.\n"
        "Only your games — never system saves.";
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.48f, 0.48f, x2 - x1 - 24.0f);
    screen_draw_string_wrap(tip, x1 + 12.0f, y1 + 20.0f, 0.48f, 0.48f, COLOR_TEXT, true, x2 - 12.0f);
}

static void save_backup_progress(ui_view* view, void* data, float* progress, char* text) {
    save_title_ctx* ctx = (save_title_ctx*) data;
    (void) progress;
    snprintf(text, PROGRESS_TEXT_MAX, "Backing up\n%s\nPlease wait…", ctx->titleName);

    char folder[SAVE_PATH_MAX];
    Result res = save_backup_title(ctx->titleId, ctx->mediaType, ctx->titleName, folder, sizeof(folder));

    ui_pop();
    info_destroy(view);

    if(R_SUCCEEDED(res)) {
        /* Optional extdata (read-only); ignore failure. */
        Result extRes = save_backup_extdata(ctx->titleId, ctx->mediaType, ctx->titleName, folder);
        char msg[420];
        if(R_SUCCEEDED(extRes)) {
            snprintf(msg, sizeof(msg),
                     "Save backed up!\n\n%s\n\nFolder on SD:\n%.160s\n\n(Extdata included.)",
                     ctx->titleName, folder);
        } else {
            snprintf(msg, sizeof(msg),
                     "Save backed up!\n\n%s\n\nFolder on SD:\n%.180s\n\n(No extdata, or skipped.)",
                     ctx->titleName, folder);
        }
        prompt_display_notify("Done!", msg, COLOR_ACCENT, NULL, NULL, NULL);
    } else if(R_MODULE(res) == RM_FS && (R_SUMMARY(res) == RS_NOTFOUND || res == (Result) 0xC8804478)) {
        /* Common "not found" / no save — show friendly */
        prompt_display_notify(
            "No save data",
            "This game does not have save data\nyet, or it could not be opened.\n\nPlay the game once, then try again.",
            COLOR_TEXT, NULL, NULL, NULL);
    } else {
        error_display_res(NULL, NULL, res, "Could not back up this save.");
    }
    free(ctx);
}

static void save_do_backup(list_item* selected) {
    title_info* info = (title_info*) selected->data;
    save_title_ctx* ctx = (save_title_ctx*) calloc(1, sizeof(save_title_ctx));
    if(ctx == NULL) {
        error_display(NULL, NULL, "Out of memory.");
        return;
    }
    ctx->titleId = info->titleId;
    ctx->mediaType = info->mediaType;
    ctx->titleItem = selected;
    if(info->hasMeta && info->meta.shortDescription[0]) {
        string_copy(ctx->titleName, info->meta.shortDescription, sizeof(ctx->titleName));
    } else {
        string_copy(ctx->titleName, selected->name, sizeof(ctx->titleName));
    }
    info_display("Back up save", "Please wait…", false, ctx, save_backup_progress, NULL);
}

/* ---- Restore: list backup folders for title ---- */

static void save_restore_run(ui_view* view, void* data, float* progress, char* text) {
    save_restore_ctx* ctx = (save_restore_ctx*) data;
    (void) progress;
    snprintf(text, PROGRESS_TEXT_MAX, "Making a safety backup,\nthen restoring…\n%s", ctx->title.titleName);

    char safety[SAVE_PATH_MAX];
    Result res = save_backup_title(ctx->title.titleId, ctx->title.mediaType, ctx->title.titleName, safety, sizeof(safety));
    if(R_FAILED(res)) {
        /* If no current save, still allow restore */
        safety[0] = '\0';
    }

    res = save_restore_from_folder(ctx->title.titleId, ctx->title.mediaType, ctx->backupRel);

    ui_pop();
    info_destroy(view);

    if(R_SUCCEEDED(res)) {
        char msg[420];
        if(safety[0]) {
            snprintf(msg, sizeof(msg),
                     "Save restored!\n\nA safety copy of the old save\nis at:\n%.180s",
                     safety);
        } else {
            snprintf(msg, sizeof(msg), "Save restored!");
        }
        prompt_display_notify("Done!", msg, COLOR_ACCENT, NULL, NULL, NULL);
    } else {
        error_display_res(NULL, NULL, res, "Could not restore this save.");
    }
    free(ctx);
}

static void save_restore_confirm(ui_view* view, void* data, u32 response) {
    (void) view;
    save_restore_ctx* ctx = (save_restore_ctx*) data;
    if(response == PROMPT_YES) {
        info_display("Restore save", "Please wait…", false, ctx, save_restore_run, NULL);
    } else {
        free(ctx);
    }
}

static void save_pick_backup_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    save_title_ctx* title = (save_title_ctx*) data;

    if(hidKeysDown() & KEY_B) {
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }
        ui_pop();
        list_destroy(view);
        free(title);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected->data == NULL) {
            return;
        }
        save_restore_ctx* ctx = (save_restore_ctx*) calloc(1, sizeof(save_restore_ctx));
        if(ctx == NULL) return;
        ctx->title = *title;
        string_copy(ctx->backupRel, (char*) selected->data, sizeof(ctx->backupRel));
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "Restore this backup for\n%s?\n\n"
                 "A fresh backup of the current save\n"
                 "will be made first.\n\n"
                 "%.120s",
                 title->titleName, ctx->backupRel);
        prompt_display_yes_no("Restore save?", msg, COLOR_WARNING, ctx, NULL, save_restore_confirm);
        return;
    }

    if(linked_list_size(items) == 0) {
        FS_Archive sdmc = 0;
        if(R_FAILED(FSUSER_OpenArchive(&sdmc, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
            list_item* empty = calloc(1, sizeof(list_item));
            if(empty) {
                string_copy(empty->name, "(Could not open SD card)", sizeof(empty->name));
                empty->color = COLOR_DANGER;
                linked_list_add(items, empty);
            }
            return;
        }
        fs_ensure_dir(sdmc, "/fbi/");
        fs_ensure_dir(sdmc, SAVE_ROOT);

        FS_Path* root = fs_make_path_utf8(SAVE_ROOT);
        Handle dir = 0;
        char tidHex[20];
        snprintf(tidHex, sizeof(tidHex), "%016llX", (unsigned long long) title->titleId);

        if(root != NULL && R_SUCCEEDED(FSUSER_OpenDirectory(&dir, sdmc, *root))) {
            while(true) {
                u32 n = 0;
                FS_DirectoryEntry entry;
                if(R_FAILED(FSDIR_Read(dir, &n, 1, &entry)) || n == 0) break;
                if(!(entry.attributes & FS_ATTRIBUTE_DIRECTORY)) continue;
                char name[FILE_NAME_MAX];
                utf16_to_utf8((uint8_t*) name, entry.name, FILE_NAME_MAX - 1);
                if(strstr(name, tidHex) == NULL) continue;

                list_item* row = calloc(1, sizeof(list_item));
                char* pathCopy = (char*) malloc(SAVE_PATH_MAX);
                if(row == NULL || pathCopy == NULL) {
                    free(row);
                    free(pathCopy);
                    continue;
                }
                snprintf(pathCopy, SAVE_PATH_MAX, "%s/%s", SAVE_ROOT, name);
                string_copy(row->name, name, sizeof(row->name));
                row->color = COLOR_TEXT;
                row->data = pathCopy;
                linked_list_add(items, row);
            }
            FSDIR_Close(dir);
        }
        fs_free_path_utf8(root);
        FSUSER_CloseArchive(sdmc);

        if(linked_list_size(items) == 0) {
            list_item* empty = calloc(1, sizeof(list_item));
            if(empty) {
                string_copy(empty->name, "(No backups for this game yet)", sizeof(empty->name));
                empty->color = COLOR_HINT;
                linked_list_add(items, empty);
            }
        }
    }
}

static void save_open_restore_picker(list_item* selected) {
    title_info* info = (title_info*) selected->data;
    save_title_ctx* ctx = calloc(1, sizeof(save_title_ctx));
    if(ctx == NULL) return;
    ctx->titleId = info->titleId;
    ctx->mediaType = info->mediaType;
    if(info->hasMeta && info->meta.shortDescription[0]) {
        string_copy(ctx->titleName, info->meta.shortDescription, sizeof(ctx->titleName));
    } else {
        string_copy(ctx->titleName, selected->name, sizeof(ctx->titleName));
    }
    list_display("Pick a backup", "A = restore (asks first), B = back", ctx, save_pick_backup_update, save_draw_top);
}

static void save_action_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    list_item* titleItem = (list_item*) data;
    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);
        return;
    }
    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected->data == save_do_backup) {
            ui_pop();
            list_destroy(view);
            save_do_backup(titleItem);
            return;
        }
        if(selected->data == save_open_restore_picker) {
            ui_pop();
            list_destroy(view);
            save_open_restore_picker(titleItem);
            return;
        }
    }
    if(linked_list_size(items) == 0) {
        static list_item backup = {"Back up save now", COLOR_SUCCESS, save_do_backup};
        static list_item restore = {"Restore a backup…", COLOR_TEXT, save_open_restore_picker};
        linked_list_add(items, &backup);
        linked_list_add(items, &restore);
    }
}

static void save_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    savebackup_data* listData = (savebackup_data*) data;

    if(hidKeysDown() & KEY_B) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) svcSleepThread(1000000);
        }
        ui_pop();
        task_clear_titles(items);
        list_destroy(view);
        free(listData);
        return;
    }

    if(!listData->populated || (hidKeysDown() & KEY_X)) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) svcSleepThread(1000000);
        }
        listData->populateData.items = items;
        Result res = task_populate_titles(&listData->populateData);
        if(R_FAILED(res)) {
            error_display_res(NULL, NULL, res, "Failed to list games.");
        }
        listData->populated = true;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        list_display("Save options", "A = choose, B = back", selected, save_action_update, save_draw_top);
        return;
    }
}

void savebackup_open(void) {
    savebackup_data* data = calloc(1, sizeof(savebackup_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Out of memory.");
        return;
    }
    data->populateData.userData = data;
    data->populateData.filter = save_filter;
    data->populateData.compare = save_compare;
    data->populateData.finished = true;
    list_display("Back up my saves", "A = choose game, B = back, X = refresh", data, save_update, save_draw_top);
}
