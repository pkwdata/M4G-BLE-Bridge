# Serial Connection Debugging Guide

## LATEST UPDATE: Enhanced Logging & Cleanup (v2)

### What Changed
1. **Added session delimiter**: Each connect attempt starts with `==== NEW CONNECTION ATTEMPT ====`
2. **Enabled Transport tracing**: Now shows esptool-js internal operations
3. **Enhanced port enumeration**: Shows state of ALL known ports before attempting new connection
4. **Better cleanup ordering**: Disconnect → delay 200ms → clear refs

### Your Current Issue Pattern

Based on your logs:
```
[11:23:57] Port selection dialog was closed...  ← You cancelled (attempt 1)
[11:24:17] esptool.js                          ← You clicked Connect again (attempt 2)
[11:24:17] Serial port WebSerial VID 0x1a86... ← esptool sees the port
[11:24:17] Connecting...                       ← esptool tries to open
[11:24:17] Connection error: ...already open   ← FAILS - port is locked
```

**Theory**: After cancelling attempt #1, the browser retains the port in `navigator.serial.getPorts()` but in an unusable state.

### What to Look For in New Logs

#### ✅ Expected Clean Session
```
[timestamp] ==== NEW CONNECTION ATTEMPT ====
[timestamp] Requesting serial port access…
[timestamp] Checking for previously opened Web Serial ports…
[timestamp] Browser reported 0 known port(s).           ← GOOD: No stale ports
[timestamp] Opening serial picker with 7 USB filter(s)…
[timestamp] User selected: VID 0x1a86 PID 0x55d3
[timestamp] Immediately after selection state: readable=null (locked=false), writable=null (locked=false)
                                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                                GOOD: Port is clean, no locks
[timestamp] Before connect attempt 1 state: readable=null (locked=false), writable=null (locked=false)
[timestamp] TRACE xxx Connect xxx                       ← esptool internal trace
[timestamp] Connection successful on attempt 1
```

#### ⚠️ Stale Port After Cancel (What we expect to see)
```
[timestamp] ==== NEW CONNECTION ATTEMPT ====
[timestamp] Checking for previously opened Web Serial ports…
[timestamp] Browser reported 1 known port(s).           ← ⚠️ Port from cancelled attempt!
[timestamp] Inspecting known port VID 0x1a86 PID 0x55d3…
[timestamp] Known port VID 0x1a86 PID 0x55d3 state: readable=true (locked=false), writable=true (locked=true)
                                                                                                    ^^^^^^^^^^^^
                                                                                    ⚠️ WRITABLE IS LOCKED!
[timestamp] previous: closing serial session for VID 0x1a86…
[timestamp] previous - before lock release state: ...writable=true (locked=true)
[timestamp] previous: releasing writable lock…
[timestamp] previous: writable lock released            ← ✅ Lock freed
[timestamp] previous: port closed successfully          ← ✅ Port cleaned
[timestamp] Opening serial picker...                    ← Now shows picker again
```

#### 🔴 Persistent Lock (Browser Bug)
```
[timestamp] Known port state: ...writable=true (locked=true)
[timestamp] previous: releasing writable lock…
[timestamp] previous: failed to release writable lock: InvalidStateError: ...
                                                        ^^^^^^^^^^^^^^^^^^^^^
                                                        🔴 BROWSER WON'T RELEASE
[timestamp] previous: close failed - InvalidStateError: Cannot close...locked
```
If you see this, it's a browser-level bug. Solutions:
- Close ALL tabs with Device Manager
- Close browser entirely
- Try Edge if using Chrome (or vice versa)
- Windows: Check Device Manager for COM port conflicts

### Key Diagnostic Questions

1. **After cancelling port picker, how many ports does `getPorts()` return?**
   - Look for: `Browser reported X known port(s)`
   - Expected: 0 (clean), or 1 (stale port we can clean)

2. **Does the writable stream stay locked?**
   - Look for: `writable=true (locked=true)` in state dumps
   - If YES: Browser bug, port is stuck

3. **Does our lock release succeed?**
   - Look for: `writable lock released` or `failed to release writable lock`
   - If fails: Browser/OS-level lock, not fixable in JavaScript

4. **Does esptool trace appear BEFORE our logs?**
   - If YES: Means Transport is being created outside handleConnect (shouldn't happen now)
   - If NO: Normal flow

### WSL + Windows Browser Issue

You're running: **Web server in WSL, browser on Windows host**

The Web Serial API in your Windows Chrome/Edge sees COM ports, not `/dev/ttyUSB*`:
- WSL: `/dev/ttyUSB0`
- Windows: `COM3`, `COM4`, etc.
- **Browser talks to Windows COM ports directly**
- If any Windows process has the port open, it stays locked at OS level

## WSL + Windows Browser Issue

You're running into a known edge case: **Web server in WSL, browser on Windows host**.

### The Problem
The Web Serial API in your Windows Chrome browser sees COM ports differently than WSL does:
- WSL sees: `/dev/ttyUSB0`, `/dev/ttyACM0`, etc.
- Windows sees: `COM3`, `COM4`, etc.
- The **browser** talks directly to Windows, not WSL
- If any Windows process (including a previous browser tab/session) has the port open, it stays locked at the OS level

#### 1. Port State Inspection
```
[timestamp] Immediately after selection state: readable=true (locked=false), writable=true (locked=true)
```
- **readable/writable**: Whether streams exist
- **locked**: Whether a reader/writer is currently attached
- **Critical**: If `locked=true`, the port cannot be closed without releasing the lock first

#### 2. Lock Release Operations
```
[timestamp] already-open cleanup (attempt 1): releasing writable lock…
[timestamp] already-open cleanup (attempt 1): writable lock released
```
Watch for failures here—if locks can't be released, the port is stuck.

#### 3. Browser/Platform Info
```
[timestamp] Browser: Mozilla/5.0 (Windows NT 10.0; Win64; x64)…
[timestamp] Platform: Win32
```
Confirms we're in Windows Chrome (not WSL Chrome).

#### 4. Enhanced Error Messages
```
[timestamp] Serial open failed (attempt 1): InvalidStateError - Failed to execute 'open' on 'SerialPort': The port is already open.
[timestamp] Force-close failed: InvalidStateError: Cannot close a port while the read stream is locked. Port may be held by OS/browser.
```

### What to Check

1. **Before connecting**:
   - Close ALL other tabs with the Device Manager
   - In Windows Device Manager (Win+X → Device Manager), expand "Ports (COM & LPT)" and note which COM port your ESP32 is
   - Check Task Manager (Ctrl+Shift+Esc) → Details tab → look for any `chrome.exe` or `msedge.exe` processes holding serial handles

2. **If you still see "already open"**:
   - Look at the debug logs for:
     - `readable=true (locked=true)` or `writable=true (locked=true)` BEFORE connection
     - `Force-close failed` messages
   - This means the browser's internal state thinks the port is open even if no tab is using it

3. **Nuclear option**:
   ```bash
   # In WSL, kill the dev server
   pkill -f vite
   
   # Close ALL Chrome/Edge windows
   # Wait 5 seconds
   # Restart browser fresh
   # Start dev server again
   npm run dev
   ```

4. **Alternative: Use Windows-native dev server**:
   If WSL→Windows boundary keeps causing issues, try running the dev server natively in Windows:
   ```powershell
   # In Windows PowerShell (not WSL)
   cd \\wsl$\Ubuntu\home\me\dev\M4G-BLE-Bridge\M4G-BLE-Bridge\test\M4G-BLE-Bridge\tools\device-manager-web
   npm run dev
   ```
   This keeps browser and server in the same OS.

### Expected Log Flow (Success Case)

```
[timestamp] Requesting serial port access…
[timestamp] Checking for previously opened Web Serial ports…
[timestamp] Browser reported 0 known port(s).
[timestamp] Opening serial picker with 7 USB filter(s)…
[timestamp] Browser: Mozilla/5.0 (Windows NT 10.0; Win64; x64)…
[timestamp] Platform: Win32
[timestamp] User selected: VID 0x1a86 PID 0x55d3
[timestamp] Immediately after selection state: readable=null (locked=false), writable=null (locked=false)
[timestamp] Selected port VID 0x1a86 PID 0x55d3. Preparing connection…
[timestamp] Before closePortIfOpen state: readable=null (locked=false), writable=null (locked=false)
[timestamp] selected: port not open, skipping close
[timestamp] After closePortIfOpen, before Transport creation state: readable=null (locked=false), writable=null (locked=false)
[timestamp] Connecting to VID 0x1a86 PID 0x55d3 (attempt 1/3)…
[timestamp] Before connect attempt 1 state: readable=null (locked=false), writable=null (locked=false)
[timestamp] Connection successful on attempt 1
[timestamp] Serial transport opened @ 921,600 baud
```

### Expected Log Flow (Locked Port Case)

```
[timestamp] User selected: VID 0x1a86 PID 0x55d3
[timestamp] Immediately after selection state: readable=true (locked=false), writable=true (locked=true)
                                                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                                  ⚠️ PORT ALREADY OPEN FROM PREVIOUS SESSION
[timestamp] selected: closing serial session for VID 0x1a86 PID 0x55d3…
[timestamp] selected - before lock release state: readable=true (locked=false), writable=true (locked=true)
[timestamp] selected: releasing writable lock…
[timestamp] selected: writable lock released
[timestamp] selected - after lock release state: readable=true (locked=false), writable=true (locked=false)
[timestamp] selected: port closed successfully
[timestamp] Before connect attempt 1 state: readable=null (locked=false), writable=null (locked=false)
                                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                             ✅ PORT NOW CLEAN
[timestamp] Connection successful on attempt 1
```

### Key Differences in New Code

| Old Behavior | New Behavior |
|--------------|--------------|
| `await port.close()` fails silently if locked | Explicitly releases reader/writer locks BEFORE closing |
| No visibility into port state | Logs readable/writable/locked state at every step |
| Generic error messages | Shows exact DOMException name + message |
| Single cleanup attempt | Multiple cleanup passes with delays |
| Ignores close failures | Throws error if force-close fails (with better context) |

### If This Still Doesn't Work

The most likely remaining issues:
1. **Windows permission**: Some COM drivers require admin privileges
2. **USB driver**: CH340/CH341 driver on Windows might be buggy (VID 0x1a86 = WCH chip)
   - Try updating: [CH341SER driver](http://www.wch-ic.com/downloads/CH341SER_EXE.html)
3. **Browser bug**: Chromium's Web Serial implementation on Windows has known issues with certain USB-serial adapters
   - Try Edge instead of Chrome (different Chromium build)

### Quick Test
Run the dev server and open browser console (F12). Try connecting and paste the full log output here so we can see exactly where it's failing.
