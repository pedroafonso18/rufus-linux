/*
 * Rufus: The Reliable USB Formatting Utility
 * Windows I/O redefinitions - POSIX implementation.
 * Copyright © 2021-2026 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The Windows implementation of winio.h uses overlapped I/O so that a read
 * can proceed while the previous buffer is being written to the target
 * device. The POSIX version reproduces that behaviour with a dedicated
 * worker thread per handle.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "windows.h"
#include "win_compat.h"
#include "msapi_utf8_posix.h"

#pragma once

#define ASYNC_FD_MAX_PENDING 4

/* The Windows implementation uses this structure for its overlapped I/O. */
typedef struct {
	ULONG_PTR Internal[2];
	ULONG64 Offset;
	HANDLE hEvent;
	BOOL bOffsetUpdated;
} NOW_THATS_WHAT_I_CALL_AN_OVERLAPPED;

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	void* buffer;
	size_t requested;
	size_t transferred;
	int write;
	int done;
	int error;
	int queued;
} ASYNC_OP;

typedef struct {
	HANDLE hFile;
	int fd;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t thread;
	int thread_started;
	int stop;
	off_t offset;
	ASYNC_OP ops[ASYNC_FD_MAX_PENDING];
	int head;
	int tail;
	int count;
} ASYNC_FD;

static void* async_worker(void* param)
{
	ASYNC_FD* fd = (ASYNC_FD*)param;

	pthread_mutex_lock(&fd->mutex);
	for (;;) {
		ASYNC_OP* op;
		while (fd->count == 0 && !fd->stop)
			pthread_cond_wait(&fd->cond, &fd->mutex);
		if (fd->stop && fd->count == 0)
			break;
		op = &fd->ops[fd->head];
		pthread_mutex_unlock(&fd->mutex);

		{
			size_t done = 0;
			int err = 0;
			while (done < op->requested) {
				ssize_t r;
				if (op->write)
					r = pwrite(fd->fd, (const uint8_t*)op->buffer + done, op->requested - done, fd->offset);
				else
					r = pread(fd->fd, (uint8_t*)op->buffer + done, op->requested - done, fd->offset);
				if (r < 0) {
					if (errno == EINTR)
						continue;
					err = errno;
					break;
				}
				if (r == 0)
					break;
				done += (size_t)r;
				fd->offset += r;
			}
			pthread_mutex_lock(&fd->mutex);
			op->transferred = done;
			op->error = err;
			op->done = 1;
			pthread_cond_broadcast(&fd->cond);
		}
	}
	pthread_mutex_unlock(&fd->mutex);
	return NULL;
}

static __inline HANDLE CreateFileAsync(LPCSTR lpFileName, DWORD dwDesiredAccess,
	DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes)
{
	ASYNC_FD* fd = (ASYNC_FD*)calloc(1, sizeof(ASYNC_FD));
	pthread_attr_t attr;

	if (fd == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	fd->hFile = CreateFileU(lpFileName, dwDesiredAccess, dwShareMode, NULL,
		dwCreationDisposition, dwFlagsAndAttributes, NULL);
	if (fd->hFile == INVALID_HANDLE_VALUE) {
		free(fd);
		return NULL;
	}
	fd->fd = win_handle_to_fd(fd->hFile);
	pthread_mutex_init(&fd->mutex, NULL);
	pthread_cond_init(&fd->cond, NULL);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&fd->thread, &attr, async_worker, fd) != 0) {
		pthread_attr_destroy(&attr);
		CloseHandle(fd->hFile);
		free(fd);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	pthread_attr_destroy(&attr);
	fd->thread_started = 1;
	return (HANDLE)fd;
}

static __inline VOID CloseFileAsync(HANDLE h)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	if (fd == NULL || (HANDLE)fd == INVALID_HANDLE_VALUE)
		return;
	pthread_mutex_lock(&fd->mutex);
	fd->stop = 1;
	pthread_cond_broadcast(&fd->cond);
	pthread_mutex_unlock(&fd->mutex);
	if (fd->thread_started)
		pthread_join(fd->thread, NULL);
	if (fd->hFile != NULL && fd->hFile != INVALID_HANDLE_VALUE)
		CloseHandle(fd->hFile);
	pthread_mutex_destroy(&fd->mutex);
	pthread_cond_destroy(&fd->cond);
	free(fd);
}

static __inline BOOL async_submit(ASYNC_FD* fd, LPVOID lpBuffer, DWORD n, int write)
{
	if (fd == NULL || fd->count >= ASYNC_FD_MAX_PENDING) {
		SetLastError(ERROR_IO_PENDING);
		return FALSE;
	}
	pthread_mutex_lock(&fd->mutex);
	{
		ASYNC_OP* op = &fd->ops[fd->tail];
		op->buffer = lpBuffer;
		op->requested = n;
		op->transferred = 0;
		op->error = 0;
		op->done = 0;
		op->write = write;
		op->queued = 1;
		fd->tail = (fd->tail + 1) % ASYNC_FD_MAX_PENDING;
		fd->count++;
	}
	pthread_cond_broadcast(&fd->cond);
	pthread_mutex_unlock(&fd->mutex);
	return TRUE;
}

static __inline BOOL ReadFileAsync(HANDLE h, LPVOID lpBuffer, DWORD nNumberOfBytesToRead)
{
	return async_submit((ASYNC_FD*)h, lpBuffer, nNumberOfBytesToRead, 0);
}

static __inline BOOL WriteFileAsync(HANDLE h, LPVOID lpBuffer, DWORD nNumberOfBytesToWrite)
{
	return async_submit((ASYNC_FD*)h, lpBuffer, nNumberOfBytesToWrite, 1);
}

static __inline BOOL WaitFileAsync(HANDLE h, DWORD dwTimeout)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	int rc = 0;

	if (fd == NULL)
		return FALSE;
	pthread_mutex_lock(&fd->mutex);
	while (fd->count == 0) {
		struct timespec ts;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		ts.tv_sec = tv.tv_sec + (time_t)(dwTimeout / 1000);
		ts.tv_nsec = tv.tv_usec * 1000 + (long)(dwTimeout % 1000) * 1000000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		rc = pthread_cond_timedwait(&fd->cond, &fd->mutex, &ts);
		if (rc == ETIMEDOUT)
			break;
	}
	if (fd->count == 0) {
		pthread_mutex_unlock(&fd->mutex);
		SetLastError(ERROR_IO_PENDING);
		return FALSE;
	}
	while (!fd->ops[fd->head].done) {
		struct timespec ts;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		ts.tv_sec = tv.tv_sec + (time_t)(dwTimeout / 1000);
		ts.tv_nsec = tv.tv_usec * 1000 + (long)(dwTimeout % 1000) * 1000000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		rc = pthread_cond_timedwait(&fd->cond, &fd->mutex, &ts);
		if (rc == ETIMEDOUT)
			break;
	}
	rc = fd->ops[fd->head].done;
	pthread_mutex_unlock(&fd->mutex);
	if (!rc) {
		SetLastError(ERROR_IO_PENDING);
		return FALSE;
	}
	return TRUE;
}

static __inline BOOL GetSizeAsync(HANDLE h, LPDWORD lpNumberOfBytes)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	ASYNC_OP* op;

	if (fd == NULL)
		return FALSE;
	pthread_mutex_lock(&fd->mutex);
	if (fd->count == 0) {
		pthread_mutex_unlock(&fd->mutex);
		SetLastError(ERROR_NO_MORE_ITEMS);
		return FALSE;
	}
	op = &fd->ops[fd->head];
	if (!op->done) {
		pthread_mutex_unlock(&fd->mutex);
		SetLastError(ERROR_IO_INCOMPLETE);
		return FALSE;
	}
	if (op->error != 0) {
		fd->head = (fd->head + 1) % ASYNC_FD_MAX_PENDING;
		fd->count--;
		pthread_mutex_unlock(&fd->mutex);
		SetLastError(ERROR_READ_FAULT);
		return FALSE;
	}
	if (lpNumberOfBytes != NULL)
		*lpNumberOfBytes = (DWORD)op->transferred;
	fd->head = (fd->head + 1) % ASYNC_FD_MAX_PENDING;
	fd->count--;
	pthread_mutex_unlock(&fd->mutex);
	return TRUE;
}
