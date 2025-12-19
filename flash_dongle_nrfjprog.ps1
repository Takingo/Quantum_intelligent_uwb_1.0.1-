#!/usr/bin/env pwsh
# UWB Tag Firmware - nrfjprog Flash Script for nRF52833 Dongle
# Usage:
#   .\flash_dongle_nrfjprog.ps1
#   .\flash_dongle_nrfjprog.ps1 -Snr <JLINK_SNR>

param(
    [string]$Snr
)

$ErrorActionPreference = "Stop"

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "UWB Tag - Flash (nRF52833 Dongle)" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "NOTE: This method requires an SWD debugger (J-Link) connected to the dongle." -ForegroundColor Yellow
Write-Host "If you are only using the dongle over USB with no debugger, use a DFU/bootloader-based method instead." -ForegroundColor Yellow
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

try {
    # Check if nrfjprog is available
    $nrfjprog = (Get-Command "nrfjprog.exe" -ErrorAction SilentlyContinue).Source
    
    if (-not $nrfjprog) {
        Write-Host "ERROR: nrfjprog.exe not found!" -ForegroundColor Red
        Write-Host "Please install nRF Command Line Tools from:" -ForegroundColor Yellow
        Write-Host "  https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools" -ForegroundColor Cyan
        exit 1
    }
    
    Write-Host "Using nrfjprog: $nrfjprog" -ForegroundColor Green
    Write-Host ""

    # Auto-pick debugger SNR if only one is connected
    if (-not $Snr) {
        $idsRaw = (& nrfjprog -i 2>$null) | Where-Object { $_ -and $_.Trim().Length -gt 0 }
        $ids = @($idsRaw | ForEach-Object { $_.Trim() })
        if ($ids.Count -eq 1) {
            $Snr = $ids[0]
        }
    }

    if ($Snr) {
        Write-Host "Using debugger SNR: $Snr" -ForegroundColor Green
        Write-Host ""
    } else {
        Write-Host "NOTE: No debugger SNR specified." -ForegroundColor Yellow
        Write-Host "If you have multiple J-Links connected, pass -Snr <id> from 'nrfjprog -i'." -ForegroundColor Yellow
        Write-Host ""
    }
    
    # Erase the chip
    Write-Host "Erasing chip..." -ForegroundColor Yellow
    if ($Snr) {
        & nrfjprog --snr $Snr --family NRF52 --eraseall
    } else {
        & nrfjprog --family NRF52 --eraseall
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to erase chip!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Chip erased successfully" -ForegroundColor Green
    Write-Host ""
    
    # Program the hex file
    Write-Host "Programming firmware..." -ForegroundColor Yellow
    if ($Snr) {
        & nrfjprog --snr $Snr --family NRF52 --program $hexFile --verify
    } else {
        & nrfjprog --family NRF52 --program $hexFile --verify
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to program firmware!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Firmware programmed successfully" -ForegroundColor Green
    Write-Host ""
    
    # Reset the chip
    Write-Host "Resetting chip..." -ForegroundColor Yellow
    if ($Snr) {
        & nrfjprog --snr $Snr --family NRF52 --reset
    } else {
        & nrfjprog --family NRF52 --reset
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "WARNING: Failed to reset chip" -ForegroundColor Yellow
    } else {
        Write-Host "Chip reset successfully" -ForegroundColor Green
    }
    
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host "FLASH SUCCESSFUL!" -ForegroundColor Green
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Device is now running the firmware!" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "To view logs, use:" -ForegroundColor Yellow
    Write-Host "  JLinkRTTClient" -ForegroundColor White
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host "FLASH FAILED!" -ForegroundColor Red
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red

    Write-Host "" 
    Write-Host "Troubleshooting tips:" -ForegroundColor Yellow
    Write-Host "- Run 'nrfjprog -i' and retry with -Snr <id>." -ForegroundColor Yellow
    Write-Host "- Close JLinkRTTClient / Ozone / any app using the J-Link." -ForegroundColor Yellow
    Write-Host "- Verify the J-Link is wired to the dongle SWD pins and the target is powered (VTref present)." -ForegroundColor Yellow
    Write-Host "- Unplug/replug the J-Link USB and retry." -ForegroundColor Yellow
    exit 1
}
