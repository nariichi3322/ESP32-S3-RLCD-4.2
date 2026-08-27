// Product-level feature policy for the local clock firmware variant.
#pragma once

// This variant exposes only local clock pages. Network access is reserved for
// Settings Mode and bounded NTP synchronization windows.
inline constexpr bool kLocalClockNtpOnlyMode = true;
