#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <3ds.h>

#include "cardcalc.h"
#include "resources.h"
#include "../core/core.h"

#define CC_TALLY_MAX 64
#define CC_MONEY_BUF 48

typedef enum {
    CC_MODE_MENU = 0,
    CC_MODE_BUY,
    CC_MODE_TRADE,
    CC_MODE_TALLY
} cc_mode;

typedef enum {
    CC_FIELD_NONE = 0,
    CC_FIELD_BUY_VALUE,
    CC_FIELD_BUY_CUSTOM_PCT,
    CC_FIELD_TRADE_IN,
    CC_FIELD_TRADE_OUT,
    CC_FIELD_TRADE_PCT,
    CC_FIELD_TALLY_VALUE,
    CC_FIELD_TALLY_QTY
} cc_field;

typedef struct {
    double value;
    int qty;
} cc_tally_item;

typedef struct {
    cc_mode mode;
    int menuIndex; /* 0=buy 1=trade 2=tally */
    cc_field pendingField;

    /* Buy offer */
    double buyMarket;
    int buyPct; /* 50/60/70/80 or custom stored value */
    bool buyPctCustom;

    /* Trade-in */
    double tradeInValue;
    double tradeOutValue;
    int tradePct; /* credit percent of trade-in */

    /* Deal tally */
    cc_tally_item items[CC_TALLY_MAX];
    int itemCount;
    double tallyPendingValue;
    int tallyPendingQty;
} cardcalc_data;

static void cc_format_cad(char* out, size_t outSize, double amount) {
    if(amount < 0) {
        snprintf(out, outSize, "-$%.2f CAD", -amount);
    } else {
        snprintf(out, outSize, "$%.2f CAD", amount);
    }
}

static double cc_parse_money(const char* s) {
    if(s == NULL || s[0] == '\0') return 0.0;
    /* allow $ and commas */
    char buf[64];
    size_t j = 0;
    for(size_t i = 0; s[i] != '\0' && j + 1 < sizeof(buf); i++) {
        char c = s[i];
        if(c == '$' || c == ',' || c == ' ') continue;
        buf[j++] = c;
    }
    buf[j] = '\0';
    return strtod(buf, NULL);
}

static int cc_parse_int(const char* s) {
    if(s == NULL || s[0] == '\0') return 0;
    return (int) strtol(s, NULL, 10);
}

static void cc_ask_number(cardcalc_data* d, cc_field field, const char* hint, const char* initial);

static void cc_on_number(ui_view* view, void* data, SwkbdButton button, const char* response) {
    (void) view;
    cardcalc_data* d = (cardcalc_data*) data;
    cc_field field = d->pendingField;
    d->pendingField = CC_FIELD_NONE;
    if(button != SWKBD_BUTTON_CONFIRM) {
        return;
    }
    switch(field) {
        case CC_FIELD_BUY_VALUE:
            d->buyMarket = cc_parse_money(response);
            break;
        case CC_FIELD_BUY_CUSTOM_PCT: {
            int p = cc_parse_int(response);
            if(p < 0) p = 0;
            if(p > 100) p = 100;
            d->buyPct = p;
            d->buyPctCustom = true;
            break;
        }
        case CC_FIELD_TRADE_IN:
            d->tradeInValue = cc_parse_money(response);
            break;
        case CC_FIELD_TRADE_OUT:
            d->tradeOutValue = cc_parse_money(response);
            break;
        case CC_FIELD_TRADE_PCT: {
            int p = cc_parse_int(response);
            if(p < 0) p = 0;
            if(p > 100) p = 100;
            d->tradePct = p;
            break;
        }
        case CC_FIELD_TALLY_VALUE:
            d->tallyPendingValue = cc_parse_money(response);
            /* next ask qty */
            cc_ask_number(d, CC_FIELD_TALLY_QTY, "Quantity", "1");
            break;
        case CC_FIELD_TALLY_QTY: {
            int q = cc_parse_int(response);
            if(q < 1) q = 1;
            if(q > 9999) q = 9999;
            if(d->itemCount < CC_TALLY_MAX) {
                d->items[d->itemCount].value = d->tallyPendingValue;
                d->items[d->itemCount].qty = q;
                d->itemCount++;
            }
            d->tallyPendingValue = 0;
            d->tallyPendingQty = 1;
            break;
        }
        default:
            break;
    }
}

static void cc_ask_number(cardcalc_data* d, cc_field field, const char* hint, const char* initial) {
    d->pendingField = field;
    kbd_display(hint, initial, SWKBD_TYPE_NUMPAD, 0, SWKBD_ANYTHING, 31, d, cc_on_number);
}

static double cc_tally_total(cardcalc_data* d) {
    double t = 0;
    for(int i = 0; i < d->itemCount; i++) {
        t += d->items[i].value * (double) d->items[i].qty;
    }
    return t;
}

static void cc_draw_brand(float x1, float y1, float x2, float* yInOut) {
    const char* brand = "Unown Collectables";
    float w, h;
    screen_get_string_size(&w, &h, brand, 0.55f, 0.55f);
    screen_draw_string(brand, x1 + (x2 - x1 - w) / 2, *yInOut, 0.55f, 0.55f, COLOR_ACCENT, true);
    *yInOut += h + 4.0f;
    const char* sub = "Card Show Calculator";
    screen_get_string_size(&w, &h, sub, 0.5f, 0.5f);
    screen_draw_string(sub, x1 + (x2 - x1 - w) / 2, *yInOut, 0.5f, 0.5f, COLOR_TEXT, true);
    *yInOut += h + 10.0f;
}

static void cc_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    (void) data;
    const char* tip =
        "Offline card-show helpers for offers,\n"
        "trade-ins, and running tallies.\n\n"
        "No network. Nothing is saved.";
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.5f, 0.5f, x2 - x1 - 28.0f);
    float y = y1 + 16.0f;
    screen_draw_rect(x1 + 8.0f, y - 8.0f, (x2 - x1) - 16.0f, h + 16.0f, COLOR_PANEL, true);
    screen_draw_string_wrap(tip, x1 + 14.0f, y, 0.5f, 0.5f, COLOR_TEXT, true, x2 - 14.0f);

    const char* brand = "Unown Collectables";
    screen_get_string_size(&w, &h, brand, 0.5f, 0.5f);
    screen_draw_string(brand, x1 + (x2 - x1 - w) / 2, y2 - h - 8.0f, 0.5f, 0.5f, COLOR_ACCENT, true);
}

static void cc_set_hints(ui_view* view, cardcalc_data* d) {
    if(d->mode == CC_MODE_MENU) {
        view->info = "A = open, B = back";
        view->name = "Unown Collectables";
    } else if(d->mode == CC_MODE_BUY) {
        view->info = "A = market $, L/X/Y/R = %, Select = custom %, B = back";
        view->name = "Buy offer";
    } else if(d->mode == CC_MODE_TRADE) {
        view->info = "A = trade-in $, X = take $, Y = credit %, B = back";
        view->name = "Trade-in";
    } else {
        view->info = "A = add item, X = remove last, Y = clear, B = back";
        view->name = "Deal tally";
    }
}

static void cc_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    cardcalc_data* d = (cardcalc_data*) data;
    const float pad = 10.0f;
    float y = y1 + pad;
    float w, h;
    char line[192];
    char money[CC_MONEY_BUF];

    cc_draw_brand(x1, y1, x2, &y);

    if(d->mode == CC_MODE_MENU) {
        const char* items[3] = {"Buy offer", "Trade-in", "Deal tally"};
        for(int i = 0; i < 3; i++) {
            u32 col = (i == d->menuIndex) ? COLOR_ACCENT : COLOR_TEXT;
            snprintf(line, sizeof(line), "%s  %s", (i == d->menuIndex) ? ">" : " ", items[i]);
            screen_draw_string(line, x1 + pad, y, 0.55f, 0.55f, col, true);
            y += 22.0f;
        }
        y += 8.0f;
        screen_draw_string("Amounts shown as $0.00 CAD", x1 + pad, y, 0.4f, 0.4f, COLOR_HINT, true);
        return;
    }

    if(d->mode == CC_MODE_BUY) {
        cc_format_cad(money, sizeof(money), d->buyMarket);
        snprintf(line, sizeof(line), "Market value:  %s", money);
        screen_draw_string(line, x1 + pad, y, 0.5f, 0.5f, COLOR_TEXT, true);
        y += 18.0f;

        snprintf(line, sizeof(line), "Offer percent:  %d%%%s", d->buyPct, d->buyPctCustom ? " (custom)" : "");
        screen_draw_string(line, x1 + pad, y, 0.5f, 0.5f, COLOR_TEXT, true);
        y += 18.0f;

        screen_draw_string("Presets: L=50%  X=60%  Y=70%  R=80%  Select=custom", x1 + pad, y, 0.38f, 0.38f, COLOR_HINT, true);
        y += 16.0f;

        double offer = d->buyMarket * ((double) d->buyPct / 100.0);
        cc_format_cad(money, sizeof(money), offer);
        snprintf(line, sizeof(line), "Offer price:  %s", money);
        screen_get_string_size(&w, &h, line, 0.6f, 0.6f);
        screen_draw_rect(x1 + 8.0f, y - 4.0f, (x2 - x1) - 16.0f, h + 12.0f, COLOR_PANEL, true);
        screen_draw_string(line, x1 + pad, y, 0.6f, 0.6f, COLOR_ACCENT, true);
        return;
    }

    if(d->mode == CC_MODE_TRADE) {
        cc_format_cad(money, sizeof(money), d->tradeInValue);
        snprintf(line, sizeof(line), "Cards traded in:  %s", money);
        screen_draw_string(line, x1 + pad, y, 0.48f, 0.48f, COLOR_TEXT, true);
        y += 16.0f;

        cc_format_cad(money, sizeof(money), d->tradeOutValue);
        snprintf(line, sizeof(line), "Cards taken:  %s", money);
        screen_draw_string(line, x1 + pad, y, 0.48f, 0.48f, COLOR_TEXT, true);
        y += 16.0f;

        snprintf(line, sizeof(line), "Trade credit:  %d%%", d->tradePct);
        screen_draw_string(line, x1 + pad, y, 0.48f, 0.48f, COLOR_TEXT, true);
        y += 18.0f;

        double credit = d->tradeInValue * ((double) d->tradePct / 100.0);
        double diff = d->tradeOutValue - credit;
        cc_format_cad(money, sizeof(money), credit);
        snprintf(line, sizeof(line), "Credit toward deal:  %s", money);
        screen_draw_string(line, x1 + pad, y, 0.48f, 0.48f, COLOR_HINT, true);
        y += 18.0f;

        if(diff > 0.004) {
            cc_format_cad(money, sizeof(money), diff);
            snprintf(line, sizeof(line), "Customer owes:  %s", money);
        } else if(diff < -0.004) {
            cc_format_cad(money, sizeof(money), -diff);
            snprintf(line, sizeof(line), "Store owes:  %s", money);
        } else {
            snprintf(line, sizeof(line), "Even trade — nothing owed.");
        }
        screen_get_string_size(&w, &h, line, 0.55f, 0.55f);
        screen_draw_rect(x1 + 8.0f, y - 4.0f, (x2 - x1) - 16.0f, h + 12.0f, COLOR_PANEL, true);
        screen_draw_string(line, x1 + pad, y, 0.55f, 0.55f, COLOR_ACCENT, true);
        return;
    }

    /* tally */
    snprintf(line, sizeof(line), "Line items: %d / %d", d->itemCount, CC_TALLY_MAX);
    screen_draw_string(line, x1 + pad, y, 0.45f, 0.45f, COLOR_HINT, true);
    y += 16.0f;

    int show = d->itemCount;
    if(show > 6) show = 6;
    int start = d->itemCount - show;
    for(int i = start; i < d->itemCount; i++) {
        cc_format_cad(money, sizeof(money), d->items[i].value);
        char sub[CC_MONEY_BUF];
        cc_format_cad(sub, sizeof(sub), d->items[i].value * (double) d->items[i].qty);
        snprintf(line, sizeof(line), "%d. %s x%d = %s", i + 1, money, d->items[i].qty, sub);
        screen_draw_string(line, x1 + pad, y, 0.42f, 0.42f, COLOR_TEXT, true);
        y += 14.0f;
    }
    if(d->itemCount == 0) {
        screen_draw_string("No items yet. Press A to add one.", x1 + pad, y, 0.45f, 0.45f, COLOR_HINT, true);
        y += 16.0f;
    }
    y += 6.0f;

    cc_format_cad(money, sizeof(money), cc_tally_total(d));
    snprintf(line, sizeof(line), "Running total:  %s", money);
    screen_get_string_size(&w, &h, line, 0.58f, 0.58f);
    screen_draw_rect(x1 + 8.0f, y - 4.0f, (x2 - x1) - 16.0f, h + 12.0f, COLOR_PANEL, true);
    screen_draw_string(line, x1 + pad, y, 0.58f, 0.58f, COLOR_ACCENT, true);
}

static void cc_update(ui_view* view, void* data, float* progress, char* text) {
    (void) progress;
    cardcalc_data* d = (cardcalc_data*) data;
    if(text != NULL) text[0] = '\0';
    cc_set_hints(view, d);

    u32 down = hidKeysDown();

    if(d->mode == CC_MODE_MENU) {
        if(down & KEY_B) {
            ui_pop();
            info_destroy(view);
            free(d);
            return;
        }
        if(down & KEY_UP) {
            d->menuIndex = (d->menuIndex + 2) % 3;
            return;
        }
        if(down & KEY_DOWN) {
            d->menuIndex = (d->menuIndex + 1) % 3;
            return;
        }
        if(down & KEY_A) {
            if(d->menuIndex == 0) d->mode = CC_MODE_BUY;
            else if(d->menuIndex == 1) d->mode = CC_MODE_TRADE;
            else d->mode = CC_MODE_TALLY;
            return;
        }
        return;
    }

    if(down & KEY_B) {
        d->mode = CC_MODE_MENU;
        return;
    }

    if(d->mode == CC_MODE_BUY) {
        if(down & KEY_A) {
            char init[32];
            snprintf(init, sizeof(init), "%.2f", d->buyMarket);
            cc_ask_number(d, CC_FIELD_BUY_VALUE, "Market value ($)", init);
            return;
        }
        if(down & KEY_L) { d->buyPct = 50; d->buyPctCustom = false; return; }
        if(down & KEY_X) { d->buyPct = 60; d->buyPctCustom = false; return; }
        if(down & KEY_Y) { d->buyPct = 70; d->buyPctCustom = false; return; }
        if(down & KEY_R) { d->buyPct = 80; d->buyPctCustom = false; return; }
        if(down & KEY_SELECT) {
            char init[16];
            snprintf(init, sizeof(init), "%d", d->buyPct);
            cc_ask_number(d, CC_FIELD_BUY_CUSTOM_PCT, "Custom offer %", init);
            return;
        }
        return;
    }

    if(d->mode == CC_MODE_TRADE) {
        if(down & KEY_A) {
            char init[32];
            snprintf(init, sizeof(init), "%.2f", d->tradeInValue);
            cc_ask_number(d, CC_FIELD_TRADE_IN, "Trade-in value ($)", init);
            return;
        }
        if(down & KEY_X) {
            char init[32];
            snprintf(init, sizeof(init), "%.2f", d->tradeOutValue);
            cc_ask_number(d, CC_FIELD_TRADE_OUT, "Cards taken value ($)", init);
            return;
        }
        if(down & KEY_Y) {
            char init[16];
            snprintf(init, sizeof(init), "%d", d->tradePct);
            cc_ask_number(d, CC_FIELD_TRADE_PCT, "Trade credit %", init);
            return;
        }
        return;
    }

    /* tally */
    if(down & KEY_A) {
        cc_ask_number(d, CC_FIELD_TALLY_VALUE, "Item value ($)", "0");
        return;
    }
    if(down & KEY_X) {
        if(d->itemCount > 0) d->itemCount--;
        return;
    }
    if(down & KEY_Y) {
        d->itemCount = 0;
        return;
    }
}

void cardcalc_open(void) {
    cardcalc_data* d = (cardcalc_data*) calloc(1, sizeof(cardcalc_data));
    if(d == NULL) {
        error_display(NULL, NULL, "Failed to allocate screen data.");
        return;
    }
    d->mode = CC_MODE_MENU;
    d->buyPct = 70;
    d->tradePct = 70;
    d->tallyPendingQty = 1;

    ui_view* view = info_display("Unown Collectables", "A = open, B = back", false, d, cc_update, cc_draw_top);
    if(view == NULL) {
        free(d);
        return;
    }
    view->drawBottom = cc_draw_bottom;
}
