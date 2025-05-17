MEGA64

This is a *completely unnecessary and SLOW* Commodore 64 emulator for the MEGA65 computer.  Just built for fun.

Build with Calypsi

The D81 disk must contain:

kernal.bin
chargen.bin
basic.bin
sem

For some reason, I could not get Calypsi's fopen to load the ROM file data, so I have two BASIC boot programs that load the roms (and optional "sem" monitor to $c000).  see the included D81  for the necessary boot files.  RUN"BOOT" to load the standard emulator.  RUN"BOOT-MON" to run the emulator with the monitor loaded into $C000.

Notes on development:  Im using FAKE6502 - all credits to the original author.  I wrote this with an interest in seeing how fast a 40mhz machine using C could run emulation.  Well, as youll see... its slow. 

Also..I hate makefiles.  Just run the batch and send me a pull request with a better makefile :)