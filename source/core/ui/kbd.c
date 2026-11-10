#include <malloc.h>
#include <string.h>

#include <3ds.h>

#include "error.h"
#include "kbd.h"
#include "ui.h"

typedef struct {
    char* hint;        /* own copies: callers often pass stack buffers */
    char* initialText;
    SwkbdType type;
    u32 features;
    SwkbdValidInput validation;
    u32 maxSize;

    char* response;

    void* data;
    void (*onResponse)(ui_view* view, void* data, SwkbdButton button, const char* response);
} kbd_data;

static void kbd_update(ui_view* view, void* data, float bx1, float by1, float bx2, float by2) {
    kbd_data* kbdData = (kbd_data*) data;

    SwkbdState swkbd;
    swkbdInit(&swkbd, kbdData->type, 2, kbdData->maxSize < 65000 ? (int) kbdData->maxSize : 65000);
    swkbdSetHintText(&swkbd, kbdData->hint);
    swkbdSetInitialText(&swkbd, kbdData->initialText);
    swkbdSetFeatures(&swkbd, kbdData->features);
    swkbdSetValidation(&swkbd, kbdData->validation, 0, 0);
    if(kbdData->type == SWKBD_TYPE_NUMPAD) {
        /* Give the number pad a decimal point key for money amounts. */
        swkbdSetNumpadKeys(&swkbd, '.', 0);
    }
    SwkbdButton button = swkbdInputText(&swkbd, kbdData->response, kbdData->maxSize);

    ui_pop();

    if(kbdData->onResponse != NULL) {
        kbdData->onResponse(view, kbdData->data, button, kbdData->response);
    }

    free(kbdData->response);
    free(kbdData->hint);
    free(kbdData->initialText);
    free(kbdData);
    ui_destroy(view);
}

ui_view* kbd_display(const char* hint, const char* initialText, SwkbdType type, u32 features, SwkbdValidInput validation, u32 maxSize, void* data, void (*onResponse)(ui_view* view, void* data, SwkbdButton button, const char* response)) {
    kbd_data* kbdData = (kbd_data*) calloc(1, sizeof(kbd_data));
    if(kbdData == NULL) {
        error_display(NULL, NULL, "Failed to allocate keyboard data.");

        return NULL;
    }

    kbdData->hint = strdup(hint != NULL ? hint : "");
    kbdData->initialText = strdup(initialText != NULL ? initialText : "");
    if(kbdData->hint == NULL || kbdData->initialText == NULL) {
        error_display(NULL, NULL, "Failed to allocate keyboard data.");

        free(kbdData->hint);
        free(kbdData->initialText);
        free(kbdData);
        return NULL;
    }
    kbdData->type = type;
    kbdData->features = features;
    kbdData->validation = validation;
    kbdData->maxSize = maxSize;

    kbdData->response = (char*) calloc(1, maxSize);
    if(kbdData->response == NULL) {
        error_display(NULL, NULL, "Failed to allocate keyboard response buffer.");

        free(kbdData->hint);
        free(kbdData->initialText);
        free(kbdData);
        return NULL;
    }

    kbdData->data = data;
    kbdData->onResponse = onResponse;

    ui_view* view = ui_create();
    view->name = "";
    view->info = "";
    view->data = kbdData;
    view->update = kbd_update;
    view->drawTop = NULL;
    view->drawBottom = NULL;
    ui_push(view);

    return view;
}