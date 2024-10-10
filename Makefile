CC = gcc
CFLAGS = -Wall -Wextra -I src

SRCS = src/server.c src/commands/commands.c
OBJS = $(SRCS:.c=.o)
TARGET = server

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
