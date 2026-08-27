// Defines the persisted NTP server contract used by setup and SNTP.
#pragma once

#include <stddef.h>

inline constexpr size_t kNtpServerNameLen = 96;
inline constexpr const char *kDefaultNtpServerName = "pool.ntp.org";

// Accepts a DNS host name or IPv4-style host text. Schemes and paths are not
// accepted because ESP-IDF SNTP expects a server name, not an HTTP URL.
bool normalize_ntp_server_name(const char *input,
                               char *out,
                               size_t out_len);

