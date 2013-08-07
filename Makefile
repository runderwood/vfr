srcdir=./src/
builddir=./build/

CC=gcc
CFLAGS=-g -std=gnu99 -Wall

ras: 
	$(CC) -I. $(shell gdal-config --cflags) -I$(srcdir) \
        $(CFLAGS) \
        -o $(builddir)vrf \
         $(srcdir)vrf.c  \
         $(shell gdal-config --libs)
