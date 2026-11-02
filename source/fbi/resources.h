#pragma once

#define TEXTURE_BOTTOM_SCREEN_BG 1
#define TEXTURE_BOTTOM_SCREEN_TOP_BAR 2
#define TEXTURE_BOTTOM_SCREEN_TOP_BAR_SHADOW 3
#define TEXTURE_BOTTOM_SCREEN_BOTTOM_BAR 4
#define TEXTURE_BOTTOM_SCREEN_BOTTOM_BAR_SHADOW 5
#define TEXTURE_TOP_SCREEN_BG 6
#define TEXTURE_TOP_SCREEN_TOP_BAR 7
#define TEXTURE_TOP_SCREEN_TOP_BAR_SHADOW 8
#define TEXTURE_TOP_SCREEN_BOTTOM_BAR 9
#define TEXTURE_TOP_SCREEN_BOTTOM_BAR_SHADOW 10
#define TEXTURE_LOGO 11
#define TEXTURE_SELECTION_OVERLAY 12
#define TEXTURE_SCROLL_BAR 13
#define TEXTURE_BUTTON 14
#define TEXTURE_PROGRESS_BAR_BG 15
#define TEXTURE_PROGRESS_BAR_CONTENT 16
#define TEXTURE_META_INFO_BOX 17
#define TEXTURE_META_INFO_BOX_SHADOW 18
#define TEXTURE_BATTERY_CHARGING 19
#define TEXTURE_BATTERY_0 20
#define TEXTURE_BATTERY_1 21
#define TEXTURE_BATTERY_2 22
#define TEXTURE_BATTERY_3 23
#define TEXTURE_BATTERY_4 24
#define TEXTURE_BATTERY_5 25
#define TEXTURE_WIFI_DISCONNECTED 26
#define TEXTURE_WIFI_0 27
#define TEXTURE_WIFI_1 28
#define TEXTURE_WIFI_2 29
#define TEXTURE_WIFI_3 30

#define COLOR_NAND 1
#define COLOR_SD 2
#define COLOR_GAME_CARD 3
#define COLOR_DS_TITLE 4
#define COLOR_FILE 5
#define COLOR_DIRECTORY 6
#define COLOR_ENABLED 7
#define COLOR_DISABLED 8
#define COLOR_TICKET_IN_USE 9
#define COLOR_TICKET_NOT_IN_USE 10
#define COLOR_ACCENT 11
#define COLOR_DANGER 12
#define COLOR_WARNING 13
#define COLOR_SUCCESS 14
#define COLOR_HINT 15
#define COLOR_PANEL 16
#define COLOR_BAR_TEXT 17
#define COLOR_BAR 18 /* header/footer bar fill (darker accent) */

void resources_load();

/* Built-in Light/Dark palettes. SD textcolor.cfg still wins when present. */
bool resources_sd_color_override(void);
bool resources_is_dark(void);
void resources_set_dark(bool dark); /* applies instantly unless SD override */
void resources_apply_builtin_palette(bool dark);
bool resources_dark_fill_enabled(void); /* dark chrome overlay for default BGs */

/* Accent colour for bars, selection, progress (0=Blue, 1=Green, 2=Purple, 3=Orange). */
#define RESOURCES_ACCENT_BLUE 0
#define RESOURCES_ACCENT_GREEN 1
#define RESOURCES_ACCENT_PURPLE 2
#define RESOURCES_ACCENT_ORANGE 3
u32 resources_get_accent(void);
void resources_set_accent(u32 accent);
void resources_apply_accent(void);

