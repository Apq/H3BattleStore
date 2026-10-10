// ========== BattleUiPort.hpp ==========
// 界面契约（docs/09-界面与功能分离 第2步立契约、第3步断直连）。
// 钩子层（Entry）只经本接口驱动界面实现；界面实现不反调钩子层。
// 约束：仅游戏主线程调用；不得在 __except 过滤器内调用；事件结构保持 POD；
// 实现内禁止栈上大缓冲（路径缓冲守 kPathCap_ 堆规则）。
// 包含顺序：接口使用未限定的 H3CombatManager/H3Msg，本头不能独立 include，
// 必须先 `namespace h3 { struct H3CombatManager; struct H3Msg; }` 前向声明并
// `using namespace h3;`（生产 TU 与 tests/UiPortTests.cpp 均照此顺序）；不得
// 在本头内 using 命名空间，避免污染单 TU 其余部分。
#pragma once

#include <stdint.h>
#include <type_traits>

namespace h3 { struct H3CombatManager; struct H3Msg; }

// 键盘事件：vk 在系统源为 Windows 虚拟键码、消息源为游戏 H3 键码。
struct UiKeyEvent_
{
    int vk;
    bool down;
    bool up;
    bool repeat;   // 系统钩子自动重复
    int source;    // 0=系统键盘钩子 1=游戏消息
};

// 鼠标事件（系统钩子源，游戏坐标）。
struct UiMouseEvent_
{
    int kind;      // 0=move 1=leftDown 2=leftUp 3=rightDown 4=rightUp 5=wheel
    int gameX;
    int gameY;
    int wheelDelta;
};

static_assert(std::is_trivial<UiKeyEvent_>::value && std::is_standard_layout<UiKeyEvent_>::value,
    "UI key event must remain POD for SEH hooks");
static_assert(std::is_trivial<UiMouseEvent_>::value && std::is_standard_layout<UiMouseEvent_>::value,
    "UI mouse event must remain POD for SEH hooks");

class IBattleStoreUi {
public:
    virtual ~IBattleStoreUi() {}
    virtual void Initialize() = 0;                                    // 启动时应用本界面布局；热键由服务加载
    // ---- 第2步面板：绘制、列表、恢复与通知 ----
    virtual void Draw(H3CombatManager* mgr) = 0;                       // 绘制帧（cycle/AfterBlt）
    virtual void PollHover() = 0;                                      // 帧内按光标重算悬停
    virtual void FrameClick() = 0;                                     // 帧内消费挂起点击
    virtual void PollRebindKey() = 0;                                  // 帧内改键轮询
    virtual bool ReloadEntries(const H3CombatManager* mgr) = 0;        // 刷新存档列表（mgr 只读）
    virtual bool HitBar(H3Msg* msg, bool fullBlock) = 0;               // 消息钩子吞并判定
    virtual void HandleMouse(H3Msg* msg) = 0;                          // 消息钩子鼠标手势
    virtual void MaintainRestore(H3CombatManager* mgr) = 0;            // 调服务维护请求并呈现超时通知
    virtual void ProcessRestore(H3CombatManager* mgr, int result) = 0; // 调服务消费请求，界面确认后委托执行
    virtual void CancelRebind(const char* reason) = 0;                 // 取消改键等待
    virtual void MarkSaved(uint64_t timestampUtcMs) = 0;               // 保存成功状态提示
    virtual void MarkNotice(const char* utf8Text) = 0;                 // 保存失败等提示
    virtual void OnBattleReset() = 0;                                  // 换场清理界面状态
    // ---- 第3步事件：键盘/鼠标状态机归界面层，钩子层只做翻译 ----
    virtual bool OnSystemKey(const UiKeyEvent_& e) = 0;                // 系统键盘钩子
    virtual bool OnSystemMouse(const UiMouseEvent_& e, bool combatOpen) = 0; // 系统鼠标钩子
    virtual void OnGameKeyBefore(const H3Msg* msg, int level) = 0;     // 游戏键盘消息（原生前日志）
    virtual bool OnGameMouse(H3Msg* msg) = 0;                          // 游戏鼠标消息吞并
    virtual void OnGameKeyAfter(H3CombatManager* mgr, const H3Msg* msg, int result, unsigned now, bool waitBlocked) = 0; // 原生后存档触发
    virtual void OnFrameKeyPoll(H3CombatManager* mgr, unsigned now, bool waitBlocked) = 0; // 帧内键轮询（闩锁重arm+边沿触发）
    virtual void OnListReloaded() = 0;                                 // 列表数据刷新后清悬停
    virtual void OnFaultCleanup() = 0;                                 // 钩子故障后的输入/手势清理
    virtual bool IsRebindWaiting() const = 0;                          // 查询：改键等待中（日志用）
    virtual char RebindLatchKey() const = 0;                           // 查询：改键残留键（日志用）
};
