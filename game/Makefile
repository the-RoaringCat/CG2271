CC = gcc

RAYLIB_DIR = $(HOME)/raylib/src

CFLAGS = -Wall -Wextra -O2 \
         -I../http_server \
         -I$(RAYLIB_DIR) \
	 -std=gnu11

LIBS = $(RAYLIB_DIR)/libraylib.a -lm -lpthread -ldl -lrt -lX11

TARGET = game
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC) ../http_server/common.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
