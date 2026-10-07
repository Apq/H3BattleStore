#pragma once

// Included after LogLevel; pure policies shared with unit tests.
static int LogOutcomeLevel_(const char* outcome, bool writing)
{
    if (writing || !strcmp(outcome, "failed") || !strcmp(outcome, "persisted-unverified"))
        return LOG_ERROR;
    if (!strcmp(outcome, "ok") || !strcmp(outcome, "cancelled")
        || !strcmp(outcome, "serialized-equal") || !strcmp(outcome, "rejected"))
        return LOG_INFO;
    return LOG_WARN;
}

static int LogStageLevel_(const char* stage)
{
    if (!strcmp(stage, "restore.rollback")) return LOG_WARN;
    if (!strcmp(stage, "restore.commit-objects") || !strcmp(stage, "restore.verify")
        || !strcmp(stage, "restore.verified")) return LOG_INFO;
    return LOG_DEBUG;
}

// First event immediately, then one report per interval, carrying suppressed count.
struct LogRepeatGate_ {
    bool seen = false;
    DWORD last = 0;
    unsigned suppressed = 0;
    bool Admit(DWORD now, DWORD interval, unsigned* skipped)
    {
        *skipped = 0;
        if (seen && (DWORD)(now - last) < interval) {
            ++suppressed;
            return false;
        }
        *skipped = suppressed;
        suppressed = 0;
        seen = true;
        last = now;
        return true;
    }
};

struct LogFailureWindow_ {
    LogRepeatGate_ repeats;
    bool failing = false;
    DWORD lastFailure = 0;
    // 1=failure report, 2=stable recovery, 0=silent.
    int Observe(bool failed, DWORD now, DWORD interval, unsigned* skipped)
    {
        *skipped = 0;
        if (failed) {
            failing = true;
            lastFailure = now;
            return repeats.Admit(now, interval, skipped) ? 1 : 0;
        }
        if (failing && (DWORD)(now - lastFailure) >= interval) {
            *skipped = repeats.suppressed;
            repeats = {};
            failing = false;
            return 2;
        }
        return 0;
    }
};

struct LogKeyEdges_ {
    bool down[256] = {};
    int Level(int key, bool isDown)
    {
        if (key < 0 || key >= 256) return LOG_TRACE;
        const bool repeat = isDown && down[key];
        down[key] = isDown;
        return repeat ? LOG_TRACE : LOG_DEBUG;
    }
};
