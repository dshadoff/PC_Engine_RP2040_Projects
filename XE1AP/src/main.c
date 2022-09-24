/* 
 * PCEXE1AP - Adapts a USB controller for use with the PC Engine
 *            as a XE-1AP analog joysttack
 *            For Raspberry Pi Pico or other RP2040 MCU
 *            In particular, I like the Adafruit KB2040 board because
 *            it breaks out the D-/D+ USB wires to allow USB-A connectors
 *
 * This code is based on the TinyUSB Host HID example from pico-SDK v1.4.0
 *
 * Modifications for PCEXE1AP
 * Copyright (c) 2022 David Shadoff
 *
 * ------------------------------------
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

/* This example current worked and tested with following controller
 * - Sony DualShock 4 [CUH-ZCT2x] VID = 0x054c, PID = 0x09cc
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "clock.pio.h"
#include "protocol.pio.h"
#include "multplex.pio.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+


#ifdef ADAFRUIT_KB2040          // if build for Adafruit KB2040 board

#define DATAIN_PIN      18
#define CLKIN_PIN       DATAIN_PIN + 1  // Note - in pins must be a consecutive 'in' group

#define OUTD0_PIN       26              // Note - out pins must be a consecutive 'out' group
#define OUTD1_PIN       27
#define OUTD2_PIN       28
#define OUTD3_PIN       29

#define TEMPD0_PIN      2		// Note - 'out' pins for protocol, 'in' pins for multplex
#define TEMPD1_PIN      3
#define TEMPD2_PIN      4
#define TEMPD3_PIN      5
#define TEMPD4_PIN      6
#define TEMPD5_PIN      7
#define TEMPD6_PIN      8
#define TEMPD7_PIN      9

#endif

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
void led_blinking_task(void);

extern void cdc_task(void);
extern void hid_app_task(void);

uint32_t statusword0, statusword1, statusword2, statusword3, statusword4, statusword5;
uint32_t outword0, outword1, outword2, outword3, outword4, outword5;
volatile bool output_exclude;

PIO pio;
uint sm1, sm2, sm3;   // sm1 = clock; sm2 = protocol; sm3 = multplex


// process_signals - inner-loop processing of events:
//                   - USB polling
//                   - event processing
//                   - detection of when a PCE scan is no longer in process (reset period)
//
static void __not_in_flash_func(process_signals)(void)
{
  while (1)
  {
    // tinyusb host task
    tuh_task();
#ifndef ADAFRUIT_QTPY_RP2040
    led_blinking_task();
#endif

#if CFG_TUH_CDC
    cdc_task();
#endif

#if CFG_TUH_HID
    hid_app_task();
#endif

    if (!output_exclude) {
       outword0 = statusword0;
       outword1 = statusword1;
       outword2 = statusword2;
       outword3 = statusword3;
       outword4 = statusword4;
       outword5 = statusword5;
    }
    gpio_put(TEMPD2_PIN, outword0 & 0x10);
    gpio_put(TEMPD3_PIN, outword0 & 0x20);  // After remapping
  }
}

//
// core1_entry - inner-loop for the second core
//             - when the "CLR" line is de-asserted, set lock flag
//               protecting the output state machine from inconsistent data
//
static void __not_in_flash_func(core1_entry)(void)
{
static bool rx_bit = 0;

  while (1)
  {
     // wait for (and sync with) posedge of CLR signal; rx_data is throwaway
     rx_bit = pio_sm_get_blocking(pio, sm1);

     pio_sm_clear_fifos(pio, sm2);

     // Now we are in an update-sequence; set a lock
     // to prevent update during output transaction
     output_exclude = true;

     // Assume data is already formatted in output_word and push it to the state machine
     //
     // Note: Data is sent, 2 nybbles (1 wth TRG1 LOW, 1 with TRG1 HIGH) per cycle,
     //       for 6 cycles of the data transmission. These are sent as bytes, with
     //       Nybble 2 as most significant, and nybble 1 as least-signifcant
     //
     // The nybbles are in the following sequence:
     //  1) Buttons A, B, C, D (note: A is "pressed" is either A or A' is pressed; same with B/B')
     //  2) Buttons E1, E2, Start, Select
     //  3) most-significant 4 bits of 'Y' axis
     //  4) most-significant 4 bits of 'X' axis
     //  5) most-significant 4 bits of 'throttle' axis
     //  6) 0000
     //  7) least-significant 4 bits of 'Y' axis
     //  8) least-significant 4 bits of 'X' axis
     //  9) least-significant 4 bits of 'throttle' axis
     // 10) 0000
     // 11) Buttons A, B, A', B' (These are only able to be differentiated by this nybble)
     // 12) 1111
     //

     pio_sm_put_blocking(pio, sm2, outword0);
     pio_sm_put_blocking(pio, sm2, outword1);
     pio_sm_put_blocking(pio, sm2, outword2);
     pio_sm_put_blocking(pio, sm2, outword3);
     pio_sm_put_blocking(pio, sm2, outword4);
     pio_sm_put_blocking(pio, sm2, outword5);

     output_exclude = false;
  }
}

/*------------- MAIN -------------*/
int main(void)
{
  board_init();

  // Pause briefly for stability before starting activity
  sleep_ms(50);

  printf("TinyUSB Host HID Controller Example\r\n");
  printf("Note: Events only displayed for explictly supported controllers\r\n");

//
// Initialize the pins representing SEL and START on the multiplexed nybble
// All others belong to the PIOs
//
  gpio_init(TEMPD2_PIN);
  gpio_set_dir(TEMPD2_PIN, true);
  gpio_put(TEMPD2_PIN, 1);

  gpio_init(TEMPD3_PIN);
  gpio_set_dir(TEMPD3_PIN, true);
  gpio_put(TEMPD3_PIN, 1);


  tusb_init();

  // Both state machines can run on the same PIO processor
  pio = pio0;

  // Load the clock (synchronizing input) program, and configure a free state machine
  // to run the program.

  uint offset1 = pio_add_program(pio, &clock_program);
  sm1 = pio_claim_unused_sm(pio, true);
  clock_program_init(pio, sm1, offset1, CLKIN_PIN);

printf("pio1=%d; sm=%d; offset=%d; CLKIN_PIN=%d\n", pio, sm1, offset1, CLKIN_PIN);


  // Load the protocol (timed output) program, and configure a free state machine
  // to run the program.
  // Note that D0-D1 (button 1 & 2) are controlled by the "SET" pin group
  //           D2-D3 (select/RUN buttons) are controlled by the GPIOs
  //           D4-D7 (directions) are controlled by the "OUT" pin group

  uint offset2 = pio_add_program(pio, &protocol_program);
  sm2 = pio_claim_unused_sm(pio, true);
  protocol_program_init(pio, sm2, offset2, TEMPD0_PIN, TEMPD4_PIN);

printf("pio2=%d; sm=%d; offset=%d; TEMPD0_PIN=%d, TEMPD4_PIN=%d\n", pio, sm2, offset2, TEMPD0_PIN, TEMPD4_PIN);


  // Load the plex (multiplex output) program, and configure a free state machine
  // to run the program.

  uint offset3 = pio_add_program(pio, &multplex_program);
  sm3 = pio_claim_unused_sm(pio, true);
  multplex_program_init(pio, sm3, offset3, DATAIN_PIN, TEMPD0_PIN, OUTD0_PIN);

printf("pio3=%d; sm=%d; offset=%d; DATAIN=%d; TEMPD0=%d; OUTD0=%d\n", pio, sm3, offset3, DATAIN_PIN, TEMPD0_PIN, OUTD0_PIN);

// Initial values at startup
// before any USB packets come in
//
  statusword0 = 0x000000FF;
  statusword1 = 0x00000077;
  statusword2 = 0x00000007;
  statusword3 = 0x000000FF;
  statusword4 = 0x0000000F;
  statusword5 = 0x000000FF;

  multicore_launch_core1(core1_entry);

  process_signals();

  return 0;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  const uint32_t interval_ms = 1000;
  static uint32_t start_ms = 0;

  static bool led_state = false;

  // Blink every interval ms
  if ( board_millis() - start_ms < interval_ms) return; // not enough time
  start_ms += interval_ms;

//  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
