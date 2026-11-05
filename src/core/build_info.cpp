#include "core/build_info.h"

#include "build_info.inc" // generated: NMSC_BUILD_VERSION, NMSC_RELEASE_VERSION

namespace nmsc {

const char* build_version() { return NMSC_BUILD_VERSION; }
const char* release_version() { return NMSC_RELEASE_VERSION; }

} // namespace nmsc
