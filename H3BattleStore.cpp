// H3BattleStore.cpp
// 英雄无敌3 SoD 插件：战斗中存档与读档。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>
#include <ddraw.h>
#include <wincrypt.h>
#include <new>
#include <memory>
#include <stdarg.h>
#include <wchar.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#pragma comment(lib, "advapi32.lib")

using namespace h3;

static_assert(sizeof(H3CombatManager) == 0x140EC, "SoD combat manager layout");
static_assert(offsetof(H3CombatManager, accessibleSquares) == 0x4C, "previous shade cache layout");
static_assert(offsetof(H3CombatManager, accessibleSquares2) == 0x107, "current shade cache layout");
static_assert(offsetof(H3CombatManager, tacticsPhase) == 0x13D68, "native shade override is tactics phase");
// 移植（批次四）：autoassist 的裸结构视图 _BattleMgr_（modules\Compat.inc.cpp）与官方
// 布局共用同一对象指针，关键字段偏移必须一致——不一致会让同址单壳合并传错对象语义。
static_assert(offsetof(H3CombatManager, action) == 0x3C, "autoassist _BattleMgr_::action matches official layout");

// 跨模块前置声明：实现在 Entry.inc.cpp，供先于其包含的恢复与界面模块使用。
static bool CombatIsReadable_(const H3CombatManager* mgr);
static bool CombatCanCapture_(const H3CombatManager* mgr, const char** reason);
static bool BattleFingerprint_(const H3CombatManager* mgr, std::string* out, std::string* error);
static std::wstring ArchiveRoot_();

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

#include "modules/IniUtf8.inc.cpp"
#include "modules/ConfigLog.inc.cpp"
#include "modules/LogPack.inc.cpp"
#include "modules/LogContext.inc.cpp"
#define H3_GUARD_CONTEXT_(reason) LogRecentContext_(reason)
#include "modules/CrashGuard.hpp"
#include "modules/BattleArchive.inc.cpp"
// ArchiveSlot 定义于上：依赖它的跨模块前置声明必须放在 BattleArchive 之后。
static bool TryCaptureCombat_(hbs::ArchiveSlot slot = hbs::ArchiveSlot::Manual,
                              const char* origin = "direct");
#include "modules/BattleCodec.inc.cpp"
#include "modules/BattleDiagnostics.inc.cpp"
#include "modules/BattleCapture.inc.cpp"
#include "modules/BattleInputPolicy.hpp"
#include "modules/BattleFingerprint.hpp"
#include "modules/BattleRestore.inc.cpp"
#include "modules/BattleStoreService.inc.cpp"

// 移植（批次四）：协作直连所需的宿主回调。定义在 Entry.inc.cpp（晚于 autoassist
// 区块），因此必须在此前置声明，供 autoassist 内的自动战斗判定直连调用。
static BOOL __stdcall StoreBeforeAutoAction_(void* manager);

// ===== 2026-10-15 H3Auto 功能移植（批次二：纯逻辑核心 + 功能层）=====
// 命名空间隔离由本包含点承担：被包含文件自身都不再开命名空间
// （技能 h3-plugin-multi-merge-port 记录的 MSVC 坑：包含进 namespace 的文件
// 若再开同名 namespace 会 C2065）。源插件的日志/分层 ini/铠甲基建已丢弃，
// AutoStore.inc.cpp 里未定义的名字向外解析到宿主 ConfigLog/IniUtf8 的同名实现。
// nlohmann/json 必须在进入本命名空间之前于全局作用域先包含，否则它会在
// autoassist 内首次展开，与宿主的 nlohmann::json 形成类型分裂。
#include "third_party/nlohmann/json.hpp"
namespace autoassist {
#include "modules/AutoGuardIds.inc.cpp"  // GHID_* → 宿主字符串键铠甲 id（须先于功能层）
#include "modules/AutoUiPort.hpp"        // 界面契约（功能层只经它感知面板）
#include "modules/UiPortHost.inc.cpp"    // g_uiPort 宿主 + 兜底空实现
#include "modules/PolicyCore.hpp"        // 纯策略核心（自带 H3AutoPolicy 与 using 提升）
#include "modules/AutoStore.inc.cpp"     // 方案存档（Games\<地图名>\Auto\*.json v7）+ 自动战斗配置
#include "modules/PanelLayout.hpp"       // 面板布局常量（UiTexts/界面层共用）
#include "modules/UiTexts.inc.cpp"       // i18n 文本表 + lang\*.ini 覆盖
#include "modules/Compat.inc.cpp"        // 游戏全局对象访问垫片（窗口管理器等）
#if !defined(H3BS_UI_NULL)
// 界面层（批次三）：宿主诊断变体（H3BS_UI_NULL）与源插件 H3AUTO_UI_NULL 同口径——
// 不包含界面实现时 g_uiPort 保持 UiPortHost 的兜底空实现，面板/拾取/悬浮物不存在，
// 自动化与生命周期层照常运行。PanelGfx 须排在 SpellRules 之前（沿用源插件顺序）。
#include "modules/PanelGfx.inc.cpp"      // 面板绘制原语 + 字体/PCX 资源加载
#endif
#include "modules/SpellRules.inc.cpp"    // 施法规则（群体/熟练度）
#include "modules/AutoExecute.inc.cpp"   // 自动战斗决策与执行（含两个 HiHook 回调）
#include "modules/ForceField.inc.cpp"    // 力盾选点
#include "modules/AttackMagic.inc.cpp"   // 攻击魔法规则
#include "modules/BattleState.inc.cpp"   // 战斗状态机
#include "modules/BattleLifecycle.inc.cpp" // 战斗生命周期事件源
#if !defined(H3BS_UI_NULL)
// 界面层主体（批次三）：控件 → 设置面板（内含 MagicChannelUi）→ 输入捕获 → 绘制
// （内含 MagicChannelDraw）。SettingsDlg 尾部的 PanelUiPort 适配器在静态初始化期
// 把 g_uiPort 从兜底实现接管为真实面板实现。
#include "modules/CellControl.inc.cpp"   // 面板控件（下拉/列表/按钮/滚动）
#include "modules/SettingsDlg.inc.cpp"   // 设置面板（模态帮助页已移除打包日志按钮）
#include "modules/PanelInput.inc.cpp"    // 面板输入捕获（WH_KEYBOARD/WH_MOUSE）
#include "modules/PanelDraw.inc.cpp"     // 面板内容绘制
#endif

// 入口接线（批次四）：新地址钩子（0x4746B0 / 0x4744D0）注册、每帧段与战斗消息段，
// 以及被宿主同址单壳合并的 0x600430 / 0x4786B0 两段。必须在功能层与界面层之后。
#include "modules/AutoEntry.inc.cpp"
} // namespace autoassist

// 战斗崩溃修复（2026-10-15 迁入，用户裁定：战斗类修复归本插件，随身空间不再携带；
// 源 D:\GitHub\H3\H3BattleCrashFix，曾短暂并入 H3Space 后迁出）。三个防崩型 HiHook
// （障碍/光环/阻止清理），守卫前缀 CrashFix.*。依赖的 CrashGuard/IniUtf8 均在上方
// 全局作用域，包一层独立命名空间即可（与 crashfix 同款 MSVC 坑规避见模块头注）。
namespace crashfix {
#include "modules/CrashFix.inc.cpp"
}

#include "modules/BattleUiPort.hpp"
#if defined(H3BS_UI_NULL)
// 无界面诊断变体（编译期宏，供自动化测试/自动化验证用；与玩家可选的两套界面无关）。
#include "modules/NullUiPort.inc.cpp"
#else
// 两套玩家可选界面同在一个 DLL：BattleUi = 战场外左上角固定条（HdNativeUi）+ 共享绘制/交互；
// BattleFoldBar = 战场内右上角折叠灯（FoldableBarUi）+ 运行期分发器，必须排在其后。
#include "modules/BattleUi.inc.cpp"
#include "modules/BattleFoldBar.inc.cpp"
#endif
#include "modules/BattleAltTail.inc.cpp"
#include "modules/Entry.inc.cpp"
