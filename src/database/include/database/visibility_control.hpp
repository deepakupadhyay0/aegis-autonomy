#pragma once

#if defined _WIN32 || defined __CYGWIN__
  #ifdef DATABASE_BUILDING_DLL
    #define DATABASE_PUBLIC __declspec(dllexport)
  #else
    #define DATABASE_PUBLIC __declspec(dllimport)
  #endif
#else
  #define DATABASE_PUBLIC __attribute__((visibility("default")))
#endif
