CC=gcc
BINDIR=../bin
SRCDIR=../src
INCLUDEDIR=../include
BINNAME=atddtree


atddtree: clean
	$(CC)  -g -o $(BINDIR)/$(BINNAME) -I/usr/include -I/usr/local/include/gsl -I../include -L. -L/usr/local/lib  atddtree.c main.c -lm -lgsl -lgslcblas

lib: clean
	$(CC) -I/usr/include -I/usr/local/include/gsl -I../include -L. -L/usr/local/lib -c -fPIC atddtree.c main.c
	gcc -shared -o $(BINDIR)/libatddtree.so.1 $(SRCDIR)/atddtree.o $(SRCDIR)/main.o

install: lib
	sudo cp $(BINDIR)/libatddtree.so.1 /opt/lib
	sudo mkdir -p /usr/local/include/atddtree
	sudo cp $(INCLUDEDIR)/*.h /usr/local/include/atddtree/
	sudo ln -sf /opt/lib/libatddtree.so.1 /opt/lib/libatddtree.so

btree:
	$(CC)  -g -o $(BINDIR)/btree -I/usr/include -I/usr/local/include/gsl -I../include -L. -L/usr/local/lib  btree.c btreemain.c -lm -lgsl -lgslcblas	

testgettime: clean
	$(CC) -g -o $(BINDIR)/tgt -I/usr/include testgettime.c -lrt

clean:
	rm $(SRCDIR)/*.o -rf
	rm $(BINDIR)/* -rf