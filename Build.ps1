param(
    [switch]$Tests,
    [string]$VcVars = ''
)
$ErrorActionPreference = 'Stop'
# Discover an existing MSVC installation; never install a toolchain.
if ([string]::IsNullOrWhiteSpace($VcVars)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $found = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Auxiliary/Build/vcvars64.bat')
        if ($LASTEXITCODE -ne 0) { throw "vswhere failed: $LASTEXITCODE" }
        if ($found.Count) { $VcVars = [string]$found[0] }
    }
}
if ([string]::IsNullOrWhiteSpace($VcVars) -or !(Test-Path -LiteralPath $VcVars)) {
    throw 'MSVC x64 tools not found. Use -VcVars with the path to an existing vcvars64.bat.'
}
$VcVars = (Resolve-Path -LiteralPath $VcVars).Path
$root = $PSScriptRoot
$obj = Join-Path $root 'build\obj'
New-Item -ItemType Directory -Path $obj -Force | Out-Null
$tempRoot = [IO.Path]::GetTempPath()
$cmd = Join-Path $tempRoot ("strata-monitor-build-{0}.cmd" -f [Guid]::NewGuid().ToString('N'))
$flags = '/nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00'
# Keep assertions while removing the build-machine prefix from __FILE__ diagnostics.
# Double the final separator so CL receives one before the closing quote.
$trimPrefix = $root.TrimEnd([char]92) + [char]92 + [char]92
$flags += (' /d1trimfile:"{0}"' -f $trimPrefix)
$lines = @(
    '@echo off',
    ('call "{0}" >nul' -f $VcVars),
    'if errorlevel 1 exit /b 1',
    ('cd /d "{0}"' -f $root),
    'rc /nologo /fo "build\obj\monitor.res" resources.rc',
    'if errorlevel 1 exit /b 1',
    ('cl {0} /Fo"build\obj\\" /Fe"StrataMonitor.exe" src\main.cpp src\client.cpp src\view.cpp src\settings.cpp src\service.cpp src\launcher.cpp build\obj\monitor.res /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /MANIFEST:EMBED /MANIFESTINPUT:app.manifest user32.lib gdi32.lib' -f $flags),
    'if errorlevel 1 exit /b 1'
)
if ($Tests) {
    $lines += ('cl {0} /Fo"build\obj\\" /Fe"build\model_test.exe" tests\model_test.cpp /link /SUBSYSTEM:CONSOLE' -f $flags)
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += ('cl {0} /Fo"build\obj\\" /Fe"build\client_test.exe" tests\client_test.cpp src\client.cpp /link /SUBSYSTEM:CONSOLE user32.lib' -f $flags)
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += ('cl {0} /Fo"build\obj\\" /Fe"build\view_test.exe" tests\view_test.cpp src\view.cpp /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib' -f $flags)
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += ('cl {0} /Fo"build\obj\\" /Fe"build\updates_test.exe" tests\updates_test.cpp src\view.cpp /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib' -f $flags)
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += ('cl {0} /Fo"build\obj\\" /Fe"build\launcher_test.exe" tests\launcher_test.cpp src\launcher.cpp src\service.cpp src\view.cpp /link user32.lib gdi32.lib' -f $flags)
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += ('cl {0} /Fo"build\obj\\" /Fe"build\stop_test.exe" tests\stop_test.cpp src\view.cpp /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib' -f $flags)
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += 'build\stop_test.exe'
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += 'build\launcher_test.exe'
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += 'build\updates_test.exe'
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += 'build\view_test.exe'
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += 'build\model_test.exe'
    $lines += 'if errorlevel 1 exit /b 1'
}
$lines += 'exit /b 0'
try {
    [IO.File]::WriteAllLines($cmd, $lines, [Text.Encoding]::Default)
    & $env:ComSpec /d /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    Write-Host "Built: $root\StrataMonitor.exe"
} finally {
    try { [IO.File]::Delete($cmd) } catch { Write-Warning "Temporary build script retained: $cmd" }
}
