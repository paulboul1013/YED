#define main yed_main
#include "../main.c"
#undef main

static int failures;
static int checks;

static void setup_rows(int count) {
	for (int i = 0; i < E.numrows; i++) editor_free_row(&E.row[i]);
	free(E.row);
	memset(&E, 0, sizeof(E));
	E.screen_rows = 6;
	E.screen_cols = 80;
	for (int i = 0; i < count; i++) editor_insert_row(E.numrows, "A", 1);
}

static void expect_row(const char *name, int row, const char *expected) {
	struct abuf ab = ABUF_INIT;
	editor_draw_rows(&ab);
	char *plain = malloc(ab.len + 1);
	int length = 0;
	for (int i = 0; i < ab.len; i++) {
		if (ab.b[i] == '\x1b' && i + 1 < ab.len && ab.b[i + 1] == '[') {
			i += 2;
			while (i < ab.len && !(ab.b[i] >= '@' && ab.b[i] <= '~')) i++;
		} else {
			plain[length++] = ab.b[i];
		}
	}
	plain[length] = '\0';
	char *line = plain;
	for (int i = 0; i < row; i++) line = strstr(line, "\r\n") + 2;
	*strstr(line, "\r\n") = '\0';
	checks++;
	if (strcmp(line, expected)) {
		fprintf(stderr, "FAIL %s: expected [%s], got [%s]\n", name, expected, line);
		failures++;
	}
	free(plain);
	ab_free(&ab);
}

int main(void) {
	setup_rows(0);
	expect_row("empty buffer has no line number", 0, "  | ~");
	expect_row("empty buffer bottom has no line number", 5, "  | ~");
	setup_rows(1);
	expect_row("first file row is numbered", 0, "1 | A");
	expect_row("row after EOF is not numbered", 1, "  | ~");
	expect_row("bottom row after EOF is not numbered", 5, "  | ~");
	setup_rows(1);
	editor_insert_row(1, "", 0);
	editor_insert_row(2, "B", 1);
	expect_row("blank row inside file keeps its number", 1, "2 | ");
	expect_row("text after blank row is numbered", 2, "3 | B");
	expect_row("empty screen row stays unnumbered", 3, "  | ~");
	setup_rows(12);
	E.rowoff = 10;
	expect_row("scroll uses absolute file number", 0, "11 | A");
	expect_row("last file row is numbered", 1, "12 | A");
	expect_row("EOF after scrolling is unnumbered", 2, "   | ~");
	setup_rows(7);
	while (E.numrows > 1) editor_del_row(E.numrows - 1);
	expect_row("deleting rows removes obsolete numbers", 1, "  | ~");
	const int counts[] = {9, 99, 999};
	const char *before[] = {"1 | A", " 1 | A", "  1 | A"};
	const char *after[] = {" 1 | A", "  1 | A", "   1 | A"};
	for (int i = 0; i < 3; i++) {
		setup_rows(counts[i]);
		expect_row("gutter before digit boundary", 0, before[i]);
		editor_insert_row(E.numrows, "A", 1);
		expect_row("gutter grows at digit boundary", 0, after[i]);
		editor_del_row(E.numrows - 1);
		expect_row("gutter shrinks at digit boundary", 0, before[i]);
	}
	setup_rows(0);
	printf("Line numbers: %d/%d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
