CFLAGS+=-Os

m:m.c makefile
	$(CC) -o m $(CFLAGS) m.c
	@ls -l m

clean:
	-rm -f m

.PHONY: clean
