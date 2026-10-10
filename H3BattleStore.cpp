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

// 跨模块前置声明：实现在 Entry.inc.cpp，供先于其包含的恢复与界面模块使用。
static bool CombatIsReadable_(const H3CombatManager* mgr);
static bool CombatCanCapture_(const H3CombatManager* mgr, const char** reason);
static bool BattleFingerprint_(const H3CombatManager* mgr, std::string* out, std::string* error);
static std::wstring ArchiveRoot_();
static void ClearBattleInputs_();

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

#include "modules/IniUtf8.inc.cpp"
#include "modules/ConfigLog.inc.cpp"
#include "modules/LogPack.inc.cpp"
#include "modules/LogContext.inc.cpp"
#define H3_GUARD_CONTEXT_(reason) LogRecentContext_(reason)
#include "modules/CrashGuard.hpp"
#include "modules/BattleArchive.inc.cpp"
#include "modules/BattleCodec.inc.cpp"
#include "modules/BattleDiagnostics.inc.cpp"
#include "modules/BattleCapture.inc.cpp"
#include "modules/BattleInputPolicy.hpp"
#include "modules/BattleFingerprint.hpp"
#include "modules/BattleRestore.inc.cpp"
#include "modules/BattleUi.inc.cpp"
#include "modules/Entry.inc.cpp"
