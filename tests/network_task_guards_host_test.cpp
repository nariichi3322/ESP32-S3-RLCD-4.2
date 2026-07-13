// 验证联网任务唤醒锁和临时 HTTP 超时守卫的作用域恢复与重复释放保护。
#include "network_task_guards.h"

#include <assert.h>

int g_http_timeout_ms = 5000;

namespace {
int g_awake_acquire_calls = 0;
int g_awake_release_calls = 0;
} // namespace

void acquire_network_awake_lock()
{
    ++g_awake_acquire_calls;
}

void release_network_awake_lock()
{
    ++g_awake_release_calls;
}

int main()
{
    {
        NetworkAwakeLockGuard guard;
        assert(g_awake_acquire_calls == 1);
        assert(g_awake_release_calls == 0);
    }
    assert(g_awake_release_calls == 1);

    {
        NetworkAwakeLockGuard guard;
        assert(g_awake_acquire_calls == 2);
        guard.release();
        guard.release();
        assert(g_awake_release_calls == 2);
    }
    assert(g_awake_release_calls == 2);

    assert(g_http_timeout_ms == 5000);
    {
        NetworkHttpTimeoutGuard outer(2500);
        assert(g_http_timeout_ms == 2500);
        {
            NetworkHttpTimeoutGuard inner(750);
            assert(g_http_timeout_ms == 750);
        }
        assert(g_http_timeout_ms == 2500);
    }
    assert(g_http_timeout_ms == 5000);
    return 0;
}
