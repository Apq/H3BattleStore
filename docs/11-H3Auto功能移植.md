# 专题：H3Auto 功能移植（打铁助手 → 战场存档）

> 状态（2026-10-15）：**批次一~四全部完成并提交**（`7446d10`、`a08c5c6`、`9a41bc0`、`eb3ed19`），另有配套提交 `cdc1d21`（配置键/资源/脚本清单）与 `b4e2804`（本文件）；四批都过了 build.bat + 六组 Catch2 单测 + DLL 字节证据，批次三、四另含 `H3BS_UI_NULL` 变体构建；批次五（配置、素材、部署、文案）亦已完成并 `deploy.bat` 部署到 `Packs\热血插件\`（部署副本与构建产物 SHA256 一致）。**剩余只有实机验收，归用户执行**（§9 第 1 项）。
> 前提裁定：用户已决定**移除独立 H3Auto 插件**，只保留合并版，因此本次移植不做共存检测；但**不要同时加载 H3Auto**（见 §1.4）。
> 相关文档：[H3Auto 协作与防冲突](<10-H3Auto协作与防冲突.md>)（两插件并存时代的可选协作 ABI v1，其「经 patcher 注册表查询」的形态在合并版里改为同 TU 直连，见 §6）、[界面与功能分离](<09-界面与功能分离.md>)（宿主既有 UiPort 分层的同构做法）、[文件格式与兼容](<06-文件格式与兼容.md>)（宿主战场档格式，与本次移植的方案档无关）。

## 1. 目标与范围

### 1.1 目标

- 把 H3Auto（打铁助手，`D:\GitHub\H3\H3Auto`，只读参考）的功能合并进 H3BattleStore（`D:\GitHub\H3\H3BattleStore`），宿主保持**单翻译单元**结构：`H3BattleStore.cpp` 按依赖顺序 `#include "modules/*.inc.cpp"`。
- 玩家侧只留一个 DLL 与一套素材：自动战斗（5 套方案、接管判定、主动作提交、施法/攻击魔法、力盾、召唤、保活、自动停止）、设置面板（四页 Tab、下拉、数字框、拾取）、战场热键、i18n 全部由合并版提供。
- 不改变宿主既有契约：档案格式（codec v8 / 二进制容器 v2 / JSON 帧 v1）、恢复事务语义、CrashGuard 铠甲语义（原函数在 `__try` 之外恰好一次）、宿主既有 ini 键名与默认值。
- 每个批次可独立编译、回归、回退，不做一次性大爆炸重写。

### 1.2 非目标

- 不改宿主存档/恢复逻辑本身，也不把 H3Auto 的战场时刻塞进宿主战场档（两者目录与格式各自独立，见 §7.2）。
- 不为旧 H3Auto 方案档（JSON v5/v6）做兼容读——源侧口径本身就是「格式变更不做旧档兼容」，现行只认 v7。
- 不把 H3Auto 的开发工具与展示素材（`tools\*.js`、`img\效果图.pcx`）纳入宿主仓库。

### 1.3 排除项：打包日志（LogPack）

宿主的 `modules\LogPack.inc.cpp`（23023 B，含 `PackRecentLogs_`、`LogPackLzma_`、CF_HDROP 剪贴板）与源插件 `H3Auto\modules\LogPack.inc.cpp`（503 行 / 22667 B）是同一族实现，因此移植侧整套丢弃，只留宿主一份。

| 源侧内容 | 处置 | 依据（宿主现状） |
|---|---|---|
| `H3Auto\modules\LogPack.inc.cpp` | 不复制 | 宿主 `modules\LogPack.inc.cpp` 已有 `PackRecentLogs_(char*, int, char*, int)` 等价入口 |
| `H3Auto\lzma\`（14 个文件：`LzmaEnc.c/.h`、`LzFind.c`、`CpuArch.c`、`7zTypes.h` 等） | 不复制 | 宿主 `lzma\` 已是同一份源码（文件名逐一对齐），只服务宿主自身 LogPack |
| `<shellapi.h>` / `CF_HDROP` 打包用途 | 不移植 | 宿主 `LogPack.inc.cpp:209-239` 自带该部分 |
| `SettingsDlg` 帮助页「打包日志」按钮（源 `SettingsDlg.inc.cpp:1433-1466`，调 `PackRecentLogs_` 与 `help.pack_ok/pack_title/pack_fail`） | **已删** | 批次三 diff `-38/+6`；宿主 `modules\SettingsDlg.inc.cpp:1392-1395` 为说明注释 |
| `PanelDraw` 的 `HELP_PACK_BTN_W/H`、`GetHelpPackBtnRect_`、帮助页第 B 行绘制 | **已删** | 批次三 diff `-25/+7`；宿主 `modules\PanelDraw.inc.cpp:244-279`、`:465` 处为说明注释；声明同步从 `PanelDraw.hpp:13-15` 删除（该文件 diff `-1/+3`） |
| `UiTexts` 的 `help.pack_btn/pack_title/pack_ok/pack_note/pack_fail/pack_no_logs/pack_clipboard_fail` | **已删** | 批次三 diff `-8/+4`；宿主 `modules\UiTexts.inc.cpp:88-95` 现在只剩一段移除说明注释 |
| `lang\zh-CN.ini` 的同名 7 键 | **已删** | 批次五：7 键已删、`:3` 注释改为移除说明（文件 12122 B → 11720 B；`edit` 工具落盘后该文件不再带 BOM，读侧本就兼容无 BOM；内置默认表仍镜像同一批键，改文案要两处同步——本批只删备忘式死键，两侧键集合一致） |
| 使用说明里的「打包日志」段落 | **无需改** | 批次五核对后确认：`使用说明.txt` 全文只描述**宿主悬浮条第一行**那个打包日志按钮（本项目 2026-10-12 起自带），从未写打铁帮助页的按钮，故无需删改；改为在安装节新增「打铁助手已合并、不要再加载 H3Auto.dll」提示 |

「打包日志」功能在合并版里**仍然有，由宿主提供**（控制条按钮 → 宿主 `PackRecentLogs_`，见 `README.md:16-18` 与 [界面与功能分离](<09-界面与功能分离.md>) §2 第 1 条）；被移除的只是移植过来的第二份入口。

### 1.4 加载纪律（必须遵守）

- **不要再同时加载 H3Auto**。合并版与独立版会各自注册同址钩子（0x600430、0x4746B0、0x4744D0、0x4786B0）、各自安装 WH_KEYBOARD/WH_MOUSE、各自画自己的面板，结果是面板重叠、输入被吞两次、资源重复占用；协作 ABI 也不再是设计约束（§6）。
- 部署目录里若还留着旧 `H3Auto.dll`，HD Mod 会照样加载它。源侧 `H3Auto\deploy.ps1:43-47` 就有「部署新包后移除旧包 DLL」的先例；合并版部署同样要把 `H3Auto.dll` 从 `Packs\热血插件\` 移除。**2026-10-15 部署前实测：用户已自行删除 `H3Auto.dll` 与旧版 `H3BattleStore.dll`，该目录现只有合并版一个本插件 DLL，不存在同址双挂。** 该目录另存有历史 `H3Auto_*.log`/`H3BattleStore_*.log`，属日志残留，不影响加载（打包日志按 DLL 基名过滤，互不抓错）。

## 2. 合并架构与隔离策略

### 2.1 包含链（现行，批次三后）

`H3BattleStore.cpp:54-92` 是移植段的唯一入口，逐行如下（注释在原文里更长，这里保留行号语义）：

```cpp
// ===== 2026-10-15 H3Auto 功能移植（批次二：纯逻辑核心 + 功能层）=====
#include "third_party/nlohmann/json.hpp"   // 61：必须在进 namespace 之前
namespace autoassist {                     // 62
#include "modules/AutoGuardIds.inc.cpp"    // 63：GHID_* → 宿主字符串键铠甲 id
#include "modules/AutoUiPort.hpp"          // 64：界面契约（功能层唯一感知面板的通道）
#include "modules/UiPortHost.inc.cpp"      // 65：autoassist::g_uiPort 宿主 + 兜底空实现
#include "modules/PolicyCore.hpp"          // 66：纯策略核心（自带 H3AutoPolicy）
#include "modules/AutoStore.inc.cpp"       // 67：方案存档 + 自动战斗配置
#include "modules/PanelLayout.hpp"         // 68：面板布局常量
#include "modules/UiTexts.inc.cpp"         // 69：i18n 文本表 + lang\*.ini 覆盖
#include "modules/Compat.inc.cpp"          // 70：游戏全局对象访问垫片
#if !defined(H3BS_UI_NULL)
#include "modules/PanelGfx.inc.cpp"        // 75：面板绘制原语 + 字体/PCX 加载
#endif
#include "modules/SpellRules.inc.cpp"      // 77：施法规则（群体/熟练度）
#include "modules/AutoExecute.inc.cpp"     // 78：自动战斗决策与执行（含两个 HiHook 回调）
#include "modules/ForceField.inc.cpp"      // 79：力盾选点
#include "modules/AttackMagic.inc.cpp"     // 80：攻击魔法规则
#include "modules/BattleState.inc.cpp"     // 81：战斗状态机
#include "modules/BattleLifecycle.inc.cpp" // 82：战斗生命周期事件源
#if !defined(H3BS_UI_NULL)
#include "modules/CellControl.inc.cpp"     // 87：面板控件（下拉/列表/按钮/滚动）
#include "modules/SettingsDlg.inc.cpp"     // 88：设置面板（内含 MagicChannelUi）
#include "modules/PanelInput.inc.cpp"      // 89：面板输入捕获（WH_KEYBOARD/WH_MOUSE）
#include "modules/PanelDraw.inc.cpp"       // 90：面板内容绘制（内含 MagicChannelDraw）
#endif
} // namespace autoassist                  // 92
```

三条顺序/条件约束写在包含点注释里（`H3BattleStore.cpp:71-76`、:83-90）：

1. 界面层整体受宿主诊断变体宏 `H3BS_UI_NULL` 保护，与源插件 `H3AUTO_UI_NULL` 同口径——不包含界面实现时 `autoassist::g_uiPort` 保持 `UiPortHost` 的兜底空实现，面板/拾取/悬浮物不存在，自动化与生命周期层照常运行；
2. `PanelGfx` 必须排在 `SpellRules` 之前（沿用源插件顺序）；
3. `SettingsDlg` 必须在 `CellControl` 之后（控件先定义），`PanelDraw` 必须在 `PanelInput` 之后（输入捕获先用绘制几何）。

### 2.2 命名空间包裹

- 隔离由**包含点**承担：`namespace autoassist { ... }` 包住被包含文件；**被包含文件自身一律不开命名空间**。这是技能 `h3-plugin-multi-merge-port` 记录的 MSVC 坑：把文件 include 进 namespace 时，文件内再开同名 namespace 会编译报 C2065。
- 好处：移植代码里的 `g_profiles`、`g_active_rules`、`cfg`、`s_p`、`LoadUiTexts`、`ResetAutoState`、`TryAutoExecuteActiveStack` 等常见名字不会与宿主同 TU 的 `g_store`、`g_ui`、`g_restoreRequest` 等撞名。

### 2.3 基建复用与「未定义名向外解析」

被移植文件里的日志、分层 ini、路径/模块句柄等基建**在源文件里被删掉定义**，未定义的名字在同一个 TU 里向外（宿主文件作用域）解析到宿主实现。同名全局/函数的属主如下：

| 名字 | 宿主属主（唯一实现） | 源侧处理 |
|---|---|---|
| `LogInfo/LogDebug/LogWarn/LogError/LogTrace`、`WriteLogLv` | `modules\ConfigLog.inc.cpp`（`g_log_level` 在 :167，默认 `LOG_DEBUG`） | 源实现删；各文件里 `static void Log*(...)` 前向声明删（`AutoExecute.inc.cpp:5-6`、`BattleLifecycle.inc.cpp:15-16`、`BattleState.inc.cpp:7-10`、`PanelGfx.inc.cpp:10-11`、`CellControl.inc.cpp:7-8`、`PanelInput.inc.cpp:5-8` 的注释/删除点即证据） |
| `IniReadUtf8Layered` / `IniReadIntUtf8Layered` | `modules\ConfigLog.inc.cpp:15-39` | 源同名同签名实现删（源 `ConfigLog.inc.cpp:129`、:147） |
| `IniReadUtf8` / `IniWriteKeyUtf8` / `Utf8ToWideAlloc_` | `modules\IniUtf8.inc.cpp` | 源副本（298 行 / 11903 B）不复制；宿主 11956 B，两份仅注释 2 处不同（:1-4 ini 文件名、:185-190 默认级别说明） |
| `g_ini_path` / `g_user_ini_path` | `modules\ConfigLog.inc.cpp:5-6` | 源 `ConfigLog.inc.cpp:116` 起的同名定义删 |
| `g_hModule` / `g_disable_log` / `g_log_level` | `modules\ConfigLog.inc.cpp:41-42`、:167 | 源定义删 |
| `g_log_path` / `g_log_path_w` / `g_log_base` / `SetupDatedLogPathAndCleanup` / 旧日志清理 | `modules\ConfigLog.inc.cpp:7-12`、:100 起 | 源整段删（含 `ReadDisableLogFromIniFiles`、`CleanupOldLogFilesW`、`LogFileEntryW`） |
| `kPathCap_ = 4MB` | `modules\IniUtf8.inc.cpp` | 源副本删；路径拼接一律堆分配（AGENTS.md 路径缓冲规范），`AutoStore.inc.cpp:424-438` 的 `BattleStorePath_` 是移植侧范例 |
| `[Logging] MinLevel/DisableLog` 读取 | 宿主 `ReadConfig()`（`ConfigLog.inc.cpp:219-228`） | 源 `ReadConfig` 里的 `[Logging]` 段删；宿主是同名函数的唯一属主 |

被删段落的清单在移植脚本 `D:\logs\dsh\scripts\h3auto-port\port-batch2.ps1:52-67`（`D1`～`D8`）里有逐条记录，可用它复核「删了什么、保留了什么」。

### 2.4 铠甲基建

- 源 `H3Auto\modules\CrashGuard.inc.cpp`（304 行）整个丢弃，改用宿主 `modules\CrashGuard.hpp`（497 行，通用自包含版，含 L1 VEH/UEF、L4 SoD 字节门卫 `GuardVerifySodBytes_()`）。
- 宿主铠甲按**字符串键**注册（`GuardRegisterHook_(const char*) -> int`、`GuardCrashFilter_(int, EXCEPTION_POINTERS*)`），因此源侧 `enum GuardHookId { GHID_* }` 的下标语义改用 `modules\AutoGuardIds.inc.cpp` 里的同名常量承载（见 §4），各钩子入口的 `__except (GuardCrashFilter_(GHID_X, GetExceptionInformation()))` **一字未改**。
- 铠甲入口一律手写（技能 `h3-plugin-crash-guard`：禁止宏化、禁止熔断）。

### 2.5 nlohmann/json 必须全局先行

`/D JSON_NOEXCEPTION` 由工程定义（`H3BattleStore.vcxproj`）；`nlohmann/json.hpp` 必须在 `namespace autoassist {` **之前**于全局作用域包含（`H3BattleStore.cpp:61`）。否则它会在 `autoassist` 内首次展开，生成与宿主 `nlohmann::json` 不同的类型（类型分裂），症状是宿主与移植模块之间传 JSON 对象时出现无法解释的模板错误或静默行为差异。

### 2.6 两个 `g_uiPort` 互不干扰

| 指针 | 类型 | 定义点 | 语义 |
|---|---|---|---|
| `autoassist::g_uiPort` | `IH3AutoUi*` | `modules\UiPortHost.inc.cpp:32`（初值兜底空实现；`SettingsDlg.inc.cpp` 尾部的 `PanelUiPort_` 适配器在静态初始化期接管为真实面板） | 移植侧面板契约（帧 tick、消息过滤、绘制、拾取）。`H3BS_UI_NULL` 变体下不接管，保持兜底 |
| `g_uiPort`（宿主，文件作用域 static） | `IBattleStoreUi* const` | `modules\BattleFoldBar.inc.cpp:437-439`、`modules\NullUiPort.inc.cpp:73` | 宿主状态条/折叠灯契约，`Entry.inc.cpp` 只认它 |

两者同名但一个在命名空间内、一个是 TU 内 static，各自解析各自的调用；移植代码里对 `g_uiPort` 的引用（`AutoStore.inc.cpp:92-114`、`AutoExecute.inc.cpp:28`、`BattleState.inc.cpp:14`、`BattleLifecycle.inc.cpp:17`）全部落在 `autoassist::g_uiPort` 上。

### 2.7 编译开关

宿主工程（`H3BattleStore.vcxproj` → `build-batch2.log` 里的 CL 命令行）实际使用：`/std:c++20 /utf-8 /Zp1 /D Z7_ST /D JSON_NOEXCEPTION /D _CRT_SECURE_NO_WARNINGS`，并屏蔽 `4018 4267 4553 4005 4996 4010 4235` 等告警。移植文件沿用同一套开关，不额外加编译条件。

## 3. 模块映射表

### 3.1 批次一：纯逻辑核心（commit `7446d10`）

| 源文件（H3Auto） | 宿主文件 | 处理方式 | 规模 |
|---|---|---|---|
| `modules\PolicyCore.hpp` | `modules\PolicyCore.hpp` | 原样复制（哈希相同）；命名空间 `H3AutoPolicy`，文件末尾 `using H3AutoPolicy::X;` 提升 | 1555 行 / 64374 B |
| `modules\SpellRulesCore.hpp` | 同 | 原样 | 1284 B |
| `modules\TextLayoutCore.hpp` | 同 | 原样 | 1363 B |
| `modules\MagicChannelInputCore.hpp` | 同 | 原样 | 1626 B |
| `modules\PanelInputCore.hpp` | 同 | 原样 | 621 B |
| `modules\BattleStoreCoop.hpp` | 同 | 原样（协作顺序策略；与 §6 的直连化不冲突：策略与传输分离） | 474 B |
| `modules\BattleStoreJsonCore.inc.hpp`（v7 方案 JSON，338 行 / 14765 B） | `modules\AutoProfilesJsonCore.inc.hpp` | **改名**（宿主已有同名 `BattleStoreJsonCore.inc.hpp`，那是战斗存档 JSON v1，9556 B）；内容与源逐字节相同 | 338 行 / 14765 B |
| `modules\CrashGuardCore.hpp`（4783 B） | 不复制 | 宿主 `modules\CrashGuardCore.hpp`（6991 B）是同命名空间 `H3AutoGuard` 的超集 | — |
| `tests\PolicyCoreTests.cpp` | `tests\PolicyCoreTests.cpp` | 转 Catch2 v3 amalgamated（参照 H3Space） | 1501 行；29 用例 / 1609 断言 |

测试侧另含 `tests\MagicChannelJsonTests.inc.cpp`、`tests\MagicChannelInputTests.inc.cpp`，由 `tests\PolicyCoreTests.cpp:1467-1468` 包含。

### 3.2 批次二：功能层（commit `a08c5c6`）

| 源文件 | 宿主文件 | 处理方式 | 规模（宿主） |
|---|---|---|---|
| `modules\ConfigLog.inc.cpp`（972 行 / 40373 B） | `modules\AutoStore.inc.cpp` | **改名 + 去基建化**（D1～D8：删 ini/日志路径与实现、旧日志清理、`[Logging]` 读取；`ReadConfig` → `AutoReadConfig_`；日志标签 `[BattleStore]` → `[方案]`）；保留 5 套方案内存模型、`ClearConfirmedProfiles`、路径推导、JSON v7 IO 与 LRU、热键解析 | 756 行 / 31602 B |
| `modules\AutoExecute.inc.cpp` | 同 | 删日志前向声明（`+2/-1`） | 3038 行 |
| `modules\ForceField.hpp` / `ForceField.inc.cpp` | 同 | 原样 | 13 / 177 行 |
| `modules\AttackMagic.inc.cpp` | 同 | 原样 | 425 行 |
| `modules\BattleState.hpp` / `BattleState.inc.cpp` | 同 | 删日志前向声明（`+1/-2`） | 31 / 195 行 |
| `modules\BattleLifecycle.hpp` / `BattleLifecycle.inc.cpp` | 同 | 删日志前向声明（`+1/-1`） | 8 / 402 行 |
| `modules\SpellRules.inc.cpp` | 同 | 原样 | 56 行 |
| `modules\Compat.inc.cpp` | 同 | 原样 | 187 行 |
| `modules\UiTexts.inc.cpp` | 同 | 批次二原样；批次三删 `help.pack_*`（`-8/+4`） | 271 行 |
| `modules\AutoUiPort.hpp` / `UiPortHost.inc.cpp` | 同 | 原样 | 48 / 32 行 |
| `modules\PanelLayout.hpp` | 同 | 原样 | 263 行 |
| —（新增） | `modules\AutoGuardIds.inc.cpp` | **新建**：`GHID_*` → `AutoAssist.*` 字符串键 | 24 行 / 2039 B |
| `modules\CrashGuard.hpp`（宿主自有） | 同 | 改 `enum { kMaxGuardHooks = 32 }`（原 16）+ 注释（`CrashGuard.hpp:56-58`） | — |

### 3.3 批次三：界面层与资源（commit `9a41bc0`）

| 源文件 | 宿主文件 | 处理方式 | 规模（宿主） |
|---|---|---|---|
| `modules\PanelGfx.inc.cpp` | 同 | 删日志前向声明（`-2`）；受 `H3BS_UI_NULL` 保护 | 715 行 |
| `modules\CellControl.inc.cpp` | 同 | 删日志前向声明（`-1`）；受 `H3BS_UI_NULL` 保护 | 1947 行 |
| `modules\SettingsDlg.inc.cpp` | 同 | 去「打包日志」按钮分支（`-38/+6`）；内含 `MagicChannelUi.inc.cpp`（:672）；受 `H3BS_UI_NULL` 保护 | 2423 行 |
| `modules\PanelInput.inc.cpp` | 同 | 删日志前向声明（`-3`）；受 `H3BS_UI_NULL` 保护 | 1117 行 |
| `modules\PanelDraw.inc.cpp` | 同 | 去打包按钮矩形与帮助页第 B 行绘制（`-25/+7`）；内含 `MagicChannelDraw.inc.cpp`（:1110）；受 `H3BS_UI_NULL` 保护 | 1356 行 |
| `modules\MagicChannelDraw.inc.cpp` | 同 | 原样（被 `PanelDraw.inc.cpp:1110` 包含） | 83 行 |
| `modules\MagicChannelUi.inc.cpp` | 同 | 原样（被 `SettingsDlg.inc.cpp:672` 包含） | 224 行 |
| `modules\PanelDraw.hpp` | 同 | 删 `GetHelpPackBtnRect_` 声明，换成说明注释（`-1/+3`） | 27 行 |
| `modules\PanelInput.hpp` | 同 | 原样 | 21 行 |
| `img\HA_bg.pcx` 等 9 个 | `pcx\HA_*.pcx` | 复制（哈希相同；目标目录 2026-10-15 由 `img\` 改名） | 见 §7.4 |
| `lang\zh-CN.ini` | `lang\zh-CN.ini` | 复制（12122 B，UTF-8 带 BOM）；7 个 `pack_*` 死键待删 | — |

批次三同时把界面层挂进包含链并加了 `H3BS_UI_NULL` 保护（§2.1），因此 `Release\null\H3BattleStore.dll` 是不含面板实现的诊断变体。

### 3.4 未移植文件与原因

| 源文件/目录 | 为什么不移植 |
|---|---|
| `modules\LogPack.inc.cpp`（503 行 / 22667 B） | 宿主有同族实现 `modules\LogPack.inc.cpp`（23023 B，`PackRecentLogs_`） |
| `modules\CrashGuard.inc.cpp`（304 行） | 宿主 `modules\CrashGuard.hpp` 是通用化超集（字符串键 + VEH/UEF + L4 门卫）；源侧枚举 id 由 §4 的映射文件承载 |
| `modules\IniUtf8.inc.cpp`（298 行 / 11903 B） | 宿主同名文件 11956 B 已具备 `IniReadUtf8/IniWriteKeyUtf8/Utf8ToWideAlloc_/kPathCap_`，两份仅注释不同 |
| 源 `ConfigLog.inc.cpp` 的日志部分 | 宿主 `ConfigLog.inc.cpp` 是日志/分层 ini/日志路径的唯一属主（§2.3） |
| `modules\CrashGuardCore.hpp`（4783 B） | 宿主同名文件（6991 B）是同命名空间超集 |
| `lzma\`（14 文件） | 只为 LogPack 存在；宿主 `lzma\` 已有同一份 |
| `img\效果图.pcx` | 展示素材，不进插件包 |
| `tools\*.js` / `tools\*_shot*.ps1` | H3Auto 的开发工具（PCX 生成、截图），与合并功能无关 |
| `H3Auto.default.ini` / `H3Auto.user.ini` 文件名 | 键值并入宿主 `H3BattleStore.default.ini`，不再有独立配置文件（§7.1） |

### 3.5 改名对照（易混淆项）

| 源名 | 宿主名 | 原因 |
|---|---|---|
| `ConfigLog.inc.cpp` | `AutoStore.inc.cpp` | 宿主已有同名文件（配置与日志） |
| `BattleStoreJsonCore.inc.hpp` | `AutoProfilesJsonCore.inc.hpp` | 宿主已有同名文件（战斗存档 JSON v1） |
| `ReadConfig()` | `AutoReadConfig_()` | 宿主 `ReadConfig()` 是唯一属主 |
| 日志标签 `[BattleStore]`（方案存档） | `[方案]` | 避免与宿主「战斗存档」日志混淆 |
| `H3Auto.profiles<N>` 前缀 | `<DLL目录>\H3BattleStore.profiles` 前缀（建议，见 §7.2） | 与宿主 ini 同族 |

## 4. 铠甲钩子 id 映射（`modules\AutoGuardIds.inc.cpp`）

源侧 `enum GuardHookId` 的 8 个下标，在合并版里改为宿主字符串键注册的返回值（同名字、同用法，`__except` 过滤器一字未改）：

| 源枚举名 | 宿主字符串键 | 覆盖的入口 | 对应地址/回调 |
|---|---|---|---|
| `GHID_BLT` | `AutoAssist.AfterBlt` | `Hook_AfterBlt_` 的移植段 | LoHook `0x600430`（每帧） |
| `GHID_MSGPROC` | `AutoAssist.BattleMsgProc` | `Hook_BattleMsgProc` | LoHook `0x4746B0`（战斗消息） |
| `GHID_SHOULD_AUTO` | `AutoAssist.ShouldAuto` | `HH_ShouldAutoExecute` | HiHook `0x4744D0`（接管判定） |
| `GHID_ACTION_EXEC` | `AutoAssist.ActionExec` | `HH_OnBattleActionExecute` | HiHook `0x4786B0`（动作执行） |
| `GHID_KB` | `AutoAssist.CombatHotkey` | `CombatHotkeyKbHook_`（`AutoExecute.inc.cpp:478-499`） | `WH_KEYBOARD` 战斗热键 |
| `GHID_PANEL_KB` | `AutoAssist.PanelKeyboard` | `PanelKbHook_`（`PanelInput.inc.cpp:422`） | 面板 `WH_KEYBOARD` |
| `GHID_PANEL_MOUSE` | `AutoAssist.PanelMouse` | `PanelMouseHook_`（`PanelInput.inc.cpp:470`） | 面板 `WH_MOUSE` |
| `GHID_PANEL_ITEM` | `AutoAssist.PanelItem` | `BlockBattleItemMessage_`（`PanelInput.inc.cpp:640`） | 战场拾取期透明 item 屏障 |

上限与注册：

- `modules\CrashGuard.hpp:58`：`enum { kMaxGuardHooks = 32 }`（原 16）。原因：宿主自用 14 个 + 移植 8 个 = 22 个，留 10 个余量。宿主自用 14 个为 `BattleStore.AltTail`、`BattleStore.Objects`、`BattleStore.ObstacleTransaction`、`BattleStore.Draw`、`BattleStore.CopyPixels`、`BattleStore.Keyboard`、`BattleStore.Mouse`、`BattleStore.Message`、`BattleStore.Execute`、`BattleStore.Spell`、`BattleStore.Cycle`、`BattleStore.AfterBlt`、`BattleStore.Init`、`BattleStore.Lifecycle`。
- 注册发生在**静态初始化期**（`AutoGuardIds.inc.cpp` 的文件作用域常量初始化），早于 DllMain，与宿主自身 `GUARD_*` 注册同款；`s_guard_hooks` 是零初始化数组，单线程写入安全（`CrashGuard.hpp:55-58` 注释）。
- 名称前缀刻意分家：`BattleStore.*`（宿主）与 `AutoAssist.*`（移植），玩家崩溃日志里可直接读归属。
- 超上限时 `GuardRegisterHook_` 会把新钩子并入 0 号（`CrashGuard.hpp:84`），因此以后再加插件必须复核该常量。
- `H3BS_UI_NULL` 变体下 `AutoAssist.PanelKeyboard/PanelMouse/PanelItem` 三个 id 仍会注册，但对应入口所在文件未编译，只是保留编号。

## 5. 钩子清单与同地址合并方案

### 5.1 宿主既有钩子（合并版必须保持唯一注册）

| 地址 | 类型 | 宿主壳 | 铠甲 id | 备注 |
|---|---|---|---|---|
| `0x462600` | HiHook `SPLICE_+EXTENDED_+THISCALL_` | `Hook_BattleStart_` | `BattleStore.Lifecycle` | 战斗开始 |
| `0x462E40` | HiHook 同上 | `Hook_BattleStop_` | `BattleStore.Lifecycle` | 战斗结束 |
| `0x4786B0` | HiHook 同上 | `Hook_BattleExecute_` | `BattleStore.Execute` | **同址合并点 A** |
| `0x5A0140` | HiHook 同上 | `Hook_BattleCastSpell_` | `BattleStore.Spell` | 施法 |
| `0x473A00` | HiHook 同上 | `Hook_CombatMessage_` | `BattleStore.Message` | 游戏消息 |
| `0x476500` | HiHook 同上 | `Hook_AltTailClick_` | `BattleStore.AltTail` | ALT 退后 |
| `0x495C50` | HiHook 同上 | `Hook_CycleCombatScreen_` | `BattleStore.Cycle` | 每帧 |
| `0x600430` | LoHook | `Hook_AfterBlt_` | `BattleStore.AfterBlt` | **同址合并点 B** |
| 游戏窗口线程 | `WH_KEYBOARD` / `WH_MOUSE` | `CombatKeyboardHook_` / `CombatMouseHook_`（`Entry.inc.cpp:563-632`） | `BattleStore.Keyboard` / `BattleStore.Mouse` | 懒安装一次，DETACH 卸载 |

注册点集中在 `Entry.inc.cpp:977-984`（`StartPlugin()`）。

### 5.2 移植侧钩子

| 地址/类型 | 回调 | 源实现位置 | 铠甲 id | 处置 |
|---|---|---|---|---|
| LoHook `0x600430` | `Hook_BltComplete`（合并进 `Hook_AfterBlt_`） | 源 `Entry.inc.cpp:27-52` | `GHID_BLT` | 同址合并（点 B） |
| LoHook `0x4746B0` | `Hook_BattleMsgProc` | 源 `Entry.inc.cpp:59-89` | `GHID_MSGPROC` | **新增**（宿主无此钩子） |
| HiHook `0x4744D0` | `HH_ShouldAutoExecute` | `AutoExecute.inc.cpp:2940` 起 | `GHID_SHOULD_AUTO` | **新增**，`SPLICE_+THISCALL_` |
| HiHook `0x4786B0` | `HH_OnBattleActionExecute`（合并进 `Hook_BattleExecute_`） | `AutoExecute.inc.cpp:750-777` | `GHID_ACTION_EXEC` | 同址合并（点 A） |
| `WH_KEYBOARD`（游戏窗口线程） | `CombatHotkeyKbHook_` | `AutoExecute.inc.cpp:478-499`，安装 :501-512 | `GHID_KB` | 懒安装（`PollControlHotkeys_` :684-686）；DETACH 必须 `ShutdownCombatHotkeys()` |
| 面板 `WH_KEYBOARD` / `WH_MOUSE` | `PanelKbHook_` / `PanelMouseHook_` | `PanelInput.inc.cpp:422`、:470 | `GHID_PANEL_KB` / `GHID_PANEL_MOUSE` | 面板打开时安装（`SettingsDlg.inc.cpp:1074-1080`），关闭时卸载（:1166-1172） |
| 战场拾取期透明 item 屏障（非 Win32 钩子） | `BlockBattleItemMessage_` | `PanelInput.inc.cpp:512`；安装 :796-840（改 `local_vtable[2]`） | `GHID_PANEL_ITEM` | 靠 `StopProcessing` 吞点击，绝不触发部队行动 |

### 5.3 同址合并点 A：`0x4786B0`（HiHook）

- 现状：宿主 `Entry.inc.cpp:764-801` = 前诊断段（`__try` 785）→ `THISCALL_2(orig, mgr, parameter)`（786）→ 后诊断段（`__try` 787-797）→ `__finally { --g_executorDepth; }`（799）；源 `AutoExecute.inc.cpp:750-777` = 先做「玩家人工动作消费单次接管」（读 `This->action`，`CM_ONESHOT_LOCKED` 且 `action ∉ {0,1}` 时 `ClearOneShotManual_()` + `SetControlMode_(...)`），最后 `return THISCALL_2(int, h->GetDefaultFunc(), This, flags)`。
- 合并后单壳（关键：**源回调末尾的原函数调用必须摘掉**，抽成无原调用的段函数，否则原函数会跑两次）：

```cpp
static int __stdcall Hook_BattleExecute_(HiHook* hook, H3CombatManager* mgr, int parameter)
{
    ++g_executorDepth;
    int result = 0;
    int actionBefore = -1;
    __try {
        // ① 宿主前诊断段：读 actionParameter/turn/current/active（得到 actionBefore）
        __try { /* Entry.inc.cpp:771-784 原样 */ }
        __except (GuardCrashFilter_(GUARD_EXECUTE, GetExceptionInformation())) {}

        // ② 移植段：单次接管消费（AutoExecute.inc.cpp:752-772，去掉末尾 return orig）
        __try { autoassist::AutoActionExecuteConsume_(mgr); }
        __except (GuardCrashFilter_(autoassist::GHID_ACTION_EXEC, GetExceptionInformation())) {}

        // ③ 原版函数：在所有 __try 之外，恰好一次
        result = THISCALL_2(int, hook->GetDefaultFunc(), mgr, parameter);

        // ④ 宿主后诊断段：result / action_after
        __try { /* Entry.inc.cpp:787-797 原样 */ }
        __except (GuardCrashFilter_(GUARD_EXECUTE, GetExceptionInformation())) {}
    }
    __finally { --g_executorDepth; }
    return result;
}
```

- 挂钩登记用宿主形态 `SPLICE_, EXTENDED_, THISCALL_`（源形态 `SPLICE_, THISCALL_` 是它的子集，`EXTENDED_` 才能同时拿到 `parameter`/`flags`）。
- 顺序理由：②必须在③之前——它判定的是「玩家真正提交的动作即将进入执行链」，要在原版执行前把锁解掉；①读执行前状态、④读执行后结果，位置不能互换。
- SEH 责任边界：原函数恰一次且在 `__try` 之外，原版自身崩溃照旧传播；①②④ 各自铠甲 id，互不吞对方的故障；`__finally` 只修深度。

### 5.4 同址合并点 B：`0x600430`（LoHook）

- 现状：宿主 `Entry.inc.cpp:884-895` = `g_uiPort->Draw(mgr)`（悬浮条补画，2026-10-05 实测必须在 Blt 之后）；源 `Hook_BltComplete` = `CheckAutoFightDialogClosed()` → 面板存活时 `g_uiPort->OnFramePanelTick()`，否则 `TryAutoExecuteActiveStack(false)`（LoHook 无原函数调用）。
- 合并后单壳：

```cpp
static int __stdcall Hook_AfterBlt_(LoHook* h, HookContext* c)
{
    (void)h; (void)c;
    __try { /* A 段：宿主悬浮条补画（g_uiPort->Draw） */ }
    __except (GuardCrashFilter_(GUARD_BLT, GetExceptionInformation())) {}
    __try { /* B 段：打铁助手每帧（对话框检测 + 面板帧 / 主动作推进） */ }
    __except (GuardCrashFilter_(autoassist::GHID_BLT, GetExceptionInformation())) {}
    return EXEC_DEFAULT;   // 恰好一次
}
```

- 顺序 A→B：A 段保持宿主既有的「Blt 后补画」顺序不被插队；B 段随后做对话框检测与面板帧。两段都不调原函数，`EXEC_DEFAULT` 只返回一次；任一段异常只影响本段（安全默认＝这一帧不画/不推进）。
- 面板打开时的绘制叠加（A 的悬浮条与 B 的面板是否互相覆盖）无法离线判定，列入 §9 实机验收项。

### 5.5 两个新增钩子的细节

- `0x4746B0`（LoHook，`GHID_MSGPROC`）：面板存活时经 `g_uiPort->FilterGameMsg(msg)` 吞并（返回 `NO_EXEC_DEFAULT`）或改写坐标（鼠标移动改离屏清 hover）；面板未打开时调 `TryAutoExecuteActiveStack(true)` 提交 `battle->action`——只写字段、不跳过原函数，原版看到 `action != 0` 会走执行路径。异常安全默认＝`EXEC_DEFAULT`（放行消息），绝不吞消息。
- `0x4744D0`（HiHook，`GHID_SHOULD_AUTO`）：源实现先把 `int orig = THISCALL_1(int, h->GetDefaultFunc(), This)` 放在函数体开头（**在 `__try` 之外**），`orig != 0`（游戏本就不交给玩家）直接返回 `orig`；仅 `orig == 0` 时进 `__try` 做保活/停止/接管判定。异常安全默认＝0（交回玩家）。合并时保持这个结构与调用次数不变。

### 5.6 生命周期（批次四必做）

- `Entry.inc.cpp:988-1031` 的 `DllMain` 需要补：`autoassist::AutoReadConfig_()`、`autoassist::LoadUiTexts()`（`UiTexts.inc.cpp:197`，源侧在 `DllMain` 里调用）、`g_profiles_prefix` 初始化（§7.2）。
- `DLL_PROCESS_DETACH`（`Entry.inc.cpp:1023-1029`）需要补 `autoassist::ShutdownCombatHotkeys()`（`AutoExecute.inc.cpp:514-520`）与 `autoassist::ResetAutoState()`（源侧 DETACH 两者都调）；宿主自己的 `g_combatKeyboardHook/g_combatMouseHook` 卸载与 `GuardShutdown()` 保持原样。
- 面板的 `WH_KEYBOARD/WH_MOUSE` 由面板关闭路径卸载（`SettingsDlg.inc.cpp:1166-1172`），不需要额外补。

## 6. 协作机制直连化改造

两插件时代的协作 ABI（[协作与防冲突](<10-H3Auto协作与防冲突.md>) §2）靠 patcher 注册表 + 名字查询；合并成同 TU 后，移植侧不再需要「动态查询、加载顺序无关」，改为直接调用宿主实现。

| 源协作键 | 源调用点（`AutoExecute.inc.cpp`） | 宿主真实符号 | 直连后的调用 |
|---|---|---|---|
| `H3BattleStore.RestoreEpoch.v1` | `:2868-2871`（`_P->VarFind` + `GetValue`） | `g_storeRestoreEpoch`（`BattleStoreService.inc.cpp:498`，写侧 :697-698） | 读宿主恢复成功计数（整场恢复成功才 +1） |
| `H3BattleStore.BusyQuery.v1` | `:2885-2890`（`VarGetValue` → 函数指针） | `StoreAutomationBusy_()`（`BattleStoreService.inc.cpp:493-497`） | 直接函数调用（保存/排队/pending/fatal 期间为真） |
| `H3BattleStore.BeforeAutoAction.v1` | `:2892-2895`（`VarGetValue` → 函数指针） | `StoreBeforeAutoActionBody_()`（`BattleStoreService.inc.cpp:499-512`），注册壳 `StoreBeforeAutoAction_`（`Entry.inc.cpp:556-561`） | 直接调用函数体；注册壳可保留给外部查询 |

改造约束：

- 只能删「传输层」（`_P->VarFind/VarGetValue` 三处 + 函数指针 typedef），**不能改判定顺序**：忙态 → 恢复通知 → Before 窗口 → 管理器身份仍一致。该顺序由 `modules\BattleStoreCoop.hpp` 承载，并被 `tests\PolicyCoreTests.cpp` 的 `[policy] 与存档协作顺序（忙态/恢复通知/首档）` 用例覆盖（`TestBattleStoreCoop()`）。
- `BattleStoreRestoreNotify_` 的 `__try/__finally`（`:2873-2880`：置处理闸 → `SetControlMode_(CM_MANUAL)` → `ResetAutoState()` → `BindStackTrackingFromBattle_()`）与 SEH 语义一字不改。
- `Entry.inc.cpp:972-975` 的 `VarInit` 注册是否保留**待裁定**：合并版已无消费者，保留无害（可供第三方或调试查询），删除则要同步更新 [协作与防冲突](<10-H3Auto协作与防冲突.md>) §2 的口径。无论去留，都必须保证该注册/直连发生在 `StartPlugin()` 之前且不引入顺序依赖。

## 7. 配置与数据

### 7.1 ini 节/键迁移表

配置文件从两套（`H3Auto.default.ini` / `H3Auto.user.ini`）并成一套（`H3BattleStore.default.ini` / `H3BattleStore.user.ini`，路径在 `Entry.inc.cpp:1007-1008` 生成）。

| 节 | 键 | 归属与现状 |
|---|---|---|
| `[General]` | `DisableOnStart` | 移植侧读（`AutoStore.inc.cpp:695-696`，0/1 夹取）；宿主 `default.ini` **尚无 `[General]` 段**，批次五补出厂默认 |
| `[General]` | `LastProfile` | 移植侧：面板勾号生效时写玩家层（`AutoStore.inc.cpp:128-135`），启动读回并夹取 1..5（:700-706） |
| `[General]` | `Language` | 移植侧读（`UiTexts.inc.cpp:200`，默认 `zh-CN`，字符集校验后回退） |
| `[Hotkeys]` | `ToggleManual` / `OneShotManual` / `OpenSettings` | 移植侧读（`AutoStore.inc.cpp:711-728`，默认 `F9`/`J`/`P`）；非法或冲突时回退并 `LogWarn` |
| `[Hotkeys]` | `SaveKey` | 宿主既有（`H3BattleStore.default.ini:11-13`，默认 `G`，避开 HoD 占用的 `Z`） |
| `[Logging]` | `DisableLog` / `MinLevel` | **宿主唯一属主**：`ConfigLog.inc.cpp:63`（DisableLog）、:219-228（MinLevel，默认 `debug`）；源 `ConfigLog.inc.cpp:850` 的 `[Logging]` 读取已删（port-batch2.ps1 `D8`）。源默认 `info`、宿主默认 `debug`，合并版以宿主为准 |
| `[Ui]` | `OutsideBattlefield` | 宿主既有（`H3BattleStore.default.ini:15-19`），与移植无冲突 |
| `[Store]` | `saveFormat` | 宿主既有（:21-25），与移植无冲突 |

遗留文案：**批次五已清**——`AutoStore.inc.cpp:127`、:694-699 的注释改为「H3BattleStore.user.ini」；`UiTexts.inc.cpp:178` 的举例注释改为通用描述（不再提已删的 `help.pack_ok`）；`lang\zh-CN.ini:3` 注释改为移除说明。实际写入/读取的一直是宿主文件（`g_user_ini_path`/`g_ini_path`）。

### 7.2 方案存档：路径、JSON v7、LRU

- 目录：`<游戏根>\Games\<地图名>\Auto\<16 位大写 hex 战斗指纹>.json`（`AutoStore.inc.cpp:255-276` 的段落注释与 `BattleStorePath_` :424-438）。
- 回退链（`BattleStoreDirUtf8_` :378-403）：
  1. `Games\<地图名>\Auto`（地图名读 HD 游戏名链 `*(char**)(*(DWORD*)0x699538 + 0x1fb40)`，NULL/空白 → `Unnamed`；字符清洗同 HD `FUN_010e2e80`：拒 `../:`、控制字符与 `<>"|?*`；每次现读现拼不缓存）；
  2. 链不可读 → `Games\Auto`（:393-398 一次性 `LogWarn`）；
  3. 找不到 `Games` 根（从 DLL 目录向上最多 8 级探测，:301-329）→ DLL 同目录（`DllDirUtf8_` :281-296，**依赖 `g_profiles_prefix`**）。
- 文件内容（v7）：`{"version":7,"battle":"<hex16>","entries":[{"time":"...","active":1..5,"profiles":[5 套]}]}`；每套按字段展开 `stopTurns`、`summon`、`army[]`（`rule.waitMode` 0=无 / 1=每回合 / 2=不反兵攻城；`rule.siegeSafeHex` 仅该行动且已设置时写）、`status`（`attacks` 必需）、`forcefield`（`anchor_hex` 两整数数组）。只接受 v7 且指纹匹配，v7 之前一律视为没有存档（不做旧档兼容）。
- 保留策略：同场文件内 `kBattlesKeep_ = 30`（`AutoStore.inc.cpp:277`），内容与最后一条相同则跳过不新增（:600-604），超限丢最老（:605-607）；跨文件再按 mtime 降序保留最近 30 场（`PruneBattleStore_LRU_` :624-690，宽字符 API——UTF-8 字节喂 `FindFirstFileA` 会在中文地图名目录里静默查不到）。
- `g_profiles_prefix`：源侧是 `<DLL目录>\H3Auto.profiles`（源 `Entry.inc.cpp:194-199`，每槽一文件的旧前缀）；合并版只用于推导 DLL 目录兜底。**批次四已改为在 `autoassist::AutoInit_()` 里经 `InitAutoPaths_()` 初始化**（`modules\AutoEntry.inc.cpp`，值 `<DLL目录>\H3BattleStore.profiles`），由宿主 `StartPlugin` 在版本门卫之后调用；它是 `static char* = new char[kPathCap_]`，只被 `DllDirUtf8_` 用来剥掉文件名取目录，不再是"方案文件前缀"。
- 与宿主战场档彻底分家：宿主档在 `<存档根>\<64hex 指纹>\手动|自动\...`（binary/json），移植方案档在 `Games\<地图名>\Auto\`，两者互不读取、互不清理。

### 7.3 i18n

- 加载链（`UiTexts.inc.cpp:2-5`、:197-268）：`[General] Language`（默认 `zh-CN`）→ `<DLL目录>\lang\<语言>.ini`（键名＝内置表的 `section.key`，如 `panel.title`、`actions.*`、`selectors.*`，读法由 `IniReadUtf8` 完成）→ 命中即写入覆盖缓冲（512 B/键）；缺文件或缺键回落内置默认表（`UiTexts.inc.cpp:21` 起，与 `lang\zh-CN.ini` 镜像，改文案要两处同步）。
- 语言名做字符集校验（只允许字母、数字、`-`、`_`），非法回退 `zh-CN`（:201-210）。
- `lang\zh-CN.ini` 现 **11720 B、UTF-8 无 BOM**（删 7 个 `pack_*` 键后由 12122 B 变更；读侧无 BOM 亦兼容，故不影响加载），与内置默认表的键集合一致；改动文案时两侧都要改。BOM 的得失另注：`edit` 工具重写文件时不保留原 BOM，属工具行为，不是手工转码。

### 7.4 资源清单（`pcx\`，2026-10-15 由 `img\` 改名，与 H3MegaDesc 同规）

| 文件 | 字节 | 用途（按文件名与布局常量对应） |
|---|---|---|
| `HA_bg.pcx` | 1089560 | 设置面板底图（`PanelLayout.hpp:5` 注明 680×548，底部含状态栏木纹带） |
| `HA_cell.pcx` | 6152 | 部队卡片底图（`CELL_W 511 × CELL_H 88`） |
| `HA_grid_frame.pcx` | 22652 | 内容区边框（`GRID_FRAME_*`） |
| `HA_icon_frame.pcx` | 908 | 图标框 |
| `HA_button_frame.pcx` | 662 | 按钮框 |
| `HA_ok_normal.pcx` / `HA_ok_pressed.pcx` | 5768 / 5392 | 确定按钮常态/按下 |
| `HA_cancel_normal.pcx` / `HA_cancel_pressed.pcx` | 4289 / 4362 | 取消按钮常态/按下 |
| `HB_bg.pcx`（宿主既有） | 88119 | 宿主控制条底图（480×48，源自 `HA_bg.pcx` 中段裁切，见 `docs\变更记录.md` 2026-10-13 条） |

宿主与移植素材同目录共存不冲突：宿主用 `HB_*`，移植用 `HA_*`（`deploy.ps1` 注释同款口径）。`H3Auto\img\效果图.pcx` 不复制。目录名 2026-10-15 经用户裁定由 `img\` 改为 `pcx\`，仓库目录、部署目标目录与 DLL 内加载路径（`BattleUi.inc.cpp` 的 `L"pcx\\HB_bg.pcx"`、`PanelGfx.inc.cpp` 的 `"pcx\\"`）三处同步；两个加载器都带**旧布局兜底**（`pcx\` 打开失败后重试 `img\`，各以 `static bool` 只 `LogInfo` 一次），老安装目录未改组时不会面板整块无图或悬浮条无底图。

### 7.5 部署（批次五，已完成）

- 目标目录：`<游戏目录>\_HD3_Data\Packs\热血插件`（`deploy.ps1:13`，与 H3RndNew 共用；历史上曾用 `战场存档`）。
- `deploy.ps1` 现复制 DLL、`H3BattleStore.default.ini`、`使用说明.txt`、`pcx\HB_bg.pcx`、`pcx\HA_*.pcx`（glob，9 个）、`lang\zh-CN.ini`（`deploy.ps1` 的三个 Copy-Item 段），并建 `pcx\`/`lang\` 子目录；实测部署后 `pcx` 下 HA_* 9 个齐全、`lang\zh-CN.ini` 就位。
- `pack.ps1` 显式清单由 4 条扩到 14 条（同批资源全含），保持「不扩为整个 Release 目录」的既有口径。
- 版本号由用户手动维护（AGENTS.md 口径），agent 不自动 bump。
- `README.md` 已同步为新口径（`Packs\热血插件\`、`D:\Heroes3\Heroes3_2026.10.09`，并列出 `pcx\HA_*.pcx` 与 `lang\zh-CN.ini`）；`使用说明.txt` 的安装节同批更新，并新增「不要再同时加载 H3Auto.dll」提示。

## 8. 分批实施与验收状态

### 8.1 每批验收口径（四件套）

1. `build.bat`（清代理 + MSBuild `Release|Win32`）**0 error / 0 warning**；
2. 六组 Catch2 单测 exit 0：`tests\run-unit-tests.ps1:24` 的 `ArchiveTests`、`CodecTests`、`LogControlsTests`、`UiPortTests`、`AltTailTests`、`PolicyCoreTests`（Catch2 v3.16.1 amalgamated；`LogControlsTests` 额外链 `lzma\LzmaEnc.c`、`LzmaFind.c`、`CpuArch.c`，带 `/DZ7_ST`）；
3. DLL 字节证据：目标标记串在产物里可检索（如 `AutoAssist.*`、协作键名、面板文案）；
4. 编码归一：新写的 `.cpp/.hpp/.md` 一律 UTF-8 无 BOM + CRLF（`.ps1` 要 BOM、`.bat` 保持 GBK，见 AGENTS.md）。

界面层相关批次（批次三起）另加 `Release\null\` 的 `H3BS_UI_NULL` 变体构建。

### 8.2 批次一（已完成，commit `7446d10`）

- 交付：6 个纯逻辑核心 + 改名后的方案 JSON 核心 + Catch2 版 `PolicyCoreTests.cpp`（1501 行）。
- 证据：`tests\PolicyCoreTests.cpp` 29 用例 / 1609 断言通过（见 §8.3 六组日志末段）。

### 8.3 批次二（已完成，commit `a08c5c6`）

| 项 | 证据 |
|---|---|
| 编译 | `D:\logs\dsh\scripts\h3auto-port\build-batch2.log`（2026-10-15 12:24:49）末三行：「已成功生成。」「0 个警告」「0 个错误」，用时 8.00 s |
| 单测 | `D:\logs\dsh\scripts\h3auto-port\tests-batch2.log`（12:26:15）：Archive 217 断言/12 用例、Codec 4224/6、LogControls 30167/3、UiPort 39/3、AltTail 14/7、PolicyCore 1609/29，六组全 `All tests passed`，末尾 `PASS all unit tests` |
| DLL 字节证据 | `Release\H3BattleStore.dll` 496128 B，SHA256 `074707D81B899440F0A7B93FB956A28A5268EFB70731BE9189D3FBABD4214B97`（12:24:48）：8 个 `AutoAssist.*` 键、`打铁设置`、`H3BattleStore.BusyQuery.v1`、`H3BattleStore.BeforeAutoAction.v1`、`H3BattleStore.RestoreEpoch.v1` 全部命中 |

### 8.4 批次三（已完成，commit `9a41bc0`）

| 项 | 证据 |
|---|---|
| 编译（默认变体） | `D:\logs\dsh\scripts\h3auto-port\build-batch3.log`（12:32:22）：「已成功生成。」「0 个警告」「0 个错误」，用时 8.74 s |
| 编译（`H3BS_UI_NULL` 变体） | `D:\logs\dsh\scripts\h3auto-port\build-batch3-null.log`（12:33:34）：0 警告 0 错误，用时 7.33 s |
| 单测 | `D:\logs\dsh\scripts\h3auto-port\tests-batch3.log`（12:34:52）：Archive 217/12、Codec 4224/6、LogControls 30167/3、UiPort 39/3、AltTail 14/7、PolicyCore 1609/29，六组全 `All tests passed` + `PASS all unit tests` |
| DLL 字节证据 | 默认 `Release\H3BattleStore.dll` 674816 B，SHA256 `16E68988F7DF110B8B79EEE96AB350A5B3F00F3F96DE09FE9D82A7B6F6341D86`（12:32:21）；NULL `Release\null\H3BattleStore.dll` 335360 B，SHA256 `E38AFC68C4C21C7B1CD174E027ED7F7D0BFF91FA9C62BA8EA32CFC5A5696660E`（12:33:33）。两个产物的 `AutoAssist.AfterBlt`、`AutoAssist.PanelMouse`、`打铁设置` 均命中（NULL 变体仍带文案表与 id 注册，只是不编界面实现） |
| 遗留 | `lang\zh-CN.ini` 的 `pack_*` 死键已于批次五删除（§1.3、§8.6）；本批的 NULL 变体哈希为批次三值，批次四后重新构建为 396800 B（§8.5），跨批次比哈希无意义 |

### 8.5 批次四（已完成，commit `eb3ed19`）：`Entry.inc.cpp` 合并

1. `0x600430` 单壳合并（§5.4）——宿主 `Hook_AfterBlt_`（`Entry.inc.cpp:993` 唯一注册）先画悬浮条，再 `__try` 调 `autoassist::AutoBltFrameSegment_()`，铠甲用 `autoassist::GHID_BLT`；
2. `0x4786B0` 单壳合并（§5.3）——宿主 `Hook_BattleExecute_`（`Entry.inc.cpp:998` 唯一注册）在 `THISCALL_2(orig)` **之前**插入 `autoassist::AutoOnActionExecuteSegment_(reinterpret_cast<autoassist::_BattleMgr_*>(mgr), parameter)`（铠甲 `GHID_ACTION_EXEC`），源回调的原函数调用已摘除（抽成段函数），原版函数由宿主壳在 `__try` 之外恰好调用一次；
3. 新增 `0x4746B0`（LoHook）/`0x4744D0`（HiHook）——由 `AutoInstallHooks_()` 注册（`modules\AutoEntry.inc.cpp:106-107`），位置在宿主 `StartPlugin` 的 SoD 版本门卫之后；
4. 协作机制直连化（§6）——`StoreAutomationBusy_()`/`g_storeRestoreEpoch`/`StoreBeforeAutoAction_(void*)` 直连，全仓已无 `VarFind`/`VarGetValue`；
5. 启动接线：`StartPlugin` 调 `autoassist::AutoInit_()`（= `InitAutoPaths_()` + `AutoReadConfig_()` + `LoadUiTexts()`）与 `AutoInstallHooks_()`；`DETACH` 调 `AutoShutdown_()`（`ShutdownCombatHotkeys()` + `ResetAutoState()`）；
6. 热键与面板钩子安装/卸载路径各只有一处：`EnsureCombatKbHook_()` 惰性自装（首次进 `PollControlHotkeys_` 路径时），卸载集中在 `AutoShutdown_()`；
7. 验收：默认与 `H3BS_UI_NULL` 两变体构建各 **0 警告 0 错误**（`build-batch4.log`/`build-batch4-null.log`，末次复跑为 `build-final.log`/`build-final-null.log`）；六组 Catch2 单测 exit 0（`tests-batch4.log`、末次 `tests-final.log`：Archive 217/12、Codec 4224/6、LogControls 30167/3、UiPort 39/3、AltTail 14/7、PolicyCore 1609/29）；默认 `Release\H3BattleStore.dll` 727552 B SHA256 `E3DD935AEDACAD4D5EACE209072D576637AF251EF5145C4280B0A56F8FC31637`，NULL `Release\null\H3BattleStore.dll` 396800 B SHA256 `587DFC0A2F5401D8FD5EDC50960B1CB88D69D9B02B258BBC852CB74EF11EB732`；字节证据见 §8.6。**实机验收待用户**。

### 8.6 批次五（已完成，commit `cdc1d21` + 文档提交）：配置、素材、部署、文案

1. `H3BattleStore.default.ini` 增 `[Hotkeys] ToggleManual=F9/OneShotManual=J/OpenSettings=P` 与 `[General] DisableOnStart=0/LastProfile=1/Language=zh-CN`（出厂默认对齐源插件；分层读取器与宿主同源，故键写在宿主 ini）；
2. `lang\zh-CN.ini` 的 7 个 `pack_*` 键已删（`UiTexts` 侧批次三已删），§7.1 列出的遗留注释全部改毕；
3. `deploy.ps1` 复制 `lang\zh-CN.ini` 与 `pcx\HA_*.pcx`；`pack.ps1` 清单扩到 14 条；部署目录旧 `H3Auto.dll`（用户已删）不复存在；
4. `使用说明.txt` 补「打铁助手已合并 / 删掉旧 H3Auto.dll / 热键 F9-J-P / 方案库与战斗存档的区别」与配置示例；`README.md` 目录与文件清单口径对齐、新增合并小节与「不要再同时加载 H3Auto.dll」纪律；
5. 验收：`deploy.bat` exit 0，部署副本与构建产物 SHA256 一致（727552 B `E3DD935A…`），`pcx\HA_*.pcx` 9 个与 `lang\zh-CN.ini` 就位；仓库与部署副本的 `使用说明.txt`/`default.ini`/`lang\zh-CN.ini` 哈希逐一同；`git diff --check` exit 0，`README/使用说明/变更记录/docs/11` 均 UTF-8 无 BOM + 全 CRLF。**打包日志按钮归属**：保留的是宿主悬浮条第一行那个（本项目自带），删掉的是打铁设置面板帮助页里重复的那个。


## 9. 风险与待验证项

1. **同址钩子合并的行为只能实机验收**。`0x600430`、`0x4786B0` 合并后「段顺序、原函数恰一次、`g_executorDepth` 深度、面板帧与悬浮条绘制叠加」都无法由离线单测证明；尤其点 A 的②段必须保证原函数不在段内被调用。
2. **面板与宿主状态条并存时的交互**。合并版里宿主的常驻状态条（`WH_KEYBOARD`/`WH_MOUSE`＋每帧绘制）与移植面板（自带 `WH_KEYBOARD`/`WH_MOUSE`＋模态吞并）同在一个 DLL、同一游戏窗口线程，钩子链上会出现两个插件侧钩子；面板存活期间的读档/自动存档闸（`g_storeWaitSaveUntil`）、`g_restoreBusy` 与面板吞并的先后需要实机确认。
3. **`g_profiles_prefix` 未初始化**（当前 `AutoStore.inc.cpp:126` 只分配未填值）会让最深兜底路径（找不到 `Games` 根）写不出文件；**批次四已完成初始化**（`AutoEntry.inc.cpp` 的 `InitAutoPaths_()`，经 `AutoInit_()` 在版本门卫之后调用），此风险已消除。
4. **源文档 JSON v5 的口径已过时**。源 `H3Auto\docs` 多篇仍写 v5：`01-总体架构与Hook.md:97`、`03-英雄施法与自动停止.md:35-36`、`04-方案模型与部队身份.md:159`、`06-设置面板与输入绘制.md:135/149`、`11-验证边界与待办.md:30/38`、`README.md:17`、`设计文档.md:15/25`；只有 `07-战斗指纹与方案存档.md:50` 记的是现行 v7（v5→v6 加 `waitMode` 等待方式；v6→v7 行动新增 `AA_SIEGE`、等待方式第 3 项语义改为不反兵攻城、新增 `siegeSafeHex` 字段）。合并版文档与代码一律以 **v7** 为准。
5. **残留源文案/注释**：`AutoStore.inc.cpp:127`、:694-699 与 `UiTexts.inc.cpp:193` 仍写 `H3Auto.user.ini`/`H3Auto.default.ini`；`lang\zh-CN.ini:69-75` 仍是 7 个 `pack_*` 死键（`:3` 注释与 `UiTexts.inc.cpp:178` 的举例注释也需同步）。功能无影响，但玩家日志与后续维护会被误导。
6. **配置文件多写者**：宿主 `[Logging] MinLevel`（UI 点击写）与移植 `[General] LastProfile`（勾号写）都走 `IniWriteKeyUtf8(g_user_ini_path, ...)` 的整体读改写；同线程同步调用风险低，但同一次会话内连续写需要实测确认不互相丢键。
7. ~~**部署口径不一致**~~（已消除）：`README.md` 与 `deploy.ps1` 现同为 `Packs\热血插件`、`D:\Heroes3\Heroes3_2026.10.09`；`deploy.ps1` 亦已复制 `lang\`/`HA_*` 素材，素材目录 2026-10-15 起统一为 `pcx\`（`deploy.ps1` 建 `pcx\`/`lang\` 两个子目录）。
8. **双加载**：旧 `H3Auto.dll` 若还在 `Packs\热血插件\`，HD 会同时加载两个插件，面板/钩子/资源冲突重复（§1.4）。合并版部署必须核对该目录。
9. **铠甲上限**：`kMaxGuardHooks` 16→32 后合计 22 个钩子 id；以后并第三个插件前必须复核，超出会被静默并入 0 号（`CrashGuard.hpp:84`），故障归属将不可读。
10. **`[Logging] MinLevel` 默认值差异**：宿主 `debug`、源 `info`。合并版以宿主（唯一属主）为准，文档与使用说明需要按宿主口径描述。
11. **`H3BS_UI_NULL` 变体的覆盖面**：该宏只挡界面实现文件，不挡功能层对 `g_uiPort` 的调用——NULL 变体下这些调用落到 `UiPortHost` 的兜底空实现（`IsPanelActive()` 恒 false）。新增移植代码时不得假设面板一定存在，也不得把关键逻辑只写进界面实现。
12. **未验证的边界**：`lang\*.ini` 与 `HA_*.pcx` 缺文件时的回退（回退内置表＋面板绘制失败）只做过代码路径检查，没有实机验证；`Games` 之外的自定义地图名清洗（含 `..`、控字符）只有策略层/单测覆盖。

## 10. 本文件的事实来源

| 类别 | 文件 |
|---|---|
| 宿主 TU 与包含链 | `H3BattleStore.cpp:36-105` |
| 宿主模块 | `modules\` 全部（行数/字节经 PowerShell 实测）、`modules\CrashGuard.hpp:1-100`、`modules\ConfigLog.inc.cpp:1-45` |
| 移植侧新增 | `modules\AutoGuardIds.inc.cpp`、`modules\AutoStore.inc.cpp`（:1-150 / :240-440 / :560-756）、`modules\AutoExecute.inc.cpp`（:470-520 / :665-704 / :745-794 / :2840-2959）、`modules\UiTexts.inc.cpp`（:1-30 / :181-268）、`modules\SettingsDlg.inc.cpp`（:672 / :1063-1092 / :1166-1172 / :1391-1395 / :2343-2420）、`modules\PanelDraw.inc.cpp:1-2,1110`、`modules\PanelInput.inc.cpp:422/470/512/640/796-840`、`modules\PanelLayout.hpp:1-45` |
| 宿主 Entry / 服务 | `modules\Entry.inc.cpp`（:6-14 / :480-660 / :740-1032）、`modules\BattleStoreService.inc.cpp:485-524` |
| 源插件（只读） | `H3Auto\modules\Entry.inc.cpp`（全文）、`H3Auto\modules\ConfigLog.inc.cpp`（签名/键位）、`H3Auto\H3Auto.default.ini`、`H3Auto\deploy.ps1`、`H3Auto\docs\07-战斗指纹与方案存档.md:50` 等 |
| 构建与测试证据 | `D:\logs\dsh\scripts\h3auto-port\build-batch2.log`、`tests-batch2.log`、`build-batch3.log`、`build-batch3-null.log`、`tests-batch3.log`、`port-batch2.ps1`、`D:\logs\dsh\scripts\h3auto-port-doc\common-static-funcs.txt` / `common-static-vars.txt`、`tests\run-unit-tests.ps1`、`tests\PolicyCoreTests.cpp` |
| 其它 | `deploy.ps1`、`H3BattleStore.default.ini`、`lang\zh-CN.ini`、`pcx\` 清单、`git log`（`7446d10`、`a08c5c6`、`9a41bc0`）、`Release\H3BattleStore.dll` 与 `Release\null\H3BattleStore.dll`（大小/SHA256/标记串实测） |
