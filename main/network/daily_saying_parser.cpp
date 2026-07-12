// 实现每日文字 JSON/纯文本解析，不包含 HTTP、缓存或任务状态。
#include "daily_saying_parser.h"

#include "network_text.h"

#include "cJSON.h"
#include <string.h>

namespace daily_saying_parser {
namespace {

constexpr int kMaxJsonDepth = 8;
constexpr unsigned char kUtf8ContinuationMask = 0xC0;
constexpr unsigned char kUtf8ContinuationPrefix = 0x80;
constexpr char kJsonObjectStart = '{';
constexpr char kJsonArrayStart = '[';
constexpr size_t kCStringTerminatorSize = 1;
constexpr const char *const kJsonFields[] = {
    "content",
    "saying",
    "text",
    "sentence",
    "hitokoto",
    "quote",
    "data",
};

template <typename T, size_t N>
constexpr size_t array_count(const T (&)[N])
{
    return N;
}

constexpr bool cstr_nonempty(const char *text)
{
    return text && text[0] != '\0';
}

constexpr bool output_buffer_available(char *out, size_t out_len)
{
    return out && out_len > 0;
}

constexpr bool json_fields_nonempty()
{
    for (const char *field : kJsonFields) {
        if (!cstr_nonempty(field)) {
            return false;
        }
    }
    return true;
}

class JsonRoot {
public:
    explicit JsonRoot(const char *response)
        : root_(cJSON_Parse(response))
    {
    }

    ~JsonRoot()
    {
        cJSON_Delete(root_);
    }

    JsonRoot(const JsonRoot &) = delete;
    JsonRoot &operator=(const JsonRoot &) = delete;

    cJSON *get() const
    {
        return root_;
    }

    explicit operator bool() const
    {
        return root_ != nullptr;
    }

private:
    cJSON *root_;
};

void copy_cstr(char *out, size_t out_len, const char *text)
{
    const size_t source_len = strlen(text);
    const size_t copy_len = source_len < out_len - kCStringTerminatorSize
                                ? source_len
                                : out_len - kCStringTerminatorSize;
    memcpy(out, text, copy_len);
    out[copy_len] = '\0';
}

bool copy_trimmed_text(const char *text, char *out, size_t out_len)
{
    if (!text || !output_buffer_available(out, out_len)) {
        return false;
    }
    copy_cstr(out, out_len, text);
    trim_ascii(out);
    return out[0] != '\0';
}

bool plain_text_candidate(const char *text)
{
    return text && text[0] != '\0' && text[0] != kJsonObjectStart &&
           text[0] != kJsonArrayStart;
}

bool copy_plain_text_candidate(const char *response, char *out, size_t out_len)
{
    if (!response || !output_buffer_available(out, out_len)) {
        return false;
    }
    copy_cstr(out, out_len, response);
    trim_ascii(out);
    return plain_text_candidate(out);
}

bool copy_json_field(cJSON *obj, char *out, size_t out_len, int depth)
{
    if (!obj || !output_buffer_available(out, out_len) || depth > kMaxJsonDepth) {
        return false;
    }
    if (cJSON_IsString(obj) && obj->valuestring) {
        return copy_trimmed_text(obj->valuestring, out, out_len);
    }
    if (!cJSON_IsObject(obj)) {
        return false;
    }
    for (const char *field : kJsonFields) {
        cJSON *item = cJSON_GetObjectItem(obj, field);
        if (!item) {
            continue;
        }
        if (cJSON_IsString(item) && item->valuestring) {
            return copy_trimmed_text(item->valuestring, out, out_len);
        }
        if (cJSON_IsObject(item) && copy_json_field(item, out, out_len, depth + 1)) {
            return true;
        }
    }
    return false;
}

} // namespace

static_assert(array_count(kJsonFields) > 0,
              "daily saying JSON field table must not be empty");
static_assert(json_fields_nonempty(), "daily saying JSON fields must be non-empty");
static_assert(kMaxChars > 0, "daily saying character limit must be positive");
static_assert(kMaxJsonDepth >= 0, "daily saying JSON search depth must be non-negative");
static_assert(kUtf8ContinuationMask == 0xC0,
              "UTF-8 continuation mask must cover two high bits");
static_assert(kUtf8ContinuationPrefix == 0x80,
              "UTF-8 continuation prefix must match 10xxxxxx bytes");
static_assert(kJsonObjectStart != kJsonArrayStart,
              "JSON object and array sentinels must differ");
static_assert(kCStringTerminatorSize == 1,
              "daily saying parser terminator reservation must be one byte");

bool extract(const char *response, char *out, size_t out_len)
{
    if (!output_buffer_available(out, out_len)) {
        return false;
    }
    out[0] = '\0';
    if (!response) {
        return false;
    }
    JsonRoot root(response);
    if (root && copy_json_field(root.get(), out, out_len, 0)) {
        return true;
    }
    if (copy_plain_text_candidate(response, out, out_len)) {
        return true;
    }
    out[0] = '\0';
    return false;
}

int utf8_char_count(const char *text)
{
    if (!text) {
        return 0;
    }
    int count = 0;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
    while (*p) {
        if ((*p & kUtf8ContinuationMask) != kUtf8ContinuationPrefix) {
            ++count;
        }
        ++p;
    }
    return count;
}

bool within_length(const char *text, int *chars_out)
{
    const int chars = utf8_char_count(text);
    if (chars_out) {
        *chars_out = chars;
    }
    return chars <= kMaxChars;
}

} // namespace daily_saying_parser
