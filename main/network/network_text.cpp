// 实现不依赖网络状态或系统服务的轻量文本处理工具。
#include "network_text.h"

#include <ctype.h>
#include <string.h>

namespace {

constexpr size_t kCStringTerminatorSize = 1;

bool ascii_space(char ch)
{
    return isspace(static_cast<unsigned char>(ch));
}

} // namespace

static_assert(kCStringTerminatorSize == 1,
              "network text terminator reservation must be one byte");

void trim_ascii(char *text)
{
    if (!text) {
        return;
    }
    size_t len = strlen(text);
    while (len > 0 && ascii_space(text[len - 1])) {
        text[--len] = '\0';
    }
    char *start = text;
    while (*start && ascii_space(*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + kCStringTerminatorSize);
    }
}
