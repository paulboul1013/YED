CFLAGS = -Wall -Wextra -pedantic -std=c99

main: main.c
	$(CC) main.c -o main $(CFLAGS)

clean:
	rm -f main