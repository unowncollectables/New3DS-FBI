#pragma once

#include <stdbool.h>

/* Paths on sdmc that must not be casually deleted/renamed/overwritten. */
bool path_is_critical_sd(const char* path);
