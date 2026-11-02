#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "pathprotect.h"

static void path_normalize_lower(char* dst, size_t dstSize, const char* src) {
    size_t j = 0;
    if(src == NULL) {
        if(dstSize) dst[0] = '\0';
        return;
    }
    if(strncasecmp(src, "sdmc:", 5) == 0) {
        src += 5;
    }
    while(*src && j + 1 < dstSize) {
        char c = *src++;
        if(c == '\\') c = '/';
        dst[j++] = (char) tolower((unsigned char) c);
    }
    dst[j] = '\0';
}

bool path_is_critical_sd(const char* path) {
    char n[512];
    path_normalize_lower(n, sizeof(n), path);
    if(n[0] == '\0') return false;

    if(strcmp(n, "/boot.firm") == 0 || strcmp(n, "boot.firm") == 0) return true;
    if(strcmp(n, "/boot.3dsx") == 0 || strcmp(n, "boot.3dsx") == 0) return true;

    if(strcmp(n, "/luma") == 0 || strcmp(n, "luma") == 0) return true;
    if(strncmp(n, "/luma/", 6) == 0 || strncmp(n, "luma/", 5) == 0) return true;

    if(strcmp(n, "/nintendo 3ds") == 0 || strcmp(n, "nintendo 3ds") == 0) return true;
    if(strncmp(n, "/nintendo 3ds/", 14) == 0 || strncmp(n, "nintendo 3ds/", 13) == 0) return true;

    return false;
}
