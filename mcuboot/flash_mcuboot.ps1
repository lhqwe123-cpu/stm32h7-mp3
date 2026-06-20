# ============================================================
# MCUboot Build & Flash Script (STM32H743IIT6)
#
# Usage:
#   .\flash_mcuboot.ps1              # 编译并烧录
#   .\flash_mcuboot.ps1 build        # 仅编译
#   .\flash_mcuboot.ps1 flash        # 仅烧录（需已编译）
#   .\flash_mcuboot.ps1 clean        # 清理
# ============================================================

param([string]$Cmd = "all")

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = "$ScriptDir\build"
$Board = "stm32h743iit6"
$ZephyrEnv = "E:\zephyrproject\external\zephyr\zephyr-env.cmd"

# Helper: run a command inside Zephyr environment via cmd
function Invoke-ZephyrCmd {
    param([string]$Command)
    $Script = @"
@echo off
call "$ZephyrEnv"
cd /d "$ScriptDir"
$Command
"@
    $TempFile = [System.IO.Path]::GetTempFileName() + ".bat"
    $Script | Out-File -Encoding ASCII $TempFile
    cmd /c $TempFile
    $ExitCode = $LASTEXITCODE
    Remove-Item $TempFile -Force -ErrorAction SilentlyContinue
    if ($ExitCode -ne 0) {
        Write-Host "Command failed (exit code $ExitCode)"
        exit $ExitCode
    }
}

switch ($Cmd) {
    "clean" {
        Write-Host "Cleaning..."
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        Write-Host "Done."
        exit 0
    }
    "build" {
        Write-Host "=== Building MCUboot for $Board ==="
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        Invoke-ZephyrCmd "west build -b $Board . -d `"$BuildDir`""
        Write-Host "Build done: $BuildDir\zephyr\zephyr.hex"
    }
    "flash" {
        Write-Host "=== Flashing MCUboot ==="
        $BinFile = "$BuildDir\zephyr\zephyr.bin"
        if (-not (Test-Path $BinFile)) {
            Write-Host "ERROR: $BinFile not found. Run 'build' first."
            exit 1
        }
        $JLinkExe = "C:\Program Files\SEGGER\JLink_V942\JLink.exe"
        if (-not (Test-Path $JLinkExe)) { $JLinkExe = "C:\Program Files\SEGGER\JLink\JLink.exe" }
        if (-not (Test-Path $JLinkExe)) { Write-Host "ERROR: JLink.exe not found"; exit 1 }
        $Device = "STM32H743II"
        $Script = @"
r
h
loadbin $BinFile, 0x08000000
r
h
verifybin $BinFile, 0x08000000
r
q
"@
        $ScriptFile = "$env:TEMP\jlink_flash_mcuboot.jlink"
        $Script | Out-File -Encoding ASCII $ScriptFile
        & $JLinkExe -device $Device -if SWD -speed 4000 -autoconnect 1 $ScriptFile
        Remove-Item $ScriptFile -ErrorAction SilentlyContinue
        Write-Host "Flash done."
    }
    "all" {
        Write-Host "=== Building MCUboot for $Board ==="
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        Invoke-ZephyrCmd "west build -b $Board . -d `"$BuildDir`""
        Write-Host "Build done."
        Write-Host "=== Flashing MCUboot ==="
        $BinFile = "$BuildDir\zephyr\zephyr.bin"
        $JLinkExe = "C:\Program Files\SEGGER\JLink_V942\JLink.exe"
        if (-not (Test-Path $JLinkExe)) { $JLinkExe = "C:\Program Files\SEGGER\JLink\JLink.exe" }
        if (-not (Test-Path $JLinkExe)) { Write-Host "ERROR: JLink.exe not found"; exit 1 }
        $Device = "STM32H743II"
        $Script = @"
r
h
loadbin $BinFile, 0x08000000
r
h
verifybin $BinFile, 0x08000000
r
q
"@
        $ScriptFile = "$env:TEMP\jlink_flash_mcuboot.jlink"
        $Script | Out-File -Encoding ASCII $ScriptFile
        & $JLinkExe -device $Device -if SWD -speed 4000 -autoconnect 1 $ScriptFile
        Remove-Item $ScriptFile -ErrorAction SilentlyContinue
        Write-Host "Flash done."
    }
    default {
        Write-Host "Usage: .\flash_mcuboot.ps1 [build|flash|all|clean]"
    }
}
