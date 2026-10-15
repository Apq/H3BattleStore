$ErrorActionPreference = 'Stop'
# H3BattleStore 单元测试（Catch2 v3 amalgamated，2026-10-15 起参照 H3Space）：
#   1) third_party\catch2\catch_amalgamated.cpp 只编一次成 tests\unit-tmp\catch2.obj
#      （源码更新时才重编，避免每套测试重复编译 amalgamated 大文件）；
#   2) 每组 tests\*Tests.cpp（LogControlsTests 另加 lzma 三个 .c）与 catch2.obj 链接
#      成 tests\unit-tmp\<Name>.exe 后运行，退出码非零即失败。
#   LogControlsTests 的 fixture 目录经环境变量 H3BS_TEST_FIXTURE 传入
#   （Catch2 拥有命令行解析，不再用 argv 传参）。
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = Join-Path $PSScriptRoot 'unit-tmp'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat'
$common = '/nologo /EHsc /std:c++17 /utf-8 /W4 /wd4505 /O2 /D_CRT_SECURE_NO_WARNINGS /Ithird_party\catch2'
Push-Location $root
try {
    $catchSrc = Join-Path $root 'third_party\catch2\catch_amalgamated.cpp'
    $catchObj = Join-Path $out 'catch2.obj'
    if (-not (Test-Path $catchObj) -or (Get-Item $catchSrc).LastWriteTime -gt (Get-Item $catchObj).LastWriteTime) {
        Write-Host '== catch2 amalgamated (once) =='
        $log = Join-Path $out 'catch2-compile.log'
        cmd /d /c ('call "' + $vcvars + '" >nul && cl ' + $common + ' /c third_party\catch2\catch_amalgamated.cpp /Fo"' + $catchObj + '" > "' + $log + '" 2>&1')
        if ($LASTEXITCODE -ne 0) { throw "catch2 compile failed; see $log" }
    }
    foreach ($name in @('ArchiveTests', 'CodecTests', 'LogControlsTests', 'UiPortTests', 'AltTailTests')) {
        $sources = '"tests\' + $name + '.cpp" "' + $catchObj + '"'
        if ($name -eq 'LogControlsTests') {
            $sources += ' lzma\LzmaEnc.c lzma\LzFind.c lzma\CpuArch.c'
            $sources = '/DZ7_ST /Ilzma ' + $sources
        }
        $exe = Join-Path $out ($name + '.exe')
        $log = Join-Path $out ($name + '-compile.log')
        # Included production TUs expose unused static helpers; C4505 is expected.
        $compile = 'cl ' + $common + ' ' + $sources + ' /Fo"' + $out + '\\" /Fe"' + $exe + '" > "' + $log + '" 2>&1'
        cmd /d /c ('call "' + $vcvars + '" >nul && ' + $compile)
        if ($LASTEXITCODE -ne 0) { throw "$name compile failed; see $log" }
        Write-Host "== $name =="
        if ($name -eq 'LogControlsTests') {
            $env:H3BS_TEST_FIXTURE = Join-Path $out ([guid]::NewGuid().ToString('N'))
            & $exe
        } else { & $exe }
        if ($LASTEXITCODE -ne 0) { throw "$name failed: $LASTEXITCODE" }
    }
} finally { Pop-Location }
Write-Host 'PASS all unit tests (Catch2, no game, no hooks, no real clipboard)'
