/* =============================================================================
 * encoder.h — AMT132S-V quadrature encoder driver (STM32, HAL)
 * =============================================================================
 * Gives you: position, motor angle, joint angle, and speed (RPM and deg/s).
 * Full setup steps are in README.md. Quick version below.
 *
 * -----------------------------------------------------------------------------
 * WIRING for NUCLEO-F446RE  (encoder cable -> board header). Board UNPLUGGED.
 * -----------------------------------------------------------------------------
 *   Encoder wire   Board pin   Where on the board
 *   ------------   ---------   ---------------------------------------------
 *   GND            GND         "GND" on the left power header (CN6)
 *   5V             5V          "5V"  on the left power header (CN6)
 *   A+             PA0         "A0"  on the left analog header (CN8)
 *   B+             PA1         "A1"  on the left analog header (CN8)
 *   Z+             (none)      Leave unconnected; tape the end so it can't short.
 *
 *   FULL MODE ONLY (ENC_USE_CAPTURE = 1): also run a jumper from A+ to
 *   PA15 = CN7 pin 17 = the OUTER column of the left-hand double row of pins,
 *   9th pin from the top. Careful: the pin right next to it (inner column,
 *   CN7 pin 18) is +5V.
 *
 *   !! The encoder outputs 5 V. PA0, PA1 and PA15 are 5 V tolerant ("FT") on
 *      the STM32F446. Do NOT move A+ or B+ to PA4 or PA5: those are not.
 *   !! Check 5V and GND twice before plugging in USB.
 *   !! Keep the shaft still for the first 0.25 s after power-up (datasheet).
 *
 * -----------------------------------------------------------------------------
 * TIMERS USED (set up in CubeMX, see README.md)
 * -----------------------------------------------------------------------------
 *   TIM5  Encoder Mode on PA0 + PA1   -> counts A/B edges = position   (32-bit)
 *   TIM6  Basic timer + interrupt     -> calls Encoder_Update() ENC_SAMPLE_HZ times/s
 *   TIM2  Input Capture CH1 on PA15   -> timestamps edges of A (full mode only)
 *
 *   Why does A go to TWO pins in full mode?
 *     One pin can only feed one timer. TIM5 uses A to COUNT edges (position).
 *     TIM2 uses A to TIMESTAMP edges (precise speed, the M/T method).
 *     Start with ENC_USE_CAPTURE = 0 (no jumper, no TIM2). Position works
 *     fully; speed comes from the simpler window method.
 * ============================================================================= */
#ifndef ENCODER_H
#define ENCODER_H

#include "main.h"
#include <stdint.h>
#include <stddef.h>


/* =============================================================================
 * CONFIGURATION — CHANGE THESE TO MATCH YOUR SETUP
 * ============================================================================= */

/* 0 = QUICK START: position + window-method speed. Only TIM5 + TIM6. Start here.
 * 1 = FULL: adds TIM2 + the PA15 jumper for precise M/T speed at all speeds.
 *     Switch to 1 once quick start works. */
#define ENC_USE_CAPTURE      0

/* Encoder resolution. 2048 is the factory default. Only change this if
 * someone reprogrammed the encoder with AMT Viewpoint. */
#define ENC_PPR              2048u
#define ENC_CPR              (ENC_PPR * 4u)      /* counts per rev = 8192 */

/* How often Encoder_Update() runs (TIM6 interrupt rate). MUST match TIM6.
 *   100  (every 10 ms): bench testing, logging, printing to a laptop.
 *   1000 (every 1 ms):  when a control loop uses the speed/angle to drive
 *                       the leg. Fresher data (1 ms old instead of 10 ms).
 *   To switch to 1000: set this to 1000u AND change TIM6's Counter Period in
 *   CubeMX from 9999 to 999 (with a 1 MHz TIM6 tick). Nothing else changes.
 *   Note: at 1000 the window-method speed gets coarser (7.32 RPM steps
 *   instead of 0.73), but the M/T speed stays precise. Another reason to
 *   keep ENC_USE_CAPTURE = 1. */
#define ENC_SAMPLE_HZ        100u

/* Full mode only. Tick rate of the capture timer (TIM2):
 *   = (APB1 Timer clock) / (TIM2 Prescaler + 1). MUST match CubeMX.
 *   Find "APB1 Timer clocks (MHz)" in CubeMX's Clock Configuration tab:
 *     90 MHz -> TIM2 Prescaler 8 -> 10000000u  (no change needed)
 *     84 MHz -> TIM2 Prescaler 7 -> 10500000u
 *     16 MHz -> TIM2 Prescaler 0 -> 16000000u */
#define ENC_TICK_HZ          10000000u

/* TIM2 CH1 "Prescaler Division Ratio" in CubeMX: 1, 2, 4 or 8. MUST match.
 * 8 = timestamp every 8th edge, so the CPU isn't flooded at high speed. */
#define ENC_IC_DIV           8u

/* No edges for this long -> report speed 0 (the shaft has stopped). */
#define ENC_STOP_TIMEOUT_MS  200u

/* Motor turns per ONE joint turn. 1.0f if the encoder is directly on the
 * joint. 50.0f for a 50:1 gearbox. Negative if the gearbox reverses
 * direction. ASK THE MECHANICAL TEAM. Until set, "joint" = motor values. */
#define ENC_GEAR_RATIO       1.0f

/* 1 if the encoder timer is 32-bit (TIM5 on the F446 is), 0 if 16-bit. */
#define ENC_TIMER_IS_32BIT   1


/* =============================================================================
 * ENCODER STATE (you normally don't touch these directly; use the functions)
 * "volatile" = changed inside an interrupt, so always re-read from memory.
 * ============================================================================= */
typedef struct {
    TIM_HandleTypeDef *htim_enc;      /* encoder-mode timer (&htim5) */
    TIM_HandleTypeDef *htim_cap;      /* capture timer (&htim2), or NULL */

    /* Position + window speed */
    uint32_t          last_cnt;
    volatile int64_t  position;       /* counts since power-up / last zero */
    volatile int32_t  delta;          /* counts in the last sample period */
    volatile float    rpm_window;

    /* Raw capture data (written by Encoder_OnCapture) */
    volatile uint32_t cap_time;       /* tick of newest captured edge */
    volatile uint32_t cap_period;     /* ticks between last two captures, 0 = invalid */
    volatile int8_t   cap_dir;        /* +1 / -1 */
    volatile uint8_t  cap_have_prev;
    volatile int32_t  cap_index;      /* +1 per capture forward, -1 backward */
    volatile uint32_t cap_epoch;      /* +1 on each direction reversal */
    volatile uint32_t captures;       /* total captures (debug) */

    volatile float    rpm_edge;       /* edge-timing speed (comparison only) */

    /* M/T state (only used inside Encoder_Update) */
    uint32_t          mt_base_time;
    int32_t           mt_base_index;
    uint32_t          mt_base_epoch;
    uint8_t           mt_base_valid;
    float             mt_ticks_per_cap;
    volatile float    rpm_mt;         /* M/T speed: the main result */

    volatile uint32_t samples;        /* Update count; should rise by ENC_SAMPLE_HZ per second */
} Encoder;


/* =============================================================================
 * FUNCTIONS
 * ============================================================================= */

/* Call once in main(), BEFORE starting TIM6.
 * Pass NULL as htim_cap if ENC_USE_CAPTURE = 0. */
void    Encoder_Init(Encoder *enc, TIM_HandleTypeDef *htim_enc, TIM_HandleTypeDef *htim_cap);

/* Call from the TIM6 interrupt callback. */
void    Encoder_Update(Encoder *enc);

/* Call from the TIM2 capture interrupt callback (only if ENC_USE_CAPTURE = 1). */
void    Encoder_OnCapture(Encoder *enc);

/* Make the current position the new zero. */
void    Encoder_Zero(Encoder *enc);

/* Results — call these from anywhere. */
int64_t Encoder_GetPosition(Encoder *enc);          /* counts since zero */
float   Encoder_GetAngleDeg(Encoder *enc);          /* motor angle 0..360 (wraps) */
float   Encoder_GetAngleFromStartDeg(Encoder *enc); /* motor angle since start, no wrap */
float   Encoder_GetJointAngleDeg(Encoder *enc);     /* joint angle since start */
float   Encoder_GetRPM(Encoder *enc);               /* motor speed, best available method */
float   Encoder_GetJointSpeedDegS(Encoder *enc);    /* joint speed, deg/s */
float   Encoder_GetRPMWindow(Encoder *enc);         /* comparison: window method */
float   Encoder_GetRPMEdge(Encoder *enc);           /* comparison: edge method */

/* 1 if TIM6 is running Encoder_Update (samples increasing), else 0.
 * Handy first check when nothing seems to work. */
uint8_t Encoder_IsRunning(Encoder *enc);

/* Writes a one-line status like
 *   "pos=8192 joint=360.00deg speed=12.34deg/s rpm=2.06 samples=1234 caps=256\r\n"
 * into buf. Returns the number of characters. Send it with HAL_UART_Transmit. */
int     Encoder_FormatStatus(Encoder *enc, char *buf, size_t len);

#endif /* ENCODER_H */
