TARGET = vsfs-driver
CC = gcc
OBJ = vsfs-driver.o vsfs.o
FLAGS = -g

.PHONY: clean

all: clean $(TARGET)

%.o: %.c
	$(CC) $< -c -o $@ $(FLAGS)

$(TARGET): $(OBJ)
	$(CC) $^ -o $@
	
clean:
	rm -rf $(OBJ) $(TARGET)
