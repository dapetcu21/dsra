all: client-make server-make
clean: client-clean server-clean
install: client-install server-install 

client-make: 
	cd client; make

server-make:
	cd server; make

client-clean:
	cd client; make clean

server-clean:
	cd server; make clean

client-install:
	cd client; make install 

server-install:
	cd server; make install
