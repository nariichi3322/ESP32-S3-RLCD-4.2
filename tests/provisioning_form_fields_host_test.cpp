// 验证配网页字段别名、URL 解码、优先级和 trim 规则。
#include "provisioning_form_fields.h"

#include <assert.h>
#include <string.h>

int main()
{
    ProvisioningFormFields fields = {};
    read_provisioning_form_fields(
        "ssid=My+WiFi&pass=abc%20123&backup_ssid=Mobile+Hotspot&backup_pass=backup%20456&ntp_server=++time.example.com++&weather_city=%E6%9D%AD%E5%B7%9E",
        &fields);
    assert(strcmp(fields.ssid, "My WiFi") == 0);
    assert(strcmp(fields.pass, "abc 123") == 0);
    assert(strcmp(fields.backup_ssid, "Mobile Hotspot") == 0);
    assert(strcmp(fields.backup_pass, "backup 456") == 0);
    assert(strcmp(fields.ntp_server, "time.example.com") == 0);
    fields = {};
    read_provisioning_form_fields(
        "ssid=+Office+&password=fallback&ntp_server=+time.google.com+",
        &fields);
    assert(strcmp(fields.ssid, " Office ") == 0);
    assert(strcmp(fields.pass, "fallback") == 0);
    assert(strcmp(fields.ntp_server, "time.google.com") == 0);

    fields = {};
    read_provisioning_form_fields(
        "ssid=main&pass=primary&password=fallback&ntp_server=time.cloudflare.com",
        &fields);
    assert(strcmp(fields.pass, "primary") == 0);
    assert(strcmp(fields.ntp_server, "time.cloudflare.com") == 0);

    read_provisioning_form_fields(nullptr, &fields);
    assert(fields.ssid[0] == '\0');
    read_provisioning_form_fields("ssid=test", nullptr);
    return 0;
}
