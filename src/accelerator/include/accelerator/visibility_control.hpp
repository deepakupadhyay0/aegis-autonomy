#pragma once

#if defined _WIN32 || defined __CYGWIN__
  #ifdef ACCELERATOR_BUILDING_DLL
    #ifdef __GNUC__
      #define ACCELERATOR_PUBLIC __attribute__ ((dllexport))
    #else
      #define ACCELERATOR_PUBLIC __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define ACCELERATOR_PUBLIC __attribute__ ((dllimport))
    #else
      #define ACCELERATOR_PUBLIC __declspec(dllimport)
    #endif
  #endif
#else
  #define ACCELERATOR_PUBLIC __attribute__ ((visibility("default")))
#endif
