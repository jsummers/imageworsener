
# Makefile for ImageWorsener.

ifeq ($(origin OS),undefined)
OS:=unknown
endif

SRCDIR:=src
OUTDIR:=.

CC:=gcc
CFLAGS:=-Wall -O2
LDFLAGS:=
INCLUDES:=-I$(SRCDIR)

ifeq ($(OS),Windows_NT)
TARGET:=$(OUTDIR)/imagew.exe
else
TARGET:=$(OUTDIR)/imagew
endif

all: $(TARGET)

.PHONY: all clean

IWLIBFILE:=$(OUTDIR)/libimageworsener.a
COREIWLIBOBJS:=$(addprefix $(OUTDIR)/,imagew-main.o imagew-resize.o \
 imagew-opt.o imagew-util.o imagew-api.o)
AUXIWLIBOBJS:=$(addprefix $(OUTDIR)/,imagew-png.o imagew-jpeg.o imagew-bmp.o \
 imagew-tiff.o)
ALLOBJS:=$(COREIWLIBOBJS) $(AUXIWLIBOBJS) $(OUTDIR)/imagew-cmd.o

$(TARGET): $(OUTDIR)/imagew-cmd.o $(IWLIBFILE)
	$(CC) $(LDFLAGS) -o $@ $^ -ljpeg -lpng -lz -lm

$(IWLIBFILE): $(COREIWLIBOBJS) $(AUXIWLIBOBJS)
	ar rcs $@ $^

$(COREIWLIBOBJS): $(addprefix $(SRCDIR)/,imagew-config.h imagew-internals.h \
 imagew.h)
$(AUXIWLIBOBJS): $(addprefix $(SRCDIR)/,imagew-config.h imagew.h)
$(OUTDIR)/imagew-cmd.o: $(addprefix $(SRCDIR)/,imagew-config.h imagew.h)

$(ALLOBJS): $(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(TARGET) $(ALLOBJS) $(IWLIBFILE)

