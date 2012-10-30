TARGET = retrolaunch

CFLAGS=-std=c99 -Wall -pedantic -g

all: $(TARGET)

OBJ = main.o      \
      sha1.o      \
      parser.o    \
      cd_detect.o \
      $(NULL)

%.o: %.c
	$(CC) $< -c -o $@

clean:
	rm -f *.o
	rm -f $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(DEFINES) -o $@ $(OBJ)
