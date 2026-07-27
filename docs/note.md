# YED Terminal Editor Notes

這份筆記整理 YED 目前用到的 terminal、raw mode、ANSI escape sequence、render loop、syntax highlight 與 mouse wheel scrolling 知識。目標是能快速複習，也能回頭定位實作時該改哪裡。

## Terminal Input Mode

### Canonical Mode / Cooked Mode

canonical mode 是 terminal 預設輸入模式。

特性：

- 使用者輸入的內容會先由 terminal 暫存。
- 程式通常要等使用者按下 `Enter` 才會收到整行資料。
- 使用者可以在送出前用 Backspace 修改文字。

例子：

```text
type text -> backspace/edit -> press Enter -> program receives line
```

這種模式適合 shell，但不適合文字編輯器，因為 editor 需要每次按鍵都立即反應。

### Raw Mode

raw mode 讓程式可以 byte-by-byte 讀取輸入。

特性：

- 按任意鍵後，程式可以立即收到。
- terminal 不再自動 echo 輸入字元。
- terminal 不再自動處理部分控制鍵，例如 `Ctrl-C`、`Ctrl-Z`。
- editor 需要自己處理 Enter、Backspace、方向鍵、refresh 等行為。

YED 的核心輸入流程：

```text
enable_rawmode()
-> editor_readkey()
-> editor_process_keypress()
-> update editor state
-> editor_refresh_screen()
```

## termios 常用 API

### `tcgetattr(int fd, struct termios *termios_p)`

取得 terminal 目前設定，並存進 `termios` 結構。

YED 會先保存原本設定：

```c
tcgetattr(STDIN_FILENO, &E.orig_termios);
```

這樣離開 editor 時可以還原。

### `tcsetattr(int fd, int optional_actions, const struct termios *termios_p)`

把指定的 `termios` 設定套用到 terminal。

YED 使用：

```c
tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
```

### `TCSAFLUSH`

`tcsetattr()` 的 `optional_actions`。

意思：

- 等待 pending output 寫完。
- 丟棄還沒有被 read 的 input。

這適合切換 raw mode，避免舊輸入殘留到 editor。

### `atexit(void (*function)(void))`

來自 `<stdlib.h>`。

註冊程式結束時要呼叫的函式。YED 用它確保程式退出時會執行：

```c
disable_rawmode();
```

避免 terminal 永遠停在 raw mode。

## termios Flags

### `c_lflag`

local flags。

常見 flag：

- `ECHO`: echo input characters。關掉後，按鍵不會自動印到 terminal。
- `ICANON`: canonical mode。關掉後，可以 byte-by-byte read。
- `ISIG`: 控制 `Ctrl-C`、`Ctrl-Z` 等是否產生 signal。
- `IEXTEN`: implementation-defined input processing。

`ISIG` 例子：

```text
Ctrl-C -> SIGINT
Ctrl-Z -> SIGTSTP
```

關掉 `ISIG` 後，editor 可以自己收到並處理這些按鍵。

`IEXTEN` 例子：

某些系統中 `Ctrl-V` 會讓 terminal 等下一個字元，這是 terminal 額外處理。raw editor 通常會關掉。

### `c_iflag`

input flags。

常見 flag：

- `IXON`: 啟用 XON/XOFF flow control。
- `ICRNL`: 把 carriage return `\r` 轉成 newline `\n`。
- `BRKINT`: break condition 會送出 `SIGINT`。
- `INPCK`: input parity check。
- `ISTRIP`: strip eighth bit，把第 8 bit 清成 0。

`IXON` 影響：

```text
Ctrl-S -> stop terminal output
Ctrl-Q -> continue terminal output
```

editor 會關掉 `IXON`，讓 `Ctrl-S` 可以拿來 save。

`ICRNL` 影響：

terminal 通常會把 Enter 送成 `\r`，再轉成 `\n`。關掉 `ICRNL` 後，editor 可以直接看到 `\r`，也就是 character code 13。

### `c_oflag`

output flags。

常見 flag：

- `OPOST`: output post-processing。

關掉 `OPOST` 後，terminal 不會自動把 `\n` 轉成 `\r\n`。

所以 editor 輸出換行時要自己寫：

```c
"\r\n"
```

### `c_cflag`

control flags。

常見設定：

- `CS8`: character size 設為 8 bits per byte。

`CS8` 不是單一 flag，而是 bitmask 裡的 character size 設定。

### `VMIN`

read 至少需要多少字元才回傳。

YED 設成：

```c
raw.c_cc[VMIN] = 0;
```

代表沒有資料時，`read()` 不一定要等到一個字元。

### `VTIME`

read 最多等待多久。

YED 設成：

```c
raw.c_cc[VTIME] = 1;
```

單位是十分之一秒，所以 `1` 代表 100ms。

## Control Character

### `iscntrl(int c)`

檢查字元是否為 control character。

ASCII control character 範圍：

```text
0-31
127  // Backspace / DEL
```

### `CTRL_KEY` Macro

常見寫法：

```c
#define CTRL_KEY(k) ((k) & 0x1f)
```

例子：

```text
Ctrl-Q = 'q' & 0x1f
       = 113 & 31
       = 17
```

原因是 ASCII control code 中：

```text
Ctrl-A = 1
Ctrl-B = 2
...
Ctrl-Z = 26
```

### 顯示 Control Character

control character 本身不可列印，所以 editor 通常會轉成可見符號。

常見做法：

```c
char sym = (c[j] <= 26) ? '@' + c[j] : '?';
```

原因：

```text
'@' + 1  = 'A'  // Ctrl-A
'@' + 2  = 'B'  // Ctrl-B
'@' + 26 = 'Z'  // Ctrl-Z
```

## ANSI Escape Sequence

escape sequence 用來控制 terminal，例如移動 cursor、清畫面、改顏色。

通常以 ESC 字元開始：

```text
\x1b
```

CSI sequence 通常長這樣：

```text
ESC [ command
```

也就是：

```c
"\x1b[..."
```

## Screen / Cursor Escape Sequence

### `ESC [ 2 J`

清除整個畫面：

```c
"\x1b[2J"
```

### `ESC [ H`

設定 cursor 位置。

沒有參數時，預設移到左上角：

```c
"\x1b[H"
```

等同於：

```text
ESC [ 1 ; 1 H
```

注意：terminal row / column 是 1-based，不是 0-based。

### `ESC [ row ; col H`

移動 cursor 到指定位置：

```c
snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
```

### `ESC [ 999 C` / `ESC [ 999 B`

把 cursor 往右 / 往下移很遠，用來取得 terminal 邊界：

```c
"\x1b[999C\x1b[999B"
```

意思：

- `C`: move cursor right
- `B`: move cursor down
- `999`: 足夠大，讓 cursor 到達畫面右下角

### `ESC [ 6 n`

查詢 cursor 位置：

```c
"\x1b[6n"
```

terminal 會回傳類似：

```text
ESC [ rows ; cols R
```

### `ESC [ ? 25 l` / `ESC [ ? 25 h`

隱藏 / 顯示 cursor：

```c
"\x1b[?25l"  // hide cursor
"\x1b[?25h"  // show cursor
```

render 時通常先 hide cursor，畫完再 show cursor，避免閃爍。

### `ESC [ K`

清除目前 cursor 右側到行尾：

```c
"\x1b[K"
```

其他形式：

```text
ESC [ 0 K  clear from cursor to end of line
ESC [ 1 K  clear from cursor to beginning of line
ESC [ 2 K  clear entire line
```

YED 每列畫完後用 `ESC [ K` 清掉舊畫面殘留。

## Color Escape Sequence

### `ESC [ 7 m`

反白顯示：

```c
"\x1b[7m"
```

常用於顯示不可見 control character。

### `ESC [ m`

重置所有文字屬性：

```c
"\x1b[m"
```

### `ESC [ 30 m` 到 `ESC [ 37 m`

設定前景色：

```text
30 black
31 red
32 green
33 yellow
34 blue
35 magenta
36 cyan
37 white
```

### `ESC [ 39 m`

恢復預設前景色：

```c
"\x1b[39m"
```

注意長度是 5：

```text
ESC [ 3 9 m
```

所以 append 時要寫：

```c
ab_append(ab, "\x1b[39m", 5);
```

如果誤寫成：

```c
ab_append(ab, "\x1b[39m", 3);
```

只會送出不完整的 `ESC [ 3`，terminal 顏色可能無法 reset，下一列文字會沿用上一列顏色。

## Window Size

### Simple Way

使用 `ioctl()`：

```c
struct winsize ws;
ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
```

成功後可以取得：

```c
ws.ws_row;
ws.ws_col;
```

### Fallback Way

如果 `ioctl()` 失敗：

1. 用 `ESC [ 999 C` 和 `ESC [ 999 B` 把 cursor 移到右下角。
2. 用 `ESC [ 6 n` 查詢 cursor 位置。
3. parse terminal 回傳的 `ESC [ rows ; cols R`。

## Render Loop

YED 的主迴圈：

```c
while (1) {
    editor_refresh_screen();
    editor_process_keypress();
}
```

核心觀念：

```text
render current state
-> read input
-> update state
-> next loop render new state
```

所以 input handler 不應該直接輸出畫面，也不應該直接呼叫 `editor_refresh_screen()`。

正確分工：

- `editor_readkey()`: 把 terminal input parse 成 key code。
- `editor_process_keypress()`: 根據 key code 修改 editor state。
- `editor_scroll()`: 根據 cursor 決定 viewport offset。
- `editor_draw_rows()`: 根據 `E.rowoff` / `E.coloff` 畫出可見範圍。
- `editor_refresh_screen()`: 組合整個畫面並一次 write。

## Scrolling State

重要欄位：

```c
E.cy      // cursor row in file
E.cx      // cursor column in file
E.rx      // rendered cursor x, tabs expanded
E.rowoff  // top visible file row
E.coloff  // left visible rendered column
```

`editor_scroll()` 會根據 cursor 自動調整 viewport：

```c
if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
}

if (E.cy >= E.rowoff + E.screen_rows) {
    E.rowoff = E.cy - E.screen_rows + 1;
}
```

所以如果事件處理只改 `E.rowoff`，下一次 render 時可能被 `editor_scroll()` 拉回 cursor 位置。

第一版 scroll 實作應該優先移動 cursor：

```c
editor_move_cursor(ARROW_UP);
editor_move_cursor(ARROW_DOWN);
```

讓 cursor 和 viewport 使用同一套既有邏輯。

## Mouse Wheel Scrolling

mouse wheel 不會因為 raw mode 自動送給程式，需要開啟 terminal mouse reporting。

啟用：

```c
write(STDOUT_FILENO, "\x1b[?1000h", 8); // basic mouse reporting
write(STDOUT_FILENO, "\x1b[?1006h", 8); // SGR mouse mode
```

關閉：

```c
write(STDOUT_FILENO, "\x1b[?1000l", 8);
write(STDOUT_FILENO, "\x1b[?1006l", 8);
```

SGR mouse wheel sequence 格式：

```text
ESC [ < button ; x ; y M
```

常用 button code：

```text
64 = wheel up
65 = wheel down
```

目前 render loop 是先 render 再 process input：

```c
while (1) {
    editor_refresh_screen();
    editor_process_keypress();
}
```

所以 mouse event handler 只改 state，不直接 render。

如果需要像 vim 一樣使用獨立畫面 buffer：

```c
write(STDOUT_FILENO, "\x1b[?1049h", 8); // enter alternate screen
write(STDOUT_FILENO, "\x1b[?1049l", 8); // leave alternate screen
```

## Syntax Highlight Notes

### `row->render` vs `row->chars`

`chars` 是檔案中的原始字串。

`render` 是實際顯示用字串，例如 tab 會被展開成多個 spaces。

高亮使用 `row->render` 的索引，因為畫面輸出也是照 `render` 畫。

### `row->hl`

`row->hl` 是和 `row->render` 對齊的 highlight buffer。

每個 rendered character 都有一個 highlight type：

```c
HL_NORMAL
HL_COMMENT
HL_MLCOMMENT
HL_KEYWORD1
HL_KEYWORD2
HL_STRING
HL_NUMBER
HL_MATCH
```

### Single Line Comment

遇到 `//` 後：

```c
memset(&row->hl[i], HL_COMMENT, row->rsize - i);
break;
```

這只會標記目前 row 從 `//` 到行尾，不會跨行。

如果下一行看起來也是 comment 顏色，優先檢查 render 端 ANSI color reset 是否完整，例如 `"\x1b[39m"` 長度應為 5。

### Multiline Comment

多行註解使用：

```c
HL_MLCOMMENT
```

並靠兩個狀態運作：

```c
in_comment
row->hl_open_comment
```

流程：

```text
遇到 /*
-> in_comment = 1
-> 後續字元標成 HL_MLCOMMENT
-> 遇到 */
-> in_comment = 0
```

行尾：

```c
row->hl_open_comment = in_comment;
```

下一行開頭：

```c
int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);
```

所以如果上一行的 `/*` 沒有被 `*/` 關閉，下一行會從一開始就當成多行註解。

### String Highlight

在 string 裡的字元應該標成 `HL_STRING` 後直接 `continue`，避免掉到 number 或 keyword 邏輯。

正確概念：

```c
if (in_string) {
    row->hl[i] = HL_STRING;

    if (c == '\\' && i + 1 < row->rsize) {
        row->hl[i + 1] = HL_STRING;
        i += 2;
        continue;
    }

    if (c == in_string) {
        in_string = 0;
    }

    i++;
    prev_sep = 1;
    continue;
}
```

原因：如果 string 中的數字繼續進入 number highlight，`"123"` 裡的 `123` 可能被覆蓋成 `HL_NUMBER`。

## C Library Notes

### `memmove()`

來自 `<string.h>`。

用途類似 `memcpy()`，但可安全處理 source 和 destination 記憶體重疊的情況。

常用於插入或刪除 row 時移動陣列內容。

### `ftruncate(fd, len)`

設定檔案大小。

行為：

- 如果檔案比 `len` 大，截斷後面的內容。
- 如果檔案比 `len` 小，延伸並補 `0` bytes。
- 成功回傳 0。

常用於 save file 前，先把檔案截成目標大小。

### `strstr(const char *haystack, const char *needle)`

尋找 `needle` 在 `haystack` 中第一次出現的位置。

回傳：

- 找到：指向 substring 開頭的 pointer。
- 找不到：`NULL`。
