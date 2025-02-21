#pragma once

#ifdef FS_LITTLEFS
#include <LittleFS.h>
#define myFS LittleFS
#else
#include <SPIFFS.h>
#define myFS SPIFFS
#endif 


