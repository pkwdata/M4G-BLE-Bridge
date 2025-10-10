# CharaChorder Protocol Analysis - Test Instructions

## What Changed

The firmware now:
1. **Claims BOTH CharaChorder halves** when connected via the hub
2. **Logs detailed device information** for each device enumerated
3. **Captures raw USB packets** from both halves with device identification
4. **Only logs interrupt endpoint data** (not control transfers)

## Test Procedure

### Step 1: Connect Complete CharaChorder (Both Halves)

```bash
# Flash the updated firmware
./build-right.sh flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor
```

### Step 2: Look for Enumeration Messages

You should see:
```
=== Enumerating device addr=X (total_claimed=Y) ===
Device Descriptor: VID=0x303A PID=0x829A Class=0xXX ...
✓ Claimed CharaChorder half #1 (device addr=X, interface=Y, endpoint=0xZZ)
```

Then when the second half connects:
```
⚠️  SECOND CharaChorder half detected at addr=X (VID=0x303A PID=0x829A) - WILL CLAIM IT
Protocol analysis mode: claiming both halves to capture all data
✓ Claimed CharaChorder half #2 (device addr=X, interface=Y, endpoint=0xZZ)
```

### Step 3: Press Keys and Capture Data

Press keys on **LEFT half** and **RIGHT half** separately.

Look for messages like:
```
RAW USB RX: ep=0x83 len=8
01 00 04 00 00 00 00 00    <- Example keyboard report

HID report dev=CharaChorder_Half1_AddrX slot=0 8 bytes
```

### Step 4: Analyze the Patterns

**Key Questions:**
1. **Do BOTH halves send data when you press their keys?**
2. **Are the endpoint addresses the same or different?**
3. **What device address does each half have?**
4. **Are the HID reports standard 8-byte keyboard format?**

### Standard HID Keyboard Report Format (8 bytes):
```
Byte 0: Modifier keys (Ctrl, Shift, Alt, etc.)
Byte 1: Reserved (usually 0)
Bytes 2-7: Up to 6 simultaneous key codes
```

### Expected Outcomes

**Scenario A: Both Halves Send Independent Reports**
- You'll see `RAW USB RX` messages from both Half1 and Half2
- Each half sends its own key data
- ✅ Easy to implement - just merge the reports

**Scenario B: Only One Half Sends Data**
- You'll only see data from one device address
- The other half might be passive
- ⚠️ Need to understand the master/slave relationship

**Scenario C: Custom Protocol**
- Reports don't match standard HID format
- Non-standard byte patterns
- ⚠️ Need to decode the custom format

## Data Collection

Save the serial monitor output to a file:
```bash
idf.py -p /dev/ttyACM0 monitor | tee charachorder_protocol_capture.log
```

Record:
- Which device address responds to LEFT half keypresses
- Which device address responds to RIGHT half keypresses
- The exact byte patterns for each key

## Next Steps

Once we understand the protocol, we can:
1. Modify the firmware to identify which half sent each report
2. Implement proper ESP-NOW forwarding with half identification
3. Reconstruct the full keyboard state on the LEFT side
4. Send combined reports via BLE

---

**Start the test now!**
