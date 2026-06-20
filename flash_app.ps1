# ============================================================
# App Build, Sign & Flash Script (STM32H743IIT6)
#
# Usage:
#   .\flash_app.ps1                  # 编译 + 签名 + 烧录
#   .\flash_app.ps1 build            # 仅编译
#   .\flash_app.ps1 sign             # 仅签名（需已编译）
#   .\flash_app.ps1 flash            # 仅烧录（需已签名）
#   .\flash_app.ps1 clean            # 清理
# ============================================================

param(
    [string]$Cmd = "all",
    [string]$Version = "0.0.1"
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = "$ScriptDir\build"
$Board = "stm32h743iit6"
$KeyFile = "$ScriptDir\mcuboot\keys\root-ec-p256.pem"
$SignedBin = "$BuildDir\zephyr\zephyr.signed.bin"
$SignedHex = "$BuildDir\zephyr\zephyr.signed.hex"
$FwpkgFile = "$BuildDir\zephyr\zephyr.signed_v$Version.fwpkg"
$FwpkgScript = "$ScriptDir\bootloader\scrpt\fwpkg_pack.py"
$ZephyrEnv = "E:\zephyrproject\external\zephyr\zephyr-env.cmd"
$ObjCopy = "E:\zephyrproject\zephyr-sdk-1.0.1\gnu\arm-zephyr-eabi\bin\arm-zephyr-eabi-objcopy.exe"

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
        Write-Host "=== Building app for $Board ==="
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        Invoke-ZephyrCmd "west build -b $Board . -d `"$BuildDir`""
        Write-Host "Build done: $BuildDir\zephyr\zephyr.bin"

        # Auto sign after build
        Write-Host "=== Auto signing app v$Version ==="
        Invoke-ZephyrCmd "imgtool sign --key `"$KeyFile`" --header-size 0x400 --align 8 --version $Version --slot-size 0xC0000 --load-addr 0x08040000 `"$BuildDir\zephyr\zephyr.bin`" `"$SignedBin`""
        Write-Host "Signed: $SignedBin"
        & $ObjCopy -I binary -O ihex --change-addresses 0x08040000 $SignedBin $SignedHex
        Write-Host "Debug hex: $SignedHex"

        # Pack .fwpkg
        Write-Host "=== Packing fwpkg v$Version ==="
        $FwpkgFile = "$BuildDir\zephyr\zephyr.signed_v$Version.fwpkg"
        python "$FwpkgScript" pack "$SignedBin" $Version "$FwpkgFile"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: fwpkg pack failed (exit code $LASTEXITCODE)"
        } else {
            Write-Host "Fwpkg: $FwpkgFile"
        }
    }
    "sign" {
        if (-not (Test-Path "$BuildDir\zephyr\zephyr.bin")) {
            Write-Host "ERROR: zephyr.bin not found. Run 'build' first."
            exit 1
        }
        Write-Host "=== Signing app v$Version ==="
        Invoke-ZephyrCmd "imgtool sign --key `"$KeyFile`" --header-size 0x400 --align 8 --version $Version --slot-size 0xC0000 --load-addr 0x08040000 `"$BuildDir\zephyr\zephyr.bin`" `"$SignedBin`""
        Write-Host "Signed: $SignedBin"
        # Generate signed.hex for debugging
        & $ObjCopy -I binary -O ihex --change-addresses 0x08040000 $SignedBin $SignedHex
        Write-Host "Debug hex: $SignedHex"

        # Pack .fwpkg (version matches signing version)
        Write-Host "=== Packing fwpkg v$Version ==="
        $FwpkgFile = "$BuildDir\zephyr\zephyr.signed_v$Version.fwpkg"
        python "$FwpkgScript" pack "$SignedBin" $Version "$FwpkgFile"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: fwpkg pack failed (exit code $LASTEXITCODE)"
        } else {
            Write-Host "Fwpkg: $FwpkgFile"
        }
    }
    "flash" {
        if (-not (Test-Path $SignedBin)) {
            Write-Host "ERROR: $SignedBin not found. Run 'sign' first."
            exit 1
        }
        Write-Host "=== Flashing app to slot0 (0x08040000) ==="
        $JLinkExe = "C:\Program Files\SEGGER\JLink_V942\JLink.exe"
        if (-not (Test-Path $JLinkExe)) { $JLinkExe = "C:\Program Files\SEGGER\JLink\JLink.exe" }
        if (-not (Test-Path $JLinkExe)) { $JLinkExe = (Get-Command JLink.exe -ErrorAction SilentlyContinue).Source }
        if (-not $JLinkExe) { Write-Host "ERROR: JLink.exe not found"; exit 1 }
        $Device = "STM32H743XI"
        $Script = @"
r
h
erase 0x08040000, 0x080FFFFF
loadbin $SignedBin, 0x08040000
r
g
q
"@
        $ScriptFile = "$env:TEMP\jlink_flash_app.jlink"
        $Script | Out-File -Encoding ASCII $ScriptFile
        & $JLinkExe -device $Device -if SWD -speed 12000 -autoconnect 1 $ScriptFile
        Remove-Item $ScriptFile -ErrorAction SilentlyContinue
        Write-Host "Flash done."
    }
    "all" {
        # Build
        Write-Host "=== Building app for $Board ==="
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
        Invoke-ZephyrCmd "west build -b $Board . -d `"$BuildDir`""

        # Sign
        Write-Host "=== Signing app v$Version ==="
        Invoke-ZephyrCmd "imgtool sign --key `"$KeyFile`" --header-size 0x400 --align 8 --version $Version --slot-size 0xC0000 --load-addr 0x08040000 `"$BuildDir\zephyr\zephyr.bin`" `"$SignedBin`""
        # Generate signed.hex for debugging
        & $ObjCopy -I binary -O ihex --change-addresses 0x08040000 $SignedBin $SignedHex

        # Pack .fwpkg (version matches signing version)
        Write-Host "=== Packing fwpkg v$Version ==="
        $FwpkgFile = "$BuildDir\zephyr\zephyr.signed_v$Version.fwpkg"
        python "$FwpkgScript" pack "$SignedBin" $Version "$FwpkgFile"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: fwpkg pack failed (exit code $LASTEXITCODE)"
        } else {
            Write-Host "Fwpkg: $FwpkgFile"
        }

        # Flash
        Write-Host "=== Flashing app to slot0 (0x08040000) ==="
        $JLinkExe = "C:\Program Files\SEGGER\JLink_V942\JLink.exe"
        if (-not (Test-Path $JLinkExe)) { $JLinkExe = "C:\Program Files\SEGGER\JLink\JLink.exe" }
        if (-not (Test-Path $JLinkExe)) { $JLinkExe = (Get-Command JLink.exe -ErrorAction SilentlyContinue).Source }
        if (-not $JLinkExe) { Write-Host "ERROR: JLink.exe not found"; exit 1 }
        $Device = "STM32H743XI"
        $Script = @"
r
h
erase 0x08040000, 0x080FFFFF
loadbin $SignedBin, 0x08040000
r
g
q
"@
        $ScriptFile = "$env:TEMP\jlink_flash_app.jlink"
        $Script | Out-File -Encoding ASCII $ScriptFile
        & $JLinkExe -device $Device -if SWD -speed 12000 -autoconnect 1 $ScriptFile
        Remove-Item $ScriptFile -ErrorAction SilentlyContinue
        Write-Host "All done."
    }
    default {
        Write-Host "Usage: .\flash_app.ps1 [build|sign|flash|all|clean] [-Version 1.0.0]"
    }
}
