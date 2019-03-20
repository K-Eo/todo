CC = gcc

FLAGS					= -std=c99 -Iinclude
CFLAGS				= -pedantic -Wall -Wextra -march=native -ggdb3
DEBUGFLAGS 		= -O0 -D _DEBUG
RELEASEFLAGS	= -O2 -D NDEBUG

TARGET 	= todo
SOURCES = $(shell echo src/*.c)
HEADERS = $(shell echo include/*.h)
OBJECTS = $(SOURCES:.c=.o)

PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) -o $(TARGET) $(OBJECTS)

release: $(SOURCES)
	$(CC) $(FLAGS) $(CFLAGS) $(RELEASEFLAGS) -o $(TARGET) $(SOURCES)

install: release
	install -D $(TARGET) $(BINDIR)/$(TARGET)

install-strip: release
	install -D -s $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	-rm $(BINDIR)/$(TARGET)

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)

%.o: %.c $(HEADERS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) -c -o $@ $<
