#pragma once

#include <stdbool.h>

void settings_init(void);
void settings_exit(void);
void settings_open(void);

bool settings_show_advanced(void);
void settings_set_show_advanced(bool show);

/* True after the one-time advanced warning has been acknowledged. */
bool settings_advanced_warned(void);
void settings_set_advanced_warned(bool warned);
