#pragma once

#include <stdbool.h>
#include <3ds.h>

#define FTP_PORT 5000
#define FTP_STATUS_MAX 160
#define FTP_PATH_MAX 512

typedef enum {
    FTP_STATE_STOPPED = 0,
    FTP_STATE_STARTING,
    FTP_STATE_WAITING,
    FTP_STATE_CONNECTED,
    FTP_STATE_TRANSFER,
    FTP_STATE_ERROR
} ftp_state_t;

void ftp_init(void);
void ftp_exit(void);

/* Start listening (SD-only). Safe to call if already running. */
bool ftp_start(void);
/* Stop listening and close clients. */
void ftp_stop(void);
bool ftp_is_running(void);

ftp_state_t ftp_get_state(void);
void ftp_get_status_text(char* out, size_t outSize);
void ftp_get_url(char* out, size_t outSize); /* ftp://ip:5000 */
float ftp_get_transfer_progress(void); /* 0..1 while transferring */
const char* ftp_get_transfer_name(void);

/* UI entry: starts server, shows big URL, B stops+exits. Never auto-starts elsewhere. */
void ftp_open(void);
