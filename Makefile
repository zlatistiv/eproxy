CC = gcc

CFLAGS_COMMON = -MMD -MP
CFLAGS_RELEASE = -O2
CFLAGS_DEBUG = -g -O0

TARGET = eproxy

SRCS = eproxy.c config.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_RELEASE)

all: $(TARGET)

debug: CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_DEBUG)
debug: clean $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(TARGET) $(OBJS) $(DEPS)
