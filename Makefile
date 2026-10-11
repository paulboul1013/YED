CFLAGS = -Wall -Wextra -pedantic -std=c99

main: main.c
	$(CC) main.c -o main $(CFLAGS)

clean:
	rm -f main

test:
	@test_binary=$$(mktemp /tmp/yed-line-numbers.XXXXXX) || exit 1; \
	trap 'rm -f "$$test_binary"' EXIT; \
	$(CC) tests/line_numbers.c -o "$$test_binary" $(CFLAGS) && "$$test_binary"

.PHONY: clean test
