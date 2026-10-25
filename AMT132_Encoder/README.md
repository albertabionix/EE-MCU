# AMT132S-V Encoder Driver — NUCLEO-F446RE

Reads a Same Sky AMT132S-V incremental encoder on a NUCLEO-F446RE and reports
position, angle and speed. C, ST HAL, STM32CubeIDE.

| File | What it is |
|---|---|
| `encoder.h` | Settings, wiring, list of functions |
| `encoder.c` | The driver code |
| `main_snippets.c` | Exactly what to paste into `main.c` |
| `main_snippets_csv.c` | Version of blocks 2 and 3 for graphing (CSV output) |
| `plot_encoder.py` | Live graph + CSV logger (Python) |
| `README.md` | This guide |

**Limitation:** this is an incremental encoder. Zero is wherever the shaft was
at power-up; it does not know the leg's true angle.

---

## 1. Two modes

| Mode | `ENC_USE_CAPTURE` | Timers | Wires | Speed |
|---|---|---|---|---|
| **Quick start (do this first)** | `0` | TIM5, TIM6 | 4 | Fine above ~100 RPM, jumpy below |
| **Full** | `1` | TIM5, TIM6, TIM2 | 4 + 1 jumper | Precise at all speeds (M/T) |

---

## 2. Wiring (board unplugged)

| Encoder wire | Board pin | Where on the NUCLEO-F446RE |
|---|---|---|
| GND | GND | **GND** on the left power header (CN6) |
| 5V | 5V | **5V** on the left power header (CN6) |
| A+ | PA0 | **A0** on the left analog header (CN8) |
| B+ | PA1 | **A1** on the left analog header (CN8) |
| Z+ | — | Not used. Tape the end so it can't short |
| **Full mode only:** A+ (jumper) | PA15 | **CN7 pin 17**: outer column of the left double row of male pins, 9th from the top |

- PA0, PA1 and PA15 are 5 V tolerant (FT) on the STM32F446. **Never use PA4 or
  PA5 for encoder signals**: they are not 5 V tolerant. (To confirm, check the
  "I/O structure" column of the pin table in the STM32F446 datasheet.)
- CN7 pin 18, right next to PA15, is +5V. Count carefully.
- Check 5V and GND twice before plugging in USB.
- Remove the encoder's placement and alignment tools (datasheet page 7, step 4)
  and check the shaft spins freely.

---

## 3. CubeMX (open the `.ioc` file)

New project: **File → New → STM32 Project → Board Selector → NUCLEO-F446RE**,
and answer **Yes** to "initialize all peripherals with their default mode"
(this sets up USART2 to the PC for you).

**Clock (Clock Configuration tab):** note the value in **APB1 Timer clocks (MHz)**.
You need it below. If possible use HSE (System Core → RCC → HSE: BYPASS Clock
Source) as the PLL source for accurate timing.

**TIM5 — position (encoder)**
- Combined Channels: **Encoder Mode** → this puts CH1 on **PA0** and CH2 on **PA1**
- Prescaler **0**, Counter Period **4294967295**
- Encoder Mode: **Encoder Mode TI1 and TI2**
- Input Filter: **8** on both channels

**TIM6 — sample clock**
- Activated: ✓
- Prescaler: (APB1 Timer clock in MHz) − 1 → **89** for 90 MHz, **83** for 84 MHz, **15** for 16 MHz
- Counter Period: **9999** (100 Hz). For 1000 Hz use 999 and set `ENC_SAMPLE_HZ 1000u`
- NVIC Settings: enable **TIM6 global interrupt**

**TIM2 — edge timestamps (full mode only)**
- Clock Source: Internal Clock
- Channel 1: **Input Capture direct mode**
- In the chip view, make sure TIM2_CH1 is on **PA15** (click PA15 → TIM2_CH1). It
  must NOT be on PA0 (TIM5 uses it) or PA5.
- Prescaler: **8** for 90 MHz, **7** for 84 MHz, **0** for 16 MHz
- Counter Period **4294967295**
- Polarity **Rising Edge**, IC Selection **Direct**, Prescaler Division Ratio
  **Divided by 8**, Input Filter **8**
- NVIC Settings: enable **TIM2 global interrupt**

Save (Ctrl+S) and generate code.

---

## 4. Code

1. Copy `encoder.h` → `Core/Inc/`, `encoder.c` → `Core/Src/`.
2. In `encoder.h`, check:
   - `ENC_USE_CAPTURE` → `0` for quick start, `1` for full mode
   - `ENC_TICK_HZ` (full mode) → 10000000u for 90 MHz, 10500000u for 84 MHz, 16000000u for 16 MHz
   - `ENC_GEAR_RATIO` → 1.0f until the team gives you the gearbox ratio
3. Paste the pieces from `main_snippets.c` into `Core/Src/main.c` between the
   matching `USER CODE BEGIN` / `END` comments.
4. Build (hammer icon), then Run (green play button).

---

## 5. Test

Serial terminal at **115200 baud** on the Nucleo's COM port (STMicroelectronics
STLink Virtual COM Port). Expect:

```
pos=0 joint=0.00deg speed=0.00deg/s rpm=0.00 samples=1234 caps=0
```

| Check | Expected |
|---|---|
| `samples` | Rises by 100 per second |
| One full turn by hand | `pos` changes by about ±8192, `joint` by about ±360 |
| Reverse direction | Sign flips |
| `caps` (full mode) | Rises by 256 per turn |
| Stop | Speed reaches 0 within 0.2 s |

---

## 6. Graphing the data

1. In `main.c`, replace blocks 2 and 3 with the ones in `main_snippets_csv.c`.
   The board then sends CSV lines at 100 Hz:
   `time_ms,pos_counts,joint_deg,joint_speed_degs,rpm`
2. Install Python (python.org, tick "Add python.exe to PATH"), then run
   `pip install pyserial matplotlib`.
3. Close PuTTY, then run `python plot_encoder.py COM4` (use your COM port).
   It shows live angle and speed graphs and saves an `encoder_<date>_<time>.csv`
   file you can open in Excel.

---

## 7. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Nothing prints | Wrong COM port / baud, or USART2 not initialized |
| `samples` stays 0 | TIM6 interrupt not enabled, or `HAL_TIM_Base_Start_IT(&htim6)` missing |
| `pos` never changes | Wiring (A0/A1), no 5 V, TIM5 not in Encoder Mode, or encoder tools still installed |
| ~2048 or ~4096 per turn | Encoder Mode set to TI1 or TI2 only |
| Direction backwards | Swap A+ and B+, or make `ENC_GEAR_RATIO` negative |
| `caps` stays 0 (full) | PA15 jumper missing, TIM2_CH1 not on PA15, TIM2 interrupt off |
| Speed off by a constant factor | `ENC_TICK_HZ` / `ENC_SAMPLE_HZ` don't match CubeMX |
| Position drifts with motor running | Electrical noise: keep encoder wires away from motor wires |

In the debugger, add `enc` to **Live Expressions** to watch all values live.

---

## 8. Open questions for the team

1. Which project does the joystick/motor code live in, and which timers does it use?
2. Gear ratio between motor and joint (`ENC_GEAR_RATIO`)?
3. How do we get the leg's true angle at power-up (absolute encoder, homing to a
   hard stop then `Encoder_Zero()`, or a fixed startup pose)?
4. Where does the data go, at what rate and in what units?
5. Sample rate: 100 Hz for logging, 1000 Hz if a control loop uses it.

## 9. Ideas for later

- Fault detection (encoder unplugged, impossible speed, angle out of range)
- Z index (encoder Z+) as a once-per-turn reference
- Logging raw counts and timestamps for offline analysis
