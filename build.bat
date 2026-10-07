@echo off
chcp 936 >nul
rem 成功后是否停住窗口：0=直接关闭（默认，保持原行为），1=pause 等按键。
rem 双击前可在本文件改默认值；命令行可覆盖：set PAUSE_ON_SUCCESS=1 && build.bat
if not defined PAUSE_ON_SUCCESS set PAUSE_ON_SUCCESS=0
rem 清代理变量：HTTP_PROXY 与 http_proxy 同时存在时，MSBuild 的 .NET 环境字典
rem 键冲突（大小写不敏感）会报 MSB6001，编译直接失败。
set HTTP_PROXY=& set http_proxy=& set HTTPS_PROXY=& set https_proxy=& set ALL_PROXY=& set all_proxy=& set NO_PROXY=& set no_proxy=
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe" H3BattleStore.vcxproj /p:Configuration=Release /p:Platform=Win32 /m %*
if errorlevel 1 (
    pwsh -c "Write-Host '编译失败' -ForegroundColor Red"
    pause
    exit /b 1
)
rem Optional real-patcher ABI tests; all artifacts stay under tests\abi-tmp.
if not "%H3BATTLE_RUN_ABI_TESTS%"=="1" goto :build_success
    call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
    if errorlevel 1 exit /b 1
    if not exist "tests\abi-tmp" mkdir "tests\abi-tmp"
    if errorlevel 1 exit /b 1
    cl /nologo /std:c++20 /O2 /W4 /utf-8 /EHsc /Zp1 /DWIN32 /DNDEBUG /DWINDOWS_IGNORE_PACKING_MISMATCH /D_CRT_SECURE_NO_WARNINGS /DJSON_NOEXCEPTION /wd4018 /wd4267 /wd4553 /wd4005 /wd4996 /wd4235 /wd4010 /I"..\H3API\single_header" /I"third_party" tests\HookAbiTests.cpp /Fo"tests\abi-tmp\HookAbiTests.obj" /Fe"tests\abi-tmp\HookAbiTests.exe" /Fd"tests\abi-tmp\HookAbiTests.pdb" /link /INCREMENTAL:NO /OPT:NOICF /PDB:"tests\abi-tmp\HookAbiTests.pdb"
    if errorlevel 1 exit /b 1
    tests\abi-tmp\HookAbiTests.exe
    rem Native failures may be negative signed exit codes, not errorlevel >= 1.
    if not "%errorlevel%"=="0" exit /b 1
:build_success
pwsh -c "Write-Host '编译完成' -ForegroundColor Green"
if "%PAUSE_ON_SUCCESS%"=="1" pause