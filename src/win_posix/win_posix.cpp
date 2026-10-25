// POSIX calls the port uses, over Win32, for the msvc-target build (win_posix.h,
// pthread.h, dirent.h). Everything here is allowed in UWP apps.
#include "win_posix.h"
#include "pthread.h"
#include "dirent.h"

#include <windows.h>

#include <errno.h>
#include <string>

// --- time -------------------------------------------------------------------

static unsigned long long realtime_ns()
{
	FILETIME ft;
	GetSystemTimePreciseAsFileTime(&ft);
	const unsigned long long t100 = ((unsigned long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - 116444736000000000ULL;
	return t100 * 100ULL;
}

extern "C" int clock_gettime(clockid_t clk, struct timespec* ts)
{
	if (!ts) {
		errno = EINVAL;
		return -1;
	}
	if (clk == CLOCK_MONOTONIC) {
		static const long long freq = [] {
			LARGE_INTEGER f;
			QueryPerformanceFrequency(&f);
			return (long long)f.QuadPart;
		}();
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		ts->tv_sec = (time_t)(c.QuadPart / freq);
		ts->tv_nsec = (long)((c.QuadPart % freq) * 1000000000LL / freq);
		return 0;
	}
	const unsigned long long ns = realtime_ns();
	ts->tv_sec = (time_t)(ns / 1000000000ULL);
	ts->tv_nsec = (long)(ns % 1000000000ULL);
	return 0;
}

extern "C" int nanosleep(const struct timespec* req, struct timespec* rem)
{
	if (!req) {
		errno = EINVAL;
		return -1;
	}
	Sleep((DWORD)(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000));
	if (rem) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

extern "C" int usleep(useconds_t us)
{
	Sleep((us + 999) / 1000);
	return 0;
}

extern "C" unsigned int sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

// --- directories --------------------------------------------------------------

struct sms_DIR {
	HANDLE handle;
	WIN32_FIND_DATAW data;
	bool first;
	dirent entry;
};

static std::wstring widen(const char* s)
{
	const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1)
		MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
	return w;
}

extern "C" DIR* opendir(const char* path)
{
	std::wstring w = widen(path);
	for (size_t i = 0; i < w.size(); i++)
		if (w[i] == L'/')
			w[i] = L'\\';
	if (w.empty() || w.back() != L'\\')
		w += L'\\';
	w += L'*';
	sms_DIR* d = new sms_DIR;
	d->handle = FindFirstFileExW(w.c_str(), FindExInfoBasic, &d->data, FindExSearchNameMatch, nullptr, 0);
	if (d->handle == INVALID_HANDLE_VALUE) {
		delete d;
		errno = ENOENT;
		return nullptr;
	}
	d->first = true;
	return d;
}

extern "C" struct dirent* readdir(DIR* d)
{
	if (d->first)
		d->first = false;
	else if (!FindNextFileW(d->handle, &d->data))
		return nullptr;
	WideCharToMultiByte(CP_UTF8, 0, d->data.cFileName, -1, d->entry.d_name, (int)sizeof d->entry.d_name, nullptr, nullptr);
	d->entry.d_type = (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
	return &d->entry;
}

extern "C" int closedir(DIR* d)
{
	FindClose(d->handle);
	delete d;
	return 0;
}

// --- threads ----------------------------------------------------------------

namespace {

// Two owners at first, the thread and whoever joins or detaches it; the last to let go frees it.
struct Thread {
	HANDLE handle;
	void* result;
	void* (*start)(void*);
	void* arg;
	LONG refs;
};

thread_local Thread* t_self;

void release(Thread* t)
{
	if (InterlockedDecrement(&t->refs) == 0) {
		if (t->handle)
			CloseHandle(t->handle);
		delete t;
	}
}

DWORD WINAPI thread_main(LPVOID p)
{
	Thread* t = (Thread*)p;
	t_self = t;
	t->result = t->start(t->arg);
	release(t);
	return 0;
}

}  // namespace

extern "C" int pthread_attr_init(pthread_attr_t* a)
{
	a->stacksize = 0;
	a->stackaddr = nullptr;
	a->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

extern "C" int pthread_attr_destroy(pthread_attr_t*) { return 0; }

extern "C" int pthread_attr_setstacksize(pthread_attr_t* a, size_t size)
{
	a->stacksize = size;
	return 0;
}

extern "C" int pthread_attr_setstack(pthread_attr_t* a, void* addr, size_t size)
{
	a->stackaddr = addr;
	a->stacksize = size;
	return 0;
}

extern "C" int pthread_attr_setdetachstate(pthread_attr_t* a, int state)
{
	a->detachstate = state;
	return 0;
}

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg)
{
	Thread* t = new Thread;
	t->result = nullptr;
	t->start = start;
	t->arg = arg;
	t->refs = 2;
	t->handle = CreateThread(nullptr, attr ? attr->stacksize : 0, thread_main, t, 0, nullptr);
	if (!t->handle) {
		delete t;
		return EAGAIN;
	}
	*thread = (pthread_t)t;
	if (attr && attr->detachstate == PTHREAD_CREATE_DETACHED)
		release(t);
	return 0;
}

extern "C" int pthread_join(pthread_t thread, void** result)
{
	Thread* t = (Thread*)thread;
	WaitForSingleObject(t->handle, INFINITE);
	if (result)
		*result = t->result;
	release(t);
	return 0;
}

extern "C" int pthread_detach(pthread_t thread)
{
	release((Thread*)thread);
	return 0;
}

extern "C" pthread_t pthread_self(void)
{
	if (!t_self) {
		// a thread this layer did not create (the main thread): never freed
		t_self = new Thread();
		t_self->refs = 1;
	}
	return (pthread_t)t_self;
}

extern "C" int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

extern "C" void pthread_exit(void* result)
{
	if (t_self) {
		t_self->result = result;
		Thread* t = t_self;
		t_self = nullptr;
		release(t);
	}
	ExitThread(0);
}

// --- mutexes and condition variables -------------------------------------------

static_assert(sizeof(pthread_mutex_t) == sizeof(SRWLOCK), "SRWLOCK is one pointer");
static_assert(sizeof(pthread_cond_t) == sizeof(CONDITION_VARIABLE), "CONDITION_VARIABLE is one pointer");

static SRWLOCK* srw(pthread_mutex_t* m) { return (SRWLOCK*)m; }
static CONDITION_VARIABLE* cv(pthread_cond_t* c) { return (CONDITION_VARIABLE*)c; }

extern "C" int pthread_mutex_init(pthread_mutex_t* m, const void*)
{
	InitializeSRWLock(srw(m));
	return 0;
}

extern "C" int pthread_mutex_destroy(pthread_mutex_t*) { return 0; }

extern "C" int pthread_mutex_lock(pthread_mutex_t* m)
{
	AcquireSRWLockExclusive(srw(m));
	return 0;
}

extern "C" int pthread_mutex_trylock(pthread_mutex_t* m)
{
	return TryAcquireSRWLockExclusive(srw(m)) ? 0 : EBUSY;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t* m)
{
	ReleaseSRWLockExclusive(srw(m));
	return 0;
}

extern "C" int pthread_cond_init(pthread_cond_t* c, const void*)
{
	InitializeConditionVariable(cv(c));
	return 0;
}

extern "C" int pthread_cond_destroy(pthread_cond_t*) { return 0; }

extern "C" int pthread_cond_signal(pthread_cond_t* c)
{
	WakeConditionVariable(cv(c));
	return 0;
}

extern "C" int pthread_cond_broadcast(pthread_cond_t* c)
{
	WakeAllConditionVariable(cv(c));
	return 0;
}

extern "C" int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m)
{
	SleepConditionVariableSRW(cv(c), srw(m), INFINITE, 0);
	return 0;
}

extern "C" int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* abstime)
{
	const long long target = (long long)abstime->tv_sec * 1000000000LL + abstime->tv_nsec;
	const long long left = target - (long long)realtime_ns();
	const DWORD ms = left <= 0 ? 0 : (DWORD)((left + 999999) / 1000000);
	if (!SleepConditionVariableSRW(cv(c), srw(m), ms, 0))
		return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
	return 0;
}
