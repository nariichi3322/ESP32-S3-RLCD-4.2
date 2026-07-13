// 实现小智 MCP 响应与 Schema 共用的 cJSON 字符串字段写入。
#include "xiaozhi_mcp_json.h"

namespace xiaozhi_mcp_json {

bool add_string(cJSON *object, const char *name, const char *value)
{
    return object && name && value && cJSON_AddStringToObject(object, name, value) != nullptr;
}

} // namespace xiaozhi_mcp_json
