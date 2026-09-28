#ifndef PROXY_LOG_H
#define PROXY_LOG_H

#include <stdbool.h>
#include <wchar.h>

bool log_open(const wchar_t* loader_dir);
void log_line(const char* format, ...);
void log_close(void);

#endif
