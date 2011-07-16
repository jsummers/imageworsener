
# Makefile for ImageWorsener.

ifeq ($(origin OS),undefined)
OS:=unknown
endif

ifeq ($(origin IW_SUPPORT_PNG),undefined)
IW_SUPPORT_PNG:=1
endif
ifeq ($(origin IW_SUPPORT_JPEG),undefined)
IW_SUPPORT_JPEG:=1
endif
ifeq ($(origin IW_SUPPORT_WEBP),undefined)
IW_SUPPORT_WEBP:=1
endif

SRCDIR:=src
OUTDIR:=.

CC:=gcc
CFLAGS:=-Wall -O2
LDFLAGS:=
INCLUDES:=-I$(SRCDIR)
LIBS:=

ifeq ($(IW_SUPPORT_WEBP),1)
LIBS+=-lwebp
else
CFLAGS+=-DIW_SUPPORT_WEBP=0
endif

ifeq ($(IW_SUPPORT_PNG),1)
LIBS+=-lpng -lz
else
CFLAGS+=-DIW_SUPPORT_PNG=0
endif

ifeq ($(IW_SUPPORT_JPEG),1)
LIBS+=-ljpeg
else
CFLAGS+=-DIW_SUPPORT_JPEG=0
endif

LIBS+=-lm

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
 imagew-tiff.o imagew-miff.o imagew-webp.o imagew-gif.o)
ALLOBJS:=$(COREIWLIBOBJS) $(AUXIWLIBOBJS) $(OUTDIR)/imagew-cmd.o

$(TARGET): $(OUTDIR)/imagew-cmd.o $(IWLIBFILE)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(IWLIBFILE): $(COREIWLIBOBJS) $(AUXIWLIBOBJS)
	ar rcs $@ $^

$(COREIWLIBOBJS): $(addprefix $(SRCDIR)/,imagew-config.h imagew-internals.h \
 imagew.h)
$(AUXIWLIBOBJS): $(addprefix $(SRCDIR)/,imagew-config.h imagew.h)
$(OUTDIR)/imagew-cmd.o: $(addprefix $(SRCDIR)/,imagew-config.h imagew.h)

$(ALLOBJS): $(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OUTDIR)/*.o $(IWLIBFILE)

