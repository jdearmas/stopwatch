/*
 * High-Performance Console Stopwatch with Splits
 *
 * Optimizations:
 * - Cache Console Handle and Buffer Info
 * - Minimize API calls in render loop
 * - Inline timing calculations
 * - Reduced overhead in format_time
 * - Single-pass UI redraw when needed
 *
 * Functionality remains identical: main timer, splits, Org-mode logging
 * Controls: s=start/stop, r=reset, g=start split, h=stop split, t=save log, q=quit
 *
 * Compilation:
 * cl /O2 stopwatch.c
 * .\stopwatch.exe
 */

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include <string.h>

#define MAX_GOAL_LEN 256
#define MAX_SPLITS   20
#define FILE_PATH    "C:\\Users\\John\\org\\done.org"

typedef struct {
    char name[MAX_GOAL_LEN];
    double start, end;
} Split;

// Global state
static int running = 0;
static double elapsed = 0;
static LARGE_INTEGER freq, startCount;
static Split splits[MAX_SPLITS];
static int splitCount = 0;
static int activeSplit = -1;
static char mainGoal[MAX_GOAL_LEN];
static time_t logStart;

// Cached console info
static HANDLE hConsole;
static short screenWidth, screenHeight;

// Pre-allocated buffers
static char timeBuf[16], startBuf[16], endBuf[16], durBuf[16];

// Fast format: HH:MM:SS.mmm
static inline void format_time(double t, char* buf) {
    int ms=(int)(t*1000)%1000;
    int totalSec=(int)t;
    int s=totalSec%60;
    int m=(totalSec/60)%60;
    int h=totalSec/3600;
    // Avoid sprintf overhead by using minimal writes
    buf[0]='0'+h/10; buf[1]='0'+h%10;
    buf[2]=':';
    buf[3]='0'+m/10; buf[4]='0'+m%10;
    buf[5]=':';
    buf[6]='0'+s/10; buf[7]='0'+s%10;
    buf[8]='.';
    buf[9]='0'+(ms/100)%10;
    buf[10]='0'+(ms/10)%10;
    buf[11]='0'+ms%10;
    buf[12]='\0';
}

// Move cursor position
static inline void move_cursor(short x, short y) {
    COORD pos = {x,y};
    SetConsoleCursorPosition(hConsole, pos);
}

// Clear entire screen (used only on static redraw)
static void clear_screen() {
    DWORD written;
    COORD home = {0,0};
    FillConsoleOutputCharacter(hConsole, ' ', screenWidth*screenHeight, home, &written);
    FillConsoleOutputAttribute(hConsole, 0x07, screenWidth*screenHeight, home, &written);
    SetConsoleCursorPosition(hConsole, home);
}

// Redraw static UI once per change
static void draw_static() {
    clear_screen();
    printf("=== Enhanced Stopwatch ===\n");
    printf("Goal  : %s\n", *mainGoal ? mainGoal : "(none)");
    printf("Time  : 00:00:00.000\n");
    printf("Splits (%d):\n", splitCount);
    for(int i=0;i<splitCount;i++){
        format_time(splits[i].start, startBuf);
        if(splits[i].end>=0) {
            format_time(splits[i].end, endBuf);
            format_time(splits[i].end - splits[i].start, durBuf);
            printf(" %2d) %-12s -> %-12s= %-12s %s\n",
                   i+1, startBuf, endBuf, durBuf, splits[i].name);
        } else {
            printf(" %2d) %-12s-> %-12s = %-12s %s\n",
                   i+1, "--:--:--.---", "--:--:--.---", "--:--:--.---  ", splits[i].name);
        }
    }
    printf("\nControls: s/start/stop r/reset g/start-split h/stop-split t/save-log q/quit\n");
}

// Update only time and active split
static void draw_dynamic() {
    LARGE_INTEGER now, delta;
    double cur;
    QueryPerformanceCounter(&now);
    delta.QuadPart = now.QuadPart - startCount.QuadPart;
    cur = elapsed + (double)delta.QuadPart/freq.QuadPart;
    format_time(cur, timeBuf);
    move_cursor(0,2);
    printf("Time  : %s   ", timeBuf);
    if(activeSplit>=0) {
        double rel = cur - splits[activeSplit].start;
        format_time(rel, durBuf);
        move_cursor(0, 3 + activeSplit);
        printf(" %2d) %-12s-> %-12s = %-12s %s\n",
               activeSplit+1, "--:--:--.---","--:--:--.---  ",durBuf,splits[activeSplit].name);
    }
}

// Append to Org-mode log
static void save_log() {
    FILE* f=fopen(FILE_PATH,"a"); if(!f)return;
    time_t endt=time(NULL);
    struct tm *st=localtime(&logStart), *et=localtime(&endt);
    double tot = difftime(endt, logStart);
    format_time(tot, timeBuf);
    // Main goal
    fprintf(f,"* %s\n  :LOGBOOK:\n  CLOCK: [%04d-%02d-%02d %02d:%02d]--[%04d-%02d-%02d %02d:%02d] => %s\n  :END:\n\n",
            mainGoal, st->tm_year+1900,st->tm_mon+1,st->tm_mday,st->tm_hour,st->tm_min,
            et->tm_year+1900,et->tm_mon+1,et->tm_mday,et->tm_hour,et->tm_min,timeBuf);
    // Splits
    for(int i=0;i<splitCount;i++){
        if(splits[i].end<0) continue;
        double d = splits[i].end - splits[i].start;
        format_time(splits[i].start,startBuf);
        format_time(splits[i].end,endBuf);
        format_time(d,durBuf);
        fprintf(f,"** %s\n  :LOGBOOK:\n  CLOCK: [%02s]--[%02s] => %s\n  :END:\n\n",
                splits[i].name,startBuf,endBuf,durBuf);
    }
    fclose(f);
}

int main(){
    // Initialize console
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO cs; GetConsoleScreenBufferInfo(hConsole,&cs);
    screenWidth = cs.dwSize.X; screenHeight = cs.dwSize.Y;

    QueryPerformanceFrequency(&freq);
    *mainGoal='\0';
    draw_static();

    while(1){
        if(_kbhit()){
            char c=_getch(); LARGE_INTEGER now; QueryPerformanceCounter(&now);
            double cur = elapsed + (double)(now.QuadPart - startCount.QuadPart)/freq.QuadPart;
            switch(c){
                case 's':
                    if(running){ elapsed=cur; running=0;} else {
                        printf("\nEnter main goal: "); fgets(mainGoal,MAX_GOAL_LEN,stdin);
                        mainGoal[strcspn(mainGoal,"\n")]='\0'; logStart=time(NULL);
                        startCount=now; elapsed=0; splitCount=0; activeSplit=-1; running=1;
                    }
                    draw_static(); break;
                case 'r':
                    running=0; elapsed=0; splitCount=0; *mainGoal='\0'; activeSplit=-1;
                    draw_static(); break;
                case 'g':
                    if(running && splitCount<MAX_SPLITS){
                        splits[splitCount].start=cur; splits[splitCount].end=-1;
                        printf("\nEnter split name: "); fgets(splits[splitCount].name,MAX_GOAL_LEN,stdin);
                        splits[splitCount].name[strcspn(splits[splitCount].name,"\n")]='\0';
                        activeSplit=splitCount++; draw_static(); }
                    break;
                case 'h':
                    if(activeSplit>=0){ splits[activeSplit].end=cur; activeSplit=-1; draw_static(); }
                    break;
                case 't': if(!running && *mainGoal) save_log(); break;
                case 'q': return 0;
            }
        }
        if(running) draw_dynamic();
        Sleep(30);
    }
    return 0;
}
