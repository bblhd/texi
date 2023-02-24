name:=texi
dest:=/usr/local/bin

compile: 
	gcc -Wall -Wextra -Werror -Wno-char-subscripts -lxcb -lxcb-keysyms -o $(name) *.c

install:
	mkdir -p $(dest)
	cp $(name) $(dest)
	sudo chmod 755 $(dest)/$(name)
