CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lpthread
OBJS = userfaultfd.o pt_cache.o
TARGET = userfault

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
