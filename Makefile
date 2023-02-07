name:=texi
dest:=/usr/local/bin

compile: 
	gcc -Wall -Wextra -Werror -lxcb -lxcb-keysyms -o $(name) texi.c clipboard.c

install:
	mkdir -p $(dest)
	cp $(name) $(dest)
	sudo chmod 755 $(dest)/$(name)
