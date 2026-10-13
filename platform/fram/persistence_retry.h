#ifndef PERSISTENCE_RETRY_H
#define PERSISTENCE_RETRY_H

#include <cstdint>

class PersistenceRetryState
{
public:
    void MarkPending(uint32_t now_ms, uint32_t delay_ms)
    {
        pending_ = true;
        retry_at_ms_ = now_ms + delay_ms;
    }

    bool IsPending() const { return pending_; }

    bool IsDue(uint32_t now_ms) const
    {
        return pending_ && (int32_t)(now_ms - retry_at_ms_) >= 0;
    }

    void SaveSucceeded() { pending_ = false; }

    void SaveFailed(uint32_t now_ms, uint32_t retry_delay_ms)
    {
        MarkPending(now_ms, retry_delay_ms);
    }

    bool CompleteAttempt(bool succeeded, uint32_t now_ms, uint32_t retry_delay_ms)
    {
        if (succeeded)
        {
            SaveSucceeded();
            return true;
        }
        SaveFailed(now_ms, retry_delay_ms);
        return false;
    }

private:
    uint32_t retry_at_ms_ = 0u;
    bool pending_ = false;
};

#endif