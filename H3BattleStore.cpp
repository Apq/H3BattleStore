// H3BattleStore.cpp
// 英雄无敌3 SoD 插件：战斗中存档与读档。
// 目标版本：Shadow of Death（SOD = 0xFFFFE403），仅 x86。

#define _H3API_PATCHER_X86_
#include <H3API.hpp>
#include <new>
#include <stdarg.h>
#include <wchar.h>
#include <stdint.h>

using namespace h3;

Patcher*         _P  = nullptr;
PatcherInstance* _PI = nullptr;

#include "modules/IniUtf8.inc.cpp"
#include "modules/ConfigLog.inc.cpp"
#include "modules/Entry.inc.cpp"
