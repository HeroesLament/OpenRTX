/***************************************************************************
 *   Copyright (C) 2020 by Federico Amedeo Izzo IU2NUO,                    *
 *                         Niccolò Izzo IU2KIN                             *
 *                         Frederik Saraci IU2NRO                          *
 *                         Silvano Seva IU2KWO                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stm32f4xx.h>
#include <hwconfig.h>
#include <gpio.h>

/*
 * Sine table for PWM-based sinewave generation, containing 256 samples over one
 * period of a 64Hz sinewave. This gives a PWM base frequency of 16.384kHz.
 */
uint8_t sineTable[] =
{
    128,131,134,137,140,143,146,149,152,155,158,162,165,167,170,173,176,179,182,
    185,188,190,193,196,198,201,203,206,208,211,213,215,218,220,222,224,226,228,
    230,232,234,235,237,238,240,241,243,244,245,246,248,249,250,250,251,252,253,
    253,254,254,254,255,255,255,255,255,255,255,254,254,254,253,253,252,251,250,
    250,249,248,246,245,244,243,241,240,238,237,235,234,232,230,228,226,224,222,
    220,218,215,213,211,208,206,203,201,198,196,193,190,188,185,182,179,176,173,
    170,167,165,162,158,155,152,149,146,143,140,137,134,131,128,124,121,118,115,
    112,109,106,103,100,97,93,90,88,85,82,79,76,73,70,67,65,62,59,57,54,52,49,47,
    44,42,40,37,35,33,31,29,27,25,23,21,20,18,17,15,14,12,11,10,9,7,6,5,5,4,3,2,
    2,1,1,1,0,0,0,0,0,0,0,1,1,1,2,2,3,4,5,5,6,7,9,10,11,12,14,15,17,18,20,21,23,
    25,27,29,31,33,35,37,40,42,44,47,49,52,54,57,59,62,65,67,70,73,76,79,82,85,88,
    90,93,97,100,103,106,109,112,115,118,121,124
};

/*
 * Correction factor to compensate for the (slight) difference between the exact
 * sampling frequency of the sine table and the PWM frequency generated by the
 * timer. This difference causes a frequency error in the resulting output of
 * both CTCSS and "beep" signals.
 * To compensate for this, simply multiply the target frequency by this
 * correction factor.
 */
const float freqCorrFactor  = 16384.0f/16406.25;
const uint32_t baseSineFreq = 64;

uint32_t toneTableIndex = 0; /* Current sine table index for CTCSS generator  */
uint32_t toneTableIncr  = 0; /* CTCSS sine table index increment per tick     */

uint32_t beepTableIndex = 0; /* Current sine table index for "beep" generator */
uint32_t beepTableIncr  = 0; /* "beep" sine table index increment per tick    */
uint32_t beepTimerCount = 0; /* Downcounter for timed "beep"                  */

void __attribute__((used)) TIM3_IRQHandler()
{
    toneTableIndex += toneTableIncr;
    beepTableIndex += beepTableIncr;

    TIM3->CCR2 = sineTable[(toneTableIndex >> 16) & 0xFF];
    TIM3->CCR3 = sineTable[(beepTableIndex >> 16) & 0xFF];
    TIM3->SR = 0;

    if(beepTimerCount > 0)
    {
        beepTimerCount--;
        if(beepTimerCount == 0)
        {
            TIM3->CCER &= ~TIM_CCER_CC3E;
        }
    }

    /* Shutdown timer if both compare channels are inactive */
    if((TIM3->CCER & (TIM_CCER_CC2E | TIM_CCER_CC3E)) == 0)
    {
        TIM3->CR1 &= ~TIM_CR1_CEN;
    }
}

void toneGen_init()
{
    /*
     * Configure GPIOs:
     * - CTCSS output is on PC7 (on MD380), that is TIM3-CH2, AF2
     * - "beep" output is on PC8 (on MD380), that is TIM3-CH3, AF2
     */
    gpio_setMode(CTCSS_OUT, ALTERNATE);
    gpio_setMode(BEEP_OUT,  ALTERNATE);
    gpio_setAlternateFunction(CTCSS_OUT, 2);
    gpio_setAlternateFunction(BEEP_OUT,  2);

    /*
     * Timer configuration:
     * - APB1 frequency = 42MHz, with 1:10 prescaler we have Ftick = 4.2MHz
     * - ARR = 255 (8-bit PWM), gives an update rate of 16.406kHz
     * - Nominal update rate is 16.384kHz -> error = +22.25Hz
     */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    TIM3->ARR   = 0xFF;
    TIM3->PSC   = 9;
    TIM3->CCMR1 = TIM_CCMR1_OC2M_2  /* CH2 in PWM mode 1, preload enabled */
                | TIM_CCMR1_OC2M_1
                | TIM_CCMR1_OC2PE;
    TIM3->CCMR2 = TIM_CCMR2_OC3M_2  /* The same for CH3                   */
                | TIM_CCMR2_OC3M_1
                | TIM_CCMR2_OC3PE;
    TIM3->DIER |= TIM_DIER_UIE;     /* Interrupt on counter update        */
    TIM3->CR1  |= TIM_CR1_ARPE;     /* Enable auto preload on reload      */

    NVIC_SetPriority(TIM3_IRQn, 10);
    NVIC_EnableIRQ(TIM3_IRQn);
}

void toneGen_shutdown()
{
    RCC->APB1ENR &= ~RCC_APB1ENR_TIM3EN;
    gpio_setMode(CTCSS_OUT, INPUT);
    gpio_setMode(BEEP_OUT,  INPUT);
}

void toneGen_setToneFreq(float toneFreq)
{
    /*
     * Convert to 16.16 fixed point number, then divide by  the frequency of
     * sinewave stored in the PWM table
     */
    float dividend = toneFreq * freqCorrFactor * 65536.0f;
    toneTableIncr = ((uint32_t) dividend)/baseSineFreq;
}

void toneGen_toneOn()
{
    TIM3->CCER |= TIM_CCER_CC2E;
    TIM3->CR1  |= TIM_CR1_CEN;
}

void toneGen_toneOff()
{
    TIM3->CCER &= ~TIM_CCER_CC2E;
}

void toneGen_setBeepFreq(float beepFreq)
{
    float dividend = beepFreq * freqCorrFactor * 65536.0f;
    beepTableIncr = ((uint32_t) dividend)/baseSineFreq;
}

void toneGen_beepOn()
{
    TIM3->CCER |= TIM_CCER_CC3E;
    TIM3->CR1  |= TIM_CR1_CEN;
}

void toneGen_beepOff()
{
    TIM3->CCER &= ~TIM_CCER_CC3E;
}

void toneGen_timedBeep(uint16_t duration)
{
    /*
     * Duration is in milliseconds, while counter update rate is 16.406kHz.
     * Thus, the value for downcounter is (duration * 16.406)/1000.
     */
    beepTimerCount = (duration * 16406)/1000;
    TIM3->CCER |= TIM_CCER_CC3E;
    TIM3->CR1  |= TIM_CR1_CEN;
}