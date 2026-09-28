#ifndef CORE_PLUGIN_API_H
#define CORE_PLUGIN_API_H

#include "../../sdk/include/mt2loader.h"
#include "../include/mt2loader_host.h"
#include "game_build.h"

void plugin_api_setup(const Mt2LoaderHost* host, const GameBuild* build);
void plugin_api_load_symbols(void);
void plugin_api_start_saved_data(void);
void plugin_api_fill(PluginApi* api, const char* mod_id, const wchar_t* mod_folder);

#endif
