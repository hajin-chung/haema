CC = gcc
RM = rm

# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=    libavdevice                        \
                libavformat                        \
                libavfilter                        \
                libavcodec                         \
                libswresample                      \
                libswscale                         \
                libavutil                          \

CFLAGS += -Wall -g
CFLAGS := $(shell pkg-config --cflags $(FFMPEG_LIBS)) $(CFLAGS)
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS)) $(LDLIBS)

EXE = find_keyframes segment_transcode
SRC = $(EXE:=.c)
OBJS = $(SRC:.c=.o)
DEPS = $(SRC:.c=.d)

.phony: all clean

all: $(OBJS) $(EXE)

# $(EXE): $(OBJ)
# 	$(CC) $^ $(LDLIBS) -o $@
#
# %.o: %.c
# 	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(EXE)

