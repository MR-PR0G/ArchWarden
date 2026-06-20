CC = gcc
CFLAGS = -Wall -O2 `pkg-config --cflags gtk+-3.0 glib-2.0`
LIBS = `pkg-config --libs gtk+-3.0 glib-2.0` -lpthread

TARGET = ArchWarden
SRCS = main.c warden.c backend.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

%.o: %.c archwarden.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)