/*
 * button.c
 *
 *  Created on: Dec 4, 2025
 *      Author: 79099
 */

#include "button.h"
#include "beeper.h"

struct Button Buttons[NUM_BUTTON];
extern I2C_HandleTypeDef hi2c2;

#define BUTTON_I2C_RECOVERY_RETRY_TICKS 10u

static uint8_t s_btn_i2c_recovery_tick = BUTTON_I2C_RECOVERY_RETRY_TICKS;
static uint8_t s_i2c_keys_ok = 1u;

static void Button_SetI2cKeysError(void)
{
	Buttons[BUT_ESC].state = ButtonStateError;
	Buttons[BUT_UP].state = ButtonStateError;
	Buttons[BUT_DOWN].state = ButtonStateError;
	Buttons[BUT_ENTER].state = ButtonStateError;
}

static void Button_ClearI2cKeysError(void)
{
	for (uint8_t i = BUT_ESC; i <= BUT_ENTER; i++) {
		if (Buttons[i].state == ButtonStateError) {
			Buttons[i].state = ButtonStateReset;
		}
	}
}

static uint8_t Button_ReinitI2CDriver(void)
{
	if (HAL_I2C_DeInit(&hi2c2) != HAL_OK) {
		return 0u;
	}
	if (HAL_I2C_Init(&hi2c2) != HAL_OK) {
		return 0u;
	}
	if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
		return 0u;
	}
	if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK) {
		return 0u;
	}
	return 1u;
}

__attribute__((weak)) void Button_ReinitReaderChip(void)
{
}

void Button_Init(void)
{
	for (uint8_t i = 0; i < NUM_BUTTON; i++) {
		Buttons[i].state = ButtonStateReset;
		Buttons[i].press_counter = 0;
		Buttons[i].ispress = 0;
	}

	uint8_t but = 0xFF;
	if (HAL_I2C_Mem_Read(&hi2c2, 0x41u << 1, 0x00, I2C_MEMADD_SIZE_8BIT, &but, sizeof(but), 10) != HAL_OK) {
		s_i2c_keys_ok = 0u;
		Button_SetI2cKeysError();
	} else {
		s_i2c_keys_ok = 1u;
	}
}

void Button_Process(void)
{
	uint8_t beep_done = 0u;

	Button_ReadPin();

	if (s_i2c_keys_ok == 0u) {
		if (s_btn_i2c_recovery_tick < BUTTON_I2C_RECOVERY_RETRY_TICKS) {
			s_btn_i2c_recovery_tick++;
		} else {
			s_btn_i2c_recovery_tick = 0u;
			if (Button_ReinitI2CDriver()) {
				Button_ReinitReaderChip();
				uint8_t but = 0xFF;
				if (HAL_I2C_Mem_Read(&hi2c2, 0x41u << 1, 0x00, I2C_MEMADD_SIZE_8BIT, &but, sizeof(but), 10) == HAL_OK) {
					s_i2c_keys_ok = 1u;
					Button_ClearI2cKeysError();
				}
			}
		}
	} else {
		s_btn_i2c_recovery_tick = BUTTON_I2C_RECOVERY_RETRY_TICKS;
	}

	for (uint8_t i = 0; i < NUM_BUTTON; i++) {
		if (Buttons[i].ispress == 0) {
			Buttons[i].state = ButtonStateReset;
			Buttons[i].press_counter = 0;
		} else {
			if ((Buttons[i].press_counter >= LONG_PRESS_COUNT) && (Buttons[i].state == ButtonStatePress)) {
				Buttons[i].state = ButtonStateLongPress;
			}
			if ((Buttons[i].press_counter >= SHORT_PRESS_COUNT) && (Buttons[i].state == ButtonStateReset)) {
				Buttons[i].state = ButtonStatePress;
				if (!beep_done) {
					Beeper_ButtonAcknowledge();
					beep_done = 1u;
				}
			}
			Buttons[i].press_counter++;
		}
	}
}

ButtonState Button_GetState(uint8_t but)
{
	return Buttons[but].state;
}

void Button_ReadPin(void)
{
	Buttons[BUT_FORCE].ispress = HAL_GPIO_ReadPin(BT_FORCE_ACT_GPIO_Port, BT_FORCE_ACT_Pin);
	Buttons[BUT_STOP].ispress = HAL_GPIO_ReadPin(BT_STOP_GPIO_Port, BT_STOP_Pin);
	Buttons[BUT_FIRE].ispress = HAL_GPIO_ReadPin(BT_FIRE_GPIO_Port, BT_FIRE_Pin);

	uint8_t but = 0xFF;
	if (HAL_I2C_Mem_Read(&hi2c2, 0x41u << 1, 0x00, I2C_MEMADD_SIZE_8BIT, &but, sizeof(but), 10) != HAL_OK) {
		s_i2c_keys_ok = 0u;
		Button_SetI2cKeysError();
		Buttons[BUT_ESC].ispress = 0u;
		Buttons[BUT_UP].ispress = 0u;
		Buttons[BUT_DOWN].ispress = 0u;
		Buttons[BUT_ENTER].ispress = 0u;
		return;
	}

	s_i2c_keys_ok = 1u;
	Button_ClearI2cKeysError();
	Buttons[BUT_ENTER].ispress = (but >> BUT_ENTER) & 0x1u;
	Buttons[BUT_UP].ispress = (but >> BUT_UP) & 0x1u;
	Buttons[BUT_DOWN].ispress = (but >> BUT_DOWN) & 0x1u;
	Buttons[BUT_ESC].ispress = (but >> BUT_ESC) & 0x1u;
}
