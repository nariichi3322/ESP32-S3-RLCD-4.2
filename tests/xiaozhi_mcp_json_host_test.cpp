// 验证小智 MCP 共用字符串字段 helper 的成功与判空语义。
#include "xiaozhi_mcp_json.h"

#include <assert.h>
#include <string.h>

int main()
{
    cJSON *object = cJSON_CreateObject();
    assert(object != nullptr);
    assert(xiaozhi_mcp_json::add_string(object, "name", "value"));
    const cJSON *value = cJSON_GetObjectItem(object, "name");
    assert(cJSON_IsString(value));
    assert(strcmp(value->valuestring, "value") == 0);

    assert(!xiaozhi_mcp_json::add_string(nullptr, "name", "value"));
    assert(!xiaozhi_mcp_json::add_string(object, nullptr, "value"));
    assert(!xiaozhi_mcp_json::add_string(object, "name", nullptr));
    cJSON_Delete(object);
    return 0;
}
