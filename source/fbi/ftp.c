#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include <3ds.h>

#include "ftp.h"
#include "resources.h"
#include "section.h"
#include "settings.h"
#include "../core/core.h"

#define FTP_PASV_PORT_BASE 5001
#define FTP_IO_BUF (256 * 1024)
#define FTP_CMD_BUF 1024
#define FTP_ROOT "sdmc:"

typedef struct {
    volatile bool running;
    volatile bool stopReq;
    Thread thread;
    int listenFd;
    int clientFd;
    int pasvListenFd;
    int dataFd;
    u16 pasvPort;
    char cwd[FTP_PATH_MAX]; /* absolute under sdmc:, always starts with / */
    char rnfr[FTP_PATH_MAX];
    char status[FTP_STATUS_MAX];
    char transferName[256];
    float transferProgress;
    ftp_state_t state;
    bool binary;
    bool loggedIn;
    bool userOk;
    char userName[64];
    LightLock lock;
} ftp_server;

static ftp_server g_ftp;
static bool g_ftp_inited = false;
static bool g_prev_sleep_allowed = true;

static void ftp_set_state(ftp_state_t st, const char* status) {
    LightLock_Lock(&g_ftp.lock);
    g_ftp.state = st;
    if(status != NULL) {
        string_copy(g_ftp.status, status, sizeof(g_ftp.status));
    }
    LightLock_Unlock(&g_ftp.lock);
}

static void ftp_close_fd(int* fd) {
    if(fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void ftp_close_data(void) {
    ftp_close_fd(&g_ftp.dataFd);
    ftp_close_fd(&g_ftp.pasvListenFd);
}

/* Resolve client path into absolute /... under SD; reject escapes. */
static bool ftp_resolve_path(const char* arg, char* out, size_t outSize) {
    char combined[FTP_PATH_MAX * 2];
    if(arg == NULL || arg[0] == '\0') {
        string_copy(combined, g_ftp.cwd, sizeof(combined));
    } else if(arg[0] == '/') {
        string_copy(combined, arg, sizeof(combined));
    } else {
        if(strcmp(g_ftp.cwd, "/") == 0) {
            snprintf(combined, sizeof(combined), "/%s", arg);
        } else {
            snprintf(combined, sizeof(combined), "%s/%s", g_ftp.cwd, arg);
        }
    }

    /* Normalize . and .. */
    char parts[64][96];
    int nparts = 0;
    const char* p = combined;
    while(*p) {
        while(*p == '/') p++;
        if(*p == '\0') break;
        char seg[96];
        size_t i = 0;
        while(*p && *p != '/' && i + 1 < sizeof(seg)) {
            seg[i++] = *p++;
        }
        seg[i] = '\0';
        if(strcmp(seg, ".") == 0) {
            continue;
        }
        if(strcmp(seg, "..") == 0) {
            if(nparts > 0) nparts--;
            continue;
        }
        if(nparts + 1 >= 64) {
            return false;
        }
        string_copy(parts[nparts], seg, sizeof(parts[nparts]));
        nparts++;
    }

    char norm[FTP_PATH_MAX];
    if(nparts == 0) {
        string_copy(norm, "/", sizeof(norm));
    } else {
        norm[0] = '\0';
        for(int i = 0; i < nparts; i++) {
            size_t used = strlen(norm);
            snprintf(norm + used, sizeof(norm) - used, "/%s", parts[i]);
        }
    }

    /* Build sdmc path */
    if(strcmp(norm, "/") == 0) {
        snprintf(out, outSize, "%s/", FTP_ROOT);
    } else {
        snprintf(out, outSize, "%s%s", FTP_ROOT, norm);
    }

    /* Store absolute virtual path without sdmc: for cwd */
    return true;
}

static void ftp_virtual_from_sdmc(const char* sdmcPath, char* out, size_t outSize) {
    if(strncmp(sdmcPath, FTP_ROOT, strlen(FTP_ROOT)) == 0) {
        const char* rest = sdmcPath + strlen(FTP_ROOT);
        if(*rest == '\0') {
            string_copy(out, "/", outSize);
        } else {
            string_copy(out, rest, outSize);
        }
    } else {
        string_copy(out, "/", outSize);
    }
}

static bool ftp_send_ctrl(const char* line) {
    if(g_ftp.clientFd < 0) return false;
    char buf[FTP_CMD_BUF];
    snprintf(buf, sizeof(buf), "%s\r\n", line);
    size_t len = strlen(buf);
    size_t off = 0;
    while(off < len) {
        ssize_t n = send(g_ftp.clientFd, buf + off, len - off, 0);
        if(n <= 0) return false;
        off += (size_t) n;
    }
    return true;
}

static bool ftp_recv_line(char* out, size_t outSize) {
    size_t n = 0;
    while(n + 1 < outSize) {
        char c;
        ssize_t r = recv(g_ftp.clientFd, &c, 1, 0);
        if(r <= 0) return false;
        if(c == '\r') continue;
        if(c == '\n') break;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

static int ftp_accept_pasv(void) {
    if(g_ftp.pasvListenFd < 0) return -1;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int fd = accept(g_ftp.pasvListenFd, (struct sockaddr*) &addr, &len);
    ftp_close_fd(&g_ftp.pasvListenFd);
    if(fd >= 0) {
        int buf = FTP_IO_BUF;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    }
    return fd;
}

static bool ftp_open_pasv(void) {
    ftp_close_data();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return false;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = gethostid();
    /* Try a few ports */
    bool bound = false;
    for(u16 p = FTP_PASV_PORT_BASE; p < FTP_PASV_PORT_BASE + 32; p++) {
        addr.sin_port = htons(p);
        if(bind(fd, (struct sockaddr*) &addr, sizeof(addr)) == 0) {
            g_ftp.pasvPort = p;
            bound = true;
            break;
        }
    }
    if(!bound || listen(fd, 1) != 0) {
        close(fd);
        return false;
    }
    g_ftp.pasvListenFd = fd;
    return true;
}

static void ftp_reply_pasv(void) {
    u32 ip = (u32) gethostid();
    u8 a = ip & 0xFF, b = (ip >> 8) & 0xFF, c = (ip >> 16) & 0xFF, d = (ip >> 24) & 0xFF;
    u16 p = g_ftp.pasvPort;
    char line[96];
    snprintf(line, sizeof(line), "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u)",
             a, b, c, d, (unsigned) (p >> 8), (unsigned) (p & 0xFF));
    ftp_send_ctrl(line);
}

static void ftp_list_dir(const char* sdmcPath, bool mlsd) {
    g_ftp.dataFd = ftp_accept_pasv();
    if(g_ftp.dataFd < 0) {
        ftp_send_ctrl("425 Can't open data connection");
        return;
    }
    ftp_send_ctrl("150 Opening data connection");

    DIR* dir = opendir(sdmcPath);
    if(dir == NULL) {
        /* maybe a file */
        struct stat st;
        if(stat(sdmcPath, &st) == 0 && S_ISREG(st.st_mode)) {
            const char* name = strrchr(sdmcPath, '/');
            name = name ? name + 1 : sdmcPath;
            char line[512];
            if(mlsd) {
                snprintf(line, sizeof(line), "type=file;size=%lld; %s\r\n", (long long) st.st_size, name);
            } else {
                snprintf(line, sizeof(line), "-rw-r--r-- 1 3ds 3ds %lld Jan 1 00:00 %s\r\n", (long long) st.st_size, name);
            }
            send(g_ftp.dataFd, line, strlen(line), 0);
        }
        ftp_close_fd(&g_ftp.dataFd);
        ftp_send_ctrl("226 Transfer complete");
        return;
    }

    struct dirent* ent;
    while((ent = readdir(dir)) != NULL) {
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[FTP_PATH_MAX];
        if(sdmcPath[strlen(sdmcPath) - 1] == '/') {
            snprintf(full, sizeof(full), "%s%s", sdmcPath, ent->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", sdmcPath, ent->d_name);
        }
        struct stat st;
        if(stat(full, &st) != 0) continue;
        char line[640];
        if(mlsd) {
            if(S_ISDIR(st.st_mode)) {
                snprintf(line, sizeof(line), "type=dir; %s\r\n", ent->d_name);
            } else {
                snprintf(line, sizeof(line), "type=file;size=%lld; %s\r\n", (long long) st.st_size, ent->d_name);
            }
        } else {
            if(S_ISDIR(st.st_mode)) {
                snprintf(line, sizeof(line), "drwxr-xr-x 1 3ds 3ds 0 Jan 1 00:00 %s\r\n", ent->d_name);
            } else {
                snprintf(line, sizeof(line), "-rw-r--r-- 1 3ds 3ds %lld Jan 1 00:00 %s\r\n", (long long) st.st_size, ent->d_name);
            }
        }
        send(g_ftp.dataFd, line, strlen(line), 0);
    }
    closedir(dir);
    ftp_close_fd(&g_ftp.dataFd);
    ftp_send_ctrl("226 Transfer complete");
}

static void ftp_retr(const char* sdmcPath) {
    FILE* f = fopen(sdmcPath, g_ftp.binary ? "rb" : "r");
    if(f == NULL) {
        ftp_send_ctrl("550 Failed to open file");
        ftp_close_data();
        return;
    }
    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(total < 0) total = 0;

    g_ftp.dataFd = ftp_accept_pasv();
    if(g_ftp.dataFd < 0) {
        fclose(f);
        ftp_send_ctrl("425 Can't open data connection");
        return;
    }

    const char* name = strrchr(sdmcPath, '/');
    name = name ? name + 1 : sdmcPath;
    LightLock_Lock(&g_ftp.lock);
    string_copy(g_ftp.transferName, name, sizeof(g_ftp.transferName));
    g_ftp.transferProgress = 0;
    g_ftp.state = FTP_STATE_TRANSFER;
    snprintf(g_ftp.status, sizeof(g_ftp.status), "Sending %s…", name);
    LightLock_Unlock(&g_ftp.lock);

    ftp_send_ctrl("150 Opening data connection");

    char* buf = (char*) malloc(FTP_IO_BUF);
    if(buf == NULL) {
        fclose(f);
        ftp_close_fd(&g_ftp.dataFd);
        ftp_send_ctrl("451 Out of memory");
        ftp_set_state(FTP_STATE_CONNECTED, "Connected");
        return;
    }

    long sent = 0;
    while(!g_ftp.stopReq) {
        size_t n = fread(buf, 1, FTP_IO_BUF, f);
        if(n == 0) break;
        size_t off = 0;
        while(off < n) {
            ssize_t w = send(g_ftp.dataFd, buf + off, n - off, 0);
            if(w <= 0) { free(buf); fclose(f); ftp_close_fd(&g_ftp.dataFd); ftp_send_ctrl("426 Connection closed"); ftp_set_state(FTP_STATE_CONNECTED, "Connected"); return; }
            off += (size_t) w;
        }
        sent += (long) n;
        LightLock_Lock(&g_ftp.lock);
        g_ftp.transferProgress = total > 0 ? (float) sent / (float) total : 1.0f;
        LightLock_Unlock(&g_ftp.lock);
    }
    free(buf);
    fclose(f);
    ftp_close_fd(&g_ftp.dataFd);
    ftp_send_ctrl("226 Transfer complete");
    ftp_set_state(FTP_STATE_CONNECTED, "Connected — transfer done");

}

static void ftp_stor(const char* sdmcPath) {
    FILE* f = fopen(sdmcPath, g_ftp.binary ? "wb" : "w");
    if(f == NULL) {
        ftp_send_ctrl("550 Failed to create file");
        ftp_close_data();
        return;
    }

    g_ftp.dataFd = ftp_accept_pasv();
    if(g_ftp.dataFd < 0) {
        fclose(f);
        ftp_send_ctrl("425 Can't open data connection");
        return;
    }

    const char* name = strrchr(sdmcPath, '/');
    name = name ? name + 1 : sdmcPath;
    LightLock_Lock(&g_ftp.lock);
    string_copy(g_ftp.transferName, name, sizeof(g_ftp.transferName));
    g_ftp.transferProgress = 0;
    g_ftp.state = FTP_STATE_TRANSFER;
    snprintf(g_ftp.status, sizeof(g_ftp.status), "Receiving %s…", name);
    LightLock_Unlock(&g_ftp.lock);

    ftp_send_ctrl("150 Opening data connection");

    char* buf = (char*) malloc(FTP_IO_BUF);
    if(buf == NULL) {
        fclose(f);
        ftp_close_fd(&g_ftp.dataFd);
        ftp_send_ctrl("451 Out of memory");
        ftp_set_state(FTP_STATE_CONNECTED, "Connected");
        return;
    }

    long got = 0;
    while(!g_ftp.stopReq) {
        ssize_t n = recv(g_ftp.dataFd, buf, FTP_IO_BUF, 0);
        if(n < 0) { free(buf); fclose(f); unlink(sdmcPath); ftp_close_fd(&g_ftp.dataFd); ftp_send_ctrl("426 Connection closed"); ftp_set_state(FTP_STATE_CONNECTED, "Connected"); return; }
        if(n == 0) break;
        if(fwrite(buf, 1, (size_t) n, f) != (size_t) n) {
            free(buf); fclose(f); ftp_close_fd(&g_ftp.dataFd); ftp_send_ctrl("452 Write failed"); ftp_set_state(FTP_STATE_CONNECTED, "Connected"); return;
        }
        got += (long) n;
        LightLock_Lock(&g_ftp.lock);
        /* Unknown total size for STOR — pulse based on MB received */
        g_ftp.transferProgress = (float) ((got / (1024 * 1024)) % 100) / 100.0f;
        snprintf(g_ftp.status, sizeof(g_ftp.status), "Receiving %s (%ld KB)…", name, got / 1024);
        LightLock_Unlock(&g_ftp.lock);
    }
    free(buf);
    fclose(f);
    ftp_close_fd(&g_ftp.dataFd);
    ftp_send_ctrl("226 Transfer complete");
    ftp_set_state(FTP_STATE_CONNECTED, "Connected — transfer done");

}

static bool ftp_password_required(void) {
    const char* pw = settings_ftp_password();
    return pw != NULL && pw[0] != '\0';
}

static bool ftp_password_matches(const char* pass) {
    const char* pw = settings_ftp_password();
    if(pw == NULL || pw[0] == '\0') return true;
    return pass != NULL && strcmp(pw, pass) == 0;
}

static void ftp_handle_client(void) {
    g_ftp.binary = true;
    g_ftp.loggedIn = !ftp_password_required();
    g_ftp.userOk = false;
    g_ftp.userName[0] = '\0';
    g_ftp.rnfr[0] = '\0';
    string_copy(g_ftp.cwd, "/", sizeof(g_ftp.cwd));

    ftp_set_state(FTP_STATE_CONNECTED, "Someone connected from your PC");
    ftp_send_ctrl("220 New3DS FBI FTP (SD card only)");

    char line[FTP_CMD_BUF];
    while(!g_ftp.stopReq && ftp_recv_line(line, sizeof(line))) {
        char cmd[16];
        char arg[FTP_CMD_BUF];
        arg[0] = '\0';
        cmd[0] = '\0';
        sscanf(line, "%15s %1023[^\n]", cmd, arg);
        for(char* c = cmd; *c; c++) *c = (char) toupper((unsigned char) *c);

        if(strcmp(cmd, "QUIT") == 0) {
            ftp_send_ctrl("221 Bye");
            break;
        }
        if(strcmp(cmd, "NOOP") == 0) { ftp_send_ctrl("200 OK"); continue; }
        if(strcmp(cmd, "SYST") == 0) { ftp_send_ctrl("215 UNIX Type: L8"); continue; }
        if(strcmp(cmd, "FEAT") == 0) {
            ftp_send_ctrl("211-Features:");
            ftp_send_ctrl(" PASV");
            ftp_send_ctrl(" SIZE");
            ftp_send_ctrl(" UTF8");
            ftp_send_ctrl(" MLSD");
            ftp_send_ctrl("211 End");
            continue;
        }
        if(strcmp(cmd, "OPTS") == 0) { ftp_send_ctrl("200 OK"); continue; }
        if(strcmp(cmd, "USER") == 0) {
            string_copy(g_ftp.userName, arg, sizeof(g_ftp.userName));
            g_ftp.userOk = true;
            if(!ftp_password_required()) {
                g_ftp.loggedIn = true;
                ftp_send_ctrl("230 Logged in (no password set)");
            } else {
                ftp_send_ctrl("331 Password required");
            }
            continue;
        }
        if(strcmp(cmd, "PASS") == 0) {
            if(!g_ftp.userOk) { ftp_send_ctrl("503 Login with USER first"); continue; }
            if(ftp_password_matches(arg)) {
                g_ftp.loggedIn = true;
                ftp_send_ctrl("230 Logged in");
            } else {
                g_ftp.loggedIn = false;
                ftp_send_ctrl("530 Wrong password");
            }
            continue;
        }

        if(!g_ftp.loggedIn) {
            ftp_send_ctrl("530 Please login");
            continue;
        }

        if(strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
            char resp[FTP_PATH_MAX + 32];
            snprintf(resp, sizeof(resp), "257 \"%s\"", g_ftp.cwd);
            ftp_send_ctrl(resp);
            continue;
        }
        if(strcmp(cmd, "TYPE") == 0) {
            if(arg[0] == 'I' || arg[0] == 'i') { g_ftp.binary = true; ftp_send_ctrl("200 Type set to I"); }
            else if(arg[0] == 'A' || arg[0] == 'a') { g_ftp.binary = false; ftp_send_ctrl("200 Type set to A"); }
            else ftp_send_ctrl("504 Type not supported");
            continue;
        }
        if(strcmp(cmd, "PASV") == 0 || strcmp(cmd, "EPSV") == 0) {
            if(!ftp_open_pasv()) { ftp_send_ctrl("425 Can't open passive port"); continue; }
            ftp_reply_pasv();
            continue;
        }
        if(strcmp(cmd, "CWD") == 0 || strcmp(cmd, "XCWD") == 0) {
            char sd[FTP_PATH_MAX];
            char virt[FTP_PATH_MAX];
            if(!ftp_resolve_path(arg, sd, sizeof(sd))) { ftp_send_ctrl("550 Bad path"); continue; }
            ftp_virtual_from_sdmc(sd, virt, sizeof(virt));
            DIR* d = opendir(sd);
            if(d == NULL) { ftp_send_ctrl("550 Not a folder"); continue; }
            closedir(d);
            string_copy(g_ftp.cwd, virt, sizeof(g_ftp.cwd));
            ftp_send_ctrl("250 OK");
            continue;
        }
        if(strcmp(cmd, "CDUP") == 0 || strcmp(cmd, "XCUP") == 0) {
            char sd[FTP_PATH_MAX];
            char virt[FTP_PATH_MAX];
            ftp_resolve_path("..", sd, sizeof(sd));
            ftp_virtual_from_sdmc(sd, virt, sizeof(virt));
            string_copy(g_ftp.cwd, virt, sizeof(g_ftp.cwd));
            ftp_send_ctrl("250 OK");
            continue;
        }
        if(strcmp(cmd, "LIST") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg[0] ? arg : NULL, sd, sizeof(sd));
            ftp_list_dir(sd, false);
            continue;
        }
        if(strcmp(cmd, "MLSD") == 0 || strcmp(cmd, "MLST") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg[0] ? arg : NULL, sd, sizeof(sd));
            ftp_list_dir(sd, true);
            continue;
        }
        if(strcmp(cmd, "SIZE") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            struct stat st;
            if(stat(sd, &st) != 0 || !S_ISREG(st.st_mode)) { ftp_send_ctrl("550 Not a file"); continue; }
            char resp[64];
            snprintf(resp, sizeof(resp), "213 %lld", (long long) st.st_size);
            ftp_send_ctrl(resp);
            continue;
        }
        if(strcmp(cmd, "RETR") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            ftp_retr(sd);
            continue;
        }
        if(strcmp(cmd, "STOR") == 0 || strcmp(cmd, "APPE") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            ftp_stor(sd);
            continue;
        }
        if(strcmp(cmd, "DELE") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            if(unlink(sd) != 0) ftp_send_ctrl("550 Delete failed");
            else ftp_send_ctrl("250 Deleted");
            continue;
        }
        if(strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            if(mkdir(sd, 0777) != 0) ftp_send_ctrl("550 Mkdir failed");
            else {
                char resp[FTP_PATH_MAX + 32];
                char virt[FTP_PATH_MAX];
                ftp_virtual_from_sdmc(sd, virt, sizeof(virt));
                snprintf(resp, sizeof(resp), "257 \"%s\" created", virt);
                ftp_send_ctrl(resp);
            }
            continue;
        }
        if(strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            if(rmdir(sd) != 0) ftp_send_ctrl("550 Rmdir failed");
            else ftp_send_ctrl("250 Removed");
            continue;
        }
        if(strcmp(cmd, "RNFR") == 0) {
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            string_copy(g_ftp.rnfr, sd, sizeof(g_ftp.rnfr));
            ftp_send_ctrl("350 Ready for RNTO");
            continue;
        }
        if(strcmp(cmd, "RNTO") == 0) {
            if(g_ftp.rnfr[0] == '\0') { ftp_send_ctrl("503 RNFR first"); continue; }
            char sd[FTP_PATH_MAX];
            ftp_resolve_path(arg, sd, sizeof(sd));
            if(rename(g_ftp.rnfr, sd) != 0) ftp_send_ctrl("550 Rename failed");
            else ftp_send_ctrl("250 Renamed");
            g_ftp.rnfr[0] = '\0';
            continue;
        }

        ftp_send_ctrl("502 Command not implemented");
    }

    ftp_close_data();
    ftp_close_fd(&g_ftp.clientFd);
    if(!g_ftp.stopReq) {
        ftp_set_state(FTP_STATE_WAITING, "Waiting for your PC…");
    }
}

static void ftp_thread(void* arg) {
    (void) arg;
    ftp_set_state(FTP_STATE_WAITING, "Waiting for your PC…");

    while(!g_ftp.stopReq) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_ftp.listenFd, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        int sel = select(g_ftp.listenFd + 1, &fds, NULL, NULL, &tv);
        if(sel < 0) {
            if(g_ftp.stopReq) break;
            svcSleepThread(50 * 1000 * 1000ULL);
            continue;
        }
        if(sel == 0) continue;
        if(!FD_ISSET(g_ftp.listenFd, &fds)) continue;

        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int cfd = accept(g_ftp.listenFd, (struct sockaddr*) &addr, &len);
        if(cfd < 0) continue;

        int buf = FTP_IO_BUF;
        setsockopt(cfd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
        setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
        g_ftp.clientFd = cfd;
        ftp_handle_client();
    }

    ftp_close_data();
    ftp_close_fd(&g_ftp.clientFd);
    ftp_close_fd(&g_ftp.listenFd);
    g_ftp.running = false;
    ftp_set_state(FTP_STATE_STOPPED, "Stopped");
    g_ftp.thread = NULL;
}

void ftp_init(void) {
    if(g_ftp_inited) return;
    memset(&g_ftp, 0, sizeof(g_ftp));
    g_ftp.listenFd = -1;
    g_ftp.clientFd = -1;
    g_ftp.pasvListenFd = -1;
    g_ftp.dataFd = -1;
    LightLock_Init(&g_ftp.lock);
    string_copy(g_ftp.status, "Stopped", sizeof(g_ftp.status));
    g_ftp_inited = true;
}

void ftp_exit(void) {
    ftp_stop();
    g_ftp_inited = false;
}

bool ftp_start(void) {
    if(!g_ftp_inited) ftp_init();
    if(g_ftp.running) return true;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        ftp_set_state(FTP_STATE_ERROR, "Could not open network");
        return false;
    }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    int buf = FTP_IO_BUF;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(FTP_PORT);
    if(bind(fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        close(fd);
        ftp_set_state(FTP_STATE_ERROR, "Could not listen on port 5000");
        return false;
    }

    g_ftp.listenFd = fd;
    g_ftp.stopReq = false;
    g_ftp.running = true;
    string_copy(g_ftp.cwd, "/", sizeof(g_ftp.cwd));
    g_ftp.thread = threadCreate(ftp_thread, NULL, 0x10000, 0x18, 1, true);
    if(g_ftp.thread == NULL) {
        ftp_close_fd(&g_ftp.listenFd);
        g_ftp.running = false;
        ftp_set_state(FTP_STATE_ERROR, "Could not start FTP thread");
        return false;
    }
    ftp_set_state(FTP_STATE_WAITING, "Waiting for your PC…");
    return true;
}

void ftp_stop(void) {
    if(!g_ftp.running && g_ftp.thread == NULL) {
        ftp_close_data();
        ftp_close_fd(&g_ftp.clientFd);
        ftp_close_fd(&g_ftp.listenFd);
        ftp_set_state(FTP_STATE_STOPPED, "Stopped");
        return;
    }
    g_ftp.stopReq = true;
    /* Wake accept/select */
    ftp_close_fd(&g_ftp.listenFd);
    ftp_close_fd(&g_ftp.clientFd);
    ftp_close_data();
    /* Detached thread exits on its own; wait briefly */
    for(int i = 0; i < 50 && g_ftp.running; i++) {
        svcSleepThread(20 * 1000 * 1000ULL);
    }
    g_ftp.running = false;
    ftp_set_state(FTP_STATE_STOPPED, "Stopped");
}

bool ftp_is_running(void) {
    return g_ftp.running;
}

ftp_state_t ftp_get_state(void) {
    LightLock_Lock(&g_ftp.lock);
    ftp_state_t st = g_ftp.state;
    LightLock_Unlock(&g_ftp.lock);
    return st;
}

void ftp_get_status_text(char* out, size_t outSize) {
    LightLock_Lock(&g_ftp.lock);
    string_copy(out, g_ftp.status, outSize);
    LightLock_Unlock(&g_ftp.lock);
}

float ftp_get_transfer_progress(void) {
    LightLock_Lock(&g_ftp.lock);
    float p = g_ftp.transferProgress;
    LightLock_Unlock(&g_ftp.lock);
    return p;
}

const char* ftp_get_transfer_name(void) {
    return g_ftp.transferName;
}

void ftp_get_url(char* out, size_t outSize) {
    u32 ip = (u32) gethostid();
    snprintf(out, outSize, "ftp://%lu.%lu.%lu.%lu:%d",
             (unsigned long) (ip & 0xFF),
             (unsigned long) ((ip >> 8) & 0xFF),
             (unsigned long) ((ip >> 16) & 0xFF),
             (unsigned long) ((ip >> 24) & 0xFF),
             FTP_PORT);
}

/* ---- UI ---- */

static void ftp_ui_update(ui_view* view, void* data, float* progress, char* text) {
    (void) data;
    (void) progress;
    (void) text;

    if(hidKeysDown() & KEY_B) {
        ftp_stop();
        if(settings_ftp_keep_awake()) {
            aptSetSleepAllowed(g_prev_sleep_allowed);
        }
        ui_pop();
        info_destroy(view);
        return;
    }

    /* X toggles keep-awake while on this screen */
    if(hidKeysDown() & KEY_X) {
        bool next = !settings_ftp_keep_awake();
        settings_set_ftp_keep_awake(next);
        aptSetSleepAllowed(!next);
    }
}

static void ftp_ui_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    (void) data;

    char url[96];
    ftp_get_url(url, sizeof(url));

    float w, h;
    screen_get_string_size(&w, &h, url, 0.7f, 0.7f);
    float y = y1 + 18.0f;
    screen_draw_string(url, x1 + (x2 - x1 - w) / 2, y, 0.7f, 0.7f, COLOR_ACCENT, true);
    y += h + 14.0f;

    const char* steps =
        "1. On your PC, open File Explorer or FileZilla.\n"
        "2. Paste the address above.\n"
        "3. You will see your SD card files.\n"
        "4. Copy files in or out — SD only.";
    screen_get_string_size_wrap(&w, &h, steps, 0.48f, 0.48f, x2 - x1 - 24.0f);
    screen_draw_string_wrap(steps, x1 + 12.0f, y, 0.48f, 0.48f, COLOR_TEXT, true, x2 - 12.0f);

    const char* warn = "Only use on your home Wi-Fi. Do not use on public networks.";
    screen_get_string_size_wrap(&w, &h, warn, 0.42f, 0.42f, x2 - x1 - 24.0f);
    screen_draw_string_wrap(warn, x1 + 12.0f, y2 - h - 8.0f, 0.42f, 0.42f, COLOR_WARNING, true, x2 - 12.0f);
}

static void ftp_ui_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    (void) view;
    (void) data;

    char status[FTP_STATUS_MAX];
    ftp_get_status_text(status, sizeof(status));
    ftp_state_t st = ftp_get_state();

    float w, h;
    const char* title = "Send files from your PC";
    screen_get_string_size(&w, &h, title, 0.6f, 0.6f);
    float y = y1 + 12.0f;
    screen_draw_string(title, x1 + (x2 - x1 - w) / 2, y, 0.6f, 0.6f, COLOR_TEXT, true);
    y += h + 16.0f;

    u32 statusColor = COLOR_HINT;
    if(st == FTP_STATE_CONNECTED) statusColor = COLOR_SUCCESS;
    if(st == FTP_STATE_TRANSFER) statusColor = COLOR_ACCENT;
    if(st == FTP_STATE_ERROR) statusColor = COLOR_DANGER;

    screen_get_string_size_wrap(&w, &h, status, 0.55f, 0.55f, x2 - x1 - 20.0f);
    screen_draw_string_wrap(status, x1 + 10.0f, y, 0.55f, 0.55f, statusColor, true, x2 - 10.0f);
    y += h + 14.0f;

    if(st == FTP_STATE_TRANSFER) {
        float barW = (x2 - x1) - 24.0f;
        float barX = x1 + 12.0f;
        screen_draw_progress_bar(barX, y, barW, 18.0f, ftp_get_transfer_progress(), COLOR_HINT, COLOR_ACCENT);
        y += 26.0f;
    }

    char awake[80];
    snprintf(awake, sizeof(awake), "Keep screen awake: %s  (X = toggle)",
             settings_ftp_keep_awake() ? "On" : "Off");
    screen_get_string_size(&w, &h, awake, 0.45f, 0.45f);
    screen_draw_string(awake, x1 + (x2 - x1 - w) / 2, y2 - h - 36.0f, 0.45f, 0.45f, COLOR_HINT, true);

    const char* hint = "B = stop and go back";
    screen_get_string_size(&w, &h, hint, 0.5f, 0.5f);
    screen_draw_string(hint, x1 + (x2 - x1 - w) / 2, y2 - h - 10.0f, 0.5f, 0.5f, COLOR_TEXT, true);
}

void ftp_open(void) {
    settings_init();
    ftp_init();

    if(settings_ftp_keep_awake()) {
        g_prev_sleep_allowed = true;
        aptSetSleepAllowed(false);
    }

    if(!ftp_start()) {
        prompt_display_notify(
            "Could not start FTP",
            "Check that Wi-Fi is on,\nthen try again.\n\nB will go back.",
            COLOR_TEXT, NULL, NULL, NULL);
        if(settings_ftp_keep_awake()) {
            aptSetSleepAllowed(g_prev_sleep_allowed);
        }
        return;
    }

    /* Custom info view with top+bottom draw */
    ui_view* view = info_display("FTP", "B = stop", false, NULL, ftp_ui_update, ftp_ui_draw_top);
    if(view != NULL) {
        view->drawBottom = ftp_ui_draw_bottom;
    }
}
