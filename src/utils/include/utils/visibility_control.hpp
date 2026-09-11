#pragma once

#if defined _WIN32
  #ifdef UTILS_BUILDING_DLL
    #define UTILS_PUBLIC __declspec(dllexport)
  #else
    #define UTILS_PUBLIC __declspec(dllimport)
  #endif
#else
  #define UTILS_PUBLIC __attribute__ ((visibility("default")))
#endif
