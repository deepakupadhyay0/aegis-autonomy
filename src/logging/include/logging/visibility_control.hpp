#pragma once

#if defined _WIN32 || defined __CYGWIN__
  #ifdef __GNUC__
    #define LOGGING_EXPORT __attribute__((dllexport))
    #define LOGGING_IMPORT __attribute__((dllimport))
  #else
    #define LOGGING_EXPORT __declspec(dllexport)
    #define LOGGING_IMPORT __declspec(dllimport)
  #endif
  #ifdef LOGGING_BUILDING_DLL
    #define LOGGING_PUBLIC LOGGING_EXPORT
  #else
    #define LOGGING_PUBLIC LOGGING_IMPORT
  #endif
#else
  #define LOGGING_EXPORT __attribute__((visibility("default")))
  #define LOGGING_IMPORT
  #if __GNUC__ >= 4
    #define LOGGING_PUBLIC __attribute__((visibility("default")))
  #else
    #define LOGGING_PUBLIC
  #endif
#endif
