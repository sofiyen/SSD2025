CC = gcc
CFLAGS = -Wall -O2
OBJS = userfaultfd.o pt_cache.o
TARGET = userfault

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
