# USB Traffic Capture Guide - Finding Why CharaChorder Won't Talk to ESP32

## The Problem

The CharaChorder works perfectly when connected to your Linux computer, but refuses to send any keypress data when connected to the ESP32 board - even though the ESP32 successfully enumerates and claims the device.

**This means the CharaChorder firmware is detecting something different and refusing to activate.**

## Solution: Capture Working USB Traffic

We need to see **exactly what your computer does** when the CharaChorder works, then compare it to what the ESP32 does.

---

## Step 1: Install Wireshark with USB Capture

```bash
sudo apt update
sudo apt install wireshark tshark

# Add your user to wireshark group to capture without sudo
sudo usermod -a -G wireshark $USER

# Log out and log back in for group changes to take effect
```

## Step 2: Enable USB Monitoring (usbmon)

```bash
# Load the usbmon kernel module
sudo modprobe usbmon

# Verify it's loaded
lsmod | grep usbmon

# Give your user permission to capture USB
sudo chmod a+r /dev/usbmon*
```

## Step 3: Identify Your USB Bus

```bash
# Find which USB bus the CharaChorder is on
lsusb

# Example output:
# Bus 001 Device 022: ID 303a:829a CharaChorder CharaChorder M4G S3
#     ^^^                                   ^^^^
#  Bus number                          VID:PID
```

**Note the bus number** (e.g., Bus 001)

## Step 4: Capture Working Session

```bash
# Start Wireshark (GUI)
sudo wireshark

# OR use command-line (saves to file)
sudo tshark -i usbmon1 -w charachorder_working.pcapng
```

**In Wireshark:**
1. Select interface: `usbmon1` (or whatever bus number you found)
2. Start capture
3. **Disconnect the CharaChorder**
4. **Reconnect the CharaChorder to your computer**
5. **Wait for it to be ready**
6. **Press a few keys** (like 'a', 'b', 'c')
7. **Stop capture**
8. Save the capture file

## Step 5: Filter and Analyze

In Wireshark, apply this filter:
```
usb.idVendor == 0x303a && usb.idProduct == 0x829a
```

**Look for:**

### A. Enumeration Phase
- `GET DESCRIPTOR Device`
- `SET CONFIGURATION`
- `GET DESCRIPTOR Configuration`
- `GET DESCRIPTOR String`

### B. HID Setup Phase
Look for these control transfers:
- `SET_IDLE` (bRequest=0x0A)
- `SET_PROTOCOL` (bRequest=0x0B)
- `GET_REPORT` (bRequest=0x01)
- `SET_REPORT` (bRequest=0x09)
- Any **vendor-specific control requests** (bmRequestType with vendor bit set)

### C. Data Phase
- `URB_INTERRUPT in` - These are the keypress reports
- Look at the **data payload** when you pressed 'a', 'b', 'c'

## Step 6: Find the Magic

**Critical things to check:**

1. **Does the computer send any data to endpoint 0x04 (OUT)?**
   - Filter: `usb.endpoint_address == 0x04`
   - This might be an activation command

2. **Are there any vendor-specific control requests?**
   - Filter: `usb.bmRequestType & 0x40`
   - These would be CharaChorder-specific commands

3. **What's the exact sequence after SET_CONFIGURATION?**
   - Export this as text: File → Export → as Plain Text
   - Send me the enumeration sequence

4. **Is there any timing dependency?**
   - Check if there's a delay between enumeration and first keypress

## Step 7: Compare with ESP32

The ESP32 currently does:
1. Enumerate device
2. Claim HID interface
3. Submit interrupt transfer on endpoint 0x83
4. **Wait for data (never arrives)**

Missing from ESP32:
- Any OUT endpoint communication
- Possibly vendor-specific commands
- Possibly wrong timing

---

## Quick Alternative: Use Linux Kernel Debug

```bash
# Enable USB debug logging
echo 'module usbcore =p' | sudo tee /sys/kernel/debug/dynamic_debug/control
echo 'module usbhid =p' | sudo tee /sys/kernel/debug/dynamic_debug/control

# Watch kernel messages
sudo dmesg -w

# Now plug in the CharaChorder and capture the dmesg output
```

---

## What We're Looking For

The answer to: **"What does my Linux computer do that makes the CharaChorder happy?"**

Possibilities:
1. **Sends a specific control command** we haven't discovered
2. **Writes to the OUT endpoint (0x04)** to activate the device  
3. **Sets a specific configuration** that enables keypresses
4. **Has different timing** between enumeration steps
5. **Sends LED status** via OUT endpoint (many keyboards need this)

Once we capture the working traffic, we can implement the same sequence on the ESP32!

---

## Send Me the Results

After capturing, send:
1. The `.pcapng` file (or screenshots)
2. The exact sequence of control transfers
3. Any data sent to endpoint 0x04
4. The first few keypress packets from endpoint 0x83

This will reveal the secret handshake the CharaChorder expects!
