#pragma once

// In a helper program (the browser helper, docs/browser.md): its standard input and output carry bytes as they are.
// Windows opens them in text mode, which would turn the protocol's 0x0a bytes into 0x0d 0x0a. No SDL, so CEF's
// helper links it without the client.

namespace nmsc::platform {

void binary_stdio();

} // namespace nmsc::platform
