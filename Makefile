# soundwave - generador de videos verticales para estados de WhatsApp
#
#   make            compila el binario
#   make deps       descarga y compila las dependencias externas
#   make demo       ejemplo con un tono generado
#   make clean

CC      ?= cc
CXX     ?= c++

VENDOR  := third_party/vendor
BUILD   := third_party/build

CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
           -D_FILE_OFFSET_BITS=64 -MMD -MP \
           -I$(VENDOR)/dr_libs -I$(VENDOR)/stb -I$(VENDOR)/minimp4 \
           -I$(BUILD)/include

CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=gnu++14 -Wall -Wextra -Wno-unused-parameter \
            -D_FILE_OFFSET_BITS=64 -MMD -MP \
            -I$(VENDOR)/dr_libs -I$(VENDOR)/stb -I$(VENDOR)/minimp4 \
            -I$(BUILD)/include

LDFLAGS += -L$(BUILD)/lib -Wl,-rpath,'$$ORIGIN/$(BUILD)/lib'
LDLIBS  += -lfdk-aac -lopenh264 -lstdc++ -lpthread -lm

BIN     := soundwave

SRC := src/main.c src/audio.c src/envelope.c src/spectrum.c src/image.c \
       src/draw.c src/scene.c src/audio_enc.c src/mux.c

CXXSRC := src/video_enc.cpp

OBJ := $(SRC:.c=.o) $(CXXSRC:.cpp=.o)

PREFIX ?= /usr/local

.PHONY: all clean distclean deps headers install demo release

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---------------------------------------------------------------- deps
headers:
	sh tools/fetch_deps.sh

deps: headers
	sh tools/build_deps.sh all

# ---------------------------------------------------------------- extras
demo: $(BIN)
	sh tools/demo.sh

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

release:
	$(MAKE) clean CFLAGS="-O3 -march=native -std=gnu11 -Wall -Wextra -Wno-unused-parameter -D_FILE_OFFSET_BITS=64 -MMD -MP -I$(VENDOR)/dr_libs -I$(VENDOR)/stb -I$(VENDOR)/minimp4 -I$(BUILD)/include" CXXFLAGS="-O3 -march=native -std=gnu++14 -Wall -Wextra -Wno-unused-parameter -D_FILE_OFFSET_BITS=64 -MMD -MP -I$(VENDOR)/dr_libs -I$(VENDOR)/stb -I$(VENDOR)/minimp4 -I$(BUILD)/include" all
	strip $(BIN)

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(BIN)

distclean: clean
	rm -rf third_party/vendor third_party/src third_party/build

-include $(OBJ:.o=.d)
