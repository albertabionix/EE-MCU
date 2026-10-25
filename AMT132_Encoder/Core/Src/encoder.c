/* =============================================================================
 * encoder.c — AMT132S-V encoder driver. See encoder.h and README.md first.
 * =============================================================================
 * How it works, in one paragraph:
 *   The encoder timer (TIM5) counts every A/B edge in hardware, so its counter
 *   IS the position. 100 times per second (TIM6), Encoder_Update reads that
 *   counter and works out position and speed. For precise speed, the capture
 *   timer (TIM2) timestamps every 8th
 *   rising edge of A, and the M/T method divides "exactly n captured edges"
 *   by "exactly how many ticks between the first and last of them".
 *
 * Speed methods (all three are computed; Encoder_GetRPM returns M/T):
 *   window : counts per sample period. Simple; jumpy at low speed.
 *   edge   : time across one capture interval. Good at low speed.
 *   M/T    : n captures / exact time between them. Good at all speeds.
 * ============================================================================= */
#include "encoder.h"
#include <stdio.h>


/* ---- Conversion factors ------------------------------------------------------
 * The "f" makes a number a float (fast on STM32) instead of a double (slow).
 * (float) casts avoid integer division, where 6000 / 8192 would give 0.     */

/* RPM per count of delta:  60 s/min * samples/s / counts/rev  (0.732 at 100 Hz) */
#define K_WINDOW   (60.0f * (float)ENC_SAMPLE_HZ / (float)ENC_CPR)

/* RPM = n_captures * K_EDGE / ticks.
 * One capture = ENC_IC_DIV/PPR rev; ticks/ENC_TICK_HZ = seconds; *60 for minutes. */
#define K_EDGE     (60.0f * (float)ENC_TICK_HZ * (float)ENC_IC_DIV / (float)ENC_PPR)

/* Stop timeout converted to capture-timer ticks. */
#define STOP_TICKS ((uint32_t)ENC_STOP_TIMEOUT_MS * (ENC_TICK_HZ / 1000u))


/* ---- Helpers ---------------------------------------------------------------- */

/* Briefly block interrupts while copying values an interrupt might change
 * mid-read (e.g. the 64-bit position, which takes two 32-bit reads). */
static inline uint32_t enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* Signed change of the hardware counter. Unsigned subtraction + signed cast
 * handles the counter wrapping around (0 -> 0xFFFFFFFF reads as -1). */
static inline int32_t counter_delta(uint32_t now, uint32_t before)
{
#if ENC_TIMER_IS_32BIT
    return (int32_t)(now - before);
#else
    return (int32_t)(int16_t)(uint16_t)(now - before);
#endif
}


/* ---- Init ------------------------------------------------------------------- */

void Encoder_Init(Encoder *enc, TIM_HandleTypeDef *htim_enc, TIM_HandleTypeDef *htim_cap)
{
    enc->htim_enc         = htim_enc;
    enc->htim_cap         = htim_cap;

    /* C doesn't zero struct fields automatically. */
    enc->last_cnt         = 0;
    enc->position         = 0;
    enc->delta            = 0;
    enc->rpm_window       = 0.0f;
    enc->cap_time         = 0;
    enc->cap_period       = 0;
    enc->cap_dir          = 0;
    enc->cap_have_prev    = 0;
    enc->cap_index        = 0;
    enc->cap_epoch        = 0;
    enc->captures         = 0;
    enc->rpm_edge         = 0.0f;
    enc->mt_base_time     = 0;
    enc->mt_base_index    = 0;
    enc->mt_base_epoch    = 0;
    enc->mt_base_valid    = 0;
    enc->mt_ticks_per_cap = 0.0f;
    enc->rpm_mt           = 0.0f;
    enc->samples          = 0;

    HAL_Delay(250);   /* datasheet: 200 ms start-up, shaft must be still */

    HAL_TIM_Encoder_Start(htim_enc, TIM_CHANNEL_ALL);
    __HAL_TIM_SET_COUNTER(htim_enc, 0);

#if ENC_USE_CAPTURE
    if (htim_cap != NULL) {
        HAL_TIM_IC_Start_IT(htim_cap, TIM_CHANNEL_1);   /* "_IT" = with interrupt */
    }
#endif
}


/* ---- Capture interrupt: every ENC_IC_DIV-th rising edge of A -----------------
 * Only records data; the math happens in Encoder_Update to keep this short. */
void Encoder_OnCapture(Encoder *enc)
{
#if ENC_USE_CAPTURE
    /* Tick latched by HARDWARE at the exact edge, so it's exact even if this
     * interrupt runs late. */
    uint32_t t = HAL_TIM_ReadCapturedValue(enc->htim_cap, TIM_CHANNEL_1);

    /* Direction from the encoder timer's hardware direction bit (more reliable than
     * reading the B pin, which may have already changed at high speed). */
    int8_t dir = __HAL_TIM_IS_TIM_COUNTING_DOWN(enc->htim_enc) ? -1 : +1;

    if (enc->cap_have_prev && dir == enc->cap_dir) {
        enc->cap_period = t - enc->cap_time;   /* wraparound-safe */
    } else {
        /* First capture, or a reversal: the interval isn't a valid speed. */
        enc->cap_period = 0;
        if (enc->cap_have_prev) {
            enc->cap_epoch++;   /* tells M/T to restart after a reversal */
        }
    }

    enc->cap_index    += dir;
    enc->cap_time      = t;
    enc->cap_dir       = dir;
    enc->cap_have_prev = 1;
    enc->captures++;
#else
    (void)enc;   /* capture disabled: nothing to do ("(void)" silences a warning) */
#endif
}


/* ---- Sample interrupt: ENC_SAMPLE_HZ times per second ------------------------ */
void Encoder_Update(Encoder *enc)
{
    /* --- Position and window speed --- */
    uint32_t now_cnt = __HAL_TIM_GET_COUNTER(enc->htim_enc);
    int32_t  d       = counter_delta(now_cnt, enc->last_cnt);
    enc->last_cnt    = now_cnt;
    enc->position   += d;
    enc->delta       = d;
    enc->rpm_window  = (float)d * K_WINDOW;

#if ENC_USE_CAPTURE
    /* --- Snapshot of capture data, taken with interrupts off so the capture
     *     interrupt can't change it halfway through. --- */
    uint32_t p = enter_critical();
    uint32_t last_t = enc->cap_time;
    uint32_t period = enc->cap_period;
    int8_t   dir    = enc->cap_dir;
    uint8_t  have   = enc->cap_have_prev;
    int32_t  idx    = enc->cap_index;
    uint32_t epoch  = enc->cap_epoch;
    uint32_t now_t  = __HAL_TIM_GET_COUNTER(enc->htim_cap);
    exit_critical(p);

    uint32_t since = now_t - last_t;   /* ticks since the newest edge */

    /* --- Edge-timing speed (comparison) --- */
    if (!have || period == 0 || since > STOP_TICKS) {
        enc->rpm_edge = 0.0f;
    } else {
        /* If we've waited longer than the last interval, the shaft is slowing:
         * use the elapsed time so the reading decays instead of freezing. */
        uint32_t eff = (since > period) ? since : period;
        enc->rpm_edge = (float)dir * K_EDGE / (float)eff;
    }

    /* --- M/T speed (main result) --- */
    if (!have || since > STOP_TICKS) {
        /* Stopped: speed 0, and forget the start edge. */
        enc->rpm_mt        = 0.0f;
        enc->mt_base_valid = 0;
    }
    else if (!enc->mt_base_valid || epoch != enc->mt_base_epoch) {
        /* No start edge yet, or the shaft reversed: start a new measurement
         * from the newest edge. A speed needs a second edge, so report 0 for now. */
        enc->mt_base_time     = last_t;
        enc->mt_base_index    = idx;
        enc->mt_base_epoch    = epoch;
        enc->mt_base_valid    = 1;
        enc->mt_ticks_per_cap = 0.0f;
        enc->rpm_mt           = 0.0f;
    }
    else {
        int32_t n = idx - enc->mt_base_index;   /* captures since the start edge */

        if (n != 0) {
            /* THE MEASUREMENT: exactly n captures in exactly dt ticks. */
            uint32_t dt = last_t - enc->mt_base_time;
            enc->rpm_mt = (float)n * K_EDGE / (float)dt;

            int32_t n_abs = (n < 0) ? -n : n;
            enc->mt_ticks_per_cap = (float)dt / (float)n_abs;

            /* Newest edge becomes the next start edge. */
            enc->mt_base_time  = last_t;
            enc->mt_base_index = idx;
        }
        else if (enc->mt_ticks_per_cap > 0.0f && (float)since > enc->mt_ticks_per_cap) {
            /* No new edge this period and it's overdue: shaft is slowing.
             * Let the speed decay. (If not overdue, keep the last value.) */
            enc->rpm_mt = (float)dir * K_EDGE / (float)since;
        }
    }
#else
    /* Capture disabled: best available speed is the window method. */
    enc->rpm_mt = enc->rpm_window;
#endif

    enc->samples++;
}


/* ---- Results ------------------------------------------------------------------ */

void Encoder_Zero(Encoder *enc)
{
    /* Only the software total is reset. Resetting the timer itself would cause one
     * fake speed spike on the next update. */
    uint32_t p = enter_critical();
    enc->position = 0;
    exit_critical(p);
}

int64_t Encoder_GetPosition(Encoder *enc)
{
    uint32_t p = enter_critical();   /* 64-bit = two reads, so protect it */
    int64_t pos = enc->position;
    exit_critical(p);
    return pos;
}

float Encoder_GetAngleDeg(Encoder *enc)
{
    int64_t pos = Encoder_GetPosition(enc);
    int32_t in_rev = (int32_t)(pos % (int64_t)ENC_CPR);
    if (in_rev < 0) {
        in_rev += (int32_t)ENC_CPR;   /* C's % keeps the minus sign; fold into 0..8191 */
    }
    return (float)in_rev * (360.0f / (float)ENC_CPR);
}

float Encoder_GetAngleFromStartDeg(Encoder *enc)
{
    return (float)Encoder_GetPosition(enc) * (360.0f / (float)ENC_CPR);
}

float Encoder_GetJointAngleDeg(Encoder *enc)
{
    return Encoder_GetAngleFromStartDeg(enc) / ENC_GEAR_RATIO;
}

float Encoder_GetRPM(Encoder *enc)            { return enc->rpm_mt; }
float Encoder_GetJointSpeedDegS(Encoder *enc) { return enc->rpm_mt * 6.0f / ENC_GEAR_RATIO; } /* RPM*6 = deg/s */
float Encoder_GetRPMWindow(Encoder *enc)      { return enc->rpm_window; }
float Encoder_GetRPMEdge(Encoder *enc)        { return enc->rpm_edge; }


/* ---- Diagnostics -------------------------------------------------------------- */

uint8_t Encoder_IsRunning(Encoder *enc)
{
    /* Checks whether "samples" moved in the last 100 ms.
     * ("static" variables keep their value between calls. Supports one encoder.) */
    static uint32_t last_samples = 0;
    static uint32_t last_tick    = 0;
    static uint8_t  running      = 0;

    uint32_t now = HAL_GetTick();
    if (now - last_tick >= 100u) {
        running      = (enc->samples != last_samples);
        last_samples = enc->samples;
        last_tick    = now;
    }
    return running;
}

/* Split a float into sign / whole / two decimals, so we can print it without
 * float support in printf (which is off by default on STM32). */
static void split_x100(float v, const char **sign, long *whole, long *frac)
{
    long x = (long)(v * 100.0f + ((v >= 0.0f) ? 0.5f : -0.5f));   /* round */
    *sign  = (x < 0) ? "-" : "";
    if (x < 0) {
        x = -x;
    }
    *whole = x / 100;
    *frac  = x % 100;
}

int Encoder_FormatStatus(Encoder *enc, char *buf, size_t len)
{
    const char *s_ang, *s_spd, *s_rpm;
    long w_ang, f_ang, w_spd, f_spd, w_rpm, f_rpm;

    split_x100(Encoder_GetJointAngleDeg(enc),  &s_ang, &w_ang, &f_ang);
    split_x100(Encoder_GetJointSpeedDegS(enc), &s_spd, &w_spd, &f_spd);
    split_x100(Encoder_GetRPM(enc),            &s_rpm, &w_rpm, &f_rpm);

    int n = snprintf(buf, len,
        "pos=%ld joint=%s%ld.%02lddeg speed=%s%ld.%02lddeg/s rpm=%s%ld.%02ld samples=%lu caps=%lu\r\n",
        (long)Encoder_GetPosition(enc),
        s_ang, w_ang, f_ang,
        s_spd, w_spd, f_spd,
        s_rpm, w_rpm, f_rpm,
        (unsigned long)enc->samples,
        (unsigned long)enc->captures);

    /* snprintf returns how many characters it WANTED to write; never report
     * more than actually fit in buf. */
    if (n < 0) {
        n = 0;
    } else if ((size_t)n >= len) {
        n = (int)len - 1;
    }
    return n;
}
