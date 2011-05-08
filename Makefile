#From http://groups.google.com/group/openni-dev/browse_thread/thread/d3ee09b0b78b8df6

CC=g++

OTHERS      = -lglut -lXnVNite -lOpenNI

LIBNAME     = $(OTHERS)
              
INCLUDEPATH = -I/usr/include/nite/                      \
              -I/usr/include/ni/

# DEBUG
CFLAGS      = $(LIBPATH) -ggdb -o0 $(INCLUDEPATH)

# RELEASE
# CFLAGS      = $(LIBPATH) -o3 $(INCLUDEPATH)

SRCS        = $(wildcard src/*.cpp)

OBJS        = $(SRCS:.cpp=.o)

EXE         = build/antfarm

all: $(SRCS) $(EXE)
	# rm -f $(OBJS)

$(EXE): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBNAME) -o $@

.cpp.o:
	$(CC) -static $(CFLAGS) $(LIBNAME) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -f $(EXE)

redo:
	make clean
	make all

