# check-ui-text-keys.ps1 —— 界面文案键一致性门禁（离线，不碰游戏）
#
# 背景：UiTexts.inc.cpp 的 T(key) 查不到键时返回字面量 "!!键不存在!!"，那是画面
# 上直接可见的占位串。cell.siege_hint 曾经因为「代码在用、内置表和 lang 文件都
# 没有」而在面板上显示这行（H3Auto 源插件就漏了这个键，移植时一并带过来）。
#
# 本脚本核对四处：
#   1) modules 下 T("...") 的字面量引用
#   2) UiTexts.inc.cpp 内置表 kUiTextDefaults
#   3) lang\<语言>.ini（默认 zh-CN，可用 -Lang 换）
#   4) 单行文案宽度门禁：画在固定单行框里的键不许超宽（超了只会被裁掉右半截）
#
# 用法（任意目录）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-ui-text-keys.ps1
# 退出码：0 = 一致；1 = 有引用键未定义（画面会显示 !!键不存在!!）；2 = 路径/参数问题。

[CmdletBinding()]
param(
    [string]$Lang = 'zh-CN'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$modulesDir = Join-Path $repoRoot 'modules'
$textsFile = Join-Path $modulesDir 'UiTexts.inc.cpp'
$langFile = Join-Path $repoRoot ('lang\' + $Lang + '.ini')

foreach ($p in @($modulesDir, $textsFile, $langFile)) {
    if (-not (Test-Path -LiteralPath $p)) {
        Write-Output ('[错误] 缺少路径：' + $p)
        exit 2
    }
}

# ---- 1) 代码里 T("...") 的引用键 ----
$codeKeys = New-Object 'System.Collections.Generic.HashSet[string]'
Get-ChildItem -LiteralPath $modulesDir -File -Recurse -Include *.cpp, *.hpp | ForEach-Object {
    $t = [IO.File]::ReadAllText($_.FullName)
    foreach ($m in [Regex]::Matches($t, 'T\("([^"]+)"\)')) {
        [void]$codeKeys.Add($m.Groups[1].Value)
    }
}

# ---- 2) 内置表的定义键 ----
$texts = [IO.File]::ReadAllText($textsFile)
$tableKeys = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($m in [Regex]::Matches($texts, '\{\s*"([^"]+)"\s*,')) {
    [void]$tableKeys.Add($m.Groups[1].Value)
}

# ---- 3) lang 文件的键（[section] + key = value → "section.key"）----
$iniKeys = New-Object 'System.Collections.Generic.HashSet[string]'
$section = ''
foreach ($line in ([IO.File]::ReadAllLines($langFile))) {
    $s = $line.Trim()
    if ($s.Length -eq 0) { continue }
    if ($s.StartsWith(';') -or $s.StartsWith('#')) { continue }
    if ($s.StartsWith('[')) {
        $end = $s.IndexOf(']')
        if ($end -gt 1) { $section = $s.Substring(1, $end - 1).Trim() }
        continue
    }
    $eq = $s.IndexOf('=')
    if ($eq -le 0 -or $section.Length -eq 0) { continue }
    [void]$iniKeys.Add($section + '.' + $s.Substring(0, $eq).Trim())
}

# ---- 结果 ----
Write-Output ('仓库：' + $repoRoot)
Write-Output ('  代码引用键 = {0}；内置表键 = {1}；{2} 键 = {3}' -f $codeKeys.Count, $tableKeys.Count, $Lang, $iniKeys.Count)

$undefined = @($codeKeys | Where-Object { -not $tableKeys.Contains($_) } | Sort-Object)
$notInLang = @($tableKeys | Where-Object { -not $iniKeys.Contains($_) } | Sort-Object)
$langOnly = @($iniKeys | Where-Object { -not $tableKeys.Contains($_) } | Sort-Object)
$unused = @($tableKeys | Where-Object { -not $codeKeys.Contains($_) } | Sort-Object)

Write-Output ''
if ($undefined.Count -gt 0) {
    Write-Output ('[失败] 代码引用了但内置表没有的键（画面会显示 !!键不存在!!）共 {0} 个：' -f $undefined.Count)
    $undefined | ForEach-Object { Write-Output ('  x ' + $_) }
} else {
    Write-Output '[通过] 所有代码引用键都在内置表里（画面不会出现 !!键不存在!!）'
}

if ($notInLang.Count -gt 0) {
    Write-Output ''
    Write-Output ('[提示] 内置表有、lang\{0}.ini 没覆盖的键 {1} 个（不报错：回退内置中文默认）' -f $Lang, $notInLang.Count)
}
if ($langOnly.Count -gt 0) {
    Write-Output ''
    Write-Output ('[警告] lang\{0}.ini 有、内置表没有的键 {1} 个（拼错即静默失效，建议核对）' -f $Lang, $langOnly.Count)
    $langOnly | ForEach-Object { Write-Output ('  ? ' + $_) }
}
if ($unused.Count -gt 0) {
    Write-Output ''
    Write-Output ('[提示] 内置表有、代码未直接引用的键 {0} 个（可能由其它机制/数组使用，仅提示）' -f $unused.Count)
}

# ---- 4) 单行文案宽度门禁（超长只会被裁，不会换行）----
#
# 背景：面板上有几句文案画在**固定高度的单行框**里，例如保持状态页力盾落点下面
# 那行 —— DrawTxt(scr, small_font, T("panel.status_note"), GRID_FRAME_X,
# STATUS_NOTE_Y, GRID_FRAME_W, 18, ...)。超长不会换行、只会把右半截裁掉：
# 2026-10-16 用户实测报过（我当时把这句从 37.5 全角宽改到 65.5 全角宽）。
#
# 判据用「半角单位」：全角字符 = 2，其余 = 1。上限来源：panel.status_note 的框宽
# GRID_FRAME_W = 531；本机小字体实测约 13px/全角，而旧版 37.5 全角宽（≈488px）
# 经用户实机确认完整可见 ⇒ 取 37 全角 = 74 半角为上限（还留了余量）。
# 新增条目：凡是画在 `DrawTxt(..., <宽>, <小高度>)` 单行框里的键，按同法量一次再登记。
$singleLineBudget = @{
    'panel.status_note' = 74
    # 部队页首行标签框 ARMY_TIMING_LABEL_W = 116：2026-10-17 用户先把「每回合施法时机」
    # 缩成「每回合施法」（86px 框装不下），后要求改回全称并加宽标签框。全称是
    # **7 全角 = 14 半角**（86px 恰显示 6 字 ⇒ ≈14.3px/字，7 字 ≈ 100px），
    # 116px 两种口径都留余量 ⇒ 上限取 14。
    'panel.cast_timing_label' = 14
    # 「等待后」复选框文字框 ARMY_TIMING_WAIT_TEXT_W = 56：3 全角 = 6 半角。
    # 同款复选框（停止行的「敌方魔力≤」5 字）用 72px 框 ⇒ 约 8px/半角宽，
    # 56px 容得下 6 半角（≈48px）且留余量；上限按实际字数取 6。
    'panel.cast_timing_after_wait' = 6
    # 设置面板第二行右侧热键提示框 PROFILE_HINT_W = 100：
    # 模板「%s 打开设置」= 3 半角 + 8 = 11 半角（≈88px）；%s 运行时替换成用户键名，
    # 最坏是「F10」这类 3 半角 ⇒ 实际 12 半角（≈100px），正好贴住 100px 框。
    'panel.open_settings_hint' = 12
}

$fullWidthRe = '[\u1100-\u115F\u2E80-\uA4CF\uAC00-\uD7A3\uF900-\uFAFF\uFE30-\uFE4F\uFF00-\uFF60\uFFE0-\uFFE6]'
$overWide = @()
foreach ($key in ($singleLineBudget.Keys | Sort-Object)) {
    $budget = $singleLineBudget[$key]
    $m = [Regex]::Match($texts, '\{\s*"' + [Regex]::Escape($key) + '"\s*,\s*"([^"]*)"')
    if (-not $m.Success) {
        $overWide += ('{0}：内置表里找不到这个键' -f $key)
        continue
    }
    $val = $m.Groups[1].Value
    $fw = ([Regex]::Matches($val, $fullWidthRe)).Count
    $half = $fw * 2 + ($val.Length - $fw)
    if ($half -gt $budget) {
        $overWide += ('{0}：{1} 半角宽 > 上限 {2}（{3}）' -f $key, $half, $budget, $val)
    } else {
        Write-Output ('  ok {0}：{1} / {2} 半角宽' -f $key, $half, $budget)
    }
}

Write-Output ''
if ($overWide.Count -gt 0) {
    Write-Output ('[失败] 单行文案超宽（画面只显示前半截，静默截断）共 {0} 个：' -f $overWide.Count)
    $overWide | ForEach-Object { Write-Output ('  x ' + $_) }
    Write-Output '  修法：缩短文案，或改成两行/加宽框；别只加大框高。'
} else {
    Write-Output '[通过] 已登记的单行文案都在宽度预算内'
}

if ($undefined.Count -gt 0 -or $overWide.Count -gt 0) { exit 1 }
exit 0
