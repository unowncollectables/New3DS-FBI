#include <malloc.h>
#include <stdio.h>

#include <3ds.h>

#include "error.h"
#include "info.h"
#include "ui.h"
#include "../screen.h"
#include "../../fbi/resources.h"

#define UI_TEXT_SCALE 0.55f
#define UI_PAD 12.0f
#define PROGRESS_BAR_W 280.0f
#define PROGRESS_BAR_H 22.0f
#define PROGRESS_LERP 0.18f

typedef struct {
    bool bar;
    void* data;
    float progress;
    float displayProgress;
    char text[PROGRESS_TEXT_MAX];
    void (*update)(ui_view* view, void* data, float* progress, char* text);
    void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2);
} info_data;

static void info_update(ui_view* view, void* data, float bx1, float by1, float bx2, float by2) {
    info_data* infoData = (info_data*) data;

    if(infoData->update != NULL) {
        infoData->update(view, infoData->data, &infoData->progress, infoData->text);
    }

    /* Smooth toward target for 60fps-friendly bar motion. */
    float delta = infoData->progress - infoData->displayProgress;
    if(delta > -0.0005f && delta < 0.0005f) {
        infoData->displayProgress = infoData->progress;
    } else {
        infoData->displayProgress += delta * PROGRESS_LERP;
    }
}

static void info_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    info_data* infoData = (info_data*) data;

    if(infoData->drawTop != NULL) {
        infoData->drawTop(view, infoData->data, x1, y1, x2, y2);
    }
}

static void info_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    info_data* infoData = (info_data*) data;

    float textWidth;
    float textHeight;
    screen_get_string_size_wrap(&textWidth, &textHeight, infoData->text, UI_TEXT_SCALE, UI_TEXT_SCALE, (x2 - x1) - UI_PAD * 2);

    float textX = x1 + (x2 - x1 - textWidth) / 2;
    float textY = y1 + (y2 - y1 - textHeight) / 2;

    if(infoData->bar) {
        float barW = PROGRESS_BAR_W;
        if(barW > (x2 - x1) - UI_PAD * 2) {
            barW = (x2 - x1) - UI_PAD * 2;
        }

        float barX = x1 + (x2 - x1 - barW) / 2;
        float barY = y1 + (y2 - y1) / 2 - PROGRESS_BAR_H - 6.0f;

        /* Soft panel behind the bar */
        screen_draw_rect(barX - 8.0f, barY - 8.0f, barW + 16.0f, PROGRESS_BAR_H + 16.0f, COLOR_PANEL, true);
        screen_draw_progress_bar(barX, barY, barW, PROGRESS_BAR_H, infoData->displayProgress, COLOR_HINT, COLOR_ACCENT);

        textX = x1 + UI_PAD;
        textY = barY + PROGRESS_BAR_H + 14.0f;
        screen_draw_string_wrap(infoData->text, textX, textY, UI_TEXT_SCALE, UI_TEXT_SCALE, COLOR_TEXT, true, x2 - UI_PAD);
        return;
    }

    screen_draw_string_wrap(infoData->text, x1 + UI_PAD, textY, UI_TEXT_SCALE, UI_TEXT_SCALE, COLOR_TEXT, true, x2 - UI_PAD);
}

ui_view* info_display(const char* name, const char* info, bool bar, void* data, void (*update)(ui_view* view, void* data, float* progress, char* text),
                                                                                void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2)) {
    info_data* infoData = (info_data*) calloc(1, sizeof(info_data));
    if(infoData == NULL) {
        error_display(NULL, NULL, "Failed to allocate info data.");

        return NULL;
    }

    infoData->bar = bar;
    infoData->data = data;
    infoData->progress = 0;
    infoData->displayProgress = 0;
    snprintf(infoData->text, PROGRESS_TEXT_MAX, "Please wait...");
    infoData->update = update;
    infoData->drawTop = drawTop;

    ui_view* view = ui_create();
    view->name = name;
    view->info = info;
    view->data = infoData;
    view->update = info_update;
    view->drawTop = info_draw_top;
    view->drawBottom = info_draw_bottom;
    ui_push(view);

    return view;
}

void info_destroy(ui_view* view) {
    if(view != NULL) {
        free(view->data);
        ui_destroy(view);
    }
}
