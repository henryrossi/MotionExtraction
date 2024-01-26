CC = gcc
CFLAGS  = -g -Wall
LFLAGS = -L/usr/local/Cellar/ffmpeg/6.0_2/lib
LIBS =  -lavformat -lavcodec

main: main.o
	$(CC) $(CFLAGS) -o MotionExtraction main.o $(LFLAGS) $(LIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	$(RM) MotionExtraction *.o *~