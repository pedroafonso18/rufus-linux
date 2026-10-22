/*
 * unix_io.c --- POSIX I/O interface to the ext2fs I/O manager.
 *
 * This is the Linux counterpart of the Windows NT I/O backend (nt_io.c) that
 * Rufus ships. It exposes the same ext2fs "io_manager" interface, using
 * pread()/pwrite() on a block device or a regular file.
 *
 * Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
 * Copyright (C) 2018-2026 Pete Batard <pete@akeo.ie>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/mount.h>
#endif

#include "config.h"
#include "ext2fs.h"

/* Win32 error codes, as used by the Rufus front-end */
#ifdef __linux__
#include "posix/windows.h"
#endif

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)
#endif
#ifndef BLKSSZGET
#define BLKSSZGET _IOR(0x12, 104, int)
#endif
#ifndef BLKBSZGET
#define BLKBSZGET _IOR(0x12, 123, int)
#endif

/* Private data block */
typedef struct _UNIX_PRIVATE_DATA {
	int magic;
	int fd;
	int flags;
	char* buffer;
	unsigned int buffer_block_number;
	unsigned long buffer_size;
	int read_only;
	int written;
	/* Used by Rufus */
	unsigned long long offset;
	unsigned long long size;
} UNIX_PRIVATE_DATA, *PUNIX_PRIVATE_DATA;

static errcode_t unix_open(const char* name, int flags, io_channel* channel);
static errcode_t unix_close(io_channel channel);
static errcode_t unix_set_blksize(io_channel channel, int blksize);
static errcode_t unix_read_blk(io_channel channel, unsigned long block, int count, void* data);
static errcode_t unix_read_blk64(io_channel channel, unsigned long long block, int count, void* data);
static errcode_t unix_write_blk(io_channel channel, unsigned long block, int count, const void* data);
static errcode_t unix_write_blk64(io_channel channel, unsigned long long block, int count, const void* data);
static errcode_t unix_flush(io_channel channel);

struct struct_io_manager struct_unix_manager = {
	.magic		= EXT2_ET_MAGIC_IO_MANAGER,
	.name		= "Unix I/O Manager",
	.open		= unix_open,
	.close		= unix_close,
	.set_blksize	= unix_set_blksize,
	.read_blk	= unix_read_blk,
	.read_blk64	= unix_read_blk64,
	.write_blk	= unix_write_blk,
	.write_blk64	= unix_write_blk64,
	.flush		= unix_flush
};

io_manager unix_io_manager = &struct_unix_manager;

static void unix_set_private_data(io_channel channel, PUNIX_PRIVATE_DATA data)
{
	channel->private_data = data;
}

/*
 * Determine the size of a device or file, in bytes.
 */
static errcode_t unix_get_size(int fd, unsigned long long* size)
{
	struct stat st;
	off_t off;

	if (fstat(fd, &st) < 0)
		return errno;
	if (S_ISBLK(st.st_mode)) {
#ifdef BLKGETSIZE64
		size_t bytes = 0;
		if (ioctl(fd, BLKGETSIZE64, &bytes) == 0) {
			*size = bytes;
			return 0;
		}
#endif
		off = lseek(fd, 0, SEEK_END);
		if (off == (off_t)-1)
			return errno;
		*size = (unsigned long long)off;
		return 0;
	}
	*size = (unsigned long long)st.st_size;
	return 0;
}

static errcode_t unix_open(const char* name, int flags, io_channel* channel)
{
	PUNIX_PRIVATE_DATA data;
	int fd = -1;
	int open_flags;
	unsigned long long size = 0;

	if (!name || !channel)
		return EINVAL;

	data = (PUNIX_PRIVATE_DATA)calloc(1, sizeof(UNIX_PRIVATE_DATA));
	if (!data)
		return ENOMEM;
	data->magic = EXT2_ET_MAGIC_UNIX_IO_CHANNEL;
	data->flags = flags;
	data->buffer_block_number = 0xffffffff;
	data->read_only = ((flags & IO_FLAG_RW) == 0);

	open_flags = data->read_only ? O_RDONLY : O_RDWR;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECT
	if (flags & IO_FLAG_DIRECT_IO)
		open_flags |= O_DIRECT;
#endif

	fd = open(name, open_flags);
	if (fd < 0 && !data->read_only) {
		/* Fall back to read-only access (e.g. when the device is mounted
		 * read-only or we lack the necessary privileges). */
		fd = open(name, O_RDONLY | O_CLOEXEC);
		if (fd >= 0)
			data->read_only = 1;
	}
	if (fd < 0) {
		errcode_t err = errno;
		free(data);
		return err;
	}

	if (unix_get_size(fd, &size) < 0)
		size = 0;
	data->size = size;

	data->fd = fd;
	*channel = (io_channel)calloc(1, sizeof(struct struct_io_channel));
	if (!*channel) {
		close(fd);
		free(data);
		return ENOMEM;
	}
	(*channel)->magic = EXT2_ET_MAGIC_IO_CHANNEL;
	(*channel)->manager = unix_io_manager;
	(*channel)->name = strdup(name);
	(*channel)->block_size = 1024;
	(*channel)->refcount = 1;
	(*channel)->flags = 0;
	unix_set_private_data(*channel, data);
	return 0;
}

static errcode_t unix_close(io_channel channel)
{
	PUNIX_PRIVATE_DATA data;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (PUNIX_PRIVATE_DATA)channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (data->buffer)
		free(data->buffer);
	if (data->fd >= 0)
		close(data->fd);
	free(data);
	if (channel->name)
		free(channel->name);
	free(channel);
	return 0;
}

static errcode_t unix_set_blksize(io_channel channel, int blksize)
{
	PUNIX_PRIVATE_DATA data;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (PUNIX_PRIVATE_DATA)channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (blksize <= 0)
		return EINVAL;
	if (data->buffer) {
		free(data->buffer);
		data->buffer = NULL;
	}
	data->buffer_size = blksize * 64;
	data->buffer = malloc(data->buffer_size);
	if (!data->buffer) {
		data->buffer_size = 0;
		return ENOMEM;
	}
	data->buffer_block_number = 0xffffffff;
	channel->block_size = blksize;
	return 0;
}

static errcode_t unix_read_blk64(io_channel channel, unsigned long long block, int count, void* buf)
{
	PUNIX_PRIVATE_DATA data;
	unsigned long long offset;
	ssize_t n;
	size_t size;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (PUNIX_PRIVATE_DATA)channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (count < 0)
		return EINVAL;
	size = (count == 1) ? channel->block_size : (size_t)count * channel->block_size;
	offset = block * channel->block_size + data->offset;

	do {
		n = pread(data->fd, buf, size, (off_t)offset);
	} while (n < 0 && errno == EINTR);
	if (n < 0) {
		errcode_t err = errno;
		if (channel->read_error)
			return (channel->read_error)(channel, (unsigned long)block, count, buf, size, 0, err);
		return err;
	}
	if ((size_t)n != size) {
		/* Short read: zero-fill the remainder, as the Windows backend does */
		memset((char*)buf + n, 0, size - (size_t)n);
	}
	if (data->buffer && size >= data->buffer_size) {
		data->buffer_block_number = (unsigned int)block;
		memcpy(data->buffer, buf, data->buffer_size);
	}
	return 0;
}

static errcode_t unix_read_blk(io_channel channel, unsigned long block, int count, void* buf)
{
	return unix_read_blk64(channel, block, count, buf);
}

static errcode_t unix_write_blk64(io_channel channel, unsigned long long block, int count, const void* buf)
{
	PUNIX_PRIVATE_DATA data;
	unsigned long long offset;
	ssize_t n;
	size_t size;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (PUNIX_PRIVATE_DATA)channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (data->read_only)
		return EPERM;
	if (count < 0)
		return EINVAL;
	size = (count == 1) ? channel->block_size : (size_t)count * channel->block_size;
	offset = block * channel->block_size + data->offset;

	do {
		n = pwrite(data->fd, buf, size, (off_t)offset);
	} while (n < 0 && errno == EINTR);
	if (n < 0) {
		errcode_t err = errno;
		if (channel->write_error)
			return (channel->write_error)(channel, (unsigned long)block, count, buf, size, 0, err);
		return err;
	}
	if ((size_t)n != size) {
		errcode_t err = ENOSPC;
		if (channel->write_error)
			return (channel->write_error)(channel, (unsigned long)block, count, buf, size, (int)n, err);
		return err;
	}
	if (data->buffer && size >= data->buffer_size) {
		data->buffer_block_number = (unsigned int)block;
		memcpy(data->buffer, buf, data->buffer_size);
	}
	data->written = 1;
	return 0;
}

static errcode_t unix_write_blk(io_channel channel, unsigned long block, int count, const void* buf)
{
	return unix_write_blk64(channel, block, count, buf);
}

static errcode_t unix_flush(io_channel channel)
{
	PUNIX_PRIVATE_DATA data;

	EXT2_CHECK_MAGIC(channel, EXT2_ET_MAGIC_IO_CHANNEL);
	data = (PUNIX_PRIVATE_DATA)channel->private_data;
	EXT2_CHECK_MAGIC(data, EXT2_ET_MAGIC_UNIX_IO_CHANNEL);

	if (data->read_only)
		return 0;
	if (fsync(data->fd) < 0 && errno != EINVAL && errno != EROFS)
		return errno;
	return 0;
}

/*
 * Helper functions used by the Rufus front-end.
 */

errcode_t ext2fs_get_device_size2(const char* file, int blocksize, blk64_t* retblocks)
{
	int fd;
	unsigned long long size = 0;

	if (!file || !retblocks)
		return EINVAL;
	fd = open(file, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return errno;
	if (unix_get_size(fd, &size) < 0) {
		errcode_t err = errno;
		close(fd);
		return err;
	}
	close(fd);
	if (blocksize <= 0)
		blocksize = 1024;
	*retblocks = size / blocksize;
	return 0;
}

/*
 * Check whether the given device is mounted. On Linux we look the device up
 * in /proc/self/mountinfo (which is more reliable than /etc/mtab, as it also
 * covers the devices that are not listed there).
 */
errcode_t ext2fs_check_if_mounted(const char* file, int* mount_flags)
{
	FILE* f;
	char line[4096];
	char device[1024];
	int found = 0;

	if (!file || !mount_flags)
		return EINVAL;
	*mount_flags = 0;

	f = fopen("/proc/self/mountinfo", "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		/* mountinfo format: id parent major:minor root mountpoint ... */
		char* p = strchr(line, ' ');
		char* mountpoint;
		if (p == NULL)
			continue;
		p++;
		/* skip parent */
		p = strchr(p, ' ');
		if (p == NULL)
			continue;
		p++;
		/* major:minor */
		p = strchr(p, ' ');
		if (p == NULL)
			continue;
		p++;
		/* root */
		p = strchr(p, ' ');
		if (p == NULL)
			continue;
		p++;
		mountpoint = p;
		p = strchr(p, ' ');
		if (p != NULL)
			*p = 0;
		/* Compare the device node of the mount point with our device */
		{
			struct stat st1, st2;
			if (stat(mountpoint, &st1) != 0 || stat(file, &st2) != 0)
				continue;
			if (st1.st_dev == st2.st_dev) {
				found = 1;
				break;
			}
		}
	}
	fclose(f);
	if (found)
		*mount_flags |= EXT2_MF_MOUNTED;
	(void)device;
	return 0;
}

errcode_t ext2fs_check_mount_point(const char* file, int* mount_flags, char* mtpt, int mtlen)
{
	if (!file || !mount_flags)
		return EINVAL;
	*mount_flags = 0;
	if (mtpt && mtlen > 0)
		mtpt[0] = 0;
	return ext2fs_check_if_mounted(file, mount_flags);
}

/*
 * Map the last POSIX error to the equivalent Win32 error code. This is the
 * POSIX counterpart of the function of the same name found in the Windows
 * NT I/O backend.
 */
DWORD ext2_last_winerror(DWORD default_error)
{
	switch (errno) {
	case EACCES:
	case EPERM:
		return ERROR_ACCESS_DENIED;
	case ENOSPC:
		return ERROR_DISK_FULL;
	case EROFS:
		return ERROR_WRITE_PROTECT;
	case ENOENT:
		return ERROR_FILE_NOT_FOUND;
	case EBUSY:
		return ERROR_BUSY;
	case EINVAL:
		return ERROR_INVALID_PARAMETER;
	case ENOMEM:
		return ERROR_NOT_ENOUGH_MEMORY;
	case EIO:
		return ERROR_READ_FAULT;
	default:
		return default_error;
	}
}
