CC := gcc
CFLAGS := -Wall -Wextra -std=c11 
TARGET := /tmp/codecrafters-build-redis-c

# Find all source files
SRCS := $(shell find src -name '*.c')

# Generate object file names
OBJS := $(SRCS:.c=.o)

# Find all include directories
INCLUDE_DIRS := $(shell find src -type d)
CFLAGS += $(addprefix -I,$(INCLUDE_DIRS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
