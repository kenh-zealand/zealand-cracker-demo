CC      = C:/msys64/mingw64/bin/gcc.exe
CFLAGS  = -O2 -Wall -Wextra \
          -IC:/msys64/mingw64/include/SDL2 \
          -mwindows
LDFLAGS = -LC:/msys64/mingw64/lib \
          -lSDL2 -lSDL2main -lm

TARGET  = zealand-demo.exe
SRCS    = main.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) font.h mod_player.h
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	@echo ""
	@echo "  Build complete: $(TARGET)"
	@echo "  Run with: make run"

run: $(TARGET)
	./$(TARGET)

clean:
	del /Q $(TARGET) 2>NUL || rm -f $(TARGET)
