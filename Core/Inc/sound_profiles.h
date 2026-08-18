#ifndef INC_SOUND_PROFILES_H_
#define INC_SOUND_PROFILES_H_

/*
 * Звук: PA5 = DAC1_OUT2 → C-R → LM386 → KPEG116 (резонанс ~2 kHz).
 * TIM6 семплирует синус в ЦАП (не меандр 0/макс). Громче всего ноты около 2 kHz.
 */
#define SOUND_DAC_SAMPLE_HZ                    32000u
#define SOUND_TONE_HZ                          2093u
#define SOUND_DAC_LEVEL_MAX                    4095u
#define SOUND_DAC_LEVEL_MID                    2048u
#define SOUND_DAC_LEVEL_OFF                    0u
#define SOUND_DAC_LEVEL_ON                     SOUND_DAC_LEVEL_MAX

/* Ноты около резонанса пищалки */
#define NOTE_C6                                1047u
#define NOTE_D6                                1175u
#define NOTE_E6                                1319u
#define NOTE_F6                                1397u
#define NOTE_G6                                1568u
#define NOTE_A6                                1760u
#define NOTE_B6                                1976u
#define NOTE_C7                                2093u
#define NOTE_D7                                2349u
#define NOTE_E7                                2637u

#define SOUND_BUTTON_NOTE_HZ                   NOTE_A6
#define SOUND_STARTUP_BEEP_MS                  1400u

/*
 * Прыжок (кнопка): восходящий свип, не мелодия Nintendo.
 * Диапазон около резонанса KPEG116 (~2 kHz), иначе низ не слышно.
 */
#define SOUND_JUMP_START_HZ                    NOTE_D6
#define SOUND_JUMP_END_HZ                      NOTE_D7
#define SOUND_JUMP_MS                          170u

/* Базовые one-shot сигналы */
#define SOUND_BUTTON_ACK_ON_MS                 SOUND_JUMP_MS
#define SOUND_ONE_SHOT_LONG_ON_MS             1300u
#define SOUND_ONE_SHOT_PAUSE_MS                200u

/* НЕИСПРАВНОСТЬ: сигнальный + дежурный */
#define SOUND_FAULT_SIGNAL_ON_MS               500u
#define SOUND_FAULT_SIGNAL_OFF_MS              100u
#define SOUND_FAULT_SIGNAL_PULSES              2u
#define SOUND_FAULT_DUTY_ON_MS                  10u
#define SOUND_FAULT_DUTY_OFF_MS                 50u
#define SOUND_FAULT_DUTY_PULSES                 2u
#define SOUND_FAULT_DUTY_REPEAT_MS           10000u

/* ВНИМАНИЕ: сигнальный + дежурный (отдельный от НЕИСПРАВНОСТИ) */
#define SOUND_ATTN_SIGNAL_ON_MS                120u
#define SOUND_ATTN_SIGNAL_OFF_MS                80u
#define SOUND_ATTN_SIGNAL_PULSES                3u
#define SOUND_ATTN_DUTY_ON_MS                   80u
#define SOUND_ATTN_DUTY_OFF_MS                 120u
#define SOUND_ATTN_DUTY_PULSES                  2u
#define SOUND_ATTN_DUTY_REPEAT_MS             5000u

/* ПОЖАР/ПУСК */
#define SOUND_FIRE_DUTY_ON_MS                   70u
#define SOUND_FIRE_DUTY_OFF_MS                  50u
#define SOUND_FIRE_DUTY_PULSES                  1u
#define SOUND_FIRE_DUTY_REPEAT_MS            10000u

#define SOUND_FIRE1_SIGNAL_ON_MS              2000u
#define SOUND_FIRE1_SIGNAL_OFF_MS              500u
#define SOUND_FIRE1_SIGNAL_PULSES                1u
#define SOUND_FIRE1_SIGNAL_REPEAT_MS          2500u
#define SOUND_FIRE1_DUTY_ON_MS          SOUND_FIRE_DUTY_ON_MS
#define SOUND_FIRE1_DUTY_OFF_MS         SOUND_FIRE_DUTY_OFF_MS
#define SOUND_FIRE1_DUTY_PULSES         SOUND_FIRE_DUTY_PULSES
#define SOUND_FIRE1_DUTY_REPEAT_MS      (SOUND_FIRE_DUTY_REPEAT_MS / 2u)

#define SOUND_START_DUTY_ON_MS                 200u
#define SOUND_START_DUTY_OFF_MS                200u
#define SOUND_START_DUTY_PULSES                 1u
#define SOUND_START_DUTY_REPEAT_MS             100u

#define SOUND_START_ALL_HOLD_DUTY_MS           800u
#define SOUND_START_ALL_HOLD_PERIOD_MS        1600u

#define SOUND_TEST_ON_MS                       300u
#define SOUND_TEST_OFF_MS                      100u
#define SOUND_TEST_PULSES                        3u

#endif /* INC_SOUND_PROFILES_H_ */
