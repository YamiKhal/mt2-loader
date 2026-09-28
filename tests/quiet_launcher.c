#include <stdio.h>
#include <wchar.h>
#include <windows.h>

#define TIMEOUT_MILLISECONDS 30000
#define EXIT_LAUNCH_FAILED 200
#define EXIT_TIMED_OUT 201
#define EXIT_STATUS_PRINTED 250


int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: quiet_launcher <exe> [argument]\n");

        return EXIT_LAUNCH_FAILED;
    }

    // Inherited by the child: a missing DLL ends the process with an exit code instead of opening a dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    wchar_t command_line[2048];
    swprintf(command_line, 2048, L"\"%ls\" %ls", argv[1], argc > 2 ? argv[2] : L"");

    STARTUPINFOW startup = { .cb = sizeof startup };
    PROCESS_INFORMATION process;

    if (!CreateProcessW(argv[1], command_line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        fwprintf(stderr, L"could not start %ls (error %lu)\n", argv[1], GetLastError());

        return EXIT_LAUNCH_FAILED;
    }

    DWORD exit_code = EXIT_TIMED_OUT;

    if (WaitForSingleObject(process.hProcess, TIMEOUT_MILLISECONDS) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exit_code);
    } else {
        TerminateProcess(process.hProcess, EXIT_TIMED_OUT);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    // MSYS bash turns large NTSTATUS exit codes into its own "error while loading shared libraries".
    if (exit_code > 255) {
        printf("exit status 0x%08lx\n", exit_code);

        return EXIT_STATUS_PRINTED;
    }

    return (int)exit_code;
}
