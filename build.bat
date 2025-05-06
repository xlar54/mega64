cls
del *.o
del *.prg
del *.lst
cc6502 -O2 --target=mega65 --list-file m65.txt ./src/m65.c -o m65.o
cc6502 -O2 --target=mega65 --list-file emu.txt ./src/emu.c -o emu.o
ln6502 --target=mega65 --core=45gs02 --cstack-size=0x800 --heap-size=4000  --output-format=prg mega65-6502emu.scm m65.o emu.o --list-file emu.lst -o emu.prg
