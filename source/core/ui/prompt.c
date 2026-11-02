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

typedef struct {
    const char* text;
    u32 color;
    char options[PROMPT_OPTIONS_MAX][PROMPT_OPTION_TEXT_MAX];
    u32 optionButtons[PROMPT_OPTIONS_MAX];
    u32 numOptions;
    void* data;
    void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2);
    void (*onResponse)(ui_view* view, void* data, u32 response);
} prompt_data;

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

        float buttonWidth = (bx2 - bx1 - (12 * (promptData->numOptions + 1))) / promptData->numOptions;
        u32 buttonHeight;
        screen_get_texture_size(NULL, &buttonHeight, TEXTURE_BUTTON);
        if(buttonHeight < 40) {
            buttonHeight = 48;
        }

        for(u32 i = 0; i < promptData->numOptions; i++) {
            float buttonX = bx1 + 12 + (buttonWidth + 12) * i;
            float buttonY = by2 - 12 - buttonHeight;

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
    }
}

static const char* button_strings[32] = {
        "A",
        "B",
        "Select",
        "Start",
        "D-Pad Right",
        "D-Pad Left",
        "D-Pad Up",
        "D-Pad Down",
        "R",
        "L",
        "X",
        "Y",
        "",
        "",
        "ZL",
        "ZR",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "C-Stick Right",
        "C-Stick Left",
        "C-Stick Up",
        "C-Stick Down",
        "Circle Pad Right",
        "Circle Pad Left",
        "Circle Pad Up",
        "Circle Pad Down"
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
    prompt_data* promptData = (prompt_data*) data;

    const float pad = 12.0f;
    const float scale = 0.55f;
    float buttonWidth = (x2 - x1 - (pad * (promptData->numOptions + 1))) / promptData->numOptions;
    u32 buttonHeight;
    screen_get_texture_size(NULL, &buttonHeight, TEXTURE_BUTTON);
    if(buttonHeight < 40) {
        buttonHeight = 48;
    }

    char button[64];
    char text[PROMPT_OPTION_TEXT_MAX + 65];
    for(u32 i = 0; i < promptData->numOptions; i++) {
        float buttonX = x1 + pad + (buttonWidth + pad) * i;
        float buttonY = y2 - pad - buttonHeight;
        screen_draw_texture(TEXTURE_BUTTON, buttonX, buttonY, buttonWidth, buttonHeight);

        prompt_button_to_string(button, 64, promptData->optionButtons[i]);
        snprintf(text, sizeof(text), "%s\n(%s)", promptData->options[i], button);

        float textWidth;
        float textHeight;
        screen_get_string_size(&textWidth, &textHeight, text, 0.5f, 0.5f);
        screen_draw_string(text, buttonX + (buttonWidth - textWidth) / 2, buttonY + (buttonHeight - textHeight) / 2, 0.5f, 0.5f, promptData->color, true);
    }

    float wrapX = x2 - pad;
    float textWidth;
    float textHeight;
    screen_get_string_size_wrap(&textWidth, &textHeight, promptData->text, scale, scale, wrapX - (x1 + pad));

    float panelTop = y1 + pad;
    float panelBottom = y2 - pad - buttonHeight - pad;
    float panelH = panelBottom - panelTop;
    if(panelH > textHeight + pad * 2) {
        screen_draw_rect(x1 + pad / 2, panelTop, (x2 - x1) - pad, panelH, COLOR_PANEL, true);
    }

    float textY = panelTop + (panelH - textHeight) / 2;
    if(textY < panelTop + 6.0f) {
        textY = panelTop + 6.0f;
    }
    screen_draw_string_wrap(promptData->text, x1 + pad, textY, scale, scale, promptData->color, true, wrapX);
}

ui_view* prompt_display_multi_choice(const char* name, const char* text, u32 color, const char** options, u32* optionButtons, u32 numOptions, void* data, void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2),
                                                                                                                                                          void (*onResponse)(ui_view* view, void* data, u32 response)) {
    prompt_data* promptData = (prompt_data*) calloc(1, sizeof(prompt_data));
    if(promptData == NULL) {
        error_display(NULL, NULL, "Failed to allocate prompt data.");

        return NULL;
    }

    promptData->text = text;
    promptData->color = color;

    for(u32 i = 0; i < numOptions && i < PROMPT_OPTIONS_MAX; i++) {
        string_copy(promptData->options[i], options[i], PROMPT_OPTION_TEXT_MAX);
        promptData->optionButtons[i] = optionButtons[i];
    }

    promptData->numOptions = numOptions;
    promptData->data = data;
    promptData->drawTop = drawTop;
    promptData->onResponse = onResponse;

    ui_view* view = ui_create();
    view->name = name;
    view->info = "";
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
