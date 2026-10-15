// AutoEntry.inc.cpp
// 移植批次四：入口接线。源 Entry.inc.cpp 的每帧段 / 战斗消息段 / 钩子外壳在此落地；
// 源的 DllMain、日志基建、铠甲安装、版本自证**不**移植（宿主同名能力已是唯一属主）。
//
// 同地址钩子的单壳合并（技能 h3-plugin-multi-merge-port）：
//   0x600430 LoHook  宿主 Hook_AfterBlt_（战斗悬浮条）+ 源 Hook_BltComplete（面板帧/自动推进）
//   0x4786B0 HiHook  宿主 Hook_BattleExecute_（执行诊断）+ 源 HH_OnBattleActionExecute（单次接管消费）
// 两个壳仍各只有一处注册（宿主 Entry 内），原版函数在 __try 之外恰好调用一次；
// 源侧原来自己的那两个外壳注册连同 DllMain 一起丢弃，避免同址双壳。
// 新地址钩子（0x4746B0 LoHook、0x4744D0 HiHook）由本文件 AutoInstallHooks_ 注册。

// ========== 启动接线 ==========

// g_profiles_prefix：源 DllMain 用 <DLL 目录>\H3Auto.profiles 表示 DLL 目录
// （AutoStore 的 DllDirUtf8_ 截掉文件名即目录，文件名本身无用途）。合并版改名，
// 以免与残留的独立 H3Auto 插件互相把对方目录当成自己的。
void InitAutoPaths_()
{
    wchar_t* wpath = new(std::nothrow) wchar_t[kPathCap_ / 2]();
    if (!wpath) return;
    if (GetModuleFileNameW(g_hModule, wpath, kPathCap_ / 2)) {
        wchar_t* wslash = wcsrchr(wpath, L'\\');
        if (!wslash) wslash = wcsrchr(wpath, L'/');
        if (wslash) wcscpy(wslash + 1, L"H3BattleStore.profiles");
        else wcscpy(wpath, L"H3BattleStore.profiles");
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, g_profiles_prefix,
            kPathCap_, nullptr, nullptr);
        g_profiles_prefix[kPathCap_ - 1] = 0;
    }
    delete[] wpath;
}

// 源 DllMain 顺序：路径 → ReadConfig → LoadUiTexts → StartPlugin（挂钩子）。
// 这里只做前三步；钩子在 AutoInstallHooks_，由宿主 StartPlugin 在版本门卫之后调用。
void AutoInit_()
{
    InitAutoPaths_();
    AutoReadConfig_();
    LoadUiTexts();
}

// ========== LoHook 0x600430 每帧段（源 Hook_BltComplete_Inner_，逐字保留）==========
// 每帧检测自动战斗对话框 + 画面板；面板未开时推进自动执行（部队动作提交与观察
// 均由当前动作管线处理，再决定推进游标并提交部队主动作，不能只依赖偶发战斗消息）。
int AutoBltFrameSegment_()
{
    static int s_frame = 0;
    s_frame++;
    CheckAutoFightDialogClosed();
    if (g_uiPort->IsPanelActive()) {
        // 面板帧（模态深度/输入/绘制/拾取标记）经界面契约执行。
        g_uiPort->OnFramePanelTick();
    } else {
        __try {
            TryAutoExecuteActiveStack(false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return EXEC_DEFAULT;
}

// ========== LoHook 0x4746B0 战斗消息段（源 Hook_BattleMsgProc_Inner_，逐字保留）==========
// 0x4746B0（战斗消息处理入口 FUN_004746b0）。this=H3CombatManager 在 ECX，消息指针
// msg 在栈上 [esp+4]。msg[0]=类型（4=鼠标移动）。
// 1) 面板打开时：吞 0-9 快捷施法键、把鼠标移动坐标改成离屏（清 hover）。
// 2) 面板关闭时：若当前活动单位应由本插件主动执行，则在此提交 action 字段，
//    让原版主循环自然进入 FUN_004786b0 执行动画/伤害/回合推进。
static INT AutoBattleMsgProcSegment_(LoHook* h, HookContext* c)
{
    (void)h;

    // 战场拾取（近战选格 / 循环移动路径）的点击捕获在透明 item 屏障
    // BlockBattleItemMessage_ 里处理（靠 StopProcessing 吞点击，绝不触发
    // 部队行动）。此处不再处理拾取，避免与屏障逻辑冲突、空转。

    // 面板存活期间：吞并判定后直接返回（与旧实现一致，绝不落入主动作
    // 提交分支）；面板的消息吞并/坐标改写经界面契约执行。
    if (g_uiPort->IsPanelActive()) {
        if (g_uiPort->FilterGameMsg(*reinterpret_cast<int**>(c->esp + 4)))
            return NO_EXEC_DEFAULT;
        return EXEC_DEFAULT;
    }

    // 面板未打开：尝试主动提交当前单位动作（防御/远程/近战等）。
    // 只写 battle->action，不跳过原函数；原函数看到 action!=0 会走执行路径。
    __try {
        TryAutoExecuteActiveStack(true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return EXEC_DEFAULT;
}

// 铠甲外壳（源 Hook_BattleMsgProc 逐字保留）：GHID_MSGPROC → AutoAssist.BattleMsgProc。
INT __stdcall AutoHook_BattleMsgProc_(LoHook* h, HookContext* c)
{
    __try {
        return AutoBattleMsgProcSegment_(h, c);
    } __except (GuardCrashFilter_(GHID_MSGPROC, GetExceptionInformation())) {}
    return EXEC_DEFAULT;   // 安全默认：放行消息给原版处理
}

// ========== 钩子注册与收尾 ==========

// 由宿主 StartPlugin 在版本门卫通过后调用（SoD 指纹不符时不挂任何钩子）。
void AutoInstallHooks_()
{
    // 0x600430 与 0x4786B0 不在此注册：它们是宿主既有钩子的同址合并（见文件头）。
    _PI->WriteLoHook(0x4746B0, AutoHook_BattleMsgProc_);
    _PI->WriteHiHook(0x4744D0, SPLICE_, THISCALL_, HH_ShouldAutoExecute);
    ResetAutoState();
    LogInfo("[自动战斗] 钩子已注册：0x4746B0 / 0x4744D0（0x600430、0x4786B0 与宿主同址单壳合并）");
}

// 由宿主 DllMain 的 DLL_PROCESS_DETACH 调用（源 DllMain 同样两件事）。
void AutoShutdown_()
{
    ShutdownCombatHotkeys();
    ResetAutoState();
}
