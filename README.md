<p align="center">
  <img src="images/espatzxlogo-white.png" alt="SnapZX Logo" width="640" />
</p>

# ESPAT-ZX: AT Terminal for ZX Spectrum

**Version 1.0** | [Leer en Español](README_ES.md)

A full-featured AT command terminal for the ZX Spectrum with ESP8266/ESP-12 WiFi modules. Features a 64-column display, real-time status bar, command history, and smart initialization.

![Platform](https://img.shields.io/badge/platform-ZX%20Spectrum-blue)
![WiFi](https://img.shields.io/badge/WiFi-ESP8266%2FESP--12-green)
![License](https://img.shields.io/badge/license-MIT-brightgreen)

## Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Installation](#installation)
- [Building from Source](#building-from-source)
- [Usage](#usage)
  - [Built-in Commands](#built-in-commands)
  - [AT Commands](#at-commands)
  - [Keyboard Controls](#keyboard-controls)
- [Status Bar](#status-bar)
- [Technical Details](#technical-details)
- [Origins & Credits](#origins--credits)
- [License](#license)

## Features

### Display
- **64-column text display** using a custom 4-pixel wide font (vs standard 32 columns)
- **Three-zone interface**: Banner (top), Main output area (scrollable), Input zone (bottom)
- **Real-time status bar** showing IP, AT firmware version, SSID, signal strength bars, time, and connection indicator
- **Color-coded output**: User commands (bright white), ESP responses (bright yellow), local messages (bright green), debug info (cyan)
- **Smooth scrolling** in the main output area with automatic line wrapping

### WiFi Management
- **Smart initialization**: Automatically detects existing WiFi connections and skips unnecessary setup steps
- **Visual signal strength indicator**: 4-bar animated RSSI display with color gradient (red to green)
- **Auto-refresh**: Periodically updates connection status and signal strength in the background
- **Network scanning**: List all available WiFi networks with encryption type and signal strength
- **NTP time synchronization**: Get current time from multiple internet time servers (pool.ntp.org, time.google.com)

### Input System
- **Full line editing**: Insert/delete characters anywhere, cursor movement (left/right/home/end)
- **Command history**: Navigate through 6 previous commands with UP/DOWN arrows
- **Smart case handling**: Commands convert to uppercase, arguments preserve original case (important for passwords!)
- **Quoted string support**: Text within quotes always preserves case
- **Fast key repeat**: Optimized backspace with quick initial delay and rapid repeat rate
- **Visual cursor**: Underline cursor clearly shows insertion point

### Communication
- **AY-3-8912 bit-banging UART**: Reliable 9600 baud communication through the sound chip's I/O ports
- **256-byte ring buffer**: Prevents data loss during slow screen operations (scrolling, etc.)
- **Async noise filtering**: Automatically filters out ESP status messages (WIFI CONNECTED, WIFI GOT IP, etc.)
- **Debug mode**: Toggle visibility of all raw ESP communication for troubleshooting

## Hardware Requirements

### Minimum
- **ZX Spectrum** 48K or higher (48K/128K/+2/+2A/+2B/+3)
- **ESP8266 or ESP-12 WiFi module** with AT firmware v1.7+
- **AY-UART interface** connecting ESP to AY-3-8912 I/O ports

### Tested Configurations
- ZX Spectrum 48K + ESP-12F via custom AY-UART board
- ZX Spectrum 128K +2 + ESP8266-01 via joystick port adapter
- ZX Spectrum +3 + ESP-12E with direct wiring

### Wiring Overview

The ESP module connects through the AY-3-8912 sound chip I/O ports. The exact wiring depends on your interface:

| ESP Pin | Function | Notes |
|---------|----------|-------|
| TX | Data to Spectrum | Connect to AY Port A input |
| RX | Data from Spectrum | Connect to appropriate output |
| VCC | Power | 3.3V regulated (NOT 5V!) |
| GND | Ground | Common ground with Spectrum |
| CH_PD/EN | Chip enable | Tie to 3.3V |
| GPIO0 | Boot mode | Leave floating or tie high for normal operation |

> ⚠️ **Warning**: The ESP8266 is a 3.3V device. Using 5V will damage it permanently. Use appropriate level shifters or resistor dividers if needed.

## Installation

### Pre-built Binary

1. Download `ESPATZX.tap` from the [Releases](../../releases) page
2. Load on your ZX Spectrum using your preferred method:

**Real Hardware:**
- **DivMMC/DivIDE**: Copy to SD card, browse and load
- **ESXDOS**: Copy to SD card, use `.tapein` or NMI browser
- **TZXDuino/CASDuino**: Play TAP file as audio

**Emulators:**
- **FUSE**: File → Open and select the TAP file
- **ZEsarUX**: Smart load or tape browser
- **Spectaculator**: File → Open TAP

> 💡 **Tip**: For emulator testing of WiFi features, you'll need hardware emulation or a real ESP module connected via serial port passthrough.

## Building from Source

### Prerequisites

Install the [z88dk](https://github.com/z88dk/z88dk) cross-compiler:

**Ubuntu/Debian:**
```bash
sudo apt-get install z88dk
```

**macOS (Homebrew):**
```bash
brew install z88dk
```

**Windows:**
Download from [z88dk releases](https://github.com/z88dk/z88dk/releases) and add to PATH.

### Compilation

```bash
# Clone the repository
git clone https://github.com/yourusername/espat-zx.git
cd espat-zx

# Build (produces ESPATZX.tap)
make

# Clean build artifacts
make clean
```

### Build Output

```
ESPATZX.tap    - Loadable tape file for ZX Spectrum
ESPATZX.bin        - Raw binary (intermediate)
```

### Source Files

| File | Size | Description |
|------|------|-------------|
| `espatzx_code.c` | ~66KB | Main application source code |
| `ay_uart.asm` | ~4KB | Z80 assembly UART bit-banging driver |
| `font64_data.h` | ~12KB | 4×8 pixel font data (256 characters) |
| `font64.bin` | 2KB | Compiled font binary |
| `Makefile` | ~1KB | Build configuration |

### Compiler Flags

```
zcc +zx        - Target ZX Spectrum
-vn            - No verbose output
-startup=0     - Minimal startup code
-clib=new      - Use new C library
-create-app    - Generate TAP file
```

## Usage

### First Start

When ESPAT-ZX loads, it performs automatic initialization:

```
Initializing...
Probing ESP... OK
Checking connection...
Connected: 192.168.1.100
Ready. Type !HELP, !ABOUT or AT cmds
>_
```

If already connected to WiFi, this takes approximately 1-1.5 seconds. Otherwise, you'll see "No WiFi connection" and can use `!CONNECT` to join a network.

### Built-in Commands

All built-in commands start with `!` and are case-insensitive (arguments preserve case):

#### Network Commands

| Command | Description | Example |
|---------|-------------|---------|
| `!CONNECT ssid,pass` | Connect to WiFi network | `!CONNECT MyWiFi,Secret123` |
| `!DISCONNECT` | Disconnect from current network | `!DISCONNECT` |
| `!SCAN` | Scan for available networks | `!SCAN` |
| `!IP` | Refresh/show connection status | `!IP` |
| `!PING [host]` | Ping a host (default 8.8.8.8) | `!PING google.com` |

**Connection Examples:**
```
> !CONNECT HomeNetwork,MyPassword123
Connecting to 'HomeNetwork'...
WIFI CONNECTED
WIFI GOT IP
Done. Run !IP to check.

> !SCAN
Scanning...
+CWLAP:(3,"HomeNetwork",-45,"aa:bb:cc:dd:ee:ff",1)
+CWLAP:(4,"Neighbor",-78,"11:22:33:44:55:66",6)
OK
```

#### Information Commands

| Command | Description | Output |
|---------|-------------|--------|
| `!INFO` | ESP firmware details | AT version, SDK version, compile date |
| `!MAC` | Module MAC address | `+CIFSR:STAMAC,"aa:bb:cc:dd:ee:ff"` |
| `!TIME` | Sync time via NTP | Updates status bar clock |

#### System Commands

| Command | Description | Notes |
|---------|-------------|-------|
| `!RST` | Hardware reset ESP | Takes a few seconds to reconnect |
| `!BAUD rate` | Change baud rate | Use with caution! |
| `!RAW` | Raw traffic monitor | Press SPACE to exit |
| `!DEBUG` | Toggle debug mode | Shows all ESP traffic |
| `!CLS` | Clear main screen | Keeps status bar |
| `!HELP` / `!?` | Show help (2 pages) | SPACE for page 2 |
| `!ABOUT` | Credits & version | Press any key to exit |

### AT Commands

Type standard ESP8266 AT commands directly (they're sent as-is to the ESP module):

**Basic:**
```
AT                              Test communication (should respond "OK")
AT+RST                          Software reset
AT+GMR                          Version information
```

**WiFi:**
```
AT+CWMODE=1                     Set station mode
AT+CWJAP="SSID","password"      Connect to AP
AT+CWQAP                        Disconnect
AT+CWLAP                        List access points
AT+CIFSR                        Get local IP
```

**TCP/IP:**
```
AT+CIPSTART="TCP","host",port   Open TCP connection
AT+CIPSEND=length               Send data
AT+CIPCLOSE                     Close connection
AT+CIPMUX=0                     Single connection mode
AT+CIPMUX=1                     Multiple connections
```

**Time:**
```
AT+CIPSNTPCFG=1,0,"pool.ntp.org"   Configure NTP
AT+CIPSNTPTIME?                    Get current time
```

### Keyboard Controls

| Key | Action |
|-----|--------|
| **←** (CS+5) | Move cursor left |
| **→** (CS+8) | Move cursor right |
| **↑** (CS+7) | Previous command in history |
| **↓** (CS+6) | Next command in history |
| **DELETE** (CS+0) | Delete character before cursor |
| **ENTER** | Execute command |
| **HOME** (CS+1) | Jump to beginning of line |
| **END** (CS+2) | Jump to end of line |

**Key Repeat Timing:**
- Normal keys: 400ms initial delay (prevents doubles)
- Backspace: 240ms initial delay, then 50 chars/second
- Cursor keys: 300ms initial delay, then 25 moves/second
- History: 400ms initial delay, then 10 items/second

## Status Bar

The status bar provides at-a-glance information about your connection:

```
IP:192.168.1.100 AT:1.7.4 SSID:MyNetwork    ▂▄▆█ 17:30  ●
```

### Fields

| Field | Width | Description |
|-------|-------|-------------|
| IP | 15 chars | Current IP address or "---" |
| AT | 7 chars | ESP AT firmware version |
| SSID | 14 chars | Network name (truncated with ~) |
| Bars | 4 bars | Signal strength visualization |
| Time | 5 chars | HH:MM after NTP sync |
| Indicator | 1 char | Connection status dot |

### Signal Strength Bars

The 4-bar indicator represents RSSI (Received Signal Strength Indicator):

| Bars | RSSI Range | Quality |
|------|------------|---------|
| ▂▄▆█ (4) | > -50 dBm | Excellent |
| ▂▄▆░ (3) | -50 to -60 | Good |
| ▂▄░░ (2) | -60 to -70 | Fair |
| ▂░░░ (1) | -70 to -80 | Weak |
| ░░░░ (0) | < -80 dBm | Very weak |

### Connection Indicator Colors

| Color | Meaning |
|-------|---------|
| 🟢 Green | Connected to WiFi with IP |
| 🔴 Red | Not connected |
| 🟡 Yellow | Checking/connecting |

## Technical Details

### Memory Map

```
0x4000-0x57FF  Screen bitmap (6144 bytes)
0x5800-0x5AFF  Color attributes (768 bytes)
0x5B00-0x5BFF  Ring buffer (256 bytes)
0x5C00+        Application code and data
```

### Display Layout

```
Line 0        Banner "ESPAT-ZX: AT Terminal..."  [Blue background]
Line 1        Separator                          [Black]
Lines 2-19    Main output zone (18 lines)        [Black background]
Line 20       Status bar                         [White background]
Line 21       Separator                          [Black]
Lines 22-24   Input zone (3 lines)               [Green background]
```

### Screen Specifications

| Property | Value |
|----------|-------|
| Columns | 64 (4-pixel characters) |
| Rows | 24 (standard 8-pixel) |
| Font size | 4×8 pixels |
| Colors | 8 colors × bright = 16 |
| Character set | Full 256 ASCII |

### UART Implementation

- **Method**: Software bit-banging via AY-3-8912 I/O ports
- **Baud rate**: 9600 bps (default, configurable)
- **Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Buffer**: 256-byte circular buffer
- **Overflow protection**: Stops reading when buffer is full

### Ring Buffer

```c
#define RING_BUFFER_SIZE 256
static uint8_t ring_buffer[RING_BUFFER_SIZE];
static uint8_t rb_head = 0;  // Write position
static uint8_t rb_tail = 0;  // Read position
```

The ring buffer stores incoming UART data, allowing the main loop to process it at its own pace without losing bytes.

### Timeout Constants

```c
#define TIMEOUT_FAST  25000UL   // Quick queries (AT, CIFSR)
#define TIMEOUT_STD   200000UL  // Normal commands
#define TIMEOUT_LONG  500000UL  // SCAN, CONNECT
```

### Filtered Async Messages

The parser automatically hides these ESP status messages:

- `WIFI CONNECTED`, `WIFI GOT IP`, `WIFI DISCONNECT`
- `CONNECT`, `CLOSED` (TCP connection events)
- `+IPD,...` (incoming data notifications)
- Connection IDs (single/double digits)
- `busy p...`, `busy s...`
- `ready` (after reset)
- Proprietary protocol markers (`LAIN`, `WFXR`)

Use `!DEBUG` to see all messages including filtered ones.

## Origins & Credits

### History

ESPAT-ZX started as a fork of **esp-terminal** by Vasily Khoruzhick (anarsoul), a basic terminal for sending AT commands to an ESP8266 from a ZX Spectrum.

This project significantly expanded the original with:

- Complete UI redesign with 64-column display
- Real-time status bar with multiple data fields
- Signal strength visualization
- Command history with navigation
- Full line editing capabilities
- Smart initialization and auto-detection
- Extensive async message filtering
- NTP time synchronization
- Built-in high-level commands
- Optimized performance and timing

### Code Heritage

Portions of code derived from:

- **esp-terminal** by anarsoul - Original AT terminal concept and AY-UART basics
- **BridgeZX** - WiFi file transfer protocols and ESP communication patterns
- **NetManZX** - Network management routines and status display concepts

### Author

**ESPAT-ZX v1.0**
© 2025 M. Ignacio Monge Garcia

### Acknowledgments

- The z88dk team for the excellent cross-compiler
- The ZX Spectrum community for keeping the platform alive
- Espressif for the ESP8266 and AT firmware

## License

This project is released under the MIT License.

```
MIT License

Copyright (c) 2025 M. Ignacio Monge Garcia

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

<p align="center">
  <b>ESPAT-ZX v1.0</b><br>
  Bridging the ZX Spectrum to the modern world 🌐
</p>
