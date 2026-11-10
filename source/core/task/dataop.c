#include <malloc.h>
#include <string.h>

#include <3ds.h>
#include <jansson.h>

#include "dataop.h"
#include "../core.h"

static Result task_data_op_check_running(data_op_data* data) {
    Result res = 0;

    if(task_is_quit_all() || svcWaitSynchronization(data->cancelEvent, 0) == 0) {
        res = R_APP_CANCELLED;
    } else {
        bool suspended = svcWaitSynchronization(task_get_suspend_event(), 0) != 0;
        if(suspended) {
            if(data->suspend != NULL && R_SUCCEEDED(res)) {
                res = data->suspend(data->data, data->processed);
            }
        }

        svcWaitSynchronization(task_get_pause_event(), U64_MAX);

        if(suspended) {
            if(data->restore != NULL && R_SUCCEEDED(res)) {
                res = data->restore(data->data, data->processed);
            }
        }
    }

    return res;
}


typedef struct {
    data_op_data* data;
    u32 srcHandle;
    void* buffer;
    u64 offset;
    u32 size;
    u32 bytesRead;
    Result res;
    Handle doneEvent;
} data_op_prefetch_t;

/* Prefetch the next SD read on New 3DS core 2 while the main copy thread
   writes the current buffer to AM. Different FS handles (SD vs AM) make
   the overlap safe. */
static void task_data_op_prefetch_thread(void* arg) {
    data_op_prefetch_t* p = (data_op_prefetch_t*) arg;

    p->bytesRead = 0;
    p->res = p->data->readSrc(p->data->data, p->srcHandle, &p->bytesRead, p->buffer, p->offset, p->size);

    svcSignalEvent(p->doneEvent);
}

static Result task_data_op_copy(data_op_data* data, u32 index) {
    data->currProcessed = 0;
    data->currTotal = 0;

    data->bytesPerSecond = 0;
    data->estimatedRemainingSeconds = 0;

    Result res = 0;

    bool isDir = false;
    if(R_SUCCEEDED(res = data->isSrcDirectory(data->data, index, &isDir)) && isDir) {
        res = data->makeDstDirectory(data->data, index);
    } else {
        u32 srcHandle = 0;
        if(R_SUCCEEDED(res = data->openSrc(data->data, index, &srcHandle))) {
            if(R_SUCCEEDED(res = data->getSrcSize(data->data, srcHandle, &data->currTotal))) {
                if(data->currTotal == 0) {
                    if(data->copyEmpty) {
                        u32 dstHandle = 0;
                        if(R_SUCCEEDED(res = data->openDst(data->data, index, NULL, data->currTotal, &dstHandle))) {
                            res = data->closeDst(data->data, index, true, dstHandle);
                        }
                    } else {
                        res = R_APP_BAD_DATA;
                    }
                } else {
                    /* Double-buffered copy: overlap SD reads (core 2) with AM writes. */
                    u8* buffers[2] = {NULL, NULL};
                    buffers[0] = (u8*) calloc(1, data->bufferSize);
                    buffers[1] = (u8*) calloc(1, data->bufferSize);

                    if(buffers[0] != NULL && buffers[1] != NULL) {
                        u32 dstHandle = 0;

                        u64 ioStartTime = 0;
                        u64 lastBytesPerSecondUpdate = osGetTime();
                        u32 bytesSinceUpdate = 0;

                        data_op_prefetch_t prefetch;
                        memset(&prefetch, 0, sizeof(prefetch));
                        prefetch.data = data;
                        prefetch.srcHandle = srcHandle;

                        Thread prefetchThread = NULL;
                        bool prefetchActive = false;

                        u32 bytesRead = 0;
                        int cur = 0;

                        if(R_SUCCEEDED(res = data->readSrc(data->data, srcHandle, &bytesRead, buffers[0], 0, data->bufferSize))) {
                            if(R_SUCCEEDED(res = data->openDst(data->data, index, buffers[0], data->currTotal, &dstHandle))) {
                                while(data->currProcessed < data->currTotal) {
                                    if(R_FAILED(res = task_data_op_check_running(data))) {
                                        break;
                                    }

                                    if(bytesRead == 0) {
                                        res = R_APP_BAD_DATA;
                                        break;
                                    }

                                    u64 nextOffset = data->currProcessed + bytesRead;
                                    bool needPrefetch = nextOffset < data->currTotal;

                                    if(needPrefetch) {
                                        if(R_SUCCEEDED(svcCreateEvent(&prefetch.doneEvent, RESET_STICKY))) {
                                            prefetch.buffer = buffers[1 - cur];
                                            prefetch.offset = nextOffset;
                                            prefetch.size = data->bufferSize;
                                            prefetch.bytesRead = 0;
                                            prefetch.res = 0;

                                            /* Affinity 2 = New 3DS syscore; falls back sync if create fails. */
                                            prefetchThread = threadCreate(task_data_op_prefetch_thread, &prefetch, 0x4000, 0x18, 2, false);
                                            if(prefetchThread != NULL) {
                                                prefetchActive = true;
                                            } else {
                                                svcCloseHandle(prefetch.doneEvent);
                                                prefetch.doneEvent = 0;

                                                if(R_FAILED(res = data->readSrc(data->data, srcHandle, &prefetch.bytesRead,
                                                                                buffers[1 - cur], nextOffset, data->bufferSize))) {
                                                    break;
                                                }
                                            }
                                        } else {
                                            if(R_FAILED(res = data->readSrc(data->data, srcHandle, &prefetch.bytesRead,
                                                                            buffers[1 - cur], nextOffset, data->bufferSize))) {
                                                break;
                                            }
                                        }
                                    }

                                    u32 bytesWritten = 0;
                                    if(R_FAILED(res = data->writeDst(data->data, dstHandle, &bytesWritten,
                                                                     buffers[cur], data->currProcessed, bytesRead))) {
                                        break;
                                    }

                                    /* Prefetch offset assumed a full write; refuse short writes. */
                                    if(bytesWritten != bytesRead) {
                                        res = R_APP_BAD_DATA;
                                        break;
                                    }

                                    data->currProcessed += bytesWritten;
                                    bytesSinceUpdate += bytesWritten;

                                    u64 time = osGetTime();
                                    u64 elapsed = time - lastBytesPerSecondUpdate;
                                    if(elapsed >= 1000) {
                                        data->bytesPerSecond = (u32) (bytesSinceUpdate / (elapsed / 1000.0f));

                                        if(ioStartTime != 0) {
                                            data->estimatedRemainingSeconds = (u32) ((data->currTotal - data->currProcessed) / (data->currProcessed / ((time - ioStartTime) / 1000.0f)));
                                        } else {
                                            data->estimatedRemainingSeconds = 0;
                                        }

                                        if(ioStartTime == 0 && data->currProcessed > 0) {
                                            ioStartTime = time;
                                        }

                                        bytesSinceUpdate = 0;
                                        lastBytesPerSecondUpdate = time;
                                    }

                                    if(needPrefetch) {
                                        if(prefetchActive) {
                                            svcWaitSynchronization(prefetch.doneEvent, U64_MAX);
                                            threadJoin(prefetchThread, U64_MAX);
                                            threadFree(prefetchThread);
                                            prefetchThread = NULL;
                                            prefetchActive = false;
                                            svcCloseHandle(prefetch.doneEvent);
                                            prefetch.doneEvent = 0;

                                            if(R_FAILED(prefetch.res)) {
                                                res = prefetch.res;
                                                break;
                                            }
                                        }

                                        bytesRead = prefetch.bytesRead;
                                        cur = 1 - cur;
                                    } else {
                                        bytesRead = 0;
                                    }
                                }
                            }
                        }

                        if(prefetchActive && prefetchThread != NULL) {
                            svcWaitSynchronization(prefetch.doneEvent, U64_MAX);
                            threadJoin(prefetchThread, U64_MAX);
                            threadFree(prefetchThread);
                            svcCloseHandle(prefetch.doneEvent);
                            prefetchActive = false;
                        }

                        if(dstHandle != 0) {
                            Result closeDstRes = data->closeDst(data->data, index, res == 0, dstHandle);
                            if(R_SUCCEEDED(res)) {
                                res = closeDstRes;
                            }
                        }
                    } else {
                        res = R_APP_OUT_OF_MEMORY;
                    }

                    free(buffers[0]);
                    free(buffers[1]);
                }
            }

            Result closeSrcRes = data->closeSrc(data->data, index, res == 0, srcHandle);
            if(R_SUCCEEDED(res)) {
                res = closeSrcRes;
            }
        }
    }

    return res;
}

typedef struct {
    data_op_data* data;

    u32 index;

    u32 dstHandle;
    bool firstRun;
    u64 ioStartTime;
    u64 lastBytesPerSecondUpdate;
    u32 bytesSinceUpdate;

    u64 writeOffset;
} data_op_download_data;

static Result task_data_op_download_callback(void* userData, void* buffer, size_t size) {
    data_op_download_data* downloadData = (data_op_download_data*) userData;
    data_op_data* data = downloadData->data;

    if(downloadData->firstRun) {
        downloadData->firstRun = false;

        Result res = data->openDst(data->data, downloadData->index, buffer, data->currTotal, &downloadData->dstHandle);
        if(R_FAILED(res)) {
            return res;
        }
    }

    u32 bytesWritten = 0;
    Result res = data->writeDst(data->data, downloadData->dstHandle, &bytesWritten, buffer, downloadData->writeOffset, size);
    downloadData->writeOffset += bytesWritten;

    return res;
}

static Result task_data_op_download_check_running(void* userData) {
    data_op_download_data* downloadData = (data_op_download_data*) userData;

    return task_data_op_check_running(downloadData->data);
}

static Result task_data_op_download_progress(void* userData, u64 total, u64 curr) {
    data_op_download_data* downloadData = (data_op_download_data*) userData;
    data_op_data* data = downloadData->data;

    downloadData->bytesSinceUpdate += curr - data->currProcessed;

    data->currTotal = total;
    data->currProcessed = curr;

    u64 time = osGetTime();
    u64 elapsed = time - downloadData->lastBytesPerSecondUpdate;
    if(elapsed >= 1000) {
        data->bytesPerSecond = (u32) (downloadData->bytesSinceUpdate / (elapsed / 1000.0f));

        if(downloadData->ioStartTime != 0) {
            data->estimatedRemainingSeconds = (u32) ((data->currTotal - data->currProcessed) / (data->currProcessed / ((time - downloadData->ioStartTime) / 1000.0f)));
        } else {
            data->estimatedRemainingSeconds = 0;
        }

        if(downloadData->ioStartTime == 0 && data->currProcessed > 0) {
            downloadData->ioStartTime = time;
        }

        downloadData->bytesSinceUpdate = 0;
        downloadData->lastBytesPerSecondUpdate = time;
    }

    return 0;
}

static Result task_data_op_download(data_op_data* data, u32 index) {
    data->currProcessed = 0;
    data->currTotal = 0;

    data->bytesPerSecond = 0;
    data->estimatedRemainingSeconds = 0;

    Result res = 0;

    char url[DOWNLOAD_URL_MAX];
    if(R_SUCCEEDED(res = data->getSrcUrl(data->data, index, url, DOWNLOAD_URL_MAX))) {
        data_op_download_data downloadData = {data, index, 0, true, 0, osGetTime(), 0, 0};
        res = http_download_callback(url, data->bufferSize, &downloadData, task_data_op_download_callback, task_data_op_download_check_running, task_data_op_download_progress);

        if(downloadData.dstHandle != 0) {
            Result closeDstRes = data->closeDst(data->data, index, res == 0, downloadData.dstHandle);
            if(R_SUCCEEDED(res)) {
                res = closeDstRes;
            }
        }
    }

    return res;
}

static Result task_data_op_delete(data_op_data* data, u32 index) {
    return data->delete(data->data, index);
}

static void task_data_op_retry_onresponse(ui_view* view, void* data, u32 response) {
    ((data_op_data*) data)->retryResponse = response == PROMPT_YES;
}

static void task_data_op_thread(void* arg) {
    data_op_data* data = (data_op_data*) arg;

    for(data->processed = 0; data->processed < data->total; data->processed++) {
        Result res = 0;

        if(R_SUCCEEDED(res = task_data_op_check_running(data))) {
            switch(data->op) {
                case DATAOP_COPY:
                    res = task_data_op_copy(data, data->processed);
                    break;
                case DATAOP_DOWNLOAD:
                    res = task_data_op_download(data, data->processed);
                    break;
                case DATAOP_DELETE:
                    res = task_data_op_delete(data, data->processed);
                    break;
                default:
                    break;
            }
        }

        data->result = res;

        if(R_FAILED(res)) {
            if(res == R_APP_CANCELLED) {
                prompt_display_notify("Stopped", "Cancelled.\nAnything not finished was not changed.", COLOR_TEXT, NULL, NULL, NULL);
                break;
            } else if(res != R_APP_SKIPPED) {
                ui_view* errorView = NULL;
                bool proceed = true;
                if(data->error != NULL) {
                    proceed = data->error(data->data, data->processed, res, &errorView);
                }

                if(errorView != NULL) {
                    svcWaitSynchronization(errorView->active, U64_MAX);
                }

                if(data->continueOnError) {
                    if(!proceed) {
                        break;
                    }
                } else {
                    ui_view* retryView = prompt_display_yes_no("Try again?", "That did not work.\nDo you want to try again?", COLOR_TEXT, data, NULL, task_data_op_retry_onresponse);
                    if(retryView != NULL) {
                        svcWaitSynchronization(retryView->active, U64_MAX);

                        if(data->retryResponse) {
                            if(proceed) {
                                data->processed--;
                            } else {
                                /* Start over from the first item (loop ++ brings this to 0). */
                                data->processed = (u32) -1;
                            }
                        } else if(!proceed) {
                            break;
                        }
                    }
                }
            }
        }
    }

    svcCloseHandle(data->cancelEvent);

    data->finished = true;

    aptSetSleepAllowed(true);
}

Result task_data_op(data_op_data* data) {
    if(data == NULL) {
        return R_APP_INVALID_ARGUMENT;
    }

    data->processed = 0;

    data->currProcessed = 0;
    data->currTotal = 0;

    data->finished = false;
    data->result = 0;
    data->cancelEvent = 0;

    Result res = 0;
    if(R_SUCCEEDED(res = svcCreateEvent(&data->cancelEvent, RESET_STICKY))) {
        if(threadCreate(task_data_op_thread, data, 0x10000, 0x18, 1, true) == NULL) {
            res = R_APP_THREAD_CREATE_FAILED;
        }
    }

    if(R_FAILED(res)) {
        data->finished = true;

        if(data->cancelEvent != 0) {
            svcCloseHandle(data->cancelEvent);
            data->cancelEvent = 0;
        }
    }

    aptSetSleepAllowed(false);

    return res;
}