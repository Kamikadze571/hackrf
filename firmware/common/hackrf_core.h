/*
 * Copyright 2012 Michael Ossmann <mike@ossmann.com>
 * Copyright 2012 Benjamin Vernoux <titanmkd@gmail.com>
 *
 * This file is part of HackRF.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __HACKRF_CORE_H
#define __HACKRF_CORE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <lpc43.h>

/* hardware identification number */
#define BOARD_ID_JELLYBEAN 0

#ifdef JELLYBEAN
#define BOARD_ID BOARD_ID_JELLYBEAN
#endif

#ifdef JELLYBEAN
/*
 * Jellybean SCU PinMux
 */

/* GPIO Output PinMux */
#define SCU_PINMUX_LED1     (P4_1)  /* GPIO2[1] on P4_1 */
#define SCU_PINMUX_LED2     (P4_2)  /* GPIO2[2] on P4_2 */
#define SCU_PINMUX_LED3     (P6_12) /* GPIO2[8] on P6_12 */

#define SCU_PINMUX_EN1V8    (P6_10) /* GPIO3[6] on P6_10 */

/* GPIO Input PinMux */
#define SCU_PINMUX_BOOT0    (P1_1)  /* GPIO0[8] on P1_1 */
#define SCU_PINMUX_BOOT1    (P1_2)  /* GPIO0[9] on P1_2 */
#define SCU_PINMUX_BOOT2    (P2_8)  /* GPIO5[7] on P2_8 */
#define SCU_PINMUX_BOOT3    (P2_9)  /* GPIO1[10] on P2_9 */

/* SSP1 Peripheral PinMux */
#define SCU_SSP1_MISO       (P1_3)  /* P1_3 */
#define SCU_SSP1_MOSI       (P1_4)  /* P1_4 */
#define SCU_SSP1_SCK        (P1_19) /* P1_19 */
#define SCU_SSP1_SSEL       (P1_20) /* P1_20 */

/* TODO add other Pins */

/*
 * Jellybean GPIO Pins
 */

/* GPIO Output */
#define PIN_LED1    (BIT1) /* GPIO2[1] on P4_1 */
#define PIN_LED2    (BIT2) /* GPIO2[2] on P4_2 */
#define PIN_LED3    (BIT8) /* GPIO2[8] on P6_12 */
#define PORT_LED1_3 (GPIO2) /* PORT for LED1, 2 & 3 */

#define PIN_EN1V8   (BIT6) /* GPIO3[6] on P6_10 */
#define PORT_EN1V8  (GPIO3)

/* GPIO Input */
#define PIN_BOOT0   (BIT8)  /* GPIO0[8] on P1_1 */
#define PIN_BOOT1   (BIT9)  /* GPIO0[9] on P1_2 */
#define PIN_BOOT2   (BIT7)  /* GPIO5[7] on P2_8 */
#define PIN_BOOT3   (BIT10) /* GPIO1[10] on P2_9 */

/* Read GPIO Pin */
#define BOOT0_STATE ((GPIO0_PIN & PIN_BOOT0)==PIN_BOOT0)
#define BOOT1_STATE ((GPIO0_PIN & PIN_BOOT1)==PIN_BOOT1)
#define BOOT2_STATE ((GPIO5_PIN & PIN_BOOT2)==PIN_BOOT2)
#define BOOT3_STATE ((GPIO1_PIN & PIN_BOOT3)==PIN_BOOT3)

/* indicator LED control */
#define LED1_SET (GPIO_SET2 = PIN_LED1)
#define LED1_CLR (GPIO_CLR2 = PIN_LED1)
#define LED2_SET (GPIO_SET2 = PIN_LED2)
#define LED2_CLR (GPIO_CLR2 = PIN_LED2)
#define LED3_SET (GPIO_SET2 = PIN_LED3)
#define LED3_CLR (GPIO_CLR2 = PIN_LED3)

/* 1V8 control */
#define EN1V8_SET (GPIO_SET3 = PIN_EN1V8)
#define EN1V8_CLR (GPIO_CLR3 = PIN_EN1V8)

/* TODO add other Pins */
#endif

void cpu_clock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __HACKRF_CORE_H */
