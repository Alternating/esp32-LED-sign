# ⚡ XL4015 Buck Converter Setup Guide for LED Sign

## Why This Converter is Perfect

Your **XL4015 DC-DC Buck Converter** specs:
- ✅ **Input:** 4-38V (works with 6V lead acid AND lithium 2S)
- ✅ **Output:** 1.25-36V adjustable (perfect for 5V LEDs)
- ✅ **Current:** 5A max (4.5A recommended)
- ✅ **Efficiency:** 96% (excellent!)
- ✅ **Power:** 75W max (enough for 1 board at full power)
- ✅ **Price:** 3-pack (can use one per board)

### Compared to LM2596:

| Feature | XL4015 (Your Choice) | LM2596 (My Original) |
|---------|---------------------|---------------------|
| **Efficiency** | 96% ✓ | 85% |
| **Current** | 5A ✓ | 3A |
| **Input Range** | 4-38V ✓ | 4.5-40V |
| **Heat** | Less ✓ | More |
| **Cost** | $10-12 for 3 | $10-12 for 2 |

**Winner: XL4015** - Better in every way!

---

## Compatibility Check

### ✅ Works With 6V Lead Acid Battery
```
Lead Acid Voltage Range: 5.5V - 7.2V
XL4015 Input Range:      4V - 38V
Result: PERFECT MATCH ✓
```

### ✅ Works With Lithium 2S (2 cells in series)
```
Lithium 2S Range: 6V - 8.4V
XL4015 Input Range: 4V - 38V
Result: PERFECT MATCH ✓
```

### ❌ Does NOT Work With Lithium 1S (single cell)
```
Lithium 1S Range: 2.5V - 4.2V
XL4015 Input Range: 4V minimum
Result: Need BOOST converter for 1S
```

**Bottom Line:** Use XL4015 for lead acid or 2S lithium. For 1S lithium, you'd need a boost converter instead.

---

## Power Capacity Analysis

### Single Board (400 LEDs at 25% brightness)
```
Typical Draw: 5A @ 5V = 25W
XL4015 Rating: 5A max, 4.5A recommended
Recommendation: USE ONE CONVERTER PER BOARD ✓

At 96% efficiency:
Input Power = 25W ÷ 0.96 = 26W
From 6V battery: 26W ÷ 6V = 4.3A (safe)
```

### Two Boards (800 LEDs at 25% brightness)
```
Typical Draw: 10A @ 5V = 50W
XL4015 Rating: 5A max per converter
Recommendation: USE TWO CONVERTERS (one per board) ✓

Option A: Two separate converters
- Converter 1: Board 1 (5A)
- Converter 2: Board 2 (5A)
- Total from battery: 8.6A @ 6V

Option B: Parallel converters (NOT recommended)
- Risk of current imbalance
- One may overload
```

### Four Boards (1600 LEDs at 25% brightness)
```
Typical Draw: 20A @ 5V = 100W
XL4015 Rating: 5A max per converter
Recommendation: USE FOUR CONVERTERS (one per board) ✓
```

---

## Wiring Configurations

### Configuration 1: Single Board with 6V Lead Acid

```
┌─────────────────┐
│  6V 5Ah SLA     │
│                 │
│  + Terminal     │
│  │              │
│  └──────────────┼──────────┐
│                 │          │
│  - Terminal     │          │
│  │              │          │
└──┼──────────────┘          │
   │                         │
   │                         │
   │      ┌──────────────┐   │
   └──────┤ IN-          │   │
          │              │   │
   ┌──────┤ IN+  XL4015  │   │
   │      │              │   │
   │      │ OUT+         ├───┼────→ LED Board VCC
   │      │              │   │
   │      │ OUT-         ├───┼────→ LED Board GND
   │      └──────────────┘   │
   │                         │
   └─────────────────────────┼────→ ESP32 GND
                             │
                             └────→ All GND Common
```

**Critical:** All grounds must connect together!

---

### Configuration 2: Two Boards with 6V Lead Acid

```
                     ┌─────────────┐
                     │  6V 5Ah SLA │
                     │             │
                     │  +          │
                     │  │          │
                     │  └──┬───────┼─────────────┐
                     │     │       │             │
                     │  -  │       │             │
                     │  │  │       │             │
                     └──┼──┘       │             │
                        │          │             │
                ┌───────┴──┐   ┌───┴─────┐       │
                │          │   │         │       │
      ┌─────────┤ XL4015   │   │ XL4015  │       │
      │         │ #1       │   │ #2      │       │
      │         │          │   │         │       │
      │    OUT+ ├──────────┼───┼─────────┼──→ Board 1 VCC
      │         │          │   │         │
      │    OUT- ├──┐       │   │    OUT+ ├──→ Board 2 VCC
      │         │  │       │   │         │
      └─────────┘  │       │   │    OUT- ├──┐
                   │       │   │         │  │
                   └───────┼───┴─────────┼──┼→ All GND
                           │             │  │
                           └─────────────┴──┘
```

**Key Points:**
- Both converters share same battery input
- Each converter powers one board independently
- All grounds connect together
- Battery draws 8-9A total

---

### Configuration 3: Lead Acid in Parallel (Extended Runtime)

```
┌──────────┐    ┌──────────┐
│ Battery  │    │ Battery  │
│ 6V 5Ah   │    │ 6V 5Ah   │
│ #1       │    │ #2       │
└─┬──────┬─┘    └─┬──────┬─┘
  │+     │-       │+     │-
  │      │        │      │
  │      └────┬───┘      │
  │           │          │
  └───────┬───┴──────────┘
          │
          ├──────→ XL4015 Input (still 6V, but 10Ah capacity)
          │
          └──────→ Doubles runtime!
```

**Result:**
- Voltage: Still 6V
- Capacity: 10Ah (double)
- Runtime: Doubles from 49 min to 98 min (1 board)
- Weight: 4.4 lbs (double)

---

### Configuration 4: Lithium 2S (2 cells series) with XL4015

```
[Cell 1]     [Cell 2]
  4.2V   ───   4.2V        = 8.4V max, 7.26V nominal
    │           │
    └─────┬─────┘
          │
    ┌─────┴──────┐
    │  2S BMS    │ ← CRITICAL: Don't skip this!
    │  15A       │
    │  Balance   │
    └─────┬──────┘
          │
    ┌─────┴──────┐
    │  XL4015    │
    │  Buck Conv │
    │  Set to 5V │
    └─────┬──────┘
          │
          ├────→ LED Board VCC
          │
          └────→ GND (common with ESP32)
```

**Notes:**
- Input voltage varies: 6V-8.4V depending on charge
- XL4015 regulates to constant 5V output
- BMS is MANDATORY for lithium cells

---

## Setup Procedure

### Step 1: Set Output Voltage (BEFORE connecting LEDs!)

**What You Need:**
- Multimeter
- Small screwdriver
- 6V battery (or any 4-38V source)

**Process:**
1. Connect battery to XL4015 input (IN+ and IN-)
2. Do NOT connect LEDs yet
3. Turn on XL4015 (some have power switch)
4. Measure voltage between OUT+ and OUT-
5. Use screwdriver to adjust potentiometer (small screw)
   - Turn clockwise = higher voltage
   - Turn counter-clockwise = lower voltage
6. Adjust until multimeter reads **exactly 5.0V**
7. Double-check: Should be 4.95V - 5.05V
8. Mark the setting (use paint pen or label)

**Important:** Some modules have TWO potentiometers:
- One for voltage
- One for current limit
- The voltage one is usually marked "VOUT" or is the larger one

---

### Step 2: Load Test (Before connecting sign)

**Test with resistor:**
1. Get 5Ω 10W resistor (draws 1A @ 5V)
2. Connect to XL4015 output
3. Voltage should stay 4.9-5.1V
4. Converter should be warm but not hot
5. Run for 5 minutes

**If you don't have resistor:**
1. Connect single LED board at brightness = 10
2. Check voltage stays stable
3. Gradually increase to 25

---

### Step 3: Connect ESP32

1. Connect ESP32 GND to converter OUT-
2. Connect ESP32 5V/VIN to converter OUT+
3. ESP32 should boot normally
4. Check serial monitor for normal operation

---

### Step 4: Connect LED Boards

**For single board:**
```
XL4015 OUT+ ──→ LED VCC
XL4015 OUT- ──┬─→ LED GND
              └─→ ESP32 GND (common ground!)
```

**For multiple boards:**
```
XL4015 #1 OUT+ ──→ Board 1 VCC
XL4015 #1 OUT- ──┐
                 │
XL4015 #2 OUT+ ──┼→ Board 2 VCC
XL4015 #2 OUT- ──┤
                 ├─→ All LED GND
                 ├─→ ESP32 GND
                 └─→ Battery negative
```

---

### Step 5: Monitor During First Use

**Watch for:**
- Input voltage (battery should stay >5.5V for lead acid)
- Output voltage (should stay 5.0V ±0.1V)
- Converter temperature (warm is OK, burning hot is not)
- LED brightness stability (no dimming or flickering)

**Stop immediately if:**
- Converter gets too hot to touch (>60°C/140°F)
- Output voltage drops below 4.8V
- LEDs flicker or change color
- Burning smell

---

## Heat Management

### When Heatsink is Needed

**Your XL4015 specs say:** "Please add heatsink more than 50W"

**Calculate your power:**
```
1 Board: 25W → No heatsink needed (just warm)
2 Boards: 50W → Heatsink recommended
3+ Boards: 75W+ → Heatsink REQUIRED

Per converter:
- <4.5A / 25W: Usually fine without heatsink
- 4.5-5A / 30W+: Add heatsink
```

### Heatsink Options

**Option 1: Stick-on aluminum heatsink**
- Size: 20x20mm or larger
- Cost: $2-5 for pack
- Attach with thermal tape

**Option 2: Add cooling airflow**
- Small 5V fan
- Blow air across converter
- Especially good for enclosed builds

**Option 3: Space out converters**
- Don't stack them
- Allow air circulation
- Keep away from ESP32 (heat interference)

**How to check:** 
- Touch converter after 5 minutes of operation
- Should be warm (40-50°C) = OK
- Too hot to hold (>60°C) = Add heatsink

---

## Current Limit Adjustment

**Some XL4015 modules have current limit potentiometer**

### How to Set (Optional but Recommended)

1. **Find current limit pot** (usually marked "IOUT" or smaller pot)
2. **Calculate max current per board:**
   - 1 board @ 25% brightness = ~5A max
   - Set limit to 5.0A
3. **Use multimeter in series:**
   - Put multimeter between converter OUT+ and LED VCC
   - Measure current
   - Adjust pot until it limits at desired current
4. **Why useful:**
   - Protects converter from overload
   - Prevents damage if LEDs short
   - Adds safety margin

---

## Efficiency Calculations

### Lead Acid 6V 5Ah → Single Board

**Without XL4015 losses:**
- Battery: 6V × 5Ah = 30Wh
- Usable: 30Wh × 80% (DOD) = 24Wh
- Runtime: 24Wh ÷ 25W = 57.6 minutes

**With XL4015 at 96% efficiency:**
- After conversion: 24Wh × 0.96 = 23Wh
- Runtime: 23Wh ÷ 25W = 55.2 minutes

**Loss:** Only 2.4 minutes! (vs 8+ minutes with LM2596)

---

## Troubleshooting

### Problem: Output voltage too low (4.5V or less)

**Causes:**
- Battery voltage too low (check input)
- Converter can't supply enough current
- Potentiometer needs adjustment

**Solutions:**
- Charge battery
- Use separate converter per board
- Adjust voltage pot clockwise

---

### Problem: Converter getting very hot

**Causes:**
- Drawing too much current (>5A)
- Poor ventilation
- Input voltage too high (>38V)

**Solutions:**
- Check actual current draw
- Add heatsink
- Ensure good airflow
- Use separate converters for multiple boards

---

### Problem: LEDs flickering

**Causes:**
- Input voltage fluctuating
- Loose connections
- Ground not common

**Solutions:**
- Check battery voltage under load
- Add capacitor across battery (1000µF)
- Verify all grounds connected
- Tighten all connections

---

### Problem: ESP32 rebooting randomly

**Causes:**
- Voltage drops during LED updates
- Not enough current capacity
- ESP32 and LEDs on separate grounds

**Solutions:**
- Add 1000µF capacitor at ESP32 power
- Ensure common ground (critical!)
- Use separate converter if needed
- Check connections

---

## Shopping List with XL4015

### Complete Setup for 1 Board (Lead Acid)

```
☐ 1× 6V 5Ah SLA/AGM battery           $20
☐ 1× XL4015 buck converter (3-pack)   $12 (you get 3)
☐ 1× 6V battery charger                $12
☐ Wire, terminals, heat shrink         $5
☐ Optional: Heatsink kit               $5
☐ Optional: Multimeter (if don't have) $15
──────────────────────────────────────────
Total: $49 (without optional items)
```

### Complete Setup for 2 Boards (Lead Acid)

```
☐ 2× 6V 5Ah SLA/AGM battery           $40
☐ 1× XL4015 buck converter (3-pack)   $12 (use 2 of 3)
☐ 1× 6V battery charger                $12
☐ Wire, terminals, heat shrink         $8
☐ Optional: 2× Heatsink                $5
──────────────────────────────────────────
Total: $72
```

### Complete Setup for 1 Board (Lithium 2S2P)

```
☐ 4× LG M50 21700 cells                $28
☐ 1× 2S BMS 15A with balance           $12
☐ 2× 21700 2-cell holders              $6
☐ 1× XL4015 buck converter (3-pack)   $12 (use 1)
☐ 1× 2S lithium charger (balance)     $20
☐ Wire, connectors                     $5
☐ Nickel strip (if spot welding)      $5
──────────────────────────────────────────
Total: $88
```

---

## Advantages of Having 3-Pack

Since your XL4015 comes in **3-pack**, you have flexibility:

**Scenario 1: Single board**
- Use 1 converter
- Keep 2 as spares

**Scenario 2: Two boards**
- Use 2 converters (one per board)
- Keep 1 as spare

**Scenario 3: Three boards**
- Use all 3 converters
- Perfect match!

**Scenario 4: Four boards**
- Need to buy second 3-pack
- Use 4 of 6 converters
- 2 spares

---

## Performance Comparison

### XL4015 vs LM2596 (Both from 6V lead acid)

| Metric | XL4015 | LM2596 |
|--------|--------|---------|
| Input current (1 board) | 4.3A | 4.9A |
| Power loss (1 board) | 1.0W | 3.75W |
| Converter heat | Warm | Hot |
| Runtime (1 board) | 55 min | 49 min |
| Runtime gain | **+12%** | Baseline |

**Winner:** XL4015 gives you 6 extra minutes per charge!

---

## Final Recommendations

### ✅ Best Setup for Your Situation

**1 Board:**
- 1× 6V 5Ah lead acid
- 1× XL4015 (from your 3-pack)
- 55 minute runtime
- Total: $32

**2 Boards:**
- 2× 6V 5Ah lead acid in parallel
- 2× XL4015 (from your 3-pack)
- 55 minute runtime per 2 boards
- Total: $52

**1 Board (Portable):**
- 4× LG M50 cells (2S2P)
- 1× 2S BMS
- 1× XL4015
- 2.5 hour runtime
- Total: $88

---

## Safety Checklist

### Before First Power-On

☐ Output voltage set to exactly 5.0V  
☐ All connections tight and insulated  
☐ Battery fully charged  
☐ Common ground verified (battery - , OUT-, ESP32 GND, LED GND)  
☐ No short circuits (check with multimeter)  
☐ Converter secured (not loose)  
☐ Clear of metal objects  

### During Operation

☐ Monitor converter temperature (should be warm, not hot)  
☐ Check input voltage (>5.5V for lead acid, >6V for lithium)  
☐ Check output voltage (4.95-5.05V)  
☐ Watch for LED stability (no flickering)  
☐ Listen for unusual sounds (buzzing is normal at low volumes)  

### After Use

☐ Disconnect battery (or use switch)  
☐ Let converter cool before touching  
☐ Store battery properly (recharge lead acid, 40-60% for lithium)  
☐ Check connections for looseness  

---

## Quick Reference: XL4015 Specs

```
Input:       4V - 38V ✓ (perfect for 6V and 2S lithium)
Output:      1.25V - 36V (adjustable)
Current:     5A max, 4.5A recommended
Efficiency:  96% (excellent!)
Power:       75W max
Heat:        Add heatsink if >50W
Size:        54×23×18mm
Price:       $10-12 for 3-pack
```

---

**Bottom Line:** Your XL4015 choice is excellent! It's more efficient, handles more current, and runs cooler than alternatives. The 3-pack means you can expand to 3 boards easily.

**Next Step:** Tell me how many boards you want to power and I'll give you exact wiring diagrams!
