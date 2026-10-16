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

// 常规输入明细 trace；保存、改键及空格控制取证保留按键边缘 debug。
static int LogCommandLevel_(bool relevant, int edgeLevel)
{
    return relevant ? edgeLevel : LOG_TRACE;
}

static int LogSnapshotLevel_(const char* event)
{
    return !strcmp(event, "readback") ? LOG_TRACE : LOG_INFO;
}

// 每个动作路径独立收账；不改变游戏调用或深度，换场补账后仍保留周期门控。
struct LogActivityWindow_ {
    bool seen = false;
    DWORD last = 0;
    unsigned count = 0;
    int maxDepth = 0;
    int lastId = -1;
    int lastResult = -1;
    void Observe(DWORD now, int id, int depth) {
        if (!seen) { seen = true; last = now; }
        ++count;
        if (depth > maxDepth) maxDepth = depth;
        lastId = id;
    }
    bool Due(DWORD now, bool force) const {
        return count && (force || (DWORD)(now - last) >= 5000);
    }
    void Reported(DWORD now, bool force) {
        count = 0;
        maxDepth = 0;
        if (!force) last = now;
    }
};

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

// 逐候选/逐槽"明细突发"闸（纯策略，单测可钉）。这类明细一次行动可能几十行
// （选目标的每个候选、保活队列的每个槽），默认 debug 级不设闸会随每次行动成批
// 刷日志。策略：
//   - trace 级（用户显式选"全部"）：不限，便于深挖单次行动的全部候选；
//   - 其它级别：窗口内首次放行全量明细，其余压掉并累计批次数，下次放行时随
//     明细一起报出（不静默丢，符合"限频只管报告次数"的口径）。
// 摘要/决策行（选中结果、无候选原因、施放结果）不走此闸，决策链始终完整。
struct LogDetailBurst_ {
    LogRepeatGate_ gate;
    bool Admit(bool trace_enabled, DWORD now, DWORD interval, unsigned* suppressed)
    {
        if (suppressed) *suppressed = 0;
        if (trace_enabled) return true;
        return gate.Admit(now, interval, suppressed);
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
