CC = gcc
CFLAGS  = -g -Wall
LFLAGS = -L/usr/local/Cellar/ffmpeg/6.0_2/lib
LIBS =  -lavformat -lavcodec

main: main.o
	$(CC) $(CFLAGS) -o main main.o $(LFLAGS) $(LIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

# main.o: main.c  test.h
# 	$(CC) $(CFLAGS) -c main.c

# test.o: test.c test.h
# 	$(CC) $(CFLAGS) -c test.c

clean:
	$(RM) main *.o *~