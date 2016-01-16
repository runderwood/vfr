srcdir=./src/
builddir=./build/

CC=gcc
CFLAGS=-g -std=gnu99 -Wall

vfr: 
	$(CC) -I. \
        $(shell gdal-config --cflags) \
        $(shell pkg-config --cflags cairo) \
        $(shell pkg-config --cflags lua-5.1) \
        -I$(srcdir) \
    $(CFLAGS) \
        -o $(builddir)vfr \
         $(srcdir)vfr.c  \
         $(shell pkg-config --libs cairo) \
         $(shell pkg-config --libs lua-5.1) \
         $(shell gdal-config --libs) \
