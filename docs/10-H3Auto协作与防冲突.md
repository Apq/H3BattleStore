# H3BattleStore 与 H3Auto 协作与防冲突

2026-10-11 用户要求：自动保存失败不重试；两个插件同时启用时要协调，玩家可以只启用其中之一。

## 1. 启用组合

| 游戏中启用的插件 | 行为 |
|---|---|
| 只有 H3BattleStore | 自己在每回合首个玩家等待窗口尝试自动保存，不依赖 H3Auto。 |
| 只有 H3Auto | 没有协作接口，保持现有自动施法、接管和部队命令逻辑。 |
| 两者都启用本次更新版本 | H3Auto 自动效果前通知存档；保存、读档排队、确认及恢复期间暂停提交新自动效果；读档成功通知 H3Auto 清理旧运行态。 |

两个 DLL 无导入依赖、不强制加载对方。H3BattleStore 在 SoD 版本门卫通过后发布接口；H3Auto 动态查询 patcher 注册表，查不到接口即走原行为。不存在接口的结果不永久缓存，加载顺序不影响发现。DLL在游戏进程内常驻，当前不支持运行中FreeLibrary或热卸载。旧 H3Auto 没有调用这些接口，因此两者协作需要同时使用本次更新后的构建。

## 2. 注册表 ABI v1

所有接口由 H3BattleStore 发布、H3Auto 只读。平台为 SoD Win32 x86；DWORD 存函数地址，调用约定必须为 `__stdcall`。名字带 `.v1`，签名有破坏性变化须另用版本名。

| 注册表键 | DWORD 的含义 | 约定 |
|---|---|---|
| `H3BattleStore.BusyQuery.v1` | `BOOL (__stdcall*)()` 函数地址 | TRUE 表示不得提交自动效果。 |
| `H3BattleStore.BeforeAutoAction.v1` | `BOOL (__stdcall*)(void* mgr)` 函数地址 | TRUE 可继续；FALSE 暂时让原版建立玩家窗口。同步尝试首档；保存失败也允许继续。 |
| `H3BattleStore.RestoreEpoch.v1` | 恢复成功计数 | 仅整场恢复成功后递增；取消确认、失败回滚不递增。 |

使用 `VarFind/GetValue` 或 `VarGetValue` 查询。H3Auto 不调用 `VarInit`，因为已存在的变量会被覆写；不通过 `GetPValue` 取裸引用。忙态用查询函数读取 H3BattleStore 当前真实状态，避免某条取消/超时路径忘记同步镜像变量。

## 3. 自动效果的执行顺序

H3Auto 的 `HH_ShouldAutoExecute` 只在原版结果 `orig==0`（本会交给玩家）时协作；`TryAutoExecuteActiveStack` 覆盖帧刷新与战斗消息两条主动执行路径。两处都在 `EnsureForceFieldBeforeAction_` 及后续保活/攻击魔法/部队命令之前调用同一 helper：

1. 查询 Busy，忙则返回，不调用保存、不消费恢复通知。
2. 观察 RestoreEpoch；变化则处理恢复后的 H3Auto 运行态。
3. 查询 BeforeAutoAction；缺席直接保留原行为；存在则同步调用。
4. Before 返回 FALSE 或调用后 Busy 为 TRUE，则不执行自动效果。
5. 管理器身份仍一致才继续 H3Auto 原逻辑。

`HH_ShouldAutoExecute` 被拦时返回0，让原版沿玩家路径建立活动栈/界面；帧和消息入口被拦时返回false。原版函数仍在保护体外恰好调用一次，协作不吞原版异常。

H3BattleStore 首档尚未尝试时，Before 遵守既有严格窗口：玩家方、活动栈对齐、主战场顶层、action未提交、非动作执行器/施法核心、非自动战斗/战术/结束。接管判定早于原版刷新活动栈时，先交回原版建立窗口，后续帧/消息再尝试首档。首档已尝试后，不再以活动栈刷新滞后阻挡本回合后续自动命令。未初始化、原版自动战斗和战术阶段不等待首档。

## 4. 每回合只尝试一次与重入

保存前先调用 `BattleAutoSavePolicy_::Attempted(turn)`；成功、写盘失败、编码失败或被 SEH 铠甲接住的异常，都不再重试同回合。下一回合自然再次到期；换场或成功读档重置标记，同回合重打可重新尝试并替换旧自动档。

Busy 包含手动/自动保存整段捕获、编码、落盘、回读、列表刷新，以及读档请求pending、确认/恢复busy和fatal。自动保存不弹成功提示，不泵额外消息；重入时 H3Auto 只能查询到 Busy，不能提交施法或部队命令。跨插件回调入口由自身SEH保护，异常分支返回FALSE，不凭清理后的Busy推导成功。SEH 故障清理明确复位保存闸；恢复半写故障仍保留 fatal 阻断。保存失败不锁 H3Auto，也不会自动删除已有临时文件。

## 5. 读档后 H3Auto 的处理与限制

原生战场被恢复后，H3Auto 的旧 PS_HANDLED、施法等待、免疫跳过记录和动作游标仍可能属于恢复前的未来，不能沿用。恢复成功计数变化时，H3Auto 先置恢复处理闸并切到 `CM_MANUAL`，再执行 `ResetAutoState()` 和 `BindStackTrackingFromBattle_()`：保留已确认方案和规则，清理运行态、重新绑定当前部队；处理期间即使面板关闭触发刷新重入也不能继续自动效果，SEH finally释放闸。玩家按 F9 明确继续自动执行。

移动/近战游标从头开始。当前战场档不包含 H3Auto 的方案运行进度，不保证自动轨迹精确回到存档时的游标；要实现该能力，需另设保存/恢复 H3Auto 状态的契约。取消读档或恢复失败回滚不会清 H3Auto 运行态。H3Auto 自身方案文件与 H3BattleStore 战场档使用各自目录，不互相清理。

## 6. 验证边界

H3BattleStore最终coop4验证（2026-10-11 23:42–23:45）：五组单测PASS，默认/Native/Null三BAT Rebuild exit0、0错警，35源无漂移，唯一UI类标记通过；默认468480B SHA256 `16F944AD830F543C348CE1352E12696864E6A87AFD5D398B08ED847357A4506E`。未部署。

H3Auto最终验证（2026-10-11 23:27–23:32）：生产/测试BAT Rebuild均exit0、0警告0错误，PolicyCoreTests实跑1221项通过；74项源前后无漂移。DLL 368128B，SHA256 `C27AFBCBC7DCB6AEF8CBEBB9BB02B5E7CF8A842AE145EB39E03112C6390CDEE3`；未部署。


H3BattleStore 自动槽双格式测试覆盖覆盖写、30档保留、低回合重打、校时回退、格式/槽隔离、坏档与tmp保护；纯策略回归覆盖先消费尝试、同回合禁止重试及成功读档复位。H3Auto 生产共用协作顺序测试覆盖缺席回退、忙时禁止回调、恢复通知先于首档、窗口未建立及保存后重入忙态。

两套玩家可选界面（同在一个 DLL，`[Ui] OutsideBattlefield` 选择，2026-10-14 起不再有编译期界面二选一；另有 `H3BS_UI_NULL=1` 无界面诊断变体）和 H3Auto 均需 BAT Rebuild、单元测试及源码冻结哈希核验。上述离线结果不证明 HD 游戏内的活动栈建立时序、模态重入或自动动作画面；游戏内验收由用户执行。
