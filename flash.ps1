#!/usr/bin/env pwsh
# UWB Tag Firmware - Flash Script
# Usage: .\flash.ps1

$ErrorActionPreference = "Stop"

Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "UWB Tag Firmware - Flash Script" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host ""

# Check if build directory exists
if (-not (Test-Path "build\zephyr\zephyr.hex")) {
    Write-Host "ERROR: Build artifacts not found!" -ForegroundColor Red
    Write-Host "Please run .\build.ps1 first" -ForegroundColor Red
    exit 1
}

Write-Host "Flashing firmware..." -ForegroundColor Yellow
Write-Host ""

try {
    west flash -d build
    
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host "FLASH SUCCESSFUL!" -ForegroundColor Green
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host ""
    
} catch {
    Write-Host ""
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host "FLASH FAILED!" -ForegroundColor Red
    Write-Host "=======================================" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
