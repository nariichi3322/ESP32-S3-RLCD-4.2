// 声明小智设备标识格式化和官方激活 HTTP 请求入口。
#pragma once

#include <stddef.h>

inline constexpr size_t kXiaozhiActivationResponseSize = 3072;
inline constexpr size_t kXiaozhiDeviceIdSize = 18;

struct XiaozhiActivationResponse {
    char data[kXiaozhiActivationResponseSize] = {};
    size_t len = 0;
};

void xiaozhi_format_device_id(char *out, size_t out_len);
bool xiaozhi_request_activation(XiaozhiActivationResponse *response);
