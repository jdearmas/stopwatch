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
 * 4. Run the command: cl stopwatch.c
 *
 * Running:
 * After compilation, run the executable generated (stopwatch.exe) to start the
 * stopwatch application.
 */
#include <stdio.h>
#include <windows.h>
#include <conio.h>

void print_time(double seconds) {
    int hours = (int)(seconds / 3600);
    int minutes = ((int)(seconds / 60)) % 60;
    int secs = (int)seconds % 60;
    int milliseconds = (int)((seconds - (int)seconds) * 1000);
    printf("\r%02d:%02d:%02d.%03d", hours, minutes, secs, milliseconds);
    fflush(stdout);
}

int main() {
    int running = 0;
    double elapsed = 0.0;
    double start_time = 0.0;
    char key;

    printf("Simple Stopwatch\n");
    printf("Press:\n 's' to start/stop\n 'r' to reset\n 'q' to quit.\n");

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
                }
            } else if (key == 'r') {
                running = 0;
                elapsed = 0.0;
                printf("\r%02d:%02d:%02d.%03d", 0, 0, 0, 0);
                fflush(stdout);
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
