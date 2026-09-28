#ifndef CORE_MANIFEST_H
#define CORE_MANIFEST_H

#include <stddef.h>

#define MANIFEST_MAX_PLUGINS 16
#define MANIFEST_MAX_BUILDS 16
#define MANIFEST_ID_CAPACITY 32
#define MANIFEST_PATH_CAPACITY 260
#define MANIFEST_PROBLEM_CAPACITY 200

typedef struct ModManifest {
    char id[MANIFEST_ID_CAPACITY];
    int plugin_count;
    char plugins[MANIFEST_MAX_PLUGINS][MANIFEST_PATH_CAPACITY];
    int build_count;
    char builds[MANIFEST_MAX_BUILDS][MANIFEST_ID_CAPACITY];
    char problem[MANIFEST_PROBLEM_CAPACITY];
} ModManifest;

typedef enum ManifestResult {
    MANIFEST_NO_PLUGINS,
    MANIFEST_HAS_PLUGINS,
    MANIFEST_INVALID,
} ManifestResult;

ManifestResult manifest_parse(const char* text, ModManifest* manifest);

#endif
