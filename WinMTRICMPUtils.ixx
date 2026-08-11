module;

#include "targetver.h"

// Compatibility module retained for older project references.  The active
// tracer is implemented with the Windows 7 desktop ICMP APIs in WinMTR.Net;
// keeping this unit free of C++/WinRT avoids a Windows Runtime dependency.
export module WinMTRICMPUtils;
