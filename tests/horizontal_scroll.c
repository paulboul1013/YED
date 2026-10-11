#define main yed_main
#define write capture_write
#include "../main.c"
#undef write
#undef main

static char frame[4096];
static int failures;
static int checks;

/* Capture the actual refresh output, including its final cursor position. */
ssize_t capture_write(int fd, const void *buffer, size_t count) {
	if (fd != STDOUT_FILENO || count >= sizeof(frame)) {
		fprintf(stderr, "Unexpected screen output\n");
		exit(1);
	}
	memcpy(frame, buffer, count);
	frame[count] = '\0';
	return count;
}

static void reset_editor(void) {
	for (int i = 0; i < E.numrows; i++) editor_free_row(&E.row[i]);
	free(E.row);
	memset(&E, 0, sizeof(E));
	E.screen_rows = 6;
	E.screen_cols = 80;
}

static void setup_long_row(int rows) {
	reset_editor();
	char text[121];
	for (int i = 0; i < 120; i++) text[i] = 'A' + i % 26;
	text[120] = '\0';
	editor_insert_row(0, text, 120);
	for (int i = 1; i < rows; i++) editor_insert_row(i, "A", 1);
}

static void move_cursor(int key, int count) {
	for (int i = 0; i < count; i++) {
		editor_move_cursor(key);
		editor_refresh_screen();
	}
}

static void expect_int(const char *name, const char *field, int actual, int expected) {
	checks++;
	if (actual != expected) {
		fprintf(stderr, "FAIL %s: %s expected %d, got %d\n", name, field, expected, actual);
		failures++;
	}
}

static void expect_frame(const char *name, int expected_column, const char *prefix) {
	int cursor_column = -1;
	for (char *p = frame; *p; p++) {
		int row, column, consumed = 0;
		if (*p == '\x1b' && sscanf(p, "\x1b[%d;%dH%n", &row, &column, &consumed) == 2 && consumed)
			cursor_column = column;
	}
	expect_int(name, "cursor column", cursor_column, expected_column);

	char line[sizeof(frame)];
	int length = 0;
	for (int i = 0; frame[i] && frame[i] != '\r'; i++) {
		if (frame[i] == '\x1b' && frame[i + 1] == '[') {
			i += 2;
			while (frame[i] && !(frame[i] >= '@' && frame[i] <= '~')) i++;
		} else {
			line[length++] = frame[i];
		}
	}
	line[length] = '\0';
	checks++;
	if (strncmp(line, prefix, strlen(prefix))) {
		fprintf(stderr, "FAIL %s: row should start with [%s], got [%s]\n", name, prefix, line);
		failures++;
	}
	checks++;
	if (length > E.screen_cols) {
		fprintf(stderr, "FAIL %s: row width %d exceeds screen width %d\n", name, length, E.screen_cols);
		failures++;
	}
}

int main(void) {
	setup_long_row(1);
	move_cursor(ARROW_RIGHT, 75);
	expect_frame("last visible text column", 80, "1 | ABCDEF");
	move_cursor(ARROW_RIGHT, 1);
	expect_frame("crossing the right edge scrolls immediately", 80, "1 | BCDEFG");
	move_cursor(ARROW_RIGHT, 4);
	expect_frame("continued movement keeps cursor visible", 80, "1 | FGHIJK");
	move_cursor(ARROW_LEFT, 76);
	expect_frame("crossing the left edge scrolls back", 5, "1 | EFGHIJ");
	move_cursor(ARROW_LEFT, 4);
	expect_frame("returning to line start restores the viewport", 5, "1 | ABCDEF");

	setup_long_row(10);
	move_cursor(ARROW_RIGHT, 75);
	expect_frame("two-digit gutter reduces text width", 80, " 1 | BCDEFG");
	setup_long_row(100);
	move_cursor(ARROW_RIGHT, 74);
	expect_frame("three-digit gutter reduces text width", 80, "  1 | BCDEFG");

	setup_long_row(9);
	move_cursor(ARROW_RIGHT, 75);
	editor_insert_row(E.numrows, "A", 1);
	editor_refresh_screen();
	expect_frame("growing gutter scrolls the text", 80, " 1 | BCDEFG");

	const char *tab_rows[] = {"\tX", "a\tX", "ab\tX", "abc\tX", "abcd\tX", "\t\tX", "a\tb\tX"};
	const char *tab_prefixes[] = {"1 |     X", "1 | a   X", "1 | ab  X", "1 | abc X", "1 | abcd    X", "1 |         X", "1 | a   b   X"};
	const int tab_columns[] = {9, 9, 9, 9, 13, 13, 13};
	for (size_t i = 0; i < sizeof(tab_rows) / sizeof(tab_rows[0]); i++) {
		reset_editor();
		editor_insert_row(0, (char *)tab_rows[i], strlen(tab_rows[i]));
		move_cursor(ARROW_RIGHT, strlen(tab_rows[i]) - 1);
		expect_frame(tab_rows[i], tab_columns[i], tab_prefixes[i]);
	}

	reset_editor();
	char tab_at_edge[78];
	memset(tab_at_edge, 'A', 75);
	memcpy(tab_at_edge + 75, "\tX", 3);
	editor_insert_row(0, tab_at_edge, 77);
	move_cursor(ARROW_RIGHT, 76);
	char expected_edge[81];
	memcpy(expected_edge, "1 | ", 4);
	memset(expected_edge + 4, 'A', 74);
	memcpy(expected_edge + 78, " X", 3);
	expect_frame("Tab at right edge keeps cursor on the following character", 80, expected_edge);

	setup_long_row(1);
	E.screen_cols = 5;
	move_cursor(ARROW_RIGHT, 1);
	expect_frame("one-column text area scrolls immediately", 5, "1 | B");

	reset_editor();
	editor_insert_row(0, "A", 1);
	E.screen_cols = 4;
	editor_move_cursor(ARROW_RIGHT);
	editor_scroll();
	expect_int("gutter fills the screen", "horizontal offset", E.coloff, 1);

	reset_editor();
	printf("Horizontal scrolling: %d/%d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
