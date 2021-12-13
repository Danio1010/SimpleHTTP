CC = g++

# compiler flags:
CFLAGS  = -Wall -Wextra -std=c++17

# the build target executable:
TARGET = serwer

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).cpp -lstdc++fs -fsplit-stack

clean:
	$(RM) $(TARGET)
