
clean: local_clean sub_clean

local_clean:
	rm -f *core *[%~] *.[oa] *.map
	rm -f .[a-z]*~
	rm -f smos6502$(EXEEXT) smos6502.exe
	rm -f smcs6502$(EXEEXT) smcs6502.exe
	rm -f ucsim_mos6502$(EXEEXT) ucsim_mos6502.exe
	rm -f ucsim_mcs6502$(EXEEXT) ucsim_mcs6502.exe

sub_clean:
ifneq ($(shell test -f test/Makefile && echo ok), )
	$(MAKE) -C test -f clean.mk clean
endif

distclean: local_distclean sub_distclean

local_distclean: local_clean
	rm -f config.cache config.log config.status
	rm -f Makefile *.dep
	rm -f *.obj *.list *.lst *.hex
	rm -f test/Makefile

sub_distclean:
