# Compiler and commands
CC = gcc
RM = rm -f

FFMPEG_LIBS   = libavdevice libavformat libavfilter libavcodec libswresample libswscale libavutil
CFLAGS       += -Wall -g -MMD -MP $(shell pkg-config --cflags $(FFMPEG_LIBS)) -I$(PWD)/include
LDLIBS       += $(shell pkg-config --libs $(FFMPEG_LIBS))

SRC_DIR = src
OBJ_DIR = src
EXE_DIR = bin

EXE_NAMES = find_keyframes segment_transcode hm_transcode hm_probe
EXE = $(addprefix $(EXE_DIR)/, $(EXE_NAMES))
SRC = $(addprefix $(SRC_DIR)/, $(EXE_NAMES:=.c))
OBJ = $(addprefix $(SRC_DIR)/, $(EXE_NAMES:=.o))

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
