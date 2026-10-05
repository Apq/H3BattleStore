# 编译并运行 H3BattleStore 存档文件层测试。
# 产物和临时档只落在 tests\ 下面，不写工作区以外的路径。
$ErrorActionPreference = "Stop"

$testsDir = $PSScriptRoot
if (-not $testsDir) { throw "无法定位 tests 目录" }
$testsDir = [System.IO.Path]::GetFullPath($testsDir)
$storeDir = [System.IO.Path]::GetFullPath((Join-Path $testsDir ".."))
$source = Join-Path $testsDir "ArchiveTests.cpp"
$archive = Join-Path $storeDir "modules\BattleArchive.inc.cpp"
$outDir = Join-Path $testsDir "tmp"
$exe = Join-Path $outDir "ArchiveTests.exe"

if (-not (Test-Path -LiteralPath $source)) { throw "缺少 $source" }
if (-not (Test-Path -LiteralPath $archive)) { throw "缺少 $archive" }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$resolvedOut = [System.IO.Path]::GetFullPath($outDir)
if (-not $resolvedOut.StartsWith($testsDir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "测试输出目录跑到 tests 之外: $resolvedOut"
}

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
if (-not (Test-Path -LiteralPath $vcvars)) { throw "缺少 $vcvars" }

$compile = "cl /nologo /EHsc /std:c++17 /W4 /utf-8 /I `"$storeDir`" `"$source`" /Fo:`"$outDir\ArchiveTests.obj`" /Fe:`"$exe`""
cmd /d /c "`"$vcvars`" >nul && $compile"
if ($LASTEXITCODE -ne 0) { throw "编译失败: $LASTEXITCODE" }

& $exe
$code = $LASTEXITCODE
$leftovers = @(Get-ChildItem -LiteralPath $outDir -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne "ArchiveTests.exe" -and $_.Name -ne "ArchiveTests.obj" })
if ($leftovers.Count -gt 0) {
    $names = ($leftovers | ForEach-Object { $_.FullName }) -join "; "
    throw "测试在 tmp 留下了额外文件: $names"
}
if ($code -ne 0) { throw "测试失败: $code" }
Write-Host "archive tests passed"
