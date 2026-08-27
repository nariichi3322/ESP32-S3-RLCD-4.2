// Verifies NTP host-name normalization and rejection of URL/path input.
#include "ntp_server_config.h"

#include <assert.h>
#include <string.h>

int main()
{
    char server[kNtpServerNameLen] = {};
    assert(normalize_ntp_server_name("  pool.ntp.org  ",
                                     server,
                                     sizeof(server)));
    assert(strcmp(server, "pool.ntp.org") == 0);
    assert(normalize_ntp_server_name("192.0.2.10", server, sizeof(server)));
    assert(!normalize_ntp_server_name("https://pool.ntp.org",
                                      server,
                                      sizeof(server)));
    assert(!normalize_ntp_server_name("pool.ntp.org/path",
                                      server,
                                      sizeof(server)));
    assert(!normalize_ntp_server_name("bad..example", server, sizeof(server)));
    assert(!normalize_ntp_server_name("-bad.example", server, sizeof(server)));
    assert(!normalize_ntp_server_name("bad_.example", server, sizeof(server)));
    assert(!normalize_ntp_server_name("", server, sizeof(server)));
    assert(!normalize_ntp_server_name(kDefaultNtpServerName,
                                      server,
                                      kNtpServerNameLen - 1));
    return 0;
}
