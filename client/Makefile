EXECUTABLE=dsra
all: $(EXECUTABLE) 

include ../common/Makefile.common

LD_FLAGS = $(PALDFLAGS) $(LIBPORTAUDIO) -lpthread
CC = gcc
CFLAGS = -W -Wall -Wno-unused-parameter -O2 -I../portaudio/include
INSTALL_PATH = /usr/local/bin/
SOURCES=main.c
OBJECTS=$(SOURCES:.c=.o)

$(EXECUTABLE): $(SOURCES) $(LIBPORTAUDIO)
	$(CC) $(CFLAGS) $(LD_FLAGS) $(SOURCES) -o $@

clean:
	rm -f $(EXECUTABLE)

install: all
	mkdir -p $(INSTALL_PATH)
	cp $(EXECUTABLE) $(INSTALL_PATH)
