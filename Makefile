budalloc: budalloc.c
	gcc -o budallocrepl budalloc.c

clean:
	rm -f budallocrepl

debug:
	gcc -g -DDEBUG -o budallocrepl budalloc.c
