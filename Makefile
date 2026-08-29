# Simple Makefile for potato-launcher (mirrors CMakeLists.txt)
CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra
CFLAGS   ?= -O2 -std=c11 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
INCLUDES  = -Isrc -Ivendor -Ivendor/nlohmann -Ivendor/miniz
TARGET    = potato-launcher

SRCS = src/main.cpp src/platform.cpp src/manifest.cpp src/command.cpp \
       src/process.cpp src/zip.cpp src/launcher.cpp
MINIZ_SRCS = vendor/miniz/miniz.c vendor/miniz/miniz_tdef.c \
             vendor/miniz/miniz_tinfl.c vendor/miniz/miniz_zip.c
MINIZ_OBJS = $(MINIZ_SRCS:.c=.o)

OBJS = $(SRCS:.cpp=.o) $(MINIZ_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
