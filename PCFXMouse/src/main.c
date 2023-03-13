/* 
 * PCFXMouse - Adapts a USB mouse for use with the PC Engine
 *             For Raspberry Pi Pico or other RP2040 MCU
 *             I like the Adafruit QT Py RP2040 board at the moment
 *
 * This code is based on the TinyUSB Host HID example from pico-SDK v1.?.?
 *
 * Modifications for PCFXMouse
 * Copyright (c) 2021 David Shadoff
 *
 * ------------------------------------
 *
 * The MIT License (MIT)
 *
 * Original TinyUSB example
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "pcfxplex.pio.h"
#include "clock.pio.h"
#include "ws2812.pio.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+


// leave this uncommented if you want adjustable sensitivity form the scroll-wheel:
//
#define SENSITIVITY_SCROLL  true


#ifdef ADAFRUIT_KB2040           // if KB2040 board

#define CLKIN_PIN       28
#define LATCHIN_PIN     CLKIN_PIN + 1   // Note - in pins must be a consecutive 'in' group
#define DATAOUT_PIN     2               // Note - out pins must be a consecutive 'out' group

// Neopixel stuff
#define IS_RGBW         true
#define NUM_PIXELS      1

#define WS2812_PIN PICO_DEFAULT_WS2812_PIN

#endif

#ifndef ADAFRUIT_KB2040                           // else assume build for RP Pico board

#define CLKIN_PIN       16
#define LATCHIN_PIN     CLKIN_PIN + 1   // Note - in pins must be a consecutive 'in' group
#define DATAOUT_PIN     18              // Note - out pins must be a consecutive 'out' group

#endif


void led_blinking_task(void);

extern void cdc_task(void);
extern void hid_app_task(void);

int16_t  global_x = 0;
int16_t  global_y = 0;
uint8_t  global_buttons = 0xFF;

// When PCFX reads, set interlock to ensure atomic update
//
volatile bool  output_exclude = false;
volatile bool  scanned = false;


// output_word -> is the word sent to the state machine for output
//
// Structure of the word sent to the FIFO from the ARM:
// |00101111|111111bb|xxxxxxxx|yyyyyyyy| (PCFX mouse)
// |00001111|11111111|1m1mdddd|rsbbbbbb| (PCFX joypad)
//
// Where:
//  - 0 = must be zero
//  - 1 = must be one
// MOUSE:
//  - b = button values, arranged in right/left sequence for PC-FX use
//  - x = mouse 'x' movement; right is {1 - 0x7F} ; left is {0xFF - 0x80 }
//  - y = mouse 'y' movement; down  is {1 - 0x7F} ;  up  is {0xFF - 0x80 }
//
// JOYPAD:
//  - m = mode switch (mode 2 is more-significant bit)
//  - d = direction button (Left, Down, Right, Up in decreasing significant bit order)
//  - r = run button
//  - s = select button
//  - s = button (VI, V, IV, III, II, I  in decreasing significant bit order)
//
uint32_t output_word = 0;

int16_t  output_x = 0;
int16_t  output_y = 0;
uint8_t  output_buttons = 0xFF;

static absolute_time_t init_time;
static absolute_time_t current_time;
static const absolute_time_t reset_period = 7000;  // at 7000us (7ms), reset the scan exclude flag
absolute_time_t last_sens_time;

PIO pio, pioled;
uint sm1, sm2, smled;   // sm1 = plex; sm2 = clock

//
//// WS2812 "Neopixel" protocol transport
//
void __not_in_flash_func(put_pixel)(uint32_t pixel_grb) {
    pio_sm_put_blocking(pioled, smled, pixel_grb << 8u);
}

uint32_t __not_in_flash_func(urgb_u32)(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

/*------------- MAIN -------------*/

// note that "__not_in_flash_func" functions are loaded
// and "pinned" in SRAM - not paged in/out from XIP flash
//

void __not_in_flash_func(set_sens_led)(int level)
{
  switch(level) {
    case 0:
      put_pixel(urgb_u32(0, 7, 0));  // green
      break;
    case 1:
      put_pixel(urgb_u32(12, 12, 0));  // yellow
      break;
    case 2:
      put_pixel(urgb_u32(28, 2, 0));  // red
      break;
    default:
      put_pixel(urgb_u32(10, 10, 10));  // white
  }
}

//
// post_globals - accumulate the many intermediate mouse scans (~1ms)
//                into an accumulator which will be reported back to PCFX
//
void __not_in_flash_func(post_globals)(uint8_t buttons, uint8_t delta_x, uint8_t delta_y, int sensitivity_level)
{
static int prev_sens_level = 1;

  if (delta_x >= 128) 
    global_x = global_x - (256-delta_x);
  else
    global_x = global_x + delta_x;

  if (delta_y >= 128) 
    global_y = global_y - (256-delta_y);
  else
    global_y = global_y + delta_y;

  global_buttons = buttons;

  if (!output_exclude)
  {
     output_x = global_x;
     output_y = global_y;
     output_buttons = global_buttons;

     output_word = 0x2f000000 | ((output_buttons & 0xff) << 16) | (((~output_x>>1) & 0xff) << 8) | ((~output_y>>1) & 0xff);
  }

  // only update sensitivity LED if it has changed, and >10ms since the last update
  //
  if ((sensitivity_level != prev_sens_level) &&
      (absolute_time_diff_us(last_sens_time, get_absolute_time()) > 10000))
  {
     set_sens_led(sensitivity_level);
     prev_sens_level = sensitivity_level;
     last_sens_time = get_absolute_time();
  }
}


//
// post_to_output - push the current values into the state machine for
//                  quick-response expression onto the GPIOs
//
void __not_in_flash_func(post_to_output)(void)
{
  if (!output_exclude) {
     output_word = 0x2f000000 | ((output_buttons & 0xff) << 16) | (((~output_x>>1) & 0xff) << 8) | ((~output_y>>1) & 0xff);
     pio_sm_put(pio, sm1, output_word);
  }
}


//
// process_signals - inner-loop processing of events:
//                   - USB polling
//                   - event processing
//                   - detection of when a PCFX scan is no longer in process (reset period)
//
static void __not_in_flash_func(process_signals)(void)
{
  while (1)
  {
    // tinyusb host task
    tuh_task();
//#ifndef ADAFRUIT_QTPY_RP2040
//    led_blinking_task();
//#endif


//
// check time offset in order to detect when a PCFX scan is no longer
// in process (so that fresh values can be sent to the state machine)
//
    current_time = get_absolute_time();

    if (absolute_time_diff_us(init_time, current_time) > reset_period) {
      output_word = 0x2f000000 | ((output_buttons & 0xff) << 16) | (((~output_x>>1) & 0xff) << 8) | ((~output_y>>1) & 0xff);
      pio_sm_put(pio, sm1, output_word);

      if (scanned) {
         // decrement outputs from globals
         global_x = (global_x - output_x); 
         global_y = (global_y - output_y); 

         output_x = 0;
         output_y = 0;
         output_buttons = global_buttons;

         scanned = false;
      }

      output_exclude = false;
      init_time = current_time;
    }

#if CFG_TUH_HID
    hid_app_task();
#endif

    post_to_output();
  }
}

//
// core1_entry - inner-loop for the second core
//             - when the "LATCH" line is asserted, set lock flag
//               protecting the output state machine from inconsistent data
//
static void __not_in_flash_func(core1_entry)(void)
{
static bool rx_bit = 0;

  while (1)
  {
     // wait for (and sync with) posedge of LATCH signal; rx_data is throwaway
     rx_bit = pio_sm_get_blocking(pio, sm2);

     // PCFX scans 5 times in a row; only action the first one
     //
     if (output_exclude)
        continue;

     // Now we are in an update-sequence; set a lock
     // to prevent update during output transaction
     //
     // Note the reset to normal will happen as part of a
     // timed process on the second CPU & state machine
     //
     output_exclude = true;
     scanned = true;

     // assume data is already formatted in output_word and push it to the state machine
     pio_sm_put(pio, sm1, output_word);

     // renew countdown timeframe
     init_time = get_absolute_time();
  }
}

void ws2812_countdown()
{
int start = 20;
int count;

    for (count = start; count >= 0; count--) {
      put_pixel(urgb_u32(count, 0, 0));  // red
      sleep_ms(10);
    }
    sleep_ms(100);

    for (count = start; count >= 0; count--) {
      put_pixel(urgb_u32(0, count, 0));  // green
      sleep_ms(10);
    }
    sleep_ms(100);

    for (count = start; count >= 0; count--) {
      put_pixel(urgb_u32(0, 0, count));  // blue
      sleep_ms(10);
    }
    sleep_ms(100);

    put_pixel(urgb_u32(0, 0, 0));  // blue
    sleep_ms(1000);
}

int main(void)
{
  board_init();
  printf("TinyUSB Host HID Example\r\n");

  tusb_init();

  scanned = false;
  output_exclude = false;

  global_x = 0;
  global_y = 0;
  global_buttons = 0xff;

  output_x = 0;
  output_y = 0;
  output_buttons = 0xff;

  output_word = 0x2FFFFFFF;  // no buttons pushed, x=0, y=0

  init_time = get_absolute_time();

  // Both state machines can run on the same PIO processor
  pio = pio0;

////////////////////////
// Setup NeoPixel LED
//
  pioled = pio1;
  smled = 0;

  uint offset = pio_add_program(pioled, &ws2812_program);

  ws2812_program_init(pioled, smled, offset, WS2812_PIN, 800000, IS_RGBW);


  // Load the plex (multiplex output) program, and configure a free state machine
  // to run the program.

  uint offset1 = pio_add_program(pio, &pcfxplex_program);
  sm1 = pio_claim_unused_sm(pio, true);
  pcfxplex_program_init(pio, sm1, offset1, CLKIN_PIN, DATAOUT_PIN);


  // Load the clock (synchronizing input) program, and configure a free state machine
  // to run the program.

  uint offset2 = pio_add_program(pio, &clock_program);
  sm2 = pio_claim_unused_sm(pio, true);
  clock_program_init(pio, sm2, offset2, LATCHIN_PIN);

  multicore_launch_core1(core1_entry);

  sleep_ms(1000);
  ws2812_countdown();  // show "initialization flourish"
  set_sens_led(1);     // standard sensitivity
  last_sens_time = get_absolute_time();  // shouldn't update LED sooner than 10ms

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
