/* =============================================================================
 * main_snippets_csv.c — changes to main.c for graphing (CSV output at 100 Hz).
 * Replace blocks 2 and 3 in Core/Src/main.c with these. Other blocks unchanged.
 * Remove the "A=%d B=%d" pin-test lines if you added them.
 * ============================================================================= */

/* USER CODE BEGIN 2 */
  Encoder_Init(&enc, &htim5, NULL);   /* quick start: no capture timer */
  HAL_TIM_Base_Start_IT(&htim6);       /* start 100 Hz sampling AFTER Encoder_Init */

  /* Print the column names once, so Excel/Python know what each column is */
  HAL_UART_Transmit(&huart2, (uint8_t *)ENC_CSV_HEADER, sizeof(ENC_CSV_HEADER) - 1, 20);
/* USER CODE END 2 */


    /* USER CODE BEGIN 3 */
    char line[96];
    int n = Encoder_FormatCSV(&enc, line, sizeof line);
    HAL_UART_Transmit(&huart2, (uint8_t *)line, n, 20);
    HAL_Delay(10);                     /* one line every 10 ms = 100 lines per second */
  }
  /* USER CODE END 3 */
