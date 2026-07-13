// 声明小智 MCP 响应与 Schema 共用的 cJSON 构建 helper。
#pragma once

#include "cJSON.h"

namespace xiaozhi_mcp_json {

bool add_string(cJSON *object, const char *name, const char *value);

} // namespace xiaozhi_mcp_json
