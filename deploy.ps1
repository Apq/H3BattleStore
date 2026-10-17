$ErrorActionPreference = 'Stop'
# 共享部署目标（多目录同时部署）：
#   $env:H3_GAME_DIRS（分号分隔）> $env:H3_GAME_DIR（临时只部署一个目录）
#   > 仓库根 H3Env.ps1 的 $H3GameDirs > 内置默认值。换游戏版本只改 H3Env.ps1 一处。
$h3root = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath "$h3root\H3Env.ps1") { . "$h3root\H3Env.ps1" }
if (-not (Get-Command Get-H3GameDirs -ErrorAction SilentlyContinue)) {
    function Get-H3GameDirs {
        if ($env:H3_GAME_DIRS) { return @($env:H3_GAME_DIRS -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
        if ($env:H3_GAME_DIR) { return @($env:H3_GAME_DIR) }
        return @('D:\Heroes3\Heroes3_2026.10.09', 'D:\Heroes3\英雄无敌3 V5.4')
    }
}
# 热血插件目录与 H3RndNew 共用。2026-10-15 H3Auto 功能合并进本插件后，H3Auto
# 不再单独部署：pcx 下的 HA_*.pcx 与 lang\zh-CN.ini 由本插件负责（原属 H3Auto）。
$packsRel = '_HD3_Data\Packs\热血插件'
$src = "$PSScriptRoot\Release"

$dll = Join-Path $src 'H3BattleStore.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    Write-Host "部署错误: 未找到编译输出：$dll"
    exit 1
}

$deployed = @()
$failed = @()
foreach ($gameDir in @(Get-H3GameDirs)) {
    $packsDst = Join-Path $gameDir $packsRel
    if (-not (Test-Path -LiteralPath $gameDir)) {
        Write-Host "跳过（游戏目录不存在）：$gameDir"
        $failed += "$gameDir（游戏目录不存在）"
        continue
    }
    try {
        if (-not (Test-Path -LiteralPath $packsDst)) {
            New-Item -ItemType Directory -Path $packsDst -Force | Out-Null
        }
        Copy-Item $dll $packsDst -Force
        Copy-Item "$PSScriptRoot\H3BattleStore.default.ini" $packsDst -Force
        Copy-Item "$PSScriptRoot\使用说明.txt" $packsDst -Force

        # 素材目录名 2026-10-15 由 img 改为 pcx（与 H3MegaDesc 同规）；DLL 内加载路径同步为 pcx\。
        $pcxDst = Join-Path $packsDst 'pcx'
        if (-not (Test-Path -LiteralPath $pcxDst)) {
            New-Item -ItemType Directory -Path $pcxDst -Force | Out-Null
        }
        Copy-Item "$PSScriptRoot\pcx\HB_bg.pcx" $pcxDst -Force
        # 打铁助手面板素材（原 H3Auto 的 HA_*，与 HB_bg.pcx 不冲突）。
        Copy-Item "$PSScriptRoot\pcx\HA_*.pcx" $pcxDst -Force

        $langDst = Join-Path $packsDst 'lang'
        if (-not (Test-Path -LiteralPath $langDst)) {
            New-Item -ItemType Directory -Path $langDst -Force | Out-Null
        }
        Copy-Item "$PSScriptRoot\lang\zh-CN.ini" $langDst -Force

        Write-Host "已部署到 $packsDst"
        Write-Host "  部署哈希: $((Get-FileHash $dll -Algorithm SHA256).Hash)"
        $deployed += $packsDst
    } catch {
        Write-Host "部署失败: $packsDst -> $_"
        $failed += "$packsDst（$($_.Exception.Message)）"
    }
}

if ($failed.Count -gt 0) {
    Write-Host "部署到 $($deployed.Count) 个目录成功，$($failed.Count) 个失败："
    $failed | ForEach-Object { Write-Host "  - $_" }
    exit 1
}
Write-Host "已部署到全部 $($deployed.Count) 个游戏目录"
