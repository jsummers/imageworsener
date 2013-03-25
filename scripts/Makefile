
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
IW_SUPPORT_WEBP:=0
endif

SRCDIR:=../src
INTDIR:=../src
OUTLIBDIR:=../src
OUTEXEDIR:=..

CC:=gcc
CFLAGS:=-Wall -Wformat-security -O3
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
TARGET:=$(OUTEXEDIR)/imagew.exe
else
TARGET:=$(OUTEXEDIR)/imagew
endif

all: $(TARGET)

.PHONY: all clean

IWLIBFILE:=$(OUTLIBDIR)/libimageworsener.a
COREIWLIBOBJS:=$(addprefix $(INTDIR)/,imagew-main.o imagew-resize.o \
 imagew-opt.o imagew-util.o imagew-api.o)
AUXIWLIBOBJS:=$(addprefix $(INTDIR)/,imagew-png.o imagew-jpeg.o imagew-bmp.o \
 imagew-tiff.o imagew-miff.o imagew-webp.o imagew-gif.o imagew-pnm.o \
 imagew-zlib.o imagew-allfmts.o)
ALLOBJS:=$(COREIWLIBOBJS) $(AUXIWLIBOBJS) $(INTDIR)/imagew-cmd.o

$(TARGET): $(INTDIR)/imagew-cmd.o $(IWLIBFILE)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(IWLIBFILE): $(COREIWLIBOBJS) $(AUXIWLIBOBJS)
	ar rcs $@ $^

$(COREIWLIBOBJS): $(addprefix $(SRCDIR)/,imagew-config.h imagew-internals.h \
 imagew.h)
$(AUXIWLIBOBJS): $(addprefix $(SRCDIR)/,imagew-config.h imagew.h)
$(INTDIR)/imagew-cmd.o: $(addprefix $(SRCDIR)/,imagew-config.h imagew.h)

$(ALLOBJS): $(INTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(TARGET) $(INTDIR)/*.o $(IWLIBFILE)

