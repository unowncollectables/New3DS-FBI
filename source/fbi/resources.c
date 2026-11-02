#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "../core/core.h"

static bool g_sd_color_override = false;
static bool g_dark = false;
static u32 g_accent = RESOURCES_ACCENT_BLUE;

static FILE* resources_open_file(const char* path) {
    char realPath[FILE_PATH_MAX];
    snprintf(realPath, sizeof(realPath), "sdmc:/fbi/theme/%s", path);

    FILE* fd = fopen(realPath, "rb");

    if(fd != NULL) {
        return fd;
    } else {
        snprintf(realPath, sizeof(realPath), "romfs:/%s", path);

        return fopen(realPath, "rb");
    }
}

static bool resources_sd_file_exists(const char* path) {
    char realPath[FILE_PATH_MAX];
    snprintf(realPath, sizeof(realPath), "sdmc:/fbi/theme/%s", path);
    FILE* fd = fopen(realPath, "rb");
    if(fd != NULL) {
        fclose(fd);
        return true;
    }
    return false;
}

static void resources_load_texture(u32 id, const char* name) {
    FILE* fd = resources_open_file(name);
    if(fd == NULL) {
        error_panic("Failed to open texture \"%s\": %s\n", name, strerror(errno));
        return;
    }

    screen_load_texture_file(id, fd, true);

    fclose(fd);
}

static void resources_load_colors_from_file(FILE* fd) {
    char line[128];
    while(fgets(line, sizeof(line), fd) != NULL) {
        char key[64];
        u32 color = 0;

        sscanf(line, "%63[^=]=%lx", key, &color);

        if(strcasecmp(key, "text") == 0) {
            screen_set_color(COLOR_TEXT, color);
        } else if(strcasecmp(key, "nand") == 0) {
            screen_set_color(COLOR_NAND, color);
        } else if(strcasecmp(key, "sd") == 0) {
            screen_set_color(COLOR_SD, color);
        } else if(strcasecmp(key, "gamecard") == 0) {
            screen_set_color(COLOR_GAME_CARD, color);
        } else if(strcasecmp(key, "dstitle") == 0) {
            screen_set_color(COLOR_DS_TITLE, color);
        } else if(strcasecmp(key, "file") == 0) {
            screen_set_color(COLOR_FILE, color);
        } else if(strcasecmp(key, "directory") == 0) {
            screen_set_color(COLOR_DIRECTORY, color);
        } else if(strcasecmp(key, "enabled") == 0) {
            screen_set_color(COLOR_ENABLED, color);
        } else if(strcasecmp(key, "disabled") == 0) {
            screen_set_color(COLOR_DISABLED, color);
        } else if(strcasecmp(key, "ticketinuse") == 0) {
            screen_set_color(COLOR_TICKET_IN_USE, color);
        } else if(strcasecmp(key, "ticketnotinuse") == 0) {
            screen_set_color(COLOR_TICKET_NOT_IN_USE, color);
        } else if(strcasecmp(key, "accent") == 0) {
            screen_set_color(COLOR_ACCENT, color);
        } else if(strcasecmp(key, "danger") == 0) {
            screen_set_color(COLOR_DANGER, color);
        } else if(strcasecmp(key, "warning") == 0) {
            screen_set_color(COLOR_WARNING, color);
        } else if(strcasecmp(key, "success") == 0) {
            screen_set_color(COLOR_SUCCESS, color);
        } else if(strcasecmp(key, "hint") == 0) {
            screen_set_color(COLOR_HINT, color);
        } else if(strcasecmp(key, "panel") == 0) {
            screen_set_color(COLOR_PANEL, color);
        } else if(strcasecmp(key, "bartext") == 0) {
            screen_set_color(COLOR_BAR_TEXT, color);
        }
    }
}

void resources_apply_builtin_palette(bool dark) {
    if(dark) {
        /* Near-black chrome, light text, readable accents (AARRGGBB). */
        screen_set_color(COLOR_TEXT, 0xFFF5F5F5);
        screen_set_color(COLOR_BAR_TEXT, 0xFFFFFFFF);
        screen_set_color(COLOR_NAND, 0xFFEF5350);
        screen_set_color(COLOR_SD, 0xFF66BB6A);
        screen_set_color(COLOR_GAME_CARD, 0xFFFFA726);
        screen_set_color(COLOR_DS_TITLE, 0xFFCE93D8);
        screen_set_color(COLOR_FILE, 0xFFCFD8DC);
        screen_set_color(COLOR_DIRECTORY, 0xFF64B5F6);
        screen_set_color(COLOR_ENABLED, 0xFF81C784);
        screen_set_color(COLOR_DISABLED, 0xFF78909C);
        screen_set_color(COLOR_TICKET_IN_USE, 0xFF81C784);
        screen_set_color(COLOR_TICKET_NOT_IN_USE, 0xFFEF5350);
        screen_set_color(COLOR_ACCENT, 0xFF42A5F5);
        screen_set_color(COLOR_DANGER, 0xFFEF5350);
        screen_set_color(COLOR_WARNING, 0xFFFFB74D);
        screen_set_color(COLOR_SUCCESS, 0xFF66BB6A);
        screen_set_color(COLOR_HINT, 0xFF90A4AE);
        screen_set_color(COLOR_PANEL, 0xF01E1E1E);
    } else {
        /* Match romfs/textcolor.cfg light theme. */
        screen_set_color(COLOR_TEXT, 0xFF212121);
        screen_set_color(COLOR_BAR_TEXT, 0xFFFFFFFF);
        screen_set_color(COLOR_NAND, 0xFFC62828);
        screen_set_color(COLOR_SD, 0xFF2E7D32);
        screen_set_color(COLOR_GAME_CARD, 0xFFE65100);
        screen_set_color(COLOR_DS_TITLE, 0xFF6A1B9A);
        screen_set_color(COLOR_FILE, 0xFF37474F);
        screen_set_color(COLOR_DIRECTORY, 0xFF1565C0);
        screen_set_color(COLOR_ENABLED, 0xFF2E7D32);
        screen_set_color(COLOR_DISABLED, 0xFF90A4AE);
        screen_set_color(COLOR_TICKET_IN_USE, 0xFF2E7D32);
        screen_set_color(COLOR_TICKET_NOT_IN_USE, 0xFFC62828);
        screen_set_color(COLOR_ACCENT, 0xFF1E88E5);
        screen_set_color(COLOR_DANGER, 0xFFC62828);
        screen_set_color(COLOR_WARNING, 0xFFE65100);
        screen_set_color(COLOR_SUCCESS, 0xFF2E7D32);
        screen_set_color(COLOR_HINT, 0xFF546E7A);
        screen_set_color(COLOR_PANEL, 0xF2FFFFFF);
    }
}


void resources_apply_accent(void) {
    /* Bright = selection/progress/highlights; dark = header/footer bars. AARRGGBB. */
    u32 bright;
    u32 darkBar;
    switch(g_accent) {
        case RESOURCES_ACCENT_GREEN:
            bright = 0xFF22C55E;
            darkBar = 0xFF15803D;
            break;
        case RESOURCES_ACCENT_PURPLE:
            bright = 0xFFA855F7;
            darkBar = 0xFF7E22CE;
            break;
        case RESOURCES_ACCENT_ORANGE:
            bright = 0xFFF97316;
            darkBar = 0xFFC2410C;
            break;
        case RESOURCES_ACCENT_BLUE:
        default:
            bright = g_dark ? 0xFF42A5F5 : 0xFF1E88E5;
            darkBar = g_dark ? 0xFF1E88E5 : 0xFF1565C0;
            break;
    }
    if(!g_sd_color_override) {
        screen_set_color(COLOR_ACCENT, bright);
    } else {
        /* Still drive selection/progress accent when user picks a colour. */
        screen_set_color(COLOR_ACCENT, bright);
    }
    screen_set_color(COLOR_BAR, darkBar);
}

u32 resources_get_accent(void) {
    return g_accent;
}

void resources_set_accent(u32 accent) {
    if(accent > RESOURCES_ACCENT_ORANGE) {
        accent = RESOURCES_ACCENT_BLUE;
    }
    g_accent = accent;
    resources_apply_accent();
}

bool resources_sd_color_override(void) {
    return g_sd_color_override;
}

bool resources_is_dark(void) {
    return g_dark;
}

bool resources_dark_fill_enabled(void) {
    /* Dark fill only when dark mode is on and the user has not dropped a custom BG on SD. */
    return g_dark && !resources_sd_file_exists("bottom_screen_bg.png") && !resources_sd_file_exists("top_screen_bg.png");
}

void resources_set_dark(bool dark) {
    g_dark = dark;
    if(!g_sd_color_override) {
        resources_apply_builtin_palette(dark);
    }
    resources_apply_accent();
}

void resources_load() {
    g_sd_color_override = resources_sd_file_exists("textcolor.cfg");

    if(g_sd_color_override) {
        FILE* fd = resources_open_file("textcolor.cfg");
        if(fd != NULL) {
            resources_load_colors_from_file(fd);
            fclose(fd);
        }
    } else {
        /* Built-in Light/Dark — g_dark may already be set by settings_init. */
        resources_apply_builtin_palette(g_dark);
    }
    resources_apply_accent();

    resources_load_texture(TEXTURE_BOTTOM_SCREEN_BG, "bottom_screen_bg.png");
    resources_load_texture(TEXTURE_BOTTOM_SCREEN_TOP_BAR, "bottom_screen_top_bar.png");
    resources_load_texture(TEXTURE_BOTTOM_SCREEN_TOP_BAR_SHADOW, "bottom_screen_top_bar_shadow.png");
    resources_load_texture(TEXTURE_BOTTOM_SCREEN_BOTTOM_BAR, "bottom_screen_bottom_bar.png");
    resources_load_texture(TEXTURE_BOTTOM_SCREEN_BOTTOM_BAR_SHADOW, "bottom_screen_bottom_bar_shadow.png");
    resources_load_texture(TEXTURE_TOP_SCREEN_BG, "top_screen_bg.png");
    resources_load_texture(TEXTURE_TOP_SCREEN_TOP_BAR, "top_screen_top_bar.png");
    resources_load_texture(TEXTURE_TOP_SCREEN_TOP_BAR_SHADOW, "top_screen_top_bar_shadow.png");
    resources_load_texture(TEXTURE_TOP_SCREEN_BOTTOM_BAR, "top_screen_bottom_bar.png");
    resources_load_texture(TEXTURE_TOP_SCREEN_BOTTOM_BAR_SHADOW, "top_screen_bottom_bar_shadow.png");
    resources_load_texture(TEXTURE_LOGO, "logo.png");
    resources_load_texture(TEXTURE_SELECTION_OVERLAY, "selection_overlay.png");
    resources_load_texture(TEXTURE_SCROLL_BAR, "scroll_bar.png");
    resources_load_texture(TEXTURE_BUTTON, "button.png");
    resources_load_texture(TEXTURE_PROGRESS_BAR_BG, "progress_bar_bg.png");
    resources_load_texture(TEXTURE_PROGRESS_BAR_CONTENT, "progress_bar_content.png");
    resources_load_texture(TEXTURE_META_INFO_BOX, "meta_info_box.png");
    resources_load_texture(TEXTURE_META_INFO_BOX_SHADOW, "meta_info_box_shadow.png");
    resources_load_texture(TEXTURE_BATTERY_CHARGING, "battery_charging.png");
    resources_load_texture(TEXTURE_BATTERY_0, "battery0.png");
    resources_load_texture(TEXTURE_BATTERY_1, "battery1.png");
    resources_load_texture(TEXTURE_BATTERY_2, "battery2.png");
    resources_load_texture(TEXTURE_BATTERY_3, "battery3.png");
    resources_load_texture(TEXTURE_BATTERY_4, "battery4.png");
    resources_load_texture(TEXTURE_BATTERY_5, "battery5.png");
    resources_load_texture(TEXTURE_WIFI_DISCONNECTED, "wifi_disconnected.png");
    resources_load_texture(TEXTURE_WIFI_0, "wifi0.png");
    resources_load_texture(TEXTURE_WIFI_1, "wifi1.png");
    resources_load_texture(TEXTURE_WIFI_2, "wifi2.png");
    resources_load_texture(TEXTURE_WIFI_3, "wifi3.png");
}
