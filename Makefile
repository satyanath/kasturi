# Makefile - builds the Kasturi compiler and runtime with gcc or clang
#
#   make                 build bin/kasturi and lib/libkasturi_rt.a
#   make test            run the test suite
#   make install PREFIX=/usr/local
#   make clean

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter
AR      ?= ar
PREFIX  ?= /usr/local

ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/obj/%.o,$(SRC))
RT_SRC  := $(wildcard runtime/*.c)
RT_OBJ  := $(patsubst runtime/%.c,build/rt/%.o,$(RT_SRC))

COMPILER := bin/kasturi$(EXE)
RTLIB    := lib/libkasturi_rt.a

.PHONY: all test clean install

all: $(COMPILER) $(RTLIB)

$(COMPILER): $(OBJ) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJ)

build/obj/%.o: src/%.c $(wildcard src/*.h) | build/obj
	$(CC) $(CFLAGS) -c $< -o $@

$(RTLIB): $(RT_OBJ) | lib
	rm -f $@
	$(AR) rcs $@ $(RT_OBJ)

build/rt/%.o: runtime/%.c $(wildcard runtime/*.h) | build/rt
	$(CC) $(CFLAGS) -c $< -o $@

bin build/obj build/rt lib:
	mkdir -p $@

test: all
	sh tests/run_tests.sh

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/lib/kasturi/bin $(DESTDIR)$(PREFIX)/lib/kasturi/lib $(DESTDIR)$(PREFIX)/lib/kasturi/runtime $(DESTDIR)$(PREFIX)/bin
	cp $(COMPILER) $(DESTDIR)$(PREFIX)/lib/kasturi/bin/
	cp lib/*.kst $(RTLIB) $(DESTDIR)$(PREFIX)/lib/kasturi/lib/
	cp runtime/*.c runtime/*.h $(DESTDIR)$(PREFIX)/lib/kasturi/runtime/
	ln -sf $(PREFIX)/lib/kasturi/bin/kasturi$(EXE) $(DESTDIR)$(PREFIX)/bin/kasturi$(EXE)

clean:
	rm -rf build $(COMPILER) $(RTLIB)
