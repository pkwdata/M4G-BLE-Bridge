"""
═══════════════════════════════════════════════════════════════════════
USB POWER INJECTOR - COMPLETE PCB SCHEMATIC
For ESP32-S3 Host + Keyboard with 2S LiPo Battery
═══════════════════════════════════════════════════════════════════════

Design Specifications:
- Input: 2S LiPo (7.4V nominal, 6-8.4V range)
- Output 1: 3.3V @ 1A (ESP32-S3)
- Output 2: 5V @ 2A (Keyboard)
- USB 2.0 Data: 480 Mbps
- PCB: 2-layer, 1.0mm thickness
- Size: 85mm × 30mm (compact dongle style)
"""

schematic_overview = """
═══════════════════════════════════════════════════════════════════════
BLOCK DIAGRAM
═══════════════════════════════════════════════════════════════════════

                    ┌──────────────┐
                    │  2S LiPo     │
                    │  Battery     │
                    │  7.4V nominal│
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  2S BMS      │  ← Over-voltage, under-voltage,
                    │  Protection  │    over-current protection
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  Power       │
                    │  Switch      │
                    └──────┬───────┘
                           │ VBAT
                ┌──────────┴──────────┐
                │                     │
         ┌──────▼──────┐       ┌─────▼──────┐
         │  Buck Conv  │       │ Buck/Boost │
         │  3.3V @ 1A  │       │ 5V @ 2A    │
         └──────┬──────┘       └─────┬──────┘
                │                    │
           ┌────▼────┐          ┌────▼────┐
           │ ESP32-S3│          │Keyboard │
           │USB Port │          │USB Port │
           │ (Male)  │          │(Female) │
           └─────────┘          └─────────┘
                │                    │
                └────► D+/D- ◄───────┘
                    (Direct connection)

         ┌──────────────────────┐
         │  USB-C Charger       │  ← For charging battery
         │  2S Balance Charger  │
         └──────────────────────┘
"""

print(schematic_overview)

battery_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 1: BATTERY INPUT & PROTECTION
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
BT1: 2S LiPo Battery (7.4V, 1000-3000mAh)
U1: 2S BMS Protection IC (integrated module or discrete)
SW1: SPDT Power Switch (rated 3A+)
D1: Schottky diode 1N5819 (reverse polarity protection)
F1: Polyfuse 3A (resettable fuse)
C1: 100µF 16V electrolytic (bulk capacitance)
C2: 0.1µF 50V ceramic (bypass)

Schematic:
----------

BT1 (2S LiPo Battery):
  Pin 1 (B+) ───┬───► To BMS B+ input (8.4V max)
                │
  Pin 2 (BAL) ──┼───► To BMS Balance input (~4.2V)
                │
  Pin 3 (B-)  ──┴───► GND

2S BMS Module (U1):
  B+ input  ◄───── Battery B+
  BAL input ◄───── Battery Balance tap
  B- input  ◄───── GND
  P+ output ─────► [D1 Anode] Schottky diode for reverse protection
  P- output ─────► GND

D1 (1N5819 Schottky):
  Anode  ◄───── BMS P+ output
  Cathode ────► [F1] Polyfuse

F1 (Polyfuse 3A):
  Input  ◄───── D1 Cathode
  Output ─────► [SW1] Power Switch center pin

SW1 (Power Switch):
  Center ◄───── F1 output
  NO (On) ────► VBAT_SWITCHED
  NC (Off) ───► Not connected

VBAT_SWITCHED node:
  ├───[C1]───GND    (100µF bulk capacitor)
  ├───[C2]───GND    (0.1µF bypass)
  ├───► To 3.3V Buck Converter (U2)
  ├───► To 5V Buck/Boost Converter (U3)
  └───► To Charger Circuit

Notes:
------
• D1 provides reverse polarity protection (0.3V drop)
• F1 protects against overcurrent (trips at 3A, resets when cool)
• C1 provides bulk energy storage
• C2 filters high-frequency noise
• All grounds must be star-connected at single point
"""

charging_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 2: 2S BATTERY CHARGING CIRCUIT
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
J1: USB-C Female Connector (charge port)
U4: TP5100 2S Li-ion/LiPo Charger IC (or similar)
R1, R2: 5.1kΩ (USB-C CC pull-down)
R3: 2kΩ (charge current programming - 1A charge)
D2: LED Red (charging indicator)
R4: 1kΩ (LED current limit)
C3, C4: 10µF ceramic (input/output decoupling)

Schematic:
----------

J1 (USB-C Charge Port):
  VBUS (A4,B4,A9,B9) ───┬───► U4 VIN
                         │
  CC1 (A5) ──[R1 5.1kΩ]─┴───► GND
  CC2 (B5) ──[R2 5.1kΩ]─┬───► GND
                         │
  GND (A1,B1,A12,B12) ───┴───► GND
  
  D+/D- pins: Not connected (charge only port)

U4 (TP5100 Charger IC - TSSOP-16):
  Pin 1:  TEMP   ─────► 10kΩ NTC thermistor to GND (optional)
  Pin 2:  PROG   ─────► [R3 2kΩ] ──► GND (sets 1A charge current)
  Pin 3:  GND    ─────► GND
  Pin 4:  BAT    ─────► Battery B+ (through BMS)
  Pin 5:  CE     ─────► GND (charge enable, active low)
  Pin 6:  VCC    ─────► VIN (internal connection)
  Pin 7:  VIN    ◄──── USB-C VBUS (5V input)
  Pin 8:  STDBY  ─────► [R4 1kΩ] ──► [D2 LED] ──► GND (standby indicator)
  Pin 9:  CHRG   ─────► Connected to LED circuit
  Pin 10: B1     ─────► Battery Balance tap (Cell 1+)
  Pin 11: B2     ─────► Battery B+ (Cell 2+)
  Pin 12-16: See datasheet

Charging Status Indicators:
  CHRG pin LOW  = Charging (LED ON)
  CHRG pin HIGH = Complete (LED OFF)

Protection:
  [C3 10µF] ── VIN to GND (input filtering)
  [C4 10µF] ── BAT to GND (output filtering)

Notes:
------
• R3 value sets charge current: I_charge = 1200V / R3
  - 2kΩ = 600mA, 1.2kΩ = 1A, 3kΩ = 400mA
• TP5100 handles 2S balance charging automatically
• Add 10kΩ NTC thermistor for temperature monitoring (optional)
• LED indicates charging status
"""

power_3v3_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 3: 3.3V BUCK CONVERTER (ESP32-S3 Power)
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
U2: TPS54202 Buck Converter IC (SOT-23-6) or MP1584 module
L1: 10µH power inductor, 2A rated (5×5mm)
C5: 22µF ceramic 16V (input)
C6: 22µF ceramic 6.3V (output)
C7, C8: 0.1µF ceramic (bypass)
R5, R6: Feedback resistor divider (100kΩ, 47kΩ)
D3: Schottky diode 1N5819 (freewheeling)

Schematic:
----------

U2 (TPS54202 Buck Converter - SOT-23-6):
  Pin 1: VIN  ◄──── VBAT_SWITCHED (6-8.4V)
  Pin 2: GND  ─────► PGND (power ground)
  Pin 3: EN   ─────► VIN (always enabled) or pull-up
  Pin 4: FB   ◄──── Feedback voltage divider
  Pin 5: BOOT ─────► Bootstrap capacitor
  Pin 6: SW   ─────► Switching node to inductor

Circuit Topology:

VBAT_SWITCHED ─┬─[C5 22µF]─ GND
               │
               └──► U2 Pin 1 (VIN)

U2 Pin 6 (SW) ──┬──[L1 10µH]──┬──► 3V3_RAIL
                │              │
               [D3]          [C6 22µF]
                │              │
               GND            GND

3V3_RAIL ──[R5 100kΩ]──┬──[R6 47kΩ]── GND
                        │
                        └──► U2 Pin 4 (FB)

Bootstrap Circuit:
U2 Pin 6 (SW) ──[Cboot 0.1µF]── U2 Pin 5 (BOOT)

Feedback Calculation:
  Vout = 0.6V × (1 + R5/R6)
  3.3V = 0.6V × (1 + 100k/47k)
  Adjust R5/R6 for precise 3.3V output

Output Filtering:
3V3_RAIL ─┬─[C6 22µF]─ GND (bulk capacitor)
          ├─[C7 0.1µF]─ GND (high-freq bypass)
          └─[C8 10µF]─ GND (additional bulk)

Load Connection:
3V3_RAIL ──► To ESP32-S3 USB-C port VCC pin

Notes:
------
• L1 must be rated for peak current (~2A)
• Use low-ESR ceramic capacitors (X7R or X5R)
• D3 is optional with integrated MOSFETs but adds protection
• Keep switching node traces short to minimize EMI
• Add copper pour under U2 for heat dissipation
"""

power_5v_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 4: 5V BUCK/BOOST CONVERTER (Keyboard Power)
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
U3: TPS63070 Buck-Boost Converter IC (VQFN-16) or MT3608 module
L2: 4.7µH power inductor, 4A rated (6×6mm or larger)
C9: 47µF ceramic 16V (input)
C10: 47µF ceramic 10V (output)
C11, C12: 0.1µF ceramic (bypass)
R7, R8: Feedback divider (100kΩ, 33kΩ)
D4: Schottky diode SS34 (high current, low Vf)

Schematic:
----------

U3 (TPS63070 Buck-Boost - VQFN-16):
  Pin 1:  PGND  ─────► Power GND (with thermal pad)
  Pin 2:  L1    ─────► To inductor L2
  Pin 3:  L2    ◄──── From inductor L2
  Pin 4:  VOUT  ─────► 5V output
  Pin 5:  FB    ◄──── Feedback divider
  Pin 6:  AGND  ─────► Analog GND
  Pin 7:  EN    ─────► VIN (enable, active high)
  Pin 8:  PS/SYNC ───► GND (fixed frequency PWM mode)
  Pin 9:  VIN   ◄──── VBAT_SWITCHED
  Pin 10-16: See datasheet

Circuit Topology:

VBAT_SWITCHED ─┬─[C9 47µF]─ GND
               │
               └──► U3 Pin 9 (VIN)

U3 Pin 2 (L1) ──[L2 4.7µH]── U3 Pin 3 (L2)
                     │
                   [D4] (optional catch diode)
                     │
                    GND

U3 Pin 4 (VOUT) ──┬──► 5V_RAIL
                  │
                  ├─[C10 47µF]─ GND
                  ├─[C11 0.1µF]─ GND
                  └─[C12 10µF]─ GND

Feedback Network:
5V_RAIL ──[R7 100kΩ]──┬──[R8 33kΩ]── GND
                      │
                      └──► U3 Pin 5 (FB)

Feedback Calculation:
  Vout = 0.5V × (1 + R7/R8) + 0.5V
  5.0V = 0.5V × (1 + 100k/33k) + 0.5V ≈ 5.01V

Load Connection:
5V_RAIL ──► To Keyboard USB-C port VBUS pins

Current Limiting (optional):
  Add sense resistor (10mΩ) in series with VOUT
  Monitor voltage drop for over-current detection

Notes:
------
• TPS63070 efficiently operates across 2-16V input
• Can buck (8.4V→5V) and boost (6V→5V) as needed
• L2 must handle peak current (~4A)
• Use multiple output caps for low ESR
• Add 100µF bulk cap near load (keyboard connector)
• Thermal pad must be soldered to ground plane
"""

usb_esp32_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 5: ESP32-S3 USB PORT (Host Side - Male)
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
J2: USB-C Male Connector (PCB edge or mid-mount)
R9, R10: 5.1kΩ (CC pull-down for host mode)
R11, R12: 22Ω (USB data series termination)
C13, C14: 15pF (USB signal conditioning, optional)
U5: USBLC6-2SC6 (ESD protection IC, SOT-23-6)
C15: 10µF ceramic (VBUS decoupling, for reference only)

Schematic:
----------

J2 (USB-C Male to ESP32-S3):
  VBUS (A4,B4,A9,B9) ──┬── [C15 10µF] ── GND (decoupling only)
                       │
                       └── NOT POWERED (ESP32 doesn't need VBUS)
                           OR connect to 3V3_RAIL if ESP32 board requires it

  CC1 (A5) ──[R9 5.1kΩ]──┬── GND (host mode indication)
                          │
  CC2 (B5) ──[R10 5.1kΩ]─┴── GND

  D+ (A6) ──[R11 22Ω]──┬──► To U5 (ESD protection) ──► D+ to Keyboard
                       │
                      [C13 15pF]
                       │
                      GND (optional signal conditioning)

  D- (A7) ──[R12 22Ω]──┬──► To U5 (ESD protection) ──► D- to Keyboard
                       │
                      [C14 15pF]
                       │
                      GND (optional signal conditioning)

  GND (A1,B1,A12,B12) ──► Common GND

  SuperSpeed pins (A2,A3,B2,B3,B10,B11,A10,A11): Not connected (USB 2.0 only)

U5 (USBLC6-2SC6 ESD Protection - SOT-23-6):
  Pin 1: I/O1 ◄──► D- (ESP32 side)
  Pin 2: GND  ─────► GND
  Pin 3: I/O2 ◄──► D+ (ESP32 side)
  Pin 4: I/O2 ◄──► D+ (Keyboard side)
  Pin 5: VBUS ◄──── 3V3_RAIL or VBUS (for clamp reference)
  Pin 6: I/O1 ◄──► D- (Keyboard side)

Notes:
------
• R9, R10 (5.1kΩ): Required for USB-C host mode
  - Tells connected device "I don't provide power"
  - ESP32-S3 acts as USB host
• R11, R12 (22Ω): Series termination for signal integrity
  - Reduces ringing and reflections
  - Place close to source (ESP32 side)
• C13, C14 (15pF): Optional capacitive loading
  - Helps with signal conditioning
  - May be omitted for short traces
• U5: Critical ESD protection
  - Protects against ±15kV ESD
  - Bidirectional protection on D+/D-
  - Place very close to USB connector
• VBUS: ESP32-S3 doesn't need external VBUS
  - Some dev boards may accept 5V on VBUS
  - Check your specific ESP32-S3 board
"""

usb_keyboard_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 6: KEYBOARD USB PORT (Device Side - Female)
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
J3: USB-C Female Connector (mid-mount or through-hole)
R13, R14: 5.1kΩ (CC pull-down for device mode)
C16: 100µF ceramic 10V (VBUS bulk capacitor)
C17: 0.1µF ceramic (VBUS bypass)
C18: 10µF ceramic (VBUS additional filtering)
F2: Polyfuse 2.5A (VBUS overcurrent protection)
L3: Ferrite bead 600Ω@100MHz (EMI filtering)

Schematic:
----------

J3 (USB-C Female to Keyboard):
  VBUS (A4,B4,A9,B9) ◄──┬── [F2 Polyfuse 2.5A] ◄── 5V_RAIL
                        │
                        ├── [C16 100µF] ── GND (bulk storage)
                        ├── [L3 Ferrite] ──[C17 0.1µF]── GND
                        └── [C18 10µF] ── GND (additional filtering)

  CC1 (A5) ──[R13 5.1kΩ]──┬── GND (device mode, power sink)
                           │
  CC2 (B5) ──[R14 5.1kΩ]──┴── GND

  D+ (A6) ◄──── From U5 (ESD protection) ◄── ESP32 D+
  
  D- (A7) ◄──── From U5 (ESD protection) ◄── ESP32 D-

  GND (A1,B1,A12,B12) ──► Common GND

  SuperSpeed pins: Not connected (USB 2.0 only)

Power Injection Detail:
5V_RAIL ──[F2]──┬──[L3]──┬──► J3 VBUS (to keyboard)
                │        │
             [C16 100µF] [C17 0.1µF]
                │        │
               GND      GND

USB Data Path:
ESP32 D+ ──[R11]──[U5]──► J3 D+ (to keyboard)
ESP32 D- ──[R12]──[U5]──► J3 D- (to keyboard)

Notes:
------
• R13, R14 (5.1kΩ): Device mode configuration
  - Tells USB host "I need power"
  - Standard for USB-C devices
• F2 (Polyfuse 2.5A): Overcurrent protection
  - Protects keyboard from excessive current
  - Self-resetting after fault clears
• C16 (100µF): Bulk energy storage
  - Provides current during load transients
  - Must be low-ESR ceramic (X7R/X5R)
• L3 + C17: LC filter for EMI reduction
  - Reduces switching noise from 5V converter
  - L3 = 600Ω@100MHz ferrite bead
• All capacitors should be placed very close to connector
• VBUS trace should be wide (minimum 0.5mm/20mil)
"""

indicators_section = """
═══════════════════════════════════════════════════════════════════════
SECTION 7: STATUS INDICATORS & CONTROLS
═══════════════════════════════════════════════════════════════════════

Component List:
-------------
LED1: Red 0805 LED (charging indicator)
LED2: Green 0805 LED (3.3V power indicator)
LED3: Blue 0805 LED (5V power indicator)
R15: 1kΩ (LED1 current limit)
R16: 1kΩ (LED2 current limit)
R17: 1kΩ (LED3 current limit)
SW1: SPDT Slide Switch (main power)

Schematic:
----------

Charging Indicator (Red LED):
U4 CHRG pin ──[R15 1kΩ]──[LED1]──► GND
  (LOW when charging, HIGH when complete)

3.3V Power Indicator (Green LED):
3V3_RAIL ──[R16 1kΩ]──[LED2]──► GND

5V Power Indicator (Blue LED):
5V_RAIL ──[R17 1kΩ]──[LED3]──► GND

Current Calculations:
  I_LED = (Vsupply - Vf_LED) / R
  For 3.3V rail: I = (3.3V - 2.0V) / 1kΩ = 1.3mA ✓
  For 5V rail: I = (5.0V - 2.8V) / 1kΩ = 2.2mA ✓

Optional: Battery Voltage Monitor
VBAT_SWITCHED ──[R18 100kΩ]──┬──[R19 100kΩ]── GND
                              │
                              └──► ADC input (if ESP32 accessible)
                              
  This divider gives 0-4.2V → 0-2.1V for ADC reading

Notes:
------
• LED current kept low (1-2mA) to minimize power consumption
• Resistor values can be adjusted for desired brightness
• Use small 0805 LEDs to save space
• Consider using common-cathode RGB LED for space savings
• SW1 already defined in battery section
"""

protection_summary = """
═══════════════════════════════════════════════════════════════════════
SECTION 8: PROTECTION SUMMARY
═══════════════════════════════════════════════════════════════════════

Protection Feature Checklist:
-----------------------------
✓ Reverse Polarity Protection: D1 (1N5819 Schottky)
✓ Overcurrent Protection: F1 (3A Polyfuse on battery)
✓ Overvoltage/Undervoltage: U1 (2S BMS module)
✓ Short Circuit Protection: U1 (2S BMS module)
✓ VBUS Overcurrent: F2 (2.5A Polyfuse on keyboard port)
✓ ESD Protection: U5 (USBLC6-2SC6 on USB data lines)
✓ EMI Filtering: L3 + C17 (ferrite + capacitor)
✓ Input Filtering: C1, C2 (bulk + bypass on VBAT)
✓ Output Filtering: Multiple caps on 3.3V and 5V rails

Additional Protection Recommendations:
------------------------------------
1. Input Protection:
   - Add TVS diode (SMBJ8.5CA) across VBAT for surge protection
   - Consider PTC thermistor near battery connector

2. Output Protection:
   - Add Zener clamps on 3.3V (ZMM3V3) and 5V (ZMM5V1)
   - Protects against regulator failure overvoltage

3. Ground Protection:
   - Star ground topology: All grounds meet at one point
   - Separate analog ground from power ground, tie at star point

4. Thermal Protection:
   - Add NTC thermistor (10kΩ) to monitor board temperature
   - Connect to TP5100 TEMP pin for charge thermal protection

Complete Protection Schematic:
------------------------------

VBAT Protection Chain:
Battery → BMS (U1) → Schottky (D1) → Polyfuse (F1) → Switch (SW1) → System

3.3V Rail Protection:
VBAT → Buck (U2) → [Zener ZMM3V3] → 3V3_RAIL → Load
                         │
                        GND

5V Rail Protection:
VBAT → Buck-Boost (U3) → [Polyfuse F2] → [Zener ZMM5V1] → 5V_RAIL → Load
                                               │
                                              GND

USB Data Protection:
ESP32 D+/D- → [ESD U5] → Keyboard D+/D-
"""

component_summary = """
═══════════════════════════════════════════════════════════════════════
SECTION 9: COMPLETE BILL OF MATERIALS (BOM)
═══════════════════════════════════════════════════════════════════════

┌────┬─────────────────────────┬────────────────┬─────┬────────┬────────┐
│ #  │ Component               │ Part Number    │ Qty │ Cost   │ Package│
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ SEMICONDUCTORS          │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ U1 │ 2S BMS Module           │ Generic 2S 5A  │ 1   │ $3.00  │ Module │
│ U2 │ 3.3V Buck IC            │ TPS54202DCT    │ 1   │ $2.00  │ SOT-23 │
│ U3 │ 5V Buck-Boost IC        │ TPS63070RNM    │ 1   │ $3.50  │ VQFN16 │
│ U4 │ 2S Charger IC           │ TP5100         │ 1   │ $2.50  │ TSSOP16│
│ U5 │ ESD Protection          │ USBLC6-2SC6    │ 1   │ $0.30  │ SOT-23 │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ DIODES                  │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ D1 │ Schottky Diode          │ 1N5819         │ 1   │ $0.10  │ DO-41  │
│ D2 │ LED Red                 │ Generic 0805   │ 1   │ $0.05  │ 0805   │
│ D3 │ Schottky Diode          │ 1N5819         │ 1   │ $0.10  │ DO-41  │
│ D4 │ Schottky Diode          │ SS34           │ 1   │ $0.15  │ DO-214 │
│ LED2│ LED Green              │ Generic 0805   │ 1   │ $0.05  │ 0805   │
│ LED3│ LED Blue               │ Generic 0805   │ 1   │ $0.05  │ 0805   │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ RESISTORS (0603)        │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ R1-R2 │ 5.1kΩ (USB-C CC)    │ Generic 0603   │ 2   │ $0.02  │ 0603   │
│ R3 │ 2kΩ (Charge program)    │ Generic 0603   │ 1   │ $0.01  │ 0603   │
│ R4 │ 1kΩ (LED current)       │ Generic 0603   │ 1   │ $0.01  │ 0603   │
│ R5 │ 100kΩ (FB divider)      │ Generic 0603   │ 1   │ $0.01  │ 0603   │
│ R6 │ 47kΩ (FB divider)       │ Generic 0603   │ 1   │ $0.01  │ 0603   │
│ R7 │ 100kΩ (FB divider)      │ Generic 0603   │ 1   │ $0.01  │ 0603   │
│ R8 │ 33kΩ (FB divider)       │ Generic 0603   │ 1   │ $0.01  │ 0603   │
│ R9-R10 │ 5.1kΩ (USB-C CC)   │ Generic 0603   │ 2   │ $0.02  │ 0603   │
│ R11-R12│ 22Ω (USB series)   │ Generic 0603   │ 2   │ $0.02  │ 0603   │
│ R13-R14│ 5.1kΩ (USB-C CC)   │ Generic 0603   │ 2   │ $0.02  │ 0603   │
│ R15-R17│ 1kΩ (LED current)  │ Generic 0603   │ 3   │ $0.03  │ 0603   │
│ R18-R19│ 100kΩ (optional)   │ Generic 0603   │ 2   │ $0.02  │ 0603   │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ CAPACITORS              │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ C1 │ 100µF 16V Electrolytic │ Generic        │ 1   │ $0.10  │ Radial │
│ C2 │ 0.1µF 50V X7R          │ Generic 0603   │ 1   │ $0.02  │ 0603   │
│ C3-C4│ 10µF 16V X7R         │ Generic 0805   │ 2   │ $0.10  │ 0805   │
│ C5-C6│ 22µF 16V X7R         │ Generic 0805   │ 2   │ $0.15  │ 0805   │
│ C7-C8│ 0.1µF 16V X7R        │ Generic 0603   │ 2   │ $0.04  │ 0603   │
│ C9-C10│ 47µF 16V X7R        │ Generic 1206   │ 2   │ $0.30  │ 1206   │
│ C11-C12│ 0.1µF 16V X7R      │ Generic 0603   │ 2   │ $0.04  │ 0603   │
│ C13-C14│ 15pF NPO (opt)     │ Generic 0603   │ 2   │ $0.04  │ 0603   │
│ C15 │ 10µF 16V X7R          │ Generic 0805   │ 1   │ $0.05  │ 0805   │
│ C16 │ 100µF 10V X7R         │ Generic 1206   │ 1   │ $0.20  │ 1206   │
│ C17 │ 0.1µF 16V X7R         │ Generic 0603   │ 1   │ $0.02  │ 0603   │
│ C18 │ 10µF 10V X7R          │ Generic 0805   │ 1   │ $0.05  │ 0805   │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ INDUCTORS               │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ L1 │ 10µH 2A Inductor       │ NR4018T100M    │ 1   │ $0.30  │ 4×4mm  │
│ L2 │ 4.7µH 4A Inductor      │ NR6028T4R7M    │ 1   │ $0.40  │ 6×6mm  │
│ L3 │ Ferrite Bead 600Ω     │ BLM18PG601    │ 1   │ $0.10  │ 0603   │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ FUSES                   │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ F1 │ Polyfuse 3A            │ MF-R300        │ 1   │ $0.20  │ Radial │
│ F2 │ Polyfuse 2.5A          │ MF-R250        │ 1   │ $0.20  │ Radial │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ CONNECTORS              │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ J1 │ USB-C Female (Charge)  │ GCT USB4105-GF │ 1   │ $0.80  │ SMD    │
│ J2 │ USB-C Male (ESP32)     │ PCB Edge/Molex │ 1   │ $0.60  │ SMD    │
│ J3 │ USB-C Female (Keyboard)│ GCT USB4105-GF │ 1   │ $0.80  │ SMD    │
│ BT1│ Battery Connector      │ JST-XH 3-pin   │ 1   │ $0.20  │ THT    │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│    │ SWITCHES                │                │     │        │        │
├────┼─────────────────────────┼────────────────┼─────┼────────┼────────┤
│ SW1│ Slide Switch SPDT      │ JS102011SAQN   │ 1   │ $0.40  │ SMD    │
└────┴─────────────────────────┴────────────────┴─────┴────────┴────────┘

TOTAL COMPONENT COST: ~$18-22 (excluding battery and PCB)

PCB Manufacturing:
- PCB (2-layer, 85×30mm): $10-15 for 5 pieces
- Assembly (optional): $25-35
- TOTAL PROJECT: $53-72 including assembly
"""

pcb_layout_guide = """
═══════════════════════════════════════════════════════════════════════
SECTION 10: PCB LAYOUT GUIDELINES
═══════════════════════════════════════════════════════════════════════

PCB Specifications:
------------------
• Dimensions: 85mm × 30mm (dongle style) or 85mm × 55mm (credit card)
• Layers: 2-layer (Top + Bottom)
• Thickness: 1.0mm
• Material: FR-4
• Copper: 1oz (35µm)
• Surface Finish: ENIG (gold) for USB-C connectors
• Solder Mask: Green (or your preference)
• Silkscreen: White

Layer Stackup:
-------------
TOP Layer:    Signal traces, SMD components, USB-C connectors
BOTTOM Layer: Ground plane (primary) + power routing

Component Placement Strategy:
----------------------------
LEFT END (85mm):
  - J2: USB-C Male (ESP32 connection)
  - Can use PCB edge as male connector

MIDDLE SECTION:
  - U2: 3.3V Buck Converter + L1, passives
  - U3: 5V Buck-Boost + L2, passives
  - U4: Battery Charger IC + passives
  - J1: USB-C Female (charging port)
  - SW1: Power switch
  - LEDs and indicators

RIGHT END:
  - J3: USB-C Female (Keyboard connection)
  - F2: Polyfuse
  - C16: Large bulk capacitor

BOTTOM:
  - U1: 2S BMS module (if using external module)
  - Battery connector (BT1)
  - Large capacitors that don't fit on top

Critical Trace Routing:
---------------------
1. USB Data Lines (D+, D-):
   Route as differential pair:
   - Trace width: 0.3mm (12 mil)
   - Spacing: 0.3mm (12 mil) between pairs
   - Target impedance: 90Ω differential (not critical for USB 2.0)
   - Keep length matched within 5mm
   - Avoid vias; route on top layer only
   - Keep away from switching nodes
   - Shield with ground on either side if possible

2. Power Traces:
   - VBAT: 0.8mm (32 mil) minimum
   - 5V_RAIL: 1.0mm (40 mil) minimum
   - 3V3_RAIL: 0.6mm (24 mil) minimum
   - GND: Use bottom plane, top stitching vias
   - All power traces use 45° angles (not 90°)

3. Switching Nodes (SW pins from U2, U3):
   - Keep as short as possible (<10mm)
   - Wide trace: 0.8mm minimum
   - Avoid running under or near sensitive signals
   - Use top layer only
   - No ground plane underneath

4. Feedback Traces:
   - Keep FB traces short and away from switching nodes
   - Route away from high-current paths
   - Use Kelvin sensing (4-wire) for accurate feedback

Ground Plane Design:
------------------
Bottom Layer: Solid ground pour covering 80%+ of layer
  - Remove ground under switching nodes
  - Keep ground solid under USB connectors
  - Keep ground solid under ICs

Top Layer: Ground stitching
  - Add ground pour where space allows
  - Connect top to bottom with vias (0.3mm dia, every 5mm)
  - Add ground vias near all decoupling capacitors

Star Ground Strategy:
  Power GND (high current) ──►┐
  Analog GND (charger, FB) ──►├── Single star point
  Digital GND (USB, ICs) ────►┘    (near main capacitor C1)

Thermal Management:
-----------------
U2 (Buck): 
  - Add thermal vias under IC (0.3mm, 9 vias in 3×3 pattern)
  - Connect to bottom ground plane
  - Add copper pour on top (exposed if possible)

U3 (Buck-Boost):
  - Thermal pad MUST be soldered to ground
  - Use thermal vias (0.3mm, 4×4 pattern)
  - Large ground copper on top and bottom

Capacitor Placement:
------------------
Rule: Place decoupling caps within 5mm of IC power pins
• C2: Within 5mm of U2 VIN
• C7, C8: Within 5mm of U2 output
• C9: Within 5mm of U3 VIN
• C11, C12: Within 5mm of U3 output
• All 0.1µF bypass caps: Directly adjacent to IC pins
• Via to ground immediately from cap ground pad

EMI Reduction Techniques:
-----------------------
1. Keep switching node loops small
2. Use ground plane as shield
3. Route sensitive traces perpendicular to noisy traces
4. Add ferrite bead (L3) on keyboard VBUS
5. Add 0.1µF caps on all IC power pins
6. Use star ground topology
7. Avoid long parallel traces
8. Add ground guard traces around USB data

Silkscreen Markings:
------------------
• Component references (U1, R1, C1, etc.)
• Polarity markers for LEDs, diodes, electrolytic caps
• Pin 1 indicators for ICs
• Connector labels: "CHARGE", "ESP32", "KEYBOARD"
• "VBAT", "3V3", "5V", "GND" test points
• Your logo/version number

Design Rule Check (DRC) Settings:
-------------------------------
• Minimum trace width: 0.15mm (6 mil)
• Minimum trace spacing: 0.15mm (6 mil)
• Minimum drill size: 0.25mm (10 mil)
• Minimum annular ring: 0.15mm (6 mil)
• Copper to edge: 0.5mm minimum
• Mask to mask: 0.1mm minimum

Manufacturing Recommendations:
----------------------------
PCB Manufacturer: JLCPCB, PCBWay, or OSH Park
• JLCPCB: $2 for 5 boards (100×100mm), 2-3 day fabrication
• Specify: 1.0mm thickness, ENIG finish, 1oz copper
• Add assembly service for SMD components ($20-30 setup)

Design Files to Provide:
• Gerber files (all layers)
• Drill files (.drl)
• BOM (Bill of Materials) - CSV format
• CPL (Component Placement List) - CSV format
• Assembly notes PDF

Testing Pads:
-----------
Add test points for critical nodes:
• TP1: VBAT (post-BMS)
• TP2: 3V3_RAIL
• TP3: 5V_RAIL
• TP4: GND (multiple)
• TP5: BAL (balance tap voltage)
• TP6: D+ (USB data)
• TP7: D- (USB data)

Use 1mm diameter pads, clearly labeled

Example Layout (Top View ASCII):
--------------------------------
┌────────────────────────────────────────────────────────────────────┐
│[J2]                                                           [J3] │
│Male   [U5]  [U2]    [U4]    [U3]         [F2]  [C16]       Female │
│ESP32  ESD   Buck    Chrg    Boost         PF   100µ       Keyboard│
│USB-C        3.3V    IC      5V                            USB-C   │
│                                                                    │
│      [LED2] [LED3]  [J1]    [L1]  [L2]   [SW1]                   │
│       Grn    Blu   Charge   10µH  4.7µH  Power                    │
│                    USB-C                  Switch                  │
└────────────────────────────────────────────────────────────────────┘
  ←──────────────────── 85mm ──────────────────────────────────────→
"""

print(battery_section)
print(charging_section)
print(power_3v3_section)
print(power_5v_section)
print(usb_esp32_section)
print(usb_keyboard_section)
print(indicators_section)
print(protection_summary)
print(component_summary)
print(pcb_layout_guide)

print("""
═══════════════════════════════════════════════════════════════════════
                    END OF COMPLETE SCHEMATIC
═══════════════════════════════════════════════════════════════════════

Next Steps:
1. Import this schematic into KiCad or EasyEDA
2. Create symbol library for any custom parts
3. Connect all components as per schematic
4. Run ERC (Electrical Rules Check)
5. Create PCB layout following guidelines above
6. Run DRC (Design Rules Check)
7. Generate Gerber files
8. Order PCB from JLCPCB with assembly
9. Receive and test!

Design is optimized for:
✓ Small form factor (85mm × 30mm dongle style)
✓ All protection features included
✓ 2-layer PCB (low cost)
✓ Easy assembly (mostly 0603/0805 SMD)
✓ Professional-grade reliability

Total Project Cost: ~$60-75 including PCB and assembly

Questions? Need help with KiCad or PCB design? Just ask! 🚀
═══════════════════════════════════════════════════════════════════════
""")