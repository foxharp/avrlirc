
# Makefile for avrlirc and avrlirc2udp
# Paul Fox, April 2007

# current code assumes ATTiny2313.  (it uses UART, 16 bit timer, and
# input capture event interrupt.)
MCU = attiny2313

# location of cross-compiler -- edit to suit
#CROSS = /opt/avr-gcc-070314/bin/avr-
CROSS = avr-

CC=$(CROSS)gcc
LD=$(CROSS)gcc
NM=$(CROSS)nm
SIZE=$(CROSS)size
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -c -Os -Wwrite-strings -Wall -mmcu=$(MCU)
CFLAGS += -Wa,-adhlns=$(<:%.c=%.lst)
LFLAGS = -mmcu=$(MCU)

HOSTCC = gcc
# HCFLAGS = -DPERSONAL_HACKS
HCFLAGS = -DVERSION="\"$(VERSION)\"" -DPERSONAL_HACKS

PROG = avrlirc
SRCS = avrlirc.c
OBJS = avrlirc.o

VERSION = $(shell date +%y%m%d-%H%M)
CFLAGS += -DAVRLIRC_VERSION="\"$(VERSION)\""


all: $(PROG).hex $(PROG).lss avrlirc2udp airboard-ir

$(PROG).out: $(OBJS)
	@-test -f $(PROG).out && (echo size was: ; $(SIZE) $(PROG).out)
	$(LD) -o $@ $(LFLAGS) $(OBJS)
	$(NM) -n $@ >$(PROG).map
	@echo size is:
	@$(SIZE) $(PROG).out

$(PROG).hex: $(PROG).out
	$(OBJCOPY) -R .eeprom -O ihex $^ $@

# Create extended listing file from ELF output file.
%.lss: %.out
	$(OBJDUMP) -h -S $< > $@


sizes: $(OBJS)
	@echo
	@echo Individual:
	$(SIZE) -t $(OBJS)
	@echo
	@echo Complete:
	$(SIZE) $(PROG).out

avrlirc2udp: avrlirc2udp.c
	$(HOSTCC) $(HCFLAGS) -Wall avrlirc2udp.c -o avrlirc2udp

airboard-ir:	airboard-ir.c
	$(HOSTCC) $(HCFLAGS) -O2 -Wall airboard-ir.c -o airboard-ir

# convenience target for upgrading on multiple machines
install-airboard-ir: $(PROG) ab-installscript
	for h in kousa moss phlox lily pansy;\
	do \
	    echo $$h ;\
	    scp $(PROG) ab-installscript $$h:/tmp ;\
	    ssh -t olpc@$$h sudo sh /tmp/ab-installscript ;\
	done
	for h in jade ivy ;\
	do \
	    echo $$h ;\
	    scp $(PROG) ab-installscript $$h:/tmp ;\
	    ssh -t pgf@$$h sudo sh /tmp/ab-installscript ;\
	done

ab-installscript:
	echo 'set -x; cd /usr/local/bin; mv $(PROG) $(PROG).old; mv /tmp/$(PROG) . ; killall $(PROG)' >ab-installscript
	chmod a+x ab-installscript

 
tarball: all clean
	mkdir -p oldfiles
	mv $(PROG)-*.hex *.tar.gz oldfiles || true
	mv $(PROG).hex $(PROG)-$(VERSION).hex || true
	ln -s avrlirc ../avrlirc-$(VERSION)
	tar -C .. --dereference \
	    --exclude CVS \
	    --exclude oldfiles \
	    --exclude web \
	    --exclude '*.tar.gz' \
	    -czf ../avrlirc-$(VERSION).tar.gz avrlirc-$(VERSION)
	mv ../avrlirc-$(VERSION).tar.gz .
	rm -f ../avrlirc-$(VERSION)

program:
	sudo avrdude -p t2313 -c sp12 -U avrlirc.hex  -E noreset

bod_fuses:  # brown-out detection
	sudo avrdude -p t2313 -c sp12 -U lfuse:w:0x64:m -U hfuse:w:0xd7:m -E noreset

clean:
	rm -f *.o *.flash *.flash.* *.out *.map *.lst *.lss
	rm -f avrlirc2udp airboard-ir ab-installscript
	
clobber: clean
	rm -f avrlirc.hex

