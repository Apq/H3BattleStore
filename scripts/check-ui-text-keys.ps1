# check-ui-text-keys.ps1 —— 界面文案键一致性门禁（离线，不碰游戏）
#
# 背景：UiTexts.inc.cpp 的 T(key) 查不到键时返回字面量 "!!键不存在!!"，那是画面
# 上直接可见的占位串。cell.siege_hint 曾经因为「代码在用、内置表和 lang 文件都
# 没有」而在面板上显示这行（H3Auto 源插件就漏了这个键，移植时一并带过来）。
#
# 本脚本核对三处：
#   1) modules 下 T("...") 的字面量引用
#   2) UiTexts.inc.cpp 内置表 kUiTextDefaults
#   3) lang\<语言>.ini（默认 zh-CN，可用 -Lang 换）
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

if ($undefined.Count -gt 0) { exit 1 }
exit 0
