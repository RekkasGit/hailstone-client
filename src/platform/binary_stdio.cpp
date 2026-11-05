#include "platform/binary_stdio.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#include <cstdio>

namespace nmsc::platform {

void binary_stdio() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

} // namespace nmsc::platform
