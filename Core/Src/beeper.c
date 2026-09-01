/*
 * beeper.c
 *
 *  Created on: 2025
 *      Author: 79099
 */

#include "beeper.h"
#include "main.h"
#include "menu_ui.h"
#include "gost_mode.h"
#include "device_config.h"
#include "event_log.h"
#include "sound_profiles.h"

extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim6;

#define BEEPER_DAC_CH            DAC_CHANNEL_2
#define BEEPER_TIM6_TICK_HZ      SOUND_DAC_SAMPLE_HZ
#define BEEPER_ACK_COOLDOWN_TICKS 15u
#define BEEPER_SINE_POINTS       64u

/***********************************************************************************************************/
/* Внутренние типы и переменные */
/***********************************************************************************************************/

typedef enum
{
	BEEPER_STATE_IDLE = 0,              // Простой
	BEEPER_STATE_SHORT_BEEP,           // Одно короткое пищание
	BEEPER_STATE_DOUBLE_SHORT_BEEP,    // Два коротких пищания
	BEEPER_STATE_LONG_BEEP,            // Длинное пищание
	BEEPER_STATE_CONTINUOUS,            // Постоянное пищание
	BEEPER_STATE_FIRE_ALARM,            // Пожар: короткие включения с паузами
	BEEPER_STATE_PATTERN
} BeeperState_t;

/* Пожар: ~200 мс звук, ~300 мс тишина (шаг 10 мс в Beeper_Process) */
#define BEEPER_FIRE_ON_TICKS   20u
#define BEEPER_FIRE_OFF_TICKS  30u

static BeeperState_t beeper_state = BEEPER_STATE_IDLE;
static uint16_t beeper_counter = 0;
static uint8_t beep_phase = 0;  // Фаза для двойного пищания (0 - первое, 1 - пауза, 2 - второе)
static uint8_t fire_alarm_sound = 1u; /* 1 = фаза «звук», 0 = пауза */

static uint8_t beep_sound = 1;
static Beeper_SoundStateUiCallback g_sound_state_ui_cb = 0;
static uint16_t pattern_on_ticks = 0;
static uint16_t pattern_off_ticks = 0;
static uint16_t pattern_repeat_ticks = 0;
static uint16_t pattern_counter = 0;
static uint16_t pattern_repeat_counter = 0;
static uint8_t pattern_pulses_total = 0;
static uint8_t pattern_pulses_left = 0;
static uint8_t pattern_sound_phase = 0;

typedef struct
{
	uint8_t valid;
	BeeperState_t state;
	uint16_t on_ticks;
	uint16_t off_ticks;
	uint16_t repeat_ticks;
	uint8_t pulses_total;
} BeeperResumeCtx_t;

static BeeperResumeCtx_t g_resume_ctx = {0};

static volatile uint8_t s_tone_running = 0u;
static volatile uint32_t s_phase = 0u;
static volatile uint32_t s_phase_inc = 0u;
static volatile uint32_t s_sweep_inc_step = 0u;
static volatile uint32_t s_sweep_samples_left = 0u;
static uint8_t s_tim6_ready = 0u;
static uint16_t s_ack_cooldown_ticks = 0u;
static uint32_t s_oneshot_end_ms = 0u;

/* 64 точки, 12 бит, середина 2048, амплитуда 1800. */
static const uint16_t s_sine[BEEPER_SINE_POINTS] = {
	2048, 2224, 2399, 2571, 2737, 2897, 3048, 3190,
	3321, 3439, 3545, 3635, 3711, 3770, 3813, 3839,
	3848, 3839, 3813, 3770, 3711, 3635, 3545, 3439,
	3321, 3190, 3048, 2897, 2737, 2571, 2399, 2224,
	2048, 1872, 1697, 1525, 1359, 1199, 1048,  906,
	 775,  657,  551,  461,  385,  326,  283,  257,
	 248,  257,  283,  326,  385,  461,  551,  657,
	 775,  906, 1048, 1199, 1359, 1525, 1697, 1872
};

static uint8_t Beeper_OneShotExpired(void)
{
	return ((int32_t)(HAL_GetTick() - s_oneshot_end_ms) >= 0) ? 1u : 0u;
}

static void Beeper_ArmOneShotMs(uint16_t duration_ms)
{
	uint32_t now = HAL_GetTick();
	s_oneshot_end_ms = now + ((duration_ms == 0u) ? 1u : duration_ms);
}

static void Beeper_SetNoteHz(uint16_t hz)
{
	s_sweep_inc_step = 0u;
	s_sweep_samples_left = 0u;
	if (hz == 0u) {
		s_phase_inc = 0u;
		return;
	}
	s_phase_inc = (uint32_t)(((uint64_t)hz << 32) / SOUND_DAC_SAMPLE_HZ);
}

static uint16_t Beeper_MsToTicks(uint16_t duration_ms)
{
	uint16_t ticks = (uint16_t)((duration_ms + 9u) / 10u);
	return (ticks == 0u) ? 1u : ticks;
}

/***********************************************************************************************************/
/* Внутренние функции */
/***********************************************************************************************************/

static uint32_t Beeper_Tim6ClockHz(void)
{
	uint32_t pclk = HAL_RCC_GetPCLK1Freq();
	RCC_ClkInitTypeDef clk = {0};
	uint32_t latency = 0u;

	HAL_RCC_GetClockConfig(&clk, &latency);
	if (clk.APB1CLKDivider != RCC_HCLK_DIV1) {
		pclk *= 2u;
	}
	return pclk;
}

static void Beeper_Tim6Configure(void)
{
	const uint32_t timclk = Beeper_Tim6ClockHz();
	const uint32_t tim_tick_hz = 1000000u;
	uint32_t psc = timclk / tim_tick_hz;
	uint32_t arr;

	if (psc == 0u) {
		psc = 1u;
	}
	arr = tim_tick_hz / BEEPER_TIM6_TICK_HZ;
	if (arr < 2u) {
		arr = 2u;
	}

	htim6.Instance = TIM6;
	htim6.Init.Prescaler = (uint32_t)(psc - 1u);
	htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim6.Init.Period = (uint32_t)(arr - 1u);
	htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
	if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
		Error_Handler();
	}
	HAL_NVIC_SetPriority(TIM6_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(TIM6_IRQn);
	s_tim6_ready = 1u;
}

static void Beeper_DacWrite(uint32_t level)
{
	//TODO DELETE

	return;

	if (level > SOUND_DAC_LEVEL_MAX) {
		level = SOUND_DAC_LEVEL_MAX;
	}
	DAC1->DHR12R2 = level;
}

static void Beeper_Pa5Analog(void)
{
	GPIO_InitTypeDef gpio = {0};
	__HAL_RCC_GPIOA_CLK_ENABLE();
	gpio.Pin = GPIO_PIN_5;
	gpio.Mode = GPIO_MODE_ANALOG;
	gpio.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOA, &gpio);
}

static void Beeper_DacInit(void)
{
	RCC_PeriphCLKInitTypeDef periph = {0};
	DAC_ChannelConfTypeDef sConfig = {0};

	/* Как в рабочем тесте: ядро DAC = HCLK, не HSE от ADC MSP. */
	periph.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC;
	periph.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_HCLK;
	(void)HAL_RCCEx_PeriphCLKConfig(&periph);

	Beeper_Pa5Analog();
	HAL_NVIC_DisableIRQ(DAC1_IRQn);

	(void)HAL_DAC_Stop(&hdac1, BEEPER_DAC_CH);
	(void)HAL_DAC_DeInit(&hdac1);
	hdac1.Instance = DAC1;
	if (HAL_DAC_Init(&hdac1) != HAL_OK) {
		Error_Handler();
	}

	sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
	sConfig.DAC_DMADoubleDataMode = DISABLE;
	sConfig.DAC_SignedFormat = DISABLE;
	sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
	sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
	sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
	sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
	sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
	if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, BEEPER_DAC_CH) != HAL_OK) {
		Error_Handler();
	}
	if (HAL_DAC_Start(&hdac1, BEEPER_DAC_CH) != HAL_OK) {
		Error_Handler();
	}
	SET_BIT(DAC1->CR, DAC_CR_EN2);
	Beeper_DacWrite(SOUND_DAC_LEVEL_OFF);

	Beeper_Tim6Configure();
	__HAL_TIM_SET_COUNTER(&htim6, 0u);
	__HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
	HAL_NVIC_SetPriority(TIM6_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(TIM6_IRQn);
	if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK) {
		Error_Handler();
	}
}

void Beeper_HwStart(void)
{
	s_tim6_ready = 0u;
	Beeper_DacInit();
}

static void Beeper_ToneStop(void)
{
	s_tone_running = 0u;
	s_phase = 0u;
	s_sweep_inc_step = 0u;
	s_sweep_samples_left = 0u;
	Beeper_DacWrite(SOUND_DAC_LEVEL_OFF);
}

void Beeper_ToneStart(void)
{
	if (s_tim6_ready == 0u) {
		Beeper_DacInit();
	}

	beep_sound = 1u;
	if (s_phase_inc == 0u) {
		Beeper_SetNoteHz(SOUND_TONE_HZ);
	}
	SET_BIT(DAC1->CR, DAC_CR_EN2);
	s_phase = 0u;
	s_tone_running = 1u;
}

static void Beeper_EnsureIdle(void)
{
	if (s_tone_running != 0u && beeper_state == BEEPER_STATE_IDLE) {
		Beeper_ToneStop();
	}
}

void Beeper_OnTim6Tick(void)
{
	uint32_t idx;

	if (s_tone_running == 0u) {
		return;
	}

	if (s_sweep_samples_left != 0u) {
		s_phase_inc += s_sweep_inc_step;
		s_sweep_samples_left--;
	}

	s_phase += s_phase_inc;
	idx = s_phase >> 26; /* старшие 6 бит → 64 точки */
	Beeper_DacWrite(s_sine[idx]);
}

static void Beeper_On(void)
{
	Beeper_ToneStart();
}

static void Beeper_Off(void)
{
	Beeper_ToneStop();
}

static void Beeper_StartJumpSweep(void)
{
	const uint32_t samples = (SOUND_DAC_SAMPLE_HZ * (uint32_t)SOUND_JUMP_MS) / 1000u;
	const uint32_t inc0 = (uint32_t)(((uint64_t)SOUND_JUMP_START_HZ << 32) / SOUND_DAC_SAMPLE_HZ);
	const uint32_t inc1 = (uint32_t)(((uint64_t)SOUND_JUMP_END_HZ << 32) / SOUND_DAC_SAMPLE_HZ);

	s_phase_inc = inc0;
	s_sweep_inc_step = (samples > 0u) ? ((inc1 - inc0) / samples) : 0u;
	s_sweep_samples_left = samples;
	Beeper_On();
}

static void Beeper_PlayNoteMs(uint16_t hz, uint16_t duration_ms)
{
	if (hz == 0u) {
		Beeper_ToneStop();
		if (duration_ms > 0u) {
			HAL_Delay(duration_ms);
		}
		return;
	}
	Beeper_SetNoteHz(hz);
	Beeper_On();
	HAL_Delay(duration_ms);
	Beeper_ToneStop();
}

typedef struct
{
	uint16_t hz;
	uint16_t ms;
} BeeperMelodyNote_t;

/* Фольклорное танго в ля миноре, октава около резонанса KPEG116. */
static const BeeperMelodyNote_t s_test_murka_chorus[] = {
	{ NOTE_A6, SOUND_TEST_QUARTER_MS }, { NOTE_C7, SOUND_TEST_EIGHTH_MS }, { NOTE_E7, SOUND_TEST_DOTTED_MS },
	{ NOTE_E7, SOUND_TEST_EIGHTH_MS }, { NOTE_D7, SOUND_TEST_EIGHTH_MS }, { NOTE_C7, SOUND_TEST_EIGHTH_MS },
	{ NOTE_B6, SOUND_TEST_EIGHTH_MS }, { NOTE_A6, SOUND_TEST_QUARTER_MS },
	{ NOTE_GS6, SOUND_TEST_QUARTER_MS }, { NOTE_B6, SOUND_TEST_EIGHTH_MS }, { NOTE_D7, SOUND_TEST_DOTTED_MS },
	{ NOTE_D7, SOUND_TEST_EIGHTH_MS }, { NOTE_C7, SOUND_TEST_EIGHTH_MS }, { NOTE_B6, SOUND_TEST_EIGHTH_MS },
	{ NOTE_A6, SOUND_TEST_QUARTER_MS }, { 0u, SOUND_TEST_EIGHTH_MS },
	{ NOTE_C7, SOUND_TEST_EIGHTH_MS }, { NOTE_C7, SOUND_TEST_EIGHTH_MS }, { NOTE_D7, SOUND_TEST_EIGHTH_MS },
	{ NOTE_E7, SOUND_TEST_QUARTER_MS }, { NOTE_D7, SOUND_TEST_EIGHTH_MS }, { NOTE_C7, SOUND_TEST_EIGHTH_MS },
	{ NOTE_B6, SOUND_TEST_EIGHTH_MS }, { NOTE_A6, SOUND_TEST_QUARTER_MS },
	{ NOTE_GS6, SOUND_TEST_EIGHTH_MS }, { NOTE_B6, SOUND_TEST_EIGHTH_MS }, { NOTE_D7, SOUND_TEST_QUARTER_MS },
	{ NOTE_C7, SOUND_TEST_EIGHTH_MS }, { NOTE_B6, SOUND_TEST_EIGHTH_MS }, { NOTE_A6, SOUND_TEST_HALF_MS },
	{ 0u, SOUND_TEST_QUARTER_MS }
};

static void Beeper_PlayMelody(const BeeperMelodyNote_t *notes, uint16_t count)
{
	uint16_t i;

	for (i = 0u; i < count; i++) {
		Beeper_PlayNoteMs(notes[i].hz, notes[i].ms);
	}
}

static uint8_t Beeper_IsOneShotState(BeeperState_t st)
{
	return (st == BEEPER_STATE_SHORT_BEEP ||
		st == BEEPER_STATE_DOUBLE_SHORT_BEEP ||
		st == BEEPER_STATE_LONG_BEEP) ? 1u : 0u;
}

static void Beeper_CaptureResumeStateIfNeeded(void)
{
	/* Переснимаем контекст каждый раз: старый может устареть, если между
	 * кнопочным бипом и его завершением режим был переключён извне. */
	g_resume_ctx.valid = 0u;
	if (beeper_state == BEEPER_STATE_CONTINUOUS ||
	    beeper_state == BEEPER_STATE_FIRE_ALARM) {
		g_resume_ctx.valid = 1u;
		g_resume_ctx.state = beeper_state;
		return;
	}
	if (beeper_state == BEEPER_STATE_PATTERN && pattern_repeat_ticks > 0u) {
		g_resume_ctx.valid = 1u;
		g_resume_ctx.state = BEEPER_STATE_PATTERN;
		g_resume_ctx.on_ticks = pattern_on_ticks;
		g_resume_ctx.off_ticks = pattern_off_ticks;
		g_resume_ctx.repeat_ticks = pattern_repeat_ticks;
		g_resume_ctx.pulses_total = pattern_pulses_total;
	}
}

static void Beeper_RestoreAfterOneShot(void)
{
	if (!g_resume_ctx.valid) {
		beeper_state = BEEPER_STATE_IDLE;
		Beeper_Off();
		return;
	}

	if (g_resume_ctx.state == BEEPER_STATE_CONTINUOUS ||
	    g_resume_ctx.state == BEEPER_STATE_FIRE_ALARM) {
		beeper_state = g_resume_ctx.state;
		beeper_counter = 0u;
		beep_phase = 0u;
		Beeper_On();
		g_resume_ctx.valid = 0u;
		return;
	}

	if (g_resume_ctx.state == BEEPER_STATE_PATTERN && g_resume_ctx.pulses_total > 0u) {
		pattern_on_ticks = g_resume_ctx.on_ticks;
		pattern_off_ticks = g_resume_ctx.off_ticks;
		pattern_repeat_ticks = g_resume_ctx.repeat_ticks;
		pattern_pulses_total = g_resume_ctx.pulses_total;
		pattern_pulses_left = g_resume_ctx.pulses_total;
		pattern_sound_phase = 1u;
		pattern_counter = pattern_on_ticks;
		pattern_repeat_counter = 0u;
		beeper_state = BEEPER_STATE_PATTERN;
		Beeper_On();
		g_resume_ctx.valid = 0u;
		return;
	}

	g_resume_ctx.valid = 0u;
	beeper_state = BEEPER_STATE_IDLE;
	Beeper_Off();
}

/***********************************************************************************************************/
/* Публичные функции */
/***********************************************************************************************************/

/**
 * @brief Инициализация пищалки
 */
void Beeper_Init(void)
{
	beeper_state = BEEPER_STATE_IDLE;
	beeper_counter = 0;
	beep_phase = 0;
	s_ack_cooldown_ticks = 0u;
	s_tone_running = 0u;
	s_phase = 0u;
	s_phase_inc = 0u;
	s_sweep_inc_step = 0u;
	s_sweep_samples_left = 0u;
	s_tim6_ready = 0u;
}

/**
 * @brief Одно короткое пищание (10мс)
 */
void Beeper_ShortBeep(void)
{
	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	beeper_state = BEEPER_STATE_SHORT_BEEP;
	beeper_counter = BEEPER_SHORT_BEEP_DURATION;
	beep_phase = 0;
	Beeper_ArmOneShotMs(SOUND_JUMP_MS);
	Beeper_StartJumpSweep();
}

/**
 * @brief Два коротких пищания с паузой между ними
 */
void Beeper_DoubleShortBeep(void)
{
	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	beeper_state = BEEPER_STATE_DOUBLE_SHORT_BEEP;
	beeper_counter = BEEPER_SHORT_BEEP_DURATION;
	beep_phase = 0;  // Начинаем с первого пищания
	Beeper_On();
}

/**
 * @brief Длинное пищание (100мс)
 */
void Beeper_LongBeep(void)
{
	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	beeper_state = BEEPER_STATE_LONG_BEEP;
	beeper_counter = BEEPER_LONG_BEEP_DURATION;
	beep_phase = 0;
	Beeper_ArmOneShotMs(SOUND_ONE_SHOT_LONG_ON_MS);
	Beeper_SetNoteHz(NOTE_C7);
	Beeper_On();
}

void Beeper_LongBeep1300ms(void)
{
	Beeper_LongBeep();
}

/**
 * @brief Включить постоянное пищание
 */
void Beeper_ContinuousOn(void)
{
	g_resume_ctx.valid = 0u;
	beeper_state = BEEPER_STATE_CONTINUOUS;
	beeper_counter = 0;
	beep_phase = 0;
	Beeper_SetNoteHz(NOTE_C7);
	Beeper_On();
}

void Beeper_FireAlarmOn(void)
{
	Beeper_ContinuousOn();

	//beeper_state = BEEPER_STATE_FIRE_ALARM;
	//fire_alarm_sound = 1u;
	//beeper_counter = BEEPER_FIRE_ON_TICKS;
	//Beeper_On();
}

void Beeper_FireAlarmOff(void)
{
	if (beeper_state == BEEPER_STATE_FIRE_ALARM) {
		beeper_state = BEEPER_STATE_IDLE;
		Beeper_Off();
	}
}

/**
 * @brief Выключить постоянное пищание
 */
void Beeper_ContinuousOff(void)
{
	if (beeper_state == BEEPER_STATE_CONTINUOUS)
	{
		beeper_state = BEEPER_STATE_IDLE;
		g_resume_ctx.valid = 0u;
		Beeper_Off();
	}
}

void Beeper_StopPattern(void)
{
	if (beeper_state == BEEPER_STATE_PATTERN) {
		beeper_state = BEEPER_STATE_IDLE;
		g_resume_ctx.valid = 0u;
		Beeper_Off();
	}
}

/**
 * @brief Переключить состояние постоянного пищания
 */
void Beeper_ContinuousToggle(void)
{
	if (beeper_state == BEEPER_STATE_CONTINUOUS)
	{
		Beeper_ContinuousOff();
	}
	else
	{
		Beeper_ContinuousOn();
	}
}

void Beeper_PlayOneShotMs(uint16_t duration_ms)
{
	beep_sound = 1u;
	beeper_state = BEEPER_STATE_LONG_BEEP;
	beeper_counter = Beeper_MsToTicks(duration_ms);
	beep_phase = 0u;
	Beeper_ArmOneShotMs(duration_ms);
	Beeper_SetNoteHz(SOUND_TONE_HZ);
	Beeper_On();
}

void Beeper_StartupBeep(void)
{
	/* Оригинальный 8-bit фанфар (не мелодия Nintendo). */
	Beeper_PlayNoteMs(NOTE_E6, 75u);
	Beeper_PlayNoteMs(NOTE_G6, 75u);
	Beeper_PlayNoteMs(NOTE_C7, 150u);
	Beeper_PlayNoteMs(0u, 45u);
	Beeper_PlayNoteMs(NOTE_C7, 50u);
	Beeper_PlayNoteMs(NOTE_C7, 50u);
	Beeper_PlayNoteMs(NOTE_D7, 75u);
	Beeper_PlayNoteMs(NOTE_E7, 200u);
	Beeper_PlayNoteMs(0u, 60u);
	Beeper_PlayNoteMs(NOTE_G6, 80u);
	Beeper_PlayNoteMs(NOTE_C7, 260u);
	Beeper_ToneStop();
}

void Beeper_StartPulseTrain(uint16_t pulse_on_ms, uint16_t pulse_off_ms, uint8_t pulses, uint16_t repeat_period_ms)
{
	if (pulses == 0u) {
		Beeper_StopPattern();
		return;
	}
	g_resume_ctx.valid = 0u;
	pattern_on_ticks = Beeper_MsToTicks(pulse_on_ms);
	pattern_off_ticks = Beeper_MsToTicks(pulse_off_ms);
	pattern_repeat_ticks = (repeat_period_ms == 0u) ? 0u : Beeper_MsToTicks(repeat_period_ms);
	pattern_pulses_total = pulses;
	pattern_pulses_left = pulses;
	pattern_sound_phase = 1u;
	pattern_counter = pattern_on_ticks;
	pattern_repeat_counter = 0u;
	beeper_state = BEEPER_STATE_PATTERN;
	Beeper_SetNoteHz(NOTE_B6);
	Beeper_On();
}

void Beeper_ButtonAcknowledge(void)
{
	if (s_ack_cooldown_ticks > 0u) {
		return;
	}
	s_ack_cooldown_ticks = BEEPER_ACK_COOLDOWN_TICKS;
	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	beep_sound = 1u;
	beeper_state = BEEPER_STATE_LONG_BEEP;
	beeper_counter = Beeper_MsToTicks(SOUND_JUMP_MS);
	beep_phase = 0u;
	Beeper_ArmOneShotMs(SOUND_JUMP_MS);
	Beeper_StartJumpSweep();
}

void Beeper_PlayIndicationTest(void)
{
	const uint8_t saved_mute = beep_sound;
	uint8_t pass;

	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	/* Тест всегда проигрывается: иначе нельзя проверить звуковой канал. */
	beep_sound = 1u;
	beeper_state = BEEPER_STATE_IDLE;
	Beeper_Off();

	for (pass = 0u; pass < 2u; pass++) {
		Beeper_PlayMelody(s_test_murka_chorus,
			(uint16_t)(sizeof(s_test_murka_chorus) / sizeof(s_test_murka_chorus[0])));
	}

	beep_sound = saved_mute;
	Beeper_RestoreAfterOneShot();
}

/**
 * @brief Функция обработки состояния пищалки (вызывать каждые 10мс)
 * @note Должна вызываться из таймера или основного цикла с периодом 10мс
 */
void Beeper_Process(void)
{
	if (s_ack_cooldown_ticks > 0u) {
		s_ack_cooldown_ticks--;
	}

	if (MenuUi_IsConfigSessionActive() && !Beeper_IsOneShotState(beeper_state)) {
		Beeper_Off();
		return;
	}

	switch (beeper_state)
	{
		case BEEPER_STATE_IDLE:
			Beeper_EnsureIdle();
			break;

		case BEEPER_STATE_SHORT_BEEP:
			if (Beeper_OneShotExpired()) {
				Beeper_RestoreAfterOneShot();
			}
			break;

		case BEEPER_STATE_DOUBLE_SHORT_BEEP:
			// Два коротких пищания
			if (beeper_counter > 0)
			{
				beeper_counter--;
			}
			else
			{
				// Текущая фаза завершена
				if (beep_phase == 0)
				{
					// Первое пищание завершено, начинаем паузу
					Beeper_Off();
					beep_phase = 1;
					beeper_counter = BEEPER_PAUSE_DURATION;
				}
				else if (beep_phase == 1)
				{
					// Пауза завершена, начинаем второе пищание
					Beeper_On();
					beep_phase = 2;
					beeper_counter = BEEPER_SHORT_BEEP_DURATION;
				}
				else
				{
					// Второе пищание завершено
					beep_phase = 0;
					Beeper_RestoreAfterOneShot();
				}
			}
			break;

		case BEEPER_STATE_LONG_BEEP:
			if (Beeper_OneShotExpired()) {
				Beeper_RestoreAfterOneShot();
			}
			break;

		case BEEPER_STATE_CONTINUOUS:
			// Постоянное пищание - ничего не делаем, звук уже включен
			break;

		case BEEPER_STATE_FIRE_ALARM:
			if (beeper_counter > 0u) {
				beeper_counter--;
			} else {
				if (fire_alarm_sound) {
					Beeper_Off();
					fire_alarm_sound = 0u;
					beeper_counter = BEEPER_FIRE_OFF_TICKS;
				} else {
					Beeper_On();
					fire_alarm_sound = 1u;
					beeper_counter = BEEPER_FIRE_ON_TICKS;
				}
			}
			break;

		case BEEPER_STATE_PATTERN:
			if (pattern_counter > 0u) {
				pattern_counter--;
				break;
			}
			if (pattern_sound_phase) {
				Beeper_Off();
				pattern_sound_phase = 0u;
				pattern_counter = pattern_off_ticks;
				if (pattern_pulses_left > 0u) {
					pattern_pulses_left--;
				}
			} else {
				if (pattern_pulses_left > 0u) {
					Beeper_On();
					pattern_sound_phase = 1u;
					pattern_counter = pattern_on_ticks;
				} else {
					if (pattern_repeat_ticks == 0u) {
						beeper_state = BEEPER_STATE_IDLE;
						Beeper_Off();
						break;
					}
					if (pattern_repeat_counter < pattern_repeat_ticks) {
						pattern_repeat_counter++;
						pattern_counter = 1u;
					} else {
						pattern_repeat_counter = 0u;
						pattern_pulses_left = pattern_pulses_total;
						Beeper_On();
						pattern_sound_phase = 1u;
						pattern_counter = pattern_on_ticks;
					}
				}
			}
			break;

		default:
			// Неизвестное состояние - переходим в IDLE
			beeper_state = BEEPER_STATE_IDLE;
			Beeper_Off();
			break;
	}
}

void Beeper_SoundOnOff(bool soundOn) {
	beep_sound = soundOn ? 1u : 0u;
	if (!soundOn) {
		s_ack_cooldown_ticks = 0u;
		g_resume_ctx.valid = 0u;
		beeper_state = BEEPER_STATE_IDLE;
		Beeper_Off();
	}
}

void Beeper_SetSoundStateUiCallback(Beeper_SoundStateUiCallback cb)
{
	g_sound_state_ui_cb = cb;
}

void Beeper_ResumeSoundOnNewEvent(void)
{
#if GOST_MODE
	extern PPKYCfg PPKYConfig;

	/* ГОСТ 7.6.1.13: после ручного отключения звука новое извещение снова включает звук. */
	if (beep_sound != 0u) {
		return;
	}
	if (PPKYConfig.beep_block != 0u) {
		return;
	}
	beep_sound = 1u;
	PPKYConfig.beep = 1u;
	if (g_sound_state_ui_cb != 0) {
		g_sound_state_ui_cb(true);
	}
	EventLog_LogSoundToggle(1u, 1u); /* source: auto (новое событие) */
#else
	/* Вне режима сдачи по ГОСТ mute остаётся до ручного включения. */
#endif
}
