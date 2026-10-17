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

### 5.7 施法通道的判定时机，与消息入口补判（2026-10-15 玩家反馈修复）

**机制事实（本次定案）**：施法四通道（力盾由 `EnsureForceFieldBeforeAction_` 承担；保活/保持状态/召唤走 `TryProtectCast_`；智能攻击走 `TryAttackMagicCast_`）原先只在 `0x4744D0` 的 **`orig == 0`（控制权交给玩家）** 时刻判——`HH_ShouldAutoExecute` 里 `TryProtectCast_(This)` 是全局唯一调用点，其后才是 `TryAutoStop_` 与 `TryAttackMagicCast_`。战斗消息入口 `TryAutoExecuteActiveStack`（`0x4746B0` 段与每帧段调用）只做力盾、接管判定与动作提交，**不判施法**。而启停热键是在消息入口轮询的（`PollControlHotkeys_`）——因此**按下 F9 切到全自动时，当前部队的交权判定已经过去，当回合不会补施**；玩家要手动按一次「等待」让该部队本回合再次行动，才会走新的一次交权而施法（玩家实测现象与代码路径完全吻合）。

**日志可见性**：施法成功的留痕是 info 级（`[Protect] cast done`、`[Status] cast done`、`[Summon] 召唤成功`、`[AttackMagic] submit`），而判定的中间过程（逐队 `[Protect] queue/cand`）与 `heroCasted` 挡板是 debug 级；`g_control != CM_AUTO`（非全自动不判）这条分支**没有任何日志**。所以玩家默认的 info 级日志只能证明「没施法」，不能说明原因——排查此类反馈必须让玩家临时把日志级别切到 debug：**战场存档界面悬浮条第一行**的日志等级单选列表点「调试」即写入 `H3BattleStore.user.ini` 的 `[Logging] MinLevel` 并立即生效（留痕 `[Config] 日志等级已切换并写入用户配置：…`）。打铁帮助页自 2026-10-16 起不再提供该下拉（见 §7.7）。

**修法（A + B，均在消息入口，且必须在提交动作之前执行）**：

- `TryProbeCastChannelFromMsg_(_BattleMgr_* mgr, bool force)`（`AutoExecute.inc.cpp`，`TryAutoExecuteActiveStack` 上方）：门槛＝`mgr->action == 0`（动作未落地）＋ 与 `TrySubmitConfiguredAction_` 同口径的「自动战斗/隐藏战斗/战术期」排除；随后按交权路径同序调 `TryProtectCast_` 与 `TryAttackMagicCast_`。施放与否仍由通道内部静默判定（已施法/无法施法/方式不保活/阈值未达/无尸体格），本函数只负责「补上错过的判定」。
  - **A**（`force=false`）：判为 `CD_EXECUTE_H3AUTO` 时补判，闸门＝**每回合最多一次**（静态 `s_gate_turn`），只补错过的时刻，不改变交权路径既有的「每次行动都判」节奏。
  - **B**（`force=true`）：全局 `g_cast_probe_pending` 由 `ToggleBattleManual_()` 在**切到全自动**时置位，消息入口当帧强制补判一次——不看当前部队是否已配置规则，覆盖「当前部队是手动规则/等待中，`DecideTakeover` 不返回代发」的情形。交权路径在 `TryProtectCast_(This)` 之后消费该标志（避免下一帧重复补判）。
- 顺序约束：补判发生在 `DecideTakeover` 之后、`TrySubmitConfiguredAction_` **之前**（此刻 `action` 仍为 0）；这与交权路径「先施法、钩子返回 0、消息入口再提交动作」的实际时序一致。
- 铠甲：补判函数在 `TryAutoExecuteActiveStack` 既有的 `__try` 内被调用（与交权路径把 `TryProtectCast_` 放在 `__try` 内同规），不新增钩子、不改钩子入口结构、原版函数调用次数不变。

**同批修掉的日志缺陷**：`UpdateStackTracking_` 的 `[Track] identity lost slot=%d expect=0x%X got=0x%X` 两个实参都取 `creature_id`（常打出同一个数，无法判断是哪个身份分量不等），改为打印完整身份元组的「期望/当前」对照 + 代次 + 生物 id：`id(k=kind s=side v=value o=occurrence)`。判等口径（`H3AutoPolicy::StableStackIdentityEquals`：kind/side/value/occurrence 全等，任一侧 `STACK_ID_NONE` 即不等）未改。

**验证边界**：两变体 0 错 0 警、七组单测全绿、部署哈希一致；「按 F9 后当回合是否立即补施、『等待』不再是必须步骤」属游戏内行为，**由用户实机验收**。

### 5.8 每回合施法时机玩家可配（2026-10-16 用户需求）

原先「每回合第一次施法发生在什么时刻」是插件定死的：交权路径每次「控制权交给玩家」都判施法，配合 `heroCasted` 一回合一施，实际效果=**本回合首次轮到玩家行动时**。本需求把该时机交给玩家配置，且**不设置时保持原行为**。

- **数据**（方案级，随 5 套方案各自保存）：`SpellCastTimingFields{mode, target_kind, target_side, target_occurrence, after_wait, _reserved, target_value}`（`PolicyCore.hpp`）。`mode=CAST_TIMING_DEFAULT`(0)=首次行动（缺省）；`mode=CAST_TIMING_UNIT`(1)=指定部队，目标用与部队规则同构的 `StableStackIdentity` 四元组（本体=军队槽 0..6，召唤物/克隆=生物 id+出现序；**不存会移动的 hex**）。`after_wait`(0/1，缺省 0)=「等待后」勾选态，仅对指定部队模式有意义。字段顺序是「先单字节、`int16_t target_value` 最后」：插件用 `/Zp1`、单测的 `cl` 不带 `/Zp1`，这样排两种对齐下都是 8 字节且零隐式填充，记录去重的 `bytes_equal(sizeof)` 才不会比较到未初始化字节。`NormalizeSpellCastTiming` 把非法身份整体回默认（勾选态一起清零）、把 `after_wait` 归一成 0/1、把 `_reserved` 清零；JSON v7 不升版，`castTiming` 键**缺失=旧行为**（技能例外条款），其中的 `afterWait` **可缺、缺键=不勾选**，存在则严格校验 0/1。
- **UI**（部队页）：Army 从 4 行卡片改 3 行，首行放「每回合施法时机」——模式下拉（首次行动/指定部队）；仅指定部队模式显示右侧部队下拉，首次行动模式隐藏并禁用该框。候选=**己方存活部队含召唤物**（`BuildCastTimingCandidates_`，按 `MakeStableStackIdentity` 现算；候选下拉与保持状态第三小列同款：头像+位置+数量，展开方向/可见行/滚轮全走 `ListVisibleRowsInArea_`/`ListOriginYInArea_` 纯函数）。草稿语义同其它方案级设置：只随「确定」提交，取消回滚，切方案/读本场存档各自带值。
- **UI：等待后复选框**（2026-10-17）：部队下拉框右侧加 `panel.cast_timing_after_wait`=「等待后」复选框，**只在「指定部队」模式出现**（与部队框同生同灭），默认不勾。几何：`ARMY_TIMING_WAIT_X = ARMY_TIMING_UNIT_X + ARMY_TIMING_UNIT_W + 8`，部队框由 280 收到 264 让位，右缘 `+ ARMY_TIMING_WAIT_HIT_W = 651` 仍在内容区 660 内（单测断言）；绘制与命中共用 `ARMY_TIMING_WAIT_*`，命中区=方框+文字、高按部队框 60 取，点框或点字都切换。收起态与部队列表展开态都能点（列表画在 60px 框下方盖不到它；模式下拉的展开行会盖住该竖条，故那一路不接管点击）；切部队只换身份、保留勾选态。
- **运行时**：`CastTimingAllowsThisHandoff_(mgr, side)` 插在 `TryProtectCast_` 的 `heroCasted` 之前；独立的 `TryAttackMagicCast_` 在确认当前人类侧后也复用同一门闸，因此保活/状态/召唤与攻击魔法四通道时机一致。默认模式放行（行为不变）；指定模式取 `CurrentTurnStack_` 当前轮到部队、`BuildStableIdentitiesFromBattle_` 现算身份、`StableStackIdentityEquals` 比对——不匹配或身份不在场（阵亡/重打丢召唤物）本回合不判施法。**勾了「等待后」**时，指定部队本轮**第一次**被交权也不施法，要求它本轮不是第一次被交权：① 游戏自己的 `waiting` 位（`H3CombatCreature::info.flags` 位 0x2000000，`IsWaiting()` 即读它；`H3CreatureInformation` 大小 0x74、字段序 town/level/soundName/defName/flags(0x10)，`info` 在 +0x74 ⇒ 位在 **creature+0x84**，与游戏 `|= 0x2000000` 同口径）。**（2026-10-17 用户裁定：原「② 本轮该部队已被交权 ≥2 次」判据及其 `CastTimingWaitState`/`NoteCastTimingHandoff` 状态机已删除——该判据依赖的 `turn` 是行动计数，每次行动无条件 +1，`acts>=2` 永远凑不满，属死代码。现「等待后」唯一判据就是等待标志位：`IsWaiting()` 为真才施放，`BA_WAIT` 是唯一置位点，「防御」不置位。）** 放行/拦截留 `[CastWait]` debug 行。**行动=防御的部队例外（2026-10-17 用户口径）**：防御不产生等待，等下去等于永不施法，故 `AfterWaitAppliesToAction(AA_DEFEND)==false`，该队直接放行——语义就是「在它发出防御操作之前施法」（两个判定调用点都在提交动作之前，顺序天然满足）。`g_cast_in_flight`/`heroCasted` 防重入与一回合一施上限不变。
- **验证边界**：两变体 0 错 0 警、七组单测全绿（含新用例 `policy: 每回合施法时机` 的勾选态/状态机/几何断言与 JSON `afterWait` 往返/缺键/拒绝用例）、部署哈希一致（Release=部署副本 `AEB96D32…E0130D`、NULL `12D032EF…B07AD8`）；「指定部队轮到它行动时才施法、默认仍是首次行动、勾选『等待后』后要等它等待并再次行动才施法」属游戏内行为，**由用户实机验收**。

### 5.9 魔法下拉显示对应系别等级（2026-10-16 用户需求）

保持状态与攻击魔法两个 Tab 的魔法下拉项，在法术名称后追加「(等级系别)」，例如 `迟缓 (高级土系)`。

- 系别从游戏 `H3Spell` 表的 `school` 位掩码读取（气/火/水/土），不使用固定法术编号映射；魔法神箭（`MAGIC_ARROW`）为四系特殊法术，按英雄四系魔法技能最高等级及对应系别显示。
- 等级通过当前战场英雄 `GetSpellExpertise(spell, specialTerrain)` 实时读取，按 0..3 显示「无/初级/高级/专家」。
- 两个 Tab 共用 `StatusSpellNameWithExpertise_`，只改变下拉显示，不改变魔法资格、群体判定、目标选择或施法参数。
- 中文内置文案与 `lang/zh-CN.ini` 同步；较长下拉名称使用现有宽下拉区域，游戏内字体与实际显示仍由用户验收。

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
- `lang\zh-CN.ini` 现 **11768 B、UTF-8 无 BOM**（删 7 个 `pack_*` 键后由 12122 B → 11720 B，2026-10-15 补 `cell.siege_hint` 后 → 11768 B；读侧无 BOM 亦兼容，故不影响加载），与内置默认表的键集合一致；改动文案时两侧都要改。BOM 的得失另注：`edit` 工具重写文件时不保留原 BOM，属工具行为，不是手工转码。
- **缺键会直接显形在画面上**：`T(key)` 查不到键返回字面量 `!!键不存在!!`（打铁面板「安全位置」下面那行曾显示该串——键 `cell.siege_hint` 内置表与 lang 双侧都漏了，H3Auto 源插件同样漏，属移植带过来的上游缺陷）。现在 `T()` 命中缺键时另记一条 `LogWarn("[UiText] 文案键不存在：…")`（同一键只记一次，不随每帧刷屏），便于玩家日志定位。
- **离线门禁**：`scripts\check-ui-text-keys.ps1` 比对三处——代码里 `T("…")` 的引用、内置表 `kUiTextDefaults`、`lang\<语言>.ini`；有「引用键未定义」即 exit 1（那等于画面上出现 `!!键不存在!!`），并提示「ini 有、表没有」的可疑拼写与表内未被直接引用的键。改文案后先跑它再编译。

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

### 7.6 保持状态通道：配置字段、三小列布局与执行口径（2026-10-16）

「保持状态」页的配置随方案存档（`Games\<地图>\Auto\*.json`），字段落在 `StatusProfileFields`（`PolicyCore.hpp`）：

| 字段 | 含义 | 取值 |
|---|---|---|
| `slots[]`（容量 8，JSON 键 `buffs`） | 保持的法术列表，空槽 = 0 | 法术 id 1..80 |
| `targetModes[]`（容量 8） | 每槽目标模式：0 = 全体，1 = 指定 | 0/1，**缺键 = 0** |
| `targetHexes[]`（容量 8） | 「指定」时玩家点的那一队所在格 | -1（全体）/ 0..186，**缺键 = -1** |
| `slow` | 敌方减速开关（历史保留字段） | 0/1 |
| `refresh_turns` | 补状态阈值：剩余回合 ≤ 此值就补 | 1..9，默认 1 |

`targetModes`/`targetHexes` 是 2026-10-16 新增键，**故意不升根版本号**（仍是 7）：旧档没有这两个键时按「全体 + 无目标格」读入，即改动前的行为，所以老方案照常可用（`ParseStatusJson_` 里这两个键是可选的，值域由 `NormalizeStatusFields` 兜底：模式钳到 0/1、格钳到 -1 或 0..186、全体强制清格、槽位压缩时与 `slots` 同步搬移、尾部槽复位）。

**下拉候选（`BuildStatusCandidates_`，`SettingsDlg.inc.cpp`）**：2026-10-16 用户裁定「魔法下拉框中的可选项，改成根据魔法的目标，分为己方、敌方两大类，己方排在前面」，故状态法术白名单拆成**按目标阵营分的两张表**（`PolicyCore.hpp`）：

| 组 | 表 | 内容 |
|---|---|---|
| 己方（可持续增益） | `kBuffSpellIds`（19） | 27 神圣护盾、28 大气神盾、29 烈火神盾、30-33 四项防护、34 抗魔术、36 魔法镜像、41 祝福、43 嗜血、44 精准、46 石肤、48 祈祷、49 欢欣、51 幸运、53 加速、55 屠戮成性、58 反击 |
| 敌方（持续减益） | `kEnemyStatusSpellIds`（10） | 62 双目失明、61 遗忘、59 丧心病狂、60 蛊惑人心、52 大难临头、50 悲痛欲绝、47 瓦解射线、45 虚弱无力、42 诅咒、54 迟缓大法 |

**数组顺序 = 下拉里的显示顺序 = 同一回合有多个都该补时的优先顺序**（失明 62 排在最前、迟缓 54 排在最后，沿用既有"失明优先于减速"的裁定）。**每组项前各插一行不可选的分组标题行**（`s_status_dd_group[]`：0 = 法术项、1 =「己方」、2 =「敌方」；文案 `panel.status_group_friendly`/`panel.status_group_enemy`）：标题行**只在该组有候选时**出现，不可悬停、点它既不选中也不收起（当作"点在自己的列表里"）；空槽（法术 0）与"非标题"共用哨兵 0，所以每条高亮/命中/默认项路径都先过 `StatusRowIsHeader_`。「＋」新增槽跳过标题行取**第一个己方项**。

**"会不会"一律以英雄魔法书为准**——`hero->HasSpell(spell)`（`learnedSpells|availableSpell`，+0x3EA/+0x430 位或，装备赋予也算，与原版施法面板同口径；2026-10-16 用户裁定「英雄装备了魔法书之后…会不会，你应该以魔法书的显示为准」，与 attack 通道同款）。学校技能等级（`GetSpellExpertise` 0..3，魔法系别熟练度）**不构成资格、也不影响列举**：2026-10-16 用户最终裁定「状态保持列出的魔法改为英雄已学会的状态魔法（不区分全体了）」——**资格只剩"魔法书里有它 + 是状态魔法"两条**，不再要求法术自带群体版（原来减速与群体增益要求专家级的那层门槛一并去掉）。
**候选资格统一在 `H3AutoPolicy::IsStatusCandidateSpell(spell, has_spell)`（`PolicyCore.hpp`）判定**：`has_spell` 是唯一门槛（外加 `IsStatusSpellId` 白名单，即 `IsFriendlyStatusSpell || IsEnemyStatusSpell`），收敛成一个函数是为了让设置面板与单测共用同一口径，避免两侧漂移。**已选项剔除**（2026-10-16 最终口径：中间一轮曾按「下拉选项不用把已选择的去掉」改成不剔除，用户复核后定案改回——同一法术不重复占槽；正在编辑的槽不算已占）。
**未学会时的界面提示：按玩家要求不做**（玩家原话「没学会时明说 这个不需要」）。取而代之的是**日志留痕**：`BuildStatusCandidates_` 构建时每个候选一行 `[Panel] status cand grp=%d spell=%d book=%d exp=%d flags=0x%X used=%d ok=%d`（debug 级，`grp` = 1 己方 / 2 敌方），"下拉里为什么没有 X"可从日志直接读出——`used=1`＝已被别的槽占用，`book=0`＝魔法书里没有。
**说明文案受宽度约束**（2026-10-16 用户实测报「力盾落点 下面那行的文本过长…没显示全」后定）：力盾落点下面那行 `panel.status_note` 画在**固定高度的单行框**里（`DrawTxt(…, GRID_FRAME_W, 18, …)`，`PanelDraw.inc.cpp`），超长不换行、只裁右半截；它与阈值行之间只有 26px，塞不下第二行。现值 `下拉按英雄魔法书列，分己方/敌方；每槽选法术、全体/指定，指定后选部队` = 34 全角宽（≈443px / 框宽 531）；**上限锚在"实机确认可见"的 37.5 全角宽**，并由 `scripts\check-ui-text-keys.ps1` 的 `$singleLineBudget`（`panel.status_note = 74 半角宽`）把关。改这句话前先量宽度。注意面板底部的 `tips.*` 提示**不受**此约束——它们经 `tips.color_wrap` 进状态栏（富文本）。
**槽位文案：只用游戏法术表里的名字**（2026-10-16 用户裁定去掉「（敌方）」后缀），过宽时截断——`StatusSpellLabel_` 已删除。分组由**标题行**承担，不再往法术名后面挂后缀。

#### 三小列布局（2026-10-16 用户裁定，`PanelLayout.hpp`）

一大行 = 一个槽，**两大列 × 四行**装下 8 个槽（`STATUS_BIG_COLS=2` / `STATUS_ROWS=4`），每大列宽 `(531-10)/2 = 260`。每大列内分**三小列**：

**槽位序号是「先行后列」**（2026-10-16 用户实测裁定「魔法列表那里增加时,+按钮出现的顺序不对,现在是先列后行,应该是先行后列」）：序号 0/1 在第一行的左右两列，2/3 在第二行……映射收敛成 `PanelLayout.hpp` 的纯函数 `StatusSlotCol_(i) = i % STATUS_BIG_COLS` / `StatusSlotRow_(i) = i / STATUS_BIG_COLS`，绘制与命中都只经它们（`StatusSlotRect_` → `StatusMagicX_/StatusModeX_/StatusUnitX_/StatusRowY_`），顺序本身由 `PolicyCoreTests` 的几何用例逐序号钉住。**「＋」按钮画在第 `slot_count` 个槽位上**，所以新增槽的落点顺序就是行优先（先往右、再往下）；原实现是先列后行（0..3 把左列跑满再跳右列），加一个槽会先往下一行跑，与"从左到右、从上到下"的直觉相反。

| 小列 | 宽度 | 内容 |
|---|---|---|
| ① 魔法 | `STATUS_MAGIC_W = 72`（按 4 个汉字宽 + 内边距 + 箭头） | 该槽的法术，展开列表宽 `STATUS_MAGIC_LIST_W = 122`（浮层，压到 660 边界之外不越界） |
| ② 全体/指定 | `STATUS_MODE_W = 46`（按 2 个汉字宽） | 固定两项：`panel.status_target_all`（全体）/ `panel.status_target_pick`（指定） |
| ③ 部队 | `STATUS_UNIT_W = 134`（剩余宽度） | **仅当②选了「指定」才存在**：下拉列出**该法术能打的那一侧**存活部队（己方法术列己方、敌方法术列敌方），每项＝生物头像 + 位置（`A01` 式，`CellControl_FormatPosition`）+ 数量，项高 = 行高 |

- 行高 `STATUS_SLOT_H = 60`：按第三小列的图片高度定（头像帧 60×66 内取 56 行，再各留 2px 边距，即 `STATUS_ICON_H(56) + 4`），四行（含 `STATUS_ROW_GAP=4`）收在内容区底 `MAGIC_LIST_BOTTOM` 之上（`STATUS_ROW0_Y = 190`，末行底 442 ≤ 448）。
- 部队候选：`BuildStatusUnitCandidates_(spell_id)` **按该槽法术的目标阵营只列一侧**（`IsEnemyStatusSpell(spell) ? 敌方 : 己方`，2026-10-16 用户裁定「第三小列，根据魔法的作用目标，只显示己方或敌方部队」）——列另一侧没有意义（原版会阻止），也免得玩家挑到一个永远打不上的目标。每侧遍历 21 槽，只收 `count_current > 0` 且 `position` 有效者；`target_hex` 存格号，模型换算由原版负责。槽位没选法术时不列任何部队。换法术或把②改成「指定」都会**按新法术重建**候选，原目标不在其中就默认取第一项。
- 第二小列的**默认值＝当前等级真能做到的那一项**（`StatusDefaultTargetModeHere_` → `StatusSpellCanMass`：能做到群体就默认「全体」，单体就默认「指定」）。玩家**选了做不到的那一项时按能做到的方式施放**，并在状态栏提示一次（`panel.status_target_fallback`，日志同款 info）——用户 2026-10-16 裁定的 B 方案：不禁止选择，也不静默。
- 下拉**按空间上下择一展开**（`ListVisibleRowsInArea_`/`ListOriginYInArea_`，PanelLayout.hpp 纯函数）：优先向下，下方行数少于上方就改向上。第 3/4 行向下只剩 0~1 行，不翻上去会"点开是空的、选项还点不中"。可见行数、列表原点、命中矩形与滚轮共用同一组函数（`StatusMagicItemRect_`/`StatusModeItemRect_`/`StatusUnitItemRect_`）。
- 部队列不可见（选了全体）时，点击该空白区不产生任何动作；右击整行（含空白区）删除该槽。

**每回合最多补一个法术**。2026-10-16 起不再是"友方增益 → 敌方失明 → 敌方减速"三段硬编码优先级，而是：**己方组按槽位顺序走既有的增益补施逻辑；敌方法术走一条通用循环**（见下），两条路各自算出"该补谁、补在哪、剩多少"，取**剩余回合最少者**动手；并列时按**白名单表顺序**（失明 62 → 遗忘 61 → 丧心病狂 59 → 蛊惑人心 60 → 大难临头 52 → 悲痛欲绝 50 → 瓦解射线 47 → 虚弱无力 45 → 诅咒 42 → 迟缓 54），因此"失明优先于减速"这条既有裁定被完整保留。10 个敌方法术共用一套判定，不再有失明/减速两个特例分支。

**敌方减益的目标选择（`GatherSideStatusCandidates_` + `ChooseEnemySingleStatusTarget`/`ChooseEnemyMassStatusTarget`）**，按 2026-10-16 用户口径「全部持续型减益」：

1. 遍历敌方 21 个槽位，剔除死亡、`position` 越界、`TargetMagicAllowed_` 不通过、`CreatureCanReceiveStatusSpell` 不接受的部队 —— 具体按法术给硬条件：亡灵（`undead 0x40000`）与心智免疫位（`mindImmunity 0x400`，NH3API `CF_IMMUNE_TO_MIND_SPELLS`）拒绝失明 62、丧心病狂 59、蛊惑人心 60；**诅咒 42 / 虚弱无力 45 只对"有伤害输出"的部队有意义**（`damage_high > 0`）；**遗忘 61 只对射击部队有意义**（`shooter 0x4`）；箭塔（149）按既有规则排除。这三条硬条件是**推断口径，待实机验证**，故意取保守方向：错列一个目标会让 `CanCastSpellAtCoord` 失败、整个回合白等。
2. 候选按「剩余回合 ≤ `refresh_turns`」过滤：还够的不动它；`-1` = 不存在/不可接受，不参与判定。
3. 单体（`ChooseEnemySingleStatusTarget`）：取剩余最少者；并列时取威胁最大者：`单位价值 = aiValue > 0 ? aiValue : (fightValue > 0 ? fightValue : hitPoints)`，`威胁 = 单位价值 × 当前数量`（原版军队价值口径，见 H3Note）。玩家选了「指定」时**只认那一格**（不在候选里、或它还没到期 → 本回合不施放）。
4. 群体（`ChooseEnemyMassStatusTarget`）：沿用**语义 A**——只要敌方**还有任意一队**带着该减益（代表值 > 阈值），就算达标不再施放；全部到期/消失才补一次群体。**群体决策一律不带目标格（`target_hex = -1`）**：引擎本来就忽略它，而 `cast_hex ≥ 0` 会先过 `CanCastSpellAtCoord` 并触发"只有群体版"的兜底 info，属于假故障。
5. 单体法术**不适用**语义 A：一队到期、另一队还够时，仍然补到期的那队。无候选则不施放。

**群体可行性＝法术自带标志 + 英雄对应该系别的技能等级**（2026-10-16 用户要求 2：「魔法是否可作用的己方或敌方全体，是由魔法本身以及英雄对应的魔法等级（水火土气四系）决定的」）：`StatusSpellCanMass(spell, flags, exp)` 与引擎 `H3Spell::SingleTarget` 互补，其中 `flags` 取法术表 `0x687FA8 + spell*0x88 + 0x0C`（`0x20` = 高级群体、`0x40` = 专家群体、`0x10` = 仅单体、`0x800` = 友方群体），`exp` 取**该法术所属系别**的 `hero->GetSpellExpertise(spell, terrain)`——系别由法术自身决定，不需要玩家再选。这条同时对己方增益与敌方减益生效，也是第二小列默认值（`StatusDefaultTargetModeHere_`）的来源。

**施放路径**：门槛 = `HeroMagicAllowed_`（魔法书 / 驻地 / 双方禁魔球 / 诅咒地 / 披风 / `hero->HasSpell` / 法力 / 本回合未施法）。**施法目标格由槽位决定**（2026-10-16 起统一成一个 `cast_hex`）：

| 情形 | 群体可行性 | 实际施放 |
|---|---|---|
| 模式＝全体，当前等级有群体版 | `StatusSpellCanMass` 真 | `CastSpell(spell, -1, …)` 无目标群体施放 |
| 模式＝全体，当前等级没有群体版（如非专家减速、失明） | 假 | 按**单体**施放：`CanCastSpellAtCoord(spell, exp, hex)` 预检 → `CastSpell(spell, hex, …)`，目标格取候选里的那一队；info 提示"选了全体但当前等级没有群体版，已按单体施放"（每场每法术一次） |
| 模式＝指定，该队有效 | 假（单体） | 单体打玩家点的那一格（`target_hex`）；该队已阵亡/不可接受/剩余还够则本回合不施放（debug 留痕） |
| 模式＝指定，但当前等级只有群体版 | 真 | 原版会强制群体施放，**按全体施放**（`cast_hex = -1`）+ info 提示"选了指定但当前等级只有群体版，已按全体施放" |
| 单体法术（失明等）候选里没有可接受部队 | — | 不施放（不空放、不弹阻止框） |

技能等级只作**施法实参**（0..3 截断，等级交原版内部重算），不再作资格；群体与否最终由原版 `H3Spell::SingleTarget(spell, expertise)` 决定，插件的 `StatusSpellCanMass` 与它互补（同一套 flags 判据，单测里与旧口径 `MagicHasMassVersion` 逐样本对拍）。施后验证**只看本次施放的目标**（单体看被选中那一队，群体看该侧所有存活队）的 `active_spell_duration[spell]`：拿到即留 info，没拿到即认定全免疫/全抵抗，`g_status_immune_mask` 置位、本场跳过。

**日志**（排查玩家反馈直接可用）：`[Status] 敌方状态目标 slot=… spell=… cid=0x… count=… remain=… th=… value=… hex=… turn=…`（info，含"为什么是它"；`hex=-1` = 群体）、`[Status] cast done spell=… hex=… …`（info）、`[Status] enemy cand slot=… spell=… mode=… mass=… exp=… want=… remain=… hex=… cand=… living=… masked=…`（debug，逐槽判定留痕：`masked` = 禁魔/未学、`cand` = 可接受候选数、`living` = 存活数）、`[Status] enemy branch none turn=… slots=… buffs=[…]`（debug，整个敌方分支都没动手时）、`[Status] buff pick spell=… remain=… th=… turn=… hex=…`（debug）、无候选时一次性 info「`[Status] spell=… 当前没有可接受的存活部队，不施放`」。

**测试**：`tests\PolicyCoreTests.cpp` 覆盖 `CreatureCanReceiveStatusSpell`（亡灵/心智免疫拒绝失明 62、丧心病狂 59、蛊惑人心 60；无伤害输出拒绝诅咒 42、虚弱无力 45；非射击拒绝遗忘 61；普通生物接受）、`ChooseEnemySingleStatusTarget`（剩余最少优先、并列取威胁最大、阈值 0 的边界、`preferred_hex` 只认指定格 / 不在候选里 / 还够则不施放、空指针容错）、`ChooseEnemyMassStatusTarget`（语义 A、阈值放宽、非法 id、空表）、两张白名单的条数与逐项成员（含本轮补的 29/34/36 与 10 个敌方减益）、`ChooseBlindTarget`/`ChooseSlowTarget` 的选择序、`IsStatusCandidateSpell`（只看魔法书 + 白名单）、`StatusSpellCanMass`/`StatusDefaultTargetMode`、`ChooseSingleStatusTarget`、`NormalizeStatusFields` 的模式/格同步压缩，以及保持状态页几何（两列四行容量、三小列宽度自洽、四行不越底、**每个槽行的三个下拉都至少能看到 1 行且列表原点在内容区内**、**下拉容量 `STATUS_MAGIC_DD_CAP` 装得下"己方 19 + 敌方 10 + 两行标题"**）。策略层为生产与单测共用文件，故两侧同源。

**已知边界（待游戏内验收）**：① 各减益的实际命中与"醒来"由原版处理，插件只按 `active_spell_duration` 记剩余；被抵抗时按上面的整场跳过机制处理，不做逐次重试。② 42/45/47/59/60/61 的"可接受性"硬条件（伤害输出 / 射击 / 心智）是**推断口径，待实机验证**——取保守方向，宁可少列一个目标也不让 `CanCastSpellAtCoord` 失败白等一回合。③ 第三小列现在只列法术能打的那一侧，但"能打"仍是粗判：真正能不能施放交 `CanCastSpellAtCoord` 与原版。④ 蛊惑人心（60）被持续保持 = 敌方一队长期被控（用户明确接受）；瓦解射线在原版可叠加，本插件"保持状态"语义只按剩余回合刷新。⑤ 标题行不参与滚动锚点计算，滚动仍按行号（含标题行）计算可见行与原点（`ListVisibleRowsInArea_`/`ListOriginYInArea_` 未改）。

### 7.7 打铁帮助模态（右上角「?」）的现行内容（2026-10-16）

帮助模态 = 遮暗整面板的模态层，内容自上而下：**9 行说明文字**（打开方法/方案 1-5/智能攻击/停止/本场存档/删除/召唤/热键/有效期，文案键 `help.line_*`）→ **开源地址行**（纯文本样式、不做按钮框，点击复制到剪贴板，成功时整行闪绿 1.2 秒）→ **关闭按钮**。模态矩形 `GetHelpModalRect_` 宽 480、高 **404**；说明区 9 行 × 行高 28 抽成 `HELP_TEXT_TOP_` / `HELP_TEXT_LINES_` / `HELP_TEXT_LINE_H_` 三个常量，绘制与「开源地址行」定位共用同一组常量，开源地址行紧接说明区之后（`y + 50 + 9 × 28 + 6`）。

**不在帮助页里的控件**（均为用户裁定，避免与宿主已有界面重复）：

- 「打包日志」按钮：2026-10-15 合并时移除（宿主悬浮条第一行自带该功能，见 §3.4、§7.5）。
- **日志级别下拉：2026-10-16 用户裁定移除** —— 战场存档界面悬浮条第一行的日志等级单选列表已经提供该设置（点选即写 `H3BattleStore.user.ini` 的 `[Logging] MinLevel` 并立即生效），帮助页不再重复提供。随之删除的代码：`PanelDraw.inc.cpp` 的 `HelpLogLevelOptKey_`、`HELP_DD_LABEL_W/HELP_DD_W/HELP_DD_H`、`HelpDropdownRowHeight_`、`GetHelpLogLevelRowY_`、`GetHelpLogLevelDdRect_`、`GetHelpLogLevelItemRect_` 及「行 A」下拉与展开列表两处绘制；`PanelDraw.hpp` 的两条声明；`SettingsDlg.inc.cpp` 的 `s_help_log_dd_open` 状态与「展开列表点选」「下拉开合」两个点击分支（帮助模态的点击处理只剩开源地址行与关闭按钮）。**功能没有减少**，只是不再有两个入口。模态高度由 440 缩到 404。

**开源地址**：`help.link`（显示行）/ `help.link_url`（点击复制的载荷）/ `help.link_flash`（复制成功的闪绿副本）三键都指向 **`https://github.com/Apq/H3BattleStore.git`**（2026-10-16 用户裁定，原为 `https://github.com/Apq/H3Auto`）。改地址只需同时改 `modules\UiTexts.inc.cpp` 与 `lang\zh-CN.ini` 两侧的这三个键（键名不变）。

## 8. 分批实施与验收状态

### 8.1 每批验收口径（四件套）

1. `build.bat`（清代理 + MSBuild `Release|Win32`）**0 error / 0 warning**；
2. 七组 Catch2 单测 exit 0：`tests\run-unit-tests.ps1:24` 的 `ArchiveTests`、`CodecTests`、`LogControlsTests`、`UiPortTests`、`AltTailTests`、`PolicyCoreTests`、`CrashFixTests`（Catch2 v3.16.1 amalgamated；`LogControlsTests` 额外链 `lzma\LzmaEnc.c`、`LzFind.c`、`CpuArch.c`，带 `/DZ7_ST`；`CrashFixTests` 与日志夹具共用 `H3BS_TEST_FIXTURE`）；
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
