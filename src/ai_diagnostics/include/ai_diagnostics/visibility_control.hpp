#pragma once

#if defined _WIN32 || defined __CYGWIN__
  #ifdef AI_DIAGNOSTICS_BUILDING_DLL
    #define AI_DIAGNOSTICS_PUBLIC __declspec(dllexport)
  #else
    #define AI_DIAGNOSTICS_PUBLIC __declspec(dllimport)
  #endif
#else
  #define AI_DIAGNOSTICS_PUBLIC __attribute__((visibility("default")))
#endif
