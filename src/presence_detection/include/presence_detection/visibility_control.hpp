#pragma once

#include "base_node/core_defs.hpp"

#if defined CORE_WINDOWS
  #ifdef PRESENCE_DETECTION_BUILDING_DLL
    #define PRESENCE_DETECTION_PUBLIC __declspec(dllexport)
  #else
    #define PRESENCE_DETECTION_PUBLIC __declspec(dllimport)
  #endif
#else
  #define PRESENCE_DETECTION_PUBLIC __attribute__ ((visibility("default")))
#endif
