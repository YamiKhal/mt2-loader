#include <string.h>

static char write_directory[1024] = "";


__declspec(dllexport) int PHYSFS_setWriteDir(const char* directory) {
    strncpy(write_directory, directory, sizeof write_directory - 1);

    return 1;
}

__declspec(dllexport) const char* PHYSFS_getWriteDir(void) {
    return write_directory[0] != '\0' ? write_directory : NULL;
}

__declspec(dllexport) const char* PHYSFS_getBaseDir(void) {
    return "base/";
}
