#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <3ds.h>

#include "jukebox.h"
#include "resources.h"
#include "../core/core.h"
#include "../libs/minimp3/minimp3.h"

#define JB_MUSIC_DIR "sdmc:/music"
#define JB_MAX_SONGS 256
#define JB_PATH_MAX 512
#define JB_NAME_MAX 128
#define JB_WAVEBUFS 2
#define JB_BUF_MS 120
#define JB_THREAD_STACK (64 * 1024)
#define JB_READ_CHUNK (16 * 1024)

typedef struct {
    char path[JB_PATH_MAX];
    char name[JB_NAME_MAX];
} jb_song;

typedef struct {
    jb_song songs[JB_MAX_SONGS];
    int songCount;
    int current;
    bool shuffle;
    bool paused;
    bool ndspOk;
    bool running;
    bool quitThread;
    bool songDone;
    bool openFailed;
    char status[192];
    char dspMsg[192];

    Thread thread;
    LightEvent event;
    LightLock lock;

    FILE* file;
    mp3dec_t decoder;
    u8* mp3buf;
    size_t mp3bufSize;
    size_t mp3len;
    size_t mp3off;

    int channels;
    int sampleRate;
    ndspWaveBuf waveBuf[JB_WAVEBUFS];
    s16* audioBuffer;
    size_t waveBufSamples; /* frames per buffer */
    size_t waveBufBytes;
} jukebox_data;

static bool jb_ends_with_mp3(const char* name) {
    size_t n = strlen(name);
    if(n < 4) return false;
    return strcasecmp(name + n - 4, ".mp3") == 0;
}

static void jb_scan(jukebox_data* d) {
    d->songCount = 0;
    DIR* dir = opendir(JB_MUSIC_DIR);
    if(dir == NULL) {
        return;
    }
    struct dirent* ent;
    while((ent = readdir(dir)) != NULL && d->songCount < JB_MAX_SONGS) {
        if(ent->d_name[0] == '.') continue;
        if(!jb_ends_with_mp3(ent->d_name)) continue;
        snprintf(d->songs[d->songCount].path, JB_PATH_MAX, "%s/%s", JB_MUSIC_DIR, ent->d_name);
        snprintf(d->songs[d->songCount].name, JB_NAME_MAX, "%s", ent->d_name);
        d->songCount++;
    }
    closedir(dir);
}

static void jb_close_file(jukebox_data* d) {
    if(d->file != NULL) {
        fclose(d->file);
        d->file = NULL;
    }
    d->mp3len = 0;
    d->mp3off = 0;
}


static void jb_skip_id3(jukebox_data* d) {
    if(d->file == NULL) return;
    unsigned char hdr[10];
    long pos = ftell(d->file);
    if(fread(hdr, 1, 10, d->file) != 10) {
        fseek(d->file, pos, SEEK_SET);
        return;
    }
    if(hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
        size_t size = ((hdr[6] & 0x7f) << 21) | ((hdr[7] & 0x7f) << 14) |
                      ((hdr[8] & 0x7f) << 7) | (hdr[9] & 0x7f);
        fseek(d->file, (long)(10 + size), SEEK_SET);
    } else {
        fseek(d->file, pos, SEEK_SET);
    }
}

static bool jb_open_song(jukebox_data* d, int index) {
    jb_close_file(d);
    if(index < 0 || index >= d->songCount) {
        d->openFailed = true;
        return false;
    }
    d->current = index;
    d->file = fopen(d->songs[index].path, "rb");
    if(d->file == NULL) {
        d->openFailed = true;
        snprintf(d->status, sizeof(d->status), "Could not open that file.");
        return false;
    }
    jb_skip_id3(d);
    mp3dec_init(&d->decoder);
    d->mp3off = 0;
    d->mp3len = 0;
    d->channels = 0;
    d->sampleRate = 0;
    d->songDone = false;
    d->openFailed = false;
    d->paused = false;
    if(d->ndspOk) {
        ndspChnSetPaused(0, false);
    }
    snprintf(d->status, sizeof(d->status), "Playing");
    return true;
}

static bool jb_refill_mp3(jukebox_data* d) {
    if(d->file == NULL) return false;
    if(d->mp3off > 0 && d->mp3len > d->mp3off) {
        memmove(d->mp3buf, d->mp3buf + d->mp3off, d->mp3len - d->mp3off);
        d->mp3len -= d->mp3off;
        d->mp3off = 0;
    } else if(d->mp3off >= d->mp3len) {
        d->mp3len = 0;
        d->mp3off = 0;
    }
    if(d->mp3len < d->mp3bufSize) {
        size_t got = fread(d->mp3buf + d->mp3len, 1, d->mp3bufSize - d->mp3len, d->file);
        d->mp3len += got;
    }
    return d->mp3len > d->mp3off;
}


static bool jb_setup_ndsp_format(jukebox_data* d, int channels, int rate);

static bool jb_probe_format(jukebox_data* d) {
    if(d->file == NULL) return jb_setup_ndsp_format(d, 2, 44100);
    size_t savedOff = d->mp3off;
    size_t savedLen = d->mp3len;
    long fpos = ftell(d->file);
    u8* saved = NULL;
    if(savedLen > 0) {
        saved = (u8*) malloc(savedLen);
        if(saved) memcpy(saved, d->mp3buf, savedLen);
    }
    d->mp3off = 0;
    d->mp3len = 0;
    jb_refill_mp3(d);
    mp3dec_t tmp = d->decoder;
    mp3dec_init(&tmp);
    bool ok = false;
    for(int tries = 0; tries < 64 && !ok; tries++) {
        if(d->mp3len <= d->mp3off && !jb_refill_mp3(d)) break;
        mp3dec_frame_info_t info;
        s16 pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int samples = mp3dec_decode_frame(&tmp, d->mp3buf + d->mp3off, (int)(d->mp3len - d->mp3off), pcm, &info);
        if(info.frame_bytes > 0) d->mp3off += (size_t) info.frame_bytes;
        else { if(d->mp3len > d->mp3off) d->mp3off++; continue; }
        if(samples > 0 && info.hz > 0) {
            ok = jb_setup_ndsp_format(d, info.channels, info.hz);
            break;
        }
    }
    if(!ok) ok = jb_setup_ndsp_format(d, 2, 44100);
    /* rewind */
    fseek(d->file, 0, SEEK_SET);
    jb_skip_id3(d);
    mp3dec_init(&d->decoder);
    d->mp3off = 0;
    d->mp3len = 0;
    if(saved) free(saved);
    (void) savedOff; (void) savedLen; (void) fpos;
    return ok;
}

static bool jb_setup_ndsp_format(jukebox_data* d, int channels, int rate) {
    if(channels < 1) channels = 1;
    if(channels > 2) channels = 2;
    if(rate < 8000) rate = 44100;

    bool changed = (d->channels != channels) || (d->sampleRate != rate) || (d->audioBuffer == NULL);
    d->channels = channels;
    d->sampleRate = rate;

    if(!changed) return true;

    if(d->audioBuffer != NULL) {
        ndspChnWaveBufClear(0);
        linearFree(d->audioBuffer);
        d->audioBuffer = NULL;
    }

    d->waveBufSamples = (size_t) rate * JB_BUF_MS / 1000;
    if(d->waveBufSamples < 512) d->waveBufSamples = 512;
    d->waveBufBytes = d->waveBufSamples * (size_t) channels * sizeof(s16);
    size_t total = d->waveBufBytes * JB_WAVEBUFS;
    d->audioBuffer = (s16*) linearAlloc(total);
    if(d->audioBuffer == NULL) {
        snprintf(d->status, sizeof(d->status), "Not enough memory for audio.");
        return false;
    }
    memset(d->audioBuffer, 0, total);
    memset(d->waveBuf, 0, sizeof(d->waveBuf));

    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, (float) rate);
    ndspChnSetFormat(0, channels == 1 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {0};
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(0, mix);

    s16* ptr = d->audioBuffer;
    for(int i = 0; i < JB_WAVEBUFS; i++) {
        d->waveBuf[i].data_vaddr = ptr;
        d->waveBuf[i].nsamples = (u32) d->waveBufSamples;
        d->waveBuf[i].status = NDSP_WBUF_DONE;
        ptr += d->waveBufSamples * channels;
    }
    return true;
}

static bool jb_fill_wavebuf(jukebox_data* d, ndspWaveBuf* wb) {
    if(d->file == NULL) return false;

    size_t framesNeeded = d->waveBufSamples;
    size_t framesFilled = 0;
    s16* out = wb->data_pcm16;
    int ch = d->channels > 0 ? d->channels : 2;

    while(framesFilled < framesNeeded) {
        if(!jb_refill_mp3(d)) {
            break;
        }
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        s16 pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int samples = mp3dec_decode_frame(&d->decoder,
                                          d->mp3buf + d->mp3off,
                                          (int) (d->mp3len - d->mp3off),
                                          pcm, &info);
        if(info.frame_bytes > 0) {
            d->mp3off += (size_t) info.frame_bytes;
        } else if(samples == 0) {
            /* need more data */
            if(d->mp3len - d->mp3off < 16) {
                break;
            }
            d->mp3off++;
            continue;
        }

        if(samples <= 0) {
            continue;
        }

        if(d->channels == 0 || d->sampleRate == 0) {
            if(!jb_setup_ndsp_format(d, info.channels > 0 ? info.channels : 2, info.hz > 0 ? info.hz : 44100)) {
                return false;
            }
            ch = d->channels;
            out = wb->data_pcm16;
            framesNeeded = d->waveBufSamples;
            /* fall through using refreshed buffer pointers for this same wb index if possible */
            if(wb->data_pcm16 != out) {
                /* find matching wavebuf after realloc */
                for(int wi = 0; wi < JB_WAVEBUFS; wi++) {
                    if(&d->waveBuf[wi] == wb) {
                        wb = &d->waveBuf[wi];
                        out = wb->data_pcm16;
                        break;
                    }
                }
            }
        }

        int srcCh = info.channels;
        for(int i = 0; i < samples && framesFilled < framesNeeded; i++) {
            if(ch == 1) {
                if(srcCh >= 2) {
                    out[framesFilled] = (s16) (((int) pcm[i * 2] + (int) pcm[i * 2 + 1]) / 2);
                } else {
                    out[framesFilled] = pcm[i];
                }
            } else {
                if(srcCh >= 2) {
                    out[framesFilled * 2] = pcm[i * 2];
                    out[framesFilled * 2 + 1] = pcm[i * 2 + 1];
                } else {
                    out[framesFilled * 2] = pcm[i];
                    out[framesFilled * 2 + 1] = pcm[i];
                }
            }
            framesFilled++;
        }
    }

    if(framesFilled == 0) {
        return false;
    }

    /* pad remainder with silence so buffer length stays consistent */
    if(framesFilled < framesNeeded) {
        size_t rem = (framesNeeded - framesFilled) * (size_t) ch;
        memset(out + framesFilled * ch, 0, rem * sizeof(s16));
    }

    wb->nsamples = (u32) framesNeeded;
    DSP_FlushDataCache(wb->data_pcm16, d->waveBufBytes);
    ndspChnWaveBufAdd(0, wb);
    return true;
}

static void jb_ndsp_callback(void* arg) {
    jukebox_data* d = (jukebox_data*) arg;
    if(d == NULL || d->quitThread) return;
    LightEvent_Signal(&d->event);
}

static int jb_next_index(jukebox_data* d, int dir) {
    if(d->songCount <= 0) return 0;
    if(d->shuffle && d->songCount > 1) {
        int n = d->current;
        for(int tries = 0; tries < 8 && n == d->current; tries++) {
            n = rand() % d->songCount;
        }
        if(n == d->current) {
            n = (d->current + 1) % d->songCount;
        }
        return n;
    }
    int n = d->current + dir;
    if(n < 0) n = d->songCount - 1;
    if(n >= d->songCount) n = 0;
    return n;
}

static void jb_audio_thread(void* arg) {
    jukebox_data* d = (jukebox_data*) arg;
    while(!d->quitThread) {
        LightLock_Lock(&d->lock);
        bool paused = d->paused;
        bool haveFile = d->file != NULL;
        LightLock_Unlock(&d->lock);

        if(!paused && haveFile && d->ndspOk) {
            for(int i = 0; i < JB_WAVEBUFS; i++) {
                if(d->waveBuf[i].status != NDSP_WBUF_DONE) continue;
                LightLock_Lock(&d->lock);
                bool ok = jb_fill_wavebuf(d, &d->waveBuf[i]);
                if(!ok) {
                    d->songDone = true;
                }
                LightLock_Unlock(&d->lock);
                if(!ok) break;
            }
        }
        LightEvent_Wait(&d->event);
    }
}

static void jb_stop_thread(jukebox_data* d) {
    if(d->thread == NULL) return;
    d->quitThread = true;
    LightEvent_Signal(&d->event);
    threadJoin(d->thread, U64_MAX);
    threadFree(d->thread);
    d->thread = NULL;
}

static bool jb_start_thread(jukebox_data* d) {
    d->quitThread = false;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio -= 1;
    if(prio < 0x18) prio = 0x18;
    if(prio > 0x3F) prio = 0x3F;
    d->thread = threadCreate(jb_audio_thread, d, JB_THREAD_STACK, prio, -1, false);
    return d->thread != NULL;
}

static void jb_cleanup_audio(jukebox_data* d) {
    jb_stop_thread(d);
    if(d->ndspOk) {
        ndspSetCallback(NULL, NULL);
        ndspChnWaveBufClear(0);
        ndspChnReset(0);
        ndspExit();
        d->ndspOk = false;
    }
    if(d->audioBuffer != NULL) {
        linearFree(d->audioBuffer);
        d->audioBuffer = NULL;
    }
    jb_close_file(d);
    if(d->mp3buf != NULL) {
        free(d->mp3buf);
        d->mp3buf = NULL;
    }
}

static void jb_change_song(jukebox_data* d, int dir) {
    if(d->songCount <= 0 || !d->ndspOk) return;
    LightLock_Lock(&d->lock);
    ndspChnWaveBufClear(0);
    for(int i = 0; i < JB_WAVEBUFS; i++) {
        d->waveBuf[i].status = NDSP_WBUF_DONE;
    }
    int next = jb_next_index(d, dir);
    if(jb_open_song(d, next)) {
        jb_probe_format(d);
    }
    LightLock_Unlock(&d->lock);
    LightEvent_Signal(&d->event);
}

static void jb_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    jukebox_data* d = (jukebox_data*) data;
    const char* tip;
    if(!d->ndspOk) {
        tip = d->dspMsg;
    } else if(d->songCount == 0) {
        tip = "Put .mp3 files in the music folder on your SD card, then open this screen again.";
    } else {
        tip = "Play songs from the music folder on your SD card. Nothing is installed or deleted.";
    }
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.5f, 0.5f, x2 - x1 - 28.0f);
    float y = y1 + 16.0f;
    screen_draw_rect(x1 + 8.0f, y - 8.0f, (x2 - x1) - 16.0f, h + 16.0f, COLOR_PANEL, true);
    screen_draw_string_wrap(tip, x1 + 14.0f, y, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 14.0f);
}

static void jb_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    jukebox_data* d = (jukebox_data*) data;
    const float pad = 12.0f;
    float y = y1 + pad;
    float w, h;
    char line[256];

    const char* title = "Jukebox";
    screen_get_string_size(&w, &h, title, 0.65f, 0.65f);
    screen_draw_string(title, x1 + (x2 - x1 - w) / 2, y, 0.65f, 0.65f, COLOR_ACCENT, true);
    y += h + 12.0f;

    if(!d->ndspOk) {
        screen_get_string_size_wrap(&w, &h, d->dspMsg, 0.5f, 0.5f, x2 - x1 - pad * 2);
        screen_draw_string_wrap(d->dspMsg, x1 + pad, y, 0.5f, 0.5f, COLOR_WARNING, true, x2 - pad);
        return;
    }

    if(d->songCount == 0) {
        const char* empty =
            "No songs found.\n\n"
            "Create a folder named music on the\n"
            "root of your SD card and put .mp3\n"
            "files in it. Then come back here.";
        screen_get_string_size_wrap(&w, &h, empty, 0.5f, 0.5f, x2 - x1 - pad * 2);
        screen_draw_string_wrap(empty, x1 + pad, y, 0.5f, 0.5f, COLOR_TEXT, true, x2 - pad);
        return;
    }

    const char* name = d->songs[d->current].name;
    screen_get_string_size_wrap(&w, &h, name, 0.55f, 0.55f, x2 - x1 - pad * 2);
    screen_draw_string_wrap(name, x1 + pad, y, 0.55f, 0.55f, COLOR_TEXT, true, x2 - pad);
    y += h + 10.0f;

    snprintf(line, sizeof(line), "%d / %d", d->current + 1, d->songCount);
    screen_get_string_size(&w, &h, line, 0.45f, 0.45f);
    screen_draw_string(line, x1 + (x2 - x1 - w) / 2, y, 0.45f, 0.45f, COLOR_HINT, true);
    y += h + 10.0f;

    snprintf(line, sizeof(line), "%s%s",
             d->paused ? "Paused" : d->status,
             d->shuffle ? "  ·  Shuffle on" : "");
    screen_get_string_size(&w, &h, line, 0.5f, 0.5f);
    screen_draw_string(line, x1 + (x2 - x1 - w) / 2, y, 0.5f, 0.5f, COLOR_ACCENT, true);
    y += h + 14.0f;

    if(d->sampleRate > 0) {
        snprintf(line, sizeof(line), "%d Hz  ·  %s", d->sampleRate, d->channels == 1 ? "Mono" : "Stereo");
        screen_get_string_size(&w, &h, line, 0.4f, 0.4f);
        screen_draw_string(line, x1 + (x2 - x1 - w) / 2, y, 0.4f, 0.4f, COLOR_HINT, true);
    }
}

static void jb_update(ui_view* view, void* data, float* progress, char* text) {
    (void) progress;
    jukebox_data* d = (jukebox_data*) data;
    if(text != NULL) text[0] = '\0';

    if(d->songDone && d->ndspOk && d->songCount > 0 && !d->paused) {
        d->songDone = false;
        jb_change_song(d, +1);
    }

    u32 down = hidKeysDown();
    if(down & KEY_B) {
        jb_cleanup_audio(d);
        ui_pop();
        info_destroy(view);
        free(d);
        return;
    }

    if(!d->ndspOk || d->songCount == 0) {
        return;
    }

    if(down & KEY_A) {
        d->paused = !d->paused;
        ndspChnSetPaused(0, d->paused);
        snprintf(d->status, sizeof(d->status), d->paused ? "Paused" : "Playing");
        if(!d->paused) LightEvent_Signal(&d->event);
    }
    if(down & (KEY_RIGHT | KEY_R)) {
        jb_change_song(d, +1);
    }
    if(down & (KEY_LEFT | KEY_L)) {
        jb_change_song(d, -1);
    }
    if(down & KEY_X) {
        d->shuffle = !d->shuffle;
    }
}

void jukebox_open(void) {
    jukebox_data* d = (jukebox_data*) calloc(1, sizeof(jukebox_data));
    if(d == NULL) {
        error_display(NULL, NULL, "Failed to allocate screen data.");
        return;
    }

    LightEvent_Init(&d->event, RESET_ONESHOT);
    LightLock_Init(&d->lock);
    srand((unsigned) time(NULL) ^ (unsigned) osGetTime());

    jb_scan(d);

    Result ndspRes = ndspInit();
    if(R_FAILED(ndspRes)) {
        d->ndspOk = false;
        snprintf(d->dspMsg, sizeof(d->dspMsg),
                 "Sound could not start (DSP firmware missing).\n\n"
                 "Dump DSP firmware with the DSP1 app, then try again.\n\n"
                 "Nothing else is wrong — press B to go back.");
    } else {
        d->ndspOk = true;
        ndspSetCallback(jb_ndsp_callback, d);
        d->mp3bufSize = JB_READ_CHUNK * 4;
        d->mp3buf = (u8*) malloc(d->mp3bufSize);
        if(d->mp3buf == NULL) {
            snprintf(d->status, sizeof(d->status), "Not enough memory.");
        } else if(d->songCount > 0) {
            if(jb_open_song(d, 0)) {
                LightLock_Lock(&d->lock);
                jb_probe_format(d);
                LightLock_Unlock(&d->lock);
                jb_start_thread(d);
                LightEvent_Signal(&d->event);
            }
        }
    }

    const char* hints;
    if(!d->ndspOk || d->songCount == 0) {
        hints = "B = back";
    } else {
        hints = "A = play/pause, L/R = prev/next, X = shuffle, B = back";
    }

    ui_view* view = info_display("Jukebox", hints, false, d, jb_update, jb_draw_top);
    if(view == NULL) {
        jb_cleanup_audio(d);
        free(d);
        return;
    }
    view->drawBottom = jb_draw_bottom;
}
