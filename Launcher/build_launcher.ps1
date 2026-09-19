# Build Watermark Injection Launcher
$vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$msvcVer = "14.44.35207"
$sdkVer = "10.0.26100.0"
$vctools = "$vsRoot\VC\Tools\MSVC\$msvcVer"
$sdkDir = "C:\Program Files (x86)\Windows Kits\10\"

$env:PATH = "$vctools\bin\Hostx64\x64;$sdkDir\bin\$sdkVer\x64;$vsRoot\MSBuild\Current\Bin;$env:PATH"
$env:INCLUDE = "$vctools\include;$sdkDir\Include\$sdkVer\ucrt;$sdkDir\Include\$sdkVer\um;$sdkDir\Include\$sdkVer\shared;$sdkDir\Include\$sdkVer\winrt"
$env:LIB = "$vctools\lib\x64;$sdkDir\Lib\$sdkVer\ucrt\x64;$sdkDir\Lib\$sdkVer\um\x64"
$env:WINDOWS_SDK_DIR = $sdkDir
$env:WINDOWSSDKDIR = $sdkDir

$outDir = "D:\watermark injecting Version\Launcher"

Write-Output "=== Compiling resources ==="
rc.exe /fo "$outDir\launcher.res" "$outDir\launcher.rc"
if ($LASTEXITCODE -ne 0) { Write-Error "RC failed"; exit 1 }

Write-Output "=== Compiling launcher ==="
# Use a response file: paths contain spaces and PS5.1 native-arg quoting is unreliable
$rspPath = "$outDir\cl_args.rsp"
$clArgs = @(
    '/nologo', '/O2', '/GL', '/EHsc', '/MD', '/utf-8', '/std:c++17',
    "/Fe$outDir\WatermarkInjectionLauncher.exe",
    "/Fe$outDir\WatermarkInjectionLauncher.exe",
    "$outDir\launcher.cpp",
    "$outDir\imgui.cpp",
    "$outDir\imgui_draw.cpp",
    "$outDir\imgui_tables.cpp",
    "$outDir\imgui_widgets.cpp",
    "$outDir\imgui_impl_win32.cpp",
    "$outDir\imgui_impl_opengl3.cpp",
    "$outDir\launcher.res",
    '/link', '/LTCG', '/SUBSYSTEM:WINDOWS', '/MACHINE:X64',
    'shell32.lib', 'user32.lib', 'kernel32.lib', 'gdi32.lib', 'advapi32.lib', 'comctl32.lib', 'opengl32.lib'
)
$clArgs | ForEach-Object { "`"$_`"" } | Out-File -Encoding ascii $rspPath
Push-Location $outDir
& cl.exe "@$rspPath"
Remove-Item $rspPath -ErrorAction SilentlyContinue
Pop-Location

if ($LASTEXITCODE -eq 0) {
    Write-Output "=== Build SUCCESS ==="
    Get-Item "$outDir\WatermarkInjectionLauncher.exe" | Select-Object Name, Length
} else {
    Write-Error "=== Build FAILED ==="
}
