/*
 * Simple Stopwatch Application
 *
 * This program implements a simple stopwatch application using the Windows API.
 * It allows the user to start, stop, and reset the stopwatch, displaying the
 * elapsed time in hours, minutes, seconds, and milliseconds.
 *
 * Controls:
 * - Press 's' to start or stop the stopwatch.
 * - Press 'r' to reset the stopwatch.
 * - Press 'q' to quit the application.
 *
 * The program uses the GetTickCount64 function to measure the elapsed time in
 * milliseconds and conio.h for keyboard input handling.
 *
 * Compilation:
 * To compile this program using the native Windows C compiler (cl.exe) provided
 * by Visual Studio, follow these steps:
 * 1. Save the code to a file, e.g., stopwatch.c.
 * 2. Open the "Developer Command Prompt for Visual Studio".
 * 3. Navigate to the directory where you saved stopwatch.c.
 * 4. Run the command: 
 * cl stopwatch.c
 * stopwatch.exe
 * Running:
 * After compilation, run the executable generated (stopwatch.exe) to start the
 * stopwatch application.
 */
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <time.h>
#include <string.h>

#define MAX_GOAL_LENGTH 256
#define FILE_PATH "C:\\Users\\John\\org\\done.org"

void print_time(double seconds) {
    int hours = (int)(seconds / 3600);
    int minutes = ((int)(seconds / 60)) % 60;
    int secs = (int)seconds % 60;
    int milliseconds = (int)((seconds - (int)seconds) * 1000);
    printf("\r%02d:%02d:%02d.%03d", hours, minutes, secs, milliseconds);
    fflush(stdout);
}

void append_to_file(const char* goal, time_t start_time, time_t end_time) {
    FILE* file = fopen(FILE_PATH, "a");
    if (file == NULL) {
        printf("Error opening file. Creating a new one.\n");
        file = fopen(FILE_PATH, "w");
        if (file == NULL) {
            printf("Error creating file.\n");
            return;
        }
    }

    struct tm* start_tm = localtime(&start_time);
    struct tm* end_tm = localtime(&end_time);
    int duration = (int)difftime(end_time, start_time);

    fprintf(file, "* %s\n", goal);
    fprintf(file, "  :LOGBOOK:\n");
    fprintf(file, "  CLOCK: [%04d-%02d-%02d %s %02d:%02d]--[%04d-%02d-%02d %s %02d:%02d] => %02d:%02d:%02d (HH:MM:SS)\n",
            start_tm->tm_year + 1900, start_tm->tm_mon + 1, start_tm->tm_mday,
            (start_tm->tm_wday == 0) ? "Sun" : (start_tm->tm_wday == 1) ? "Mon" : (start_tm->tm_wday == 2) ? "Tue" : (start_tm->tm_wday == 3) ? "Wed" : (start_tm->tm_wday == 4) ? "Thu" : (start_tm->tm_wday == 5) ? "Fri" : "Sat",
            start_tm->tm_hour, start_tm->tm_min,
            end_tm->tm_year + 1900, end_tm->tm_mon + 1, end_tm->tm_mday,
            (end_tm->tm_wday == 0) ? "Sun" : (end_tm->tm_wday == 1) ? "Mon" : (end_tm->tm_wday == 2) ? "Tue" : (end_tm->tm_wday == 3) ? "Wed" : (end_tm->tm_wday == 4) ? "Thu" : (end_tm->tm_wday == 5) ? "Fri" : "Sat",
            end_tm->tm_hour, end_tm->tm_min,
            duration / 3600, (duration % 3600) / 60, duration % 60);
    fprintf(file, "  :END:\n\n");

    fclose(file);
}

int main(int argc, char** argv) {
    int running = 0;
    double elapsed = 0.0;
    double start_time = 0.0;
    char key;
    char goal[MAX_GOAL_LENGTH];
    time_t clock_start, clock_end;

    printf("Simple Stopwatch\n");
    printf("Press:\n 's' to start/stop\n 'r' to reset\n 't' to save time and goal\n 'q' to quit.\n");

    while (1) {
        if (_kbhit()) {
            key = _getch();

            if (key == 's') {
                if (running) {
                    running = 0;
                    elapsed += (GetTickCount64() - start_time) / 1000.0;
                } else {
                    running = 1;
                    start_time = GetTickCount64();
                    time(&clock_start);
                    printf("\nEnter goal: ");
                    fgets(goal, MAX_GOAL_LENGTH, stdin);
                    goal[strcspn(goal, "\n")] = 0;
                }
            } else if (key == 'r') {
                running = 0;
                elapsed = 0.0;
                printf("\r%02d:%02d:%02d.%03d", 0, 0, 0, 0);
                fflush(stdout);
            } else if (key == 't') {
                running = 0;
                elapsed += (GetTickCount64() - start_time) / 1000.0;
                time(&clock_end);
                append_to_file(goal, clock_start, clock_end);
                printf("\nTime and goal saved.\n");
            } else if (key == 'q') {
                break;
            }
        }

        if (running) {
            double current_time = elapsed + (GetTickCount64() - start_time) / 1000.0;
            print_time(current_time);
        } else {
            print_time(elapsed);
        }

        Sleep(10);
    }

    printf("\nStopwatch stopped. Exiting...\n");
    return 0;
}

