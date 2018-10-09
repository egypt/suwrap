LIBS=-lutil
BINS=su

all: $(BINS)

su: suwrap.c
	$(CC) $< $(LIBS) -o $@

clean:
	rm $(BINS)
