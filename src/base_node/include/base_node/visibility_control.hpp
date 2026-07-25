#pragma once

#include "base_node/core_defs.hpp"

#if defined CORE_WINDOWS
  #ifdef BASE_NODE_BUILDING_DLL
    #define BASE_NODE_PUBLIC __declspec(dllexport)
  #else
    #define BASE_NODE_PUBLIC __declspec(dllimport)
  #endif
#else
  #define BASE_NODE_PUBLIC __attribute__ ((visibility("default")))
#endif
