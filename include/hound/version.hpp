#pragma once

// Prefer CMake `-DHOUND_VERSION=\"${PROJECT_VERSION}\"`; fallback matches
// project(hound VERSION …) in CMakeLists.txt for IDE / non-CMake builds.
#ifndef HOUND_VERSION
#define HOUND_VERSION "0.1.0"
#endif

namespace hound {

inline constexpr const char* version_string() { return HOUND_VERSION; }

}  // namespace hound
