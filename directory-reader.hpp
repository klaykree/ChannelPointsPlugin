#pragma once
#include <util/dstr.h>

#ifdef __cplusplus
extern "C" {
#endif
    bool GetRandomFile(const char* directory, const char* extensions, struct dstr* randomFile);
#ifdef __cplusplus
}
#endif