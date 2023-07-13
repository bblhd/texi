CC := cc -std=c99 -Werror -Wall -Wextra -D_POSIX_C_SOURCE=199309L 
INSTALL := /usr/local/bin

all: texi

texi: texi.c clipboard.c
	${CC} -std=c99 $^ -o $@ -lxcb -lxcb-keysyms

clean:
	rm -f texi

install: all
	mkdir -p $(INSTALL)
	cp -f texi $(INSTALL)
	chmod 755 $(INSTALL)/texi

uninstall:
	rm -f $(INSTALL)/texi
