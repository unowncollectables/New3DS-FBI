#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <jansson.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"
#include "../libs/quirc/quirc_internal.h"

static bool remoteinstall_get_last_urls(char* out, size_t size) {
    if(out == NULL || size == 0) {
        return false;
    }

    Handle file = 0;
    if(R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, "/fbi/lasturls"), FS_OPEN_READ, 0))) {
        return false;
    }

    u32 bytesRead = 0;
    FSFILE_Read(file, &bytesRead, 0, out, size - 1);
    out[bytesRead] = '\0';

    FSFILE_Close(file);

    return bytesRead != 0;
}

static Result remoteinstall_set_last_urls(const char* urls) {
    Result res = 0;

    FS_Archive sdmcArchive = 0;
    if(R_SUCCEEDED(res = FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        FS_Path path = fsMakePath(PATH_ASCII, "/fbi/lasturls");

        Handle file = 0;
        if(R_SUCCEEDED(FSUSER_OpenFile(&file, sdmcArchive, path, FS_OPEN_READ, 0))) {
            FSFILE_Close(file);

            res = FSUSER_DeleteFile(sdmcArchive, path);
        }

        if(urls != NULL && strlen(urls) != 0
           && R_SUCCEEDED(res)
           && R_SUCCEEDED(res = fs_ensure_dir(sdmcArchive, "/fbi/"))
           && R_SUCCEEDED(res = FSUSER_OpenFile(&file, sdmcArchive, path, FS_OPEN_WRITE | FS_OPEN_CREATE, 0))) {
            u32 bytesWritten = 0;
            res = FSFILE_Write(file, &bytesWritten, 0, urls, strlen(urls), FS_WRITE_FLUSH | FS_WRITE_UPDATE_TIME);

            Result closeRes = FSFILE_Close(file);
            if(R_SUCCEEDED(res)) {
                res = closeRes;
            }
        }

        Result closeRes = FSUSER_CloseArchive(sdmcArchive);
        if(R_SUCCEEDED(res)) {
            res = closeRes;
        }
    }

    return res;
}

#define QR_IMAGE_WIDTH 400
#define QR_IMAGE_HEIGHT 240

typedef struct {
    struct quirc* qrContext;
    u32 tex;

    bool capturing;
    capture_cam_data captureInfo;
} remoteinstall_qr_data;

static void remoteinstall_qr_stop_capture(remoteinstall_qr_data* data) {
    if(!data->captureInfo.finished) {
        svcSignalEvent(data->captureInfo.cancelEvent);
        while(!data->captureInfo.finished) {
            svcSleepThread(1000000);
        }
    }

    data->capturing = false;

    if(data->captureInfo.buffer != NULL) {
        memset(data->captureInfo.buffer, 0, QR_IMAGE_WIDTH * QR_IMAGE_HEIGHT * sizeof(u16));
    }
}

static void remoteinstall_qr_free_data(remoteinstall_qr_data* data) {
    remoteinstall_qr_stop_capture(data);

    if(data->captureInfo.buffer != NULL) {
        free(data->captureInfo.buffer);
        data->captureInfo.buffer = NULL;
    }

    if(data->tex != 0) {
        screen_unload_texture(data->tex);
        data->tex = 0;
    }

    if(data->qrContext != NULL) {
        quirc_destroy(data->qrContext);
        data->qrContext = NULL;
    }

    free(data);
}

static void remoteinstall_qr_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    remoteinstall_qr_data* installData = (remoteinstall_qr_data*) data;

    if(installData->tex != 0) {
        svcWaitSynchronization(installData->captureInfo.mutex, U64_MAX);
        screen_load_texture_untiled(installData->tex, installData->captureInfo.buffer, QR_IMAGE_WIDTH * QR_IMAGE_HEIGHT * sizeof(u16), QR_IMAGE_WIDTH, QR_IMAGE_HEIGHT, GPU_RGB565, false);
        svcReleaseMutex(installData->captureInfo.mutex);

        screen_draw_texture(installData->tex, 0, 0, QR_IMAGE_WIDTH, QR_IMAGE_HEIGHT);
    }
}

static void remoteinstall_qr_update(ui_view* view, void* data, float* progress, char* text) {
    remoteinstall_qr_data* installData = (remoteinstall_qr_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        info_destroy(view);

        remoteinstall_qr_free_data(installData);

        return;
    }

    if(hidKeysDown() & KEY_X) {
        remoteinstall_qr_stop_capture(installData);

        installData->captureInfo.camera = installData->captureInfo.camera == CAMERA_OUTER ? CAMERA_INNER : CAMERA_OUTER;

        return;
    }

    if(!installData->capturing) {
        Result capRes = task_capture_cam(&installData->captureInfo);
        if(R_FAILED(capRes)) {
            ui_pop();
            info_destroy(view);

            error_display_res(NULL, NULL, capRes, "Failed to start camera capture.");

            remoteinstall_qr_free_data(installData);
            return;
        } else {
            installData->capturing = true;
        }
    }

    if(installData->captureInfo.finished) {
        ui_pop();
        info_destroy(view);

        if(R_FAILED(installData->captureInfo.result)) {
            error_display_res(NULL, NULL, installData->captureInfo.result, "Error while capturing camera frames.");
        }

        remoteinstall_qr_free_data(installData);

        return;
    }

    int w = 0;
    int h = 0;
    uint8_t* qrBuf = quirc_begin(installData->qrContext, &w, &h);

    svcWaitSynchronization(installData->captureInfo.mutex, U64_MAX);

    for(int x = 0; x < w; x++) {
        for(int y = 0; y < h; y++) {
            u16 px = installData->captureInfo.buffer[y * QR_IMAGE_WIDTH + x];
            qrBuf[y * w + x] = (u8) (((((px >> 11) & 0x1F) << 3) + (((px >> 5) & 0x3F) << 2) + ((px & 0x1F) << 3)) / 3);
        }
    }

    svcReleaseMutex(installData->captureInfo.mutex);

    quirc_end(installData->qrContext);

    int qrCount = quirc_count(installData->qrContext);
    for(int i = 0; i < qrCount; i++) {
        struct quirc_code qrCode;
        quirc_extract(installData->qrContext, i, &qrCode);

        struct quirc_data qrData;
        quirc_decode_error_t err = quirc_decode(&qrCode, &qrData);

        if(err == 0) {
            remoteinstall_qr_stop_capture(installData);

            remoteinstall_set_last_urls((const char*) qrData.payload);

            action_install_url("Install from the scanned QR code?", (const char*) qrData.payload, NULL, NULL, NULL, NULL, NULL);
            return;
        }
    }

    snprintf(text, PROGRESS_TEXT_MAX, "Waiting for QR code...");
}

static void remoteinstall_scan_qr_code() {
    remoteinstall_qr_data* data = (remoteinstall_qr_data*) calloc(1, sizeof(remoteinstall_qr_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate QR install data.");

        return;
    }

    data->tex = 0;

    data->capturing = false;

    data->captureInfo.width = QR_IMAGE_WIDTH;
    data->captureInfo.height = QR_IMAGE_HEIGHT;

    data->captureInfo.camera = CAMERA_OUTER;

    data->captureInfo.finished = true;

    data->qrContext = quirc_new();
    if(data->qrContext == NULL) {
        error_display(NULL, NULL, "Failed to create QR context.");

        remoteinstall_qr_free_data(data);
        return;
    }

    if(quirc_resize(data->qrContext, QR_IMAGE_WIDTH, QR_IMAGE_HEIGHT) != 0) {
        error_display(NULL, NULL, "Failed to resize QR context.");

        remoteinstall_qr_free_data(data);
        return;
    }

    data->captureInfo.buffer = (u16*) calloc(1, QR_IMAGE_WIDTH * QR_IMAGE_HEIGHT * sizeof(u16));
    if(data->captureInfo.buffer == NULL) {
        error_display(NULL, NULL, "Failed to create image buffer.");

        remoteinstall_qr_free_data(data);
        return;
    }

    data->tex = screen_allocate_free_texture();

    info_display("QR Code Install", "B: Return, X: Switch Camera", false, data, remoteinstall_qr_update, remoteinstall_qr_draw_top);
}

static void remoteinstall_manually_enter_urls_onresponse(ui_view* view, void* data, SwkbdButton button, const char* response) {
    if(button == SWKBD_BUTTON_CONFIRM) {
        remoteinstall_set_last_urls(response);

        action_install_url("Install from the entered URL(s)?", response, NULL, NULL, NULL, NULL, NULL);
    }
}

static void remoteinstall_manually_enter_urls() {
    kbd_display("Enter URL(s)", "", SWKBD_TYPE_NORMAL, SWKBD_MULTILINE, SWKBD_NOTEMPTY_NOTBLANK, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX, NULL, remoteinstall_manually_enter_urls_onresponse);
}

static void remoteinstall_repeat_last_request() {
    char* textBuf = (char*) calloc(1, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX);
    if(textBuf != NULL) {
        if(remoteinstall_get_last_urls(textBuf, DOWNLOAD_URL_MAX * INSTALL_URLS_MAX)) {
            action_install_url("Install from the last requested URL(s)?", textBuf, NULL, NULL, NULL, NULL, NULL);
        } else {
            prompt_display_notify("Failure", "No previously requested URL(s) could be found.", COLOR_TEXT, NULL, NULL, NULL);
        }

        free(textBuf);
    } else {
        error_display_res(NULL, NULL, R_APP_OUT_OF_MEMORY, "Failed to allocate URL text buffer.");
    }
}

static void remoteinstall_forget_last_request() {
    Result forgetRes = remoteinstall_set_last_urls(NULL);
    if(R_SUCCEEDED(forgetRes)) {
        prompt_display_notify("Success", "Last requested URL(s) forgotten.", COLOR_TEXT, NULL, NULL, NULL);
    } else {
        error_display_res(NULL, NULL, forgetRes, "Failed to forget last requested URL(s).");
    }
}

static bool remoteinstall_str_ends_with_ci(const char* str, const char* suffix) {
    if(str == NULL || suffix == NULL) {
        return false;
    }

    size_t strLen = strlen(str);
    size_t suffixLen = strlen(suffix);
    if(suffixLen > strLen) {
        return false;
    }

    const char* a = str + (strLen - suffixLen);
    for(size_t i = 0; i < suffixLen; i++) {
        if(tolower((unsigned char) a[i]) != tolower((unsigned char) suffix[i])) {
            return false;
        }
    }

    return true;
}

static bool remoteinstall_parse_github_repo(const char* input, char* owner, size_t ownerSize, char* repo, size_t repoSize) {
    if(input == NULL || owner == NULL || repo == NULL || ownerSize == 0 || repoSize == 0) {
        return false;
    }

    const char* start = input;
    while(*start != '\0' && isspace((unsigned char) *start)) {
        start++;
    }

    if(strncmp(start, "https://", 8) == 0) {
        start += 8;
    } else if(strncmp(start, "http://", 7) == 0) {
        start += 7;
    }

    if(strncmp(start, "www.", 4) == 0) {
        start += 4;
    }

    if(strncmp(start, "github.com/", 11) == 0) {
        start += 11;
    }

    char buf[256];
    string_copy(buf, start, sizeof(buf));

    char* end = buf + strlen(buf);
    while(end > buf && isspace((unsigned char) end[-1])) {
        *--end = '\0';
    }

    if(end > buf + 4 && strcmp(end - 4, ".git") == 0) {
        end[-4] = '\0';
    }

    while(end > buf && end[-1] == '/') {
        *--end = '\0';
    }

    char* slash = strchr(buf, '/');
    if(slash == NULL || slash == buf || slash[1] == '\0') {
        return false;
    }

    *slash = '\0';
    char* repoPart = slash + 1;
    char* extra = strchr(repoPart, '/');
    if(extra != NULL) {
        *extra = '\0';
    }

    if(strlen(buf) == 0 || strlen(repoPart) == 0) {
        return false;
    }

    string_copy(owner, buf, ownerSize);
    string_copy(repo, repoPart, repoSize);
    return true;
}

typedef struct {
    char* url;
    u64 size;
} remoteinstall_github_asset;

typedef struct {
    list_item** items;
    remoteinstall_github_asset* assets;
    u32 count;
    char listTitle[64];
} remoteinstall_github_assets_data;

static void remoteinstall_github_free_assets(remoteinstall_github_assets_data* data) {
    if(data == NULL) {
        return;
    }

    if(data->items != NULL) {
        for(u32 i = 0; i < data->count; i++) {
            free(data->items[i]);
        }

        free(data->items);
        data->items = NULL;
    }

    if(data->assets != NULL) {
        for(u32 i = 0; i < data->count; i++) {
            free(data->assets[i].url);
        }

        free(data->assets);
        data->assets = NULL;
    }

    data->count = 0;
    free(data);
}

static bool remoteinstall_sd_has_space(u64 needed) {
    FS_ArchiveResource resource = {0};
    if(R_FAILED(FSUSER_GetArchiveResource(&resource, SYSTEM_MEDIATYPE_SD))) {
        /* If we cannot query free space, do not block; AM will fail safely. */
        return true;
    }

    u64 freeBytes = (u64) resource.freeClusters * (u64) resource.clusterSize;
    /* Leave a small cushion for filesystem overhead. */
    return freeBytes > needed + (2ull * 1024ull * 1024ull);
}

static void remoteinstall_github_assets_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    remoteinstall_github_assets_data* assetsData = (remoteinstall_github_assets_data*) data;

    if(hidKeysDown() & KEY_B) {
        remoteinstall_github_free_assets(assetsData);

        ui_pop();
        list_destroy(view);
        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        u32 index = 0;
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            if(item == selected) {
                break;
            }

            index++;
        }

        if(index < assetsData->count && assetsData->assets[index].url != NULL) {
            u64 size = assetsData->assets[index].size;
            if(size > 0 && !remoteinstall_sd_has_space(size)) {
                prompt_display_notify("Failure", "Not enough free space on the SD card.", COLOR_TEXT, NULL, NULL, NULL);
                return;
            }

            remoteinstall_set_last_urls(assetsData->assets[index].url);
            action_install_url("Install this GitHub release asset?\n(Unsafe/system CIAs are refused.)",
                               assetsData->assets[index].url, NULL, NULL, NULL, NULL, NULL);
        }

        return;
    }

    if(linked_list_size(items) == 0 && assetsData != NULL) {
        for(u32 i = 0; i < assetsData->count; i++) {
            linked_list_add(items, assetsData->items[i]);
        }
    }
}

typedef struct {
    char apiUrl[DOWNLOAD_URL_MAX];
    bool painted;
} remoteinstall_github_fetch_data;

static void remoteinstall_github_show_http_error(Result res) {
    if(res >= R_APP_HTTP_ERROR_BASE && res < R_APP_HTTP_ERROR_END) {
        u32 code = (u32) (res - R_APP_HTTP_ERROR_BASE);
        if(code == 403 || code == 429) {
            error_display_res(NULL, NULL, res, "GitHub rate limit or access denied.\nWait and try again later.");
            return;
        }

        if(code == 404) {
            error_display_res(NULL, NULL, res, "GitHub repository or release not found.");
            return;
        }
    }

    error_display_res(NULL, NULL, res, "Failed to fetch GitHub release.");
}

static void remoteinstall_github_fetch_update(ui_view* view, void* data, float* progress, char* text) {
    remoteinstall_github_fetch_data* fetchData = (remoteinstall_github_fetch_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        info_destroy(view);
        free(fetchData);
        return;
    }

    if(!fetchData->painted) {
        fetchData->painted = true;
        *progress = 0;
        snprintf(text, PROGRESS_TEXT_MAX, "Fetching latest release...\nB: Cancel");
        return;
    }

    snprintf(text, PROGRESS_TEXT_MAX, "Downloading release metadata...");

    Result res = 0;
    json_t* json = NULL;
    remoteinstall_github_assets_data* assetsData = NULL;

    if(R_SUCCEEDED(res = http_download_json(fetchData->apiUrl, &json, 256 * 1024))) {
        if(json_is_object(json)) {
            json_t* message = json_object_get(json, "message");
            if(json_is_string(message)) {
                /* GitHub error payload masquerading as 200 in rare cases. */
                res = R_APP_BAD_DATA;
            } else {
                json_t* tagName = json_object_get(json, "tag_name");
                json_t* assets = json_object_get(json, "assets");
                if(json_is_array(assets)) {
                    size_t total = json_array_size(assets);
                    assetsData = (remoteinstall_github_assets_data*) calloc(1, sizeof(remoteinstall_github_assets_data));
                    if(assetsData == NULL) {
                        res = R_APP_OUT_OF_MEMORY;
                    } else {
                        if(json_is_string(tagName)) {
                            snprintf(assetsData->listTitle, sizeof(assetsData->listTitle), "Release %s", json_string_value(tagName));
                        } else {
                            snprintf(assetsData->listTitle, sizeof(assetsData->listTitle), "GitHub Release Assets");
                        }

                        if(total > 0) {
                            assetsData->items = (list_item**) calloc(total, sizeof(list_item*));
                            assetsData->assets = (remoteinstall_github_asset*) calloc(total, sizeof(remoteinstall_github_asset));
                            if(assetsData->items == NULL || assetsData->assets == NULL) {
                                res = R_APP_OUT_OF_MEMORY;
                            }
                        }
                    }

                    if(R_SUCCEEDED(res) && assetsData != NULL && assetsData->items != NULL && assetsData->assets != NULL) {
                        for(size_t i = 0; i < total; i++) {
                            json_t* val = json_array_get(assets, i);
                            if(!json_is_object(val)) {
                                continue;
                            }

                            json_t* assetName = json_object_get(val, "name");
                            json_t* assetUrl = json_object_get(val, "browser_download_url");
                            json_t* assetSize = json_object_get(val, "size");

                            if(!json_is_string(assetName) || !json_is_string(assetUrl)) {
                                continue;
                            }

                            const char* name = json_string_value(assetName);
                            if(!remoteinstall_str_ends_with_ci(name, ".cia") && !remoteinstall_str_ends_with_ci(name, ".3dsx")) {
                                continue;
                            }

                            list_item* item = (list_item*) calloc(1, sizeof(list_item));
                            const char* srcUrl = json_string_value(assetUrl);
                            char* urlCopy = (char*) calloc(strlen(srcUrl) + 1, sizeof(char));
                            if(item == NULL || urlCopy == NULL) {
                                free(item);
                                free(urlCopy);
                                res = R_APP_OUT_OF_MEMORY;
                                break;
                            }

                            string_copy(urlCopy, srcUrl, strlen(srcUrl) + 1);

                            u64 size = json_is_integer(assetSize) ? (u64) json_integer_value(assetSize) : 0;
                            if(size > 0) {
                                snprintf(item->name, LIST_ITEM_NAME_MAX, "%s (%llu KiB)", name, (unsigned long long) (size / 1024));
                            } else {
                                snprintf(item->name, LIST_ITEM_NAME_MAX, "%s", name);
                            }

                            item->color = COLOR_TEXT;
                            item->data = NULL;

                            assetsData->items[assetsData->count] = item;
                            assetsData->assets[assetsData->count].url = urlCopy;
                            assetsData->assets[assetsData->count].size = size;
                            assetsData->count++;
                        }
                    }
                } else {
                    res = R_APP_BAD_DATA;
                }
            }
        } else {
            res = R_APP_BAD_DATA;
        }

        json_decref(json);
    }

    ui_pop();
    info_destroy(view);

    if(R_SUCCEEDED(res) && assetsData != NULL && assetsData->count > 0) {
        list_display(assetsData->listTitle, "A = install, B = back", assetsData, remoteinstall_github_assets_update, NULL);
    } else {
        if(assetsData != NULL) {
            remoteinstall_github_free_assets(assetsData);
        }

        if(R_FAILED(res)) {
            remoteinstall_github_show_http_error(res);
        } else {
            prompt_display_notify("Failure", "Latest release has no .cia/.3dsx assets.", COLOR_TEXT, NULL, NULL, NULL);
        }
    }

    free(fetchData);
}

static bool remoteinstall_github_name_ok(const char* s) {
    if(s == NULL || *s == '\0') {
        return false;
    }

    for(const char* p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char) *p;
        if(!(isalnum(c) || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }

    return true;
}

static void remoteinstall_github_onresponse(ui_view* view, void* data, SwkbdButton button, const char* response) {
    if(button != SWKBD_BUTTON_CONFIRM) {
        return;
    }

    char owner[64];
    char repo[64];
    if(!remoteinstall_parse_github_repo(response, owner, sizeof(owner), repo, sizeof(repo))
       || !remoteinstall_github_name_ok(owner)
       || !remoteinstall_github_name_ok(repo)) {
        prompt_display_notify("Failure", "Enter owner/repo (or a github.com URL).", COLOR_TEXT, NULL, NULL, NULL);
        return;
    }

    remoteinstall_github_fetch_data* fetchData = (remoteinstall_github_fetch_data*) calloc(1, sizeof(remoteinstall_github_fetch_data));
    if(fetchData == NULL) {
        error_display_res(NULL, NULL, R_APP_OUT_OF_MEMORY, "Failed to allocate GitHub fetch data.");
        return;
    }

    snprintf(fetchData->apiUrl, sizeof(fetchData->apiUrl), "https://api.github.com/repos/%s/%s/releases/latest", owner, repo);
    info_display("GitHub Release", "B: Cancel", true, fetchData, remoteinstall_github_fetch_update, NULL);
}

static void remoteinstall_github_release() {
    kbd_display("GitHub owner/repo", "", SWKBD_TYPE_NORMAL, 0, SWKBD_NOTEMPTY_NOTBLANK, 128, NULL, remoteinstall_github_onresponse);
}

static list_item scan_qr_code = {"Scan a QR code", COLOR_TEXT, remoteinstall_scan_qr_code};
static list_item manually_enter_urls = {"Type a download link", COLOR_TEXT, remoteinstall_manually_enter_urls};
static list_item github_release = {"Install from GitHub", COLOR_TEXT, remoteinstall_github_release};
static list_item repeat_last_request = {"Try the last link again", COLOR_TEXT, remoteinstall_repeat_last_request};
static list_item forget_last_request = {"Forget the last link", COLOR_TEXT, remoteinstall_forget_last_request};

static void remoteinstall_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && (selectedTouched || hidKeysDown() & KEY_A) && selected->data != NULL) {
        ((void(*)()) selected->data)();
        return;
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &scan_qr_code);
        linked_list_add(items, &manually_enter_urls);
        linked_list_add(items, &github_release);
        linked_list_add(items, &repeat_last_request);
        linked_list_add(items, &forget_last_request);
    }
}

void remoteinstall_open() {
    list_display("Install from the internet", "A = choose, B = back", NULL, remoteinstall_update, NULL);
}
