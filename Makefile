srcdir=./src/
builddir=./build/

CC=gcc
CFLAGS=-g -std=gnu99 -Wall

vfr: 
	$(CC) -I. \
        $(shell gdal-config --cflags) \
        $(shell pkg-config --cflags cairo) \
        -I$(srcdir) \
    $(CFLAGS) \
        -o $(builddir)vfr \
         $(srcdir)vfr.c  \
         $(shell pkg-config --libs cairo) \
         $(shell gdal-config --libs) \
