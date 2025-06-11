CFLAGS = -O2 -g -Wall -Wextra
LDFLAGS = -levent 
objects = 10klob.o dbi.o

10klob: $(objects) 
	cc $(CFLAGS) $(objects) -o 10klob $(LDFLAGS)

dbi.o:
	@git clone "https://github.com/danieltuveson/dbi"
	@sed -i "s/DBI_DISABLE_IO 0/DBI_DISABLE_IO 1/g" dbi/dbi.h
	@sudo $(MAKE) -C dbi 
	@cd ..
	@cp dbi/dbi.o . 
	@cp dbi/dbi.h . 
	@rm -rf dbi

# Installs deps on Debian
deps:
	@sudo apt-get install libevent-dev

clean:
	rm -f 10klob *.o *.a
	rm -rf dbi

