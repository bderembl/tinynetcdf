CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lm

all: test_tinynetcdf

tinynetcdf.o: tinynetcdf.c tinynetcdf.h
	$(CC) $(CFLAGS) -c tinynetcdf.c -o tinynetcdf.o

test_tinynetcdf: test_tinynetcdf.c tinynetcdf.o
	$(CC) $(CFLAGS) test_tinynetcdf.c tinynetcdf.o -o test_tinynetcdf $(LDFLAGS)

test: test_tinynetcdf
	./test_tinynetcdf

clean:
	rm -f *.o test_tinynetcdf test.nc test_dbl.nc test_rec.nc test_append.nc
