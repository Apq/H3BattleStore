// ========== BattleStoreService.inc.cpp ==========
// 领域服务层（docs/09-界面与功能分离 第1步抽服务）：存档列表数据、恢复请求状态、
// 热键策略、玩家文案目录与保存窗口门禁。仅游戏主线程调用；不绘制、不吞输入。
// 本步为符号原样搬移（BattleUi/Entry → 此处），行为零变化。

// 列表条目：时间戳、同秒撞名后缀与按需读取的档案路径。
struct StoreEntry
{
    uint64_t timestampUtcMs;
    uint32_t sequence;
    uint32_t filenameAttempt = 1;
    std::wstring path;
};

// 功能状态：热键值、当前场次指纹与存档列表。界面实现只读，写路径在服务函数内。
static struct
{
    char saveKey = 'G';
    std::string battleKey;
    std::vector<StoreEntry> entries;
} g_store;

// 读档请求：从确认到恢复执行的单飞行状态（执行调度第4步迁入）。
static struct {
    bool pending = false;
    unsigned generation = 0;
    DWORD requested = 0;
    std::string battleKey;
    StoreEntry entry;
} g_restoreRequest;

// 一次物理按下只允许触发一次保存：game-message 与 system-frame 两个输入源都会
// 看到同一次按下边沿（19:05 日志 Op1→Op2 相隔 76ms 双触发），窗口打开后会存两档。
// 首个到达的边沿置位，后续边沿吞掉；按键松开后由帧循环清零重新武装。
static volatile LONG g_saveEdgeConsumed = 0;

// 热键占用表（原版战斗/SoD_SP/打铁助手/HoD 之外的允许字母）。
static const char* const kStoreFreeKeys_ = "BFGKMNUVXY";

static bool StoreKeyIsFree_(char key)
{
    return key >= 'A' && key <= 'Z' && strchr(kStoreFreeKeys_, key) != nullptr;
}

static char StoreLoadHotkey_()
{
    char keyText[16] = {};
    IniReadUtf8(g_user_ini_path, "Hotkeys", "SaveKey", "G", keyText, sizeof(keyText));
    if (StoreKeyIsFree_(keyText[0])) return keyText[0];
    return 'G';
}

static void StoreSaveHotkey_()
{
    char keyText[8] = {};
    _snprintf(keyText, sizeof(keyText), "%c", g_store.saveKey);
    if (!IniWriteKeyUtf8(g_user_ini_path, "Hotkeys", "SaveKey", keyText))
        LogError("[Config] SaveKey persistence failed runtime_key=%c", g_store.saveKey);
}

static void StoreFormatStamp_(const StoreEntry& entry, char* out, size_t cap)
{
    if (!hbs::detail::FormatNumberedArchiveStamp(entry.timestampUtcMs, entry.filenameAttempt, out, cap, nullptr)
        && out && cap > 0)
        _snprintf_s(out, cap, _TRUNCATE, "%s", "----");
}

static LogRepeatGate_ g_storeListFailure;
static void StoreListFailure_(const char* phase, const std::string& error)
{
    unsigned skipped = 0;
    if (g_storeListFailure.Admit(GetTickCount(), 30000, &skipped))
        LogWarn("[List op=%ld] phase=%s failed reason=%s suppressed=%u", g_diag.id, phase, error.c_str(), skipped);
}

// 列表数据扫描：按场次指纹取档、填 entries；成功补记失败限频恢复。
// （输入管道清理由界面包装 UiReloadEntries_ 负责，服务不触碰输入状态。）
static bool StoreReloadList_(const H3CombatManager* mgr)
{
    g_store.entries.clear();
    std::string battleKey;
    std::string error;
    if (!BattleFingerprint_(mgr, &battleKey, &error)) { StoreListFailure_("fingerprint", error); return false; }
    g_store.battleKey = battleKey;
    hbs::ArchiveStore store(ArchiveRoot_());
    std::vector<hbs::ArchiveRecord> records;
    std::wstring storeError;
    if (!store.List(battleKey, "", records, storeError)) { StoreListFailure_("scan", DiagUtf8_(storeError)); return false; }
    if (!storeError.empty()) LogWarn("[List op=%ld] scan warning: %s", g_diag.id, DiagUtf8_(storeError).c_str());
    LogDebug("[List op=%ld] battle=%s records=%u", g_diag.id, battleKey.c_str(), (unsigned)records.size());
    if (g_storeListFailure.seen) {
        LogInfo("[List] recovered suppressed=%u", g_storeListFailure.suppressed);
        g_storeListFailure = {};
    }
    g_store.entries.reserve(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        StoreEntry entry;
        entry.timestampUtcMs = records[i].timestampUtcMs;
        entry.sequence = records[i].sequence;
        entry.path = records[i].path;
        hbs::detail::ParseGeneratedName(hbs::detail::FileNameOf(entry.path),
            entry.timestampUtcMs, records[i].battleKey, &entry.filenameAttempt);
        g_store.entries.push_back(entry);
    }
    return true;
}

// Player text never includes raw diagnostics, even for a future unknown error.
static std::string StorePlayerTextZh_(const std::string& reason)
{
    static const struct { const char* raw; const char* zh; } reasons[] = {
        {"battle generation changed", "确认期间战斗已切换，请重新选择存档。"},
        {"restore window changed", "当前不在玩家等待下令的时刻，请回到战场后重试。"},
        {"battle fingerprint changed", "当前战斗与所选存档不一致，请重新选择存档。"},
        {"battle fingerprint failed", "无法识别当前战斗，请重新进入战斗。"},
        {"未取得本场战斗的战前指纹，请重新进入战斗", "未取得本场战斗的战前指纹，请重新进入战斗。"},
        {"archive battle key mismatch", "存档所属战斗与当前战斗不一致，不能读取。"},
        {"archive not found", "所选存档已不存在，可能已被删除或移动。"},
        {"unsupported capture version; create a new v4 save", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"unsupported capture version", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"capture version", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"saved state is not a player waiting turn", "该存档不是玩家等待下令时的状态，不能恢复。"},
        {"live active stack uses reserved slot", "存档快照版本不兼容，请使用当前版本重新存档。"},
        {"battle participant count exceeds supported slots", "参战部队数量超出当前支持范围。"},
        {"siege restoration is not supported yet", "暂不支持读取攻城战存档。"},
        {"reserved slot scalar outside valid range", "存档中的保留位数值超出有效范围。"},
        {"reserved slot AI target invalid", "存档中的保留位行动目标无效。"},
        {"arrow tower restoration is not supported yet", "暂不支持恢复箭塔状态。"},
        {"saved stack slot reference invalid", "存档中的部队位置引用无效。"},
        {"saved stack scalar outside valid range", "存档中的部队数值超出有效范围。"},
        {"non-finite spell effect", "存档中的法术效果数值无效。"},
        {"saved AI target invalid", "存档中的电脑行动目标无效。"},
        {"saved spell deque too large", "存档中的部队法术记录过多。"},
        {"saved spell id invalid", "存档中存在无效的法术编号。"},
        {"saved relation vector too large", "存档中的部队关联记录过多。"},
        {"saved relation target invalid", "存档中的部队关联目标无效。"},
        {"saved active stack is not alive", "存档中的行动部队已无存活单位。"},
        {"saved obstacle count exceeds 4096", "存档中的障碍物数量超出支持上限。"},
        {"saved obstacle kind outside known table", "存档中存在无法识别的障碍物类型。"},
        {"saved obstacle geometry invalid", "存档中的障碍物位置或占格数据无效。"},
        {"saved obstacle owner side invalid", "存档中的障碍物所属阵营无效。"},
        {"saved obstacle def name invalid", "存档中的障碍物图像资源名称无效。"},
        {"saved obstacle cell off board", "存档中的障碍物占格超出战场。"},
        {"saved obstacles overlap on one hex", "存档中的障碍物占格重叠，无法安全恢复。"},
        {"battlefield references reserved slot", "战场格子引用了不支持的部队保留位置。"},
        {"live corpse count invalid", "当前战场的尸体数量数据无效。"},
        {"live corpse references reserved slot", "当前战场尸体引用了不支持的部队保留位置。"},
        {"corpse count invalid", "存档中的尸体数量数据无效。"},
        {"square references absent stack", "存档中的战场格子引用了不存在的部队。"},
        {"square position or second hex inconsistent", "存档中的部队占格与位置数据不一致。"},
        {"corpse identity invalid", "存档中的尸体所属部队无效。"},
        {"stack and battlefield position disagree", "存档中的部队位置与战场格子不一致。"},
        {"double-wide second hex missing", "存档中双格部队的第二个占格缺失。"},
        {"battle section overflow", "战斗数据段超出存档容量限制。"},
        {"capture exceeds an exact-save limit", "战斗快照超出精确存档的支持范围。"},
        {"null capture output", "无法创建战斗快照，请重新尝试。"},
        {"capture is missing a required section", "存档缺少必要的战斗数据段。"},
        {"battle wall arrays are truncated", "存档中的城墙数据不完整。"},
        {"battle section is corrupt", "存档中的战斗数据段已损坏。"},
        {"stack section version mismatch", "存档中的部队数据段版本不兼容。"},
        {"stack section is corrupt", "存档中的部队数据段已损坏。"},
        {"square section header mismatch", "存档中的战场格子数据头不匹配。"},
        {"square section is corrupt", "存档中的战场格子数据段已损坏。"},
        {"obstacle section version mismatch", "存档中的障碍物数据段版本不兼容。"},
        {"obstacle count exceeds 4096", "存档中的障碍物数量超出支持上限。"},
        {"obstacle section is corrupt", "存档中的障碍物数据段已损坏。"},
        {"log section version mismatch", "存档中的战斗日志数据段版本不兼容。"},
        {"log count exceeds 100000", "存档中的战斗日志条数超出支持上限。"},
        {"log section is corrupt", "存档中的战斗日志数据段已损坏。"},
        {"hero section version mismatch", "存档中的英雄数据段版本不兼容。"},
        {"hero section is corrupt or mana differs from battle section", "存档中的英雄数据损坏，或魔法值与战斗数据不一致。"},
        {"relation section version mismatch", "存档中的部队关联数据段版本不兼容。"},
        {"relation section is corrupt", "存档中的部队关联数据段已损坏。"},
        {"spell section version mismatch", "存档中的法术数据段版本不兼容。"},
        {"spell section is corrupt", "存档中的法术数据段已损坏。"},
        {"combat manager is not readable", "无法读取当前战斗状态，请回到战场后重试。"},
        {"eagle-eye set is invalid", "当前战斗的鹰眼术记录无效。"},
        {"square corpse count outside 0..14", "当前战场格子的尸体数量超出有效范围。"},
        {"obstacle container is not readable", "无法读取当前战斗的障碍物记录。"},
        {"obstacle info pointer is not a recognized kind", "当前战斗存在无法识别的障碍物。"},
        {"obstacle blocked count outside 0..8", "当前障碍物的占格数量超出有效范围。"},
        {"obstacle def name is not readable", "无法读取当前障碍物的图像资源名称。"},
        {"obstacle anchor hex off board", "当前障碍物的起始格超出战场。"},
        {"obstacle cell off board", "当前障碍物的占格超出战场。"},
        {"obstacle anchor square linkage is broken", "当前障碍物与起始格的关联已失效。"},
        {"obstacle cell square linkage is broken", "当前障碍物与战场占格的关联已失效。"},
        {"combat log container is not readable", "无法读取当前战斗的日志记录。"},
        {"combat log line is not readable", "当前战斗日志中存在无法读取的记录。"},
        {"AI目标指针不属于本战场", "当前电脑行动目标不属于本战场。"},
        {"stack relation points outside the combat manager", "当前部队的关联目标不属于本战场。"},
        {"spell deque is not readable", "无法读取当前部队的法术记录。"},
        {"active stack index", "存档中的行动部队编号无效。"},
        {"stack identity", "存档中的部队类型或位置无效。"},
        {"relation target", "存档中的部队关联目标无效。"},
        {"empty capture", "存档中没有可恢复的部队。"},
        {"obstacle vector is not accessible", "当前障碍物记录不可访问，无法安全恢复。"},
        {"live obstacle resource is unavailable", "当前障碍物的图像资源不可用。"},
        {"live obstacle release slot is unavailable", "当前障碍物的资源释放接口不可用。"},
        {"saved obstacle payload is invalid", "存档中的障碍物数据无效。"},
        {"saved obstacle kind is not present in this game build", "存档中的障碍物类型与当前游戏版本不兼容。"},
        {"saved obstacle def name does not match the live table", "存档中的障碍物图像资源与当前游戏不一致。"},
        {"saved obstacle cell count does not match the live table", "存档中的障碍物占格数量与当前游戏不一致。"},
        {"saved obstacle cell layout does not match the live table", "存档中的障碍物占格布局与当前游戏不一致。"},
        {"obstacle vector would exceed the supported size", "恢复后的障碍物记录数量将超出支持上限。"},
        {"resource donor failed validation", "用于恢复的障碍物图像资源未通过校验。"},
        {"obstacle def could not be loaded", "无法加载存档所需的障碍物图像资源。"},
        {"obstacle def failed validation", "存档所需的障碍物图像资源未通过校验。"},
        {"live obstacle kind degraded since capture", "当前障碍物类型已发生变化，请重新尝试。"},
        {"not at outer player message boundary", "当前不在玩家等待下令的安全时刻，请稍后重试。"},
        {"hero mana not writable", "当前英雄魔法值不可写入，无法安全恢复。"},
        {"battle memory not writable", "当前战斗状态不可写入，无法安全恢复。"},
        {"log dialog not writable", "当前战斗日志窗口不可写入，无法安全恢复。"},
        {"log preallocation failed", "为战斗日志分配内存失败，请稍后重试。"},
        {"object preallocation failed", "为战斗对象分配内存失败，请稍后重试。"},
        {"prepared eagle-eye set mismatch", "准备恢复的鹰眼术记录未通过一致性校验。"},
        {"saved creature DEF frame unavailable", "存档所需的部队动画帧不可用。"},
        {"rollback creature DEF frame unavailable", "当前部队的回滚动画帧不可用，无法安全恢复。"},
        {"obstacle rebuild failed", "障碍物重建失败，未完成读档。"},
        {"restore mismatch; rolled back", "恢复结果与存档不一致，已回滚到读档前状态。"},
        {"resource pool release faulted", "释放障碍物资源时发生异常，已停止战斗。"},
        {"resource pool pin faulted", "保留障碍物资源时发生异常，已停止战斗。"},
        {"resource pool load faulted", "加载障碍物资源时发生异常，已停止战斗。"},
        {"obstacle scratch capacity was not prepared", "障碍物恢复工作区未准备完成，已停止战斗。"},
        {"prepared obstacle resource is missing", "已准备的障碍物资源丢失，已停止战斗。"},
        {"obstacle zombie cleanup faulted", "清理已失效的障碍物时发生异常，已停止战斗。"},
        {"obstacle removal faulted", "移除障碍物时发生异常，已停止战斗。"},
        {"obstacle entry reference faulted", "引用障碍物资源时发生异常，已停止战斗。"},
        {"obstacle vector insert faulted", "添加障碍物时发生异常，已停止战斗。"},
        {"obstacle square placement faulted", "恢复障碍物占格时发生异常，已停止战斗。"},
        {"rebuilt obstacle set lost saved identity", "重建后的障碍物与存档不一致，已停止战斗。"},
        {"obstacle rebuild fault; stopping with partial write", "障碍物重建发生异常，状态可能仅部分恢复，已停止战斗。"},
        {"rollback fault; stopping with unverified state", "回滚发生异常，战斗状态无法确认，已停止战斗。"},
        {"rollback verification failed", "回滚状态未通过校验，已停止战斗。"},
        {"native object allocator fault", "分配战斗对象时发生异常，已停止战斗。"},
        {"deque allocation cleanup fault", "清理法术记录内存时发生异常，已停止战斗。"},
        {"deque map cleanup fault", "清理法术记录索引时发生异常，已停止战斗。"},
        {"eagle-eye set constructor fault", "创建鹰眼术记录时发生异常，已停止战斗。"},
        {"eagle-eye set insertion fault", "恢复鹰眼术记录时发生异常，已停止战斗。"},
        {"native stack preparation fault", "准备部队对象时发生异常，已停止战斗。"},
        {"native stack release fault", "释放部队对象时发生异常，已停止战斗。"},
        {"eagle-eye set release fault", "释放鹰眼术记录时发生异常，已停止战斗。"},
        {"恢复随机数状态时发生异常，已停止战斗", "恢复随机数状态时发生异常，已停止战斗。"},
        {"恢复后刷新战场发生异常，已停止战斗", "恢复后刷新战场发生异常，已停止战斗。"},
        {"回滚后刷新战场发生异常，已停止战斗", "回滚后刷新战场发生异常，已停止战斗。"},
        {"提交随机数状态时发生异常，已停止战斗", "提交随机数状态时发生异常，已停止战斗。"},
        {"archive CRC mismatch", "存档文件校验失败，文件已损坏或被修改。"},
        {"section CRC mismatch", "存档数据段校验失败，文件已损坏或被修改。"},
        {"unsupported archive version", "存档文件版本不兼容，请重新存档。"},
        {"bad archive magic", "所选文件不是有效的战场存档。"},
        {"archive shorter than minimum frame", "存档文件不完整，无法读取。"},
        {"archive exceeds 64MB", "存档文件大小超出支持上限。"},
        {"filename does not match archive identity", "存档文件名与内容不一致，无法安全读取。"},
        {"record metadata does not match archive", "存档内容已改变，请刷新列表后重新选择。"},
        {"archive root is empty", "存档目录未设置，请检查配置。"},
        {"archive root is not a directory", "存档目录路径不是文件夹，请检查配置。"},
        {"archive path has no directory", "存档文件路径缺少目录信息。"},
        {"archive path is outside the store root", "存档文件路径不在允许的存档目录内。"},
        {"refusing archive path outside the store root", "存档文件路径不在允许的存档目录内。"},
        {"GetFinalPathNameByHandleW failed", "无法确认存档文件的实际路径。"},
        {"GetFinalPathNameByHandleW truncated", "存档文件的实际路径不完整。"},
        {"cannot create archive root", "无法创建存档目录，请检查目录路径和访问权限。"},
        {"cannot scan archive root", "无法扫描存档目录，请检查目录访问权限。"},
        {"archive scan ended early", "扫描存档目录时中断，请重新尝试。"},
        {"CreateFileW failed", "无法打开存档，请检查文件访问权限或是否被占用。"},
        {"cannot open archive", "无法打开存档，请检查文件访问权限或是否被占用。"},
        {"cannot read archive size", "无法读取存档文件大小。"},
        {"ReadFile failed", "读取存档文件失败，请检查磁盘或文件占用情况。"},
        {"short archive read", "存档文件未完整读出，可能已被截断。"},
        {"utf8 conversion failed", "无法解析读档失败原因，请查看插件日志。"}
    };
    for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i)
        if (reason == reasons[i].raw) return reasons[i].zh;

    if (std::any_of(reason.begin(), reason.end(), [](unsigned char c) { return c >= 0x80; })) return reason;
    std::string lower = reason;
    for (size_t i = 0; i < lower.size(); ++i)
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += 'a' - 'A';
    auto has = [&](const char* token) { return lower.find(token) != std::string::npos; };
    if (has("crc") || has("checksum")) return "存档校验失败，文件已损坏或被修改。";
    if (has("access denied") || has("permission")) return "没有访问存档文件或目录的权限，请检查访问权限。";
    if (has("path not found") || has("invalid path") || has("invalid name") || has("directory"))
        return "存档目录或文件路径无效，请检查路径配置。";
    if (has("file not found") || has("not exist") || has("vanished")) return "所选存档已不存在，可能已被删除或移动。";
    if (has("sharing violation") || has("lock violation")) return "存档文件正被其他程序占用，请稍后重试。";
    if (has("outside") || has("path") || has("filename")) return "存档路径或文件名不符合要求，无法安全读取。";
    if (has("version")) return "存档版本与当前插件不兼容，请重新存档。";
    if (has("fingerprint") || has("key") || has("metadata")) return "存档标识与当前战斗或列表记录不一致。";
    if (has("truncated") || has("corrupt") || has("header") || has("section") || has("trailing bytes"))
        return "存档数据不完整或已损坏，无法读取。";
    if (has("allocation") || has("preallocation") || has("overflow") || has("too large") || has("exceeds"))
        return "读档所需内存或数据规模超出支持范围。";
    if (has("rollback")) return "战斗恢复或回滚未通过校验，请查看插件日志。";
    if (has("fault") || has("exception")) return "恢复战斗时发生内部异常，请查看插件日志。";
    if (has("obstacle") || has("resource") || has("def")) return "障碍物或图像资源不满足恢复条件。";
    if (has("stack") || has("corpse") || has("square") || has("relation")) return "部队或战场格子数据不满足恢复条件。";
    if (has("spell") || has("eagle-eye") || has("mana")) return "法术或英雄数据不满足恢复条件。";
    if (has("read") || has("open") || has("scan")) return "无法读取存档或当前战斗数据，请稍后重试。";
    return "无法完成读档，具体原因请查看插件日志。";
}

// Archive errors omit Win32 codes; only attach a code to generic I/O failures.
static std::string StoreArchiveReason_(const std::wstring& error, DWORD code)
{
    std::string raw = DiagUtf8_(error);
    if (raw != "CreateFileW failed" && raw != "cannot open archive" && raw != "cannot scan archive root"
        && raw != "cannot create archive root" && raw != "ReadFile failed") return raw;
    switch (code) {
    case ERROR_ACCESS_DENIED: raw += "; access denied"; break;
    case ERROR_FILE_NOT_FOUND: raw += "; file not found"; break;
    case ERROR_PATH_NOT_FOUND: raw += "; path not found"; break;
    case ERROR_INVALID_NAME: raw += "; invalid name"; break;
    case ERROR_BAD_PATHNAME:
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_INVALID_DRIVE:
    case ERROR_DIRECTORY: raw += "; invalid path"; break;
    case ERROR_SHARING_VIOLATION: raw += "; sharing violation"; break;
    case ERROR_LOCK_VIOLATION: raw += "; lock violation"; break;
    default: break;
    }
    LogWarn("[Load op=%ld] archive raw=%s win32=%lu", g_diag.id, DiagUtf8_(error).c_str(), code);
    return raw;
}

// 2026-10-06 用户裁定：保存窗口 = 轮到该玩家行动且尚未下令。
// 窗口外按存档键静默丢弃，不等待动画、不加输入锁。
// Native path cache (+0x14031) is not an executor busy flag. H3API's
// travelingSquares member starts one byte early; do not use it as a gate.
static bool StorePlayerWindow_(const H3CombatManager* mgr, int messageResult, const char** reason)
{
    if (messageResult == 2) { if (reason) *reason = "battle message closes manager"; return false; }
    if (g_restoreBusy || g_restoreRequest.pending || !CombatStorageWindow_(mgr)) {
        if (reason) *reason = "not at player input boundary";
        return false;
    }
    return true;
}

// 保存请求（原 Entry TrySave_，第3步迁入）：边沿闩锁 + 窗口门禁 + 发起捕获。
// 返回 false = 捕获失败（调用方负责界面提示）。rebindWaiting/rebindLatch 为
// 界面交互态快照，仅入日志不参与判定。
static bool StoreRequestSave_(H3CombatManager* mgr, int messageResult, const char* origin,
    bool rebindWaiting, char rebindLatch)
{
    if (InterlockedExchange(&g_saveEdgeConsumed, 1)) {
        LogTrace("[Input] duplicate edge suppressed origin=%s", origin);
        return true;
    }
    if (!StorePlayerWindow_(mgr, messageResult, nullptr)) return true;
    DiagBegin_("save", origin, mgr);
    LogDebug("[Input op=%ld] saveKey=%c rebind=%d latch=%c messageResult=%d", g_diag.id,
        g_store.saveKey, rebindWaiting ? 1 : 0, rebindLatch ? rebindLatch : '-', messageResult);
    return TryCaptureCombat_();
}
