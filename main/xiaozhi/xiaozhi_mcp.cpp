// 实现小智 WebSocket 上的轻量 MCP JSON-RPC 工具服务。
#include "xiaozhi_mcp.h"

#ifdef XIAOZHI_MCP_HOST_TEST
#include "xiaozhi_mcp_host_port.h"
#else
#include "app_state.h"
#include "audio_services.h"
#include "network_services.h"
#include "sensor_services.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string.h>

namespace {
constexpr const char *kMcpProtocolVersion = "2024-11-05";
constexpr const char *kMcpServerName = "ESP32-S3-RLCD-4.2";
constexpr const char *kMcpType = "mcp";
constexpr const char *kMcpJsonRpcVersion = "2.0";
constexpr const char *kMcpInitializeMethod = "initialize";
constexpr const char *kMcpToolsListMethod = "tools/list";
constexpr const char *kMcpToolsCallMethod = "tools/call";
constexpr const char *kMcpNotificationPrefix = "notifications";
constexpr const char *kDeviceStatusTool = "self.get_device_status";
constexpr const char *kSetVolumeTool = "self.audio_speaker.set_volume";
constexpr const char *kSetAlarmTool = "self.alarm.set";
constexpr const char *kDisableAlarmTool = "self.alarm.disable";
constexpr const char *kSetCountdownTool = "self.timer.set_countdown";
constexpr const char *kPomodoroControlTool = "self.pomodoro.control";
constexpr const char *kSetWeatherCityTool = "self.weather.set_city";
constexpr const char *kDeviceStatusDescription =
    "Get local temperature, humidity, battery percentage and speaker volume.";
constexpr const char *kSetVolumeDescription =
    "Set the device speaker volume from 0 to 100 percent.";
constexpr const char *kSetAlarmDescription =
    "Set or replace the one-shot alarm at the next occurrence of the supplied 24-hour local hour and minute. Requests such as tomorrow at 06:30, tonight, or this morning should call this tool directly with hour=6 and minute=30; no date lookup, date parameter, or follow-up confirmation is needed.";
constexpr const char *kDisableAlarmDescription =
    "Disable or stop the local alarm.";
constexpr const char *kSetCountdownDescription =
    "Set a local countdown reminder in seconds.";
constexpr const char *kPomodoroControlDescription =
    "Control the focus Pomodoro timer. You MUST call this tool for every request containing focus, 专注, or 番茄钟, and MUST NOT claim that a timer started or changed unless the tool returned success. "
    "Use start to create or replace it, cancel to stop it, and status to query it. "
    "Do not use alarm tools for focus or Pomodoro requests, and do not use this tool for ordinary reminders.";
constexpr const char *kSetWeatherCityDescription =
    "Set the QWeather location from the user's spoken city name. Pass the city exactly as spoken, including an optional Chinese 市 suffix; the device normalizes and validates a manual city with QWeather before saving. To restore IP-based automatic location, call this same tool with city=自动. Never claim the location changed unless the tool returned success.";
constexpr int kJsonRpcInvalidRequest = -32600;
constexpr int kJsonRpcMethodNotFound = -32601;
constexpr int kJsonRpcInvalidParams = -32602;
constexpr int kJsonRpcInternalError = -32603;
constexpr size_t kToolResultTextLen = 256;
constexpr int kHoursPerDay = 24;
constexpr int kMinutesPerHour = 60;
constexpr uint32_t kMaxCountdownSeconds = 7U * 24U * 60U * 60U;
constexpr uint32_t kDefaultPomodoroSeconds = 25U * 60U;
constexpr uint32_t kMaxPomodoroSeconds = 99U * 60U + 59U;

std::atomic<bool> s_volume_save_pending{false};
XiaozhiMcpAlarmHandler s_alarm_handler = nullptr;
XiaozhiMcpAlarmDisableHandler s_alarm_disable_handler = nullptr;
XiaozhiMcpCountdownHandler s_countdown_handler = nullptr;
XiaozhiMcpPomodoroHandler s_pomodoro_handler = nullptr;
XiaozhiMcpWeatherCityHandler s_weather_city_handler = nullptr;

struct JsonOwner {
    cJSON *value = nullptr;
    ~JsonOwner()
    {
        cJSON_Delete(value);
    }
};

bool contains_mcp_json_token(const char *message, size_t message_len)
{
    constexpr char kMcpJsonToken[] = "\"mcp\"";
    constexpr size_t kMcpJsonTokenLen = sizeof(kMcpJsonToken) - 1;
    if (!message || message_len < kMcpJsonTokenLen) {
        return false;
    }
    for (size_t offset = 0; offset <= message_len - kMcpJsonTokenLen; ++offset) {
        if (memcmp(message + offset, kMcpJsonToken, kMcpJsonTokenLen) == 0) {
            return true;
        }
    }
    return false;
}

bool add_string(cJSON *object, const char *name, const char *value)
{
    return object && name && value && cJSON_AddStringToObject(object, name, value) != nullptr;
}

cJSON *create_object_schema()
{
    cJSON *schema = cJSON_CreateObject();
    if (!schema || !add_string(schema, "type", "object") ||
        !cJSON_AddItemToObject(schema, "properties", cJSON_CreateObject())) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return schema;
}

cJSON *create_tool(const char *name, const char *description, cJSON *input_schema)
{
    if (!input_schema) {
        return nullptr;
    }
    cJSON *tool = cJSON_CreateObject();
    if (!tool || !add_string(tool, "name", name) ||
        !add_string(tool, "description", description)) {
        cJSON_Delete(tool);
        cJSON_Delete(input_schema);
        return nullptr;
    }
    cJSON_AddItemToObject(tool, "inputSchema", input_schema);
    return tool;
}

bool add_required_integer(cJSON *schema,
                          const char *name,
                          int minimum,
                          int maximum)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, "properties") : nullptr;
    cJSON *property = cJSON_CreateObject();
    cJSON *required = schema ? cJSON_GetObjectItem(schema, "required") : nullptr;
    if (!required) {
        required = cJSON_CreateArray();
        if (schema && required) {
            cJSON_AddItemToObject(schema, "required", required);
        }
    }
    if (!cJSON_IsObject(properties) || !property || !cJSON_IsArray(required) ||
        !add_string(property, "type", "integer") ||
        !cJSON_AddNumberToObject(property, "minimum", minimum) ||
        !cJSON_AddNumberToObject(property, "maximum", maximum) ||
        !cJSON_AddItemToArray(required, cJSON_CreateString(name))) {
        cJSON_Delete(property);
        return false;
    }
    cJSON_AddItemToObject(properties, name, property);
    return true;
}

bool add_optional_string(cJSON *schema, const char *name)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, "properties") : nullptr;
    cJSON *property = cJSON_CreateObject();
    if (!cJSON_IsObject(properties) || !property || !add_string(property, "type", "string")) {
        cJSON_Delete(property);
        return false;
    }
    cJSON_AddItemToObject(properties, name, property);
    return true;
}

bool add_required_string(cJSON *schema, const char *name)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, "properties") : nullptr;
    cJSON *property = cJSON_CreateObject();
    cJSON *required = schema ? cJSON_GetObjectItem(schema, "required") : nullptr;
    if (!required) {
        required = cJSON_CreateArray();
        if (schema && required) {
            cJSON_AddItemToObject(schema, "required", required);
        }
    }
    if (!cJSON_IsObject(properties) || !property || !cJSON_IsArray(required) ||
        !add_string(property, "type", "string") ||
        !cJSON_AddItemToArray(required, cJSON_CreateString(name))) {
        cJSON_Delete(property);
        return false;
    }
    cJSON_AddItemToObject(properties, name, property);
    return true;
}

bool add_optional_integer(cJSON *schema, const char *name, int minimum, int maximum)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, "properties") : nullptr;
    cJSON *property = cJSON_CreateObject();
    if (!cJSON_IsObject(properties) || !property ||
        !add_string(property, "type", "integer") ||
        !cJSON_AddNumberToObject(property, "minimum", minimum) ||
        !cJSON_AddNumberToObject(property, "maximum", maximum)) {
        cJSON_Delete(property);
        return false;
    }
    cJSON_AddItemToObject(properties, name, property);
    return true;
}

bool add_required_action(cJSON *schema)
{
    cJSON *properties = schema ? cJSON_GetObjectItem(schema, "properties") : nullptr;
    cJSON *property = cJSON_CreateObject();
    cJSON *values = cJSON_CreateArray();
    cJSON *required = cJSON_CreateArray();
    if (!cJSON_IsObject(properties) || !property || !values || !required ||
        !add_string(property, "type", "string") ||
        !cJSON_AddItemToArray(values, cJSON_CreateString("start")) ||
        !cJSON_AddItemToArray(values, cJSON_CreateString("cancel")) ||
        !cJSON_AddItemToArray(values, cJSON_CreateString("status")) ||
        !cJSON_AddItemToArray(required, cJSON_CreateString("action"))) {
        cJSON_Delete(property);
        cJSON_Delete(values);
        cJSON_Delete(required);
        return false;
    }
    cJSON_AddItemToObject(property, "enum", values);
    cJSON_AddItemToObject(properties, "action", property);
    cJSON_AddItemToObject(schema, "required", required);
    return true;
}

cJSON *create_integer_tool(const char *name,
                           const char *description,
                           const char *argument,
                           int minimum,
                           int maximum)
{
    cJSON *schema = create_object_schema();
    if (!schema || !add_required_integer(schema, argument, minimum, maximum)) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(name, description, schema);
}

cJSON *create_alarm_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema ||
        !add_required_integer(schema, "hour", 0, kHoursPerDay - 1) ||
        !add_required_integer(schema, "minute", 0, kMinutesPerHour - 1) ||
        !add_optional_string(schema, "label")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kSetAlarmTool, kSetAlarmDescription, schema);
}

cJSON *create_countdown_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema ||
        !add_required_integer(schema, "duration_seconds", 1, static_cast<int>(kMaxCountdownSeconds)) ||
        !add_optional_string(schema, "label")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kSetCountdownTool, kSetCountdownDescription, schema);
}

cJSON *create_pomodoro_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema ||
        !add_required_action(schema) ||
        !add_optional_integer(schema,
                              "duration_seconds",
                              1,
                              static_cast<int>(kMaxPomodoroSeconds))) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kPomodoroControlTool, kPomodoroControlDescription, schema);
}

cJSON *create_weather_city_tool()
{
    cJSON *schema = create_object_schema();
    if (!schema || !add_required_string(schema, "city")) {
        cJSON_Delete(schema);
        return nullptr;
    }
    return create_tool(kSetWeatherCityTool, kSetWeatherCityDescription, schema);
}

bool add_tool_if_present(cJSON *tools, cJSON *tool)
{
    return tools && tool && cJSON_AddItemToArray(tools, tool);
}

cJSON *create_tools_list_result()
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateArray();
    cJSON *status_schema = create_object_schema();
    if (!result || !tools || !status_schema ||
        !add_tool_if_present(tools, create_tool(kDeviceStatusTool,
                                                kDeviceStatusDescription,
                                                status_schema)) ||
        !add_tool_if_present(tools, create_integer_tool(kSetVolumeTool,
                                                        kSetVolumeDescription,
                                                        "volume",
                                                        0,
                                                        100)) ||
        (s_alarm_handler && !add_tool_if_present(tools, create_alarm_tool())) ||
        (s_alarm_disable_handler &&
         !add_tool_if_present(tools, create_tool(kDisableAlarmTool,
                                                 kDisableAlarmDescription,
                                                 create_object_schema()))) ||
        (s_countdown_handler && !add_tool_if_present(tools, create_countdown_tool()))) {
        cJSON_Delete(result);
        cJSON_Delete(tools);
        return nullptr;
    }
    if (s_pomodoro_handler && !add_tool_if_present(tools, create_pomodoro_tool())) {
        cJSON_Delete(result);
        cJSON_Delete(tools);
        return nullptr;
    }
    if (s_weather_city_handler && !add_tool_if_present(tools, create_weather_city_tool())) {
        cJSON_Delete(result);
        cJSON_Delete(tools);
        return nullptr;
    }
    cJSON_AddItemToObject(result, "tools", tools);
    return result;
}

cJSON *create_initialize_result()
{
    cJSON *result = cJSON_CreateObject();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON *server_info = cJSON_CreateObject();
    if (!result || !capabilities || !tools || !server_info ||
        !add_string(result, "protocolVersion", kMcpProtocolVersion) ||
        !add_string(server_info, "name", kMcpServerName) ||
        !add_string(server_info, "version", APP_VERSION)) {
        cJSON_Delete(result);
        cJSON_Delete(capabilities);
        cJSON_Delete(tools);
        cJSON_Delete(server_info);
        return nullptr;
    }
    cJSON_AddItemToObject(capabilities, "tools", tools);
    cJSON_AddItemToObject(result, "capabilities", capabilities);
    cJSON_AddItemToObject(result, "serverInfo", server_info);
    return result;
}

cJSON *create_tool_content(const char *text, bool is_error)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    if (!result || !content || !item ||
        !add_string(item, "type", "text") ||
        !add_string(item, "text", text ? text : "")) {
        cJSON_Delete(result);
        cJSON_Delete(content);
        cJSON_Delete(item);
        return nullptr;
    }
    cJSON_AddItemToArray(content, item);
    cJSON_AddItemToObject(result, "content", content);
    cJSON_AddBoolToObject(result, "isError", is_error);
    return result;
}

cJSON *create_device_status_result()
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    bool sensor_available = get_local_sensor_snapshot(&temperature, &humidity, nullptr, nullptr);
    JsonOwner status{cJSON_CreateObject()};
    if (!status.value) {
        return nullptr;
    }
    cJSON_AddBoolToObject(status.value, "sensor_available", sensor_available);
    if (sensor_available) {
        cJSON_AddNumberToObject(status.value, "temperature_c", temperature);
        cJSON_AddNumberToObject(status.value, "humidity_percent", humidity);
    } else {
        cJSON_AddNullToObject(status.value, "temperature_c");
        cJSON_AddNullToObject(status.value, "humidity_percent");
    }
    bool battery_available = g_battery_percent >= 0 && g_battery_percent <= 100;
    cJSON_AddBoolToObject(status.value, "battery_available", battery_available);
    if (battery_available) {
        cJSON_AddNumberToObject(status.value, "battery_percent", g_battery_percent);
    } else {
        cJSON_AddNullToObject(status.value, "battery_percent");
    }
    cJSON_AddNumberToObject(status.value, "volume_percent", g_chime_volume_percent);
    char *status_text = cJSON_PrintUnformatted(status.value);
    if (!status_text) {
        return nullptr;
    }
    cJSON *result = create_tool_content(status_text, false);
    cJSON_free(status_text);
    return result;
}

bool json_integer_in_range(const cJSON *object,
                           const char *name,
                           int minimum,
                           int maximum,
                           int *value)
{
    const cJSON *item = cJSON_IsObject(object) ? cJSON_GetObjectItem(object, name) : nullptr;
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble != static_cast<double>(item->valueint) ||
        item->valueint < minimum || item->valueint > maximum) {
        return false;
    }
    if (value) {
        *value = item->valueint;
    }
    return true;
}

void copy_optional_label(const cJSON *arguments, char *label, size_t label_len)
{
    if (!label || label_len == 0) {
        return;
    }
    label[0] = '\0';
    const cJSON *item = cJSON_IsObject(arguments) ? cJSON_GetObjectItem(arguments, "label") : nullptr;
    if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(label, item->valuestring, label_len);
    }
}

cJSON *call_set_volume(const cJSON *arguments)
{
    int volume = 0;
    if (!json_integer_in_range(arguments, "volume", 0, 100, &volume)) {
        return nullptr;
    }
    if (g_chime_volume_percent != volume) {
        g_chime_volume_percent = volume;
        s_volume_save_pending.store(true);
        apply_xiaozhi_speaker_volume(volume);
    }
    char result[kToolResultTextLen] = {};
    snprintf(result, sizeof(result), "{\"volume_percent\":%d}", g_chime_volume_percent);
    return create_tool_content(result, false);
}

cJSON *call_alarm(const cJSON *arguments)
{
    if (!s_alarm_handler) {
        return nullptr;
    }
    XiaozhiMcpAlarmRequest request = {};
    if (!json_integer_in_range(arguments, "hour", 0, kHoursPerDay - 1, &request.hour) ||
        !json_integer_in_range(arguments, "minute", 0, kMinutesPerHour - 1, &request.minute)) {
        return nullptr;
    }
    copy_optional_label(arguments, request.label, sizeof(request.label));
    char result[kToolResultTextLen] = {};
    return s_alarm_handler(request, result, sizeof(result))
               ? create_tool_content(result, false)
               : create_tool_content(result[0] ? result : "alarm rejected", true);
}

cJSON *call_disable_alarm()
{
    if (!s_alarm_disable_handler) {
        return nullptr;
    }
    char result[kToolResultTextLen] = {};
    return s_alarm_disable_handler(result, sizeof(result))
               ? create_tool_content(result, false)
               : create_tool_content(result[0] ? result : "alarm disable rejected", true);
}

cJSON *call_countdown(const cJSON *arguments)
{
    if (!s_countdown_handler) {
        return nullptr;
    }
    XiaozhiMcpCountdownRequest request = {};
    int seconds = 0;
    if (!json_integer_in_range(arguments,
                               "duration_seconds",
                               1,
                               static_cast<int>(kMaxCountdownSeconds),
                               &seconds)) {
        return nullptr;
    }
    request.duration_seconds = static_cast<uint32_t>(seconds);
    copy_optional_label(arguments, request.label, sizeof(request.label));
    char result[kToolResultTextLen] = {};
    return s_countdown_handler(request, result, sizeof(result))
               ? create_tool_content(result, false)
               : create_tool_content(result[0] ? result : "countdown rejected", true);
}

cJSON *call_pomodoro(const cJSON *arguments)
{
    if (!s_pomodoro_handler || !cJSON_IsObject(arguments)) {
        return nullptr;
    }
    const cJSON *action = cJSON_GetObjectItem(arguments, "action");
    const cJSON *duration = cJSON_GetObjectItem(arguments, "duration_seconds");
    if (!cJSON_IsString(action) || (duration && !cJSON_IsNumber(duration))) {
        return nullptr;
    }
    XiaozhiMcpPomodoroRequest request = {};
    if (strcmp(action->valuestring, "start") == 0) {
        request.action = kXiaozhiMcpPomodoroStart;
        request.has_duration_seconds = duration != nullptr;
        request.duration_seconds = request.has_duration_seconds
                                       ? static_cast<uint32_t>(duration->valueint)
                                       : kDefaultPomodoroSeconds;
        if (request.duration_seconds == 0 || request.duration_seconds > kMaxPomodoroSeconds ||
            (duration && duration->valuedouble != duration->valueint)) {
            return nullptr;
        }
    } else if (strcmp(action->valuestring, "cancel") == 0) {
        if (duration) {
            return nullptr;
        }
        request.action = kXiaozhiMcpPomodoroCancel;
    } else if (strcmp(action->valuestring, "status") == 0) {
        if (duration) {
            return nullptr;
        }
        request.action = kXiaozhiMcpPomodoroStatus;
    } else {
        return nullptr;
    }
    char result[kToolResultTextLen] = {};
    return s_pomodoro_handler(request, result, sizeof(result))
               ? create_tool_content(result, false)
               : create_tool_content(result[0] ? result : "pomodoro request rejected", true);
}

cJSON *call_weather_city(const cJSON *arguments)
{
    if (!s_weather_city_handler || !cJSON_IsObject(arguments)) {
        return nullptr;
    }
    XiaozhiMcpWeatherCityRequest request = {};
    const cJSON *city = cJSON_GetObjectItem(arguments, "city");
    if (!cJSON_IsString(city) || !city->valuestring || city->valuestring[0] == '\0' ||
        strlen(city->valuestring) >= sizeof(request.city)) {
        return nullptr;
    }
    strlcpy(request.city, city->valuestring, sizeof(request.city));
    char result[kToolResultTextLen] = {};
    return s_weather_city_handler(request, result, sizeof(result))
               ? create_tool_content(result, false)
               : create_tool_content(result[0] ? result : "weather city request rejected", true);
}

cJSON *create_tool_call_result(const cJSON *params,
                               bool allow_alarm_disable,
                               int *error_code,
                               const char **error_message)
{
    const cJSON *name = cJSON_IsObject(params) ? cJSON_GetObjectItem(params, "name") : nullptr;
    const cJSON *arguments = cJSON_IsObject(params) ? cJSON_GetObjectItem(params, "arguments") : nullptr;
    if (!cJSON_IsString(name) || (arguments && !cJSON_IsObject(arguments))) {
        *error_code = kJsonRpcInvalidParams;
        *error_message = "Invalid tool parameters";
        return nullptr;
    }
    if (strcmp(name->valuestring, kDeviceStatusTool) == 0) {
        return create_device_status_result();
    }
    if (strcmp(name->valuestring, kSetVolumeTool) == 0) {
        cJSON *result = call_set_volume(arguments);
        if (!result) {
            *error_code = kJsonRpcInvalidParams;
            *error_message = "volume must be an integer from 0 to 100";
        }
        return result;
    }
    if (strcmp(name->valuestring, kSetAlarmTool) == 0 && s_alarm_handler) {
        cJSON *result = call_alarm(arguments);
        if (!result) {
            *error_code = kJsonRpcInvalidParams;
            *error_message = "Invalid alarm parameters";
        }
        return result;
    }
    if (strcmp(name->valuestring, kDisableAlarmTool) == 0 && s_alarm_disable_handler) {
        if (!allow_alarm_disable) {
            return create_tool_content("alarm unchanged while exiting Xiaozhi", false);
        }
        return call_disable_alarm();
    }
    if (strcmp(name->valuestring, kSetCountdownTool) == 0 && s_countdown_handler) {
        cJSON *result = call_countdown(arguments);
        if (!result) {
            *error_code = kJsonRpcInvalidParams;
            *error_message = "Invalid countdown parameters";
        }
        return result;
    }
    if (strcmp(name->valuestring, kPomodoroControlTool) == 0 && s_pomodoro_handler) {
        cJSON *result = call_pomodoro(arguments);
        if (!result) {
            *error_code = kJsonRpcInvalidParams;
            *error_message = "Invalid pomodoro parameters";
        }
        return result;
    }
    if (strcmp(name->valuestring, kSetWeatherCityTool) == 0 && s_weather_city_handler) {
        cJSON *result = call_weather_city(arguments);
        if (!result) {
            *error_code = kJsonRpcInvalidParams;
            *error_message = "city must be a non-empty string";
        }
        return result;
    }
    *error_code = kJsonRpcMethodNotFound;
    *error_message = "Unknown tool";
    return nullptr;
}

bool write_response(const char *session_id,
                    const cJSON *request_id,
                    cJSON *result,
                    int error_code,
                    const char *error_message,
                    char *response,
                    size_t response_len)
{
    JsonOwner root{cJSON_CreateObject()};
    cJSON *payload = cJSON_CreateObject();
    if (!root.value || !payload || !response || response_len == 0 ||
        !add_string(root.value, "session_id", session_id ? session_id : "") ||
        !add_string(root.value, "type", kMcpType) ||
        !add_string(payload, "jsonrpc", kMcpJsonRpcVersion)) {
        cJSON_Delete(payload);
        cJSON_Delete(result);
        return false;
    }
    cJSON *id_copy = cJSON_Duplicate(request_id, true);
    if (!id_copy) {
        cJSON_Delete(payload);
        cJSON_Delete(result);
        return false;
    }
    cJSON_AddItemToObject(payload, "id", id_copy);
    if (result) {
        cJSON_AddItemToObject(payload, "result", result);
    } else {
        cJSON *error = cJSON_CreateObject();
        if (!error || !cJSON_AddNumberToObject(error, "code", error_code) ||
            !add_string(error, "message", error_message ? error_message : "MCP error")) {
            cJSON_Delete(error);
            cJSON_Delete(payload);
            return false;
        }
        cJSON_AddItemToObject(payload, "error", error);
    }
    cJSON_AddItemToObject(root.value, "payload", payload);
    response[0] = '\0';
    return cJSON_PrintPreallocated(root.value,
                                   response,
                                   static_cast<int>(response_len),
                                   false);
}
} // namespace

bool xiaozhi_mcp_message_calls_weather_city(const char *message, size_t message_len)
{
    if (!message || message_len == 0 || !contains_mcp_json_token(message, message_len)) {
        return false;
    }
    JsonOwner request{cJSON_ParseWithLength(message, message_len)};
    const cJSON *type = request.value ? cJSON_GetObjectItem(request.value, "type") : nullptr;
    const cJSON *payload = cJSON_IsString(type) && strcmp(type->valuestring, kMcpType) == 0
                               ? cJSON_GetObjectItem(request.value, "payload")
                               : nullptr;
    const cJSON *method = cJSON_IsObject(payload) ? cJSON_GetObjectItem(payload, "method") : nullptr;
    const cJSON *params = cJSON_IsObject(payload) ? cJSON_GetObjectItem(payload, "params") : nullptr;
    const cJSON *name = cJSON_IsObject(params) ? cJSON_GetObjectItem(params, "name") : nullptr;
    return cJSON_IsString(method) && strcmp(method->valuestring, kMcpToolsCallMethod) == 0 &&
           cJSON_IsString(name) && strcmp(name->valuestring, kSetWeatherCityTool) == 0;
}

XiaozhiMcpMessageResult xiaozhi_mcp_handle_message(const char *message,
                                                   size_t message_len,
                                                   const char *session_id,
                                                   char *response,
                                                   size_t response_len,
                                                   bool allow_alarm_disable)
{
    if (!message || message_len == 0) {
        return kXiaozhiMcpNotHandled;
    }
    // 普通 STT/TTS/LLM 文本占绝大多数，先做有界 token 筛选，避免每帧
    // 在 MCP 和原会话处理器中重复分配并解析两棵 cJSON 树。
    if (!contains_mcp_json_token(message, message_len)) {
        return kXiaozhiMcpNotHandled;
    }
    JsonOwner request{cJSON_ParseWithLength(message, message_len)};
    const cJSON *type = request.value ? cJSON_GetObjectItem(request.value, "type") : nullptr;
    if (!cJSON_IsString(type) || strcmp(type->valuestring, kMcpType) != 0) {
        return kXiaozhiMcpNotHandled;
    }
    if (response && response_len > 0) {
        response[0] = '\0';
    }
    const cJSON *payload = cJSON_GetObjectItem(request.value, "payload");
    const cJSON *version = cJSON_IsObject(payload) ? cJSON_GetObjectItem(payload, "jsonrpc") : nullptr;
    const cJSON *method = cJSON_IsObject(payload) ? cJSON_GetObjectItem(payload, "method") : nullptr;
    if (!cJSON_IsString(version) || strcmp(version->valuestring, kMcpJsonRpcVersion) != 0 ||
        !cJSON_IsString(method)) {
        return kXiaozhiMcpHandledWithoutResponse;
    }
    if (strncmp(method->valuestring,
                kMcpNotificationPrefix,
                strlen(kMcpNotificationPrefix)) == 0) {
        return kXiaozhiMcpHandledWithoutResponse;
    }
    const cJSON *id = cJSON_GetObjectItem(payload, "id");
    if (!cJSON_IsNumber(id) && !cJSON_IsString(id)) {
        return kXiaozhiMcpHandledWithoutResponse;
    }
    const cJSON *params = cJSON_GetObjectItem(payload, "params");
    cJSON *result = nullptr;
    int error_code = kJsonRpcInvalidRequest;
    const char *error_message = "Invalid MCP request";
    if (strcmp(method->valuestring, kMcpInitializeMethod) == 0) {
        result = create_initialize_result();
    } else if (strcmp(method->valuestring, kMcpToolsListMethod) == 0) {
        result = create_tools_list_result();
    } else if (strcmp(method->valuestring, kMcpToolsCallMethod) == 0) {
        result = create_tool_call_result(params,
                                         allow_alarm_disable,
                                         &error_code,
                                         &error_message);
    } else {
        error_code = kJsonRpcMethodNotFound;
        error_message = "Method not implemented";
    }
    if (!result && error_code == kJsonRpcInvalidRequest) {
        error_code = kJsonRpcInternalError;
        error_message = "MCP response allocation failed";
    }
    return write_response(session_id,
                          id,
                          result,
                          error_code,
                          error_message,
                          response,
                          response_len)
               ? kXiaozhiMcpHandledWithResponse
               : kXiaozhiMcpHandledWithoutResponse;
}

bool xiaozhi_mcp_volume_save_pending()
{
    return s_volume_save_pending.load();
}

bool xiaozhi_mcp_flush_pending_settings()
{
    if (!s_volume_save_pending.load()) {
        return true;
    }
    if (!save_hourly_chime_setting()) {
        return false;
    }
    s_volume_save_pending.store(false);
    return true;
}

void xiaozhi_mcp_register_alarm_handler(XiaozhiMcpAlarmHandler handler)
{
    s_alarm_handler = handler;
}

void xiaozhi_mcp_register_alarm_disable_handler(XiaozhiMcpAlarmDisableHandler handler)
{
    s_alarm_disable_handler = handler;
}

void xiaozhi_mcp_register_countdown_handler(XiaozhiMcpCountdownHandler handler)
{
    s_countdown_handler = handler;
}

void xiaozhi_mcp_register_pomodoro_handler(XiaozhiMcpPomodoroHandler handler)
{
    s_pomodoro_handler = handler;
}

void xiaozhi_mcp_register_weather_city_handler(XiaozhiMcpWeatherCityHandler handler)
{
    s_weather_city_handler = handler;
}
