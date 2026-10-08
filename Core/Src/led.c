/*
 * led.c
 *
 *  Created on: Dec 4, 2025
 *      Author: 79099
 */


#include "main.h"
#include "led.h"
#include "button.h"

extern I2C_HandleTypeDef hi2c2;

#define NUM_LED 15
#define LED_ON 1
#define LED_OFF 0
#define LED_STATUS_FIRST LED_POWER
#define LED_STATUS_LAST  LED_AUTO_OFF
#define LED_STATUS_COUNT (LED_STATUS_LAST - LED_STATUS_FIRST + 1u)

uint8_t cur_led_state[NUM_LED];
static uint8_t cur_led_power[NUM_LED];
static uint8_t hw_led_state[NUM_LED];
static uint8_t hw_led_power[NUM_LED];
static uint8_t led_sync_tick = 0u;

/* Счетчик неактивности кнопок для управления яркостью подсветки ENTER/ESC */
static uint16_t led_but_idle_counter = 0;
static uint8_t  led_but_is_bright = 0; /* 0 - базовая яркость, 1 - максимальная */
/* Автозатухание статусных ламп, если их состояние долго не меняется */
static uint8_t  led_status_prev_state[LED_STATUS_COUNT];
static uint16_t led_status_idle_counter[LED_STATUS_COUNT];
static uint8_t  led_status_is_dimmed[LED_STATUS_COUNT];
/* Софт-мигание для logical state==2 (MODE_BLINK): период 2×400 мс при тике 10 мс. */
#define LED_BLINK_HALF_TICKS 40u
static uint16_t s_led_blink_tick = 0u;
static uint8_t  s_led_blink_on = 1u;

/* ТЕСТ2: полный захват LED (ППКУ / внутренняя логика не перебивают). */
static uint8_t s_led_test_mode = 0u;
static uint8_t s_led_test_saved_state[NUM_LED];
static uint8_t s_led_test_saved_power[NUM_LED];

static void Led_UpdateStatusBrightness(void)
{
	for (uint8_t led = LED_STATUS_FIRST; led <= LED_STATUS_LAST; led++) {
		uint8_t idx = (uint8_t)(led - LED_STATUS_FIRST);
		uint8_t st = (uint8_t)(cur_led_state[led] & 0x03u);
		if (st != led_status_prev_state[idx]) {
			led_status_prev_state[idx] = st;
			led_status_idle_counter[idx] = 0u;
			led_status_is_dimmed[idx] = 0u;
			if (st != LED_OFF) {
				Led_SetBrightness(led, LED_STATUS_MAX_BRIGHTNESS);
			}
			continue;
		}
		if (st == LED_OFF) {
			led_status_idle_counter[idx] = 0u;
			led_status_is_dimmed[idx] = 0u;
			continue;
		}
		if (led_status_idle_counter[idx] < LED_STATUS_IDLE_TIMEOUT_TICKS) {
			led_status_idle_counter[idx]++;
		}
		if ((led_status_idle_counter[idx] >= LED_STATUS_IDLE_TIMEOUT_TICKS) && !led_status_is_dimmed[idx]) {
			Led_SetBrightness(led, LED_STATUS_DIM_BRIGHTNESS);
			led_status_is_dimmed[idx] = 1u;
		}
	}
}

static uint8_t Led_PackGroupState(const uint8_t *state_arr, uint8_t group)
{
	uint8_t base = (uint8_t)(group * 4u);
	uint8_t val = 0u;
	for (uint8_t i = 0u; i < 4u; i++) {
		uint8_t idx = (uint8_t)(base + i);
		uint8_t st = (idx < NUM_LED) ? state_arr[idx] : LED_OFF;
		st &= 0x03u;
		/* I2C (AW9523): на LED 2 бита, кодирование (st<<1): 0→00 off, 1→10 LED on.
		 * st==2 (логический BLINK) даёт (2<<1)=0b100 и затирает соседний канал
		 * (LED_START blink → физически OFF + LED_STOP ON). В пакет — как ON/OFF
		 * по фазе; мигание крутит Led_Process. */
		if (st == 2u) {
			st = (s_led_blink_on != 0u) ? 1u : 0u;
		} else if (st > 1u) {
			st = 1u;
		}
		val |= (uint8_t)((st << 1u) << (i * 2u));
	}
	return val;
}

static void Led_SyncToI2C(void)
{
	/* 1) Яркость по каналам */
	for (uint8_t i = 0u; i < NUM_LED; i++) {
		if (cur_led_power[i] != hw_led_power[i]) {
			uint8_t pwr = cur_led_power[i];
			HAL_I2C_Mem_Write(&hi2c2, 0xC0, (uint16_t)(2u + i), I2C_MEMADD_SIZE_8BIT, &pwr, sizeof(pwr), 20);
			hw_led_power[i] = pwr;
		}
	}

	/* 2) Состояния (регистры по 4 LED) */
	for (uint8_t group = 0u; group < 4u; group++) {
		uint8_t base = (uint8_t)(group * 4u);
		uint8_t dirty = 0u;
		uint8_t cur_val;
		uint8_t hw_val;
		/* 0xFF в hw-кэше — «форс sync» после смены фазы BLINK.
		 * Pack(0xFF&3→3→1) совпадает с фазой blink_on=1 → cur==hw и
		 * I2C-запись пропускалась: LED_FIRE (MODE_BLINK/ПОЖАР2) оставался OFF. */
		for (uint8_t i = 0u; i < 4u; i++) {
			uint8_t idx = (uint8_t)(base + i);
			if (idx < NUM_LED && hw_led_state[idx] == 0xFFu) {
				dirty = 1u;
				break;
			}
		}
		cur_val = Led_PackGroupState(cur_led_state, group);
		hw_val = Led_PackGroupState(hw_led_state, group);
		if (dirty != 0u || cur_val != hw_val) {
			HAL_I2C_Mem_Write(&hi2c2, 0xC0, (uint16_t)(0x14u + group), I2C_MEMADD_SIZE_8BIT, &cur_val, sizeof(cur_val), 20);
			for (uint8_t i = 0u; i < 4u; i++) {
				uint8_t idx = (uint8_t)(base + i);
				if (idx < NUM_LED) {
					hw_led_state[idx] = cur_led_state[idx];
				}
			}
		}
	}
}

void Led_Init() {
	  HAL_StatusTypeDef st = HAL_ERROR;
	  uint8_t led = 0b1111;
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20); // Задаём режим OSC = normal
	  //TODO обозначить ошибку, еслит нет связи
	  led =  0xff;
	  for(uint8_t i = 2; i <= 0x10; i++) {
		  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, i, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20); // включаем максимальную подстветку на каждом канале
		  HAL_Delay(1);
	  }
	  /*
	  // зажигаем все светодиоды на 500мс
	  led = 0xFF;
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x14, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x15, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x16, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x17, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  HAL_Delay(100);
	  */
	  // выключаем все светодиоды
	  led = 0;
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x14, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x15, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x16, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  st = HAL_I2C_Mem_Write(&hi2c2, 0xC0, 0x17, I2C_MEMADD_SIZE_8BIT, &led, sizeof(led), 20);
	  HAL_Delay(100);

	  Led_Snake(1);
	  Led_Snake(0);

	  for(uint8_t i = 0; i < NUM_LED; i++) {
		  cur_led_state[i] = LED_OFF;
		  cur_led_power[i] = 0xFFu;
		  hw_led_state[i] = 0xFFu; /* форсируем первую синхронизацию */
		  hw_led_power[i] = 0xFFu; /* форсируем первую синхронизацию */
	  }
	  for (uint8_t i = 0; i < LED_STATUS_COUNT; i++) {
		  led_status_prev_state[i] = LED_OFF;
		  led_status_idle_counter[i] = 0u;
		  led_status_is_dimmed[i] = 0u;
	  }

	  /* Подсветка кнопок ENTER/ESC по умолчанию включена с небольшой яркостью */
	  Led_Set(LED_BUT_ENTER_UP, LED_ON);
	  Led_Set(LED_BUT_ESC_DW,  LED_ON);
	  Led_SetBrightness(LED_BUT_ENTER_UP, LED_BUT_DIM_BRIGHTNESS);
	  Led_SetBrightness(LED_BUT_ESC_DW,  LED_BUT_DIM_BRIGHTNESS);

	  /* Надпись ПУСК ОБЩИЙ — дежурный режим (кнопка выкл.). */
	  Led_Set(LED_STR_START_ALL, LED_ON);
	  Led_Set(LED_BUT_START_ALL, LED_OFF);
	  Led_SetBrightness(LED_STR_START_ALL, LED_BUT_DIM_BRIGHTNESS);
	  Led_SetBrightness(LED_BUT_START_ALL, LED_BUT_DIM_BRIGHTNESS);

	  /* POWER/NORM only from PPKU snapshot (rs_v3_sync_status_leds). */
	  Led_Set(LED_POWER, LED_OFF);
	  Led_Set(LED_NORM,  LED_OFF);
	  Led_SetBrightness(LED_POWER, LED_STATUS_MAX_BRIGHTNESS);
	  Led_SetBrightness(LED_NORM,  LED_STATUS_MAX_BRIGHTNESS);

  led_but_is_bright = 0;
  led_sync_tick = LED_I2C_SYNC_PERIOD_TICKS;
  Led_Process();


}

void Led_SetAll(uint8_t power) {
	if (s_led_test_mode != 0u) {
		return;
	}
	for(uint8_t i = 0; i < NUM_LED; i++) {
		cur_led_state[i] = LED_ON;
		cur_led_power[i] = power;
	}
}

void Led_OffAll() {
	if (s_led_test_mode != 0u) {
		return;
	}
	for(uint8_t i = 0; i < NUM_LED; i++) {
		cur_led_state[i] = LED_OFF;
	}
}


void Led_Set(uint8_t led, uint8_t st) {
	if (s_led_test_mode != 0u) {
		return;
	}
	if (led >= NUM_LED) {
		return;
	}
    // st – только 2 бита
    st &= 0x03;
    if (cur_led_state[led] == st) {
        /* Состояние не изменилось */
        return;
    }
    cur_led_state[led] = st;
}

void Led_EnterTestMode(void)
{
	uint8_t i;
	if (s_led_test_mode != 0u) {
		return;
	}
	for (i = 0u; i < NUM_LED; i++) {
		s_led_test_saved_state[i] = cur_led_state[i];
		s_led_test_saved_power[i] = cur_led_power[i];
		cur_led_state[i] = LED_OFF;
		cur_led_power[i] = LED_BUT_MAX_BRIGHTNESS;
		hw_led_state[i] = 0xFFu;
		hw_led_power[i] = 0xFFu;
	}
	s_led_test_mode = 1u;
	led_but_is_bright = 1u;
	led_but_idle_counter = 0u;
	led_sync_tick = LED_I2C_SYNC_PERIOD_TICKS;
	Led_SyncToI2C();
}

void Led_ExitTestMode(void)
{
	uint8_t i;
	if (s_led_test_mode == 0u) {
		return;
	}
	s_led_test_mode = 0u;
	for (i = 0u; i < NUM_LED; i++) {
		cur_led_state[i] = s_led_test_saved_state[i];
		cur_led_power[i] = s_led_test_saved_power[i];
		hw_led_state[i] = 0xFFu;
		hw_led_power[i] = 0xFFu;
	}
	led_sync_tick = LED_I2C_SYNC_PERIOD_TICKS;
	Led_SyncToI2C();
}

uint8_t Led_IsTestMode(void)
{
	return s_led_test_mode;
}

void Led_TestSet(uint8_t led, uint8_t st)
{
	if (led >= NUM_LED) {
		return;
	}
	st &= 0x03u;
	if (st > 1u) {
		st = 1u;
	}
	cur_led_state[led] = st;
	cur_led_power[led] = LED_BUT_MAX_BRIGHTNESS;
	hw_led_state[led] = 0xFFu;
	hw_led_power[led] = 0xFFu;
	led_sync_tick = LED_I2C_SYNC_PERIOD_TICKS;
}

uint8_t Led_TestGet(uint8_t led)
{
	if (led >= NUM_LED) {
		return LED_OFF;
	}
	return (uint8_t)(cur_led_state[led] & 0x03u);
}

void Led_Snake(uint8_t state) {
	const uint8_t seq[] = {
		LED_BUT_ENTER_UP, LED_FIRE, LED_BUT_ESC_DW, LED_AUTO_OFF, LED_POWER,
		LED_NORM, LED_START, LED_STOP, LED_ERR, LED_BUT_START_ALL,
		LED_STR_START_ALL, LED_STR_STOP, LED_BUT_STOP, LED_BUT_START_SP, LED_STR_START_SP
	};
	for (uint8_t i = 0u; i < (uint8_t)(sizeof(seq) / sizeof(seq[0])); i++) {
		Led_Set(seq[i], state);
		Led_SyncToI2C();
		HAL_Delay(50);
	}


/*
	for(uint8_t i = 0; i < NUM_LED; i++) {
		LedSet(i, state);
		HAL_Delay(delay);
	}
*/
}

void Led_RunIndicationSnake(void)
{
	uint8_t saved_state[NUM_LED];
	uint8_t saved_power[NUM_LED];
	uint8_t i;

	for (i = 0u; i < NUM_LED; i++) {
		saved_state[i] = cur_led_state[i];
		saved_power[i] = cur_led_power[i];
	}

	Led_Snake(1u);
	Led_Snake(0u);

	for (i = 0u; i < NUM_LED; i++) {
		cur_led_state[i] = saved_state[i];
		cur_led_power[i] = saved_power[i];
		hw_led_state[i] = 0xFFu; /* форсировать синхронизацию после теста */
		hw_led_power[i] = 0xFFu;
	}
	Led_SyncToI2C();
}

void Led_TestToogle() {
	for(uint8_t i = 2; i < (NUM_LED - 2); i++) {
		if(cur_led_state[i]) {
			Led_Set(i, 0);
			if(i < (NUM_LED - 1 - 2))
				Led_Set(i + 1, 1);
			else
				Led_Set(2, 1);
			return;
		}
	}
	Led_Set(2, 1);
}

void Led_Process() {
	/* Подсветка кнопок ENTER/ESC/UP/DOWN:
	 * - по умолчанию горит с мощностью LED_BUT_DIM_BRIGHTNESS
	 * - при активности пользовательских кнопок – максимум LED_BUT_MAX_BRIGHTNESS
	 * - если LED_BUT_IDLE_TIMEOUT_TICKS нет нажатий –
	 *   снова небольшая яркость
	 */

	if (s_led_test_mode != 0u) {
		/* ТЕСТ2: яркость всегда максимум, автозатухание выкл. */
		led_but_is_bright = 1u;
		led_but_idle_counter = 0u;
		for (uint8_t i = 0u; i < NUM_LED; i++) {
			if (cur_led_power[i] != LED_BUT_MAX_BRIGHTNESS) {
				cur_led_power[i] = LED_BUT_MAX_BRIGHTNESS;
				hw_led_power[i] = 0xFFu;
			}
		}
	} else {
	/* Кнопки ПУСК СП и ОСТАНОВ ПУСКА не влияют на общую подсветку
	 * (требование режима НОРМА). */
	ButtonState st_enter = Button_GetState(BUT_ENTER);
	ButtonState st_esc   = Button_GetState(BUT_ESC);
	ButtonState st_up    = Button_GetState(BUT_UP);
	ButtonState st_down  = Button_GetState(BUT_DOWN);
	ButtonState st_force = Button_GetState(BUT_FORCE);
	uint8_t any_pressed = ((st_enter == ButtonStatePress) || (st_enter == ButtonStateLongPress) ||
	                    (st_esc == ButtonStatePress)   || (st_esc == ButtonStateLongPress) ||
	                    (st_up == ButtonStatePress)    || (st_up == ButtonStateLongPress) ||
	                    (st_down == ButtonStatePress)  || (st_down == ButtonStateLongPress) ||
	                    (st_force == ButtonStatePress) || (st_force == ButtonStateLongPress));

	if (any_pressed) {
		/* Есть нажатие – сразу делаем максимум и обнуляем таймер */
		led_but_idle_counter = 0;
		if (!led_but_is_bright) {
			led_but_is_bright = 1;
			Led_SetBrightness(LED_BUT_ENTER_UP, LED_BUT_MAX_BRIGHTNESS);
			Led_SetBrightness(LED_BUT_ESC_DW,  LED_BUT_MAX_BRIGHTNESS);
		}
	} else {
		/* Нет нажатий – считаем время простоя */
		if (led_but_idle_counter < LED_BUT_IDLE_TIMEOUT_TICKS) {
			led_but_idle_counter++;
			if ((led_but_idle_counter >= LED_BUT_IDLE_TIMEOUT_TICKS) && led_but_is_bright) {
				/* По истечении таймаута возвращаемся к небольшой яркости */
				led_but_is_bright = 0;
				Led_SetBrightness(LED_BUT_ENTER_UP, LED_BUT_DIM_BRIGHTNESS);
				Led_SetBrightness(LED_BUT_ESC_DW,  LED_BUT_DIM_BRIGHTNESS);
			}
		}
	}
	Led_UpdateStatusBrightness();
	}

	/* Logical BLINK (state==2): переключаем фазу и форсируем I2C sync. */
	{
		uint8_t any_blink = 0u;
		uint8_t li;
		for (li = 0u; li < NUM_LED; li++) {
			if ((cur_led_state[li] & 0x03u) == 2u) {
				any_blink = 1u;
				break;
			}
		}
		if (any_blink != 0u) {
			if (s_led_blink_tick < LED_BLINK_HALF_TICKS) {
				s_led_blink_tick++;
			}
			if (s_led_blink_tick >= LED_BLINK_HALF_TICKS) {
				s_led_blink_tick = 0u;
				s_led_blink_on = (uint8_t)!s_led_blink_on;
				/* Сброс hw-кэша для blink-каналов — иначе Pack cur==hw и sync не уйдёт. */
				for (li = 0u; li < NUM_LED; li++) {
					if ((cur_led_state[li] & 0x03u) == 2u) {
						hw_led_state[li] = 0xFFu;
					}
				}
				led_sync_tick = LED_I2C_SYNC_PERIOD_TICKS;
			}
		} else {
			s_led_blink_tick = 0u;
			s_led_blink_on = 1u;
		}
	}

	/* Периодическая синхронизация состояния/яркости в драйвер LED по I2C */
	if (led_sync_tick < LED_I2C_SYNC_PERIOD_TICKS) {
		led_sync_tick++;
	}
	if (led_sync_tick >= LED_I2C_SYNC_PERIOD_TICKS) {
		led_sync_tick = 0u;
		Led_SyncToI2C();
	}
}

void Led_SetBrightness(uint8_t led, uint8_t power) {
	if (s_led_test_mode != 0u) {
		return;
	}
	if (led >= NUM_LED) {
		return;
	}
	cur_led_power[led] = power;
}

void Led_ForceStatusBright(uint8_t led)
{
	if (s_led_test_mode != 0u) {
		return;
	}
	if (led < LED_STATUS_FIRST || led > LED_STATUS_LAST) {
		return;
	}
	uint8_t idx = (uint8_t)(led - LED_STATUS_FIRST);
	led_status_idle_counter[idx] = 0u;
	led_status_is_dimmed[idx] = 0u;
	Led_SetBrightness(led, LED_STATUS_MAX_BRIGHTNESS);
}


