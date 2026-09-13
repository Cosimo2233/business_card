param(
    [string]$ToolchainBin = 'D:\MRS2\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin'
)
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$outputDir = Join-Path $projectDir 'build'
$compiler = Join-Path $ToolchainBin 'riscv-none-embed-gcc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw "找不到 WCH 编译器，请用 -ToolchainBin 指定 bin 目录：$compiler" }
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$flags = @('-march=rv32imac','-mabi=ilp32','-mcmodel=medany','-msmall-data-limit=8',
    '-mno-save-restore','-Os','-g','-fsigned-char','-ffunction-sections','-fdata-sections','-fno-common',
    "-I$projectDir\StdPeriphDriver\inc", "-I$projectDir\RVMSIS")
$sources = @('src/Main.c','Startup/startup_CH583.S','RVMSIS/core_riscv.c',
    'StdPeriphDriver/CH58x_sys.c','StdPeriphDriver/CH58x_clk.c',
    'StdPeriphDriver/CH58x_gpio.c','StdPeriphDriver/CH58x_adc.c')
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $outputDir (($source -replace '[/\\]','_') + '.o')
    $extra = @()
    if ($source -eq 'src/Main.c') { $extra = @('-Wall','-Wextra','-Werror','-std=gnu99') }
    & $compiler @flags @extra -c (Join-Path $projectDir $source) -o $object
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
    $objects += $object
}
$elf = Join-Path $outputDir 'CH582M.elf'
& $compiler @flags -nostartfiles -T "$projectDir\Ld\Link.ld" '-Wl,--gc-sections' `
    "-Wl,-Map,$outputDir\CH582M.map" '-Wl,--print-memory-usage' --specs=nano.specs --specs=nosys.specs `
    @objects "-L$projectDir\StdPeriphDriver" -lISP583 -lm -o $elf
if ($LASTEXITCODE -ne 0) { throw '链接失败' }
& (Join-Path $ToolchainBin 'riscv-none-embed-objcopy.exe') -O ihex $elf "$outputDir\CH582M.hex"
if ($LASTEXITCODE -ne 0) { throw 'HEX 导出失败' }
& (Join-Path $ToolchainBin 'riscv-none-embed-objcopy.exe') -O binary $elf "$outputDir\CH582M.bin"
if ($LASTEXITCODE -ne 0) { throw 'BIN 导出失败' }
& (Join-Path $ToolchainBin 'riscv-none-embed-size.exe') $elf
Write-Host "固件已生成：$outputDir\CH582M.hex"
