/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stm32f4_discovery_audio.h"
#include "math.h"
#include "audio_settings.h"
#include "debug_uart.h"
#include "queue.h"
#include "usbd_midi.h"
#include "midi_event.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi3_tx;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t debugUartTaskHandle;
const osThreadAttr_t debugTask_attributes = {
  .name = "DebugUART",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
const osThreadAttr_t audioInitTask_attributes = {
  .name = "AudioInit",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
osThreadId_t midiTaskHandle;
const osThreadAttr_t midiTask_attributes = {
  .name = "midiTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* USER CODE BEGIN PV */
xQueueHandle midiQueue;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2S3_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */
void OnMIDIReceive(uint8_t *msg, uint32_t len);
void AudioTask(void *pv);
void MidiTask(void *pv);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define WAVETABLE_LENGTH 64
uint16_t audio_buffer[AUDIO_BUFFER_SIZE];
float wavetable[WAVETABLE_LENGTH];
volatile uint8_t audio_muted = 0;

typedef enum {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvState;

typedef struct {
    float phase;
    float frequency;
    float volume;
    uint8_t active;
    uint8_t midi_note;
    float phase_inc;

    // envelope
    EnvState env_state;
    float env_amplitude;   // current amplitude 0.0 to 1.0
    float attack_rate;     // amplitude increase per sample
    float release_rate;    // amplitude decrease per sample
} Voice;

#define MAX_VOICES 4

#define UNISON_VOICES     16
#define DETUNE_CENTS      10.0f

Voice voices[MAX_VOICES] = {0};
float MIDI_to_frequency(uint8_t note);

#define NUM_WAVETABLES 4
typedef enum {
    WT_SINE = 0,
    WT_SAW,
    WT_SQUARE,
    WT_TRIANGLE
} WavetableType;

volatile WavetableType current_wavetable = WT_SINE;
typedef struct {
    const char *name;
    void (*fill_wavetable)(void);
    float reverb_decay;
    uint32_t reverb_delay_samples;
    float attack_ms;
    float release_ms;
} SynthPreset;

void wt_supersaw(void) {
	for (int i = 0; i < WAVETABLE_LENGTH; i++)
		wavetable[i] = 0.0f;

	for (int u = 0; u < UNISON_VOICES; u++) {
		float detune_cents = -DETUNE_CENTS * 0.5f
						   + (DETUNE_CENTS * u) / (float)(UNISON_VOICES - 1);

		float ratio = powf(2.0f, detune_cents / 1200.0f);

		for (int i = 0; i < WAVETABLE_LENGTH; i++) {
			float t = (float)i / (float)WAVETABLE_LENGTH;
			float pos = fmodf(t * ratio, 1.0f);
			wavetable[i] += 2.0f * pos - 1.0f;
		}
	}

	float peak = 0.0f;
	for (int i = 0; i < WAVETABLE_LENGTH; i++) {
		float abs_val = fabsf(wavetable[i]);
		if (abs_val > peak) peak = abs_val;
	}
	if (peak > 0.0f) {
		for (int i = 0; i < WAVETABLE_LENGTH; i++)
			wavetable[i] /= peak;
	}
}

void wt_sine(void) {
    for (int i = 0; i < WAVETABLE_LENGTH; i++)
        wavetable[i] = sinf((2.0f * M_PI * i) / WAVETABLE_LENGTH);
}

void wt_square(void) {
    for (int i = 0; i < WAVETABLE_LENGTH; i++)
        wavetable[i] = (i < WAVETABLE_LENGTH / 2) ? 1.0f : -1.0f;
}

void wt_triangle(void) {
    for (int i = 0; i < WAVETABLE_LENGTH; i++) {
        float t = (float)i / WAVETABLE_LENGTH;
        wavetable[i] = (t < 0.5f) ? (4.0f*t - 1.0f) : (3.0f - 4.0f*t);
    }
}

typedef struct {
    float   decay;
    uint32_t delay_samples;
} ReverbParams;


#define REVERB_DELAY_MAX 9600
#define REVERB_DECAY 0.5f


volatile ReverbParams active_reverb = { REVERB_DECAY, REVERB_DELAY_MAX };

static const SynthPreset presets[] = {
    //  name          fill_fn       reverb_decay  delay_ms→samples   attack  release
    { "SuperSaw",   wt_supersaw,   0.7f,          4800,              10.0f,  10.0f },
    { "Sine Pad",   wt_sine,       0.75f,         9600,              1000.0f,  1000.0f },
    { "Square",     wt_square,     0.2f,           960,               5.0f,   50.0f },
    { "Triangle",   wt_triangle,   0.4f,          2400,              15.0f,  150.0f },
};
#define NUM_PRESETS (sizeof(presets) / sizeof(presets[0]))

volatile uint8_t current_preset_idx = 0;

void init_wavetable(void) {
    const SynthPreset *p = &presets[current_preset_idx];
    p->fill_wavetable();
}

uint16_t float2uint16(float f)
{
    if (f >  1.0f) f =  1.0f;
    if (f < -1.0f) f = -1.0f;
    return (uint16_t)(int16_t)(f * 32767.0f);
}

static float reverb_buffer[REVERB_DELAY_MAX] = {0};
static uint32_t reverb_write_idx = 0;

float process_reverb(float sample)
{
    uint32_t delay  = active_reverb.delay_samples;
    float    decay  = active_reverb.decay;

    if (delay == 0)            delay = 1;
    if (delay > REVERB_DELAY_MAX) delay = REVERB_DELAY_MAX;

    uint32_t read_idx = (reverb_write_idx + REVERB_DELAY_MAX - delay) % REVERB_DELAY_MAX;
    float delayed = reverb_buffer[read_idx];

    reverb_buffer[reverb_write_idx] = sample + delayed * decay;

    reverb_write_idx = (reverb_write_idx + 1) % REVERB_DELAY_MAX;

    return sample * 0.7f + delayed * 0.3f;
}
void fill_buffer(uint32_t start_frame, uint32_t num_frames)
{
    for (int frame = start_frame; frame < start_frame + num_frames; frame++) {
    	if (audio_muted) {
			audio_buffer[2 * frame]     = 0;
			audio_buffer[2 * frame + 1] = 0;
			continue;
		}

    	float mixed = 0.0f;
        int active_count = 0;

        for (int v = 0; v < MAX_VOICES; v++) {
            if (!voices[v].active) continue;

            switch (voices[v].env_state) {
                case ENV_ATTACK:
                    voices[v].env_amplitude += voices[v].attack_rate;
                    if (voices[v].env_amplitude >= 1.0f) {
                        voices[v].env_amplitude = 1.0f;
                        voices[v].env_state = ENV_SUSTAIN;
                    }
                    break;
                case ENV_RELEASE:
                    voices[v].env_amplitude -= voices[v].release_rate;
                    if (voices[v].env_amplitude <= 0.0f) {
                        voices[v].env_amplitude = 0.0f;
                        voices[v].env_state = ENV_IDLE;
                        voices[v].active = 0;
                    }
                    break;
                default:
                    break;
            }

            float phase_inc = voices[v].phase_inc;
            mixed += voices[v].env_amplitude * voices[v].volume
                     * wavetable[(uint32_t)voices[v].phase % WAVETABLE_LENGTH];

            voices[v].phase += phase_inc;
            if (voices[v].phase >= WAVETABLE_LENGTH)
                voices[v].phase -= WAVETABLE_LENGTH;

            active_count++;
        }

        if (active_count > 0)
            mixed /= MAX_VOICES;

        mixed = process_reverb(mixed);

        uint16_t sample = float2uint16(mixed);
        audio_buffer[2 * frame]     = sample;
        audio_buffer[2 * frame + 1] = sample;
    }
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
    fill_buffer(0, AUDIO_BUFFER_SAMPLES);
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
    fill_buffer(AUDIO_BUFFER_SAMPLES, AUDIO_BUFFER_SAMPLES);
}
uint16_t* get_synth_audio_buffer(void) {
	return audio_buffer;
}

void init_audio(void) {
	init_wavetable();
	if (BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_HEADPHONE, VOLUME, SAMPLE_RATE) != AUDIO_OK) {
		Error_Handler();
	}

	fill_buffer(0, AUDIO_BUFFER_SAMPLES);
	fill_buffer(AUDIO_BUFFER_SAMPLES, AUDIO_BUFFER_SAMPLES);

	BSP_AUDIO_OUT_Play(&(audio_buffer[0]), AUDIO_BUFFER_SIZE * sizeof(uint16_t));
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

	__HAL_RCC_WWDG_CLK_DISABLE();
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  __HAL_RCC_GPIOD_CLK_ENABLE();

  WWDG->CR &= ~WWDG_CR_WDGA;
  HAL_GPIO_WritePin(GPIOD, LD3_Pin, GPIO_PIN_SET);
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_I2S3_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  // Force USB disconnect BEFORE USB peripheral init
  // Disable USB OTG clock first so we can control PA12 as GPIO
  __HAL_RCC_USB_OTG_FS_FORCE_RESET();
  HAL_Delay(50);
  __HAL_RCC_USB_OTG_FS_RELEASE_RESET();

  // Now drive D+ low as plain GPIO
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET);
  HAL_Delay(100);  // Host sees disconnect here

  // Release PA12 — USB peripheral will reclaim it during USBD_Init
  HAL_GPIO_DeInit(GPIOA, GPIO_PIN_12);

  Debug_Init();
  Debug_Print("UART TEST");
  MIDI_RegisterReceiveCallback(OnMIDIReceive);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  midiQueue = xQueueCreate(64, sizeof(MidiEvent_t));
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  debugUartTaskHandle = osThreadNew(DebugUartTask, NULL, &debugTask_attributes);
  midiTaskHandle = osThreadNew(MidiTask, NULL, &midiTask_attributes);

//  xTaskCreate(DebugUartTask, "DBG",  256, NULL, 1, NULL);
//  xTaskCreate(AudioTask,     "AUD",  512, NULL, 5, NULL);
//  xTaskCreate(MidiTask,      "MIDI", 256, NULL, 3, NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 72;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2S3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S3_Init(void)
{

  /* USER CODE BEGIN I2S3_Init 0 */

  /* USER CODE END I2S3_Init 0 */

  /* USER CODE BEGIN I2S3_Init 1 */

  /* USER CODE END I2S3_Init 1 */
  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;
  hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S3_Init 2 */

  /* USER CODE END I2S3_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PDM_OUT_Pin */
  GPIO_InitStruct.Pin = PDM_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PDM_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);
  HAL_NVIC_SetPriority(EXTI0_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD6_Pin
                           Audio_RST_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void MidiTask(void *pv) {
    for (;;) {
        MidiEvent_t evt;
        xQueueReceive(midiQueue, &evt, portMAX_DELAY);

		if (evt.status >= 0x80 && evt.status <= 0x8F)
		{
			for (int v = 0; v < MAX_VOICES; v++) {
				if (voices[v].active &&
					voices[v].midi_note == evt.note &&
					voices[v].env_state != ENV_RELEASE) {
					voices[v].env_state = ENV_RELEASE;
				}
			}
		}
		else if (evt.status >= 0x90 && evt.status <= 0x9F)
		{
			if (evt.velocity == 0) {
				for (int v = 0; v < MAX_VOICES; v++) {
					if (voices[v].active &&
						voices[v].midi_note == evt.note &&
						voices[v].env_state != ENV_RELEASE) {
						voices[v].env_state = ENV_RELEASE;
					}
				}
			}
			else {
			    const SynthPreset *p = &presets[current_preset_idx];
			    float freq = MIDI_to_frequency(evt.note);
			    float vol  = evt.velocity / 127.0f;
			    float att  = 1.0f / (p->attack_ms  * 0.001f * SAMPLE_RATE);
			    float rel  = 1.0f / (p->release_ms * 0.001f * SAMPLE_RATE);

			    int target = -1;
			    float lowest_amp = 2.0f;

			    taskENTER_CRITICAL();

			    for (int v = 0; v < MAX_VOICES; v++) {
			        if (!voices[v].active) { target = v; break; }
			    }

			    if (target == -1) {
			        for (int v = 0; v < MAX_VOICES; v++) {
			            if (voices[v].env_state == ENV_RELEASE &&
			                voices[v].env_amplitude < lowest_amp) {
			                lowest_amp = voices[v].env_amplitude;
			                target = v;
			            }
			        }
			    }

			    if (target == -1) target = 0;

			    voices[target].frequency     = freq;
			    voices[target].phase_inc = (freq / SAMPLE_RATE) * WAVETABLE_LENGTH;
			    voices[target].volume        = vol;
			    voices[target].phase         = 0.0f;
			    voices[target].env_amplitude = 0.0f;
			    voices[target].env_state     = ENV_ATTACK;
			    voices[target].attack_rate   = att;
			    voices[target].release_rate  = rel;
			    voices[target].midi_note     = evt.note;
			    voices[target].active        = 1;

			    taskEXIT_CRITICAL();
			}
		}
    }
}

float MIDI_to_frequency(uint8_t note) {
	return powf(2, (note - 69.0f) / 12.0f) * 440.0f;
}

void OnMIDIReceive(uint8_t *msg, uint32_t len)
{
    for (uint32_t i = 0; i + 3 < len; i += 4) {
        uint8_t status = msg[i + 1];

        uint8_t msg_type = status & 0xF0;
        if (msg_type != 0x80 && msg_type != 0x90) continue;

        MidiEvent_t evt = {0};
        evt.status   = status;
        evt.msg_type = msg_type;
        evt.channel  = status & 0x0F;
        evt.note     = msg[i + 2];
        evt.velocity = msg[i + 3];

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendToBackFromISR(midiQueue, &evt, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == B1_Pin) {
		static uint32_t last_press = 0;
		uint32_t now = HAL_GetTick();
		if (now - last_press < 300) return;
		last_press = now;

		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		vTaskNotifyGiveFromISR(defaultTaskHandle, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 5 */
  init_audio();

  /* Infinite loop */
  for (;;) {
          ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

          audio_muted = 1;

          __DMB();
          for (int v = 0; v < MAX_VOICES; v++) {
              voices[v].active = 0;
              voices[v].env_state = ENV_IDLE;
              voices[v].env_amplitude = 0.0f;
          }

          current_preset_idx = (current_preset_idx + 1) % NUM_PRESETS;
          init_wavetable();
          __DMB();
          audio_muted = 0;
      }
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
