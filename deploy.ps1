$ErrorActionPreference = 'Stop'
$gameDir = 'D:\Heroes3\Heroes3_2026.10.07'
# 热血插件目录与 H3Auto、H3RndNew 共用；img 下 HB_bg.pcx 与 H3Auto 的 HA_* 不冲突。
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

    Write-Host "已部署到 $packsDst"
} catch {
    Write-Host "部署错误: $_"
    exit 1
}
