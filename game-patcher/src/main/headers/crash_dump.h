#pragma once

#include <windows.h>
#include <dbghelp.h>

bool applyCrashDump();

typedef BOOL (WINAPI *MiniDumpWriteDumpFunction)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
                                                 PMINIDUMP_EXCEPTION_INFORMATION exception,
                                                 PMINIDUMP_USER_STREAM_INFORMATION userStreams,
                                                 PMINIDUMP_CALLBACK_INFORMATION callback);

// MiniDumpWriteDump of the system's dbghelp (the game's own is from 2003), or null
MiniDumpWriteDumpFunction systemDumpWriter();
