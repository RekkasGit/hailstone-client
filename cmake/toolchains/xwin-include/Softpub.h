// cmake/toolchains/clang-cl-xwin.sh: CEF's wrapper library includes <Softpub.h>, which the Windows SDK as xwin
// unpacks it spells softpub.h; this folder is searched after the SDK's, for case-sensitive file systems.
#pragma once
#include <softpub.h>
