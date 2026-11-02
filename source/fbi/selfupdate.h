#pragma once

#include <3ds.h>
#include <stdbool.h>

/* Own CIA title (UniqueId 0xF83FB). */
#define SELFUPDATE_TITLE_ID 0x000400000F83FB00ULL

#define SELFUPDATE_TAG_MAX 32
#define SELFUPDATE_BODY_MAX 1200
#define SELFUPDATE_URL_MAX 1024

void selfupdate_init(void);
void selfupdate_exit(void);

/* Non-blocking: spawn background check if enabled. Never blocks startup. */
void selfupdate_start_background_check(void);

bool selfupdate_is_available(void);
bool selfupdate_check_enabled(void);
void selfupdate_set_check_enabled(bool enabled);

const char* selfupdate_latest_tag(void);
const char* selfupdate_display_tag(void);
const char* selfupdate_body(void);
const char* selfupdate_cia_url(void);

void selfupdate_skip_latest(void);
bool selfupdate_is_skipped(void);

/* Open the non-pushy update screen (changelog / install / toggles). */
void selfupdate_open(void);
