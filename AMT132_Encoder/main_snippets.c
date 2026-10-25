/* =============================================================================
 * main_snippets.c — NOT a file to add to the project.
 * Copy each block into Core/Src/main.c between the matching
 * USER CODE BEGIN / USER CODE END comments that CubeMX already put there.
 * (Code outside those comments is deleted when CubeMX regenerates.)
 * ============================================================================= */


/* ---- 1. Near the top of main.c ------------------------------------------- */
/* USER CODE BEGIN Includes */
#include "encoder.h"
/* USER CODE END Includes */


/* ---- 2. Just below that ---------------------------------------------------- */
/* USER CODE BEGIN PV */
Encoder enc;                       /* the encoder "object" */
/* USER CODE END PV */


/* ---- 3. Inside main(), after all the MX_..._Init() calls ------------------- */
/* USER CODE BEGIN 2 */

/* QUICK START (ENC_USE_CAPTURE = 0): no capture timer */
Encoder_Init(&enc, &htim5, NULL);

/* FULL MODE (ENC_USE_CAPTURE = 1): use this line instead of the one above
 * (htim2 only exists once TIM2 is enabled in CubeMX)
 * Encoder_Init(&enc, &htim5, &htim2);
 */

HAL_TIM_Base_Start_IT(&htim6);     /* start sampling AFTER Encoder_Init */
/* USER CODE END 2 */


/* ---- 4. Inside while (1) ---------------------------------------------------- */
/* USER CODE BEGIN 3 */
{
    char line[128];
    int n = Encoder_FormatStatus(&enc, line, sizeof line);
    HAL_UART_Transmit(&huart2, (uint8_t *)line, n, 20);   /* USART2 = USB to PC */
    HAL_Delay(100);                                       /* print 10x per second */
}
/* USER CODE END 3 */


/* ---- 5. Near the bottom of main.c --------------------------------------------- */
/* USER CODE BEGIN 4 */

/* TIM6 fires ENC_SAMPLE_HZ times per second -> update the encoder. */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        Encoder_Update(&enc);
    }
}

/* FULL MODE ONLY: TIM2 captured an edge of A -> record it.
 * In quick start you can leave this out, or leave it in (it's never called). */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        Encoder_OnCapture(&enc);
    }
}

/* USER CODE END 4 */

/* If the build says HAL_TIM_PeriodElapsedCallback is defined twice, CubeMX
 * already made one: delete this one and put the "if (htim->Instance == TIM6)"
 * block inside CubeMX's version. */
