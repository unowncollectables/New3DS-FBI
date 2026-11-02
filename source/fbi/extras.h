#pragma once

#include <stdbool.h>
#include <3ds.h>

/* ---- Free space ---- */
void extras_init_lazy(void);

u64 extras_sd_free_bytes(void);
/* Returns true if SD has at least needBytes free (with small margin). */
bool extras_sd_has_space(u64 needBytes, u64* haveOut);
void extras_format_bytes(char* out, size_t outSize, u64 bytes);

/* ---- Battery ---- */
/* true = OK to proceed; false = user should see warning (low & not charging). */
bool extras_battery_ok_for_long_job(void);
u8 extras_battery_percent_approx(void); /* 0-5 level mapped roughly */

/* ---- Install history (sdmc:/fbi/history.txt) ---- */
void extras_history_append(const char* name, const char* result);
void extras_history_open(void); /* Recent installs screen */

/* ---- Sound on done ---- */
bool extras_sound_on_done(void);
void extras_set_sound_on_done(bool on);
void extras_play_done_sound(void);

/* ---- Sleep during install ---- */
void extras_install_begin(void); /* disable sleep, mark start time */
void extras_install_end(void);   /* restore sleep */
u32 extras_install_elapsed_seconds(void);

