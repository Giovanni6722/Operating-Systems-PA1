// /OpSysPA1.exe.c
//gcc -Wall -Wextra -O2 OpSysPA1.c -o OpSysPA1.exe
//./OpSysPA1.exe

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 20

static int min_in_range(const int *a, int start, int end_excl) 
{
    int m = a[start];
    for (int i = start + 1; i < end_excl; i++) {if (a[i] < m) m = a[i];}
    return m;
}

// reads 10 ints from stdin, prints the min to stdout (ONLY the number).
static int child_main(void) {
    int vals[N / 2];
    for (int i = 0; i < N / 2; i++) 
    {
        if (scanf("%d", &vals[i]) != 1) 
        {
            fprintf(stderr, "CHILD PID=%lu failed to read input\n", (unsigned long)GetCurrentProcessId());
            return 2;
        }
    }

    int m = min_in_range(vals, 0, N / 2);
    fprintf(stderr, "CHILD  PID=%lu  min(second half)=%d\n", (unsigned long)GetCurrentProcessId(), m);

    // Send only the min back to parent via stdout pipe
    printf("%d\n", m);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) 
{
    printf("START parent PID=%lu\n", (unsigned long)GetCurrentProcessId());
    fflush(stdout);

    // If launched as child
    if (argc >= 2 && strcmp(argv[1], "child") == 0) {return child_main();}

    // Parent mode
    int arr[N];
    srand((unsigned)time(NULL) ^ (unsigned)GetCurrentProcessId());
    for (int i = 0; i < N; i++) arr[i] = rand() % 1000;

    int parent_min = min_in_range(arr, 0, N / 2);

    // --- Create pipes for child stdin and child stdout ---
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE; // IMPORTANT for CreateProcess handle inheritance

    HANDLE childStdoutRd = NULL, childStdoutWr = NULL;
    HANDLE childStdinRd  = NULL, childStdinWr  = NULL;

    if (!CreatePipe(&childStdoutRd, &childStdoutWr, &sa, 0)) 
    {
        fprintf(stderr, "CreatePipe(stdout) failed: %lu\n", (unsigned long)GetLastError());
        return 1;
    }
    if (!SetHandleInformation(childStdoutRd, HANDLE_FLAG_INHERIT, 0)) 
    {
        fprintf(stderr, "SetHandleInformation(stdoutRd) failed: %lu\n", (unsigned long)GetLastError());
        return 1;
    }

    if (!CreatePipe(&childStdinRd, &childStdinWr, &sa, 0)) 
    {
        fprintf(stderr, "CreatePipe(stdin) failed: %lu\n", (unsigned long)GetLastError());
        return 1;
    }
    if (!SetHandleInformation(childStdinWr, HANDLE_FLAG_INHERIT, 0)) 
    {
        fprintf(stderr, "SetHandleInformation(stdinWr) failed: %lu\n", (unsigned long)GetLastError());
        return 1;
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    char cmdLine[MAX_PATH + 32];
    snprintf(cmdLine, sizeof(cmdLine), "\"%s\" child", exePath);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput  = childStdinRd;
    si.hStdOutput = childStdoutWr;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE); // child debug prints visible in console

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL ok = CreateProcessA(
        NULL,
        cmdLine,
        NULL,
        NULL,
        TRUE,             
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    // Parent no longer needs these ends
    CloseHandle(childStdinRd);
    CloseHandle(childStdoutWr);

    if (!ok) 
    {
        fprintf(stderr, "CreateProcess failed: %lu\n", (unsigned long)GetLastError());
        CloseHandle(childStdoutRd);
        CloseHandle(childStdinWr);
        return 1;
    }

    DWORD parentPID = GetCurrentProcessId();
    DWORD childPID  = pi.dwProcessId;

    // Send second half to child through child's stdin pipe
    // Send as text (each number newline). Easy for scanf.
    for (int i = N / 2; i < N; i++) 
    {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%d\n", arr[i]);
        DWORD written = 0;
        if (!WriteFile(childStdinWr, buf, (DWORD)len, &written, NULL)) 
        {
            fprintf(stderr, "WriteFile to child stdin failed: %lu\n", (unsigned long)GetLastError());
            break;
        }
    }
    CloseHandle(childStdinWr); // signal EOF to child

    // Read child's min from child's stdout pipe
    char outBuf[128];
    DWORD readBytes = 0;
    if (!ReadFile(childStdoutRd, outBuf, sizeof(outBuf) - 1, &readBytes, NULL)) 
    {
        fprintf(stderr, "ReadFile from child stdout failed: %lu\n", (unsigned long)GetLastError());
        CloseHandle(childStdoutRd);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }
    
    outBuf[readBytes] = '\0';
    CloseHandle(childStdoutRd);

    int child_min = atoi(outBuf);

    // Wait for child to finish
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Print array + results
    printf("ARRAY: ");
    for (int i = 0; i < N; i++) printf("%d%s", arr[i], (i == N - 1) ? "" : " ");
    printf("\n");

    printf("PARENT PID=%lu  min(first half)=%d\n", (unsigned long)parentPID, parent_min);
    printf("CHILD  PID=%lu  min(second half)=%d\n", (unsigned long)childPID, child_min);

    int overall = (parent_min < child_min) ? parent_min : child_min;
    printf("FINAL  min(array)=%d\n", overall);

    return 0;
}