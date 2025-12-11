# Hardware Documentation

## 🔌 Complete Pin Mapping

### nRF52833 → DW3000 Connections (Verified)

| Function | nRF52833 Pin | DW3000 Pin | Status | Notes |
|----------|--------------|------------|--------|-------|
| **SPI SCK** | P0.31 | SPICLOCK | ✅ Working | 2MHz verified |
| **SPI MOSI** | P0.30 | SPIMOSI | ✅ Working | TX working |
| **SPI MISO** | P0.28 | SPIMISO | ✅ Working | RX working |
| **CS** | P0.02 | SPICSn | ✅ Working | Manual control |
| **RESET** | P0.29 | RSTn | ✅ Working | 10ms+50ms delays |
| **IRQ** | P0.24 | IRQ | ⚠️ Not used | Polling mode |
| **LED** | P0.05 | - | ✅ Working | Active Low |
| **VCC** | 3.3V | VDDIO/VDD | ✅ Working | 40mA TX |
| **GND** | GND | VSS | ✅ Working | Common ground |

### ⚠️ Critical Pin Conflicts Resolved

**Problem:** nRF52833 Dongle default configuration uses P0.30/P0.31 for UART0 console.

**Solution:** UART0 disabled in device tree overlay:

```dts
&uart0 {
    status = "disabled";  // Frees P0.30/P0.31 for SPI3
};
```

**Consequence:** RTT must be used for logging (configured and working).

---

## 📐 Hardware Design Notes

### DW3000 Power Requirements

```
VDD:     3.3V ± 10%  (Core logic)
VDDIO:   3.3V        (I/O voltage)
VDD3V3:  3.3V        (RF PA supply)

Current:
  Sleep:    <1µA
  Idle:     ~5mA
  RX:       ~15mA
  TX:       ~40mA @ default power
```

**Recommendation:** Add decoupling capacitors:
- 10µF + 100nF near VDD3V3
- 100nF near VDD, VDDIO

### SPI Interface Specifications

```
Mode:          0 (CPOL=0, CPHA=0)
Frequency:     Max 38MHz (we use 2MHz for stability)
Word Size:     8 bits
Bit Order:     MSB first
CS Timing:     5ns setup, 5ns hold (we add k_busy_wait())
```

### Reset Timing (From DW3000 Datasheet)

```
RST Low:      >10ms  (hardware reset pulse)
RST → Init:   >50ms  (stabilization time)
Total:        ~60ms  (implemented in platform_port.c)
```

---

## 🔧 Schematic Reference

### Minimal Working Circuit

```
    nRF52833 Dongle              DW3000 Module
   ┌─────────────┐              ┌──────────────┐
   │             │              │              │
   │ P0.31 (SCK) ├─────────────►│ SPICLOCK     │
   │ P0.30 (MOSI)├─────────────►│ SPIMOSI      │
   │ P0.28 (MISO)│◄─────────────┤ SPIMISO      │
   │ P0.02 (CS)  ├─────────────►│ SPICSn       │
   │ P0.29 (RST) ├─────────────►│ RSTn         │
   │ P0.24 (IRQ) │◄─────────────┤ IRQ          │ (optional)
   │             │              │              │
   │ 3.3V        ├──────┬───────┤ VDD/VDDIO    │
   │             │      │       │              │
   │ GND         ├──────┴───────┤ VSS          │
   │             │              │              │
   └─────────────┘              └──────────────┘
        │
        │ P0.05
        ▼
       LED (Active Low)
```

---

## 🧪 Hardware Verification Steps

### 1. Power Check
```powershell
# Check current draw
# Expected: ~40mA during TX, ~5mA idle
```

### 2. SPI Loopback Test (Optional)
```c
// In main.c, enable raw_spi_test() to verify SPI working
raw_spi_test();  // Reads Device ID register
```

### 3. Device ID Verification
```
Expected RTT Output:
RAW SPI TEST: Reading Device ID (Reg 0x00)...
SPI Read Success! Data: 00 02 03 CA DE
Device ID: 0xDECA0302  ✅ SUCCESS!
```

### 4. LED Check
```
Expected: LED on P0.05 blinks every 1 second
If not blinking: Check LED polarity (Active Low)
```

---

## 🛠️ Debugging Hardware Issues

### Device ID reads 0x00000000

**Possible Causes:**
1. SPI not connected properly
2. DW3000 not powered (check 3.3V)
3. CS pin incorrect
4. Wrong SPI mode
5. Timing issues

**Diagnostic Steps:**
```powershell
# 1. Check nRF52833 power
nrfjprog --readcode 0x0000C000 --n 16  # Should return valid data

# 2. Check SPI pins (use multimeter)
# P0.31, P0.30, P0.28 should toggle during boot

# 3. Check CS pin (P0.02)
# Should be high (3.3V) when idle, pulse low during SPI
```

### Device ID reads 0xFFFFFFFF

**Cause:** MISO not connected or floating

**Fix:** Check MISO wire (P0.28)

### No RTT Output

**Cause:** USB/JLink not connected

**Fix:**
```powershell
nrfjprog --ids  # Should show serial number
```

---

## 📊 Tested Hardware Configurations

| Board | DW3000 | Status | Notes |
|-------|--------|--------|-------|
| nRF52833 Dongle | C0 (0xDECA0302) | ✅ Working | Current config |
| nRF52 DK | C0 | ⚠️ Untested | Pin mapping needed |

---

## 🔌 Alternative Pin Configurations

If P0.30/P0.31 are needed for UART, alternative SPI pins:

```dts
// Option: Use SPI1 instead of SPI3
&spi1 {
    status = "okay";
    sck-pin = <3>;    // P0.03
    mosi-pin = <4>;   // P0.04
    miso-pin = <5>;   // P0.05
};
```

**Note:** Requires changing CS, RST, IRQ pins accordingly.

---

**Hardware Documentation Version:** 1.0.1  
**Last Verified:** December 11, 2024  
**Test Hardware:** nRF52833 Dongle + DW3000 C0
