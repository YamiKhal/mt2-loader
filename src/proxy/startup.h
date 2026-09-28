#ifndef PROXY_STARTUP_H
#define PROXY_STARTUP_H

#include <windows.h>

void startup_attach(HMODULE proxy_module);
void startup_detach(void);

#endif
