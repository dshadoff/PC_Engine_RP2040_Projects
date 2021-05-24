# PC_Engine_RP2040_Projects - Projects designed for PC Engine, using RPi Pico or other RP2040 hardware

## Memory Base 128 reimplementation

This is the third implementation of the Memory Base 128 I have written for modern hardware, and the easiest code to read.
The PIOs areused for edge-sensing of the data input, and the ARM core is used for processing.

The data is loaded into SRAM at startup, and saved into Flash after transactions take place.  This may not be ideal, as the
device is not capable of processing additional commands while the Flash flush is in progress (it takes place 0.75 seconds
after the last read/write of a group of read or write transactions).

## Mouse

This project is to allow modern mouse hardware to interface with a PC Engine.
The PIOs present the approrpaite data to the PC Engine joypad port based on signalling from teh PC Engine, and the RP2040
device is used for USB Host HID interfacing to a mouse.
Notes:
- Wired mice are recognized, but wireless mice aren't due to current limitations in the TinyUSB stack within the Pico SDK.
This is expected to be corrected in the near future.
- Some wired mice work better than others; this is thought to be due to the way data is sent via signal buffers on USB; this
is expected to be corrected in the immediate future.

## Notes
I have tried to use the Adafruit QtPy RP2040 as much as possible, as it is a compact form factor which is easy to design around.

Unfortunately, the Adafruit site tries todirect all users toward their verison of CircuitPython rather than the Pi SDK, and
as a result, Pinout pages of the "Pinout" page for their RP2040 devices don't include references to the GPIO numbers.

Therefore, I am including a graphic here:

![Adafruit QtPy RP2040 GPIO pinout](img/qtpy_rp2040_GPIO.png)
