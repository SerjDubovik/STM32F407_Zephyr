[CmdletBinding()]
param([ValidateSet('Build', 'Flash', 'Setup')] [string]$Action = 'Build',
      [string]$Python = 'python',
      [string]$Toolchain = $env:GNUARMEMB_TOOLCHAIN_PATH)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root '.deps'
$venv = Join-Path $root '.venv\Scripts'
$venvPython = Join-Path $venv 'python.exe'
$west = Join-Path $venv 'west.exe'
$env:ZEPHYR_BASE = Join-Path $deps 'zephyr'
$env:Path = "$venv;$env:Path"

function Invoke-Checked($Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with $LASTEXITCODE" }
}

if ($Action -eq 'Setup') {
    if (!(Test-Path $venvPython)) { Invoke-Checked $Python @('-m','venv', (Join-Path $root '.venv')) }
    if (!(Test-Path $west)) { Invoke-Checked $venvPython @('-m','pip','install','west') }
    New-Item -ItemType Directory -Force $deps | Out-Null
    if (!(Test-Path (Join-Path $env:ZEPHYR_BASE 'west.yml'))) {
        Invoke-Checked git @('-c','core.longpaths=true','clone','--depth','1','--branch','v4.2.1',
            'https://github.com/zephyrproject-rtos/zephyr.git', $env:ZEPHYR_BASE)
    }
    Push-Location $deps
    try {
        if (!(Test-Path '.west\config')) { Invoke-Checked $west @('init','-l','zephyr') }
        Invoke-Checked $west @('update','--narrow','-o=--depth=1','cmsis','cmsis_6','hal_stm32')
        Invoke-Checked $venvPython @('-m','pip','install','-r','zephyr/scripts/requirements-base.txt')
    } finally { Pop-Location }
    return
}

if (!(Test-Path $west) -or !(Test-Path (Join-Path $deps '.west\config'))) {
    throw 'Run the Zephyr Setup task first.'
}
if (!$Toolchain) { $Toolchain = 'C:\VStools\gnu-tools-for-stm32\tools' }
if (!(Test-Path (Join-Path $Toolchain 'bin\arm-none-eabi-gcc.exe'))) {
    throw 'Set GNUARMEMB_TOOLCHAIN_PATH to the Arm GNU toolchain root.'
}
$env:GNUARMEMB_TOOLCHAIN_PATH = $Toolchain
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'gnuarmemb'
$build = Join-Path $root 'build'
Push-Location $deps
try {
    Invoke-Checked $west @('build','-p','auto','-b','devebox_f407vg','-d',$build,$root)
    if ($Action -eq 'Flash') {
        Invoke-Checked $west @('flash','-d',$build,'--skip-rebuild','--runner','openocd',
            '--openocd', (Get-Command openocd -CommandType Application).Source, '--verify')
    }
} finally { Pop-Location }
