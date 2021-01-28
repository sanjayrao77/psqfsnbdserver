CFLAGS=-g -Wall -O2 -DDEBUG
# CFLAGS=-Wall -O2
CC=gcc
all: psqfs-nbd-server-notls
psqfs-nbd-server: main.o misc.o scan.o sort_dirent_scan.o sort_id_scan.o sort_inode_scan.o mkfs.o range.o assemble.o export.o tcpsocket.o nbd-tls.o runninglist.o common/mapmem.o common/mmapread.o common/blockmem.o common/overwrite_environ.o common/unixaf.o
	gcc -o $@ $^ -lz -lgnutls
psqfs-nbd-server-notls: main.o misc.o scan.o sort_dirent_scan.o sort_id_scan.o sort_inode_scan.o mkfs.o range.o assemble.o export.o tcpsocket.o nbd.o runninglist.o common/mapmem.o common/mmapread.o common/blockmem.o common/overwrite_environ.o common/unixaf.o
	gcc -o $@ $^ -lz
nbd-tls.o: nbd.c
	gcc -o nbd-tls.o -c nbd.c ${CFLAGS} -DHAVETLS
clean:
	rm -f *.o common/*.o psqfs-nbd-server core psqfs-nbd-server-notls
upload: clean
	scp -pr * dance:src/nbd/
jesus: clean
	tar -jcf - . | jesus src.squashfs.tar.bz2
.PHONY: clean jesus upload
