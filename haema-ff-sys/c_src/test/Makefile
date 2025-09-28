# Compiler and commands
CC = gcc
RM = rm -f

FFMPEG_LIBS   = libavdevice libavformat libavfilter libavcodec libswresample libswscale libavutil
CFLAGS       += -Wall -g -MMD -MP $(shell pkg-config --cflags $(FFMPEG_LIBS)) -I$(PWD)../include
LDLIBS       += $(shell pkg-config --libs $(FFMPEG_LIBS))

EXE_NAMES = test_hm_transcode
EXE = $(EXE_NAMES)
SRC = $(EXE_NAMES:=.c)
OBJ = $(EXE_NAMES:=.o)

all: $(EXE)

$(EXE_DIR)/%: $(OBJ_DIR)/%.o
	@echo "LD $@"
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "CC $<"
	$(CC) $(CFLAGS) -c $< -o $@

DEPS = $(OBJ:.o=.d)
-include $(DEPS)

.PHONY: all clean

clean:
	@echo "Cleaning up..."
	$(RM) $(EXE) $(OBJ) $(DEPS)
	$(RM) out/*

