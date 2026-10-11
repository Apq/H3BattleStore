$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out = Join-Path $PSScriptRoot 'unit-tmp'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat'
Push-Location $root
try {
    foreach ($name in @('ArchiveTests', 'CodecTests', 'LogControlsTests', 'UiPortTests', 'AltTailTests')) {
        $sources = '"tests\' + $name + '.cpp"'
        $defines = '/D_CRT_SECURE_NO_WARNINGS'
        if ($name -eq 'LogControlsTests') {
            $sources += ' lzma\LzmaEnc.c lzma\LzFind.c lzma\CpuArch.c'
            $defines += ' /DZ7_ST /Ilzma'
        }
        $exe = Join-Path $out ($name + '.exe')
        $log = Join-Path $out ($name + '-compile.log')
        # Included production TUs expose unused static helpers; C4505 is expected.
        $compile = 'cl /nologo /EHsc /std:c++17 /utf-8 /W4 /wd4505 /O2 ' + $defines + ' ' + $sources +
            ' /Fo"' + $out + '\\" /Fe"' + $exe + '" user32.lib > "' + $log + '" 2>&1'
        cmd /d /c ('call "' + $vcvars + '" >nul && ' + $compile)
        if ($LASTEXITCODE -ne 0) { throw "$name compile failed; see $log" }
        if ($name -eq 'UiPortTests' -or $name -eq 'LogControlsTests') {
            $fixture = Join-Path $out ([guid]::NewGuid().ToString('N'))
            & $exe $fixture
        } else { & $exe }
        if ($LASTEXITCODE -ne 0) { throw "$name failed: $LASTEXITCODE" }
    }
} finally { Pop-Location }
Write-Host 'PASS all unit tests (no game, no hooks, no real clipboard)'
