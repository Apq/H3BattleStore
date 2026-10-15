$ErrorActionPreference = 'Stop'
# 共享部署目标：$env:H3_GAME_DIR > 仓库根 H3Env.ps1 > 内置默认值（换版本改 H3Env.ps1）。
$h3root = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath "$h3root\H3Env.ps1") { . "$h3root\H3Env.ps1" }
if (-not (Get-Command Get-H3GameDir -ErrorAction SilentlyContinue)) {
    function Get-H3GameDir {
        if ($env:H3_GAME_DIR -and (Test-Path -LiteralPath $env:H3_GAME_DIR)) { $env:H3_GAME_DIR }
        else { 'D:\Heroes3\Heroes3_2026.10.09' }
    }
}
$gameDir = Get-H3GameDir
# 热血插件目录与 H3RndNew 共用。2026-10-15 H3Auto 功能合并进本插件后，H3Auto
# 不再单独部署：img 下的 HA_*.pcx 与 lang\zh-CN.ini 由本插件负责（原属 H3Auto）。
$packsDst = "$gameDir\_HD3_Data\Packs\热血插件"
$src = "$PSScriptRoot\Release"

try {
    if (-not (Test-Path $packsDst)) {
        New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
    }
    $dll = Join-Path $src 'H3BattleStore.dll'
    if (-not (Test-Path $dll)) {
        throw "未找到编译输出：$dll"
    }
    Copy-Item $dll $packsDst -Force
    Copy-Item "$PSScriptRoot\H3BattleStore.default.ini" $packsDst -Force
    Copy-Item "$PSScriptRoot\使用说明.txt" $packsDst -Force

    $imgDst = Join-Path $packsDst 'img'
    if (-not (Test-Path $imgDst)) {
        New-Item -ItemType Directory -Path $imgDst -Force | Out-Null
    }
    Copy-Item "$PSScriptRoot\img\HB_bg.pcx" $imgDst -Force
    # 打铁助手面板素材（原 H3Auto 的 HA_*，与 HB_bg.pcx 不冲突）。
    Copy-Item "$PSScriptRoot\img\HA_*.pcx" $imgDst -Force

    $langDst = Join-Path $packsDst 'lang'
    if (-not (Test-Path $langDst)) {
        New-Item -ItemType Directory -Path $langDst -Force | Out-Null
    }
    Copy-Item "$PSScriptRoot\lang\zh-CN.ini" $langDst -Force

    Write-Host "已部署到 $packsDst"
} catch {
    Write-Host "部署错误: $_"
    exit 1
}
