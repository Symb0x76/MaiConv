/* Threads.c -- multithreading library
2014-09-21 : Igor Pavlov : Public domain */

#include "Precomp.h"

#ifdef _WIN32

#ifndef UNDER_CE
#include <process.h>
#endif

#else
#include <errno.h>
#include <stdlib.h>
#endif

#include "Threads.h"

#ifdef _WIN32

static WRes GetError() {
  DWORD res = GetLastError();
  return (res) ? (WRes)(res) : 1;
}

WRes HandleToWRes(HANDLE h) { return (h != 0) ? 0 : GetError(); }
WRes BOOLToWRes(BOOL v) { return v ? 0 : GetError(); }

WRes HandlePtr_Close(HANDLE *p) {
  if (*p != NULL)
    if (!CloseHandle(*p))
      return GetError();
  *p = NULL;
  return 0;
}

WRes Handle_WaitObject(HANDLE h) {
  return (WRes)WaitForSingleObject(h, INFINITE);
}

WRes Thread_Create(CThread *p, THREAD_FUNC_TYPE func, LPVOID param) {
  /* Windows Me/98/95: threadId parameter may not be NULL in
   * _beginthreadex/CreateThread functions */

#ifdef UNDER_CE

  DWORD threadId;
  *p = CreateThread(0, 0, func, param, 0, &threadId);

#else

  unsigned threadId;
  *p = (HANDLE)_beginthreadex(NULL, 0, func, param, 0, &threadId);

#endif

  /* maybe we must use errno here, but probably GetLastError() is also OK. */
  return HandleToWRes(*p);
}

WRes Event_Create(CEvent *p, BOOL manualReset, int signaled) {
  *p = CreateEvent(NULL, manualReset, (signaled ? TRUE : FALSE), NULL);
  return HandleToWRes(*p);
}

WRes Event_Set(CEvent *p) { return BOOLToWRes(SetEvent(*p)); }
WRes Event_Reset(CEvent *p) { return BOOLToWRes(ResetEvent(*p)); }

WRes ManualResetEvent_Create(CManualResetEvent *p, int signaled) {
  return Event_Create(p, TRUE, signaled);
}
WRes AutoResetEvent_Create(CAutoResetEvent *p, int signaled) {
  return Event_Create(p, FALSE, signaled);
}
WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *p) {
  return ManualResetEvent_Create(p, 0);
}
WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *p) {
  return AutoResetEvent_Create(p, 0);
}

WRes Semaphore_Create(CSemaphore *p, UInt32 initCount, UInt32 maxCount) {
  *p = CreateSemaphore(NULL, (LONG)initCount, (LONG)maxCount, NULL);
  return HandleToWRes(*p);
}

static WRes Semaphore_Release(CSemaphore *p, LONG releaseCount,
                              LONG *previousCount) {
  return BOOLToWRes(ReleaseSemaphore(*p, releaseCount, previousCount));
}
WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 num) {
  return Semaphore_Release(p, (LONG)num, NULL);
}
WRes Semaphore_Release1(CSemaphore *p) { return Semaphore_ReleaseN(p, 1); }

WRes CriticalSection_Init(CCriticalSection *p) {
  /* InitializeCriticalSection can raise only STATUS_NO_MEMORY exception */
#ifdef _MSC_VER
  __try
#endif
  {
    InitializeCriticalSection(p);
    /* InitializeCriticalSectionAndSpinCount(p, 0); */
  }
#ifdef _MSC_VER
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return 1;
  }
#endif
  return 0;
}

#else

WRes Thread_Close(CThread *p) {
  p->created = 0;
  return 0;
}

WRes Thread_Wait(CThread *p) {
  if (!p->created)
    return 0;
  return (pthread_join(p->thread, NULL) == 0) ? 0 : (WRes)errno;
}

WRes Thread_Create(CThread *p, THREAD_FUNC_TYPE func, void *param) {
  if (pthread_create(&p->thread, NULL, func, param) != 0)
    return (WRes)errno;
  p->created = 1;
  return 0;
}

static WRes Event_Create(CEvent *p, int manualReset, int signaled) {
  if (pthread_mutex_init(&p->mutex, NULL) != 0)
    return (WRes)errno;
  if (pthread_cond_init(&p->cond, NULL) != 0) {
    pthread_mutex_destroy(&p->mutex);
    return (WRes)errno;
  }
  p->created = 1;
  p->manualReset = manualReset;
  p->signaled = signaled ? 1 : 0;
  return 0;
}

WRes Event_Close(CEvent *p) {
  if (!p->created)
    return 0;
  pthread_cond_destroy(&p->cond);
  pthread_mutex_destroy(&p->mutex);
  p->created = 0;
  return 0;
}

WRes Event_Wait(CEvent *p) {
  if (!p->created)
    return 1;
  if (pthread_mutex_lock(&p->mutex) != 0)
    return (WRes)errno;
  while (!p->signaled) {
    if (pthread_cond_wait(&p->cond, &p->mutex) != 0) {
      pthread_mutex_unlock(&p->mutex);
      return (WRes)errno;
    }
  }
  if (!p->manualReset)
    p->signaled = 0;
  pthread_mutex_unlock(&p->mutex);
  return 0;
}

WRes Event_Set(CEvent *p) {
  if (!p->created)
    return 1;
  if (pthread_mutex_lock(&p->mutex) != 0)
    return (WRes)errno;
  p->signaled = 1;
  if (p->manualReset)
    pthread_cond_broadcast(&p->cond);
  else
    pthread_cond_signal(&p->cond);
  pthread_mutex_unlock(&p->mutex);
  return 0;
}

WRes Event_Reset(CEvent *p) {
  if (!p->created)
    return 1;
  if (pthread_mutex_lock(&p->mutex) != 0)
    return (WRes)errno;
  p->signaled = 0;
  pthread_mutex_unlock(&p->mutex);
  return 0;
}

WRes ManualResetEvent_Create(CManualResetEvent *p, int signaled) {
  return Event_Create(p, 1, signaled);
}

WRes AutoResetEvent_Create(CAutoResetEvent *p, int signaled) {
  return Event_Create(p, 0, signaled);
}

WRes ManualResetEvent_CreateNotSignaled(CManualResetEvent *p) {
  return ManualResetEvent_Create(p, 0);
}

WRes AutoResetEvent_CreateNotSignaled(CAutoResetEvent *p) {
  return AutoResetEvent_Create(p, 0);
}

WRes Semaphore_Close(CSemaphore *p) {
  if (!p->created)
    return 0;
  sem_destroy(&p->sem);
  p->created = 0;
  return 0;
}

WRes Semaphore_Wait(CSemaphore *p) {
  if (!p->created)
    return 1;
  while (sem_wait(&p->sem) != 0) {
    if (errno != EINTR)
      return (WRes)errno;
  }
  return 0;
}

WRes Semaphore_Create(CSemaphore *p, UInt32 initCount, UInt32 maxCount) {
  (void)maxCount;
  if (sem_init(&p->sem, 0, initCount) != 0)
    return (WRes)errno;
  p->created = 1;
  return 0;
}

WRes Semaphore_ReleaseN(CSemaphore *p, UInt32 num) {
  UInt32 i;
  if (!p->created)
    return 1;
  for (i = 0; i < num; ++i) {
    if (sem_post(&p->sem) != 0)
      return (WRes)errno;
  }
  return 0;
}

WRes Semaphore_Release1(CSemaphore *p) { return Semaphore_ReleaseN(p, 1); }

WRes CriticalSection_Init(CCriticalSection *p) {
  return (pthread_mutex_init(p, NULL) == 0) ? 0 : (WRes)errno;
}

#endif
