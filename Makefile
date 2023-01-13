PROG = xnotify
OBJS = ${PROG:=.o}
SRCS = ${OBJS:.o=.c}

PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib

INCS = -I${LOCALINC} -I${X11INC} -I/usr/include/freetype2 -I${X11INC}/freetype2
LIBS = -L${LOCALLIB} -L${X11LIB} -lfontconfig -lXft -lX11 -lXinerama -lImlib2

all: ${PROG}

${PROG}: ${OBJS}
	${CC} -o $@ ${OBJS} ${LIBS} ${LDFLAGS}

${OBJS}: ${PROG:=.h} config.h

.c.o:
	${CC} ${INCS} ${CFLAGS} ${CPPFLAGS} -c $<

tags: ${SRCS}
	ctags ${SRCS}

clean:
	rm -f ${OBJS} ${PROG} ${PROG:=.core} tags

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin/${PROG}
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 644 ${PROG:=.1} ${DESTDIR}${MANPREFIX}/man1/${PROG:=.1}

uninstall:
	rm ${DESTDIR}${PREFIX}/bin/${PROG}
	rm ${DESTDIR}${MANPREFIX}/man1/${PROG:=.1}

.PHONY: all tags clean install uninstall
