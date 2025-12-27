# Changelog

All notable changes to ESPAT-ZX are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2025-12-27

### 🎉 First Stable Release

This is the first stable release of ESPAT-ZX, a complete rewrite and enhancement of the original esp-terminal project.

### Added

#### Display System
- **64-column display**: Custom 4×8 pixel font supporting all 256 ASCII characters
- **Three-zone interface**: 
  - Banner zone (line 0) with title and version
  - Main output zone (lines 2-19) with smooth scrolling
  - Input zone (lines 22-24) with visual prompt
- **Real-time status bar** (line 20) displaying:
  - IP address (15 characters)
  - AT firmware version (7 characters)
  - SSID with truncation indicator (14 characters + ~)
  - Signal strength bars (4-level visual indicator)
  - NTP-synchronized time (HH:MM format)
  - Connection status indicator (colored dot)
- **Color-coded output**:
  - User commands: Bright white
  - ESP responses: Bright yellow
  - Local messages: Bright green
  - Debug output: Cyan
  - Error messages: Red

#### Input System
- **Full line editing**:
  - Insert characters at cursor position
  - Delete characters with backspace
  - Cursor movement: left, right, home, end
- **Command history**:
  - Store up to 6 previous commands
  - Navigate with UP/DOWN arrow keys
  - Circular buffer implementation
- **Smart case handling**:
  - Commands auto-convert to uppercase
  - Arguments preserve original case
  - Quoted strings always preserve case
- **Optimized key repeat**:
  - Differentiated initial delays per key type
  - Fast backspace repeat (50 chars/second)
  - Smooth cursor movement (25 moves/second)
- **Visual cursor**: Underline style cursor on scanline 7

#### Network Features
- **Smart initialization**:
  - Auto-detect existing WiFi connections
  - Skip unnecessary setup when already connected
  - Fast startup (~1-1.5 seconds when connected)
- **Built-in commands**:
  - `!CONNECT ssid,password` - Connect to WiFi
  - `!DISCONNECT` - Disconnect from network
  - `!SCAN` - Scan available networks
  - `!IP` - Refresh connection status
  - `!PING [host]` - Ping host (default 8.8.8.8)
  - `!TIME` - NTP time synchronization
  - `!INFO` - ESP firmware information
  - `!MAC` - Show MAC address
- **Signal strength visualization**:
  - 4-bar graphical indicator
  - Color gradient based on RSSI
  - Auto-refresh in background

#### System Features
- **Additional commands**:
  - `!RST` - Reset ESP module
  - `!BAUD rate` - Change baud rate
  - `!RAW` - Raw traffic monitor
  - `!DEBUG` - Toggle debug mode
  - `!CLS` - Clear screen
  - `!HELP` / `!?` - Two-page help system
  - `!ABOUT` - Credits and version
- **Async message filtering**: Automatically hide ESP status messages:
  - `WIFI CONNECTED`, `WIFI GOT IP`
  - `CONNECT`, `CLOSED`
  - `+IPD` notifications
  - Connection IDs
  - `busy`, `ready` messages
  - Proprietary markers (`LAIN`, `WFXR`)

#### Communication
- **256-byte ring buffer**: Prevents data loss during screen operations
- **Overflow protection**: Stops reading when buffer full
- **Configurable timeouts**:
  - TIMEOUT_FAST (25000) for quick queries
  - TIMEOUT_STD (200000) for normal commands
  - TIMEOUT_LONG (500000) for SCAN/CONNECT
- **AY-UART driver**: Reliable 9600 baud bit-banging

### Technical Improvements

#### Performance Optimizations
- **Fast startup sequence**:
  - Reduced `uart_init()` delays: 60 → 20 halts
  - Reduced `uart_flush_hard()`: 8 → 3 halts
  - Adaptive probe timeout: shorter on first try
  - Reduced retry delay: 50 → 20 halts
- **Optimized backspace**:
  - Special case for deletion at end of line
  - Only redraws affected characters
  - Limited trailing space clearing (8 chars max)
- **Efficient history navigation**:
  - Only clears residual characters when needed
  - Includes cursor position in cleanup

#### Bug Fixes
- **Parser noise filtering**: Fixed incorrect messages appearing from async ESP notifications
- **Case-sensitive input**: Fixed forced uppercase on WiFi passwords and SSIDs
- **Backspace speed**: Fixed slow character deletion (was redrawing entire line)
- **System hangs**: Fixed by adding proper UART buffer draining and halt timing
- **AT version parsing**: Fixed trailing dot in version string (e.g., "1.7." → "1.7")
- **WiFi detection**: Fixed initialization order causing missed connection detection
- **History display**: Fixed residual characters when navigating to shorter commands
- **Function declaration order**: Fixed `refresh_cursor_char` forward declaration issue

#### Code Quality
- **Consistent timeout types**: All timeout variables now use `uint32_t`
- **Ring buffer protection**: Added `rb_full()` check before writing
- **Reduced redundancy**: Optimized `input_clear()` to avoid double clearing
- **Frame-based timing**: Main loop uses `halt` for consistent 50Hz operation

### Changed
- Main source file renamed to `espatzx_code.c`
- Output file renamed to `ESPATZX.tap`
- Status bar layout reorganized for AT version display
- Help screens updated with all new commands

### Dependencies
- z88dk cross-compiler
- GNU Make

### Files
```
terminal_v8.c    - Main application (~2300 lines, ~66KB)
ay_uart.asm      - UART driver (~200 lines, ~4KB)
font64_data.h    - Font data (256 chars × 8 bytes)
font64.bin       - Compiled font binary (2KB)
Makefile         - Build configuration
README.md        - English documentation
README_ES.md     - Spanish documentation
CHANGELOG.md     - This file
RELEASE.md       - Release notes
```

---

## [Pre-1.0] Development History

### v0.8 (Internal)
- Added command history
- Implemented line editing
- Added status bar

### v0.7 (Internal)
- Implemented 64-column display
- Added custom font support
- Three-zone interface

### v0.6 (Internal)
- Ring buffer implementation
- Async message filtering
- Signal strength display

### v0.5 (Internal)
- Basic AT command sending
- Simple output display
- Initial ESP communication

### v0.1-0.4 (Internal)
- Project initialization
- AY-UART driver development
- Basic terminal functionality

---

## Attribution

Based on **esp-terminal** by Vasily Khoruzhick (anarsoul).

Includes code from:
- BridgeZX - WiFi file transfer protocols
- NetManZX - Network management utilities

---

[1.0.0]: https://github.com/yourusername/espat-zx/releases/tag/v1.0.0
