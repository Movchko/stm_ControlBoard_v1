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
#include "panel_host_cache.h"
#include "event_log.h"
#include "sound_profiles.h"
#include "fire.h"

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
	/* Точная фаза: иначе после BTN_ACK дежурный стартует заново и «прилипает» к клику. */
	uint8_t pulses_left;
	uint8_t sound_phase;
	uint16_t pattern_counter;
	uint16_t pattern_repeat_counter;
	uint16_t note_hz;
} BeeperResumeCtx_t;

static BeeperResumeCtx_t g_resume_ctx = {0};
static uint16_t s_current_note_hz = 0u;

static volatile uint8_t s_tone_running = 0u;
static volatile uint8_t s_tone_volume_pct = 100u; /* 100 = полная амплитуда синуса */
static volatile uint32_t s_phase = 0u;
static volatile uint32_t s_phase_inc = 0u;
static volatile uint32_t s_sweep_inc_step = 0u;
static volatile uint32_t s_sweep_samples_left = 0u;
static uint8_t s_tim6_ready = 0u;
static uint16_t s_ack_cooldown_ticks = 0u;
static uint32_t s_oneshot_end_ms = 0u;

/* ТЕСТ3: захват звукового канала. */
static uint8_t s_beeper_test_mode = 0u;
static uint8_t s_beeper_test_api = 0u; /* 1 = вызов из TestPlay/TestStop */
static uint8_t s_beeper_test_saved_mute = 1u;
static uint16_t s_beeper_test_stop_ticks = 0u;
/* Дежурные: стоп после N завершённых пакетов (не по wall-clock 2×REPEAT). */
static uint8_t s_beeper_test_bursts_left = 0u;
/* Временный звук при mute (клик кнопки / тест индикации). */
static uint8_t s_sound_override = 0u;
/* GOST: unmute по новому событию — не дать POLL с SOUND_ON=0 сразу заглушить. */
static uint8_t s_gost_resume_hold = 0u;

static uint8_t Beeper_TestModeBlocksExternal(void)
{
	return (s_beeper_test_mode != 0u && s_beeper_test_api == 0u) ? 1u : 0u;
}

static uint8_t Beeper_IsAudible(void)
{
	if (s_beeper_test_api != 0u || s_sound_override != 0u) {
		return 1u;
	}
	return (beep_sound != 0u) ? 1u : 0u;
}

static void Beeper_TestClearDeadline(void)
{
	s_beeper_test_stop_ticks = 0u;
	s_beeper_test_bursts_left = 0u;
}

static void Beeper_TestArmBurstCount(uint8_t bursts)
{
	s_beeper_test_stop_ticks = 0u;
	s_beeper_test_bursts_left = bursts;
}

static void Beeper_TestForceAllOff(void)
{
	s_beeper_test_api = 1u;
	Beeper_TestClearDeadline();
	Beeper_AllOff();
	s_beeper_test_api = 0u;
}

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
	s_current_note_hz = hz;
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

static void Beeper_TestArmStopMs(uint16_t duration_ms)
{
	s_beeper_test_bursts_left = 0u;
	s_beeper_test_stop_ticks = Beeper_MsToTicks(duration_ms);
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
	//TODO DELETE  return;
	//return;

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
	s_tone_volume_pct = 100u;
	s_phase = 0u;
	s_sweep_inc_step = 0u;
	s_sweep_samples_left = 0u;
	Beeper_DacWrite(SOUND_DAC_LEVEL_OFF);
}

void Beeper_ToneStart(void)
{
	if (Beeper_IsAudible() == 0u) {
		return;
	}
	if (s_tim6_ready == 0u) {
		Beeper_DacInit();
	}

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
	uint16_t sample;

	if (s_tone_running == 0u) {
		return;
	}

	if (s_sweep_samples_left != 0u) {
		s_phase_inc += s_sweep_inc_step;
		s_sweep_samples_left--;
	}

	s_phase += s_phase_inc;
	idx = s_phase >> 26; /* старшие 6 бит → 64 точки */
	sample = s_sine[idx];
	if (s_tone_volume_pct < 100u) {
		int32_t centered = (int32_t)sample - (int32_t)SOUND_DAC_LEVEL_MID;
		centered = (centered * (int32_t)s_tone_volume_pct) / 100;
		sample = (uint16_t)((int32_t)SOUND_DAC_LEVEL_MID + centered);
	}
	Beeper_DacWrite(sample);
}

static void Beeper_On(void)
{
	Beeper_ToneStart();
}

static void Beeper_Off(void)
{
	Beeper_ToneStop();
}

#if 0 /* LEGACY: частотные свипы (Mario jump / лазер «пиу») — не удалять */
/* Общий частотный свип start→end за duration_ms (DAC TIM6).
 * Нисходящий: s_sweep_inc_step = −Δ в uint32 (wrap в OnTim6Tick). */
static void Beeper_StartFreqSweep(uint16_t start_hz, uint16_t end_hz, uint16_t duration_ms)
{
	const uint32_t samples = (SOUND_DAC_SAMPLE_HZ * (uint32_t)duration_ms) / 1000u;
	const uint32_t inc0 = (uint32_t)(((uint64_t)start_hz << 32) / SOUND_DAC_SAMPLE_HZ);
	const uint32_t inc1 = (uint32_t)(((uint64_t)end_hz << 32) / SOUND_DAC_SAMPLE_HZ);

	s_phase_inc = inc0;
	s_sweep_samples_left = samples;
	if (samples == 0u) {
		s_sweep_inc_step = 0u;
	} else if (inc1 >= inc0) {
		s_sweep_inc_step = (inc1 - inc0) / samples;
	} else {
		s_sweep_inc_step = (uint32_t)(0u - ((inc0 - inc1) / samples));
	}
	Beeper_On();
}

static void Beeper_StartJumpSweep(void)
{
	Beeper_StartFreqSweep(SOUND_JUMP_START_HZ, SOUND_JUMP_END_HZ, SOUND_JUMP_MS);
}

static void Beeper_StartLaserSweep(void)
{
	Beeper_StartFreqSweep(SOUND_LASER_START_HZ, SOUND_LASER_END_HZ, SOUND_LASER_MS);
}
#endif

/* Короткий «пип» на фиксированной ноте (−20 % амплитуды). */
static void Beeper_StartPip(void)
{
	Beeper_SetNoteHz(SOUND_BUTTON_NOTE_HZ);
	s_tone_volume_pct = SOUND_PIP_VOLUME_PCT;
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

static void Beeper_RestoreAfterOneShot(void);

/* ТЕСТ1: неблокирующая мелодия (шаг в Beeper_Process). */
static uint8_t s_ind_melody_active = 0u;
static uint8_t s_ind_saved_mute = 1u;
static const BeeperMelodyNote_t *s_ind_notes = 0;
static uint16_t s_ind_count = 0u;
static uint16_t s_ind_idx = 0u;
static uint32_t s_ind_note_end_ms = 0u;

/*
 * ТЕСТ1 — «Кукла колдуна» (вступление/припев), тональность Am.
 * Источник (Dm на 2-й струне, pereborom.ru):
 *   6-5-5-3-3-6-5-3  3-5-6-11-10-10-6-6  11-10-5
 * → Am: C B B A A C B A | A B C F E E C C | F E B(half)
 */
#define KUKLA_E(hz) { (hz), SOUND_KUKLA_ON_MS }, { 0u, SOUND_KUKLA_GAP_MS },

static const BeeperMelodyNote_t s_test_kukla_chorus[] = {
	/* такт 1 (Am) */
	KUKLA_E(NOTE_C7) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_A6)
	KUKLA_E(NOTE_A6) KUKLA_E(NOTE_C7) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_A6)
	/* такт 2 (Dm): A B C F E E C C */
	KUKLA_E(NOTE_A6) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_C7) KUKLA_E(NOTE_F7)
	KUKLA_E(NOTE_E7) KUKLA_E(NOTE_E7) KUKLA_E(NOTE_C7) KUKLA_E(NOTE_C7)
	/* такт 3 (E): F E + B половинная */
	KUKLA_E(NOTE_F7) KUKLA_E(NOTE_E7)
	{ NOTE_B6, SOUND_KUKLA_HALF_MS },
	{ 0u, SOUND_KUKLA_BAR_REST_MS },
	/* повтор */
	KUKLA_E(NOTE_C7) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_A6)
	KUKLA_E(NOTE_A6) KUKLA_E(NOTE_C7) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_A6)
	KUKLA_E(NOTE_A6) KUKLA_E(NOTE_B6) KUKLA_E(NOTE_C7) KUKLA_E(NOTE_F7)
	KUKLA_E(NOTE_E7) KUKLA_E(NOTE_E7) KUKLA_E(NOTE_C7) KUKLA_E(NOTE_C7)
	KUKLA_E(NOTE_F7) KUKLA_E(NOTE_E7)
	{ NOTE_B6, SOUND_KUKLA_HALF_MS }
};

static void Beeper_IndMelodyFinish(void)
{
	s_ind_melody_active = 0u;
	s_ind_notes = 0;
	s_ind_count = 0u;
	s_ind_idx = 0u;
	beep_sound = s_ind_saved_mute;
	Beeper_ToneStop();
	Beeper_RestoreAfterOneShot();
}

static void Beeper_IndMelodyStartCurrentNote(void)
{
	uint16_t hz;
	uint16_t ms;

	if (s_ind_idx >= s_ind_count) {
		Beeper_IndMelodyFinish();
		return;
	}
	hz = s_ind_notes[s_ind_idx].hz;
	ms = s_ind_notes[s_ind_idx].ms;
	if (ms == 0u) {
		ms = 1u;
	}
	s_ind_note_end_ms = HAL_GetTick() + (uint32_t)ms;
	if (hz == 0u) {
		Beeper_ToneStop();
	} else {
		Beeper_SetNoteHz(hz);
		Beeper_On();
	}
}

static void Beeper_IndMelodyProcess(void)
{
	if (s_ind_melody_active == 0u) {
		return;
	}
	if ((int32_t)(HAL_GetTick() - s_ind_note_end_ms) < 0) {
		return;
	}
	s_ind_idx++;
	Beeper_IndMelodyStartCurrentNote();
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
		g_resume_ctx.note_hz = (s_current_note_hz != 0u) ? s_current_note_hz : SOUND_FIRE_TONE_HZ;
		return;
	}
	if (beeper_state == BEEPER_STATE_PATTERN && pattern_repeat_ticks > 0u) {
		g_resume_ctx.valid = 1u;
		g_resume_ctx.state = BEEPER_STATE_PATTERN;
		g_resume_ctx.on_ticks = pattern_on_ticks;
		g_resume_ctx.off_ticks = pattern_off_ticks;
		g_resume_ctx.repeat_ticks = pattern_repeat_ticks;
		g_resume_ctx.pulses_total = pattern_pulses_total;
		g_resume_ctx.pulses_left = pattern_pulses_left;
		g_resume_ctx.sound_phase = pattern_sound_phase;
		g_resume_ctx.pattern_counter = pattern_counter;
		g_resume_ctx.pattern_repeat_counter = pattern_repeat_counter;
		g_resume_ctx.note_hz = (s_current_note_hz != 0u) ? s_current_note_hz : NOTE_B6;
	}
}

static void Beeper_RestoreAfterOneShot(void)
{
	s_sound_override = 0u;
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
		Beeper_SetNoteHz(g_resume_ctx.note_hz);
		Beeper_On();
		g_resume_ctx.valid = 0u;
		return;
	}

	if (g_resume_ctx.state == BEEPER_STATE_PATTERN && g_resume_ctx.pulses_total > 0u) {
		pattern_on_ticks = g_resume_ctx.on_ticks;
		pattern_off_ticks = g_resume_ctx.off_ticks;
		pattern_repeat_ticks = g_resume_ctx.repeat_ticks;
		pattern_pulses_total = g_resume_ctx.pulses_total;
		pattern_pulses_left = g_resume_ctx.pulses_left;
		pattern_sound_phase = g_resume_ctx.sound_phase;
		pattern_counter = g_resume_ctx.pattern_counter;
		pattern_repeat_counter = g_resume_ctx.pattern_repeat_counter;
		beeper_state = BEEPER_STATE_PATTERN;
		Beeper_SetNoteHz(g_resume_ctx.note_hz);
		if (pattern_sound_phase != 0u) {
			Beeper_On();
		} else {
			Beeper_Off();
		}
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
	s_tone_volume_pct = 100u;
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
	Beeper_ArmOneShotMs(SOUND_PIP_MS);
	Beeper_StartPip();
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
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	if (Beeper_IsAudible() == 0u) {
		return;
	}
	g_resume_ctx.valid = 0u;
	beeper_state = BEEPER_STATE_CONTINUOUS;
	beeper_counter = 0;
	beep_phase = 0;
	Beeper_SetNoteHz(NOTE_C7);
	Beeper_On();
}

void Beeper_FireAlarmOn(void)
{
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	if (Beeper_IsAudible() == 0u) {
		return;
	}
	/* Непрерывный тон на резонансе KPEG116 (2.0 kHz) — максимум громкости. */
	g_resume_ctx.valid = 0u;
	beeper_state = BEEPER_STATE_CONTINUOUS;
	beeper_counter = 0;
	beep_phase = 0;
	Beeper_SetNoteHz(SOUND_FIRE_TONE_HZ);
	Beeper_On();
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
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	if (beeper_state == BEEPER_STATE_CONTINUOUS)
	{
		beeper_state = BEEPER_STATE_IDLE;
		g_resume_ctx.valid = 0u;
		Beeper_Off();
	}
}

void Beeper_StopPattern(void)
{
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	/* Сбрасываем resume всегда: SOUND_OFF мог прийти во время BTN_ACK (LONG_BEEP),
	 * когда state != PATTERN — иначе после клика снова стартует START_ALL_HOLD. */
	g_resume_ctx.valid = 0u;
	if (beeper_state == BEEPER_STATE_PATTERN) {
		beeper_state = BEEPER_STATE_IDLE;
		Beeper_Off();
	}
}

/**
 * @brief Полный стоп звука (паттерн / тревога / one-shot) и запрет resume после ACK.
 */
void Beeper_AllOff(void)
{
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	if (s_ind_melody_active != 0u) {
		s_ind_melody_active = 0u;
		s_ind_notes = 0;
		s_ind_count = 0u;
		s_ind_idx = 0u;
		beep_sound = s_ind_saved_mute;
	}
	g_resume_ctx.valid = 0u;
	beeper_state = BEEPER_STATE_IDLE;
	pattern_repeat_ticks = 0u;
	pattern_pulses_left = 0u;
	pattern_pulses_total = 0u;
	Beeper_Off();
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
	s_sound_override = 1u;
	beeper_state = BEEPER_STATE_LONG_BEEP;
	beeper_counter = Beeper_MsToTicks(duration_ms);
	beep_phase = 0u;
	Beeper_ArmOneShotMs(duration_ms);
	Beeper_SetNoteHz(SOUND_TONE_HZ);
	Beeper_On();
}

void Beeper_StartupBeep(void)
{
	/*
	 * Старт: Windows «USB/Hardware Insert» — исходные длительности нот
	 * (не ускорять; длиннее 2 с — только обрезать хвост, здесь хвоста нет).
	 * override: POLL мог уже выставить mute до конца boot — стартовый звук всё равно нужен.
	 */
	const uint8_t saved_override = s_sound_override;
	s_sound_override = 1u;
#if 0 /* LEGACY: прежний 8-bit фанфар */
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
#endif
#if 0 /* LEGACY: Windows XP Startup-подобное арпеджио */
	Beeper_PlayNoteMs(NOTE_E5, 180u);
	Beeper_PlayNoteMs(NOTE_GS5, 180u);
	Beeper_PlayNoteMs(NOTE_B5, 180u);
	Beeper_PlayNoteMs(NOTE_E6, 220u);
	Beeper_PlayNoteMs(NOTE_GS6, 220u);
	Beeper_PlayNoteMs(NOTE_B6, 260u);
	Beeper_PlayNoteMs(NOTE_E7, 420u);
	Beeper_PlayNoteMs(0u, 80u);
	Beeper_PlayNoteMs(NOTE_B6, 200u);
	Beeper_PlayNoteMs(NOTE_E7, 520u);
	Beeper_PlayNoteMs(NOTE_GS6, 280u);
	Beeper_PlayNoteMs(NOTE_E7, 600u);
#endif
	Beeper_PlayNoteMs(NOTE_A5, 90u);
	Beeper_PlayNoteMs(0u, 35u);
	Beeper_PlayNoteMs(NOTE_B5, 100u);
	Beeper_PlayNoteMs(0u, 30u);
	Beeper_PlayNoteMs(NOTE_C6, 200u);
	Beeper_PlayNoteMs(0u, 500u);
	Beeper_ToneStop();
	s_sound_override = saved_override;
}

void Beeper_StartPulseTrain(uint16_t pulse_on_ms, uint16_t pulse_off_ms, uint8_t pulses, uint16_t repeat_period_ms)
{
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	if (Beeper_IsAudible() == 0u) {
		return;
	}
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
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	/* Mute: клики и дежурные молчат; unmute — только fire/fault (Resume). */
	if (beep_sound == 0u) {
		return;
	}
	if (s_ack_cooldown_ticks > 0u) {
		return;
	}
	s_ack_cooldown_ticks = BEEPER_ACK_COOLDOWN_TICKS;
	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	/* ПУСК ОБЩИЙ уже отпущен — не возвращать hold-паттерн после клика мелкой кнопки. */
	if (Fire_IsStartAllHoldActive() == 0u &&
	    g_resume_ctx.valid != 0u &&
	    g_resume_ctx.state == BEEPER_STATE_PATTERN &&
	    g_resume_ctx.on_ticks == Beeper_MsToTicks(SOUND_START_ALL_HOLD_DUTY_MS) &&
	    g_resume_ctx.pulses_total == 1u) {
		g_resume_ctx.valid = 0u;
	}
	beeper_state = BEEPER_STATE_LONG_BEEP;
	beeper_counter = Beeper_MsToTicks(SOUND_PIP_MS);
	beep_phase = 0u;
	Beeper_ArmOneShotMs(SOUND_PIP_MS);
	/* LEGACY Mario jump: Beeper_StartJumpSweep(); */
	/* LEGACY лазер «пиу»: Beeper_StartLaserSweep(); */
	Beeper_StartPip();
}

void Beeper_PlayIndicationTest(void)
{
	if (s_ind_melody_active != 0u) {
		Beeper_StopIndicationTest();
	}
	if (!Beeper_IsOneShotState(beeper_state)) {
		Beeper_CaptureResumeStateIfNeeded();
	}
	/* Тест всегда слышен: иначе нельзя проверить звуковой канал. */
	s_ind_saved_mute = beep_sound;
	beep_sound = 1u;
	beeper_state = BEEPER_STATE_IDLE;
	Beeper_ToneStop();

	s_ind_notes = s_test_kukla_chorus;
	s_ind_count = (uint16_t)(sizeof(s_test_kukla_chorus) / sizeof(s_test_kukla_chorus[0]));
	s_ind_idx = 0u;
	s_ind_melody_active = 1u;
	Beeper_IndMelodyStartCurrentNote();
}

uint8_t Beeper_IsIndicationTestPlaying(void)
{
	return s_ind_melody_active;
}

void Beeper_StopIndicationTest(void)
{
	if (s_ind_melody_active == 0u) {
		return;
	}
	Beeper_IndMelodyFinish();
}

void Beeper_EnterTestMode(void)
{
	if (s_beeper_test_mode != 0u) {
		return;
	}
	s_beeper_test_saved_mute = beep_sound;
	s_beeper_test_mode = 1u;
	Beeper_TestForceAllOff();
	beep_sound = 1u;
}

void Beeper_ExitTestMode(void)
{
	if (s_beeper_test_mode == 0u) {
		return;
	}
	Beeper_TestForceAllOff();
	s_beeper_test_mode = 0u;
	beep_sound = s_beeper_test_saved_mute;
}

uint8_t Beeper_IsTestMode(void)
{
	return s_beeper_test_mode;
}

void Beeper_TestStop(void)
{
	if (s_beeper_test_mode == 0u) {
		return;
	}
	Beeper_TestForceAllOff();
}

uint8_t Beeper_IsTestPlaying(void)
{
	if (s_beeper_test_mode == 0u) {
		return 0u;
	}
	return (s_beeper_test_stop_ticks > 0u || s_beeper_test_bursts_left > 0u) ? 1u : 0u;
}

void Beeper_TestPlay(uint8_t profile_id)
{
	if (s_beeper_test_mode == 0u || profile_id >= (uint8_t)BEEPER_TEST_PROFILE_COUNT) {
		return;
	}

	s_beeper_test_api = 1u;
	Beeper_TestClearDeadline();
	Beeper_AllOff();
	beep_sound = 1u;

	switch ((BeeperTestProfile_t)profile_id) {
	case BEEPER_TEST_PROFILE_FIRE1_SIGNAL:
		Beeper_StartPulseTrain(SOUND_FIRE1_SIGNAL_ON_MS, SOUND_FIRE1_SIGNAL_OFF_MS,
		                       SOUND_FIRE1_SIGNAL_PULSES, SOUND_FIRE1_SIGNAL_REPEAT_MS);
		Beeper_TestArmStopMs(10000u);
		break;
	case BEEPER_TEST_PROFILE_FIRE1_DUTY:
		Beeper_StartPulseTrain(SOUND_FIRE1_DUTY_ON_MS, SOUND_FIRE1_DUTY_OFF_MS,
		                       SOUND_FIRE1_DUTY_PULSES, SOUND_FIRE1_DUTY_REPEAT_MS);
		Beeper_TestArmBurstCount(2u);
		break;
	case BEEPER_TEST_PROFILE_FIRE2_SIGNAL:
		Beeper_FireAlarmOn();
		Beeper_TestArmStopMs(10000u);
		break;
	case BEEPER_TEST_PROFILE_FIRE2_DUTY:
		Beeper_StartPulseTrain(SOUND_FIRE_DUTY_ON_MS, SOUND_FIRE_DUTY_OFF_MS,
		                       SOUND_FIRE_DUTY_PULSES, SOUND_FIRE_DUTY_REPEAT_MS);
		Beeper_TestArmBurstCount(2u);
		break;
	case BEEPER_TEST_PROFILE_FAULT_SIGNAL:
		/* Нет штатного REPEAT — цикл = длительность сигнального пакета. */
		Beeper_StartPulseTrain(SOUND_FAULT_SIGNAL_ON_MS, SOUND_FAULT_SIGNAL_OFF_MS,
		                       SOUND_FAULT_SIGNAL_PULSES,
		                       (uint16_t)(SOUND_FAULT_SIGNAL_ON_MS * SOUND_FAULT_SIGNAL_PULSES +
		                                  SOUND_FAULT_SIGNAL_OFF_MS * SOUND_FAULT_SIGNAL_PULSES));
		Beeper_TestArmStopMs(10000u);
		break;
	case BEEPER_TEST_PROFILE_FAULT_DUTY:
		Beeper_StartPulseTrain(SOUND_FAULT_DUTY_ON_MS, SOUND_FAULT_DUTY_OFF_MS,
		                       SOUND_FAULT_DUTY_PULSES, SOUND_FAULT_DUTY_REPEAT_MS);
		Beeper_TestArmBurstCount(2u);
		break;
	case BEEPER_TEST_PROFILE_START:
		Beeper_StartPulseTrain(SOUND_START_DUTY_ON_MS, SOUND_START_DUTY_OFF_MS,
		                       SOUND_START_DUTY_PULSES, SOUND_START_DUTY_REPEAT_MS);
		Beeper_TestArmStopMs(10000u);
		break;
	default:
		break;
	}

	s_beeper_test_api = 0u;
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

	if (s_beeper_test_mode != 0u && s_beeper_test_stop_ticks > 0u) {
		s_beeper_test_stop_ticks--;
		if (s_beeper_test_stop_ticks == 0u) {
			Beeper_TestForceAllOff();
		}
	}

	Beeper_IndMelodyProcess();

	if (s_beeper_test_mode == 0u &&
	    MenuUi_IsConfigSessionActive() &&
	    !Beeper_IsOneShotState(beeper_state) &&
	    s_ind_melody_active == 0u) {
		Beeper_Off();
		return;
	}

	switch (beeper_state)
	{
		case BEEPER_STATE_IDLE:
			if (s_ind_melody_active == 0u) {
				Beeper_EnsureIdle();
			}
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
					/* Пакет (burst) завершён. */
					if (s_beeper_test_mode != 0u && s_beeper_test_bursts_left > 0u) {
						s_beeper_test_bursts_left--;
						if (s_beeper_test_bursts_left == 0u) {
							Beeper_TestForceAllOff();
							break;
						}
					}
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
	if (Beeper_TestModeBlocksExternal() != 0u) {
		return;
	}
	const uint8_t next = soundOn ? 1u : 0u;
	const uint8_t prev = beep_sound;
	PanelHostCache *host = PanelHostCache_Get();

	beep_sound = next;
	if (!soundOn) {
		s_gost_resume_hold = 0u; /* ручной mute снимает GOST-hold */
		s_sound_override = 0u;
		s_ack_cooldown_ticks = 0u;
		g_resume_ctx.valid = 0u;
		beeper_state = BEEPER_STATE_IDLE;
		Beeper_Off();
	}
	/* Кэш/меню всегда = фактический mute (иначе POLL «выкл» + Beeper_SoundOnOff(true)
	 * из main оставляют клики слышными при «Откл» в меню). */
	if (host != 0) {
		host->beep = next;
		MenuUi_SetSoundValue(next, host->beep_block);
	}
	if (prev != next && g_sound_state_ui_cb != 0) {
		g_sound_state_ui_cb(soundOn);
	}
}

uint8_t Beeper_IsSoundOn(void)
{
	return (beep_sound != 0u) ? 1u : 0u;
}

uint8_t Beeper_IsGostResumeHold(void)
{
	return s_gost_resume_hold;
}

void Beeper_AckGostResumeFromHost(void)
{
	s_gost_resume_hold = 0u;
}

void Beeper_SetSoundStateUiCallback(Beeper_SoundStateUiCallback cb)
{
	g_sound_state_ui_cb = cb;
}

uint8_t Beeper_ResumeSoundOnNewEvent(void)
{
#if GOST_MODE
	PanelHostCache *host = PanelHostCache_Get();

	/* ГОСТ 7.6.1.13: после ручного отключения звука новое извещение снова включает звук. */
	if (beep_sound != 0u) {
		return 0u;
	}
	if (host == 0 || host->beep_block != 0u) {
		return 0u;
	}
	s_gost_resume_hold = 1u; /* до SYS_SOUND_ON=1 не принимать mute из POLL */
	Beeper_SoundOnOff(true);
	EventLog_LogSoundToggle(1u, 1u); /* source: auto (новое событие) */
	return 1u;
#else
	/* Вне режима сдачи по ГОСТ mute остаётся до ручного включения. */
	return 0u;
#endif
}
