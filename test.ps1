param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$outputDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$testExe = Join-Path $outputDir 'firmware_test.exe'
& $Compiler -std=c99 -Wall -Wextra -Werror "-I$PSScriptRoot\src" "-I$PSScriptRoot\StdPeriphDriver\inc" `
    "$PSScriptRoot\tests\firmware_test.c" -o $testExe
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'Firmware tests failed' }
