// Firmware version strings: "0.5", "v0.5.1", "1.0-rc1". Plain C, tested on the host.
#pragma once

// Compares two versions by their numeric parts (major.minor.patch; missing parts are 0).
// A leading "v" is ignored. A pre-release suffix ("-rc1") sorts before the same version
// without it. Returns <0 if a < b, 0 if equal, >0 if a > b.
int fwversion_compare(const char *a, const char *b);
