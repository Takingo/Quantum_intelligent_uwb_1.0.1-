#!/usr/bin/env pwsh
# UWB Tag Firmware - nrfjprog Flash Script for nRF52833 Dongle
# Usage: .\flash_dongle_nrfjprog.ps1

$ErrorActionPreference = "Stop"

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "UWB Tag - Flash (nRF52833 Dongle)" -ForegroundColor Cyan
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
    
    # Erase the chip
    Write-Host "Erasing chip..." -ForegroundColor Yellow
    & nrfjprog --family NRF52 --eraseall
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to erase chip!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Chip erased successfully" -ForegroundColor Green
    Write-Host ""
    
    # Program the hex file
    Write-Host "Programming firmware..." -ForegroundColor Yellow
    & nrfjprog --family NRF52 --program $hexFile --verify
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to program firmware!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Firmware programmed successfully" -ForegroundColor Green
    Write-Host ""
    
    # Reset the chip
    Write-Host "Resetting chip..." -ForegroundColor Yellow
    & nrfjprog --family NRF52 --reset
    
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
    exit 1
}
