#ifndef PROXY_SESSION_H
#define PROXY_SESSION_H

#include <stdbool.h>
#include <wchar.h>

#define SESSION_CRASHES_BEFORE_SAFE_MODE 2

typedef struct SessionHistory {
    bool previous_session_crashed;
    bool previous_session_had_core;
    int crash_streak;
} SessionHistory;

void session_begin(const wchar_t* loader_dir, SessionHistory* history);
void session_mark_core_loaded(void);
void session_mark_clean_exit(void);

#endif
