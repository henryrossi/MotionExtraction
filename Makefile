
# FOR HANDLING FFMPEG LIBRARY
# The classical approach, on linux and variants at least, 
# is to just declare your dependencies and let the user decide 
# how to supply them, i.e. from the system package manager, 
# a third-party repository or a custom version compiled from 
# source. Then use a tool (cmake/autoconf) to verify that the 
# dependencies are satisfied when building.

# This puts a slightly higher burden on the user, but on 
# average I've had less problems with this than with libraries 
# who bundle all third-party dependencies.

CC = gcc
CFLAGS  = -g -Wall -Wextra
# LFLAGS = -L/usr/local/Cellar/ffmpeg/6.0_2/lib
LIBS =  -lavformat -lavcodec -lavutil

main: src/*.c src/*.h
	$(CC) $(CFLAGS) -o MotionExtraction src/*.c src/*.h  $(LIBS)

clean:
	$(RM) MotionExtraction *.o *~