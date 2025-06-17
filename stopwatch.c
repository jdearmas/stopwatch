/*
 * Enhanced Console Stopwatch with Subgoals and Split Timers
 *
 * Features:
 * - High-resolution timing via QueryPerformanceCounter
 * - Partial console redraws (only updates time and active split)
 * - Start/Stop main timer (s), Reset (r)
 * - Start subgoal/split timer (g), Stop split timer (h)
 * - Save Log (t), Quit (q)
 * - Multiple named splits with start, end, and duration
 * - Log output in Org-mode style: each split as its own entry
 *
 * Compilation:
 * cl stopwatch.c
 */

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include <string.h>

#define MAX_GOAL_LEN    256
#define MAX_SPLITS      20
#define FILE_PATH       "C:\\Users\\John\\org\\done.org"

typedef struct {
    char name[MAX_GOAL_LEN];
    double start;
    double end;
} Split;

// Global state
static int running = 0;
static double elapsed = 0;
static LARGE_INTEGER freq, startCount;
static Split splits[MAX_SPLITS];
static int splitCount = 0;
static int activeSplit = -1;
static char mainGoal[MAX_GOAL_LEN] = "";
static time_t log_start;
static int need_redraw = 1;

// Move cursor to (x,y)
void move_cursor(int x, int y) {
    COORD c = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

// Format seconds into HH:MM:SS.mmm
void format_time(double t, char* buf) {
    int hrs = (int)(t / 3600);
    int mins = ((int)t / 60) % 60;
    int secs = (int)t % 60;
    int ms   = (int)((t - (int)t) * 1000);
    sprintf(buf, "%02d:%02d:%02d.%03d", hrs, mins, secs, ms);
}

// Clear entire console
void clear_screen() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD written;
    COORD home = {0, 0};
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return;
    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacter(h, ' ', cells, home, &written);
    FillConsoleOutputAttribute(h, csbi.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(h, home);
}

// Draw static UI: header, goal, splits, controls
void draw_static_ui() {
    clear_screen();
    printf("=== Enhanced Stopwatch ===\n");
    printf("Goal  : %s\n", mainGoal[0] ? mainGoal : "(none)");
    printf("Time  : 00:00:00.000\n");
    printf("Splits (%d):\n", splitCount);
    for (int i = 0; i < splitCount; i++) {
        char startBuf[16];
        format_time(splits[i].start, startBuf);
        if (splits[i].end >= 0) {
            char endBuf[16], durBuf[16];
            format_time(splits[i].end, endBuf);
            format_time(splits[i].end - splits[i].start, durBuf);
            printf(" %2d) %-10s -> %-10s = %-10s  %s\n",
                   i+1, startBuf, endBuf, durBuf, splits[i].name);
        } else {
            printf(" %2d) %-10s -> %-10s = %-10s  %s\n",
                   i+1, "--:--:--.---", "--:--:--.---", "--:--:--.---", splits[i].name);
        }
    }
    printf("\nControls: s=start/stop | r=reset | g=start split | h=stop split | t=save log | q=quit\n");
    need_redraw = 0;
}

// Update time line and active split progress
void draw_time_line() {
    double nowT = elapsed;
    if (running) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        nowT += (double)(now.QuadPart - startCount.QuadPart) / freq.QuadPart;
    }
    char buf[16];
    format_time(nowT, buf);
    move_cursor(0, 2);
    printf("Time  : %s   ", buf);
    if (activeSplit >= 0 && activeSplit < splitCount) {
        double rel = nowT - splits[activeSplit].start;
        char relBuf[16];
        format_time(rel, relBuf);
        move_cursor(0, 3 + activeSplit);
        printf(" %2d) %-10s -> %-10s = %-10s  %s\n",
               activeSplit+1,
               "--:--:--.---", "--:--:--.---", relBuf,
               splits[activeSplit].name);
    }
}

// Save main goal and splits as separate Org entries
void save_log() {
    FILE* f = fopen(FILE_PATH, "a");
    if (!f) { perror("Failed to open log file"); return; }
    time_t endt;
    time(&endt);
    struct tm* st = localtime(&log_start);
    struct tm* et = localtime(&endt);
    double totalDur = difftime(endt, log_start);
    char totalBuf[16];
    format_time(totalDur, totalBuf);

    // Main goal entry
    fprintf(f, "* %s\n", mainGoal);
    fprintf(f, "  :LOGBOOK:\n");
    fprintf(f, "  CLOCK: [%04d-%02d-%02d %02d:%02d]--[%04d-%02d-%02d %02d:%02d] => %s\n",
            st->tm_year+1900, st->tm_mon+1, st->tm_mday, st->tm_hour, st->tm_min,
            et->tm_year+1900, et->tm_mon+1, et->tm_mday, et->tm_hour, et->tm_min,
            totalBuf);
    fprintf(f, "  :END:\n\n");

    // Each split as its own entry
    for (int i = 0; i < splitCount; i++) {
        if (splits[i].end < 0) continue;
        double dur = splits[i].end - splits[i].start;
        char startBuf[16], endBuf[16], durBuf[16];
        format_time(splits[i].start, startBuf);
        format_time(splits[i].end, endBuf);
        format_time(dur, durBuf);
        fprintf(f, "** %s\n", splits[i].name);
        fprintf(f, "  :LOGBOOK:\n");
        fprintf(f, "  CLOCK: [%04d-%02d-%02d %02d:%02d:%02d]--[%04d-%02d-%02d %02d:%02d:%02d] => %s\n",
                st->tm_year+1900, st->tm_mon+1, st->tm_mday, st->tm_hour, st->tm_min, st->tm_sec,
                et->tm_year+1900, et->tm_mon+1, et->tm_mday, et->tm_hour, et->tm_min, et->tm_sec,
                durBuf);
        fprintf(f, "  :END:\n\n");
    }
    fclose(f);
    need_redraw = 1;
}

int main() {
    QueryPerformanceFrequency(&freq);
    draw_static_ui();
    while (1) {
        if (_kbhit()) {
            char c = _getch();
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double curr = elapsed + (double)(now.QuadPart - startCount.QuadPart) / freq.QuadPart;
            switch (c) {
                case 's':
                    if (running) {
                        elapsed = curr;
                        running = 0;
                    } else {
                        printf("\nEnter main goal: ");
                        fgets(mainGoal, MAX_GOAL_LEN, stdin);
                        mainGoal[strcspn(mainGoal, "\n")] = '\0';
                        time(&log_start);
                        startCount = now;
                        elapsed = 0;
                        splitCount = 0;
                        activeSplit = -1;
                        running = 1;
                        need_redraw = 1;
                    }
                    break;
                case 'r':
                    running = 0;
                    elapsed = 0;
                    splitCount = 0;
                    mainGoal[0] = '\0';
                    activeSplit = -1;
                    need_redraw = 1;
                    break;
                case 'g':
                    if (running && splitCount < MAX_SPLITS) {
                        splits[splitCount].start = curr;
                        splits[splitCount].end = -1;
                        printf("\nEnter split name: ");
                        fgets(splits[splitCount].name, MAX_GOAL_LEN, stdin);
                        splits[splitCount].name[strcspn(splits[splitCount].name, "\n")] = '\0';
                        activeSplit = splitCount;
                        splitCount++;
                        need_redraw = 1;
                    }
                    break;
                case 'h':
                    if (activeSplit >= 0) {
                        splits[activeSplit].end = curr;
                        activeSplit = -1;
                        need_redraw = 1;
                    }
                    break;
                case 't':
                    if (!running && mainGoal[0]) save_log();
                    break;
                case 'q':
                    return 0;
            }
            if (need_redraw) draw_static_ui();
        }
        if (running) draw_time_line();
        Sleep(50);
    }
    return 0;
}
