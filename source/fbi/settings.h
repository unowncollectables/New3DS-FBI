#pragma once

#include <stdbool.h>

#define FTP_PASSWORD_MAX 64

void settings_init(void);
void settings_exit(void);
void settings_open(void);

bool settings_show_advanced(void);
void settings_set_show_advanced(bool show);

bool settings_advanced_warned(void);
void settings_set_advanced_warned(bool warned);

/* Optional FTP password (empty = no password). */
const char* settings_ftp_password(void);
void settings_set_ftp_password(const char* password);

/* When true, FTP screen keeps the console awake. Default on. */
bool settings_ftp_keep_awake(void);
void settings_set_ftp_keep_awake(bool keep);
