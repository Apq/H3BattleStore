// ========== BattleUiPort.hpp ==========
// 界面契约（docs/09-界面与功能分离 第2步立契约）。
// 钩子层（Entry）只经本接口驱动界面实现；界面实现不反调钩子层。
// 第2步为函数面板形态：方法与现有 UI 入口一一对应，行为零变化；
// 第3步把键盘/鼠标/帧轮询收敛为事件并清零 Entry 对界面内部状态的直连。
// 约束：仅游戏主线程调用；不得在 __except 过滤器内调用；事件结构保持 POD；
// 实现内禁止栈上大缓冲（路径缓冲守 kPathCap_ 堆规则）。
#pragma once

#include <stdint.h>

namespace h3 { struct H3CombatManager; struct H3Msg; }

class IBattleStoreUi {
public:
    virtual ~IBattleStoreUi() {}
    virtual void Draw(H3CombatManager* mgr) = 0;                       // 绘制帧（cycle/AfterBlt）
    virtual void PollHover() = 0;                                      // 帧内按光标重算悬停
    virtual void FrameClick(int gameX, int gameY) = 0;                 // 帧内消费挂起点击
    virtual void PollRebindKey() = 0;                                  // 帧内改键轮询
    virtual bool ReloadEntries(const H3CombatManager* mgr) = 0;        // 刷新存档列表（mgr 只读）
    virtual bool HitBar(H3Msg* msg, bool fullBlock) = 0;               // 消息钩子吞并判定
    virtual void HandleMouse(H3Msg* msg) = 0;                          // 消息钩子鼠标手势
    virtual void MaintainRestore(H3CombatManager* mgr) = 0;            // 读档请求维护（第4步移服务）
    virtual void ProcessRestore(H3CombatManager* mgr, int result) = 0; // 读档执行/收尾（第4步移服务）
    virtual void CancelRebind(const char* reason) = 0;                 // 取消改键等待
    virtual void MarkSaved(uint64_t timestampUtcMs) = 0;               // 保存成功状态提示
    virtual void MarkNotice(const char* utf8Text) = 0;                 // 保存失败等提示
    virtual void OnBattleReset() = 0;                                  // 换场清理界面状态
};
