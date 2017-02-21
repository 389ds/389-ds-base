# THIS IS A GENERATED MAKEFILE. PLEASE SEE m4/lfds.m4
CFLAGS = -O0 -Wall -Wextra -Wunused -fno-omit-frame-pointer -Wstrict-overflow -fno-strict-aliasing -fsanitize=address -lasan $(AM_CFLAGS) -fPIC -Wno-unknown-pragmas
lfds_make_target = ar_dbg
lfds_path = ./
.PHONY: dvi pdf ps info html check
dvi pdf ps info html check:
# This won't work on FreeBSD, because it requires gmake, not make.
install all:
	mkdir -p $(lfds_path)/bin/
	mkdir -p $(lfds_path)/obj/
	CFLAGS="$(CFLAGS)" make -C $(lfds_path)/build/gcc_gnumake $(lfds_make_target)

clean:
	make -C $(lfds_path)/build/gcc_gnumake clean

