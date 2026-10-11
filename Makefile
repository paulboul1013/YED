CFLAGS = -Wall -Wextra -pedantic -std=c99

main: main.c
	$(CC) main.c -o main $(CFLAGS)

clean:
	rm -f main

test:
	@test_binary=$$(mktemp /tmp/yed-line-numbers.XXXXXX) || exit 1; \
	trap 'rm -f "$$test_binary"' EXIT; \
	for test_source in tests/line_numbers.c tests/horizontal_scroll.c; do \
		$(CC) "$$test_source" -o "$$test_binary" $(CFLAGS) && "$$test_binary" || exit 1; \
	done

.PHONY: clean test
