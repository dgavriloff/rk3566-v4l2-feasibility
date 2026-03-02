CC      ?= gcc
CFLAGS  := -Wall -Wextra -Werror -g -O1 \
           -Wno-unused-parameter -Wno-unused-function \
           -Wno-sign-compare \
           -I.

TARGET  := sim_test
SRCS    := sim_hw.c sim_test.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

sim_hw.o: sim_hw.c kstubs.h
	$(CC) $(CFLAGS) -c -o $@ $<

sim_test.o: sim_test.c kstubs.h ../driver/rkvenc_regs.h ../driver/rkvenc_h264.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
