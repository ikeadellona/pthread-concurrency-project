.phony all:
all: ACS
ACS: main.c
	gcc -pthread -o ACS main.c

.PHONY clean:
clean:
	rm -rf *.o ACS
