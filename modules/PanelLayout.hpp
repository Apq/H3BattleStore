// PanelLayout.hpp - 设置面板布局常量（SettingsDlg / PanelGfx / PanelDraw 共用）。
#pragma once

static const int PANEL_W    = 680;
// 底部状态栏 +26，背景图 HA_bg.pcx 同步为 680×548（底部 y=508 前插 20px 木纹带，底框原样下移）。
static const int PANEL_H    = 548;
static const int COLS       = 1;   // 一行一格（单列宽格）
static const int VISIBLE_ROWS = 3; // 首行放每回合施法时机，下方三行部队卡片
static const int CELL_W     = 511; // 内容区 129..660(=帮助按钮右缘636+24)，滚动条前留4px
// 移除施法行后卡片高88，其余信息上移22px，头像原坐标保持。
static const int CELL_H     = 88;
static const int CELL_STEP_X = CELL_W - 2;
static const int CELL_STEP_Y = CELL_H - 2;
// 内容区边界（原金框范围；框已不画，卡片与保活行以此对齐）。
static const int GRID_FRAME_W = 531;  // 内容区右缘 660 = 帮助按钮右缘(HELP_BTN_X 636+SIZE 24)
static const int GRID_FRAME_H = 342;
static const int GRID_FRAME_X = 129;  // 左界左移 10
static const int GRID_FRAME_Y = 109;  // 窗口加高20：内容整体下移15
static const int GRID_X      = GRID_FRAME_X; // 卡片贴内容区左缘
// 2026-10-17 用户实测微调：首行整体再下移 5（105→110），后再移 3（110→113），
// 部队框底 173。
static const int ARMY_TIMING_Y = 113;
static const int ARMY_TIMING_H = 18;
static const int ARMY_TIMING_LABEL_W = 86;
static const int ARMY_TIMING_MODE_X = GRID_FRAME_X + ARMY_TIMING_LABEL_W;
static const int ARMY_TIMING_MODE_W = 86;
static const int ARMY_TIMING_UNIT_X = GRID_FRAME_X + ARMY_TIMING_LABEL_W
    + ARMY_TIMING_MODE_W + 10;
// 2026-10-17 起右侧还要放「等待后」复选框（见下），部队框由 280 收窄到 264，
// 让整行右缘（WAIT_X + WAIT_HIT_W = 651）仍落在内容区右缘 660 之内。
static const int ARMY_TIMING_UNIT_W = 264;
// 「等待后」复选框：只在模式为「指定部队」时出现，画在部队下拉框右侧
// （用户要求「后面再加一个复选框」）。命中区＝方框 + 文字，交互与停止条件的
// 「敌方魔力≤」复选框同款（点方框或文字都切换）。
static const int ARMY_TIMING_WAIT_CHECK_W = 12;
static const int ARMY_TIMING_WAIT_TEXT_W  = 56;   // 「等待后」3 字（6 半角宽）
static const int ARMY_TIMING_WAIT_HIT_W   =
    ARMY_TIMING_WAIT_CHECK_W + 4 + ARMY_TIMING_WAIT_TEXT_W;
static const int ARMY_TIMING_WAIT_X = ARMY_TIMING_UNIT_X + ARMY_TIMING_UNIT_W + 8;
// 首行行高按部队框（要放头像，与保持状态第三小列同款）＝STATUS_UNIT_ITEM_H。
static const int GRID_Y      = 186;  // 2026-10-17 用户实测：三卡片+滚动条 171→181 后再移 5（181→186）；首行（113+60）下方 13px，三卡片底 446
static const int SCROLL_W    = 16;
static const int SCROLL_X    = GRID_FRAME_X + GRID_FRAME_W - SCROLL_W; // 贴内容区右缘
static const int SCROLL_Y    = GRID_Y;
static const int SCROLL_H    = CELL_H + (VISIBLE_ROWS - 1) * CELL_STEP_Y;  // 260（3行）
static const int MARGIN      = 20;
static const int TITLE_H    = 44;
static const int BTN_W      = 64;
static const int BTN_H      = 30;
static const int BTN_FRAME_W = 66;
static const int BTN_FRAME_H = 32;
static const int BTN_GAP    = 24;
static const int BTN_Y      = PANEL_H - 80;
static const int OK_X       = (PANEL_W - BTN_GAP) / 2 - BTN_W;
static const int CANCEL_X   = (PANEL_W + BTN_GAP) / 2;
static const int CELL_COUNT = COLS * VISIBLE_ROWS;
static const int MAX_STACKS = 21;
static const int PROFILE_COUNT = 5;
static const int PROFILE_BTN_W = 104;
static const int PROFILE_BTN_H = 18;
static const int PROFILE_BTN_GAP = 8;
static const int PROFILE_BTN_Y = 47;
static const int PROFILE_BTNS_W = PROFILE_COUNT * PROFILE_BTN_W
    + (PROFILE_COUNT - 1) * PROFILE_BTN_GAP;
static const int PROFILE_BTN_X = (PANEL_W - PROFILE_BTNS_W) / 2;

// 右上角帮助按钮（点击弹使用说明模态框）
static const int HELP_BTN_SIZE = 24;
static const int HELP_BTN_X = PANEL_W - HELP_BTN_SIZE - 20; // 再左收 1px
static const int HELP_BTN_Y = 20; // 再下收 2px

// 本场存档下拉（原「读档」「存档」两按钮合并区）：打开列出该场战斗
// 每次「确定」的存档（时间降序），选中即载入草稿。列表向下展开浮层。
static const int STORE_BTN_W = 44;
static const int STORE_BTN_GAP = 6;
static const int SAVE_BTN_X = HELP_BTN_X - STORE_BTN_GAP - STORE_BTN_W;
static const int LOAD_BTN_X = SAVE_BTN_X - STORE_BTN_GAP - STORE_BTN_W;
// 按钮与展开列表同宽（160，容纳时间戳）、右缘对齐原两按钮区右缘。
static const int BATTLE_DD_W = 160;
static const int BATTLE_DD_X = SAVE_BTN_X + STORE_BTN_W - BATTLE_DD_W;
static const int BATTLE_DD_Y = HELP_BTN_Y;
static const int BATTLE_DD_H = HELP_BTN_SIZE;
static const int BATTLE_DD_LIST_W = BATTLE_DD_W;
static const int BATTLE_DD_LIST_X = BATTLE_DD_X;
static const int BATTLE_DD_LIST_Y = HELP_BTN_Y + HELP_BTN_SIZE;
static const int BATTLE_DD_ITEM_H = 14;
static const int BATTLE_DD_MAX_ITEMS = 30;

// ---- 多级导航：左侧 Tab 条 + 金框色分隔竖线（方案行以下、确定/取消以上）----
enum {
    PAGE_ARMY    = 0,   // 部队：21 槽卡片表
    PAGE_SUMMON  = 1,   // 召唤：召唤通道参数（法术/阈值）+ 召唤物共享行动规则
    PAGE_STATUS  = 2,   // 保持状态：力盾落点 + 友方增益列表 + 敌方减速
    PAGE_ATTACK  = 3,   // 攻击魔法：智能攻击配置（两大列行布局）
    PAGE_COUNT   = 4,
};
static const int TAB_VISIBLE_COUNT = 4;
static const int kTabOrder[TAB_VISIBLE_COUNT] = {
    PAGE_ARMY, PAGE_SUMMON, PAGE_STATUS, PAGE_ATTACK,
};
static const int FF_ROW_Y = 108;
static const int FF_LABEL_W = 80;
static const int FF_SLOT_X = GRID_FRAME_X + FF_LABEL_W + 6;
static const int FF_SLOT_W = 40;
static const int FF_SLOT_H = 18;
static const int FF_SLOT_GAP = 6;
static const int TAB_X       = 17;   // Tab 条左缘
static const int TAB_W       = 108;  // Tab 条总宽
static const int TAB_ITEM_W  = 100;
static const int TAB_ITEM_H  = 24;
static const int TAB_GAP     = 8;
static const int TAB_FIRST_Y = 85;  // 与保活行上缘对齐
static const int TAB_SEP_X   = 119;  // 分隔竖线（2px 金框色）
static const int TAB_SEP_Y0  = 72;   // 随内容+15后上移10（相对内容）
static const int TAB_SEP_Y1  = 456;  // 随内容+15；2026-10-17 卡片再下移后卡片底=446，与下线间距 10px（线位置不动）
// 两条横向金线（2px，与竖线同色）：上线接竖线上端、下线接竖线下端，
// 框住 Tab 条与内容区。
static const int TAB_HLINE_X0 = 17;
static const int TAB_HLINE_X1 = 660;  // 横线右端对齐内容区右缘/帮助按钮右缘
static const int TAB_HLINE_Y  = TAB_SEP_Y0;  // 上线：接竖线上端
static const int TAB_HLINE2_Y = TAB_SEP_Y1;  // 下线：接竖线下端（确定/取消上方）
static const int STATUS_SEP_Y  = BTN_Y + BTN_H + 8; // 状态栏分隔线（原图抹除，代码绘制；原版486+20，文字509紧贴线下）

// 自动停止行（全局常显）：上横线与表格之间的新行（原方案页专属，方案页已移除）。
// 保活与召唤同一条施法通道（保活优先、召唤兜底），不再有方案级「保活」下拉；
// 召唤是否启用在召唤页的复选框（SUMMON_ENABLE_*）。
static const int STOP_ROW_Y        = 85;   // 与Tab标签上缘对齐（原83再下移2）
static const int STOP_ROW_H        = 18;

// 保活行最右侧：自动停止回合数。点击后用数字键录入（可编辑文本框：
// 预填当前值、光标可左右移动、退格删除；见 s_stop_turns_* 状态）。
// 其右是「[✓] 敌方魔力≤ [框] 时停」组合（复选框=启用，框=阈值 0..32767
// 默认 6，见 s_mana_th_* 状态），停止组整体左移让位。
static const int STOP_MANA_CHECK_W = 12;    // 复选框边长（框在文字前）
static const int STOP_MANA_TEXT_W  = 72;    // 「敌方魔力≤」（5 字符）
static const int STOP_MANA_HIT_W   = STOP_MANA_CHECK_W + 4 + STOP_MANA_TEXT_W;
static const int STOP_MANA_TH_BOX_W = 36;   // 魔力阈值数字框
static const int STOP_MANA_TAIL_W  = 30;    // 「时停」
static const int STOP_MANA_X       = GRID_FRAME_X + GRID_FRAME_W
    - STOP_MANA_TAIL_W - STOP_MANA_TH_BOX_W - 2 - STOP_MANA_TEXT_W
    - 4 - STOP_MANA_CHECK_W;
static const int STOP_MANA_TH_BOX_X = STOP_MANA_X + STOP_MANA_HIT_W;
static const int STOP_MANA_TAIL_X   = STOP_MANA_TH_BOX_X
    + STOP_MANA_TH_BOX_W + 2;
static const int STOP_MANA_TH_MAX_DIGITS = 5; // 0..32767
static const int STOP_BOX_W = 52;
static const int STOP_BOX_X = STOP_MANA_X - 10 - STOP_BOX_W;
static const int STOP_LABEL_W = 42;
static const int STOP_LABEL_X = STOP_BOX_X - 4 - STOP_LABEL_W;

// 停止条件组金框（1px）：把「停止 [框] 回合」与「[✓] 敌方魔力≤ [框] 时停」
// 两个一起生效的停止条件框起来，提示玩家这是一组。左边留 5px 内边距；
// 右侧「时停」文字右缘已贴内容区右缘（660），框右缘=尾部文字右缘对齐，
// 不再外扩（外扩会超出内容区、与右上控件挤）。上下各 4px 包住整行控件。
static const int STOP_GROUP_PAD_X = 5;
static const int STOP_GROUP_PAD_Y = 4;
static const int STOP_GROUP_X = STOP_LABEL_X - STOP_GROUP_PAD_X;
static const int STOP_GROUP_Y = STOP_ROW_Y - STOP_GROUP_PAD_Y;
static const int STOP_GROUP_W = STOP_MANA_TAIL_X + STOP_MANA_TAIL_W
    - STOP_GROUP_X;
static const int STOP_GROUP_H = STOP_ROW_H + STOP_GROUP_PAD_Y * 2;

// ---- 召唤页（PAGE_SUMMON）：启用行 + 设置行 + 说明行 + 召唤物行动卡 ----
// 启用复选框（第一行）：勾选后复活无人可救时自动召唤兜底。
static const int SUMMON_ENABLE_Y  = STOP_ROW_Y;   // 与「停止:」同一行（高度对齐）
static const int SUMMON_ENABLE_H  = STOP_ROW_H;
static const int SUMMON_ENABLE_X  = GRID_FRAME_X;
static const int SUMMON_ENABLE_CHECK_W = 12;
static const int SUMMON_ENABLE_TEXT_W  = 110;  // 「启用自动召唤」
static const int SUMMON_ENABLE_HIT_W =
    SUMMON_ENABLE_CHECK_W + 4 + SUMMON_ENABLE_TEXT_W;
static const int SUMMON_NOTE_Y = 108;   // 说明行（紧跟启用行）
static const int SUMMON_NOTE_H = 18;
static const int SUMMON_SPELL_Y = 132;   // 魔法行
static const int SUMMON_ROW1_Y  = 160;   // 条件框第一行（队数）
static const int SUMMON_ROW2_Y  = 182;   // 条件框第二行（和/或 + 血量）
static const int SUMMON_DD_H    = 18;    // 行内控件高（与停止行一致）
// 魔法选择下拉：自动 + 气水火土（顺序同 kSummonSpellIds）。
static const int SUMMON_DD_LABEL_X  = GRID_FRAME_X;
static const int SUMMON_DD_LABEL_W  = 40;   // 「魔法:」
static const int SUMMON_DD_X        = GRID_FRAME_X + 44;
static const int SUMMON_DD_W        = 118;
static const int SUMMON_DD_ITEM_H   = 18;
// 两个召唤条件的 1px 金框。框左缘与上下两个下拉对齐，框左写「条件:」：
//   行一（左端留空）  队数 < [2]
//   行二  [和▾]       血量 ≤ [750]
static const int SUMMON_COND_PAD    = 5;
static const int SUMMON_COND_LABEL_X = GRID_FRAME_X;
static const int SUMMON_COND_LABEL_W = 48;                 // 「条件:」
static const int SUMMON_CB_DD_X     = SUMMON_DD_X + SUMMON_COND_PAD;
static const int SUMMON_CB_DD_W     = 44;
static const int SUMMON_CB_DD_ITEM_H = 18;
static const int SUMMON_COND_X      = SUMMON_CB_DD_X + SUMMON_CB_DD_W + 8;
static const int SUMMON_CNT_LABEL_X = SUMMON_COND_X;       // 「队数 <」
static const int SUMMON_CNT_LABEL_W = 48;
static const int SUMMON_CNT_BOX_X   = SUMMON_CNT_LABEL_X + SUMMON_CNT_LABEL_W + 4;
static const int SUMMON_CNT_BOX_W   = 44;
static const int SUMMON_CNT_MAX_DIGITS = 2;
// 血量阈值框（≥0，默认 750；口径同自动停止的己方血量合计）。
static const int SUMMON_HP_LABEL_X  = SUMMON_COND_X;       // 「血量 ≤」
static const int SUMMON_HP_LABEL_W  = 48;
static const int SUMMON_HP_BOX_X    = SUMMON_HP_LABEL_X + SUMMON_HP_LABEL_W + 4;
static const int SUMMON_HP_BOX_W    = 64;
static const int SUMMON_HP_MAX_DIGITS = 10;
static const int SUMMON_COND_GROUP_X = SUMMON_DD_X;
static const int SUMMON_COND_GROUP_Y = SUMMON_ROW1_Y - SUMMON_COND_PAD;
static const int SUMMON_COND_GROUP_W =
    SUMMON_HP_BOX_X + SUMMON_HP_BOX_W + SUMMON_COND_PAD - SUMMON_COND_GROUP_X;
static const int SUMMON_COND_GROUP_H =
    SUMMON_ROW2_Y + SUMMON_DD_H + SUMMON_COND_PAD - SUMMON_COND_GROUP_Y;
// 召唤物行动行（普通行，不做成卡片）：行动下拉 + 降级勾选，与设置行同款。
static const int SUMMON_ACT_ROW_Y = SUMMON_COND_GROUP_Y + SUMMON_COND_GROUP_H + 8;
static const int SUMMON_ACT_LABEL_X = GRID_FRAME_X;
static const int SUMMON_ACT_LABEL_W = 40;   // 「行动:」
static const int SUMMON_ACT_DD_X    = GRID_FRAME_X + 44;
static const int SUMMON_ACT_DD_Y    = SUMMON_ACT_ROW_Y;
static const int SUMMON_ACT_DD_W    = 130;
static const int SUMMON_ACT_DD_H    = 18;
static const int SUMMON_ACT_DD_ITEM_H = 18;
// 「允许降级为防御」复选框：框在文字前；仅行动=随机移动时显示并生效。
static const int SUMMON_FB_CHECK_W = 12;
static const int SUMMON_FB_X  = SUMMON_ACT_DD_X + SUMMON_ACT_DD_W + 18;
static const int SUMMON_FB_Y  = SUMMON_ACT_ROW_Y + 3;   // 与 18 高下拉中线对齐
static const int SUMMON_FB_TEXT_W = 130;

// 保持状态页：力盾落点行 → 说明 → 阈值 → 槽位区。
// 槽位区（2026-10-16 用户要求重排）：**两大列**，每大列 4 行（2×4＝8＝
// kStatusSlotCapacity），**序号按"先行后列"排**（见 StatusSlotCol_/StatusSlotRow_）；
// 每槽**三个小列**——魔法下拉（按 4 个汉字宽）、
// 全体/指定下拉（按 2 个汉字宽）、部队下拉（生物头像 + 位置 + 数量）。
// 行高按第三个下拉框的头像高度算（用户要求），整块收在 MAGIC_LIST_BOTTOM 之上。
static const int STATUS_NOTE_Y = FF_ROW_Y + 28;
static const int STATUS_BIG_COLS = 2;
static const int STATUS_ROWS = 4;
static const int STATUS_BIG_GAP = 10;   // 两大列之间
static const int STATUS_BIG_W =
    (GRID_FRAME_W - (STATUS_BIG_COLS - 1) * STATUS_BIG_GAP) / STATUS_BIG_COLS;
static const int STATUS_SUB_GAP = 4;    // 大列内三小列之间
// TwCrPort 原图框（与部队页卡片同源：58×64 头像画在 60×66 框里）。
static const int STATUS_ICON_FRAME_W = 60;
static const int STATUS_ICON_FRAME_H = 66;
static const int STATUS_ICON_H = 56;    // 头像高＝行高基准
static const int STATUS_ICON_W = 50;    // 按原图框等比
static const int STATUS_SLOT_H = STATUS_ICON_H + 4;
static const int STATUS_ROW_GAP = 4;

// 槽位序号 → (大列, 行) 的映射：**先行后列**（2026-10-16 用户实测裁定
// 「魔法列表那里增加时,+按钮出现的顺序不对,现在是先列后行,应该是先行后列」）。
// 序号 0/1 = 第一行的左右两列，2/3 = 第二行，依此类推。「＋」按钮画在第
// slot_count 个槽位上，所以玩家看到的新增落点顺序就是行优先；原实现是先列后行
// （0..3 把左列跑满再跳右列），与"从左到右、从上到下"的直觉相反。
// 绘制与命中都只经这两个函数（技能 h3auto-panel-dropdown-store：几何必须同源），
// 顺序本身由 PolicyCoreTests 直接钉住——它不像行数/列数那样会被 static_assert 拦住。
inline int StatusSlotCol_(int index) { return index % STATUS_BIG_COLS; }
inline int StatusSlotRow_(int index) { return index / STATUS_BIG_COLS; }
static const int STATUS_MAGIC_W = 72;   // 魔法下拉：4 个字 + 内边距 + 箭头
static const int STATUS_MODE_W  = 46;   // 全体/指定：2 个字 + 内边距 + 箭头
static const int STATUS_UNIT_W  = STATUS_BIG_W - STATUS_MAGIC_W - STATUS_MODE_W
    - 2 * STATUS_SUB_GAP;               // 头像 + 位置 + 数量
// 魔法下拉展开列表覆盖当前大列宽，确保法术名与等级系别后缀完整可见。
// 浮层只在列表展开时使用，收起态仍保持原来的窄框宽度。
static const int STATUS_MAGIC_LIST_W = STATUS_BIG_W;
static const int STATUS_ADD_W = STATUS_BIG_W;
static const int STATUS_ADD_H = STATUS_SLOT_H;
static const int STATUS_ITEM_H = 18;      // 魔法下拉列表项高
static const int STATUS_MODE_ITEM_H = 18; // 全体/指定列表项高
// 部队下拉列表项高＝行高（要放头像），可见行数按内容区底 MAGIC_LIST_BOTTOM 收。
static const int STATUS_UNIT_ITEM_H = STATUS_SLOT_H;
// 战场存活部队候选上限：双方各 21 槽。
static const int STATUS_UNIT_CAP = 42;
// 保持状态魔法下拉的候选容量：己方 19 + 敌方 10 + 最多两行分组标题（「己方」/
// 「敌方」各一行，只在对应组非空时出现）。留足余量：以后扩表若撑破这里，多出来
// 的法术会静默不出现在下拉里（能看到但选不到），故 UiPortTests 侧不测，
// 由 PolicyCoreTests 的几何用例钉住"表大小 + 标题行 ≤ 容量"。
static const int STATUS_MAGIC_DD_CAP = 96;

// 下拉列表展开几何（纯函数，单测可钉）：在内容区 [area_top, area_bottom] 内，
// 以收起框 (box_y, box_h) 为基准，优先向下展开；下方能放的行数比上方少时改成
// 向上展开，可见行数取两者较大的那个。末几行（保持状态页第 3、4 行）向下只剩
// 0~1 行，若不翻上去就会出现"点开是空的、选项也点不中"。绘制、命中与滚轮共用。
inline int ListVisibleRowsInArea_(int box_y, int box_h, int item_h, int count,
    int area_top, int area_bottom, bool* open_up)
{
    if (open_up) *open_up = false;
    if (item_h <= 0 || count <= 0) return 0;
    int below = (area_bottom - (box_y + box_h)) / item_h;
    int above = (box_y - area_top) / item_h;
    if (below < 0) below = 0;
    if (above < 0) above = 0;
    if (below > count) below = count;
    if (above > count) above = count;
    const bool up = above > below;
    if (open_up) *open_up = up;
    return up ? above : below;
}

inline int ListOriginYInArea_(int box_y, int box_h, int item_h, int count,
    int area_top, int area_bottom)
{
    bool up = false;
    const int rows = ListVisibleRowsInArea_(box_y, box_h, item_h, count,
        area_top, area_bottom, &up);
    return up ? box_y - rows * item_h : box_y + box_h;
}

// 阈值行（说明行下方、槽位上方）：框内数字 1..9（默认 1）——任一已
// 配置增益/减速在任一队上剩余回合 ≤ 它就补施该法术（多达标取剩余
// 最少者）。见 s_status_th_* 编辑态。
static const int STATUS_TH_ROW_Y = STATUS_NOTE_Y + 26;
static const int STATUS_TH_ROW_H = 18;
static const int STATUS_TH_LABEL_W = 66;   // 「剩余回合≤」（5 字符）
static const int STATUS_TH_BOX_W  = 36;
static const int STATUS_TH_TAIL_W = 27;    // 「时补」
static const int STATUS_TH_BOX_X  =
    GRID_FRAME_X + STATUS_TH_LABEL_W + 2;
static const int STATUS_TH_TAIL_X =
    STATUS_TH_BOX_X + STATUS_TH_BOX_W + 2;
static const int STATUS_TH_MAX_DIGITS = 1; // 1..9

// 槽位区第一行（阈值行下方）。
static const int STATUS_ROW0_Y =
    STATUS_TH_ROW_Y + STATUS_TH_ROW_H + 10;

// 状态槽区下界（槽位与其展开列表都收在这条线以上）。
static const int MAGIC_LIST_BOTTOM = 448;
// 攻击魔法页（PAGE_ATTACK）独立布局：没有力盾行/状态槽，说明行从停止
// 行下方开始；标题与列表为独立常量，不再由状态槽位置推导。
static const int ATT_NOTE_Y = 105;               // 说明行（停止行底103+2）
static const int ATT_HEADER_Y = ATT_NOTE_Y + 26; // 标题行
static const int ATT_LIST_Y = ATT_HEADER_Y + 26; // 首个视觉行上缘
// 智能攻击区两列布局：每个视觉行排两大列，每大列内部两小列
// （法术列 + 条件控件列），合计占满面板内容区的 4 个小列位。
static const int MAGIC_ATT_COL_W = 261;                       // 单个大列宽
static const int MAGIC_ATT_COL_GAP =
    GRID_FRAME_W - MAGIC_ATT_COL_W * 2;                        // 大列间距
static const int MAGIC_ATT_SPELL_W = 135;                     // 小列：法术下拉
static const int MAGIC_ATT_CTRL_W =
    MAGIC_ATT_COL_W - MAGIC_ATT_SPELL_W - 6;                   // 小列：目标/位置
static const int MAGIC_ATT_SUB_GAP = 6;                        // 大列内小列间距

// 召唤页说明/启用文案已外置（UiTexts panel.summon_enable 等）。

// 硬编码默认标签（INI 加载失败时使用）
static const char* DEFAULT_ACTION_LABELS[AA_COUNT] = {
    "手动", "防御", "等待", "循环移动", "循环近战", "远程攻击", "急救治疗",
    "散开", "随机移动", "不反兵攻城",
};
static const char* DEFAULT_SELECTOR_LABELS[SEL_COUNT] = {
    "随机", "远程飞兵高速优先", "数量最多", "失血比例", "失血数值",
};

// 运行时标签
