# Makefile for hungarian external

# Platform detection
UNAME := $(shell uname -s)

# Common settings
EXTERNALS = orbifold voice_leading

# PD include path (adjust as needed)
PD_INCLUDE = /usr/include/pd
# Or on Mac: PD_INCLUDE = /Applications/Pd.app/Contents/Resources/include/pd

# Platform-specific settings
ifeq ($(UNAME), Darwin)
    EXTENSION = pd_darwin
    CFLAGS = -DPD -O3 -funroll-loops -fomit-frame-pointer -Wall -W -Wshadow \
             -Wstrict-prototypes -Wno-unused -Wno-parentheses -Wno-switch
    LDFLAGS = -bundle -undefined suppress -flat_namespace
endif

ifeq ($(UNAME), Linux)
    EXTENSION = pd_linux
    CFLAGS = -DPD -O3 -funroll-loops -fomit-frame-pointer -Wall -W -Wshadow \
             -Wstrict-prototypes -Wno-unused -Wno-parentheses -Wno-switch -fPIC
    LDFLAGS = -shared
endif

# Build targets
all: $(EXTERNALS:%=%.$(EXTENSION))

%.$(EXTENSION): %.c
	gcc $(CFLAGS) -I$(PD_INCLUDE) -o $@ $< $(LDFLAGS)

clean:
	rm -f *.pd_* *.o

install: $(EXTERNALS:%=%.$(EXTENSION))
	mkdir -p ~/pd-externals/
	cp $^ ~/pd-externals/