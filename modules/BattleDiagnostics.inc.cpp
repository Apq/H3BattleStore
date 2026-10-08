// 单游戏线程操作上下文。仅在请求/阶段变化时落盘，不逐帧刷战场状态。
static const char* kDiagnosticBuild_ = "battle-diag-ammo-equipment-v7";
static LONG g_diagSequence = 0;
static struct {
    LONG id;
    DWORD started;
    DWORD stageStarted;
    const char* kind;
    const char* stage;
    int side;
    int slot;
    bool writing;
} g_diag = {};

static std::string DiagUtf8_(const std::wstring& text)
{
    if (text.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (!length) return "utf8 conversion failed";
    std::vector<char> buffer(length);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, buffer.data(), length, nullptr, nullptr);
    return std::string(buffer.data());
}

static void DiagState_(const H3CombatManager* mgr, const char* event)
{
    if (!CombatIsReadable_(mgr)) {
        LogWarn("[State op=%ld] event=%s mgr=%p unreadable", g_diag.id, event, mgr);
        return;
    }
    int moving = 0, firstMoving = -1;
    for (int i = 0; i < 187; ++i) if (*((const uint8_t*)mgr + 0x14031 + i)) {
        if (firstMoving < 0) firstMoving = i;
        ++moving;
    }
    LogDebug("[State op=%ld] event=%s mgr=%p dlg=%p turn=%d current=%d:%d active=%p activeSide=%d finished=%d auto=%d tactics=%d wait=%d any_action_taken=%d path_nonzero=%d action=%d/%d/%d/%d",
        g_diag.id, event, mgr, mgr->dlg, mgr->turn, mgr->currentMonSide, mgr->currentMonIndex,
        mgr->activeStack, mgr->currentActiveSide, (int)mgr->finished, (int)mgr->autoCombat,
        (int)mgr->tacticsPhase, mgr->waitPhase, (int)*((const uint8_t*)mgr + 0x14030), moving,
        (int)mgr->action, (int)mgr->actionParameter, (int)mgr->actionTarget, (int)mgr->actionParameter2);
    if (moving) LogDebug("[State op=%ld] first_path_nonzero=%d", g_diag.id, firstMoving);
}

static void DiagInputState_(const H3CombatManager* mgr, const char* event, int result)
{
    if (!LogEnabled_(LOG_DEBUG) || !CombatIsReadable_(mgr) || !mgr->dlg) return;
    H3DlgItem* wait = mgr->dlg->GetH3DlgItem(0x7D9);
    H3DlgItem* defend = mgr->dlg->GetH3DlgItem(0x7DA);
    LogDebug("[InputState op=%ld] event=%s result=%d control=%d current=%d:%d active=%p action=%d wait_phase=%d wait_enabled=%d wait_shaded=%d defend_enabled=%d defend_shaded=%d",
        g_diag.id, event, result, *((const int32_t*)((const uint8_t*)mgr + 0x132B4)),
        mgr->currentMonSide, mgr->currentMonIndex, mgr->activeStack, (int)mgr->action,
        *((const uint8_t*)mgr + 0x13DE4),
        wait ? (wait->IsEnabled() ? 1 : 0) : -1,
        wait ? (wait->IsSet(h3::NH3DlgControls::NState::SHADED) ? 1 : 0) : -1,
        defend ? (defend->IsEnabled() ? 1 : 0) : -1,
        defend ? (defend->IsSet(h3::NH3DlgControls::NState::SHADED) ? 1 : 0) : -1);
}

static void DiagBegin_(const char* kind, const char* origin, const H3CombatManager* mgr)
{
    if (g_diag.id) LogWarn("[Op %ld] replaced by new request stage=%s writing=%d", g_diag.id, g_diag.stage, g_diag.writing ? 1 : 0);
    g_diag.id = InterlockedIncrement(&g_diagSequence);
    g_diag.started = g_diag.stageStarted = GetTickCount();
    g_diag.kind = kind;
    g_diag.stage = "request";
    g_diag.side = g_diag.slot = -1;
    g_diag.writing = false;
    LogInfo("[Op %ld] begin kind=%s origin=%s tid=%lu", g_diag.id, kind, origin, GetCurrentThreadId());
    DiagState_(mgr, "request");
}

static void DiagStage_(const char* stage)
{
    const DWORD now = GetTickCount();
    WriteLogLv(LogStageLevel_(stage), "[Op %ld] stage=%s previous=%s previous_ms=%lu elapsed_ms=%lu writing=%d",
        g_diag.id, stage, g_diag.stage ? g_diag.stage : "none",
        now - g_diag.stageStarted, now - g_diag.started, g_diag.writing ? 1 : 0);
    g_diag.stage = stage;
    g_diag.stageStarted = now;
    g_diag.side = g_diag.slot = -1;
}

static void DiagCursor_(int side, int slot)
{
    g_diag.side = side;
    g_diag.slot = slot;
}

static void DiagEnd_(const char* outcome, const char* reason)
{
    WriteLogLv(LogOutcomeLevel_(outcome, g_diag.writing),
        "[Op %ld] end kind=%s outcome=%s stage=%s side=%d slot=%d elapsed_ms=%lu writing=%d reason=%s",
        g_diag.id, g_diag.kind ? g_diag.kind : "none", outcome,
        g_diag.stage ? g_diag.stage : "none", g_diag.side, g_diag.slot,
        GetTickCount() - g_diag.started, g_diag.writing ? 1 : 0, reason ? reason : "none");
    g_diag.id = 0;
    g_diag.writing = false;
}

static void DiagSummary_(const CodecCapture& capture, const char* event)
{
    unsigned occupied = 0, alive = 0, spells = 0, relations = 0;
    for (int side = 0; side < 2; ++side) {
        for (int slot = 0; slot < 21; ++slot) {
            const CodecStack& s = capture.stacks[side][slot];
            if (!s.occupied) continue;
            ++occupied;
            if (s.numberAlive > 0) ++alive;
            spells += (unsigned)s.spellIds.size();
            for (int i = 0; i < 4; ++i) relations += (unsigned)s.relations[i].size();
            LogDebug("[Stack op=%ld] event=%s slot=%d:%d type=%d pos=%d alive=%d dead=%d hpLost=%d start=%d morale=%d luck=%d spells=%u clone=%d animation=%d/%d shots=%d ammoCart=%d",
                g_diag.id, event, side, slot, s.type, s.position, s.numberAlive, s.numberForeverDead,
                s.healthLost, s.numberAtStart, s.morale, s.luck, (unsigned)s.spellIds.size(),
                s.cloneId, s.animation, s.animationFrame, s.infoCombat[6], capture.warMachines[side][1].id);
        }
        LogDebug("[Hero op=%ld] event=%s side=%d present=%d machines=%d/%d,%d/%d,%d/%d,%d/%d",
            g_diag.id, event, side, capture.heroPresent[side] ? 1 : 0,
            capture.warMachines[side][0].id, capture.warMachines[side][0].subtype,
            capture.warMachines[side][1].id, capture.warMachines[side][1].subtype,
            capture.warMachines[side][2].id, capture.warMachines[side][2].subtype,
            capture.warMachines[side][3].id, capture.warMachines[side][3].subtype);
    }
    LogInfo("[Snapshot op=%ld] event=%s version=%u turn=%d current=%d:%d occupied=%u alive=%u obstacles=%u logs=%u spells=%u relations=%u mana=%d/%d casted=%d/%d rngTls=%08X rngMirror=%08X",
        g_diag.id, event, capture.version, capture.turn, capture.currentMonSide, capture.currentMonIndex,
        occupied, alive, (unsigned)capture.obstacles.size(), (unsigned)capture.logLines.size(),
        spells, relations, (int)capture.spellPoints[0], (int)capture.spellPoints[1],
        (int)capture.heroCasted[0], (int)capture.heroCasted[1], capture.rngTlsSeed, capture.rngMirrorSeed);
}

static void DiagSections_(const std::vector<hbs::ArchiveSection>& sections, const char* event)
{
    if (!LogEnabled_(LOG_DEBUG)) return;
    for (size_t i = 0; i < sections.size(); ++i) {
        const hbs::ArchiveSection& s = sections[i];
        LogDebug("[Section op=%ld] event=%s id=%u bytes=%u crc32=%08X", g_diag.id, event, s.id,
            (unsigned)s.bytes.size(), hbs::detail::Crc32(s.bytes.data(), s.bytes.size()));
    }
}

static bool CaptureBattle_(const H3CombatManager* mgr, CodecCapture* out, std::string* error);

static bool DiagVerifyRestore_(const H3CombatManager* mgr, const CodecCapture& expected)
{
    DiagStage_("verify.recapture");
    std::unique_ptr<CodecCapture> actualStorage(new CodecCapture{});
    CodecCapture& actual = *actualStorage;
    std::string error;
    if (!CaptureBattle_(mgr, &actual, &error)) {
        LogError("[Verify op=%ld] recapture failed: %s", g_diag.id, error.c_str());
        return false;
    }
    DiagSummary_(actual, "after-restore");
    // Only saved hover intent is invalidated. Actual caches must match exactly.
    std::unique_ptr<CodecCapture> normalized(new CodecCapture(expected));
    CodecInvalidateHover_(normalized.get());
    std::vector<hbs::ArchiveSection> saved, current;
    if (!CodecEncode(*normalized, &saved, &error) || !CodecEncode(actual, &current, &error)) {
        LogError("[Verify op=%ld] encode failed: %s", g_diag.id, error.c_str());
        return false;
    }
    bool equal = true;
    for (size_t i = 0; i < saved.size(); ++i) {
        const hbs::ArchiveSection* got = FindSection_(current, saved[i].id);
        size_t first = 0;
        const bool match = got && CodecSectionEqual_(saved[i], *got, &first);
        WriteLogLv(match ? LOG_DEBUG : LOG_WARN,
            "[Verify op=%ld] section=%u equal=%d expected_bytes=%u actual_bytes=%u first_diff=%u expected_crc=%08X actual_crc=%08X",
            g_diag.id, saved[i].id, match ? 1 : 0, (unsigned)saved[i].bytes.size(),
            got ? (unsigned)got->bytes.size() : 0, (unsigned)first,
            hbs::detail::Crc32(saved[i].bytes.data(), saved[i].bytes.size()),
            got ? hbs::detail::Crc32(got->bytes.data(), got->bytes.size()) : 0);
        if (!match && got) {
            // 字节级差异取证：打印 first_diff 起两侧各 12 字节，避免事后手算布局。
            const size_t at = first < saved[i].bytes.size() ? first : saved[i].bytes.size();
            const size_t span = 12;
            const size_t maxLen = saved[i].bytes.size() > got->bytes.size()
                ? saved[i].bytes.size() : got->bytes.size();
            const size_t end = at + span < maxLen ? at + span : maxLen;
            char expHex[64] = {}, actHex[64] = {};
            size_t eLen = 0, aLen = 0;
            for (size_t k = at; k < end; ++k) {
                if (k < saved[i].bytes.size() && eLen + 3 < sizeof(expHex))
                    eLen += (size_t)_snprintf(expHex + eLen, sizeof(expHex) - eLen, "%02X ", saved[i].bytes[k]);
                if (k < got->bytes.size() && aLen + 3 < sizeof(actHex))
                    aLen += (size_t)_snprintf(actHex + aLen, sizeof(actHex) - aLen, "%02X ", got->bytes[k]);
            }
            WriteLogLv(LOG_WARN, "[Verify op=%ld] section=%u diff_at=%u expected[%u..]=%s actual[%u..]=%s",
                g_diag.id, saved[i].id, (unsigned)at, (unsigned)at, expHex, (unsigned)at, actHex);
        }
        equal = equal && match;
    }
    WriteLogLv(equal ? LOG_INFO : LOG_WARN, "[Verify op=%ld] sections=%u serialized_equal=%d", g_diag.id, (unsigned)saved.size(), equal ? 1 : 0);
    DiagState_(mgr, "after-restore");
    return equal;
}
