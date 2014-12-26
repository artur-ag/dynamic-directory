# Projecto de Redes de Computadores e Internet, Marco de 2014
#
# Artur Goncalves, 69271
# Grupo 2
#
# Executable is built on the same folder as the source code.
# Obj files are created inside the ./obj folder.
#

CC=gcc
CFLAGS=-c -Wall
SOURCES=$(wildcard *.c)
OBJECTS=$(SOURCES:%.c=obj/%.o)
EXECUTABLE=dd

all: preamble $(SOURCES) $(EXECUTABLE)
	
preamble:
	mkdir -p obj
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@

obj/%.o: %.c
	$(CC) -c -o $@ $^
	
clean:
	rm obj/*.o
