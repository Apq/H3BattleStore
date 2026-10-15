// ========== AutoGuardIds.inc.cpp ==========
// 2026-10-15 H3Auto 移植：铠甲钩子 id 映射（源 enum GHID_* → 宿主字符串键铠甲）。
//
// 源插件 CrashGuard.inc.cpp 用 enum GuardHookId { GHID_BLT, GHID_MSGPROC,
// GHID_SHOULD_AUTO, GHID_ACTION_EXEC, GHID_KB, GHID_PANEL_KB, GHID_PANEL_MOUSE,
// GHID_PANEL_ITEM } 的下标直接索引自己的钩子表。合并版宿主铠甲按字符串键注册
// （GuardRegisterHook_ 返回自增 id），因此这里保留源侧 GHID_* 名字与用法
// （各钩子入口 __except 过滤器一字不改），值改为宿主注册返回的 id。
//
// 约束：本文件必须在宿主 CrashGuard.hpp 之后、任何使用 GHID_* 的被移植模块之前
// 包含；注册在静态初始化期完成（宿主 s_guard_hooks 为文件作用域零初始化数组，
// 动态初始化期写入安全，与宿主自身 GUARD_* 注册同款）。
//
// 钩子入口铠甲一律手写（技能 h3-plugin-crash-guard：禁止宏化、禁止熔断）。
// 名称前缀 AutoAssist.* 与宿主 BattleStore.* 互不混淆，玩家崩溃日志可直接读归属。

static const int GHID_BLT          = GuardRegisterHook_("AutoAssist.AfterBlt");     // 0x600430 LoHook 每帧
static const int GHID_MSGPROC      = GuardRegisterHook_("AutoAssist.BattleMsgProc"); // 0x4746B0 LoHook 战斗消息
static const int GHID_SHOULD_AUTO  = GuardRegisterHook_("AutoAssist.ShouldAuto");   // 0x4744D0 HiHook 接管判定
static const int GHID_ACTION_EXEC  = GuardRegisterHook_("AutoAssist.ActionExec");   // 0x4786B0 HiHook 动作执行
static const int GHID_KB           = GuardRegisterHook_("AutoAssist.CombatHotkey"); // WH_KEYBOARD 战斗热键回调
static const int GHID_PANEL_KB     = GuardRegisterHook_("AutoAssist.PanelKeyboard");// 面板 WH_KEYBOARD
static const int GHID_PANEL_MOUSE  = GuardRegisterHook_("AutoAssist.PanelMouse");   // 面板 WH_MOUSE
static const int GHID_PANEL_ITEM   = GuardRegisterHook_("AutoAssist.PanelItem");    // 面板项输入屏障
