#!/usr/bin/env pwsh
# UWB Tag Firmware - Build Script
# Usage: .\build.ps1 [board_name] [clean]
#   Example: .\build.ps1 nrf52833dongle_nrf52833
#   Example: .\build.ps1 nrf52833dk_nrf52833 clean

param(
    [string]$Board = "nrf52833dongle_nrf52833",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "UWB Tag Firmware - Build Script" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "Board: $Board" -ForegroundColor Yellow
Write-Host ""

# Check if west is available
try {
    $westVersion = west --version 2>&1
    Write-Host "West version: $westVersion" -ForegroundColor Green
} catch {
    Write-Host "ERROR: 'west' command not found!" -ForegroundColor Red
    Write-Host "Please install NRF Connect SDK and ensure west is in PATH" -ForegroundColor Red
    exit 1
}

# Clean build directory if requested
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path "build") {
        Remove-Item -Recurse -Force build
        Write-Host "Build directory cleaned" -ForegroundColor Green
    }
}

# Build the project
Write-Host ""
Write-Host "Building firmware for $Board..." -ForegroundColor Yellow
Write-Host ""

try {
    west build -b $Board -d build -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Firmware location: build\zephyr\zephyr.hex" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "To flash the firmware, run:" -ForegroundColor Yellow
    Write-Host "  west flash -d build" -ForegroundColor White
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host "BUILD FAILED!" -ForegroundColor Red
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
