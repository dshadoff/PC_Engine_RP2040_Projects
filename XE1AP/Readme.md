# PC_Engine_RP2040_Projects - XE-1AP Analog Joystick Converter

## Overview

This project is intended to allow use of a standard USB-based PS4 gamepad on a PC Engine as a XE-1AP analog joypad.
While the code and board are in working condition, there is likely still room for improvement, as I feel that
analog positioning may not be as smooth as it could be.


## PC Board and Assembly

Please see the PC Board used in the "PCEMouse" project in this repository (KB2040 board version).
This uses the same board (just different firmware).

I also intend to try to make a board which could work with the Megadrive at some point.


## Source Code

### Compilation

This was updated to use Pico-SDK version 1.4.0 .

pico_sdk_import.cmake is from the SDK, but is required by CMake (and thus replicated here)

This is based on the TinyUSB Host HID controller example, and since this often changes (required by refactoring of
the Host HID code), the initial commit baseline is the source code of that example.

To build the source, first ensure that you have the right version of the RaspberryPi/piso-sdk installed.
As this board targets the Adafruit KB2040 board, you should run the build_ada_kb2040.sh script (under UNIX).
Then, "cd build" and "make".

I have also included a release version of the program as a uf2 file in the releases/ folder; just drag and drop it
onto the virtual drive presented when putting the board into BOOTSEL mode (holding the 'boot' button, connect the
board by USB to a host computer, and release the button; a new drive should appear on the computer).


### Theory of Operation

At a high level, this is a multi-processor system, withe the division of work as follows:
- CPU0 : perform USB scanning, analyze controller button statuses and joysticks' X/Y offsets, and keep the data up-to-date in a temporary storage area.
- CPU1 : watch PIO State Machine #1 for the signal identifying start of scan, set locking to prevent mid-scan updates, and push the series of last-known-good data to state machine #2.
- PIO State Machine #1 : Monitor host electrical signals, blocking thread #1 until triggereed, and signalling state machine #2 to start its
protocol via an IRQ signal.
- PIO State Machine #2 : Wait for IRQ signal from state machine #1, and then send the data out on the GPIOs according to the standard timing (get the data from the TX FIFO as sent by CPU1).
- PIO State Machine #3 : Watch the SEL line identifying which bits to multiplex, and send the correct bits out through the data outputs
(note: the XE3-HE did not multiplex these in the same sequence as the Megadrive, so some bit-rearranging takes place here).

#### XE-1AP Protocol:

The PC Engine triggers a start-of-read trigger by flipping the CLR line low briefly, and returning it to HIGH.  Note that this is
not normal behaviour, and the XE-1AP is NOT compatible with the multi-tap. Immediately following the rise of the CLR line, the
joystick values are latched. During this time, SEL is held LOW as the PC Engine is expected to watch for state transitions on the
buttons 'I' and 'II', which are on D0 and D1 respectively.  D0 is LOW and D1 is HIGH during the waiting period.
After roughly 68.4 microseconds, data is ready, which is signalled by D1 (button 'II') transitioning low.

When the console sees this, its duty is to transition SEL to HIGH, in order to read the four data bits on D0-D3
(which normally correspond to UP/DOWN/LEFT/RIGHT), which in this situation correspond to the first nybble of data
from the joystick.  Then, SEL needs to transition back to LOW in order to monitor D0 and D1 again.  The window of data
availability (when button 'II' stays LOW) is fairly short, roughly 12.1 microseconds, requiring a tight loop on the
host console.

Before the next nybble is made available, button 'II' transitions HIGH for roughly 3.8 microseconds while data lines
transition.  Button 'I' transitions HIGH, representing the second nybble - and button 'II' transitions LOW again (for
12.1 microseconds again), signalling data ready, and the console needs to fetch the nybble by toggling SEL and reading
the lines during the availability window.

When button 'II' toggles high again, this time it stays high for longer (to prepare for the next 2 nybbles), at
21.8 microseconds.  The pattern repeats, with 6 bytes (12 nybbles) in all being transferred; the entire time from the
initial pulse to the end of the data transfer is roughly 350 microseconds.

Note that timing on this implementation is rounded to the next higher number of microseconds in most situations, with the difference
being removed from the 'dead' times between the second half of one byte, and the first half of the next byte being sent.

#### Data sequence output:

The sequence of data (and bit-order) is as follows (from original joystick):

All values are low when pressed, high when not pressed

 1. Buttons A, B, C, D - (Note: A is pressed if either A or A' is pressed; same with B or B')
 2. Buttons E1, E2, Start(F), Select (G)
 3. Top 4 bits of 'channel 0' (Y-axis;   limit up   = 0x00, limit down  = 0xFF)
 4. Top 4 bits of 'channel 1' (X-axis;   limit left = 0x00, limit right = 0xFF)
 5. Top 4 bits of 'channel 2' (Throttle; limit up   = 0xFF, limit down  = 0x00)
 6. 0000 (unused)
 7. Bottom 4 bits of 'channel 0' (Y-axis)
 8. Bottom 4 bits of 'channel 1' (X-axis)
 9. Bottom 4 bits of 'channel 2' (Throttle)
10. 0000 (unused)
11. Buttons A, B, A', B' (This can differentiate between the buttons, whereas scan #1 merges them)
12. 1111 (all high)


#### Logic state diagram:

![/../PCE_Controller_Info/blob/main/images/XHE-3_protocol.png](/../PCE_Controller_Info/blob/main/images/XHE-3_protocol.png)



**Compatibility:** Games which are not specifically written to support the X-HE3/XE-1AP in analog format will not work properly with this code.

Compatible games include:
 - AfterBurner II
 - Forgotten Worlds
 - Operation Wolf
 - Outrun

