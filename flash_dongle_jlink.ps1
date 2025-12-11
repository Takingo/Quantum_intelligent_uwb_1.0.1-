#!/usr/bin/env pwsh
# UWB Tag Firmware - JLink Flash Script for nRF52833 Dongle
# Usage: .\flash_dongle_jlink.ps1

$ErrorActionPreference = "Stop"

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "UWB Tag - JLink Flash (nRF52833 Dongle)" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""

# Check if build directory exists
if (-not (Test-Path "build\zephyr\zephyr.hex")) {
    Write-Host "ERROR: Build artifacts not found!" -ForegroundColor Red
    Write-Host "Please run .\build.ps1 first" -ForegroundColor Red
    exit 1
}

# Get the hex file path
$hexFile = (Resolve-Path "build\zephyr\zephyr.hex").Path
Write-Host "Firmware: $hexFile" -ForegroundColor Yellow
Write-Host ""

# Create JLink command file
$jlinkScript = @"
device nRF52833_xxAA
si SWD
speed 4000
loadfile "$hexFile"
r
g
qc
"@

$jlinkScriptFile = "$env:TEMP\flash_nrf52833.jlink"
$jlinkScript | Out-File -FilePath $jlinkScriptFile -Encoding ASCII

Write-Host "JLink Script created: $jlinkScriptFile" -ForegroundColor Green
Write-Host ""
Write-Host "Flashing firmware..." -ForegroundColor Yellow
Write-Host ""

try {
    # Try to find JLink executable
    $jlinkExe = $null
    
    # Common JLink installation paths
    $jlinkPaths = @(
        "C:\Program Files\SEGGER\JLink\JLink.exe",
        "C:\Program Files (x86)\SEGGER\JLink\JLink.exe",
        "C:\Program Files\Nordic Semiconductor\nrf-command-line-tools\bin\JLink.exe"
    )
    
    foreach ($path in $jlinkPaths) {
        if (Test-Path $path) {
            $jlinkExe = $path
            break
        }
    }
    
    if (-not $jlinkExe) {
        # Try to find it in PATH
        $jlinkExe = (Get-Command "JLink.exe" -ErrorAction SilentlyContinue).Source
    }
    
    if (-not $jlinkExe) {
        Write-Host "ERROR: JLink.exe not found!" -ForegroundColor Red
        Write-Host "Please install SEGGER JLink tools from:" -ForegroundColor Yellow
        Write-Host "  https://www.segger.com/downloads/jlink/" -ForegroundColor Cyan
        exit 1
    }
    
    Write-Host "Using JLink: $jlinkExe" -ForegroundColor Green
    Write-Host ""
    
    # Run JLink
    & $jlinkExe -CommanderScript $jlinkScriptFile
    
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host "FLASH SUCCESSFUL!" -ForegroundColor Green
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Device programmed successfully!" -ForegroundColor Cyan
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host "FLASH FAILED!" -ForegroundColor Red
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
} finally {
    # Clean up temp file
    if (Test-Path $jlinkScriptFile) {
        Remove-Item $jlinkScriptFile -ErrorAction SilentlyContinue
    }
}
