// 最近调用/命令：debug保留内存证据，trace逐条落盘。
// 回调只读插件自有静态副本；try-lock不等待、不分配堆、不读游戏对象。
static const unsigned kLogContextCapacity_ = 96;
struct LogContextRecord_ { unsigned long long sequence; DWORD tick; char text[512]; };
static LogContextRecord_ g_logContext[kLogContextCapacity_] = {};
static volatile LONG g_logContextLock = 0;
static unsigned long long g_logContextSequence = 0, g_logContextReported = 0;
static volatile LONG g_logContextDropped = 0;

static void LogDetail_(const char* fmt, ...)
{
    if (!LogEnabled_(LOG_DEBUG)) return;
    char text[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, fmt, ap);
    va_end(ap);
    LogTrace("%s", text);
    if (InterlockedCompareExchange(&g_logContextLock, 1, 0) != 0) {
        InterlockedIncrement(&g_logContextDropped);
        return;
    }
    __try {
        const unsigned long long sequence = ++g_logContextSequence;
        LogContextRecord_& record = g_logContext[(sequence - 1) % kLogContextCapacity_];
        record.sequence = sequence;
        record.tick = GetTickCount();
        strcpy_s(record.text, sizeof(record.text), text);
    }
    __finally { InterlockedExchange(&g_logContextLock, 0); }
}

static bool LogContextWrite_(const char* fmt, ...)
{
    char text[768];
    SYSTEMTIME now; GetLocalTime(&now);
    const int offset = _snprintf_s(text, sizeof(text), _TRUNCATE,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [debug] ",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    if (offset < 0) return false;
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(text + offset, sizeof(text) - offset, _TRUNCATE, fmt, ap);
    va_end(ap);
    return AppendUtf8LogLine(text);
}

static bool LogRecentContext_(const char* reason)
{
    if (!LogEnabled_(LOG_DEBUG)) return true;
    if (InterlockedCompareExchange(&g_logContextLock, 1, 0) != 0) return false;
    __try {
        const unsigned long long pending = g_logContextSequence - g_logContextReported;
        const LONG dropped = InterlockedCompareExchange(&g_logContextDropped, 0, 0);
        if (pending || dropped) {
            const unsigned count = pending < kLogContextCapacity_ ? (unsigned)pending : kLogContextCapacity_;
            if (!LogContextWrite_("[Context] reason=%s recent=%u overwritten=%llu contention_dropped=%ld last_seq=%llu",
                reason, count, pending - count, dropped, g_logContextSequence)) return false;
            for (unsigned i = 0; i < count; ++i) {
                const unsigned long long seq = g_logContextSequence - count + i + 1;
                const LogContextRecord_& record = g_logContext[(seq - 1) % kLogContextCapacity_];
                if (!LogContextWrite_("[Context] seq=%llu tick=%lu %s", record.sequence, record.tick, record.text)) return false;
            }
            g_logContextReported = g_logContextSequence;
            InterlockedExchangeAdd(&g_logContextDropped, -dropped);
        }
    }
    __finally { InterlockedExchange(&g_logContextLock, 0); }
    return true;
}
