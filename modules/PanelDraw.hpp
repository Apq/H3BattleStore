// PanelDraw.hpp - 面板内容绘制的函数声明（实现在 PanelDraw.inc.cpp，
// H3Auto.cpp 中排在 SettingsDlg.inc.cpp 之后）。仅声明 SettingsDlg 的
// 输入处理/钩子/生命周期直接调用的函数；其余绘制函数是 PanelDraw 内部静态。
#pragma once

#include "PanelLayout.hpp"

static void DrawMeleePickMarker_();
static void GetHelpModalCloseRect_(int* out_x, int* out_y, int* out_w, int* out_h);
static void GetHelpLinkRect_(int* out_x, int* out_y, int* out_w, int* out_h);
// 2026-10-15 移植：源插件的 GetHelpPackBtnRect_（打包日志按钮矩形）已随
// 「打包日志」功能一并移除（宿主已有该功能，见 docs/11）。
// 2026-10-16：GetHelpLogLevelDdRect_/GetHelpLogLevelItemRect_（帮助页的
// 日志级别下拉）同批移除——战场存档界面第一行已有日志等级单选列表。
static void StatusSlotRect_(int index, int* x, int* y);
// 保持状态页三小列几何（2026-10-16 两大列×三小列重排）：绘制与命中共用同一
// 组映射函数，避免两处各留一份常量。见 PanelDraw.inc.cpp 定义。
static int StatusRowY_(int index);
static int StatusMagicX_(int index);
static int StatusModeX_(int index);
static int StatusUnitX_(int index);
// 三小列的下拉列表：展开方向与可见行数（绘制/命中/滚轮共用同一组函数）。
static int StatusListVisibleRows_(int index, int item_h, int count);
static int StatusListOriginY_(int index, int item_h, int count);
static void StatusMagicItemRect_(int visual_row, int* x, int* y, int* w, int* h);
static void StatusModeItemRect_(int item, int* x, int* y, int* w, int* h);
static void StatusUnitItemRect_(int visual_row, int* x, int* y, int* w, int* h);
static void GetSummonSpellDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h);
static void GetSummonCondDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h);
static void GetSummonActDdItemRect_(int item, int* out_x, int* out_y,
    int* out_w, int* out_h);
static void DrawPanelToBuffer_();

static void DrawAttackMagicPage_(H3LoadedPcx16* scr);
static void DrawAttackMagicDropdown_(H3LoadedPcx16* scr);
static const char* StatusSpellName_(int spell_id, char* out, int out_size);
