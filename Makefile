srcdir=./src/
builddir=./build/

CC=gcc
CFLAGS=-g -std=gnu99 -Wall

vfr: 
	$(CC) -I. $(shell gdal-config --cflags) -I$(srcdir) \
        $(CFLAGS) \
        -o $(builddir)vfr \
         $(srcdir)vfr.c  \
         $(shell gdal-config --libs)
