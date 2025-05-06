MEGA64

This is a *completely unnecessary and SLOW* Commodore 64 emulator for the MEGA65 computer.  Just built for fun.

Build with Calypsi

The D81 disk must contain:

kernal.bin
chargen.bin
basic.bin
sem

For some reason, I could not get Calypsi's fopen to load the ROM file data, so I have two BASIC boot programs that load the roms (and optional "sem" monitor to $c000).  see the included D81  for the necessary boot files.  RUN"BOOT" to load the standard emulator.  RUN"BOOT-MON" to run the emulator with the monitor loaded into $C000.  Also, I wrote this testing the ability to read and write from attic ram on the MEGA65, so the ROMs are in ATTIC RAM (of course this alone will have a performance impact).  The emulated RAM is in 0x10000.  I may try wiping out bank 2, and it for my loaded ROMs for speed improvements.

Notes on development:  Im using FAKE6502 - all credits to the original author.  I wrote this with an interest in seeing how fast a 40mhz machine using C could run emulation.  Well, as youll see... its slow.  *It takes about 35 seconds just to get to the boot screen (!)*  But its also not very optimized (yet). 

While we await a C128 or PLUS/4 core, it would be quite interesting if they could emulated at a decent speed.  Micheal Steil has already extracted the BASIC interpreters for the PET, VIC-20, C64, 128.  https://github.com/mist64/kernalemu  So who knows...

Also..I hate makefiles.  Just run the batch and send me a pull request with a better makefile :)