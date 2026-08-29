CC = gcc
CFLAGS = -Wall -Wextra -g $(shell pkg-config --cflags gtk+-3.0 vte-2.91)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0 vte-2.91) -lcurl -lpthread

SRCS = main.c editor.c explorer.c terminal.c ai_client.c ai_panel.c ui_lang.c theme.c recent.c
OBJS = $(SRCS:.c=.o)
TARGET = miniide_c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
