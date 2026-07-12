// 提供可见页面按需同步共用的请求、超时、重试和退避状态机。
#pragma once

template <typename Tick>
class VisibleSyncRetryState {
public:
    void reset_request()
    {
        requested_ = false;
        request_tick_ = 0;
    }

    void reset_attempts()
    {
        attempts_ = 0;
        backoff_until_tick_ = 0;
    }

    void reset()
    {
        reset_request();
        reset_attempts();
    }

    bool request_if_due(Tick now,
                        bool sync_in_flight,
                        bool request_blocked,
                        Tick retry_ticks,
                        int max_attempts,
                        Tick backoff_ticks)
    {
        if (backoff_until_tick_ != 0 && now >= backoff_until_tick_) {
            reset_attempts();
        }
        bool backoff_active = backoff_until_tick_ != 0 && now < backoff_until_tick_;
        if (requested_ && !sync_in_flight &&
            request_tick_ != 0 && now - request_tick_ >= retry_ticks) {
            reset_request();
            if (attempts_ >= max_attempts) {
                backoff_until_tick_ = now + backoff_ticks;
                backoff_active = true;
            }
        }
        if (requested_ ||
            attempts_ >= max_attempts ||
            backoff_active ||
            request_blocked ||
            sync_in_flight) {
            return false;
        }
        requested_ = true;
        request_tick_ = now;
        ++attempts_;
        return true;
    }

    bool requested() const { return requested_; }
    Tick request_tick() const { return request_tick_; }
    int attempts() const { return attempts_; }
    Tick backoff_until_tick() const { return backoff_until_tick_; }

private:
    bool requested_ = false;
    Tick request_tick_ = 0;
    int attempts_ = 0;
    Tick backoff_until_tick_ = 0;
};
