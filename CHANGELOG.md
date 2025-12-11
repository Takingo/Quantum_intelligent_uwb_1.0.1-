# Changelog

All notable changes to Quantum Intelligent UWB Tag firmware will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [1.0.1] - 2024-12-11

### ✅ Fixed
- **LED Pin Correction**: Changed from P0.06 to P0.05 (verified working)
- **CS GPIO Logic**: Fixed ACTIVE_LOW handling in platform_port.c
- **SPI Timing**: Added k_busy_wait() delays for proper CS setup/hold
- **Device Tree**: Corrected overlay syntax (resolved 53+ errors)
- **Pin Conflict**: Disabled UART0 to free P0.30/P0.31 for SPI3

### 🎉 Verified
- DW3000 Device ID: 0xDECA0302 (C0 silicon)
- SPI3 @ 2MHz communication stable
- IEEE 802.15.4 BLINK frames transmitting every 500ms
- LED status indicator blinking at 1Hz
- 24+ hour continuous operation stable

### 📚 Documentation
- Comprehensive README.md with hardware pinout
- Troubleshooting guide
- Quick start documentation
- Technical specifications

---

## [1.0.0] - 2024-12-10

### 🚀 Initial Release
- nRF52833 + DW3000 integration
- Zephyr RTOS v3.5.99
- SPI3 driver implementation
- Basic UWB frame transmission
- RTT logging support
- PowerShell build scripts

### 🔧 Hardware Support
- nRF52833 Dongle board
- Qorvo DW3000 UWB transceiver
- J-Link programmer support

### 📦 Features
- IEEE 802.15.4 BLINK frame format
- Manual CS/RST control
- LED status feedback
- SEGGER RTT debugging
- West build system integration

---

**Key:** 
- 🚀 New features
- ✅ Fixed bugs
- 🎉 Verified working
- 📚 Documentation
- 🔧 Hardware
- 📦 Features
