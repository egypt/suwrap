
all:
	$(CC) suwrap.c -lutil -o su

clean:
	rm su
