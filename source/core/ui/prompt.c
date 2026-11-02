#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "error.h"
#include "prompt.h"
#include "ui.h"
#include "../screen.h"
#include "../stringutil.h"
#include "../../fbi/resources.h"

#define PROMPT_TEXT_MAX 2048
#define PROMPT_PAD 10.0f
#define PROMPT_TEXT_SCALE 0.5f

typedef struct {
    char text[PROMPT_TEXT_MAX];
    u32 accentColor; /* COLOR_TEXT / DANGER / WARNING — accent only; body always COLOR_TEXT */
    char options[PROMPT_OPTIONS_MAX][PROMPT_OPTION_TEXT_MAX];
    u32 optionButtons[PROMPT_OPTIONS_MAX];
    u32 numOptions;
    void* data;
    void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2);
    void (*onResponse)(ui_view* view, void* data, u32 response);
    float scroll;
    float contentHeight;
    float viewHeight;
} prompt_data;

static float prompt_button_height(void) {
    u32 buttonHeight = 0;
    screen_get_texture_size(NULL, &buttonHeight, TEXTURE_BUTTON);
    if(buttonHeight < 40) {
        buttonHeight = 48;
    }
    return (float) buttonHeight;
}

static float prompt_footer_height(prompt_data* promptData) {
    (void) promptData;
    return prompt_button_height() + PROMPT_PAD * 2.0f;
}

static void prompt_notify_response(ui_view* view, prompt_data* promptData, u32 response) {
    ui_pop();

    if(promptData->onResponse != NULL) {
        promptData->onResponse(view, promptData->data, response);
    }

    free(promptData);
    ui_destroy(view);
}

static void prompt_update(ui_view* view, void* data, float bx1, float by1, float bx2, float by2) {
    prompt_data* promptData = (prompt_data*) data;

    float footerH = prompt_footer_height(promptData);
    float contentH = (by2 - by1) - footerH;
    promptData->viewHeight = contentH;

    float maxScroll = promptData->contentHeight - contentH + PROMPT_PAD;
    if(maxScroll < 0.0f) {
        maxScroll = 0.0f;
    }

    if((hidKeysDown() | hidKeysHeld()) & KEY_DOWN) {
        promptData->scroll += 8.0f;
    }
    if((hidKeysDown() | hidKeysHeld()) & KEY_UP) {
        promptData->scroll -= 8.0f;
    }
    if(promptData->scroll < 0.0f) {
        promptData->scroll = 0.0f;
    }
    if(promptData->scroll > maxScroll) {
        promptData->scroll = maxScroll;
    }

    u32 down = hidKeysDown();
    for(u32 i = 0; i < promptData->numOptions; i++) {
        if(down & (promptData->optionButtons[i] & ~KEY_TOUCH)) {
            prompt_notify_response(view, promptData, i);
            return;
        }
    }

    if(hidKeysDown() & KEY_TOUCH) {
        touchPosition pos;
        hidTouchRead(&pos);

        float buttonWidth = (bx2 - bx1 - (PROMPT_PAD * (promptData->numOptions + 1))) / promptData->numOptions;
        float buttonHeight = prompt_button_height();

        for(u32 i = 0; i < promptData->numOptions; i++) {
            float buttonX = bx1 + PROMPT_PAD + (buttonWidth + PROMPT_PAD) * i;
            float buttonY = by2 - PROMPT_PAD - buttonHeight;

            if(pos.px >= buttonX && pos.py >= buttonY && pos.px < buttonX + buttonWidth && pos.py < buttonY + buttonHeight) {
                prompt_notify_response(view, promptData, i);
                return;
            }
        }
    }
}

static void prompt_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    prompt_data* promptData = (prompt_data*) data;

    if(promptData->drawTop != NULL) {
        promptData->drawTop(view, promptData->data, x1, y1, x2, y2);
        return;
    }

    /* Default tip panel when callers leave top blank. */
    const char* tip = "Read the message on the touch screen, then choose a button.";
    if(promptData->numOptions == 1) {
        tip = "Read the message, then press any button when you are done.";
    } else if(promptData->numOptions >= 2) {
        tip = "Read the message, then choose Yes or No.";
    }

    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.55f, 0.55f, x2 - x1 - 28.0f);
    float y = y1 + (y2 - y1 - h) / 2.0f;
    if(y < y1 + 8.0f) {
        y = y1 + 8.0f;
    }
    screen_draw_rect(x1 + 8.0f, y - 8.0f, (x2 - x1) - 16.0f, h + 16.0f, COLOR_PANEL, true);
    screen_draw_string_wrap(tip, x1 + 14.0f, y, 0.55f, 0.55f, COLOR_TEXT, true, x2 - 14.0f);
}

static const char* button_strings[32] = {
        "A", "B", "Select", "Start",
        "D-Pad Right", "D-Pad Left", "D-Pad Up", "D-Pad Down",
        "R", "L", "X", "Y",
        "", "", "ZL", "ZR",
        "", "", "", "", "", "", "", "",
        "C-Stick Right", "C-Stick Left", "C-Stick Up", "C-Stick Down",
        "Circle Pad Right", "Circle Pad Left", "Circle Pad Up", "Circle Pad Down"
};

static void prompt_button_to_string(char* out, size_t size, u32 button) {
    if(button == PROMPT_BUTTON_ANY) {
        snprintf(out, size, "Any Button");
        return;
    }

    size_t pos = 0;
    for(u8 bit = 0; bit < 32 && pos < size; bit++) {
        if(button & (1 << bit)) {
            if(pos > 0) {
                pos += snprintf(out + pos, size - pos, "/");
            }
            pos += snprintf(out + pos, size - pos, button_strings[bit]);
        }
    }
}

static void prompt_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    prompt_data* promptData = (prompt_data*) data;

    float buttonHeight = prompt_button_height();
    float footerH = buttonHeight + PROMPT_PAD * 2.0f;
    float contentBottom = y2 - footerH;
    float contentTop = y1 + PROMPT_PAD;
    float wrapWidth = (x2 - x1) - PROMPT_PAD * 2.0f;

    float textWidth = 0.0f;
    float textHeight = 0.0f;
    screen_get_string_size_wrap(&textWidth, &textHeight, promptData->text, PROMPT_TEXT_SCALE, PROMPT_TEXT_SCALE, wrapWidth);
    promptData->contentHeight = textHeight + PROMPT_PAD;

    /* Content panel (full content area). */
    screen_draw_rect(x1 + 4.0f, y1 + 4.0f, (x2 - x1) - 8.0f, contentBottom - (y1 + 4.0f), COLOR_PANEL, true);

    /* Accent strip for danger/warning dialogs. */
    if(promptData->accentColor == COLOR_DANGER || promptData->accentColor == COLOR_WARNING) {
        screen_draw_rect(x1 + 4.0f, y1 + 4.0f, 6.0f, contentBottom - (y1 + 4.0f), promptData->accentColor, true);
    }

    float textX = x1 + PROMPT_PAD;
    float textY = contentTop - promptData->scroll;
    /* Body text: COLOR_TEXT on COLOR_PANEL (works in light and dark). */
    screen_draw_string_wrap(promptData->text, textX, textY, PROMPT_TEXT_SCALE, PROMPT_TEXT_SCALE, COLOR_TEXT, true, x2 - PROMPT_PAD);

    /* Opaque footer covers any overflowing text so buttons never overlap body. */
    screen_draw_rect(x1, contentBottom, x2 - x1, footerH, COLOR_PANEL, true);
    screen_draw_rect(x1 + 4.0f, contentBottom, (x2 - x1) - 8.0f, 2.0f, COLOR_HINT, true);

    if(promptData->contentHeight > (contentBottom - contentTop)) {
        const char* scrollHint = "Up/Down = scroll";
        float sw, sh;
        screen_get_string_size(&sw, &sh, scrollHint, 0.4f, 0.4f);
        screen_draw_string(scrollHint, x1 + (x2 - x1 - sw) / 2, contentBottom - sh - 2.0f, 0.4f, 0.4f, COLOR_HINT, true);
    }

    float buttonWidth = (x2 - x1 - (PROMPT_PAD * (promptData->numOptions + 1))) / promptData->numOptions;
    char button[64];
    char label[PROMPT_OPTION_TEXT_MAX + 65];
    for(u32 i = 0; i < promptData->numOptions; i++) {
        float buttonX = x1 + PROMPT_PAD + (buttonWidth + PROMPT_PAD) * i;
        float buttonY = y2 - PROMPT_PAD - buttonHeight;
        screen_draw_texture(TEXTURE_BUTTON, buttonX, buttonY, buttonWidth, buttonHeight);

        prompt_button_to_string(button, sizeof(button), promptData->optionButtons[i]);
        snprintf(label, sizeof(label), "%s\n(%s)", promptData->options[i], button);

        float lw, lh;
        screen_get_string_size(&lw, &lh, label, 0.45f, 0.45f);
        /* White labels on button art. */
        screen_draw_string(label, buttonX + (buttonWidth - lw) / 2, buttonY + (buttonHeight - lh) / 2, 0.45f, 0.45f, COLOR_BAR_TEXT, true);
    }
}

ui_view* prompt_display_multi_choice(const char* name, const char* text, u32 color, const char** options, u32* optionButtons, u32 numOptions, void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2),
                                                                                                                                                          void (*onResponse)(ui_view* view, void* data, u32 response)) {
    prompt_data* promptData = (prompt_data*) calloc(1, sizeof(prompt_data));
    if(promptData == NULL) {
        error_display(NULL, NULL, "Failed to allocate prompt data.");
        return NULL;
    }

    string_copy(promptData->text, text != NULL ? text : "", sizeof(promptData->text));
    promptData->accentColor = color;

    for(u32 i = 0; i < numOptions && i < PROMPT_OPTIONS_MAX; i++) {
        string_copy(promptData->options[i], options[i], PROMPT_OPTION_TEXT_MAX);
        promptData->optionButtons[i] = optionButtons[i];
    }

    promptData->numOptions = numOptions;
    promptData->data = data;
    promptData->drawTop = drawTop;
    promptData->onResponse = onResponse;
    promptData->scroll = 0.0f;

    ui_view* view = ui_create();
    view->name = name;
    view->info = "Up/Down scroll · choose below";
    view->data = promptData;
    view->update = prompt_update;
    view->drawTop = prompt_draw_top;
    view->drawBottom = prompt_draw_bottom;
    ui_push(view);

    return view;
}

ui_view* prompt_display_notify(const char* name, const char* text, u32 color, void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2),
                                                                                          void (*onResponse)(ui_view* view, void* data, u32 response)) {
    static const char* options[1] = {"OK, got it"};
    static u32 optionButtons[1] = {PROMPT_BUTTON_ANY};
    return prompt_display_multi_choice(name, text, color, options, optionButtons, 1, data, drawTop, onResponse);
}

ui_view* prompt_display_yes_no(const char* name, const char* text, u32 color, void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2),
                                                                                          void (*onResponse)(ui_view* view, void* data, u32 response)) {
    static const char* options[2] = {"Yes, do it", "No, go back"};
    static u32 optionButtons[2] = {KEY_A, KEY_B};
    return prompt_display_multi_choice(name, text, color, options, optionButtons, 2, data, drawTop, onResponse);
}

ui_view* prompt_display_safety_refusal(const char* name, const char* text, void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2),
                                                                                     void (*onResponse)(ui_view* view, void* data, u32 response)) {
    return prompt_display_notify(name, text, COLOR_DANGER, data, drawTop, onResponse);
}
