// H3BattleStore.cpp
// 英雄无敌3 SoD 插件：战斗中存档与读档。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>
#include <wincrypt.h>
#include <new>
#include <stdarg.h>
#include <wchar.h>
#include <stdint.h>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")

using namespace h3;

// 跨模块前置声明：实现在 Entry.inc.cpp，供先于其包含的恢复与界面模块使用。
static bool CombatIsReadable_(const H3CombatManager* mgr);
static bool CombatCanCapture_(const H3CombatManager* mgr, const char** reason);
static bool BattleFingerprint_(const H3CombatManager* mgr, std::string* out, std::string* error);
static std::wstring ArchiveRoot_();

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

#include "modules/IniUtf8.inc.cpp"
#include "modules/ConfigLog.inc.cpp"
#include "modules/BattleArchive.inc.cpp"
#include "modules/BattleCodec.inc.cpp"
#include "modules/BattleCapture.inc.cpp"
#include "modules/BattleRestore.inc.cpp"
#include "modules/BattleUi.inc.cpp"
#include "modules/Entry.inc.cpp"
