> **Legacy guide** from the original single-ESP32, 5–8-board project. For building boards for *this* sign (one LOLIN32 + battery per board, ESP-NOW) use **[`HARDWARE_ASSEMBLY.md`](HARDWARE_ASSEMBLY.md)**. The GPIO pin table, multi-board/daisy-chain wiring, software setup and web-interface sections below do not apply; parts, buying tips and battery/XL4015 material still do.

# Modular LED Sign System - Multi-Board Display Controller

## 🎯 Project Overview

WiFi-controlled modular LED matrix sign system supporting:
- **Multiple display boards** (up to 8 boards)
- **Battery powered** - Portable operation with LiFePO4 batteries
- **Word sequencing** - Flash individual words in sequence
- **Scrolling text** - Smooth pixel-by-pixel scrolling
- **Individual characters** - Display single letters/numbers/symbols
- **Full effect control** - Blink, fade, color cycle with adjustable speeds
- **Web interface** - Mobile-friendly control panel
- **Brightness control** - 0-100 range for optimal viewing
- **3D printable mount** - Battery holder/hanger included

---

## 🛒 Parts List & Shopping Guide

### Complete Bill of Materials

All parts needed to build a complete battery-powered LED sign system. Links go directly to the exact products used in this build.

| Item | Description | Qty | Link |
|------|-------------|-----|------|
| **ESP32 Microcontroller** | ESP32 ESP-WROOM-32 Development Board<br>• 2.4GHz WiFi + Bluetooth<br>• Dual core processor<br>• USB Type-C interface<br>• 30 pin<br>• Compatible with Arduino IDE | 3-pack<br>(1 per sign) | [Amazon - ESP32 3-Pack](https://www.amazon.com/dp/B0DN61LVYV?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_12&th=1) |
| **LiFePO4 Battery** ⭐ | Rechargeable 6V 6Ah LiFePO4 Battery<br>• Built-in USB charging<br>• 2000+ cycle life<br>• Built-in BMS protection<br>• F1 terminals<br>• 0.7 lbs per battery | 2-pack<br>(1-2 per sign) | [Amazon - LiFePO4 2-Pack](https://www.amazon.com/dp/B0D6YJ1RK6?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) |
| **Buck Converter** | XL4015 DC-DC Step-Down Converter<br>• 5A output (4-38V input)<br>• 96% efficiency<br>• LED indicators<br>• Adjustable voltage | 10-pack<br>(1 per board) | [Amazon - XL4015 10-Pack](https://www.amazon.com/dp/B0D9JQBQYJ?ref=ppx_yo2ov_dt_b_fed_asin_title) |
| **LED Strip Lights** | WS2812B 5V Addressable LED Strip<br>• Individual pixel control<br>• 60 LEDs/meter (or 144/meter)<br>• 5V power<br>• 400 LEDs per 20×20 board | 6m roll<br>(per board)<br>Custom 10m available | [Amazon - WS2812B 6m](https://www.amazon.com/dp/B01CDTEJBG?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_2&th=1)<br>*Custom 10m direct from BTF* |
| **Power Connectors** | DC Power Pigtail Barrel Plug<br>• 2.1mm × 5.5mm<br>• Male + Female pairs<br>• **Real 18AWG** wire<br>• 12V 5A rated | 10 pairs | [Amazon - DC Pigtail 10-Pack](https://www.amazon.com/dp/B072BXB2Y8?ref=ppx_yo2ov_dt_b_fed_asin_title) |
| **Battery Connectors** | F1 Female Spade Lug Connector<br>• 0.187" (4.75mm)<br>• For LiFePO4 battery terminals<br>• Model: TCC-18N2 | As needed | [BatterySpec - F1 Connectors](https://www.batteryspec.com/cgi-bin/cart.cgi?action=link&product=1045) |
| **Hookup Wire** | 22 AWG Stranded Silicone Wire<br>• 6 colors (60ft each)<br>• Flexible silicone insulation<br>• Total: 360 feet<br>• Colors: Red, Black, Yellow, Blue, White, Green | 1 kit | [Amazon - 22AWG Wire Kit](https://www.amazon.com/dp/B0BWGXH5SF?ref_=ppx_hzsearch_conn_dt_b_fed_asin_title_2&th=1) |
| **Dupont Connectors** | Dupont Connector Kit<br>• 1550 pieces<br>• Headers, housings, pins<br>• Male/Female connectors<br>• For ESP32 connections | 1 kit | [Amazon - Dupont Kit](https://www.amazon.com/IWISS-1550PCS-Connector-Headers-Balancer/dp/B08X6C7PZM/) |
| **Mounting Board** | White Foam Board<br>• 20" × 30" sheets<br>• LED matrix backing<br>• Lightweight and rigid<br>• Available at local Walmart | 1 per board | [Walmart - Foam Board](https://www.walmart.com/ip/Pen-Gear-White-Foam-Board-20-x-30/5347546821) |

### Cost Breakdown

**Single Board Setup (Battery Powered):**

| Component | Cost | Notes |
|-----------|------|-------|
| ESP32 (from 3-pack) | ~$8 | 3 boards for ~$24 |
| LiFePO4 Battery (from 2-pack) | ~$12 | 2 batteries for ~$24 |
| XL4015 Converter (from 10-pack) | ~$1.50 | 10 units for ~$15 |
| WS2812B LED Strip (6m) | ~$25 | Per board |
| Foam Board | ~$2 | Local pickup |
| Wires & Connectors | ~$5 | From kits above |
| **Total per board:** | **~$53.50** | Complete portable sign |

**Additional boards:** ~$45 each (battery, LEDs, converter, foam board)

**Shared Components (one-time purchase):**
- Wire kit: $13 (lasts many builds)
- Dupont kit: $15 (lasts many builds)
- DC connectors: $11 (10 pairs)
- F1 connectors: $5 (as needed)

### What You Get

**For ~$54 per board:**
- ✅ Fully portable LED sign (battery powered)
- ✅ 83 minutes runtime (166 min with 2 batteries)
- ✅ WiFi controlled via phone/computer
- ✅ USB rechargeable (use any phone charger)
- ✅ 20×20 LED matrix (400 individually controllable LEDs)
- ✅ Multiple display modes (scrolling, static, word sequence)
- ✅ Full RGB color control
- ✅ Built-in effects (blink, fade, color cycle)
- ✅ Professional 3D-printed battery mount (STL included)

### Optional Upgrades

| Item | Purpose | Approx. Cost |
|------|---------|--------------|
| **Logic Level Shifter** | Convert 3.3V to 5V signals<br>Improves LED reliability | $5-8 |
| **LED Voltage Meter** | Monitor battery level<br>Shows real-time voltage | $8-12 |
| **On/Off Switch** | Power control<br>10A rated rocker switch | $3-5 |
| **Heatsinks** | Cooling for XL4015<br>If running hot | $3-5 |
| **Extra Batteries** | Extended runtime<br>Swap batteries for all-day use | $12 each |
| **USB Fast Charger** | Faster battery charging<br>2-3A output recommended | $8-15 |

### Quantity Guide

**Building 1 Sign:**
- 1× ESP32 (buy 3-pack, keep spares)
- 1× Battery (buy 2-pack, extend runtime or keep spare)
- 1× XL4015 (buy 10-pack, massive spares)
- 1× LED strip (6m minimum)
- 1× Foam board
- Wire/connectors from kits

**Building 2 Signs:**
- 2× ESP32 (from 3-pack)
- 2× Batteries (use full 2-pack)
- 2× XL4015 (from 10-pack)
- 2× LED strips
- 2× Foam boards
- Wire/connectors from kits

**Building 4 Signs:**
- 2× ESP32 3-packs (6 total, 2 spare)
- 2× Battery 2-packs (4 total)
- 1× XL4015 10-pack (6 spare)
- 4× LED strips
- 4× Foam boards
- Wire/connectors from kits

### Where to Buy Alternatives

**If Amazon links don't work or out of stock:**

**ESP32:**
- AliExpress: Search "ESP32 WROOM development board"
- eBay: Often cheaper but slower shipping
- Local electronics stores: Higher price but immediate

**LiFePO4 Batteries:**
- Search: "6V 6Ah LiFePO4 USB charging BMS"
- Alternative brands: Ampere Time, ExpertPower, Renogy
- **Must have:** Built-in BMS + USB charging

**XL4015 Buck Converters:**
- eBay: Often $8-10 for 5-pack
- AliExpress: Cheapest but slow shipping (4-6 weeks)
- Search: "XL4015 5A buck converter adjustable"

**WS2812B LED Strips:**
- BTF-Lighting Store (direct): Best quality, custom lengths
- AliExpress: Cheaper, verify 5V and 60 LED/m
- eBay: Quick shipping options
- **Important:** Get 5V version, not 12V!

**Wire & Connectors:**
- Local electronics stores (RadioShack alternatives)
- Hardware stores for basic wire
- Online: Adafruit, SparkFun (higher quality)

### Important Specifications

**When Buying LED Strips:**
- ✅ **Must be 5V** (not 12V or 24V)
- ✅ WS2812B chipset (not WS2811 or SK6812)
- ✅ 60 LEDs/meter minimum (144/meter is better but pricier)
- ✅ White PCB preferred (better light diffusion on foam)
- ✅ IP30 (non-waterproof) is fine for indoor use

**When Buying Batteries:**
- ✅ 6V nominal voltage (5.5-6.4V range)
- ✅ 6Ah capacity minimum (5Ah okay, 7-10Ah better runtime)
- ✅ **Must have built-in BMS** (critical for safety)
- ✅ USB charging port (makes life easier)
- ✅ F1 terminals (0.187" blade terminals)
- ✅ LiFePO4 chemistry (safer than Li-ion)

**When Buying Buck Converters:**
- ✅ XL4015 chipset (not LM2596 - less efficient)
- ✅ 5A minimum output current
- ✅ 4-38V input range (must handle 6V battery)
- ✅ Adjustable output (1.25-36V)
- ✅ LED indicators preferred

### Tips for Buying

💡 **Money Saving Tips:**
1. Buy in bulk (3-packs, 10-packs) - cheaper per unit
2. Check eBay for converters - often half Amazon price
3. Get foam board locally (avoids shipping cost)
4. One wire kit lasts 5+ builds
5. Extra ESP32s make good spares or other projects

⚠️ **What NOT to Compromise On:**
1. **Battery quality** - Must have BMS, buy from reputable seller
2. **LED strip** - Cheap strips fail, stick to WS2812B
3. **Wire gauge** - Don't go thinner than 22 AWG for data, 18 AWG for power
4. **ESP32 authenticity** - Clones often have WiFi issues

🎯 **Best Value Strategy:**
- Order all at once (save on shipping)
- Get 2-packs/3-packs even for 1 sign (spares are invaluable)
- Check eBay before Amazon (often 20-30% cheaper)
- Wait for sales (Prime Day, Black Friday)
- Bundle items from same seller (combined shipping)

---

## 📌 ESP32 GPIO Pin Configuration

### Pin Mapping Reference

The following table maps GPIO numbers to the **actual board labels** on the ESP-WROOM-32:

| Board # | GPIO Pin | Board Label | Wire Color (Suggested) | Notes |
|---------|----------|-------------|------------------------|-------|
| Board 1 | GPIO 5   | **CS**      | Red                    | Default/Primary board |
| Board 2 | GPIO 16  | **RX2**     | Orange                 | UART2 RX |
| Board 3 | GPIO 17  | **TX2**     | Yellow                 | UART2 TX |
| Board 4 | GPIO 18  | **SCK**     | Green                  | SPI Clock |
| Board 5 | GPIO 19  | **MISO**    | Blue                   | SPI MISO |
| Board 6 | GPIO 21  | **SDA**     | Purple                 | I2C Data |
| Board 7 | GPIO 22  | **SCL**     | Gray                   | I2C Clock |
| Board 8 | GPIO 23  | **MOSI**    | White                  | SPI MOSI |

### Pin Location Quick Reference

**Right Side of Board (looking at USB port at bottom):**
- Pin 15: GPIO 23 (MOSI) - Board 8
- Pin 14: GPIO 22 (SCL) - Board 7
- Pin 13: GPIO 1 (TX0) - Not used
- Pin 12: GPIO 3 (RX0) - Not used
- Pin 11: GPIO 21 (SDA) - Board 6
- Pin 10: GPIO 19 (MISO) - Board 5
- Pin 9: GPIO 18 (SCK) - Board 4
- Pin 8: GPIO 5 (CS) - Board 1
- Pin 7: GPIO 17 (TX2) - Board 3
- Pin 6: GPIO 16 (RX2) - Board 2

### Important Notes

⚠️ **Do NOT use these pins** (reserved for ESP32 system):
- GPIO 0, 2, 15 (boot mode pins)
- GPIO 6-11 (connected to flash memory)
- GPIO 1, 3 (UART0 TX/RX - USB serial)

✅ **Safe to use** - All 8 pins listed above are safe for LED data output

---

## 🔌 Hardware Setup

### Required Components

**Core Components:**
- **ESP-WROOM-32** development board
- **WS2812B LED matrices** (20×20 pixels each)
- **470Ω resistors** (one per board) - between ESP32 pin and LED data input
- **Wire** (16-18 AWG recommended)

**Power Options (choose one):**

**Option A: Wall Power (Permanent Installations)**
- **5V power supply** (adequate for number of boards)
  - Estimate: 60mA per LED at full brightness (400 LEDs × 60mA = 24A max)
  - Typical usage at 25% brightness: ~5A per board
  - 1 board: 10A 5V power supply
  - 2 boards: 20A 5V power supply
  - 4+ boards: Multiple power supplies
- **1000µF capacitor** (across power supply terminals)

**Option B: Battery Power (Portable) ⭐ Recommended for portability**
- **LiFePO4 6V 6Ah batteries** (1 per board for 83 min runtime)
  - Built-in BMS protection
  - USB charging (use any phone charger)
  - Weight: Only 0.7 lbs per battery
  - Cost: ~$12-15 each
- **XL4015 DC-DC buck converters** (1 per board)
  - Converts 6V battery to 5V for LEDs
  - 96% efficient
  - Cost: ~$10-12 for 3-pack
- **3D printable battery mount** (STL file included)

**Optional but Recommended:**
- **Logic level shifter** - Converts 3.3V ESP32 signals to 5V for WS2812B
- **On/off switch** - For battery configurations
- **Voltage meter** - Monitor battery level

### Wiring Each Board

**Wall Power Configuration:**
```
ESP32 GPIO Pin → 470Ω Resistor → LED Matrix Data In
                                  
Power Supply 5V+ → LED Matrix VCC
Power Supply GND → LED Matrix GND
                 → ESP32 GND (IMPORTANT!)
```

**Battery Power Configuration:**
```
ESP32 GPIO Pin → 470Ω Resistor → LED Matrix Data In

Battery 6V+ → XL4015 IN+
Battery GND → XL4015 IN- → ESP32 GND (IMPORTANT!)

XL4015 OUT+ (5.0V) → LED Matrix VCC  
XL4015 OUT- → LED Matrix GND → ESP32 GND (common ground!)
```

**Critical:** All grounds must be connected together:
- Battery ground (-)
- XL4015 OUT-
- ESP32 GND
- LED board GND

**See "Battery Power" section for complete battery wiring diagrams**

### Multi-Board Chain Configuration

**Option 1: Individual Control** (Recommended)
- Each board has its own GPIO pin
- Better control and troubleshooting
- Can display different content on each board

```
ESP32 GPIO 5  → Board 1 Data In
ESP32 GPIO 16 → Board 2 Data In
ESP32 GPIO 17 → Board 3 Data In
... etc
```

**Option 2: Daisy Chain** (Current code uses this)
- All boards connected in series on one GPIO
- Simpler wiring but limited to sequential display

```
ESP32 GPIO 5 → Board 1 Data In
Board 1 Data Out → Board 2 Data In
Board 2 Data Out → Board 3 Data In
... etc
```

---

## 🔋 Battery Power (Portable Operation)

### Battery Power Overview

Make your LED sign portable with rechargeable LiFePO4 batteries! These batteries provide:
- ✅ **USB charging** - Use any phone charger
- ✅ **Long runtime** - 83+ minutes per board
- ✅ **Lightweight** - Only 0.7 lbs per battery
- ✅ **Safe** - Built-in BMS protection
- ✅ **Long life** - 2000+ charge cycles
- ✅ **3D printable mount** - Battery holder/hanger STL included

### Recommended Battery: LiFePO4 6V 6Ah

**Specifications:**
- **Voltage:** 6V (5.5-6.4V operating range)
- **Capacity:** 6Ah (36Wh energy)
- **Chemistry:** LiFePO4 (Lithium Iron Phosphate)
- **Weight:** 0.7 lbs (compared to 2.2 lbs for lead acid)
- **BMS:** Built-in protection (overcharge, over-discharge, overcurrent)
- **Charging:** USB input (use any USB adapter)
- **Terminals:** F1 blade terminals (0.187")
- **Cycle Life:** 2000+ charge/discharge cycles
- **Safety:** Won't catch fire, very stable chemistry

**Where to buy:**
- Amazon: Search "6V 6Ah LiFePO4 battery USB"
- Typical price: $12-15 each (often sold in 2-packs for $24)
- Example: TOPUSSE 6V 6Ah LiFePO4 (2-pack)

### Voltage Converter: XL4015 Buck Converter

**Required to convert 6V battery to 5V for LEDs:**

**Specifications:**
- **Model:** XL4015 DC-DC Step-Down Buck Converter
- **Input:** 4-38V DC (works perfectly with 6V battery)
- **Output:** 1.25-36V adjustable (set to 5.0V)
- **Current:** 5A maximum (4.5A recommended)
- **Efficiency:** 96% (excellent!)
- **Power:** 75W max
- **Size:** 54×23×18mm
- **Price:** $10-12 for 3-pack

**Where to buy:**
- Amazon: Search "XL4015 DC-DC buck converter"
- Get the 3-pack (use 1 per board, keep spares)

### Runtime Calculations

**Power consumption at 25% brightness (typical use):**
- 1 board: 5A @ 5V = 25W
- 2 boards: 10A @ 5V = 50W
- 3 boards: 15A @ 5V = 75W
- 4 boards: 20A @ 5V = 100W

**Single 6V 6Ah battery (36Wh energy):**

| Boards | Runtime | Notes |
|--------|---------|-------|
| 1 | **83 minutes** | Great for demos/events |
| 2 | **41 minutes** | Good for short displays |
| 3 | **28 minutes** | Quick presentations |
| 4 | **21 minutes** | Brief shows |

**Two batteries in parallel (72Wh total):**

| Boards | Runtime | Notes |
|--------|---------|-------|
| 1 | **166 minutes** (2.8 hrs) | All-day events |
| 2 | **83 minutes** (1.4 hrs) | Extended displays |
| 3 | **55 minutes** | Longer shows |
| 4 | **41 minutes** | Good presentations |

### Battery Power Wiring

#### Single Board Configuration:

```
┌─────────────────────┐
│  LiFePO4 Battery    │
│  6V 6Ah w/ BMS      │
│                     │
│  [+] Terminal       │
│   │                 │
│   └─────────────┐   │
│                 │   │
│  [-] Terminal   │   │
│   │             │   │
└───┼─────────────┘   │
    │                 │
    │  ┌───────────┐  │
    └──┤ XL4015    │  │
       │ IN-       │  │
    ┌──┤ IN+       │  │
    │  │           │  │
    │  │ OUT+ ─────┼──┼──→ LED Board VCC
    │  │    (5.0V) │  │
    │  │ OUT- ─────┼──┼──→ LED Board GND
    │  └───────────┘  │         ↓
    │                 │    ESP32 GND
    └─────────────────┴──→ (common ground!)

Components:
• 1× LiFePO4 6V 6Ah battery ($12-15)
• 1× XL4015 buck converter ($4 from 3-pack)
• 16-18 AWG wire
• F1 blade connectors

Runtime: 83 minutes @ 1 board
Weight: 0.7 lbs total
```

#### Two Boards with Separate Converters:

```
┌──────────────┐
│  LiFePO4     │
│  6V 6Ah      │
│              │
│  [+] ────┬───┼─────────┐
│          │   │         │
│  [-] ──┐ │   │         │
│        │ │   │         │
└────────┼─┘   │         │
         │     │         │
    ┌────┴──┐ ┌┴──────┐  │
    │XL4015 │ │XL4015 │  │
    │  #1   │ │  #2   │  │
    │       │ │       │  │
    │OUT+───┼─┼───→ Board 1 VCC
    │       │ │       │
    │OUT-─┐ │ │OUT+───┼──→ Board 2 VCC
    │     │ │ │       │
    └─────┘ │ │OUT-─┐ │
            │ │     │ │
            └─┴─────┴─┴──→ All GND common

Runtime: 41 minutes @ 2 boards
Weight: 0.7 lbs
```

#### Extended Runtime: Two Batteries in Parallel

```
┌─────────┐  ┌─────────┐
│Battery 1│  │Battery 2│
│ 6V 6Ah  │  │ 6V 6Ah  │
└─┬────┬──┘  └─┬────┬──┘
  +    -       +    -
  │    │       │    │
  └─┬──┼───────┘    │
    │  └────────┬───┘
    │           │
    └──→ XL4015 IN+ and IN-
         (Still 6V, but 12Ah capacity!)

Runtime: 166 minutes @ 1 board
         83 minutes @ 2 boards
Weight: 1.4 lbs total
```

### XL4015 Setup Procedure

**CRITICAL: Set voltage BEFORE connecting LEDs!**

1. **Charge battery fully via USB**
   - Red LED = Charging
   - Green LED = Fully charged (6.3-6.4V)

2. **Connect battery to XL4015 input**
   - Battery (+) → XL4015 IN+
   - Battery (-) → XL4015 IN-

3. **Adjust output voltage**
   - Use multimeter on OUT+ and OUT-
   - Turn potentiometer clockwise to increase
   - Set to exactly **5.0V** (±0.05V okay)
   - Mark the setting with paint pen

4. **Connect LEDs**
   - XL4015 OUT+ → LED Board VCC
   - XL4015 OUT- → LED Board GND **AND** ESP32 GND (common!)

5. **Test at low brightness**
   - Start at 10% brightness
   - Verify voltage stays 4.95-5.05V
   - Feel XL4015: should be warm, not hot
   - Gradually increase to 25%

### USB Charging (Built-in)

**How to charge:**
1. Disconnect battery from LED sign (or use on/off switch)
2. Plug USB cable into battery charging port
3. Connect to any USB power adapter
4. Red LED indicates charging
5. Green LED indicates fully charged

**Charge times:**
- 1A adapter (phone charger): 6-7 hours
- 2A adapter (iPad charger): 3-4 hours ⭐ Recommended
- 3A adapter (fast charger): 2-3 hours

**What you can use:**
- Phone charger (works!)
- iPad/tablet charger (faster)
- USB power bank (portable charging)
- Computer USB port (slow but works)
- Any 5V USB adapter

### Battery Voltage Guide (LiFePO4)

**Check battery level:**

| Voltage | Charge | Action |
|---------|--------|--------|
| 6.4V | 100% | Fully charged |
| 6.3V | 90% | Excellent |
| 6.2V | 70% | Good |
| 6.1V | 50% | Half |
| 6.0V | 30% | ⚠️ Recharge soon |
| 5.8V | 10% | ⚠️ Stop using, charge now |
| 5.5V | 5% | ❌ BMS will cut off |

**Best practice:** Recharge when voltage drops to 6.0V (30% remaining)

### 3D Printable Battery Mount

**Included STL file:** `battery_hanger_-_led_sign_v4.stl`

**Features:**
- Holds LiFePO4 6V 6Ah battery securely
- Hanger design for wall/surface mounting
- Easy battery insertion/removal
- Cable management built-in

**Print settings:**
- Material: PLA or PETG
- Layer height: 0.2mm
- Infill: 20-30%
- Supports: May be needed (check in slicer)
- Print time: ~3-4 hours

**Mounting:**
1. Print battery holder/hanger
2. Insert battery into holder
3. Mount hanger to wall/surface/sign back
4. Route wires through cable management
5. Connect to XL4015 as shown in wiring diagrams

### Battery Care & Safety

**Daily use:**
- ✓ Recharge after each use
- ✓ Check LED indicator before use
- ✓ Store in cool, dry place
- ✓ Keep terminals clean

**Storage:**
- Short-term (days): Store fully charged
- Long-term (months): Store at 60% charge
- Check every 3 months
- Never store completely discharged

**Safety features (Built-in BMS):**
- ✅ Overcharge protection (stops at 7.2V)
- ✅ Over-discharge protection (cuts at 5.0V)
- ✅ Overcurrent protection (limits to ~6-10A)
- ✅ Short circuit protection
- ✅ Temperature protection

**Lifespan optimization:**
- Get 2000+ cycles by avoiding full discharge
- Keep battery cool during use (<40°C)
- Don't exceed 6A current draw per battery
- Recharge within 24 hours after use

### Battery vs. Wall Power Comparison

| Feature | Battery Power | Wall Power |
|---------|--------------|------------|
| **Portability** | ✅ Fully portable | ❌ Needs outlet |
| **Runtime** | 83+ min per battery | ∞ Unlimited |
| **Weight** | 0.7 lbs extra | No extra weight |
| **Complexity** | Medium (converter needed) | Simple |
| **Cost** | +$16 per board | $0 extra |
| **Recharging** | USB charging | N/A |
| **Use cases** | Events, demos, outdoor | Permanent displays |

**When to use battery:**
- Portable displays
- Events/parties
- Outdoor installations
- Places without power outlets
- Quick setups/teardowns

**When to use wall power:**
- Permanent installations
- Home displays
- Long-running messages (>2 hours)
- High-power configurations (4+ boards)

### Shopping List: Battery Power Add-On

**For single board portable setup:**

```
☐ 1× LiFePO4 6V 6Ah battery       $12-15
☐ 1× XL4015 3-pack converters     $10-12 (use 1, keep 2 spare)
☐ F1 blade connector kit          $3-5
☐ 16-18 AWG wire (red/black)      $5
☐ Heat shrink tubing              $3
☐ Optional: Battery voltage meter $8
☐ Optional: On/off switch (10A)   $3
─────────────────────────────────────────
Total: $33-43 (gives you 83 min runtime)

Note: USB charger not needed - use your phone charger!
```

**For two board extended runtime:**

```
☐ 2× LiFePO4 6V 6Ah batteries     $24-30 (often sold in 2-packs)
☐ 1× XL4015 3-pack converters     $10-12 (use 2)
☐ Wires, connectors, etc.         $8-10
─────────────────────────────────────────
Total: $42-52 (gives you 83 min @ 2 boards)
```

**Print the battery mount:**
- Use included STL file: `battery_hanger_-_led_sign_v4.stl`
- PLA or PETG filament (~50g)
- 3-4 hour print time

### Troubleshooting Battery Power

**LEDs dim or flickering:**
- Check battery voltage (should be >6.0V)
- Verify XL4015 output still 5.0V
- Ensure common ground connected
- Battery may be depleted - recharge

**Short runtime:**
- Battery not fully charged (check voltage)
- High current draw (measure actual current)
- Battery aging (normal after 1000+ cycles)
- Cold temperature reduces capacity

**Battery won't charge:**
- Try different USB cable
- Try different USB adapter
- Battery may be over-discharged
- BMS may be locked (disconnect 10 min, retry)

**XL4015 getting hot:**
- Add heatsink ($2-3)
- Improve airflow
- Use separate converter per board
- Reduce brightness temporarily

---

## 💻 Software Setup

### 1. Install Arduino IDE

Download from: https://www.arduino.cc/en/software

### 2. Install ESP32 Board Support

1. Open Arduino IDE
2. Go to **File → Preferences**
3. Add to "Additional Boards Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools → Board → Boards Manager**
5. Search for "ESP32"
6. Install **"ESP32 by Espressif Systems"**

### 3. Install Required Library

**FastLED Library** (for WS2812B control)
- Go to **Tools → Manage Libraries**
- Search: "FastLED"
- Install **"FastLED by Daniel Garcia"**

### 4. Configure Your Build

Edit these settings in the `.ino` file:

```cpp
// ----- MULTI-BOARD CONFIGURATION -----
#define NUM_SIGNS     1          // Change to your number of boards (1-8)
#define LED_PIN       5          // Default: GPIO 5 (CS)
```

**For multiple boards**, update the pin configuration:
```cpp
// Example for 3 boards:
#define NUM_SIGNS     3
#define LED_PIN_1     5          // GPIO 5 (CS) - Board 1
#define LED_PIN_2     16         // GPIO 16 (RX2) - Board 2
#define LED_PIN_3     17         // GPIO 17 (TX2) - Board 3
```

### 5. Upload Code

1. Connect ESP32 via USB
2. Select **Tools → Board → ESP32 Dev Module**
3. Select correct **Port**
4. Click **Upload** button

---

## 📱 Using the Web Interface

### Connect to WiFi

1. Power on ESP32
2. Wait 10 seconds for WiFi AP to start
3. On phone/computer, connect to WiFi:
   - **Network Name:** LED_SIGN_AP
   - **Password:** sign1234

4. Open browser and go to:
   - http://ledsign.local
   - OR http://192.168.4.1

### Display Modes

#### 🔤 Single Character Mode
- Display one letter, number, or symbol
- Stays on screen until changed
- Good for: Status indicators, room numbers

#### 📝 Message Mode  
- Cycles through characters one at a time
- Adjustable speed per character (200-3000ms)
- Good for: Short messages, names

#### 📜 Scroll Mode
- Smooth scrolling text across all boards
- Auto-detects if scrolling needed (text longer than display)
- Adjustable scroll speed (20-200ms per pixel)
- Good for: Long messages, announcements

#### 💬 Word Sequence Mode ⭐ NEW
- Flash individual words in sequence
- Example: "WALK" → "THE" → "DOG"
- Adjustable display time per word
- Good for: Instructions, multi-step directions

---

## 🎨 Effects & Controls

### Color Control
- Full RGB color picker
- Presets available via web interface

### Brightness Control
- Range: **0-100** (limited for comfort/power)
- Maps to FastLED range 0-255 internally

### Visual Effects

| Effect | Description | Speed Control |
|--------|-------------|---------------|
| **Static** | Solid color, no animation | N/A |
| **Blink** | On/off flashing | 100-2000ms |
| **Fade** | Smooth brightness fade in/out | 5-50ms per step |
| **Color Cycle** | Rainbow color rotation | 10-200ms per hue |

---

## 🏗️ Display Mode Examples

### Example 1: Two-Word Sequence
**Input:** "Hello World"  
**Result:**
1. Display "HELLO" for 2 seconds
2. Display "WORLD" for 2 seconds
3. Repeat

### Example 2: Three-Word Instructions
**Input:** "Walk The Dog"  
**Result:**
1. Display "WALK" for 2 seconds
2. Display "THE" for 2 seconds
3. Display "DOG" for 2 seconds
4. Repeat

### Example 3: Long Scrolling Message
**Input:** "Welcome to our amazing LED sign system!"  
**Result:** Smooth scrolling text across all boards (auto-wraps)

### Example 4: Single Board Alert
**Input:** "!"  
**Result:** Exclamation mark displayed on board 1

---

## 🔧 Troubleshooting

### LEDs Don't Light Up

1. **Check power supply**
   - Is 5V reaching LED boards?
   - Is GND connected between ESP32 and power supply?

2. **Check GPIO pin**
   - Verify correct GPIO number in code
   - Check physical connection to board

3. **Check LED type**
   - Must be WS2812B (or compatible)
   - Check `LED_TYPE` in code

### WiFi Connection Issues

1. **Can't see "LED_SIGN_AP" network**
   - Wait 30 seconds after power on
   - Check ESP32 serial monitor for errors
   - Verify ESP32 has power (USB or external 5V)

2. **Can't access http://ledsign.local**
   - Try IP address instead: http://192.168.4.1
   - Check if connected to LED_SIGN_AP network
   - Some devices don't support mDNS - use IP

### Display Issues

1. **Characters look wrong**
   - Verify NUM_SIGNS matches physical setup
   - Check LED matrix wiring (data flow direction)

2. **Scrolling is choppy**
   - Reduce scroll speed (higher ms value)
   - Check WiFi signal strength

3. **Colors are wrong**
   - Check `COLOR_ORDER` in code (should be GRB for WS2812B)
   - Some clones use RGB order

---

## 📊 Technical Specifications

### LED Matrix
- **Type:** WS2812B (NeoPixel compatible)
- **Size:** 20×20 pixels per board
- **Total pixels:** 400 per board × NUM_SIGNS
- **Control:** Single wire per board (data)
- **Voltage:** 5V DC
- **Color:** 24-bit RGB (16.7M colors)
- **Current draw:** ~24A max per board @ 100% white (typical: 5A @ 25%)

### ESP32 Controller
- **Chip:** ESP-WROOM-32
- **WiFi:** 2.4GHz 802.11 b/g/n
- **CPU:** Dual-core 240MHz
- **Flash:** 4MB
- **GPIO:** 34 pins (8 used for LED data)
- **Voltage:** 3.3V logic, 5V power input via USB

### Performance
- **Refresh Rate:** 60+ FPS
- **Scroll Speed:** 20-200ms per pixel
- **Effect Update:** Real-time via web interface
- **WiFi Range:** ~30 meters (open space)

### Power Requirements

**Wall Power (AC/DC Power Supplies):**

| Configuration | Typical @ 25% | Max @ 100% | Recommended PSU |
|---------------|---------------|------------|-----------------|
| 1 board | 5A @ 5V | 24A | 10A 5V |
| 2 boards | 10A @ 5V | 48A | 20A 5V |
| 4 boards | 20A @ 5V | 96A | 30A+ 5V or Multiple PSUs |
| 8 boards | 40A @ 5V | 192A | Multiple PSUs Required |

**Battery Power (Portable with LiFePO4):**

| Configuration | Power Draw | Runtime (1 battery) | Runtime (2 batteries) |
|---------------|------------|--------------------|-----------------------|
| 1 board | 25W | 83 minutes | 166 minutes (2.8 hrs) |
| 2 boards | 50W | 41 minutes | 83 minutes (1.4 hrs) |
| 3 boards | 75W | 28 minutes | 55 minutes |
| 4 boards | 100W | 21 minutes | 41 minutes |

**Notes:**
- Power consumption at 25% brightness (typical usage)
- Maximum values are for all LEDs at 100% white (rarely used)
- Battery runtime based on 6V 6Ah LiFePO4 (36Wh energy)
- Two batteries = connect in parallel for double capacity
- See "Battery Power" section for complete setup details

---

## 📁 File Structure

```
LED_Sign_Project/
├── LED_Sign_WiFi_MultiBoard.ino       # Main program
├── character_patterns.h               # 20×20 font library
├── scrolling_text.h                   # Scrolling text engine
├── battery_hanger_-_led_sign_v4.stl   # 3D printable battery mount
└── README.md                          # This file
```

**3D Print Files:**
- `battery_hanger_-_led_sign_v4.stl` - Battery holder/hanger for portable operation
  - Compatible with LiFePO4 6V 6Ah batteries
  - Integrated cable management
  - Print with 20-30% infill, PLA or PETG

---

## 🚀 Future Enhancements

**Display & Features:**
- [ ] Save/load presets to EEPROM
- [ ] Schedule messages by time of day
- [ ] Add more fonts (5×7, 8×8, variable width)
- [ ] Advanced animations library
- [ ] QR code display support

**Connectivity:**
- [ ] MQTT integration for IoT control
- [ ] Home Assistant integration
- [ ] Google Home / Alexa voice control
- [ ] Mobile app (native iOS/Android)
- [ ] Bluetooth LE control option

**Power & Hardware:**
- [ ] Solar charging for batteries
- [ ] Power consumption optimization
- [ ] Sleep mode for extended battery life
- [ ] Temperature monitoring
- [ ] Battery level indicator in web UI

**Advanced:**
- [ ] Multi-sign synchronization
- [ ] Cloud message scheduling
- [ ] Live data feeds (weather, stocks, news)
- [ ] Custom symbol designer in web UI

---

## 📄 License

Open source - MIT License

---

## 🤝 Contributing

Contributions welcome! Please test thoroughly before submitting pull requests.

---

## ⚠️ Safety Warnings

**Power & Electrical:**
1. **Power supply sizing** - Underpowered supplies cause brownouts
2. **Heat dissipation** - High brightness generates heat, add cooling if needed
3. **Current limiting** - Use proper gauge wire for current load (16-18 AWG minimum)
4. **Polarity** - Double-check +5V and GND connections before powering on
5. **Common ground** - ALL grounds must be connected together (critical!)

**Battery Safety (if using battery power):**
1. **BMS required** - Only use batteries with built-in BMS protection
2. **Correct voltage** - Set XL4015 to exactly 5.0V before connecting LEDs
3. **Don't over-discharge** - Stop using when battery reaches 5.8V
4. **Proper charging** - Use USB charging built into battery, don't bypass BMS
5. **No short circuits** - Keep battery terminals protected
6. **Temperature** - Don't charge or use in extreme temperatures (<0°C or >40°C)
7. **Storage** - Store at 60% charge for long-term, fully charged for short-term

**Visual & Operational:**
1. **Eye safety** - Don't stare directly at LEDs at full brightness
2. **Ventilation** - Ensure adequate airflow, especially at high brightness
3. **Mounting** - Secure all components properly, especially batteries

---

## 📞 Support

For issues or questions:
1. Check this README thoroughly
2. Review wiring against pinout diagram
3. Check serial monitor for error messages
4. Verify power supply voltage and current

---

**Built with ❤️ for makers and LED enthusiasts**

Version 3.0 - Battery power support with 3D printable mount | 20×20 LED matrices | Portable operation
