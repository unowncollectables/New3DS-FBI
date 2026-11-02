#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/listtickets.h"
#include "task/listpendingtitles.h"
#include "task/uitask.h"
#include "../core/core.h"

/* Only user SD app tickets (00040000), never system. */
static bool leftovers_is_user_app(u64 titleId) {
    return ((titleId >> 32) & 0xFFFFFFFFULL) == 0x00040000ULL;
}

typedef struct {
    populate_tickets_data ticketsPop;
    populate_pending_titles_data pendingPop;
    linked_list tickets;
    linked_list pending;
    bool ticketsDone;
    bool pendingDone;
    bool built;
} leftovers_data;

static void leftovers_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    (void) view;
    leftovers_data* d = (leftovers_data*) data;
    (void) d;
    if(selected != NULL && selected->data != NULL) {
        /* data pointer tags type via low bit? use name prefix instead */
        if(strncmp(selected->name, "[Ticket]", 8) == 0) {
            task_draw_ticket_info(view, selected->data, x1, y1, x2, y2);
            return;
        }
        if(strncmp(selected->name, "[Pending]", 9) == 0) {
            task_draw_pending_title_info(view, selected->data, x1, y1, x2, y2);
            return;
        }
    }
    const char* tip =
        "Leftovers are tickets or unfinished installs\n"
        "with no matching game.\n\n"
        "Deleting is optional and asks first.\n"
        "System items are never listed.";
    float w, h;
    screen_get_string_size_wrap(&w, &h, tip, 0.48f, 0.48f, x2 - x1 - 24.0f);
    screen_draw_string_wrap(tip, x1 + 12.0f, y1 + 20.0f, 0.48f, 0.48f, COLOR_TEXT, true, x2 - 12.0f);
}

typedef struct {
    leftovers_data* parent;
    list_item* item;
    bool isTicket;
} leftovers_del_ctx;

static void leftovers_delete_onresponse(ui_view* view, void* data, u32 response) {
    (void) view;
    leftovers_del_ctx* ctx = (leftovers_del_ctx*) data;
    if(ctx == NULL) {
        return;
    }
    if(response == PROMPT_YES && ctx->item != NULL) {
        linked_list tmp;
        linked_list_init(&tmp);
        linked_list_add(&tmp, ctx->item);
        if(ctx->isTicket) {
            action_delete_ticket(&tmp, ctx->item);
        } else {
            action_delete_pending_title(&tmp, ctx->item);
        }
        /* async; wrapper freed by action or leaked until exit — free name only after */
    } else {
        free(ctx->item);
    }
    free(ctx);
}

static void leftovers_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    leftovers_data* d = (leftovers_data*) data;

    if(hidKeysDown() & KEY_B) {
        if(!d->ticketsPop.finished) {
            svcSignalEvent(d->ticketsPop.cancelEvent);
            while(!d->ticketsPop.finished) svcSleepThread(1000000);
        }
        if(!d->pendingPop.finished) {
            svcSignalEvent(d->pendingPop.cancelEvent);
            while(!d->pendingPop.finished) svcSleepThread(1000000);
        }
        task_clear_tickets(&d->tickets);
        task_clear_pending_titles(&d->pending);
        linked_list_destroy(&d->tickets);
        linked_list_destroy(&d->pending);
        /* free display items (shallow copies of names pointing to same data — we calloc'd wrappers) */
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* it = (list_item*) linked_list_iter_next(&iter);
            free(it);
        }
        linked_list_clear(items);
        ui_pop();
        list_destroy(view);
        free(d);
        return;
    }

    if(hidKeysDown() & KEY_X) {
        if(!d->ticketsPop.finished) {
            svcSignalEvent(d->ticketsPop.cancelEvent);
            while(!d->ticketsPop.finished) svcSleepThread(1000000);
        }
        if(!d->pendingPop.finished) {
            svcSignalEvent(d->pendingPop.cancelEvent);
            while(!d->pendingPop.finished) svcSleepThread(1000000);
        }
        linked_list_iter iter;
        linked_list_iterate(items, &iter);
        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }
        task_clear_tickets(&d->tickets);
        task_clear_pending_titles(&d->pending);
        d->built = false;
        d->ticketsDone = false;
        d->pendingDone = false;
        d->ticketsPop.items = NULL;
        d->pendingPop.items = NULL;
        d->ticketsPop.finished = true;
        d->pendingPop.finished = true;
    }

    if(!d->built && d->ticketsPop.finished && d->pendingPop.finished && !d->ticketsDone) {
        d->ticketsPop.items = &d->tickets;
        d->pendingPop.items = &d->pending;
        Result tr = task_populate_tickets(&d->ticketsPop);
        Result pr = task_populate_pending_titles(&d->pendingPop);
        if(R_FAILED(tr) || R_FAILED(pr)) {
            d->ticketsDone = true;
            d->pendingDone = true;
            d->built = true;
            list_item* empty = (list_item*) calloc(1, sizeof(list_item));
            if(empty) {
                string_copy(empty->name, "(Could not scan leftovers)", sizeof(empty->name));
                empty->color = COLOR_DANGER;
                linked_list_add(items, empty);
            }
        } else {
            d->ticketsDone = false;
            d->pendingDone = false;
        }
    }

    if(!d->built && d->ticketsPop.finished && d->pendingPop.finished && d->ticketsPop.items != NULL) {
        d->ticketsDone = true;
        d->pendingDone = true;
    }

    if(d->ticketsDone && d->pendingDone && !d->built) {
        linked_list_iter iter;
        linked_list_iterate(&d->tickets, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* src = (list_item*) linked_list_iter_next(&iter);
            ticket_info* info = src != NULL ? (ticket_info*) src->data : NULL;
            if(info == NULL || info->inUse) continue;
            if(!leftovers_is_user_app(info->titleId)) continue;
            list_item* row = (list_item*) calloc(1, sizeof(list_item));
            if(row == NULL) continue;
            snprintf(row->name, LIST_ITEM_NAME_MAX, "[Ticket] %s", src->name);
            row->color = COLOR_WARNING;
            row->data = info; /* borrow */
            linked_list_add(items, row);
        }
        linked_list_iterate(&d->pending, &iter);
        while(linked_list_iter_has_next(&iter)) {
            list_item* src = (list_item*) linked_list_iter_next(&iter);
            pending_title_info* info = src != NULL ? (pending_title_info*) src->data : NULL;
            if(info == NULL) continue;
            if(!leftovers_is_user_app(info->titleId)) continue;
            list_item* row = (list_item*) calloc(1, sizeof(list_item));
            if(row == NULL) continue;
            snprintf(row->name, LIST_ITEM_NAME_MAX, "[Pending] %s", src->name);
            row->color = COLOR_HINT;
            row->data = info;
            linked_list_add(items, row);
        }
        if(linked_list_size(items) == 0) {
            list_item* empty = (list_item*) calloc(1, sizeof(list_item));
            if(empty) {
                string_copy(empty->name, "(Nothing left over — nice!)", sizeof(empty->name));
                empty->color = COLOR_SUCCESS;
                linked_list_add(items, empty);
            }
        }
        d->built = true;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        if(selected->name[0] == '(') {
            return;
        }
        leftovers_del_ctx* ctx = (leftovers_del_ctx*) calloc(1, sizeof(leftovers_del_ctx));
        if(ctx == NULL) {
            return;
        }
        list_item* wrap = (list_item*) calloc(1, sizeof(list_item));
        if(wrap == NULL) {
            free(ctx);
            return;
        }
        string_copy(wrap->name, selected->name, sizeof(wrap->name));
        wrap->data = selected->data;
        ctx->parent = d;
        ctx->item = wrap;
        ctx->isTicket = strncmp(selected->name, "[Ticket]", 8) == 0;
        prompt_display_yes_no(
            "Delete leftover?",
            "Remove this leftover from the system?\nThis cannot be undone.\n(Not a system item.)",
            COLOR_WARNING, ctx, NULL, leftovers_delete_onresponse);
    }
}

void leftovers_open(void) {
    leftovers_data* d = (leftovers_data*) calloc(1, sizeof(leftovers_data));
    if(d == NULL) {
        error_display(NULL, NULL, "Out of memory.");
        return;
    }
    linked_list_init(&d->tickets);
    linked_list_init(&d->pending);
    d->ticketsPop.finished = true;
    d->pendingPop.finished = true;
    list_display("Leftover finder", "A = delete (asks first), X = refresh, B = back", d, leftovers_update, leftovers_draw_top);
}
