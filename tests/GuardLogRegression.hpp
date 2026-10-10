#pragma once

// 仅供现有 LogControlsTests 包含并调用；不安装钩子/VEH、不触发真实异常。
#include <windows.h>
#include <stdint.h>
#include <string>

// 给真实生产 filter/flush 注入确定性 DWORD 时钟；不等待墙钟五秒。
static DWORD s_guard_regression_tick_ = 0;
static DWORD GuardRegressionTick_() { return s_guard_regression_tick_; }
#define GetTickCount GuardRegressionTick_
#include "../modules/CrashGuard.hpp"
#undef GetTickCount

namespace hbs_guard_test {

inline bool SameSignature(const H3AutoGuard::GuardFaultSignature& a,
    const H3AutoGuard::GuardFaultSignature& b)
{
    return a.code == b.code && a.fault_addr == b.fault_addr
        && a.av_addr == b.av_addr && a.av_operation == b.av_operation && a.has_av == b.has_av;
}

inline bool PolicyRegression()
{
    using namespace H3AutoGuard;
    const GuardFaultSignature first(0xC0000005ul, 0x11112222ull, 0x33334444ull, 0);
    const GuardFaultSignature latest(0xC0000005ul, 0x55556666ull, 0x77778888ull, 8);
    GuardFaultWindow a, b;
    if (a.Pending() || a.Due(0, true) || !a.Observe(first)
        || !a.Due(0, false) || a.NewCount() != 1) return false;
    a.Commit(100, false);
    if (a.Observe(latest) || a.Due(5099, false) || !a.Due(5100, false)
        || !SameSignature(a.first, first) || !SameSignature(a.recent, latest)) return false;
    if (!b.Observe(latest) || !b.Due(200, false) || b.total != 1 || a.total != 2) return false;
    b.Commit(200, false);
    a.Commit(5100, false);
    if (a.Pending() || a.Due(99999, true)) return false;

    // force 补账不推迟原先 10100 的周期边界，也不会重复空汇总。
    a.Observe(first);
    if (!a.Due(6000, true) || a.Due(6000, false)) return false;
    a.Commit(6000, true);
    if (a.last_report_tick != 5100 || a.Due(6001, true)) return false;
    a.Observe(latest);
    if (a.Due(10099, false) || !a.Due(10100, false)) return false;
    // 没有后续 Observe，正常 cycle 仍可收取静默余量。
    a.Commit(10100, false);
    if (a.Pending() || a.total != 4 || a.reported != 4) return false;

    GuardFaultWindow wrap;
    wrap.Observe(first);
    wrap.Commit(0xFFFFFFF0ul, false);
    wrap.Observe(latest);
    if (wrap.Due(0x1377ul, false) || !wrap.Due(0x1378ul, false)) return false;
    wrap.Commit(0x1378ul, false);
    if (wrap.Pending()) return false;

    // 历史 503700 条风暴在同一五秒内仅首详情 + 一条周期汇总。
    GuardFaultWindow storm;
    unsigned long lines = 0, reported = 0;
    storm.Observe(first);
    reported += storm.NewCount();
    storm.Commit(0, false);
    ++lines;
    for (unsigned long i = 1; i < 503700ul; ++i) {
        storm.Observe(latest);
        if (storm.Due(i % 4999ul, false)) return false;
    }
    if (storm.total != 503700ul || storm.NewCount() != 503699ul
        || !storm.Due(5000, false) || !SameSignature(storm.first, first)
        || !SameSignature(storm.recent, latest)) return false;
    reported += storm.NewCount();
    storm.Commit(5000, false);
    ++lines;
    return lines == 2 && reported == 503700ul && !storm.Pending();
}

inline bool ReadText(const std::wstring& path, std::string* text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(file); return false; }
    text->assign(size, '\0');
    DWORD read = 0;
    const bool ok = ReadFile(file, &(*text)[0], size, &read, nullptr) && read == size;
    CloseHandle(file);
    return ok;
}

inline size_t Count(const std::string& text, const char* needle)
{
    size_t count = 0, pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) { ++count; ++pos; }
    return count;
}

inline bool WriterRegression(const std::wstring& root)
{
    // 此测试运行在独立单元测试进程，局部构造记录代替真实 SEH。
    const std::wstring path = root + L"\\guard-time-regression.log";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    const int a = GuardRegisterHook_("GuardRegression.A");
    const int b = GuardRegisterHook_("GuardRegression.B");
    const int c = GuardRegisterHook_("GuardRegression.Contended");
    const int d = GuardRegisterHook_("GuardRegression.NoSignature");
    if (a == b || b == c || a == c || d == a || d == b || d == c) return false;
    // 日志文件本身不能作目录，稳定模拟打开失败而不依赖外部权限。
    GuardSetLogPathW((path + L"\\unwritable.log").c_str());
    EXCEPTION_RECORD er = {};
    er.ExceptionCode = 0xC0000005ul;
    er.ExceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(0x11112222));
    er.NumberParameters = 2;
    er.ExceptionInformation[0] = 0;
    er.ExceptionInformation[1] = 0x33334444;
    EXCEPTION_POINTERS ep = { &er, nullptr };
    s_guard_regression_tick_ = 0;
    if (GuardCrashFilter_(a, &ep) != EXCEPTION_EXECUTE_HANDLER
        || s_guard_hooks[a].first_reported || s_guard_hooks[a].window.reported) return false;
    GuardSetLogPathW(path.c_str());
    GuardFlushFaults_("writer-retry", false);
    if (!s_guard_hooks[a].first_reported || s_guard_hooks[a].window.reported != 1) return false;
    er.ExceptionAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(0x55556666));
    er.ExceptionInformation[0] = 1;
    er.ExceptionInformation[1] = 0x77778888;
    s_guard_regression_tick_ = 1;
    GuardCrashFilter_(a, &ep);
    GuardCrashFilter_(b, &ep); // 独立钩子首异常不受 A 门控影响。
    s_guard_regression_tick_ = 4999;
    GuardFlushFaults_("early", false);
    if (s_guard_hooks[a].window.reported != 1) return false;
    s_guard_regression_tick_ = 5000;
    GuardFlushFaults_("quiet-cycle", false); // 无新异常也补账，保留最近签名。
    if (s_guard_hooks[a].window.reported != 2) return false;
    er.ExceptionCode = 0xC000001Dul;
    s_guard_regression_tick_ = 5100;
    GuardCrashFilter_(a, &ep);
    s_guard_regression_tick_ = 6000;
    GuardFlushFaults_("battle-change", true);
    GuardFlushFaults_("empty-force", true);
    if (s_guard_hooks[a].window.last_report_tick != 5000) return false;
    er.ExceptionCode = 0xC0000005ul;
    er.ExceptionInformation[0] = 8;
    s_guard_regression_tick_ = 6001;
    GuardCrashFilter_(a, &ep);
    s_guard_regression_tick_ = 9999;
    GuardFlushFaults_("early-after-force", false);
    if (s_guard_hooks[a].window.reported != 3) return false;
    s_guard_regression_tick_ = 10000;
    GuardFlushFaults_("period-after-force", false);
    // 模拟日志递归/跨钩子忙：签名先留存，日志补报不丢失详情。
    InterlockedExchange(&s_guard_fault_log_busy, 1);
    GuardCrashFilter_(c, &ep);
    InterlockedExchange(&s_guard_fault_log_busy, 0);
    GuardFlushFaults_("deferred-first", false);
    // 模拟同钩子递归：立刻放弃签名，计数仍增加，后续明确补账。
    InterlockedExchange(&s_guard_hooks[c].report_lock, 1);
    GuardCrashFilter_(c, &ep);
    InterlockedExchange(&s_guard_hooks[c].report_lock, 0);
    GuardFlushFaults_("contended-tail", true);
    if (s_guard_hooks[a].faults != 4 || s_guard_hooks[b].faults != 1
        || s_guard_hooks[c].faults != 2 || s_guard_hooks[c].signature_misses != 1
        || s_guard_hooks[c].report_lock || s_guard_fault_log_busy) return false;
    // 首次就抢锁失败且此后平静，cycle 仍补记计数，不造零地址签名。
    InterlockedExchange(&s_guard_hooks[d].report_lock, 1);
    GuardCrashFilter_(d, &ep);
    InterlockedExchange(&s_guard_hooks[d].report_lock, 0);
    GuardFlushFaults_("no-signature-tail", false);
    if (s_guard_hooks[d].faults != 1 || s_guard_hooks[d].window.reported != 1) return false;
    GuardFlushFaults_("empty-final", true);
    GuardSetLogPathW(nullptr);
    std::string text;
    if (!ReadText(path, &text)) return false;
    return Count(text, "异常汇总") == 5 && Count(text, "[Guard]") == 8
        && text.find("读取 0x33334444") != std::string::npos
        && text.find("addr=0x11112222") != std::string::npos
        && text.find("reason=quiet-cycle 新增=1 累计=2") != std::string::npos
        && text.find("code=0xC0000005 fault_addr=0x55556666 av_addr=0x77778888 av_op=1(写入)") != std::string::npos
        && text.find("reason=battle-change 新增=1 累计=3") != std::string::npos
        && text.find("code=0xC000001D") != std::string::npos
        && text.find("reason=period-after-force 新增=1 累计=4") != std::string::npos
        && text.find("av_op=8(执行(DEP))") != std::string::npos
        && text.find("reason=contended-tail 新增=1 累计=2") != std::string::npos
        && text.find("签名未留存累计=1") != std::string::npos
        && text.find("reason=no-signature-tail 新增=1 累计=1 最近签名未留存") != std::string::npos
        && text.find("reason=empty-") == std::string::npos
        && text.find("reason=early") == std::string::npos;
}

} // namespace hbs_guard_test
