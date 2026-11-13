SRC=$(wildcard *.c)
APPS=$(SRC:%.c=%)
SNDLIBS=$(LIBS) $(shell pkgconf --cflags --libs sndfile) -lm

.PHONY: default clean

default: $(APPS)

%: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SNDLIBS)

clean:
	rm -f $(APPS)
