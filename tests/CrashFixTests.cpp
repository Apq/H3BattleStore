// CrashFixTests.cpp
// 战斗崩溃修复（crashfix，2026-10-15 自 H3Space 迁入，源 H3BattleCrashFix）
// 纯逻辑回归（Catch2 v3 amalgamated，与宿主其余测试同框架）。
//
// 覆盖：障碍向量的 def 校验与「清 duration 阻止清理循环再来」语义、越界索引拒绝、
// IsProbablyReadableDword 的低位地址门槛、[CrashFix] 开关的分层读取（default → user 覆盖）。
// 全靠假 CombatManager（按源码偏移 +0x13D5C/+0x13D60 摆放 obstacleInfo 向量），
// 不进游戏、不挂钩、不写玩家 ini（配置用例写 runner 传入的 fixture 目录）。
//
// 边界声明：本套只验证纯逻辑与配置读取。三个 HH_* 钩子入口（THISCALL/FASTCALL 透传、
// 异常吞并后的跳过语义）依赖真实游戏地址与 patcher，不做单测，仍归实机验收。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include <catch_amalgamated.hpp>

// 本版 Catch2 v3 amalgamated 未提供 *_MESSAGE 变体：INFO + 断言等价实现。
// 条件整体加括号：Catch2 的表达式分解器不容忍断言顶层的 && / ||。
#define REQUIRE_MESSAGE(condition, message) do { INFO(message); REQUIRE((condition)); } while (0)

// ---- 最小 H3API 替身（只求链接期同名可编译；测试从不走会把控制交给游戏地址的分支）----
// THISCALL_n/FASTCALL_n 是**不求值表达式**：既能当语句也能赋值，且不会跳到游戏内存。
static const int SPLICE_ = 1;
static const int EXTENDED_ = 2;
static const int THISCALL_ = 3;
static const int FASTCALL_ = 4;
static const int EXEC_DEFAULT = 0;

struct HiHook {
    void* GetDefaultFunc() const { return nullptr; }
};
class PatcherInstance {
public:
    template <typename Fn> void WriteHiHook(uint32_t, int, int, int, Fn) {}
    template <typename Fn> void WriteLoHook(uint32_t, Fn) {}
};
class Patcher {
public:
    PatcherInstance* CreateInstance(const char*) { return nullptr; }
};
static Patcher* _P = nullptr;
static PatcherInstance* _PI = nullptr;

#define THISCALL_1(ret, addr, a1) ((ret)0)
#define THISCALL_2(ret, addr, a1, a2) ((ret)0)
#define FASTCALL_2(ret, addr, a1, a2) ((ret)0)

#include "../modules/IniUtf8.inc.cpp"
#include "../modules/ConfigLog.inc.cpp"
#include "../modules/LogContext.inc.cpp"
#define H3_GUARD_CONTEXT_(reason) LogRecentContext_(reason)
#include "../modules/CrashGuard.hpp"

// 与生产 TU（H3BattleStore.cpp）同构：模块文件自带包装已移除，命名空间在包含点打开。
namespace crashfix {
#include "../modules/CrashFix.inc.cpp"
}

namespace {

// ---- 假 CombatManager：obstacleInfo 向量在 +0x13D5C/+0x13D60，元素 0x18 字节 ----
struct FakeCombatMgr {
    std::vector<char> buf;
    std::vector<char> vec;
    char def_storage[16];  // 一块可读的「def 对象」

    explicit FakeCombatMgr(int count)
        : buf(0x14000, 0), vec((size_t)(count > 0 ? count : 0) * 0x18, 0)
    {
        memset(def_storage, 0, sizeof(def_storage));
        if (count > 0) {
            *reinterpret_cast<char**>(&buf[0x13D5C]) = vec.data();
            *reinterpret_cast<char**>(&buf[0x13D60]) = vec.data() + (size_t)count * 0x18;
        } else {
            *reinterpret_cast<char**>(&buf[0x13D5C]) = nullptr;
            *reinterpret_cast<char**>(&buf[0x13D60]) = nullptr;
        }
    }
    DWORD self() { return (DWORD)(size_t)buf.data(); }
    void set_def(int i, char* def) { *reinterpret_cast<DWORD*>(&vec[(size_t)i * 0x18 + 0x00]) = (DWORD)(size_t)def; }
    void set_duration(int i, DWORD dur) { *reinterpret_cast<DWORD*>(&vec[(size_t)i * 0x18 + 0x10]) = dur; }
    DWORD duration(int i) const { return *reinterpret_cast<const DWORD*>(&vec[(size_t)i * 0x18 + 0x10]); }
};

std::wstring FixtureRoot_()
{
    const wchar_t* env = _wgetenv(L"H3BS_TEST_FIXTURE");
    REQUIRE_MESSAGE(env != nullptr && env[0] != 0,
        "fixture directory comes from the H3BS_TEST_FIXTURE env var set by run-unit-tests.ps1");
    return env;
}

void SetPath(char* out, const std::wstring& path)
{
    REQUIRE_MESSAGE(WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, out, kPathCap_, nullptr, nullptr) > 0,
        "UTF-8 path conversion");
}

} // namespace

// ==================== 障碍移除修复：向量校验语义 ====================

TEST_CASE("crashfix::IsValidObstacle: def 有效时放行且不动 duration", "[crashfix]")
{
    FakeCombatMgr mgr(2);
    mgr.set_def(0, (char*)mgr.def_storage);
    mgr.set_duration(0, 7);
    REQUIRE_MESSAGE(crashfix::IsValidObstacle(mgr.self(), 0), "readable def is accepted");
    REQUIRE_MESSAGE(mgr.duration(0) == 7, "valid obstacle keeps its duration");
}

TEST_CASE("crashfix::IsValidObstacle: def=0 时清 duration 并拒绝", "[crashfix]")
{
    FakeCombatMgr mgr(2);
    mgr.set_def(1, nullptr);
    mgr.set_duration(1, 9);
    REQUIRE_MESSAGE(!crashfix::IsValidObstacle(mgr.self(), 1), "null def is rejected");
    // 修复语义：清 duration，阻止清理循环再次进来（宁可跳过这次清理，也不让战斗崩溃）。
    REQUIRE_MESSAGE(mgr.duration(1) == 0, "rejected obstacle has its duration cleared");
}

TEST_CASE("crashfix::IsValidObstacle: 越界与非法索引拒绝", "[crashfix]")
{
    FakeCombatMgr mgr(2);
    mgr.set_def(0, (char*)mgr.def_storage);
    REQUIRE_MESSAGE(!crashfix::IsValidObstacle(mgr.self(), 2), "index == count is rejected");
    REQUIRE_MESSAGE(!crashfix::IsValidObstacle(mgr.self(), -1), "negative index is rejected");
    REQUIRE_MESSAGE(!crashfix::IsValidObstacle(mgr.self(), 100), "far out-of-range index is rejected");
}

TEST_CASE("crashfix::IsProbablyReadableDword: 低位地址拒绝、真实地址可读", "[crashfix]")
{
    volatile DWORD on_stack = 0x1234;
    REQUIRE_MESSAGE(!crashfix::IsProbablyReadableDword(0), "null is rejected");
    REQUIRE_MESSAGE(!crashfix::IsProbablyReadableDword(0xFFFF), "low addresses are rejected");
    REQUIRE_MESSAGE(crashfix::IsProbablyReadableDword((DWORD)(size_t)&on_stack),
        "a genuinely readable dword is accepted");
}

// ==================== 配置读取：分层 ini ====================

TEST_CASE("crashfix::ReadConfig_: [CrashFix] 开关与 user 覆盖", "[config][crashfix]")
{
    const std::wstring root = FixtureRoot_();
    REQUIRE_MESSAGE(CreateDirectoryW(root.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS,
        "fixture directory");
    const std::wstring defIni = root + L"\\H3BattleStore.default.ini";
    const std::wstring userIni = root + L"\\H3BattleStore.user.ini";
    SetPath(g_ini_path, defIni);
    SetPath(g_user_ini_path, userIni);

    REQUIRE_MESSAGE(IniWriteKeyUtf8(g_ini_path, "CrashFix", "DisableObstacle", "1"), "write default fixture");
    // 同一文件上的第二次写入：既有的 DisableObstacle 值必须原样留下。
    // （2026-10-15 修：老写入器在重写时会把已有键的值原地截断 —— 见
    //   IniUtf8.inc.cpp 内的移植说明，这里正是该缺陷的隔离回归。）
    REQUIRE_MESSAGE(IniWriteKeyUtf8(g_ini_path, "CrashFix", "DisableAura", "0"), "write default fixture");
    char kept[32] = {};
    REQUIRE_MESSAGE(IniReadUtf8(g_ini_path, "CrashFix", "DisableObstacle", "", kept, (int)sizeof(kept))
            && std::strcmp(kept, "1") == 0,
        "rewriting the same ini keeps the previously written key value");

    crashfix::ReadConfig_();
    REQUIRE_MESSAGE(crashfix::g_disable_obstacle, "default DisableObstacle=1 is honored");
    REQUIRE_MESSAGE(!crashfix::g_disable_aura, "default DisableAura=0 leaves the aura fix enabled");

    // user 层覆盖 default：DisableAura=1 盖过 default 的 0。
    REQUIRE_MESSAGE(IniWriteKeyUtf8(g_user_ini_path, "CrashFix", "DisableAura", "1"), "write user override");
    crashfix::ReadConfig_();
    REQUIRE_MESSAGE(crashfix::g_disable_aura, "user override wins over the default layer");

    // 复原开关，避免影响同进程内其它用例。
    crashfix::g_disable_obstacle = false;
    crashfix::g_disable_aura = false;
}
