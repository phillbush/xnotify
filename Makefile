PROG = xnotify
OBJS = ${PROG:=.o} ctrlfnt.o
SRCS = ${OBJS:.o=.c}
MANS = ${PROG:=.1}
HEDS = ctrlfnt.h

PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC = /usr/local/include
LOCALLIB = /usr/local/lib
X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

DEFS = -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -D_BSD_SOURCE
INCS = -I${LOCALINC} -I${X11INC} -I/usr/include/freetype2 -I${X11INC}/freetype2
LIBS = -L${LOCALLIB} -L${X11LIB} -lImlib2 -lfontconfig -lXrender -lXft -lXinerama -lX11
PROG_CFLAGS = -std=c99 -pedantic ${DEFS} ${INCS} ${CFLAGS} ${CPPFLAGS}
PROG_LDFLAGS = ${LIBS} ${LDLIBS} ${LDFLAGS}

bindir = ${DESTDIR}${PREFIX}/bin
mandir = ${DESTDIR}${MANPREFIX}/man1

all: ${PROG}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${PROG_LDFLAGS}

.c.o:
	${CC} ${PROG_CFLAGS} -o $@ -c $<

${OBJS}: ${HEDS}

tags: ${SRCS}
	ctags ${SRCS}

lint: ${SRCS}
	-mandoc -T lint -W warning ${MANS}
	-clang-tidy ${SRCS} -- ${PROG_CFLAGS}

clean:
	rm -f ${OBJS} ${PROG} ${PROG:=.core} tags

install: all
	mkdir -p ${bindir}
	mkdir -p ${mandir}
	install -m 755 ${PROG} ${bindir}/${PROG}
	install -m 644 ${MANS} ${mandir}/${MANS}

uninstall:
	-rm ${bindir}/${PROG}
	-rm ${mandir}/${MANS}

.PHONY: all clean install uninstall lint
