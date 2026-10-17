#pragma once

#include <stdint.h>
#include <stddef.h>

// H3Auto 的纯策略核心：不得依赖 H3API、游戏地址、窗口或日志。
// 生产代码和 tests/PolicyCoreTests.cpp 共用这里的规则，避免测试副本与实际逻辑漂移。
namespace H3AutoPolicy {

enum AutoActionKind : uint8_t {
    AA_MANUAL = 0,
    AA_DEFEND,
    AA_WAIT,
    AA_MOVE,
    AA_MELEE_ATTACK,
    AA_RANGED_ATTACK,
    AA_FIRST_AID,
    // 召唤物共享规则专用（普通部队行动下拉不出现）：
    AA_SCATTER,       // 散开：与己方其它部队分级拉开距离（≥2 格 → ≥1 格 → 原地防御）
    AA_RANDOM_MOVE,   // 随机移动：可达格均匀随机
    // 不反兵攻城（2026-10-14 用户提出）：不反兵（九头/蛇女等）在攻城战里的循环打法。
    // 「等待」是一种部队行动选择——现在轮到它但先不动，等其它部队动完再次轮到它；
    // 所以第 1 回合：先等待，再次轮到才穿开着的城门进去打一个敌人（目标自动按
    // 「远程飞兵高速优先」挑）；第 2 回合：不等待，直接退到玩家指定的「安全位置」
    // （退回城外，让敌人行动阶段够不到它）。两步交替循环。详见 docs/02 对应小节。
    AA_SIEGE,
    AA_COUNT
};

enum AutoTargetKind : uint8_t {
    AT_NONE = 0,
    AT_STACK,
    AT_POSITION,
    AT_COUNT
};

enum AutoTargetSide : uint8_t {
    ATS_OWN = 0,
    ATS_ENEMY,
    ATS_EITHER,
    ATS_COUNT
};

enum AutoTargetSelector : uint8_t {
    SEL_RANDOM = 0,
    SEL_RANGED_SPEED,
    SEL_COUNT_HIGH,
    SEL_WOUND_RATIO,
    SEL_WOUND_VALUE,
    SEL_COUNT
};

// H3 内部 creature id。纯核心不引用 H3API 的 eCreature 枚举。
static constexpr int CREATURE_CATAPULT = 0x91;
static constexpr int CREATURE_BALLISTA = 0x92;
static constexpr int CREATURE_FIRST_AID_TENT = 0x93;
static constexpr int CREATURE_AMMO_CART = 0x94;
static constexpr int CREATURE_ARROW_TOWER = 0x95;

static constexpr int MELEE_PAIR_CAPACITY = 10;
static constexpr int MOVE_WAYPOINT_CAPACITY = 16;

// ---- 召唤通道常量（保活通道 = 兵力不足时召唤）----
static constexpr int SUMMON_ELEMENT_COUNT = 4;   // 固定顺序：气/水/火/土（与法术下拉一致）
static constexpr int kSummonSpellIds[SUMMON_ELEMENT_COUNT] = {69, 68, 66, 67};   // 气/水/火/土
static constexpr int kSummonCreatureIds[SUMMON_ELEMENT_COUNT] = {112, 115, 114, 113};
// 自动停止第二条件（敌方魔力停手）默认阈值：约「最多再放一次」低阶法术的量。
// 随方案可调（0..32767，随战斗 JSON 方案存档）。
static constexpr int kSummonStopManaDefault = 6; // 敌方魔力阈值默认（0..32767 可调）
// 战场六格坐标（15×11，索引 = y*15+x；实参语义与原版距离对拍见实施文档 §5）。
static constexpr int BATTLEFIELD_COLS = 15;
static constexpr int BATTLEFIELD_HEXES = 165;

struct AutoTargetRule {
    AutoTargetKind kind;
    AutoTargetSide side;
    AutoTargetSelector selector;
    int16_t meleeStandHex;
    int16_t meleeAttackHex;
    // 不反兵攻城的「安全位置」：退避那一步走到这里（攻城的城外围格）。
    // 只对该动作有意义，-1=未设置。
    int16_t siegeSafeHex;
    int16_t moveWaypoints[MOVE_WAYPOINT_CAPACITY];
    int8_t moveWaypointCount;
    int16_t meleeStandHexes[MELEE_PAIR_CAPACITY];
    int16_t meleeAttackHexes[MELEE_PAIR_CAPACITY];
    int8_t meleePairCount;
};

// 保活方式（部队级，三选一）：挂在每条规则上。
// PM_NONE=不保活（默认）：该队永不参与保活施法。
enum ProtectMode : uint8_t {
    PM_COUNT_BELOW = 0,   // 按剩余数量：剩余数量 ≤ 该队阈值才救（默认 2；0=只救全灭）
    PM_LOSS_GT_RESTORE,   // 损失量大于恢复量：已损 HP 严格大于一次可恢复量才救
    PM_NONE,              // 不保活：该队不参与（默认）
    PM_COUNT
};

// 等待方式（部队级，三选一）：游戏内「等待」是每支部队自己的一次机会。
// AW_NONE=不等待（默认）：轮到直接执行配置动作。
// AW_EVERY_TURN=每回合：每次轮到它时先选「等待」，等其它部队动完再次轮到它时再执行配置动作。
// AW_SIEGE=不反兵攻城：这套打法自己的等待规则——入城攻击那一步先等待，退避那一步不等待。
// （2026-10-14 用户裁定：原「智能」占位项改名为「不反兵攻城」，即本打法的等待规则。）
enum AutoWaitMode : uint8_t {
    AW_NONE = 0,
    AW_EVERY_TURN,
    AW_SIEGE,
    AW_COUNT
};

struct AutoStackRule {
    AutoActionKind action;
    AutoTargetRule target;
    bool allowDefendFallback;

    // 等待方式（默认不等待）。手动行动不参与等待：那支部队整回合交给玩家。
    uint8_t         waitMode;            // AutoWaitMode

    // 保活方式三选一（默认不保活）；法术自动选：亡灵用聚灵、活体用复活。
    uint8_t         protectMode;         // ProtectMode：0=按剩余数量；1=损失量大于恢复量；2=不保活
    // 按数量保活阈值（PM_COUNT_BELOW）：该队剩余数量 ≤ 此值才救。
    // 每队各自一份，默认 2，范围 0..2147483647；损失量方式下不参与判定。
    int             protectCountBelow;
};

inline AutoStackRule MakeDefaultRule()
{
    AutoStackRule r = {};
    r.action = AA_MANUAL;
    r.target.kind = AT_NONE;
    r.target.side = ATS_ENEMY;
    r.target.selector = SEL_RANDOM;
    r.target.meleeStandHex = -1;
    r.target.meleeAttackHex = -1;
    r.target.siegeSafeHex = -1;
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i)
        r.target.moveWaypoints[i] = -1;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        r.target.meleeStandHexes[i] = -1;
        r.target.meleeAttackHexes[i] = -1;
    }
    r.target.moveWaypointCount = 0;
    r.target.meleePairCount = 0;
    r.allowDefendFallback = false;
    r.waitMode = AW_NONE;
    r.protectMode = PM_NONE;
    r.protectCountBelow = 2;
    return r;
}

// 状态魔法白名单按**目标阵营**分成两张表（2026-10-16 用户裁定：「魔法下拉框
// 中的可选项改成根据魔法的目标分为己方、敌方两大类，己方排在前面」）。
//
// 己方持续增益（19）：原版友方范围增益，＋本轮补入的烈火神盾(29)/抗魔术(34)/
// 魔法镜像(36) 三个可持续增益。白名单只界定"哪些算状态魔法"，**不再要求法术
// 表带群体版**：会不会以魔法书为准，全体还是指定由每槽的目标模式（+该法术所属
// 学派的熟练度）决定。
static constexpr int kBuffSpellCount = 19;
static constexpr int kBuffSpellIds[kBuffSpellCount] = {
    27, 28, 29, 30, 31, 32, 33, 34, 36, 41, 43, 44, 46, 48, 49, 51, 53, 55, 58,
};
// 敌方持续减益（10）：诅咒42、虚弱无力45、瓦解射线47、悲痛欲绝50、大难临头52、
// 迟缓大法54、丧心病狂59、蛊惑人心60、遗忘61、双目失明62。
// 数组顺序＝下拉里的显示顺序，也是"同一回合有多个都该补"时的优先顺序：
// 短效/单体/控制类排前面，迟缓大法（长时效群体）排最后——保留 2026-10-16
// 早期裁定「失明与减速同时到期先补失明」。
static constexpr int kEnemyStatusSpellCount = 10;
static constexpr int kEnemyStatusSpellIds[kEnemyStatusSpellCount] = {
    62, 61, 59, 60, 52, 50, 47, 45, 42, 54,
};
static constexpr int kSpellFlagFriendlyMass = 0x800;
static constexpr int kSpellFlagExpertMass = 0x40;
// 法术表 flags 的群体相关位（与 H3Spell::SingleTarget = 0x59E360 同口径）：
//   0x10 singleOnly：永远单体（失明 62）；0x40：专家级才有群体版；
//   0x20：高级（学校等级 ≥2）才有群体版。见 H3Note\英雄魔法禁施与目标免疫.md。
static constexpr unsigned kSpellFlagSingleOnly   = 0x10u;
static constexpr unsigned kSpellFlagAdvancedMass = 0x20u;
static constexpr int kSlowSpellId = 54;
// 敌方失明（法术 62，单体、心智类，H3CstSpells.hpp:111 BLIND=62）：
// 敌方减益里最急的一项，白名单里排第一。
static constexpr int kBlindSpellId = 62;

// 目标阵营判定（只看白名单，与是否已学、学校等级、能否群体无关）。
inline bool IsFriendlyStatusSpell(int spell_id)
{
    for (int id : kBuffSpellIds)
        if (id == spell_id) return true;
    return false;
}

inline bool IsEnemyStatusSpell(int spell_id)
{
    for (int id : kEnemyStatusSpellIds)
        if (id == spell_id) return true;
    return false;
}

// 「这是不是一个状态魔法」——只看法术 id 白名单，与是否已学、等级、群体无关。
inline bool IsStatusSpellId(int spell_id)
{
    return IsFriendlyStatusSpell(spell_id) || IsEnemyStatusSpell(spell_id);
}

// 该状态法术在当前学校等级下能不能群体施放（＝原版非单体）：判据就是用户
// 2026-10-16 定的「由魔法本身（法术表 flags）＋英雄对应学派（水火土气）的
// 熟练度决定」——
//   单体位（失明 62）永远 false；专家群体位要等级 3；高级群体位要等级 ≥2；
//   都没有群体位（如屠戮成性 55）也 false。与 StatusSpellCanMass 互补的
//   "只能单体"就是 !can_mass（原版 SingleTarget 的等价判据）。
inline bool StatusSpellCanMass(int spell_id, unsigned spell_flags, int expertise)
{
    if (!IsStatusSpellId(spell_id)) return false;
    if (expertise < 0 || expertise > 3) return false;
    if ((spell_flags & kSpellFlagSingleOnly) != 0) return false;
    if ((spell_flags & kSpellFlagExpertMass) != 0 && expertise < 3) return false;
    if ((spell_flags & kSpellFlagAdvancedMass) != 0 && expertise < 2) return false;
    return true;
}

// 每槽的目标模式（2026-10-16 用户裁定：第二小列固定「全体/指定」两项）。
static constexpr int kStatusTargetAll  = 0;  // 全体：能群体就群体，不能则按单体兜底
static constexpr int kStatusTargetPick = 1;  // 指定：打 target_hex 那一队
static constexpr int kStatusTargetModeCount = 2;

inline int StatusDefaultTargetMode(int spell_id, unsigned spell_flags, int expertise)
{
    return StatusSpellCanMass(spell_id, spell_flags, expertise)
        ? kStatusTargetAll : kStatusTargetPick;
}

// 保持状态下拉候选资格（2026-10-16 三轮定案）：**唯一门槛是英雄会不会**——
// has_spell = hero->HasSpell（learnedSpells|availableSpell，穿戴装备赋予也算，
// 与原版施法面板同口径，见 attack 通道 BuildAttackMagicCandidates_）。
// 学校技能等级（GetSpellExpertise 0..3）不是资格：它只决定群体版/效果强度
// （"学校 expertise0 不等于未学魔法"，见 H3Note\英雄魔法禁施与目标免疫.md）。
// 判定收敛成这一个函数：设置面板与单测共用同一口径，避免两侧漂移。
inline bool IsStatusCandidateSpell(int spell_id, bool has_spell)
{
    return spell_id > 0 && has_spell && IsStatusSpellId(spell_id);
}
static constexpr int kBlessSpellId = 41;
static constexpr int kPrecisionSpellId = 44;
static constexpr int kMirthSpellId = 49;
static constexpr int kSorrowSpellId = 50;
static constexpr int kFortuneSpellId = 51;
static constexpr int kMisfortuneSpellId = 52;
static constexpr int kSlayerSpellId = 55;
// 敌方减益里需要判"硬不可接受"的几个（2026-10-16 扩表时补）：
static constexpr int kCurseSpellId = 42;
static constexpr int kWeaknessSpellId = 45;
static constexpr int kDisruptingRaySpellId = 47;
static constexpr int kBerserkSpellId = 59;
static constexpr int kHypnotizeSpellId = 60;
static constexpr int kForgetfulnessSpellId = 61;
static constexpr int kArrowTowerCreatureId = 149;
// 与 H3CreatureInformation::flags / H3Spell::flags 同一套位。
static constexpr unsigned kCreatureFlagShooter = 0x4u;
static constexpr unsigned kCreatureFlagSiegeWeapon = 0x40u;
static constexpr unsigned kCreatureFlagNoMorale = 0x20000u;
static constexpr unsigned kCreatureFlagUndead = 0x40000u;
// 原版心智免疫位（H3CreatureInformation::mindImmunity = 0x400，
// NH3API CF_IMMUNE_TO_MIND_SPELLS）：免疫失明/发狂/遗忘/催眠/悲痛等
// 心智法术。亡灵另有 undead 位，失明在两边都拒绝。
static constexpr unsigned kCreatureFlagMindImmune = 0x400u;
static constexpr unsigned kSpellFlagCannotTargetSiege = 0x1000u;
// 补状态阈值默认值：剩余回合 ≤ 该值才补（阈值本体在
// StatusProfileFields.refresh_turns，玩家可改）。
static constexpr int kStatusRefreshTurns = 1;
static constexpr int kStatusRefreshTurnsMax = 9;
static constexpr int kStatusSlotCapacity = 8;

// 方案级智能攻击。target_mode 语义按法术类别强制归一：
// 单体（15/16/17/18/19/57）→ 0=远程飞兵速度优先、1=随机；
// 范围（20/21/22/23）→ 2=固定落点，配 positions[0]（唯一落点，
// 2026-10-11 用户裁定：点“+”到战场选一个位置，不循环）；
// 全体（24/25/26）→ -1=无目标控件，positions 清空。
static constexpr int kAttackMagicCapacity = 8;
static constexpr int kAttackMagicPositionCapacity = 1; // 范围法术只选一个落点
// 英雄可用的直接伤害法术。15..23 需敌人或中心落点；24 DEATH_RIPPLE/
// 25 DESTROY_UNDEAD/26 ARMAGEDDON 为全场伤害，原版按法术 id 全场
// 结算，不需要目标格（用户要求一并列出）。
// 火墙13需完整空段规则暂不纳入；57（泰坦之箭）同列。运行时资格用
// HasSpell（learnedSpells|availableSpell，穿戴装备赋予也算，2026-10-11
// 用户裁定），学校技能等级不构成资格；此处白名单只约束法术 id。
static constexpr int kAttackMagicSpellIds[] = {15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 57};

// 攻击法术类别：0 单体指定、1 范围中心、2 全场。-1 = 空行/非白名单。
inline int AttackMagicKind(int spell_id)
{
    if (spell_id == 24 || spell_id == 25 || spell_id == 26) return 2;
    if (spell_id == 20 || spell_id == 21 || spell_id == 22 || spell_id == 23) return 1;
    if (spell_id >= 15 && spell_id <= 19) return 0;
    if (spell_id == 57) return 0;
    return -1;
}

struct AttackMagicRule {
    int spell_id; // -1 = 空行，不参与执行
    int target_mode;
    int positions[kAttackMagicPositionCapacity]; // 范围法术唯一落点；空 -1
    int position_count;                        // 范围法术 0..1，其它类别 0
};

inline bool IsAttackMagicSpellId(int spell_id)
{
    for (int id : kAttackMagicSpellIds)
        if (id == spell_id) return true;
    return false;
}

inline AttackMagicRule MakeDefaultAttackMagicRule()
{
    AttackMagicRule rule = {};
    rule.spell_id = -1;
    for (int i = 0; i < kAttackMagicPositionCapacity; ++i)
        rule.positions[i] = -1;
    return rule;
}

inline void NormalizeAttackMagicRule(AttackMagicRule* rule)
{
    if (!rule) return;
    if (!IsAttackMagicSpellId(rule->spell_id)) rule->spell_id = -1;
    int requested = rule->position_count;
    if (requested < 0) requested = 0;
    if (requested > kAttackMagicPositionCapacity) requested = kAttackMagicPositionCapacity;
    int count = 0;
    for (int i = 0; i < requested; ++i) {
        const int hex = rule->positions[i];
        // SoD 的真实索引为 0..186；左右边缘列由游戏侧按目标语义过滤。
        if (hex >= 0 && hex <= 186) rule->positions[count++] = hex;
    }
    for (int i = count; i < kAttackMagicPositionCapacity; ++i) rule->positions[i] = -1;
    rule->position_count = count;
    // target_mode 按法术类别强制归一：单体只有 0/1 两个目标策略且不带
    // 落点，范围固定落点（唯一，容量 1），全体无目标控件（-1）且不带
    // 落点；空行回到默认 0。落点是范围法术的专属数据，其余类别一律清空。
    const int kind = AttackMagicKind(rule->spell_id);
    if (kind == 1) {
        rule->target_mode = 2;
    } else {
        if (kind == 2) rule->target_mode = -1;
        else if (kind == 0) rule->target_mode = rule->target_mode == 1 ? 1 : 0;
        else rule->target_mode = 0; // 空行
        for (int i = 0; i < kAttackMagicPositionCapacity; ++i) rule->positions[i] = -1;
        rule->position_count = 0;
    }
}

// 保持状态与攻击列表均为方案级。slots 空槽 0；slow 为专家群体减速开关。
// target_mode/target_hex 是每槽的目标（2026-10-16 用户要求）：全体＝原版
// 群体施放；指定＝打 target_hex 那一队（单体），-1 表示还没指定。
// 每回合施法时机（方案级）：默认=本回合首次轮到玩家行动时；指定=轮到所选己方部队时。
// 目标身份使用 StableStackIdentity 的同构字段，避免保存会移动的战场 hex；召唤物
// 以 creature id + occurrence 标识，跨战斗不可匹配时自然回退为不施法。
static constexpr int CAST_TIMING_DEFAULT = 0;
static constexpr int CAST_TIMING_UNIT = 1;
// 字段顺序刻意「先单字节、后 16 位」：int16_t 若排在单字节字段之间，自然对齐下
// 会插入 1 字节隐式填充，而生产插件用 /Zp1、单测的 cl 命令不带 /Zp1，两侧
// sizeof 与布局就会不一致。这样排下来两种对齐下都是 8 字节、零隐式填充，
// 记录比较用的 bytes_equal(sizeof) 不会碰到未初始化字节。
struct SpellCastTimingFields {
    uint8_t mode;       // CAST_TIMING_DEFAULT / CAST_TIMING_UNIT
    int8_t target_kind; // StableStackIdentityKind
    int8_t target_side;
    int8_t target_occurrence;
    // 「等待后」：仅对「指定部队」有意义（2026-10-17 用户需求）。置 1 表示该部队
    // 先「等待」、等本轮它再次被交权时才施法；置 0 = 老行为（轮到它就施法）。
    uint8_t after_wait;
    uint8_t _reserved;  // 只为把结构体凑成 8 字节，恒为 0，不参与语义
    int16_t target_value;
};

inline SpellCastTimingFields MakeDefaultSpellCastTiming()
{
    SpellCastTimingFields f = {};
    f.mode = CAST_TIMING_DEFAULT;
    f.target_kind = 0;
    f.target_side = -1;
    f.target_value = -1;
    f.target_occurrence = 0;
    f.after_wait = 0;
    f._reserved = 0;
    return f;
}

inline void NormalizeSpellCastTiming(SpellCastTimingFields* f)
{
    if (!f) return;
    if (f->mode != CAST_TIMING_UNIT) {
        *f = MakeDefaultSpellCastTiming();
        return;
    }
    if (f->target_side < 0 || f->target_side > 1
        || f->target_kind <= 0 || f->target_kind > 3
        || f->target_value < 0 || f->target_occurrence < 0) {
        *f = MakeDefaultSpellCastTiming();
        return;
    }
    f->after_wait = f->after_wait ? 1 : 0;
    f->_reserved = 0;
}

// 「等待后」的唯一判据（2026-10-17 用户裁定）：只认游戏自己记的等待标志
// （`H3CombatCreature::IsWaiting()`，creature+0x84 bit25），不再保留
// 「本轮该部队已被交权 ≥2 次」这条备用判据——它用的 turn 是行动计数
// （BattleMgr+0x13D6C，行动入口每 action 无条件 +1），每次交权都会清零，
// 同一 turn 内第一次询问后又立刻把状态锁死，所以那条判据永远凑不满、
// 是死代码。留着只会误导后来者，故删除；判定语义随之变成：
// 勾了「等待后」= 该部队本轮「已等待」才施法，与「第几次被交权」无关。

struct StatusProfileFields {
    int slots[kStatusSlotCapacity];
    int target_mode[kStatusSlotCapacity];
    int target_hex[kStatusSlotCapacity];
    int slot_count;
    int slow;
    int refresh_turns;
    AttackMagicRule attacks[kAttackMagicCapacity];
    int attack_count;
};

inline StatusProfileFields MakeDefaultStatusFields()
{
    StatusProfileFields fields = {};
    fields.refresh_turns = kStatusRefreshTurns;
    for (int i = 0; i < kStatusSlotCapacity; ++i)
        fields.target_hex[i] = -1;
    for (int i = 0; i < kAttackMagicCapacity; ++i)
        fields.attacks[i] = MakeDefaultAttackMagicRule();
    return fields;
}

inline void NormalizeStatusFields(StatusProfileFields* fields)
{
    if (!fields) return;
    fields->slow = fields->slow ? 1 : 0;
    if (fields->refresh_turns < 1 || fields->refresh_turns > kStatusRefreshTurnsMax)
        fields->refresh_turns = kStatusRefreshTurns;
    int requested = fields->slot_count;
    if (requested < 0) requested = 0;
    if (requested > kStatusSlotCapacity) requested = kStatusSlotCapacity;
    int count = 0;
    // 槽位压缩时空槽的 target_mode/target_hex 跟着一起搬，否则删除中间一槽
    // 会把后面各槽的目标错位到前一槽上。
    for (int i = 0; i < requested; ++i) {
        if (fields->slots[i] <= 0 || fields->slots[i] >= 81) continue;
        const int mode = fields->target_mode[i] == kStatusTargetPick
            ? kStatusTargetPick : kStatusTargetAll;
        int hex = fields->target_hex[i];
        if (hex < 0 || hex > 186) hex = -1;
        if (mode == kStatusTargetAll) hex = -1; // 全体不用目标格，清掉陈旧值
        fields->slots[count] = fields->slots[i];
        fields->target_mode[count] = mode;
        fields->target_hex[count] = hex;
        ++count;
    }
    for (int i = count; i < kStatusSlotCapacity; ++i) {
        fields->slots[i] = 0;
        fields->target_mode[i] = kStatusTargetAll;
        fields->target_hex[i] = -1;
    }
    fields->slot_count = count;
    if (fields->attack_count < 0) fields->attack_count = 0;
    if (fields->attack_count > kAttackMagicCapacity) fields->attack_count = kAttackMagicCapacity;
    for (int i = 0; i < fields->attack_count; ++i) NormalizeAttackMagicRule(&fields->attacks[i]);
    for (int i = fields->attack_count; i < kAttackMagicCapacity; ++i)
        fields->attacks[i] = MakeDefaultAttackMagicRule();
}

inline bool AddAttackMagicRule(StatusProfileFields* fields)
{
    if (!fields) return false;
    NormalizeStatusFields(fields);
    if (fields->attack_count >= kAttackMagicCapacity) return false;
    fields->attacks[fields->attack_count++] = MakeDefaultAttackMagicRule();
    return true;
}

inline void RemoveAttackMagicRule(StatusProfileFields* fields, int index)
{
    if (!fields) return;
    NormalizeStatusFields(fields);
    if (index < 0 || index >= fields->attack_count) return;
    for (int i = index; i + 1 < fields->attack_count; ++i) fields->attacks[i] = fields->attacks[i + 1];
    fields->attacks[--fields->attack_count] = MakeDefaultAttackMagicRule();
}

inline bool AddAttackMagicPosition(AttackMagicRule* rule, int position)
{
    if (!rule || position < 0 || position > 186) return false;
    NormalizeAttackMagicRule(rule);
    if (rule->position_count >= kAttackMagicPositionCapacity) return false;
    rule->positions[rule->position_count++] = position;
    return true;
}

inline void RemoveAttackMagicPosition(AttackMagicRule* rule, int index)
{
    if (!rule) return;
    NormalizeAttackMagicRule(rule);
    if (index < 0 || index >= rule->position_count) return;
    for (int i = index; i + 1 < rule->position_count; ++i) rule->positions[i] = rule->positions[i + 1];
    rule->positions[--rule->position_count] = -1;
}

struct StatusMaintainChoice {
    int spell_id;   // -1 = 本回合不补
    int target_hex; // 群体减速时是剩余回合最短的那队
    int mass;       // 1 = 专家群体，目标格只作施法锚点
};

// 全体法术的代表剩余回合。durations 是目标侧各存活队的原始值：
// >0 已带，0 未带，-1 不存在。全体法术各可接受部队的剩余回合一致，
// 所以找到任一已带部队就采用它；全是 0 才表示缺失。无法接受的 0
// 不覆盖已经找到的正值。
// 原版 FUN_0044a1a0 在真正上效果之前按法术分开判断，不是 AI 价值。
// 这里只复述“一定不能接受”的硬条件；敌方抵抗、反魔法不在这里。
// 读不到的字段由调用方决定是否放行，本函数不猜。
inline bool CreatureCanReceiveStatusSpell(int spell_id, unsigned spell_flags,
    unsigned creature_flags, int damage_high, int creature_id)
{
    if (creature_id == kArrowTowerCreatureId) return false;
    if ((creature_flags & kCreatureFlagSiegeWeapon) != 0
            && (spell_flags & kSpellFlagCannotTargetSiege) != 0)
        return false;
    switch (spell_id) {
    case kBlessSpellId:
        if ((creature_flags & kCreatureFlagUndead) != 0) return false;
        return damage_high > 0;
    case kFortuneSpellId:
    case kMisfortuneSpellId:
    case kSlayerSpellId:
        return damage_high > 0;
    case kMirthSpellId:
    case kSorrowSpellId:
        return (creature_flags & kCreatureFlagNoMorale) == 0;
    case kPrecisionSpellId:
        return (creature_flags & kCreatureFlagShooter) != 0;
    // 2026-10-16 扩敌方减益表时补的硬条件（下列三条为推断口径，待实机验证；
    // 宁可少列一个目标：错列会让 CanCastSpellAtCoord 失败、整回合白等）。
    case kCurseSpellId:
    case kWeaknessSpellId:
        // 降攻击：没有伤害的部队（弹药车、急救帐篷）不吃。
        return damage_high > 0;
    case kForgetfulnessSpellId:
        // 遗忘：只对射击部队有意义（阻止远程攻击）。
        return (creature_flags & kCreatureFlagShooter) != 0;
    case kBerserkSpellId:
    case kHypnotizeSpellId:
        // 丧心病狂/蛊惑人心：心智类，与失明同规则（亡灵与心智免疫拒绝）。
        return (creature_flags & (kCreatureFlagMindImmune | kCreatureFlagUndead))
            == 0;
    case kBlindSpellId:
        // 失明（62，心智类单体）：亡灵与带心智免疫位的生物不接受
        // （元素、石像/铁人/金人/钻石人、傀儡龙等）。
        return (creature_flags & (kCreatureFlagMindImmune | kCreatureFlagUndead))
            == 0;
    default:
        return true;
    }
}

inline int RepresentativeMassDuration(const int* durations, int count)
{
    if (!durations || count <= 0) return -1;
    bool saw_stack = false;
    for (int i = 0; i < count; ++i) {
        if (durations[i] < 0) continue;
        saw_stack = true;
        if (durations[i] > 0) return durations[i];
    }
    return saw_stack ? 0 : -1;
}

// durations[i] 是 spell_ids[i] 的代表剩余回合：>0 = 已带，0 = 整侧缺失，
// -1 = 未选/英雄不可施。refresh_turns：只补 ≤ 该值的；多个达标取剩余最少者。
inline int ChooseBuffToRefresh(const int* durations, int count,
    int refresh_turns)
{
    if (!durations || count <= 0) return -1;
    if (refresh_turns < 0) refresh_turns = kStatusRefreshTurns;
    int best = -1;
    for (int i = 0; i < count; ++i) {
        if (durations[i] < 0 || durations[i] > refresh_turns) continue;
        if (best < 0 || durations[i] < durations[best]) best = i;
    }
    return best;
}

// 单体（「指定」）目标选择：durations/hexes 是已通过可接受性过滤的存活队
// （durations < 0 = 该队不可接受，不参与）。preferred_hex ≥ 0 时优先用它
// ——玩家在第三小列里明确点过的那一队；它已不在候选里（阵亡/不可接受/换算
// 过场）就退回"剩余回合最少者"（并列取先出现的，即己方槽位在前）。
// 返回选中下标，无候选返回 -1。
inline int ChooseSingleStatusTarget(const int* durations, const int* hexes,
    int count, int preferred_hex)
{
    if (!durations || !hexes || count <= 0) return -1;
    if (preferred_hex >= 0) {
        for (int i = 0; i < count; ++i)
            if (durations[i] >= 0 && hexes[i] == preferred_hex) return i;
    }
    int best = -1;
    for (int i = 0; i < count; ++i) {
        if (durations[i] < 0) continue;
        if (best < 0 || durations[i] < durations[best]) best = i;
    }
    return best;
}

// 敌方**单体**状态目标（失明/遗忘/丧心病狂/蛊惑人心，以及「指定」模式下的任何
// 敌方法术）的通用选择：durations < 0 = 该队不存在/不可接受（免疫、战争机器、
// 没有可施法位置），不参与判定。
//   preferred_hex ≥ 0 ＝玩家在第三小列里点过的那一队 → 只认它：它还在候选里、
//     且剩余 ≤ 阈值才施放（目标已阵亡/不接受，或还够回合 → -1＝本回合不施放，
//     不偷偷换一个目标）；
//   preferred_hex < 0 ＝按规则自己挑：在"剩余 ≤ 阈值"的候选里取剩余最少者
//     （0 = 已经没效果了，比还剩 1 的更急），并列取威胁最大的。
// 返回候选下标，-1 = 不施放。
inline int ChooseEnemySingleStatusTarget(const int* durations, const int* hexes,
    const int64_t* threats, int count, int refresh_turns, int preferred_hex)
{
    if (!durations || !hexes || count <= 0) return -1;
    if (refresh_turns < 0) refresh_turns = kStatusRefreshTurns;
    if (preferred_hex >= 0) {
        for (int i = 0; i < count; ++i) {
            if (durations[i] < 0 || hexes[i] != preferred_hex) continue;
            return durations[i] <= refresh_turns ? i : -1;
        }
        return -1;
    }
    int best = -1;
    for (int i = 0; i < count; ++i) {
        if (durations[i] < 0 || durations[i] > refresh_turns) continue;
        if (best < 0) {
            best = i;
            continue;
        }
        if (durations[i] != durations[best]) {
            if (durations[i] < durations[best]) best = i;
            continue;
        }
        const int64_t threat_i = threats ? threats[i] : 0;
        const int64_t threat_best = threats ? threats[best] : 0;
        if (threat_i > threat_best) best = i;
    }
    return best;
}

// 敌方**群体**状态（迟缓大法这类当前等级能群体的敌方法术）：语义 A
// （2026-10-05 用户定，沿用至今）——按「全体覆盖」算达标：敌方已有任意一队还
// 带着（剩余 > 阈值）就不再施，全都没到才补一次全体。理由：敌方可能有抵抗术，
// 部分命中是常态，若按"任一队缺失即补"会在被抵抗后连续多回合重复施放，挤占
// 一回合一次的施法位与魔力。-1（不存在/免疫）不参与覆盖判定。
// out_remain 回填代表剩余回合（用于跨法术比较"谁最急"）；不施放时给 -1。
inline StatusMaintainChoice ChooseEnemyMassStatusTarget(int spell_id,
    const int* durations, int count, int refresh_turns, int* out_remain)
{
    StatusMaintainChoice choice = {-1, -1, 0};
    if (out_remain) *out_remain = -1;
    if (spell_id <= 0 || !durations || count <= 0) return choice;
    if (refresh_turns < 0) refresh_turns = kStatusRefreshTurns;
    const int rep = RepresentativeMassDuration(durations, count);
    if (rep < 0 || rep > refresh_turns) return choice;
    if (out_remain) *out_remain = rep;
    choice.spell_id = spell_id;
    choice.target_hex = -1; // 群体：目标格由引擎忽略，统一用 -1
    choice.mass = 1;
    return choice;
}

// 敌方减速：己方必须是专家群体。enemy_durations 覆盖全部存活敌方。
// 语义 A（2026-10-05 用户定）：群体迟缓按「全体覆盖」算达标——敌方已
// 有任意一队带迟缓 buff（剩余 > refresh_turns）就不再施，只有
// 敌方没有任何一队被覆盖时才施全群体。理由：敌方可能有抵抗术，部分
// 命中是常态，若按「任一队缺失即补」会在被抵抗后连续多回合重复施放，
// 挤占一回合一次的施法位与魔力；被抵抗的队留到全体 buff 将断时随群
// 体一起重新覆盖。-1 表示该队不存在/免疫，不参与覆盖判定。
inline StatusMaintainChoice ChooseSlowTarget(bool expert_mass,
    const int* enemy_durations, const int* enemy_hexes, int enemy_count,
    int refresh_turns)
{
    StatusMaintainChoice choice = {-1, -1, 0};
    if (!expert_mass || !enemy_hexes) return choice;
    return ChooseEnemyMassStatusTarget(kSlowSpellId, enemy_durations,
        enemy_count, refresh_turns, nullptr);
}

// 敌方失明（法术 62，单体）。与减速共用「剩余回合 ≤ refresh_turns 就补」的
// 补状态阈值语义，但单体法术是逐队判定——不存在减速那套「敌方任意一队已
// 覆盖即达标、不施放」的整体规则：一队到期就补它，其余队不受影响。
// enemy_durations/enemy_hexes/enemy_threat 覆盖全部存活敌方（21 槽）：
// durations < 0 = 该队不存在/不可接受失明（免疫、战争机器、没有可施法位置），
// 不参与判定；threat 由调用方按原版口径算好（单位价值 aiValue → fightValue
// → hitPoints，再乘当前数量，见 Heroes3Src/docs/ARMY_VALUE_ALGORITHM_DETAILED.md）。
// 候选排序：剩余回合越少越急（已经醒来 0 排在还带着 1 的前面），同剩余取
// 威胁最大的；一个候选都没有时不施放（spell_id = -1）。
inline StatusMaintainChoice ChooseBlindTarget(const int* enemy_durations,
    const int* enemy_hexes, const int64_t* enemy_threat, int enemy_count,
    int refresh_turns)
{
    StatusMaintainChoice choice = {-1, -1, 0};
    const int idx = ChooseEnemySingleStatusTarget(enemy_durations, enemy_hexes,
        enemy_threat, enemy_count, refresh_turns, -1);
    if (idx < 0) return choice;
    choice.spell_id = kBlindSpellId;
    choice.target_hex = enemy_hexes[idx];
    choice.mass = 0; // 单体：必须带目标格
    return choice;
}

inline bool IsWarMachineType(int creature_type)
{
    return creature_type == CREATURE_CATAPULT
        || creature_type == CREATURE_BALLISTA
        || creature_type == CREATURE_FIRST_AID_TENT
        || creature_type == CREATURE_AMMO_CART
        || creature_type == CREATURE_ARROW_TOWER;
}

// 配置动作失败时，是否可把当前战争机器交给玩家操作。
// 普通部队始终可交；战争机器须具备对应技能。无技能时由集成层保持
// 原有分支，不在纯策略层推断后续行为。
inline bool CanYieldFailedActionToPlayer(int creature_type,
    bool has_ballistics, bool has_artillery, bool has_first_aid)
{
    switch (creature_type) {
    case CREATURE_CATAPULT:
        return has_ballistics;
    case CREATURE_BALLISTA:
    case CREATURE_ARROW_TOWER:
        return has_artillery;
    case CREATURE_FIRST_AID_TENT:
        return has_first_aid;
    case CREATURE_AMMO_CART:
        return false;
    default:
        return true;
    }
}

enum StableStackIdentityKind : uint8_t {
    STACK_ID_NONE = 0,
    STACK_ID_ARMY_SLOT,
    STACK_ID_WAR_MACHINE,
    // 召唤物/克隆：仅本场内有效，重打/跨场不迁移（remap 视为不可匹配）。
    STACK_ID_SUMMON,
};

struct StableStackIdentity {
    StableStackIdentityKind kind;
    int8_t side;
    int16_t value;
    int8_t occurrence;
};

// Original army stacks keep source_army_slot (0..6) across a quick-battle
// retry. War machines do not have one, so identify them by kind and ordinal.
// Summons and clones only carry a within-battle identity (type + ordinal);
// BuildStableStackSlotRemap refuses to carry their rules across a retry.
inline StableStackIdentity MakeStableStackIdentity(int side,
    int source_army_slot, int creature_type, int occurrence)
{
    StableStackIdentity id = {};
    id.side = static_cast<int8_t>(side);
    if (side < 0 || side > 1) return id;
    if (source_army_slot >= 0 && source_army_slot < 7) {
        id.kind = STACK_ID_ARMY_SLOT;
        id.value = static_cast<int16_t>(source_army_slot);
        return id;
    }
    if (IsWarMachineType(creature_type)) {
        id.kind = STACK_ID_WAR_MACHINE;
        id.value = static_cast<int16_t>(creature_type);
        id.occurrence = static_cast<int8_t>(occurrence < 0 ? 0 : occurrence);
        return id;
    }
    if (creature_type >= 0) {
        id.kind = STACK_ID_SUMMON;
        id.value = static_cast<int16_t>(creature_type);
        id.occurrence = static_cast<int8_t>(occurrence < 0 ? 0 : occurrence);
    }
    return id;
}

inline bool StableStackIdentityEquals(const StableStackIdentity& a,
    const StableStackIdentity& b)
{
    if (a.kind == STACK_ID_NONE || b.kind == STACK_ID_NONE) return false;
    return a.kind == b.kind && a.side == b.side && a.value == b.value
        && a.occurrence == b.occurrence;
}

// ======================================================================
// 战斗内容指纹（§12：同一场战斗的稳定标识）
//
// 目标：重打 / 战斗中读档 S&L / 关游戏再开，同一场战斗得到同一个值；
// 不同战斗（不同敌情或不同触发点）得到不同值。喂入的必须全部是
// 「开局态」数据（重打与 S&L 后均回到同一快照）：
//   - 双方各 ≤21 槽 (type, numberAtStart)，按 (type,count) 升序排序后
//     喂入——与内部槽位顺序、镜像无关；侧序号参与，攻守互换算不同战斗。
//   - 双方英雄 id（无英雄 = -1；不含技能/法术/军队——军队已在槽表内）。
//   - 战场地形、攻城类型。
//   - 冒险地图触发点坐标 (x,y,z)：z 区分地上/地下；取**被攻击方**位置
//     （守方英雄格 / 与攻方英雄相邻的野怪或城镇格 / 兜底攻方英雄格），
//     不用攻击发起格——同一目标从不同方向攻击应视为同一场。
// 哈希 = FNV-1a 64 逐字段滚动。碰撞（同指纹不同战斗）的后果只是把上次
// 方案带进本场（同敌情方案通常仍适用），可由下一次「确定」覆盖自愈。
struct BattleFingerprintInput {
    int side_types[2][21];    // 21 槽生物类型（空槽 0）
    int side_counts[2][21];   // 21 槽开局数量
    int hero_id[2];           // -1 = 无英雄
    int terrain;              // 战场地形
    int siege_kind;           // 0 = 野战
    int map_x, map_y, map_z;  // 冒险地图触发点（z: 0 地上 / 1 地下）
};

inline unsigned long long Fnv1a64_Mix_(unsigned long long h, int v)
{
    const unsigned long long prime = 1099511628211ULL;
    const unsigned int u = static_cast<unsigned int>(v);
    for (int i = 0; i < 4; ++i) {
        h ^= (u >> (i * 8)) & 0xFF;
        h *= prime;
    }
    return h;
}

inline unsigned long long ComputeBattleFingerprint(
    const BattleFingerprintInput& in)
{
    unsigned long long h = 1469598103934665603ULL;
    for (int side = 0; side < 2; ++side) {
        h = Fnv1a64_Mix_(h, 0x53494445 + side); // 侧标签：攻守互换 ≠ 同场
        h = Fnv1a64_Mix_(h, in.hero_id[side]);
        // 槽 (type,count) 排序后喂入：槽位顺序无关。
        int idx[21];
        int n = 0;
        for (int i = 0; i < 21; ++i) {
            if (in.side_types[side][i] > 0 && in.side_counts[side][i] > 0)
                idx[n++] = i;
        }
        for (int i = 1; i < n; ++i) { // 插入排序（n ≤ 21）
            const int cur = idx[i];
            int j = i - 1;
            while (j >= 0
                && (in.side_types[side][idx[j]] > in.side_types[side][cur]
                    || (in.side_types[side][idx[j]] == in.side_types[side][cur]
                        && in.side_counts[side][idx[j]]
                            > in.side_counts[side][cur]))) {
                idx[j + 1] = idx[j];
                --j;
            }
            idx[j + 1] = cur;
        }
        h = Fnv1a64_Mix_(h, n);
        for (int i = 0; i < n; ++i) {
            h = Fnv1a64_Mix_(h, in.side_types[side][idx[i]]);
            h = Fnv1a64_Mix_(h, in.side_counts[side][idx[i]]);
        }
    }
    h = Fnv1a64_Mix_(h, in.terrain);
    h = Fnv1a64_Mix_(h, in.siege_kind);
    h = Fnv1a64_Mix_(h, in.map_x);
    h = Fnv1a64_Mix_(h, in.map_y);
    h = Fnv1a64_Mix_(h, in.map_z);
    return h;
}


// For each current battle slot, return the previous slot holding its rule.
// Unmatched slots remain -1 and receive defaults in the integration layer.
// 召唤物/克隆没有跨重打身份：不参与重排，重打后回到默认规则。
inline void BuildStableStackSlotRemap(const StableStackIdentity* previous,
    int previous_count, const StableStackIdentity* current, int current_count,
    int* previous_slot_for_current)
{
    if (!previous_slot_for_current || current_count <= 0) return;
    for (int i = 0; i < current_count; ++i) {
        previous_slot_for_current[i] = -1;
        if (!current || current[i].kind == STACK_ID_NONE) continue;
        if (current[i].kind == STACK_ID_SUMMON) continue;
        for (int j = 0; previous && j < previous_count; ++j) {
            if (previous[j].kind == STACK_ID_SUMMON) continue;
            if (StableStackIdentityEquals(previous[j], current[i])) {
                previous_slot_for_current[i] = j;
                break;
            }
        }
    }
}

// 面板准入纯规则（含阵亡）：count_initial>0 即该槽本场存在过部队——阵亡
// （当前数量 0）的己方非召唤部队仍列入面板，规则可见可改、复活后继续生效
// （方案生效范围=面板所见部队）；未存在过的空槽不列。弹药车永不进入；
// 投石车必须有弹道术；召唤物/克隆物（稳定身份 STACK_ID_SUMMON，由集成层
// 按与 MakeStableStackIdentity 同口径判定）不进部队页——其行动只由召唤页
// 的共享规则控制，配置出口唯一（§2.6）。
inline bool IsConfigurablePanelStack(int creature_type, int count_initial,
    bool has_ballistics, bool is_summon_clone)
{
    if (count_initial <= 0) return false;
    if (is_summon_clone) return false;
    if (creature_type == CREATURE_AMMO_CART) return false;
    if (creature_type == CREATURE_CATAPULT) return has_ballistics;
    return true;
}

enum ResultLifecycleEvent : uint8_t {
    RESULT_NONE = 0,
    RESULT_SHOWN,
    RESULT_ACCEPT_CLICKED,
    RESULT_CANCEL_CLICKED,
    RESULT_CLOSED_WITH_BATTLE_UI,
    RESULT_CLOSED_WITHOUT_BATTLE_UI,
};

enum ResultLifecycleAction : uint8_t {
    RESULT_WAIT = 0,
    RESULT_KEEP_AND_REBIND,
    RESULT_CLEAR_SETTINGS,
};

struct ResultLifecycleState {
    bool saw_result;
    bool accept_armed;
    bool cancel_armed;
};

// 结果窗状态机纯函数：取消/重打只保留并重绑，接受才清除。
inline ResultLifecycleAction ApplyResultLifecycle(
    ResultLifecycleState* state, ResultLifecycleEvent event)
{
    if (!state) return RESULT_WAIT;
    if (event == RESULT_SHOWN) {
        state->saw_result = true;
        state->accept_armed = false;
        state->cancel_armed = false;
        return RESULT_WAIT;
    }
    if (!state->saw_result) return RESULT_WAIT;
    if (event == RESULT_ACCEPT_CLICKED) {
        state->accept_armed = true;
        state->cancel_armed = false;
        return RESULT_WAIT;
    }
    if (event == RESULT_CANCEL_CLICKED) {
        state->cancel_armed = true;
        state->accept_armed = false;
        return RESULT_WAIT;
    }
    if (event == RESULT_CLOSED_WITH_BATTLE_UI && state->accept_armed) {
        *state = {};
        return RESULT_CLEAR_SETTINGS;
    }
    // Returning BattleUI is the authoritative retry signal when accept was
    // not explicitly captured. Do not depend on the exact cancel mouse frame.
    if (event == RESULT_CLOSED_WITH_BATTLE_UI) {
        *state = {};
        return RESULT_KEEP_AND_REBIND;
    }
    // Cancel can close CPResult one or more frames before BattleUI reappears.
    // Keep waiting instead of clearing profiles during that transition.
    if (event == RESULT_CLOSED_WITHOUT_BATTLE_UI && state->cancel_armed)
        return RESULT_WAIT;
    if (event == RESULT_CLOSED_WITHOUT_BATTLE_UI) {
        *state = {};
        return RESULT_CLEAR_SETTINGS;
    }
    return RESULT_WAIT;
}

inline int GetAllowedActions(int creature_type, bool is_ranged,
    bool has_artillery, bool has_first_aid, AutoActionKind out_actions[AA_COUNT])
{
    int n = 0;
    auto push = [&](AutoActionKind action) {
        if (n < AA_COUNT) out_actions[n++] = action;
    };

    switch (creature_type) {
    case CREATURE_FIRST_AID_TENT:
        if (has_first_aid) push(AA_MANUAL);
        push(AA_FIRST_AID);
        break;
    case CREATURE_CATAPULT:
        push(AA_MANUAL);
        push(AA_DEFEND);
        break;
    case CREATURE_BALLISTA:
    case CREATURE_ARROW_TOWER:
        if (has_artillery) push(AA_MANUAL);
        push(AA_RANGED_ATTACK);
        break;
    case CREATURE_AMMO_CART:
        push(AA_MANUAL);
        break;
    default:
        push(AA_MANUAL);
        push(AA_DEFEND);
        push(AA_MOVE);
        push(AA_MELEE_ATTACK);
        // 不反兵攻城：普通部队都能选（打法是玩家自己按不反兵部队去用）。
        push(AA_SIEGE);
        if (is_ranged) push(AA_RANGED_ATTACK);
        break;
    }
    return n;
}

inline bool ActionNeedsTarget(AutoActionKind action)
{
    return action == AA_MOVE || action == AA_MELEE_ATTACK
        || action == AA_RANGED_ATTACK || action == AA_FIRST_AID
        || action == AA_SIEGE; // 需要第三列画「安全位置」槽
}

inline bool ActionUsesTwoHex(AutoActionKind action)
{
    return action == AA_MELEE_ATTACK;
}

inline bool ActionShowsFallback(int creature_type, AutoActionKind action)
{
    if (IsWarMachineType(creature_type)) return false;
    // 召唤动作：随机移动可配「允许降级为防御」；散开的降级链固定终于防御，不显示。
    if (action == AA_RANDOM_MOVE) return true;
    if (action == AA_SCATTER) return false;
    return action != AA_MANUAL && action != AA_DEFEND && action != AA_WAIT;
}

inline AutoTargetRule DefaultTargetForAction(AutoActionKind action)
{
    AutoTargetRule t = {};
    t.meleeStandHex = -1;
    t.meleeAttackHex = -1;
    t.siegeSafeHex = -1; // ={} 只会清零；未设置必须显式 -1（0 不是合法格）
    for (int i = 0; i < MOVE_WAYPOINT_CAPACITY; ++i)
        t.moveWaypoints[i] = -1;
    for (int i = 0; i < MELEE_PAIR_CAPACITY; ++i) {
        t.meleeStandHexes[i] = -1;
        t.meleeAttackHexes[i] = -1;
    }

    switch (action) {
    case AA_MOVE:
    case AA_MELEE_ATTACK:
        t.kind = AT_POSITION;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    case AA_RANGED_ATTACK:
        t.kind = AT_STACK;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    // 不反兵攻城：入城打谁由插件自动挑，规则固定用「远程飞兵高速优先」
    // （2026-10-14 用户裁定：使用远程攻击类的该条规则）。
    case AA_SIEGE:
        t.kind = AT_STACK;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANGED_SPEED;
        break;
    case AA_FIRST_AID:
        t.kind = AT_STACK;
        t.side = ATS_OWN;
        t.selector = SEL_WOUND_VALUE;
        break;
    default:
        t.kind = AT_NONE;
        t.side = ATS_ENEMY;
        t.selector = SEL_RANDOM;
        break;
    }
    return t;
}

struct TargetCandidate {
    int count_current;
    int count_at_start;
    int hit_points;
    int lost_hp;
    int shots;
    int speed;
    int flyer;
    int ranged; // 生物类型固有远程标志（H3CreatureInformation.shooter），与当前弹药无关
};

inline int StackRemainingHp(const TargetCandidate& candidate)
{
    const int hp = candidate.hit_points > 0 ? candidate.hit_points : 1;
    const int alive = candidate.count_current > 0 ? candidate.count_current : 0;
    int64_t total = static_cast<int64_t>(alive) * hp;
    const int lost = candidate.lost_hp > 0 ? candidate.lost_hp : 0;
    if (lost > 0 && total > lost) total -= lost;
    else if (lost > 0) total = 0;
    if (total > 0x7FFFFFFF) total = 0x7FFFFFFF;
    return static_cast<int>(total);
}

// 保活复活（§3.1.1）纯判定逻辑。
// 可恢复量 = 法术表 baseValue[等级] + 力量 × spEffect（H3Spell::GetBaseEffect）。
// 读不到法术表时退回旧估算：基础 50×力量 / 高级 75×力量 / 专家 100×力量。
inline int ResurrectionRestoreHp(int expertise, int spell_power)
{
    if (spell_power <= 0) return 0;
    int base = 0;
    if (expertise == 3) base = 100;
    else if (expertise == 2) base = 75;
    else if (expertise == 0 || expertise == 1) base = 50;
    else return 0;
    return base * spell_power;
}

inline int ResurrectionRestoreHp(int base_value, int sp_effect,
    int expertise, int spell_power)
{
    if (expertise < 0 || expertise > 3 || spell_power <= 0) return 0;
    if (base_value < 0 || sp_effect <= 0)
        return ResurrectionRestoreHp(expertise, spell_power);
    const int64_t total = static_cast<int64_t>(base_value)
        + static_cast<int64_t>(spell_power) * sp_effect;
    if (total <= 0) return ResurrectionRestoreHp(expertise, spell_power);
    if (total > 0x7FFFFFFF) return 0x7FFFFFFF;
    return static_cast<int>(total);
}

// 保活与召唤是同一条施法通道，没有方案级通道选择：每次行动（还有施法
// 次数时）先按各队的 ProtectMode 判保活（§3.1.1，默认不保活），
// 没人要救且召唤已启用（SummonProfileFields.enabled）再判召唤（§3.7）。

// 是否够格：无损失不触发；按该队自己的保活方式判定。
// PM_COUNT_BELOW 要求剩余数量 ≤ 该队阈值（全灭数量 0 天然满足，0=只救全灭）；
// PM_LOSS_GT_RESTORE 要求已损 HP 严格大于一次可恢复量；PM_NONE 永不触发。
inline bool ProtectShouldCast(ProtectMode mode,
    int restorable_hp, int wound_value,
    int count_current, int count_below)
{
    if (mode == PM_NONE) return false;
    if (wound_value <= 0) return false;
    switch (mode) {
    case PM_LOSS_GT_RESTORE: return restorable_hp > 0
        && wound_value > restorable_hp;
    case PM_COUNT_BELOW:     return count_current <= count_below;
    default:                 return false;
    }
}

inline int WoundValue(const TargetCandidate& candidate)
{
    const int hp = candidate.hit_points > 0 ? candidate.hit_points : 1;
    const int dead = candidate.count_at_start > candidate.count_current
        ? candidate.count_at_start - candidate.count_current : 0;
    const int lost = candidate.lost_hp > 0 ? candidate.lost_hp : 0;
    return dead * hp + lost;
}

inline int WoundRatioKey(const TargetCandidate& candidate)
{
    const int hp = candidate.hit_points > 0 ? candidate.hit_points : 1;
    const int start = candidate.count_at_start > 0
        ? candidate.count_at_start : candidate.count_current;
    const int total = start * hp;
    if (total <= 0) return 0;
    return static_cast<int>((static_cast<int64_t>(WoundValue(candidate)) * 10000) / total);
}

// 够格者中选目标下标：血量最低（全灭者剩余 0 天然最前）；
// 平分保留先出现者。返回 -1 = 无合适目标。
inline int SelectProtectTargetIndex(const TargetCandidate* candidates, int count)
{
    if (!candidates || count <= 0) return -1;
    int best = 0;
    for (int i = 1; i < count; ++i)
        if (StackRemainingHp(candidates[i]) < StackRemainingHp(candidates[best]))
            best = i;
    return best;
}

// 自动停止（§3.1.2）。按本场掉血外推敌方还要几回合全灭。
// 返回值 < 0 表示现在不能停：阈值为 0、还没有基线、没掉过血。
// 否则返回预计剩余回合（向上取整）。
inline int ProjectEnemyTurnsLeft(int threshold, int baseline_hp,
    int current_hp, int elapsed_turns)
{
    if (threshold <= 0) return -1;
    if (baseline_hp <= 0 || current_hp <= 0 || elapsed_turns <= 0) return -1;
    const int damage = baseline_hp - current_hp;
    if (damage <= 0) return -1;
    const int64_t product = static_cast<int64_t>(current_hp) * elapsed_turns;
    return static_cast<int>((product + damage - 1) / damage);
}

inline bool AutoStopShouldYield(int threshold, int baseline_hp,
    int current_hp, int elapsed_turns)
{
    const int left = ProjectEnemyTurnsLeft(threshold, baseline_hp,
        current_hp, elapsed_turns);
    return left >= 0 && left <= threshold;
}

// ---------------------------------------------------------------------------
// 召唤通道（保活复活的兜底：无人可救且已启用时）与召唤物行动的纯判定。
// ---------------------------------------------------------------------------

// 自动停止第二条件：敌方英雄法力耗尽前停手。
// 无英雄/无魔法书不触发（无书英雄法力常为 0，不能开战即误停）。
inline bool ShouldStopOnEnemyMana(int stop_flag, bool has_enemy_hero,
    bool has_spellbook, int enemy_mana, int threshold)
{
    if (!stop_flag) return false;
    if (!has_enemy_hero || !has_spellbook) return false;
    if (enemy_mana < 0) return false;
    return enemy_mana <= threshold;
}

// 队数/血量两条件的组合方式：0=和（两者都满足才召，默认）；1=或（任一满足即召）。
static constexpr int SUMMON_COMBINE_AND = 0;
static constexpr int SUMMON_COMBINE_OR  = 1;

// 召唤时机：己方存活队数（含召唤物、不含战争机器）< 阈值（严格小于）、
// 剩余血量合计（同口径）≤ 阈值；组合方式 cond_combine：和=都满足、或=任一。
// 本回合未施法、已学、法力够。
inline bool SummonShouldCast(int stack_count, int count_th, int hp_total,
    int hp_th, int cond_combine, int mana, int mana_cost, bool hero_casted,
    bool learned)
{
    if (hero_casted) return false;
    if (!learned) return false;
    if (mana_cost > 0 && mana < mana_cost) return false;
    const bool count_ok = stack_count < count_th;
    const bool hp_ok = hp_total <= hp_th;
    return cond_combine == SUMMON_COMBINE_OR ? (count_ok || hp_ok)
                                             : (count_ok && hp_ok);
}

inline bool IsSummonedElemental(int creature_id)
{
    for (int i = 0; i < SUMMON_ELEMENT_COUNT; ++i)
        if (creature_id == kSummonCreatureIds[i]) return true;
    return false;
}

// 选召唤法术。amounts[i]/learned[i]：各元素本次召唤总量（调用方按等级×力量×生物算好）
// 与是否已学；spell_pick：0=自动，1..4=固定元素；locked_index：本场锁定（-1 无）。
// 返回元素下标 0..3（对应 kSummonSpellIds/kSummonCreatureIds），-1 = 不召。
// 固定未学 → 不召；自动有锁定 → 用锁定（锁定元素已不可施放时也不召）；
// 否则取总量最大（平手取靠前，稳定可测）。游戏规则：本场召唤过一种元素后
// 只能继续召唤该种（2026-10-11 用户裁定：玩家手动召唤同样构成锁定）。
inline int PickSummonSpell(const int amounts[SUMMON_ELEMENT_COUNT],
    const bool learned[SUMMON_ELEMENT_COUNT], int spell_pick, int locked_index)
{
    if (spell_pick >= 1 && spell_pick <= SUMMON_ELEMENT_COUNT) {
        const int fixed = spell_pick - 1;
        return learned[fixed] ? fixed : -1;
    }
    if (locked_index >= 0 && locked_index < SUMMON_ELEMENT_COUNT)
        return learned[locked_index] ? locked_index : -1;
    int best = -1;
    for (int i = 0; i < SUMMON_ELEMENT_COUNT; ++i) {
        if (!learned[i]) continue;
        if (best < 0 || amounts[i] > amounts[best]) best = i;
    }
    return best;
}

// 己方存活侧统计（调用方先过滤：只传存活槽、剔除战争机器）。
inline int CountAliveSideStacks(const TargetCandidate* stacks, int count)
{
    int alive = 0;
    for (int i = 0; i < count; ++i)
        if (stacks[i].count_current > 0) ++alive;
    return alive;
}

inline int SumSideRemainingHp(const TargetCandidate* stacks, int count)
{
    int64_t total = 0;
    for (int i = 0; i < count; ++i) {
        if (stacks[i].count_current <= 0) continue;
        total += StackRemainingHp(stacks[i]);
        if (total > 0x7FFFFFFF) return 0x7FFFFFFF;
    }
    return static_cast<int>(total);
}

// 战场六格距离（15×11，odd-r：奇数行右移半格；转立方坐标取三轴最大差）。
// 与原版距离的对拍列入实施文档 §5 上机验证。
inline int HexCoordDistance(int hex_a, int hex_b)
{
    if (hex_a < 0 || hex_b < 0
        || hex_a >= BATTLEFIELD_HEXES || hex_b >= BATTLEFIELD_HEXES)
        return 0x7FFFFFFF;
    const int x1 = hex_a % BATTLEFIELD_COLS, y1 = hex_a / BATTLEFIELD_COLS;
    const int x2 = hex_b % BATTLEFIELD_COLS, y2 = hex_b / BATTLEFIELD_COLS;
    const int cx1 = x1 - ((y1 - (y1 & 1)) >> 1);
    const int cx2 = x2 - ((y2 - (y2 & 1)) >> 1);
    const int dx = cx2 - cx1;
    const int dz = y2 - y1;
    const int dy = -dx - dz;
    int d = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    const int az = dz < 0 ? -dz : dz;
    if (ay > d) d = ay;
    if (az > d) d = az;
    return d;
}

// 召唤物移动目标格。candidates：可达格；own_positions：己方其它存活部队格
// （不含自身，含战争机器）。返回目标格；-1 = 原地（不动/防御）。
// - AA_SCATTER 分级满足：先「与所有己方部队距离 ≥2」，降「≥1」，两档均无则原地；
//   当前格已满足该档则不移动；该档多格取离部队最远者（平手取候选序靠前者，稳定可测）。
// - AA_RANDOM_MOVE：排除当前格后均匀随机（rng 由调用方传入）。
inline int ChooseSummonMoveHex(AutoActionKind kind, const int* candidates,
    int cand_count, int cur_hex, const int* own_positions, int own_count,
    uint32_t rng)
{
    if (kind == AA_RANDOM_MOVE) {
        int pool[BATTLEFIELD_HEXES] = {};
        int n = 0;
        for (int i = 0; i < cand_count && n < BATTLEFIELD_HEXES; ++i) {
            const int hex = candidates[i];
            if (hex >= 0 && hex < BATTLEFIELD_HEXES && hex != cur_hex)
                pool[n++] = hex;
        }
        if (n <= 0) return -1;
        return pool[rng % static_cast<uint32_t>(n)];
    }
    if (kind != AA_SCATTER) return -1;
    auto min_dist = [&](int hex) -> int {
        int m = 0x7FFFFFFF;
        for (int i = 0; i < own_count; ++i) {
            const int d = HexCoordDistance(hex, own_positions[i]);
            if (d < m) m = d;
        }
        return m;
    };
    for (int tier = 2; tier >= 1; --tier) {
        // 当前格已满足该档：不动（原地防御由调用方提交）。
        if (min_dist(cur_hex) >= tier) return -1;
        int best = -1, best_d = -1;
        for (int i = 0; i < cand_count; ++i) {
            const int hex = candidates[i];
            if (hex < 0 || hex >= BATTLEFIELD_HEXES || hex == cur_hex) continue;
            const int d = min_dist(hex);
            if (d >= tier && d > best_d) {
                best = hex;
                best_d = d;
            }
        }
        if (best >= 0) return best;
    }
    return -1; // 两档都做不到：原地防御
}

// 召唤物共享规则的行动集（专用，不套用普通部队）。
inline int GetAllowedSummonActions(AutoActionKind out_actions[4])
{
    if (!out_actions) return 0;
    out_actions[0] = AA_MANUAL;
    out_actions[1] = AA_DEFEND;
    out_actions[2] = AA_SCATTER;
    out_actions[3] = AA_RANDOM_MOVE;
    return 4;
}

// 召唤共享规则规范化：行动裁剪到 4 动作集；无目标/施法/保活；
// 「允许降级为防御」仅随机移动保留。
inline void NormalizeSummonRule(AutoStackRule* rule)
{
    if (!rule) return;
    bool action_ok = false;
    AutoActionKind allowed[4] = {};
    const int n = GetAllowedSummonActions(allowed);
    for (int i = 0; i < n; ++i)
        if (allowed[i] == rule->action) action_ok = true;
    if (!action_ok) rule->action = AA_MANUAL;
    rule->target = DefaultTargetForAction(AA_MANUAL);
    rule->allowDefendFallback =
        (rule->action == AA_RANDOM_MOVE) && rule->allowDefendFallback;
    rule->protectMode = PM_NONE;   // 召唤物共享规则不走保活
    rule->protectCountBelow = 2;
    rule->waitMode = AW_NONE;      // 召唤物共享规则不走等待方式
}

// 返回候选下标；平分时保留先出现者，与原执行器行为一致。
// random_value 由生产侧传入 rand()，测试侧可传固定值。
inline int SelectTargetIndex(const TargetCandidate* candidates, int count,
    AutoTargetSelector selector, uint32_t random_value = 0)
{
    if (!candidates || count <= 0) return -1;
    if (selector == SEL_RANDOM)
        return static_cast<int>(random_value % static_cast<uint32_t>(count));

    int best = 0;
    for (int i = 1; i < count; ++i) {
        bool better = false;
        switch (selector) {
        case SEL_COUNT_HIGH:
            better = candidates[i].count_current > candidates[best].count_current;
            break;
        case SEL_RANGED_SPEED: {
            // 远程按生物类型固有标志，不用当前弹药（shots 弹药打光即归零）。
            const int current_ranged = candidates[i].ranged > 0 ? 1 : 0;
            const int best_ranged = candidates[best].ranged > 0 ? 1 : 0;
            const int current_flyer = candidates[i].flyer > 0 ? 1 : 0;
            const int best_flyer = candidates[best].flyer > 0 ? 1 : 0;
            // 远程 > 飞行 > 速度；同类同速时优先打剩余总血量更高的那队。
            better = current_ranged > best_ranged
                || (current_ranged == best_ranged && current_flyer > best_flyer)
                || (current_ranged == best_ranged && current_flyer == best_flyer
                    && candidates[i].speed > candidates[best].speed)
                || (current_ranged == best_ranged && current_flyer == best_flyer
                    && candidates[i].speed == candidates[best].speed
                    && StackRemainingHp(candidates[i]) > StackRemainingHp(candidates[best]));
            break;
        }
        case SEL_WOUND_VALUE:
            better = WoundValue(candidates[i]) > WoundValue(candidates[best]);
            break;
        case SEL_WOUND_RATIO:
            better = WoundRatioKey(candidates[i]) > WoundRatioKey(candidates[best]);
            break;
        default:
            return static_cast<int>(random_value % static_cast<uint32_t>(count));
        }
        if (better) best = i;
    }
    return best;
}

// 方案存档：一套方案一个 JSON 对象，字段分开存，数组按实际数量存。
// 不再使用 H3AP<N> 数字串。旧数字串一律拒绝，视为没有存档。
static constexpr int PROFILE_STORE_SLOTS = 21;
static constexpr int DEFAULT_STOP_TURNS = 4;

// 方案级召唤配置（随战斗 JSON 方案存档）。保活与召唤同一条施法通道：
// 保活优先，无人可救且 enabled 时才走召唤。
struct SummonProfileFields {
    int enabled;          // 是否启用自动召唤（0/1，默认 0）：复活无人可救时的兜底
    int count_th;         // 队数阈值，默认 2，2..21（存活队数严格小于此值才触发）
    int hp_th;            // 血量阈值，默认 750，≥0（口径同自动停止）
    int spell_pick;       // 法术选择：0=自动；1..SUMMON_ELEMENT_COUNT=固定元素
    int cond_combine;     // 队数/血量两条件组合：0=和（默认）；1=或
    int stop_enemy_mana;  // 自动停止第二条件勾选（0/1）
    int stop_mana_th;     // 敌方魔力阈值，默认 6，0..32767（勾选时生效）
    AutoStackRule summon_rule; // 召唤物共享行动规则
};

inline SummonProfileFields MakeDefaultSummonFields()
{
    SummonProfileFields fields = {};
    fields.enabled = 0;   // 默认不召唤：只保活复活
    fields.count_th = 2;
    fields.hp_th = 750;
    fields.spell_pick = 0;
    fields.cond_combine = SUMMON_COMBINE_AND; // 默认「和」：两条件都满足才召
    fields.stop_enemy_mana = 0; // 默认不勾（2026-10-11 用户裁定）；勾选后敌方魔力≤阈值即停
    fields.stop_mana_th = kSummonStopManaDefault;
    fields.summon_rule = MakeDefaultRule();
    fields.summon_rule.action = AA_SCATTER; // 召唤物默认散开（2026-10-11 用户裁定，原防御）
    return fields;
}

// 力盾随战斗 JSON 存档。
struct ForceFieldProfileFields {
    int anchor_hex[2];
};

inline ForceFieldProfileFields MakeDefaultForceFieldFields()
{
    ForceFieldProfileFields fields = {{-1, -1}};
    return fields;
}

inline bool IsValidForceFieldAnchor(int anchor_hex)
{
    return anchor_hex >= 1 && anchor_hex <= 185
        && anchor_hex % 17 != 0 && anchor_hex % 17 != 16;
}

inline void NormalizeForceFieldFields(ForceFieldProfileFields* fields)
{
    if (!fields) return;
    for (int i = 0; i < 2; ++i)
        if (!IsValidForceFieldAnchor(fields->anchor_hex[i])) fields->anchor_hex[i] = -1;
    if (fields->anchor_hex[0] == fields->anchor_hex[1]) fields->anchor_hex[1] = -1;
    if (fields->anchor_hex[0] == -1 && fields->anchor_hex[1] != -1) {
        fields->anchor_hex[0] = fields->anchor_hex[1];
        fields->anchor_hex[1] = -1;
    }
}

enum ForceFieldPresence {
    FF_UNKNOWN = -1,
    FF_ABSENT = 0,
    FF_PRESENT = 1,
};

inline int SelectForceFieldAnchor(const ForceFieldProfileFields& fields,
    const ForceFieldPresence presence[2])
{
    for (int i = 0; i < 2; ++i)
        if (IsValidForceFieldAnchor(fields.anchor_hex[i]) && presence[i] != FF_PRESENT)
            return fields.anchor_hex[i];
    return -1;
}

// ======================================================================
// 战斗存档记录（§17 智能存读档）：一场战斗一条 = 时间戳 + 激活方案 +
// 5 套完整方案。存档文件 <指纹>.json 按时间序保存 ≤30 条（集成层
// ConfigLog 负责 JSON 编解码与落盘；结构与内容比较是纯函数，在此可测）。
// ======================================================================
struct BattleStoreRecord {
    char time[20];                  // "yyyymmdd-hhmmss"
    int  active;                    // 0..4
    AutoStackRule rules[5][21];
    uint16_t stop_turns[5];
    SummonProfileFields summon[5];
    StatusProfileFields status[5];
    SpellCastTimingFields cast_timing[5];
    ForceFieldProfileFields forcefield[5];
};

// 两条记录内容是否完全相同（忽略时间戳）：「确定」时与文件里最后一
// 条比对，相同则不新增存档。
inline bool BattleStoreRecordContentEquals(const BattleStoreRecord& a,
    const BattleStoreRecord& b)
{
    // 字节级手写比较（本文件不依赖 <cstring>）。
    auto bytes_equal = [](const void* p, const void* q, int n) -> bool {
        const unsigned char* x = static_cast<const unsigned char*>(p);
        const unsigned char* y = static_cast<const unsigned char*>(q);
        for (int i = 0; i < n; ++i)
            if (x[i] != y[i]) return false;
        return true;
    };
    if (a.active != b.active) return false;
    for (int p = 0; p < 5; ++p) {
        if (a.stop_turns[p] != b.stop_turns[p]) return false;
        if (!bytes_equal(&a.summon[p], &b.summon[p], sizeof(SummonProfileFields)))
            return false;
        if (!bytes_equal(&a.status[p], &b.status[p], sizeof(StatusProfileFields)))
            return false;
        if (!bytes_equal(&a.cast_timing[p], &b.cast_timing[p], sizeof(SpellCastTimingFields)))
            return false;
        for (int i = 0; i < 2; ++i)
            if (a.forcefield[p].anchor_hex[i] != b.forcefield[p].anchor_hex[i])
                return false;
        if (!bytes_equal(a.rules[p], b.rules[p], sizeof(a.rules[p])))
            return false;
    }
    return true;
}

// 时间戳格式校验：15 字符 yyyymmdd-hhmmss，除第 8 位为 '-' 外全数字，
// 且月份 01..12、日 01..31、时分秒 ≤ 59 的粗校验。
inline bool BattleStoreStampValid(const char* s)
{
    if (!s) return false;
    for (int i = 0; i < 15; ++i) {
        if (!s[i]) return false;
        if (i == 8) {
            if (s[i] != '-') return false;
        } else if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    if (s[15] != 0) return false;
    const int mon  = (s[4] - '0') * 10 + (s[5] - '0');
    const int day  = (s[6] - '0') * 10 + (s[7] - '0');
    const int hour = (s[9] - '0') * 10 + (s[10] - '0');
    const int min  = (s[11] - '0') * 10 + (s[12] - '0');
    const int sec  = (s[13] - '0') * 10 + (s[14] - '0');
    return mon >= 1 && mon <= 12 && day >= 1 && day <= 31 && hour <= 23
        && min <= 59 && sec <= 59;
}


// 读档四轮关联（存档部队 → 当前部队槽）：
//   轮 1：槽位 + 生物类型 + 初始数量（三项全等，零误配）
//   轮 2：生物类型 + 初始数量（换了槽、规模没变；数量相等天然消歧）
//   轮 3：槽位 + 生物类型（没换槽、规模变了；同类型多组时降级跳过）
//   轮 4：只按生物类型（兜底，先到先得）
// 每轮跳过两边已被匹配的槽位；arch_for_cur[cur] = 存档槽号，-1 = 未匹配。
// 空槽（类型 < 0）不参与。未匹配的存档规则由调用方丢弃。
inline void BuildArchiveSlotMapByRounds(const int* arch_type,
    const int* arch_count, const int* cur_type, const int* cur_count,
    int* arch_for_cur)
{
    if (!arch_type || !arch_count || !cur_type || !cur_count || !arch_for_cur)
        return;
    bool cur_used[PROFILE_STORE_SLOTS] = {};
    bool arch_used[PROFILE_STORE_SLOTS] = {};
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i)
        arch_for_cur[i] = -1;

    // 某类型在数组中的出现次数（仅有效槽）。O(21^2)，规模固定无所谓。
    auto count_type = [](const int* types, int t) -> int {
        int n = 0;
        for (int i = 0; i < PROFILE_STORE_SLOTS; ++i)
            if (types[i] == t) ++n;
        return n;
    };

    // 轮 1：槽位 + 生物类型 + 初始数量。
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i) {
        if (cur_used[i] || arch_used[i]) continue;
        if (cur_type[i] < 0 || arch_type[i] < 0) continue;
        if (cur_type[i] == arch_type[i]
            && cur_count[i] == arch_count[i]) {
            arch_for_cur[i] = i;
            cur_used[i] = true;
            arch_used[i] = true;
        }
    }
    // 轮 2：生物类型 + 初始数量（当前槽升序，存档槽取第一个命中）。
    for (int c = 0; c < PROFILE_STORE_SLOTS; ++c) {
        if (cur_used[c] || cur_type[c] < 0) continue;
        for (int a = 0; a < PROFILE_STORE_SLOTS; ++a) {
            if (arch_used[a] || arch_type[a] < 0) continue;
            if (arch_type[a] == cur_type[c]
                && arch_count[a] == cur_count[c]) {
                arch_for_cur[c] = a;
                cur_used[c] = true;
                arch_used[a] = true;
                break;
            }
        }
    }
    // 轮 3：槽位 + 生物类型。同类型在任一边出现多组时降级跳过
    // （此时槽位是仅剩信号但不可靠，交给轮 4 按类型先到先得）。
    for (int i = 0; i < PROFILE_STORE_SLOTS; ++i) {
        if (cur_used[i] || arch_used[i]) continue;
        if (cur_type[i] < 0 || arch_type[i] < 0) continue;
        if (cur_type[i] != arch_type[i]) continue;
        if (count_type(cur_type, cur_type[i]) > 1
            || count_type(arch_type, arch_type[i]) > 1)
            continue;
        arch_for_cur[i] = i;
        cur_used[i] = true;
        arch_used[i] = true;
    }
    // 轮 4：只按生物类型（先到先得）。
    for (int c = 0; c < PROFILE_STORE_SLOTS; ++c) {
        if (cur_used[c] || cur_type[c] < 0) continue;
        for (int a = 0; a < PROFILE_STORE_SLOTS; ++a) {
            if (arch_used[a] || arch_type[a] < 0) continue;
            if (arch_type[a] == cur_type[c]) {
                arch_for_cur[c] = a;
                cur_used[c] = true;
                arch_used[a] = true;
                break;
            }
        }
    }
}

inline int GetAllowedSelectors(AutoActionKind action, AutoTargetKind /*kind*/,
    AutoTargetSelector out_selectors[SEL_COUNT])
{
    int n = 0;
    auto push = [&](AutoTargetSelector selector) {
        if (n < SEL_COUNT) out_selectors[n++] = selector;
    };
    if (ActionUsesTwoHex(action)) return 0;
    if (action == AA_FIRST_AID) {
        push(SEL_WOUND_VALUE);
        push(SEL_WOUND_RATIO);
        push(SEL_RANDOM);
    } else {
        push(SEL_RANDOM);
        push(SEL_RANGED_SPEED);
        push(SEL_COUNT_HIGH);
    }
    return n;
}

// 纯策略部分的规范化；近战槽位/移动坐标的几何校验仍由 CellControl 负责。
inline void NormalizeRule(AutoStackRule* rule, int creature_type, bool is_ranged,
    bool has_artillery, bool has_first_aid)
{
    if (!rule) return;
    if (rule->protectCountBelow < 0) rule->protectCountBelow = 0;
    if (rule->protectMode >= PM_COUNT) rule->protectMode = PM_NONE;
    if (rule->waitMode >= AW_COUNT) rule->waitMode = AW_NONE;

    AutoActionKind allowed[AA_COUNT] = {};
    const int n = GetAllowedActions(creature_type, is_ranged,
        has_artillery, has_first_aid, allowed);
    bool action_ok = false;
    for (int i = 0; i < n; ++i)
        if (allowed[i] == rule->action) action_ok = true;
    if (!action_ok) rule->action = n > 0 ? allowed[0] : AA_MANUAL;

    if (!ActionNeedsTarget(rule->action)) {
        rule->target = DefaultTargetForAction(AA_MANUAL);
        rule->allowDefendFallback = false;
        return;
    }

    if (rule->target.kind == AT_NONE)
        rule->target = DefaultTargetForAction(rule->action);
    switch (rule->action) {
    case AA_MELEE_ATTACK:
        rule->target.kind = AT_POSITION;
        rule->target.side = ATS_ENEMY;
        rule->target.selector = SEL_RANDOM;
        break;
    case AA_RANGED_ATTACK:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_ENEMY;
        break;
    case AA_FIRST_AID:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_OWN;
        break;
    case AA_MOVE:
        rule->target.kind = AT_POSITION;
        rule->target.selector = SEL_RANDOM;
        break;
    // 不反兵攻城：目标固定按「远程飞兵高速优先」自动挑，玩家只配安全位置。
    case AA_SIEGE:
        rule->target.kind = AT_STACK;
        rule->target.side = ATS_ENEMY;
        rule->target.selector = SEL_RANGED_SPEED;
        if (rule->target.siegeSafeHex < 1 || rule->target.siegeSafeHex > 185)
            rule->target.siegeSafeHex = -1;
        break;
    default:
        break;
    }
    // 其它动作不留安全位置残值（换动作时由面板重新默认目标）。
    if (rule->action != AA_SIEGE) rule->target.siegeSafeHex = -1;

    if (!ActionUsesTwoHex(rule->action)) {
        AutoTargetSelector selectors[SEL_COUNT] = {};
        const int selector_count = GetAllowedSelectors(rule->action,
            rule->target.kind, selectors);
        bool selector_ok = false;
        for (int i = 0; i < selector_count; ++i)
            if (selectors[i] == rule->target.selector) selector_ok = true;
        if (!selector_ok && selector_count > 0)
            rule->target.selector = selectors[0];
    }
    if (!ActionShowsFallback(creature_type, rule->action))
        rule->allowDefendFallback = false;
}

} // namespace H3AutoPolicy

// 保持现有生产代码的未限定名称，避免一次性改动全部模块。
using H3AutoPolicy::AutoActionKind;
using H3AutoPolicy::AutoTargetKind;
using H3AutoPolicy::AutoTargetSide;
using H3AutoPolicy::AutoTargetSelector;
using H3AutoPolicy::AutoTargetRule;
using H3AutoPolicy::AutoStackRule;
using H3AutoPolicy::SummonProfileFields;
using H3AutoPolicy::MakeDefaultSummonFields;
using H3AutoPolicy::ForceFieldProfileFields;
using H3AutoPolicy::MakeDefaultForceFieldFields;
using H3AutoPolicy::IsValidForceFieldAnchor;
using H3AutoPolicy::NormalizeForceFieldFields;
using H3AutoPolicy::ForceFieldPresence;
using H3AutoPolicy::FF_UNKNOWN;
using H3AutoPolicy::FF_ABSENT;
using H3AutoPolicy::FF_PRESENT;
using H3AutoPolicy::SelectForceFieldAnchor;
using H3AutoPolicy::BattleStoreRecord;
using H3AutoPolicy::BattleStoreRecordContentEquals;
using H3AutoPolicy::BattleStoreStampValid;
using H3AutoPolicy::AA_MANUAL;
using H3AutoPolicy::AA_DEFEND;
using H3AutoPolicy::AA_WAIT;
using H3AutoPolicy::AA_MOVE;
using H3AutoPolicy::AA_MELEE_ATTACK;
using H3AutoPolicy::AA_RANGED_ATTACK;
using H3AutoPolicy::AA_FIRST_AID;
using H3AutoPolicy::AA_SCATTER;
using H3AutoPolicy::AA_RANDOM_MOVE;
using H3AutoPolicy::AA_SIEGE;
using H3AutoPolicy::AA_COUNT;
using H3AutoPolicy::AT_NONE;
using H3AutoPolicy::AT_STACK;
using H3AutoPolicy::AT_POSITION;
using H3AutoPolicy::ATS_OWN;
using H3AutoPolicy::ATS_ENEMY;
using H3AutoPolicy::ATS_EITHER;
using H3AutoPolicy::SEL_RANDOM;
using H3AutoPolicy::SEL_RANGED_SPEED;
using H3AutoPolicy::SEL_COUNT_HIGH;
using H3AutoPolicy::SEL_WOUND_RATIO;
using H3AutoPolicy::SEL_WOUND_VALUE;
using H3AutoPolicy::SEL_COUNT;
using H3AutoPolicy::MELEE_PAIR_CAPACITY;
using H3AutoPolicy::MOVE_WAYPOINT_CAPACITY;
using H3AutoPolicy::AT_COUNT;
using H3AutoPolicy::ATS_COUNT;
using H3AutoPolicy::ProtectMode;
using H3AutoPolicy::PM_COUNT_BELOW;
using H3AutoPolicy::PM_LOSS_GT_RESTORE;
using H3AutoPolicy::PM_NONE;
using H3AutoPolicy::AutoWaitMode;
using H3AutoPolicy::AW_NONE;
using H3AutoPolicy::AW_EVERY_TURN;
using H3AutoPolicy::AW_SIEGE;
using H3AutoPolicy::AW_COUNT;
using H3AutoPolicy::SUMMON_ELEMENT_COUNT;
using H3AutoPolicy::SUMMON_COMBINE_AND;
using H3AutoPolicy::SUMMON_COMBINE_OR;
using H3AutoPolicy::MakeDefaultRule;
using H3AutoPolicy::StatusProfileFields;
using H3AutoPolicy::SpellCastTimingFields;
using H3AutoPolicy::MakeDefaultSpellCastTiming;
using H3AutoPolicy::NormalizeSpellCastTiming;
using H3AutoPolicy::StableStackIdentity;
using H3AutoPolicy::MakeStableStackIdentity;
using H3AutoPolicy::CAST_TIMING_DEFAULT;
using H3AutoPolicy::CAST_TIMING_UNIT;
using H3AutoPolicy::MakeDefaultStatusFields;
using H3AutoPolicy::kStatusSlotCapacity;
using H3AutoPolicy::kBuffSpellIds;
using H3AutoPolicy::kEnemyStatusSpellIds;
using H3AutoPolicy::kEnemyStatusSpellCount;
using H3AutoPolicy::IsFriendlyStatusSpell;
using H3AutoPolicy::IsEnemyStatusSpell;
using H3AutoPolicy::ChooseEnemySingleStatusTarget;
using H3AutoPolicy::ChooseEnemyMassStatusTarget;
using H3AutoPolicy::kCurseSpellId;
using H3AutoPolicy::kWeaknessSpellId;
using H3AutoPolicy::kDisruptingRaySpellId;
using H3AutoPolicy::kBerserkSpellId;
using H3AutoPolicy::kHypnotizeSpellId;
using H3AutoPolicy::kForgetfulnessSpellId;
using H3AutoPolicy::kAttackMagicCapacity;
using H3AutoPolicy::kAttackMagicPositionCapacity;
using H3AutoPolicy::kAttackMagicSpellIds;
using H3AutoPolicy::AttackMagicRule;
using H3AutoPolicy::MakeDefaultAttackMagicRule;
using H3AutoPolicy::NormalizeAttackMagicRule;
using H3AutoPolicy::NormalizeStatusFields;
using H3AutoPolicy::AddAttackMagicRule;
using H3AutoPolicy::RemoveAttackMagicRule;
using H3AutoPolicy::AddAttackMagicPosition;
using H3AutoPolicy::RemoveAttackMagicPosition;
using H3AutoPolicy::IsAttackMagicSpellId;
