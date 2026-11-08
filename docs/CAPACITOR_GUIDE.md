# Capacitor Guide for LED Sign System

## Quick Answer

**For each LED board (400 LEDs):**
- **1× 1000µF electrolytic** capacitor across power supply
- **1× 0.1µF ceramic** capacitor at LED data input (optional but recommended)

**Voltage rating:** 16V or 25V (must exceed 5V)

---

## Why You Need Capacitors

### Without Capacitors:
```
Power Draw Over Time:
     ▲
  6A │     ┌──┐      ┌──┐
     │     │  │      │  │
  3A │  ┌──┘  └──┐ ┌─┘  └──
     │  │        │ │
  0A │──┘        └─┘
     └────────────────────────► Time
     
Power supply struggles to respond to rapid changes
→ Voltage sags
→ LEDs flicker or show wrong colors
```

### With Capacitors:
```
Power Draw Over Time:
     ▲
  6A │     ┌──┐      ┌──┐
     │     │  │      │  │
  3A │  ┌──┘  └──┐ ┌─┘  └──
     │  │        │ │
  0A │──┘        └─┘
     └────────────────────────► Time
     
Capacitor smooths the peaks
→ Stable voltage
→ Consistent display ✅
```

---

## Capacitor Types & Sizes

### 1. Bulk Capacitance (Electrolytic)

**Purpose:** Smooth out large current fluctuations

**Recommended Sizes:**

| Boards | Capacitor Size | Quantity | Total Capacitance |
|--------|---------------|----------|-------------------|
| 1      | 1000µF        | 1        | 1000µF            |
| 2      | 1000µF        | 2        | 2000µF            |
| 3-4    | 2200µF        | 1 per board | 6600-8800µF  |
| 5-8    | 2200µF        | 1 per board | 11000-17600µF |

**Specifications:**
- **Type:** Electrolytic (aluminum)
- **Voltage:** 16V or 25V minimum
- **Size:** 1000µF, 1500µF, or 2200µF
- **Physical size:** ~13mm diameter × 20mm tall
- **Cost:** $0.50-$2 each

**Examples (Specific Part Numbers):**
- Nichicon UPW1E102MHD (1000µF, 25V) - **Recommended**
- Panasonic EEU-FM1E222 (2200µF, 25V) - For larger setups
- Rubycon 25YXF1000M (1000µF, 25V) - Budget option

---

### 2. Bypass Capacitance (Ceramic)

**Purpose:** Filter high-frequency noise on data lines

**Recommended:**
- **Size:** 0.1µF (100nF)
- **Type:** Ceramic (MLCC)
- **Voltage:** 16V minimum
- **Quantity:** 1 per board (at data input)
- **Physical size:** ~5mm × 2mm (small!)
- **Cost:** $0.10-$0.30 each

**Examples:**
- TDK C1608X7R1C104K (0.1µF, 16V)
- Murata GRM188R71C104KA01D (0.1µF, 16V)
- Samsung CL21B104KBCNNNC (0.1µF, 50V)

**Note:** These are optional but recommended for cleaner signals

---

## Where to Place Capacitors

### Single Board Setup

```
    Power Supply (5V, 10A)
         +5V  GND
          │    │
          │    │  ┌────────────────────┐
          │    └──┤- 1000µF Capacitor +├──┐
          │       └────────────────────┘  │
          │                               │
          └───────┬───────────────────────┘
                  │
          ┌───────┴────────┐
          │  LED Board 1   │
          │  (400 LEDs)    │
          │                │
          │  VCC    GND    │
          └────────────────┘
```

**Placement:**
- Capacitor as close to LED board VCC/GND as possible
- Ideally within 2 inches of power input
- Short, thick wires

---

### Multi-Board Setup (2-4 Boards)

```
    Power Supply (5V, 30A)
         +5V  GND
          │    │
          ├────┴───────┐
          │            │
       ┌──┴─────┐   ┌──┴─────┐   ┌────────┐   ┌────────┐
       │ 2200µF │   │ 2200µF │   │ 2200µF │   │ 2200µF │
       │  Cap   │   │  Cap   │   │  Cap   │   │  Cap   │
       └──┬─────┘   └──┬─────┘   └──┬─────┘   └──┬─────┘
          │            │            │            │
      ┌───┴───┐    ┌───┴───┐    ┌───┴───┐    ┌───┴───┐
      │Board 1│    │Board 2│    │Board 3│    │Board 4│
      └───────┘    └───────┘    └───────┘    └───────┘
```

**Key Points:**
- One capacitor per board
- Each capacitor at its own board's power input
- NOT at the power supply (too far away)

---

### Power Supply Capacitor (Optional)

You can also add a large cap at the power supply itself:

```
    Power Supply
         │
    ┌────┴─────┐
    │ 4700µF   │ ← Optional: Extra smoothing
    │ 16V Cap  │    at power supply
    └────┬─────┘
         │
    Distribution to boards
```

**Size:** 4700µF or larger
**Purpose:** Smooth power supply output
**Note:** Most quality power supplies already have this internally

---

## Bypass Capacitor Placement

```
LED Board Data Input:
                           
    ESP32 GPIO ──[470Ω]──┬──→ LED Data In
                         │
                    ┌────┴────┐
                    │ 0.1µF   │ ← Ceramic capacitor
                    │ Ceramic │    (filters noise)
                    └────┬────┘
                         │
                        GND
```

**Purpose:** 
- Filters high-frequency noise
- Prevents data corruption
- Improves signal integrity

**Note:** Less critical than bulk capacitors, but helps

---

## Capacitor Polarity

### Electrolytic Capacitors - POLARITY MATTERS! ⚠️

```
Electrolytic Capacitor:
    
    ┌─────────┐
    │    +    │ ← Long lead (positive)
    │         │
    │    -    │ ← Short lead, stripe (negative)
    └─────────┘

Connection:
    + terminal → +5V power
    - terminal → GND
```

**CRITICAL:** Connect backwards = EXPLOSION! 💥

**Stripe side is NEGATIVE** - this is the most common mistake!

---

### Ceramic Capacitors - No Polarity

```
Ceramic Capacitor:
    
    ┌─────┐
    │     │ ← Either direction
    │     │    No polarity
    └─────┘
```

Can be connected either way around.

---

## Recommended Shopping List

### For 1 Board:
- **1× 1000µF, 25V electrolytic** ($0.50-$1)
- **1× 0.1µF, 16V ceramic** (optional, $0.10)

**Amazon Search:** "1000uf 25v electrolytic capacitor"

### For 2-4 Boards:
- **4× 2200µF, 25V electrolytic** ($2-$4 total)
- **4× 0.1µF, 16V ceramic** (optional, $0.50 total)

### For 5-8 Boards:
- **8× 2200µF, 25V electrolytic** ($4-$8 total)
- **8× 0.1µF, 16V ceramic** (optional, $1 total)
- **1× 4700µF, 25V electrolytic** for PSU (optional, $2)

---

## Installation Instructions

### Step 1: Identify Polarity

**Look for:**
- Stripe on one side = **NEGATIVE**
- Longer lead = **POSITIVE**
- "+" marking = **POSITIVE**

### Step 2: Trim Leads

```
Before:                After:
  Long leads          Short leads
     │ │                  │ │
  ───┘ └───            ───┘ └───
     ┃ ┃                  ┃ ┃
  ┌──┻─┻──┐            ┌──┻─┻──┐
  │ 1000µF│            │ 1000µF│
  └───────┘            └───────┘
```

Trim to about 1/4 inch (6mm) for easy soldering

### Step 3: Connect

**Method 1: Direct Solder** (Best)
```
Power Wire ──┬──→ LED Board VCC
             │
            [+] ← Capacitor positive
            [-] ← Capacitor negative
             │
Ground Wire ─┴──→ LED Board GND
```

**Method 2: Wire Wrap** (Easier)
```
1. Strip 1 inch of wire insulation
2. Wrap capacitor lead around power wire
3. Wrap other lead around ground wire
4. Solder connections
5. Heat shrink or electrical tape
```

**Method 3: Terminal Block** (Cleanest)
```
Power Supply → Terminal Block
                    ├─ To LED board
                    ├─ Capacitor +
                    └─ Capacitor -
```

### Step 4: Secure

- Use hot glue to hold capacitor in place
- Or use zip ties
- Keep leads from shorting together
- Keep capacitor upright (electrolytic only)

---

## Sizing Calculations (Technical)

### How Much Capacitance?

**Rule of thumb:** 1000µF per board minimum

**Detailed calculation:**

```
Each LED at full white:
- Current: 60mA
- 400 LEDs: 400 × 60mA = 24A peak

Capacitor sizing:
C = I × t / ΔV

Where:
- I = Current change (24A)
- t = Time duration (0.001s = 1ms)
- ΔV = Acceptable voltage drop (0.2V)

C = 24A × 0.001s / 0.2V = 120,000µF

BUT: Not all LEDs switch simultaneously
Realistic: 1000-2200µF per board is adequate
```

**In practice:**
- 1000µF handles most situations
- 2200µF for extra safety
- 4700µF+ for very demanding applications

---

## Testing Your Capacitors

### Visual Test (Before Installation)

**Bulging top?** = BAD, don't use
```
Good Cap:        Bad Cap:
  Flat top       Bulging top
    ┌─┐            ╱─╲
    │ │           │   │ ← Swollen
    └─┘            ╲─╱
```

### Multimeter Test

**Set to Capacitance mode:**
1. Touch probes to leads
2. Should read close to rated value
3. 1000µF cap should read 900-1100µF

**Polarity test (if no cap mode):**
1. Set to resistance/ohms
2. Touch probes to leads
3. Should see rising resistance (charging)
4. Reverse probes, should see same behavior

---

## Common Mistakes to Avoid

### ❌ Wrong Polarity
```
WRONG:                    RIGHT:
+5V ──────[- +]          +5V ──────[+ -]
GND ──────[+ -]          GND ──────[- +]

Result: EXPLOSION!        Result: Works ✅
```

**Always check stripe = negative!**

---

### ❌ Capacitor Too Far Away
```
WRONG:                    RIGHT:
PSU ──[Cap]──(10ft)──LEDs   PSU ──(10ft)──[Cap]──LEDs

Capacitor at PSU          Capacitor at LED board
= Ineffective             = Effective ✅
```

**Capacitor must be close to LED board!**

---

### ❌ Voltage Rating Too Low
```
WRONG:                    RIGHT:
5V system                 5V system
10V capacitor            16V or 25V capacitor

Voltage spikes can       Headroom for spikes ✅
destroy capacitor
```

**Always use 16V or higher for 5V systems**

---

### ❌ Sharing Capacitor Between Boards
```
WRONG:                    RIGHT:
    [One 1000µF]             [1000µF] [1000µF]
         │                       │         │
    ┌────┴────┐              Board 1   Board 2
  Board 1  Board 2

Inadequate capacitance    Each board has its own ✅
```

---

## Frequently Asked Questions

### Q: Do I really need capacitors?

**A:** For WS2812B LEDs, **highly recommended** but not always mandatory.

**You NEED them if:**
- Seeing random flickering
- Colors change unexpectedly
- Brightness varies
- Using long power wires (>3 feet)

**You might skip them if:**
- Very short power wires (<6 inches)
- High-quality regulated power supply
- Running at low brightness (<50%)
- Only using a few LEDs (<100)

**Our recommendation:** Always use them! Cost: <$1 per board

---

### Q: Can I use bigger capacitors?

**A:** Yes! Bigger is better (to a point).

**Safe range:**
- Minimum: 1000µF
- Recommended: 1000-2200µF  
- Maximum practical: 10,000µF

**Diminishing returns above 4700µF per board**

---

### Q: What happens if I use 10V capacitors on 5V system?

**A:** They'll work, but **not recommended**.

**Voltage rating should be 2-3× operating voltage:**
- 5V system → 16V or 25V capacitor
- Gives headroom for voltage spikes
- More reliable long-term

**10V might work but could fail under spikes**

---

### Q: Can I use multiple smaller capacitors?

**A:** Yes! Capacitors in parallel add up.

```
Two 470µF = 940µF total
Three 330µF = 990µF total
Four 220µF = 880µF total

All approximately equal to one 1000µF
```

**But simpler to use one larger cap**

---

### Q: Where do I buy these?

**Online (USA):**
- Amazon: "1000uf 25v capacitor"
- Digi-Key: More selection, bulk pricing
- Mouser: Professional components
- eBay: Cheapest, slower shipping

**Local:**
- RadioShack (if still open near you)
- Micro Center
- Fry's Electronics
- Local electronics hobby stores

**Cost:** $0.50-$2 per capacitor

---

### Q: Do ceramic and electrolytic work the same?

**A:** No! Different purposes.

| Type | Size | Purpose | Required? |
|------|------|---------|-----------|
| Electrolytic | 1000µF+ | Bulk power smoothing | **YES** |
| Ceramic | 0.1µF | High-frequency filtering | Optional |

**You need the electrolytic!**

**Ceramic is bonus for data line filtering**

---

## Installation Examples

### Example 1: Single Board on Breadboard

```
Power Rail:
    +5V ═══[1000µF Cap]═══ LED VCC
                              
    GND ══════════════════ LED GND

Capacitor legs plugged directly into breadboard
```

---

### Example 2: Permanent Installation

```
1. Cut power and ground wires
2. Strip 1/2 inch insulation
3. Twist capacitor leads into wires:
   
   Power wire ──┬── [+]
                │    Cap
   Ground wire ─┴── [-]

4. Solder connections
5. Heat shrink tube over joints
6. Secure with hot glue or zip tie
```

---

### Example 3: Terminal Block Method

```
Buy: 2-position terminal block

Wire to block:
  Position 1: +5V in, +5V out, Cap +
  Position 2: GND in, GND out, Cap -

Clean and reusable!
```

---

## Troubleshooting

### Capacitor Gets Hot

**Causes:**
- ❌ Wrong polarity (reverse it!)
- ❌ Voltage rating too low
- ❌ Bad capacitor
- ❌ Excessive current

**Solution:** Replace with correct orientation and rating

---

### Capacitor Exploded/Leaked

**Causes:**
- ❌ Definitely wrong polarity
- ❌ Voltage spike exceeded rating

**Cleanup:**
- Electrolyte is corrosive
- Wear gloves
- Clean with isopropyl alcohol
- Replace with correct capacitor

---

### No Improvement After Adding Capacitors

**Check:**
1. Capacitor is at LED board, not PSU
2. Polarity is correct
3. Connections are solid
4. Capacitor is good (test with multimeter)
5. Your main issue might be software (use fixed code!)

---

## Summary & Recommendations

### Absolute Minimum:
- **1× 1000µF, 25V per board** at LED power input

### Recommended:
- **1× 2200µF, 25V per board** at LED power input
- **1× 0.1µF ceramic per board** at data line

### Professional:
- **1× 2200µF per board**
- **1× 0.1µF per board**  
- **1× 4700µF at power supply**
- All connections soldered and secured

### Shopping List (4 Boards):
- 4× 2200µF, 25V electrolytic ($6)
- 4× 0.1µF ceramic ($1)
- Total: ~$7

**Worth it for stable, flicker-free operation! ✅**

---

Version 2.1 | Capacitor Guide for LED Signs
