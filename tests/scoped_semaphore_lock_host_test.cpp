// 验证任务 semaphore 作用域锁的成功、失败、空句柄和自动释放语义。
#include "scoped_semaphore_lock.h"

#include <assert.h>

int main()
{
    TestSemaphore available = {.take_allowed = true};
    {
        ScopedSemaphoreLock lock(&available, 17);
        assert(lock);
        assert(available.take_count == 1);
        assert(available.give_count == 0);
        assert(available.last_wait_ticks == 17);
    }
    assert(available.give_count == 1);

    TestSemaphore unavailable = {.take_allowed = false};
    {
        ScopedSemaphoreLock lock(&unavailable);
        assert(!lock);
        assert(unavailable.take_count == 1);
        assert(unavailable.last_wait_ticks == portMAX_DELAY);
    }
    assert(unavailable.give_count == 0);

    ScopedSemaphoreLock empty(nullptr);
    assert(!empty);
    return 0;
}
