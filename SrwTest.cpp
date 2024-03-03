#include "stdafx.h"

#ifndef _NTPEBTEB_H

typedef struct TEB_ACTIVE_FRAME_CONTEXT
{
	ULONG Flags;
	PCSTR FrameName;
} *PTEB_ACTIVE_FRAME_CONTEXT;

typedef struct TEB_ACTIVE_FRAME
{
	ULONG Flags;
	TEB_ACTIVE_FRAME *Previous;
	const TEB_ACTIVE_FRAME_CONTEXT* Context;
} *PTEB_ACTIVE_FRAME;

EXTERN_C_START

NTSYSAPI
VOID
NTAPI
RtlPushFrame(PTEB_ACTIVE_FRAME Frame);

NTSYSAPI
VOID
NTAPI
RtlPopFrame(PTEB_ACTIVE_FRAME Frame);

NTSYSAPI
PTEB_ACTIVE_FRAME
NTAPI
RtlGetFrame();

EXTERN_C_END

#endif

#define NtCurrentThread() ( (HANDLE)(LONG_PTR) -2 ) 

BOOL SetBpOnAddress(PVOID pv)
{
	CONTEXT ctx {};
	ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
	ctx.Dr3 = (ULONG_PTR)pv;
	ctx.Dr7 = 0xF0000040;
	return SetThreadContext(NtCurrentThread(), &ctx);
}

struct MY_VEX_FRAME : TEB_ACTIVE_FRAME 
{
	inline static const TEB_ACTIVE_FRAME_CONTEXT _S_afc = { 0, "MY_VEX_FRAME" };

	PSRWLOCK SRWLock;
	HANDLE hThread = 0;

	MY_VEX_FRAME(PSRWLOCK SRWLock) : SRWLock(SRWLock)
	{
		Flags = 0;
		Context = &_S_afc;
		RtlPushFrame(this);
	}

	~MY_VEX_FRAME()
	{
		if (hThread) CloseHandle(hThread);
		RtlPopFrame(this);
	}

	static MY_VEX_FRAME* get()
	{
		if (PTEB_ACTIVE_FRAME frame = RtlGetFrame())
		{
			do 
			{
				if (frame->Context == &_S_afc)
				{
					return static_cast<MY_VEX_FRAME*>(frame);
				}
			} while (frame = frame->Previous);
		}

		return 0;
	}
};

int Message( _In_opt_ PCWSTR lpText, _In_ UINT uType)
{
	wchar_t msg[0x10];
	swprintf_s(msg, _countof(msg), L"[%x]", GetCurrentThreadId());
	return MessageBoxW(0, lpText, msg, uType);
}

struct ThreadTestData 
{
	HANDLE hEvent;
	PSRWLOCK SRWLock = 0;
	PSRWLOCK SRWLockAlt = 0;
	LONG numThreads = 1;

	void EndThread()
	{
		if (!InterlockedDecrementNoFence(&numThreads))
		{
			if (!SetEvent(hEvent)) __debugbreak();
		}
	}

	void DoStuff()
	{
		AcquireSRWLockShared(SRWLock);

		Message(__FUNCTIONW__, MB_ICONINFORMATION);

		ReleaseSRWLockShared(SRWLock);

		EndThread();
	}

	static ULONG WINAPI _S_DoStuff(PVOID data)
	{
		reinterpret_cast<ThreadTestData*>(data)->DoStuff();
		return 0;
	}

	void TestInternal(ULONG n = 2)
	{
		MY_VEX_FRAME vf(SRWLockAlt);
		SetBpOnAddress(SRWLock);

		AcquireSRWLockExclusive(SRWLock);

		do 
		{
			numThreads++, --n;

			if (HANDLE hThread = CreateThread(0, 0, _S_DoStuff, this, n ? 0 : CREATE_SUSPENDED, 0))
			{
				if (n)
				{
					CloseHandle(hThread);
				}
				else
				{
					vf.hThread = hThread;
				}
			}
			else
			{
				numThreads--;
			}

		} while (n);

		// give time for first thread call AcquireSRWLockShared (sleep or messagebox)
		Sleep(1000);
		//Message(__FUNCTIONW__, MB_ICONWARNING);

		ReleaseSRWLockExclusive(SRWLock);

		EndThread();

		if (WAIT_OBJECT_0 != WaitForSingleObject(hEvent, INFINITE))
		{
			__debugbreak();
		}

		Message(L"Done !", MB_ICONWARNING);
	}

	void Test()
	{
		if (hEvent = CreateEventW(0, 0, 0, 0))
		{
			if (HANDLE hSection = CreateFileMappingW(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, sizeof(SRWLOCK), 0))
			{
				if (SRWLock = (PSRWLOCK)MapViewOfFile(hSection, FILE_MAP_WRITE, 0, 0, 0))
				{
					SRWLockAlt = (PSRWLOCK)MapViewOfFile(hSection, FILE_MAP_WRITE, 0, 0, 0);
				}

				CloseHandle(hSection);

				if (SRWLock)
				{
					if (SRWLockAlt)
					{
						TestInternal();

						UnmapViewOfFile(SRWLockAlt);
					}
				}

				UnmapViewOfFile(SRWLock);
			}

			CloseHandle(hEvent);
		}
	}
};

#define TRACE_FLAG 0x100

LONG NTAPI OnVex(PEXCEPTION_POINTERS ExceptionInfo)
{
	if (STATUS_SINGLE_STEP == ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
		if (MY_VEX_FRAME* frame = MY_VEX_FRAME::get())
		{
			union {
				PVOID Ptr;
				struct  
				{
					ULONG_PTR L : 1;
					ULONG_PTR W : 1;
					ULONG_PTR K : 1;
					ULONG_PTR M : 1;
					ULONG_PTR SC : sizeof(ULONG_PTR) * 8 - 4;
				};
			};

			Ptr = frame->SRWLock->Ptr;

			if (K)
			{
				ResumeThread(frame->hThread);
				// give time for second thread call AcquireSRWLockShared (sleep or messagebox)
				Sleep(1000);
				//Message(L"K bit", MB_ICONWARNING);
				ExceptionInfo->ContextRecord->Dr3 = 0;
				ExceptionInfo->ContextRecord->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
			}

			return EXCEPTION_CONTINUE_EXECUTION;
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

void WINAPI ep(PVOID Vex)
{
	if (Vex = AddVectoredExceptionHandler(TRUE, OnVex))
	{
		ThreadTestData data;
		data.Test();

		RemoveVectoredExceptionHandler(Vex);
	}

	ExitProcess(0);
}