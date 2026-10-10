// ========== NullUiPort.inc.cpp ==========
// 无界面实现（UI separation step 5, H3BS_UI_NULL 编译变体）。
// 目标：不绘制、不吞输入、不改游戏交互；仅记录日志并可完成核心功能路径。
// 适用于单元测试、接口实验与未来“无界面运行”；正式发版默认仍为 HdNativeUi。
// 只经契约抽象使用；不引入线程，不触碰 H3UI 资源。
#include "BattleUiPort.hpp"

// NullUi 只经契约与服务交互；服务不持有界面实现，因此无反向依赖。
// 界面安装由 Entry 的 g_uiPort 承担（见 H3BattleStore.cpp 的编译期单选）。

class NullUi final : public IBattleStoreUi {
public:
    void Initialize() override { LogInfo("[NullUi] initialized"); }

    // 生命周期与界面请求：不绘制、不交互。
    void Draw(H3CombatManager* mgr) override { (void)mgr; }
    void PollHover() override {}
    void FrameClick() override {}
    void PollRebindKey() override {}
    bool ReloadEntries(const H3CombatManager* mgr) override { return StoreReloadList_(mgr); }
    bool HitBar(H3Msg* msg, bool fullBlock) override { (void)msg; (void)fullBlock; return false; }
    void HandleMouse(H3Msg* msg) override { (void)msg; }

    void MaintainRestore(H3CombatManager* mgr) override
    {
        if (StoreMaintainRestore_(mgr)) {
            LogWarn("[NullUi] restore request expired; ui could not display timeout notice");
        }
    }

    void ProcessRestore(H3CombatManager* mgr, int result) override
    {
        // 无界面确认框时，读档不可执行；只在时机有效时给出日志提示。
        StoreEntry entry {};
        unsigned generation = 0;
        std::string expectedKey;
        if (!StoreConsumeRestore_(mgr, result, &entry, &generation, &expectedKey)) return;
        LogWarn("[NullUi] restore request cannot be confirmed without UI; request discarded");
    }

    void CancelRebind(const char* reason) override { (void)reason; }
    void MarkSaved(uint64_t timestampUtcMs) override
    {
        LogInfo("[NullUi] saved timestamp_utc_ms=%llu", static_cast<unsigned long long>(timestampUtcMs));
    }
    void MarkNotice(const char* utf8Text) override
    {
        LogInfo("[NullUi] notice logged: utf8=%s", utf8Text ? utf8Text : "");
    }
    void OnBattleReset() override { LogInfo("[NullUi] battle state reset"); }

    // 输入事件一律透传，不参与战场交互。
    bool OnSystemKey(const UiKeyEvent_& e) override { (void)e; return false; }
    bool OnSystemMouse(const UiMouseEvent_& e, bool combatOpen) override { (void)e; (void)combatOpen; return false; }
    void OnGameKeyBefore(const H3Msg* msg, int level) override { (void)msg; (void)level; }
    bool OnGameMouse(H3Msg* msg) override { (void)msg; return false; }
    void OnGameKeyAfter(H3CombatManager* mgr, const H3Msg* msg, int result, unsigned now, bool waitBlocked) override
    {
        (void)mgr; (void)msg; (void)result; (void)now; (void)waitBlocked;
    }
    void OnFrameKeyPoll(H3CombatManager* mgr, unsigned now, bool waitBlocked) override
    {
        (void)mgr; (void)now; (void)waitBlocked;
    }
    void OnListReloaded() override {}
    void OnFaultCleanup() override { LogInfo("[NullUi] fault cleanup acknowledged"); }

    bool IsRebindWaiting() const override { return false; }
    char RebindLatchKey() const override { return 0; }
};

static NullUi g_nullUi;
static IBattleStoreUi* const g_uiPort = &g_nullUi;
