// ========== CrashFix.inc.cpp ==========
// 战斗崩溃修复（源 D:\GitHub\H3\H3BattleCrashFix；2026-10-15 用户裁定归 H3BattleStore，
// 同日曾短暂并入随身空间后迁出——战斗类修复统一随热血插件走，随身空间保持纯仓库职能）。
// 三个 HiHook（SPLICE_ + EXTENDED_，无 LoHook）：
//   1) [障碍]     CombatMgr::RemoveObstacle        @0x466710  def 双重释放崩溃
//   2) [光环]     UpdateAuraLinks                  @0x43E790  独角兽抗魔光环 × 丧心病狂/蛊惑
//   3) [阻止清理] Vector::FindAndRemove            @0x43E720  损坏 vector 保护
//
// 与宿主的交点：BattleRestore.inc.cpp 以函数指针直呼 0x466710（kObstacleRemove），
// 挂钩后该调用同样经过这里的铠甲与校验——恢复事务里清障碍也受保护，正是想要的语义。
//
// 配置：[CrashFix] DisableObstacle / DisableAura（0=启用修复，1=不挂钩），
// 写宿主 H3BattleStore.default.ini / H3BattleStore.user.ini（分层读取）。
//
// 铠甲语义（技能 h3-plugin-crash-guard）：三个钩子都是**防崩型**——整函数
// __try，异常被吞并落盘，安全默认 = 跳过本次原函数调用（正是本功能的修复
// 语义：宁可跳过一次清理，也不让战斗崩溃）。原函数是尾部调用，不存在双执行。
//
// 符号隔离：本文件**不自带 namespace 包裹**——H3BattleStore.cpp 在包含点包
// `namespace crashfix { ... }`（MSVC 坑：include 进命名空间的文件再开同名
// namespace 会破坏同块后续文件查找）。对外只暴露 ReadConfig_ / StartHooks_。
// 守卫名沿用 "CrashFix.*" 前缀（与宿主 BattleStore.*/AutoAssist.* 互不混淆，
// 玩家崩溃日志可直接读归属）。

// ---- 钩子 id（静态初始化期注册，必须先于下方使用）----
static const int GUARD_CF_OBSTACLE = GuardRegisterHook_("CrashFix.RemoveObstacle");
static const int GUARD_CF_AURA = GuardRegisterHook_("CrashFix.UpdateAuraLinks");
static const int GUARD_CF_FINDREMOVE = GuardRegisterHook_("CrashFix.FindAndRemove");

// ---- 配置 ----
static bool g_disable_obstacle = false;
static bool g_disable_aura = false;

// ---- 地址常量（SoD 1.x，随全局 GuardVerifySodBytes_ 门卫保护）----
static const DWORD ADDR_REMOVE_OBSTACLE = 0x466710;
static const DWORD ADDR_UPDATE_AURA_LINKS = 0x43E790;
static const DWORD ADDR_VECTOR_FIND_AND_REMOVE = 0x43E720;

static const int SPELL_BERSERK = 59;    // 丧心病狂
static const int SPELL_HYPNOTIZE = 60;  // 蛊惑

static const DWORD OFS_TYPE = 0x34;
static const DWORD OFS_SIDE = 0xF4;
static const DWORD OFS_ACTIVE_SPELLS_DURATION = 0x198;

// CombatManager 的 obstacleInfo 向量（元素 H3Obstacle 0x18 字节）。
static const DWORD OFS_OBSTACLE_VEC_BEGIN = 0x13D5C;
static const DWORD OFS_OBSTACLE_VEC_END = 0x13D60;
static const DWORD OBSTACLE_ENTRY_SIZE = 0x18;
static const DWORD OBSTACLE_OFS_DEF = 0x00;
static const DWORD OBSTACLE_OFS_DURATION = 0x10;

static bool IsProbablyReadableDword(DWORD address)
{
    if (address < 0x10000)
        return false;
    __try {
        volatile DWORD value = *(DWORD*)address;
        (void)value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- 修复 1：[障碍] RemoveObstacle ----
// 原函数在 0x466813 处 mov ecx,[edi] 读取 obstacle.def；def=NULL（已被移除）
// 时后续 mov edx,[ecx] 崩溃。包裹整个函数，调用原函数前校验 def 有效性，
// 无效则清 duration 跳过原函数（阻止清理循环再次进来）。
static bool IsValidObstacle(DWORD combatMgr, int obstacleNum)
{
    __try {
        DWORD vecBegin = *(DWORD*)(combatMgr + OFS_OBSTACLE_VEC_BEGIN);
        DWORD vecEnd = *(DWORD*)(combatMgr + OFS_OBSTACLE_VEC_END);
        DWORD count = (vecEnd - vecBegin) / OBSTACLE_ENTRY_SIZE;

        if (obstacleNum < 0 || (DWORD)obstacleNum >= count)
            return false;

        DWORD obstacle = vecBegin + obstacleNum * OBSTACLE_ENTRY_SIZE;
        DWORD def = *(DWORD*)(obstacle + OBSTACLE_OFS_DEF);
        DWORD duration = *(DWORD*)(obstacle + OBSTACLE_OFS_DURATION);

        if (!def || !IsProbablyReadableDword(def)) {
            // 清除 duration，防止清理循环再次调用。
            *(DWORD*)(obstacle + OBSTACLE_OFS_DURATION) = 0;
            LogInfo("[障碍] 跳过 obstacle#%u def=0x%08X dur=%u (已移除或无效)",
                obstacleNum, def, duration);
            return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int __stdcall HH_RemoveObstacle(HiHook* h, DWORD combatMgr, int obstacleNum)
{
    __try {
        if (!IsValidObstacle(combatMgr, obstacleNum)) {
            return 0;  // 原函数返回 void，这里返回任意值
        }

        DWORD vecBegin = *(DWORD*)(combatMgr + OFS_OBSTACLE_VEC_BEGIN);
        DWORD obstacle = vecBegin + obstacleNum * OBSTACLE_ENTRY_SIZE;
        DWORD def = *(DWORD*)(obstacle + OBSTACLE_OFS_DEF);
        DWORD duration = *(DWORD*)(obstacle + OBSTACLE_OFS_DURATION);
        LogInfo("[障碍] 移除 obstacle#%u addr=0x%08X def=0x%08X dur=%u",
            obstacleNum, obstacle, def, duration);
    }
    __except (GuardCrashFilter_(GUARD_CF_OBSTACLE, GetExceptionInformation())) {
        return 0;
    }

    THISCALL_2(void, h->GetDefaultFunc(), combatMgr, obstacleNum);
    return 0;
}

// ---- 修复 2：[光环] UpdateAuraLinks ----
// target 处于丧心病狂(59)或蛊惑(60)时跳过整个函数（抗魔光环双向关联在这些
// 状态下会把已失效的关联写坏）。
static int __stdcall HH_UpdateAuraLinks(HiHook* h, DWORD target)
{
    bool skip = false;
    __try {
        if (target && IsProbablyReadableDword(target)) {
            DWORD berserk_dur = *(DWORD*)(target + OFS_ACTIVE_SPELLS_DURATION + SPELL_BERSERK * 4);
            DWORD hypnotize_dur = *(DWORD*)(target + OFS_ACTIVE_SPELLS_DURATION + SPELL_HYPNOTIZE * 4);

            if (berserk_dur > 0 || hypnotize_dur > 0) {
                DWORD type = *(DWORD*)(target + OFS_TYPE);
                DWORD side = *(DWORD*)(target + OFS_SIDE);
                const char* status = berserk_dur > 0 ? "丧心病狂" : "蛊惑";
                DWORD dur = berserk_dur > 0 ? berserk_dur : hypnotize_dur;
                LogInfo("[光环] 跳过 target=0x%08X type=%u side=%u %s中(duration=%u)",
                    target, type, side, status, dur);
                skip = true;
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_CF_AURA, GetExceptionInformation())) {
        return 0;
    }
    if (skip) return 0;

    THISCALL_1(void, h->GetDefaultFunc(), target);
    return 0;
}

// ---- 修复 3：[阻止清理] Vector::FindAndRemove 保护 ----
// 校验 begin / end / endCapacity 才放行（损坏向量的 (end-begin)>>2 会算出
// 天文数字，原函数随即越界读）。
static int __stdcall HH_VectorFindAndRemove(HiHook* h, DWORD vec, DWORD value)
{
    bool skip = false;
    __try {
        if (!vec || !IsProbablyReadableDword(vec)) {
            LogInfo("[阻止清理] 跳过 vec=0 (结构无效)");
            skip = true;
        } else {
            DWORD begin = *(DWORD*)(vec + 0);
            DWORD end = *(DWORD*)(vec + 4);
            DWORD endCap = *(DWORD*)(vec + 8);

            if (begin == 0 && end == 0) {
                skip = true;  // 空向量，不调用原函数
            } else if (begin < 0x10000 || end < 0x10000 || endCap < 0x10000) {
                LogInfo("[阻止清理] 跳过 vec=0x%08X begin=0x%08X end=0x%08X 无效", vec, begin, end);
                skip = true;
            } else if (end > endCap) {
                LogInfo("[阻止清理] 跳过 vec=0x%08X end>endCap (end=0x%08X cap=0x%08X)", vec, end, endCap);
                skip = true;
            } else if (((end - begin) >> 2) > 40) {
                LogInfo("[阻止清理] 跳过 vec=0x%08X count=%u 过大", vec, (end - begin) >> 2);
                skip = true;
            }
        }
    }
    __except (GuardCrashFilter_(GUARD_CF_FINDREMOVE, GetExceptionInformation())) {
        return 0;
    }
    if (skip) return 0;

    FASTCALL_2(void, h->GetDefaultFunc(), vec, value);
    return 0;
}

// ---- 装配 ----

static void ReadConfig_()
{
    g_disable_obstacle = IniReadIntUtf8Layered("CrashFix", "DisableObstacle", 0) != 0;
    g_disable_aura = IniReadIntUtf8Layered("CrashFix", "DisableAura", 0) != 0;
}

static void StartHooks_()
{
    if (!g_disable_obstacle) {
        _PI->WriteHiHook(ADDR_REMOVE_OBSTACLE, SPLICE_, EXTENDED_, THISCALL_, HH_RemoveObstacle);
        LogInfo("[CrashFix] [障碍] RemoveObstacle @0x%08X 已注册（HiHook SPLICE_ THISCALL_）",
            ADDR_REMOVE_OBSTACLE);
    } else {
        LogInfo("[CrashFix] [障碍] 已通过 ini 禁用");
    }

    if (!g_disable_aura) {
        _PI->WriteHiHook(ADDR_UPDATE_AURA_LINKS, SPLICE_, EXTENDED_, THISCALL_, HH_UpdateAuraLinks);
        LogInfo("[CrashFix] [光环] UpdateAuraLinks @0x%08X 已注册（HiHook SPLICE_ THISCALL_）",
            ADDR_UPDATE_AURA_LINKS);

        _PI->WriteHiHook(ADDR_VECTOR_FIND_AND_REMOVE, SPLICE_, EXTENDED_, FASTCALL_, HH_VectorFindAndRemove);
        LogInfo("[CrashFix] [阻止清理] FindAndRemove @0x%08X 已注册（HiHook SPLICE_ FASTCALL_）",
            ADDR_VECTOR_FIND_AND_REMOVE);
    } else {
        LogInfo("[CrashFix] [光环] 已通过 ini 禁用");
    }
}
