#From http://groups.google.com/group/openni-dev/browse_thread/thread/d3ee09b0b78b8df6

CC=g++

OTHERS      = -lcurl -lcv -lhighgui -lglut -lXnVNite -lOpenNI -lp3framework -lpanda   \
     -lpandafx -lpandaexpress -lp3dtoolconfig -lp3dtool -lp3pystub -lp3direct

LIBNAME     = $(OTHERS)

LIBRARYPATH = -L/usr/lib/panda3d/
              
INCLUDEPATH = -I/usr/include/curl/                      \
              -I/usr/include/opencv/                    \
              -I/usr/include/nite/                      \
              -I/usr/include/ni/                        \
              -I/usr/include/panda3d/                   \
              -I/usr/include/python2.6/

# DEBUG
#CFLAGS      = $(LIBPATH) -ggdb -o0 $(INCLUDEPATH)

# RELEASE
CFLAGS      = $(LIBPATH) -o3 $(INCLUDEPATH)

SRCS        = src/SendCharacter.c $(wildcard src/*.cpp)

OBJS        = $(SRCS:.cpp=.o)

EXE         = build/antfarm

all: $(SRCS) $(EXE)
	# rm -f $(OBJS)

$(EXE): $(OBJS)
	$(CC) $(CFLAGS) $(LIBRARYPATH) $(OBJS) $(LIBNAME) -o $@

.cpp.o:
	$(CC) -static $(CFLAGS) $(LIBNAME) -c $< -o $@

clean:
	rm -f src/*.o
	rm -f $(EXE)

redo:
	make clean
	make all

