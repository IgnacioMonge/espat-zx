# ESPAT-ZX v1.0.0 Release Notes

**Release Date:** December 27, 2025

## 🎉 First Stable Release!

We're excited to announce the first stable release of ESPAT-ZX, a full-featured AT command terminal for the ZX Spectrum with ESP8266/ESP-12 WiFi modules.

---

## ✨ Highlights

### 64-Column Display
Double the text density of standard ZX Spectrum applications with our custom 4-pixel wide font. See more information at a glance without constant scrolling.

### Real-Time Status Bar
Always know your connection status with the persistent status bar showing:
- 📍 IP Address
- 📡 AT Firmware Version  
- 📶 Network Name (SSID)
- 📊 Signal Strength Bars
- 🕐 NTP-Synchronized Time
- 🔴🟢 Connection Status

### Smart Initialization
ESPAT-ZX detects existing WiFi connections and skips unnecessary setup steps. Boot time is approximately **1-1.5 seconds** when already connected to a network.

### Command History
Never retype long commands! Navigate through your last 6 commands using the UP/DOWN arrow keys.

### Full Line Editing
Edit commands anywhere in the line with:
- Left/Right cursor movement
- Home/End navigation
- Insert and delete at any position

### Case-Sensitive Passwords
WiFi passwords with mixed case? No problem! Arguments preserve their original case while commands are auto-uppercased.

---

## 📦 Download

| File | Description |
|------|-------------|
| `ESPATZX.tap` | Ready-to-load tape file for ZX Spectrum |
| `espatzx-zx-1.0.0-src.zip` | Complete source code |

---

## 🔧 Built-in Commands

### Network
| Command | Description |
|---------|-------------|
| `!CONNECT ssid,pass` | Connect to WiFi |
| `!DISCONNECT` | Disconnect |
| `!SCAN` | List networks |
| `!IP` | Show connection info |
| `!PING [host]` | Test connectivity |

### Information
| Command | Description |
|---------|-------------|
| `!TIME` | Sync NTP time |
| `!INFO` | ESP firmware info |
| `!MAC` | Show MAC address |

### System
| Command | Description |
|---------|-------------|
| `!RST` | Reset ESP |
| `!RAW` | Traffic monitor |
| `!DEBUG` | Debug mode |
| `!CLS` | Clear screen |
| `!HELP` | Show help |
| `!ABOUT` | Credits |

Plus full support for direct **AT commands**!

---

## 🛠 System Requirements

### Hardware
- ZX Spectrum 48K or higher
- ESP8266 or ESP-12 WiFi module
- AY-UART interface connection

### Software (for building)
- z88dk cross-compiler
- GNU Make

---

## 📋 Technical Specifications

| Specification | Value |
|---------------|-------|
| Display | 64 columns × 24 rows |
| Font | 4×8 pixels, 256 characters |
| UART | 9600 baud (AY-3-8912 bit-bang) |
| Buffer | 256-byte ring buffer |
| History | 6 commands × 64 characters |
| Input | 80 characters max |

---

## 🐛 Bug Fixes in This Release

- ✅ Fixed parser showing garbage from ESP async messages
- ✅ Fixed uppercase forcing on WiFi passwords
- ✅ Fixed slow backspace deletion
- ✅ Fixed system hangs after extended use
- ✅ Fixed AT version showing trailing dot
- ✅ Fixed WiFi detection during initialization
- ✅ Fixed history navigation visual artifacts

---

## ⚡ Performance Improvements

| Operation | Before | After |
|-----------|--------|-------|
| Cold start (no WiFi) | ~4 sec | ~2 sec |
| Warm start (connected) | ~3 sec | ~1.5 sec |
| Backspace repeat | Slow | 50 char/sec |
| Buffer overflow | Data loss | Protected |

---

## 📝 Known Limitations

- Maximum input line length: 80 characters
- Command history: 6 entries
- SSID display truncated at 14 characters
- Requires ESP AT firmware v1.7 or higher
- 9600 baud only (higher rates may be unstable)

---

## 🙏 Credits

**Author:** M. Ignacio Monge Garcia

**Based on:** esp-terminal by Vasily Khoruzhick (anarsoul)

**Code contributions from:**
- BridgeZX
- NetManZX

**Thanks to:**
- The z88dk team
- The ZX Spectrum community
- Espressif Systems

---

## 📄 License

MIT License - See [LICENSE](LICENSE) for details.

---

## 🔗 Links

- [Full Documentation (English)](README.md)
- [Documentación Completa (Español)](README_ES.md)
- [Changelog](CHANGELOG.md)
- [Source Code](https://github.com/yourusername/espat-zx)

---

<p align="center">
  <b>ESPAT-ZX v1.0.0</b><br>
  <i>Bridging the ZX Spectrum to the modern world</i> 🌐
</p>
