/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - compression library.
 * Copyright © 2011-2026 Pete Batard <pete@akeo.ie>
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
 * The Windows build of Rufus uses the vendored BLED library to decompress
 * disk images and ZIP files. On Linux, we use the standard libraries
 * (zlib, liblzma and libzstd) instead, and the "unzip" command for ZIP
 * archives.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <lzma.h>
#include <zlib.h>
#include <zstd.h>

#include "linux_api.h"
#include "linux_headers.h"
#include "bled/bled.h"
#include "posix/win_compat.h"

static uint32_t bled_buffer_size = 256 * 1024;
static progress_t bled_progress_function = NULL;
static switch_t bled_switch_function = NULL;
static unsigned long* bled_cancel_request = NULL;

int bled_init(uint32_t buffer_size, printf_t print_function, read_t read_function, write_t write_function,
	progress_t progress_function, switch_t switch_function, unsigned long* cancel_request)
{
	(void)print_function;
	(void)read_function;
	(void)write_function;
	if (buffer_size >= 64 * 1024)
		bled_buffer_size = buffer_size;
	bled_progress_function = progress_function;
	bled_switch_function = switch_function;
	bled_cancel_request = cancel_request;
	return 0;
}

void bled_exit(void)
{
	bled_progress_function = NULL;
	bled_switch_function = NULL;
	bled_cancel_request = NULL;
}

static BOOL bled_cancelled(void)
{
	return (bled_cancel_request != NULL) && (*bled_cancel_request != 0);
}

/*
 * Decompress a gzip stream into a file.
 */
static int64_t gunzip_to_file(const char* src, const char* dst)
{
	gzFile in;
	FILE* out;
	uint8_t* buffer;
	int64_t total = 0;

	in = gzopen(src, "rb");
	if (in == NULL)
		return -1;
	out = fopen(dst, "wb");
	if (out == NULL) {
		gzclose(in);
		return -1;
	}
	buffer = (uint8_t*)malloc(bled_buffer_size);
	if (buffer == NULL) {
		gzclose(in);
		fclose(out);
		return -1;
	}
	for (;;) {
		int n = gzread(in, buffer, (unsigned int)bled_buffer_size);
		if (n < 0) {
			free(buffer);
			gzclose(in);
			fclose(out);
			return -1;
		}
		if (n == 0)
			break;
		if (fwrite(buffer, 1, (size_t)n, out) != (size_t)n) {
			free(buffer);
			gzclose(in);
			fclose(out);
			return -1;
		}
		total += n;
		if (bled_progress_function != NULL)
			bled_progress_function(total);
		if (bled_cancelled())
			break;
	}
	free(buffer);
	gzclose(in);
	fclose(out);
	return total;
}

/*
 * Decompress an xz or lzma stream into a file.
 */
static int64_t lzma_to_file(const char* src, const char* dst, int type)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	FILE* in;
	FILE* out;
	uint8_t* buffer;
	int64_t total = 0;
	lzma_ret ret;

	in = fopen(src, "rb");
	if (in == NULL)
		return -1;
	out = fopen(dst, "wb");
	if (out == NULL) {
		fclose(in);
		return -1;
	}
	buffer = (uint8_t*)malloc(bled_buffer_size);
	if (buffer == NULL) {
		fclose(in);
		fclose(out);
		return -1;
	}
	if (type == BLED_COMPRESSION_LZMA)
		ret = lzma_alone_decoder(&strm, UINT64_MAX);
	else
		ret = lzma_stream_decoder(&strm, UINT64_MAX, 0);
	if (ret != LZMA_OK) {
		free(buffer);
		fclose(in);
		fclose(out);
		return -1;
	}
	strm.next_in = NULL;
	strm.avail_in = 0;
	for (;;) {
		size_t in_size, out_size;
		if (bled_cancelled())
			break;
		if (strm.avail_in == 0) {
			in_size = fread(buffer, 1, bled_buffer_size, in);
			if (in_size == 0)
				break;
			strm.next_in = buffer;
			strm.avail_in = in_size;
		}
		strm.next_out = buffer;
		strm.avail_out = bled_buffer_size;
		ret = lzma_code(&strm, LZMA_RUN);
		out_size = bled_buffer_size - strm.avail_out;
		if (out_size > 0) {
			if (fwrite(buffer, 1, out_size, out) != out_size)
				break;
			total += (int64_t)out_size;
			if (bled_progress_function != NULL)
				bled_progress_function(total);
		}
		if (ret == LZMA_STREAM_END)
			break;
		if (ret != LZMA_OK)
			break;
	}
	lzma_end(&strm);
	free(buffer);
	fclose(in);
	fclose(out);
	return total;
}

/*
 * Decompress a zstd stream into a file.
 */
static int64_t zstd_to_file(const char* src, const char* dst)
{
	ZSTD_DStream* stream;
	FILE* in;
	FILE* out;
	uint8_t* buffer;
	size_t in_size, out_size;
	int64_t total = 0;
	int ret;

	in = fopen(src, "rb");
	if (in == NULL)
		return -1;
	out = fopen(dst, "wb");
	if (out == NULL) {
		fclose(in);
		return -1;
	}
	stream = ZSTD_createDStream();
	if (stream == NULL) {
		fclose(in);
		fclose(out);
		return -1;
	}
	ZSTD_initDStream(stream);
	buffer = (uint8_t*)malloc(ZSTD_DStreamOutSize());
	if (buffer == NULL) {
		ZSTD_freeDStream(stream);
		fclose(in);
		fclose(out);
		return -1;
	}
	while (!bled_cancelled()) {
		in_size = fread(buffer, 1, ZSTD_DStreamInSize(), in);
		if (in_size == 0)
			break;
		ZSTD_inBuffer input = { buffer, in_size, 0 };
		while (input.pos < input.size) {
			ZSTD_outBuffer output = { buffer, ZSTD_DStreamOutSize(), 0 };
			ret = ZSTD_decompressStream(stream, &output, &input);
			if (ZSTD_isError(ret)) {
				free(buffer);
				ZSTD_freeDStream(stream);
				fclose(in);
				fclose(out);
				return -1;
			}
			out_size = output.pos;
			if (out_size > 0) {
				if (fwrite(buffer, 1, out_size, out) != out_size) {
					free(buffer);
					ZSTD_freeDStream(stream);
					fclose(in);
					fclose(out);
					return -1;
				}
				total += (int64_t)out_size;
				if (bled_progress_function != NULL)
					bled_progress_function(total);
			}
			if (ret == 0)
				break;
		}
	}
	free(buffer);
	ZSTD_freeDStream(stream);
	fclose(in);
	fclose(out);
	return total;
}

/*
 * Run an external decompression command (used for bzip2).
 */
static int64_t run_decompressor(const char* command, const char* src, const char* dst)
{
	pid_t pid;
	int status;
	char* argv[6];

	argv[0] = (char*)command;
	argv[1] = (char*)"-d";
	argv[2] = (char*)"-c";
	argv[3] = (char*)src;
	argv[4] = NULL;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		FILE* out;
		int fd;
		if (dst != NULL) {
			out = fopen(dst, "wb");
			if (out == NULL)
				_exit(127);
			fd = fileno(out);
			dup2(fd, STDOUT_FILENO);
			fclose(out);
		} else {
			dup2(STDOUT_FILENO, STDOUT_FILENO);
		}
		execvp(command, argv);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	return -1;
}

int64_t bled_uncompress(const char* src, const char* dst, int type)
{
	switch (type) {
	case BLED_COMPRESSION_GZIP:
		return gunzip_to_file(src, dst);
	case BLED_COMPRESSION_XZ:
		return lzma_to_file(src, dst, type);
	case BLED_COMPRESSION_LZMA:
		return lzma_to_file(src, dst, type);
	case BLED_COMPRESSION_ZSTD:
		return zstd_to_file(src, dst);
	case BLED_COMPRESSION_BZIP2:
		return run_decompressor("bzip2", src, dst);
	default:
		return -1;
	}
}

int64_t bled_get_uncompressed_size(const char* src, int type)
{
	FILE* f;
	int64_t size = -1;

	switch (type) {
	case BLED_COMPRESSION_GZIP: {
		gzFile in = gzopen(src, "rb");
		if (in == NULL)
			return -1;
		/* The uncompressed size is stored in the last 4 bytes of the file */
		f = fopen(src, "rb");
		if (f != NULL) {
			uint32_t value = 0;
			if (fseek(f, -4, SEEK_END) == 0 && fread(&value, 1, 4, f) == 4)
				size = (int64_t)value;
			fclose(f);
		}
		gzclose(in);
		return size;
	}
	case BLED_COMPRESSION_ZSTD: {
		ZSTD_DStream* stream = ZSTD_createDStream();
		if (stream == NULL)
			return -1;
		size = (int64_t)ZSTD_getFrameContentSize(src, ZSTD_CONTENTSIZE_UNKNOWN);
		if (size == (int64_t)ZSTD_CONTENTSIZE_UNKNOWN || size == (int64_t)ZSTD_CONTENTSIZE_ERROR)
			size = -1;
		ZSTD_freeDStream(stream);
		return size;
	}
	default:
		return -1;
	}
}

int64_t bled_uncompress_to_buffer(const char* src, char* buf, size_t size, int type)
{
	char* tmp;
	int64_t written;
	FILE* f;

	(void)type;
	tmp = strdup(src);
	if (tmp == NULL)
		return -1;
	/* Decompress to a temporary file, then copy it over */
	{
		char* tmpfile = strdup("/tmp/rufus-bled-XXXXXX");
		int fd;
		if (tmpfile == NULL) {
			free(tmp);
			return -1;
		}
		fd = mkstemp(tmpfile);
		if (fd < 0) {
			free(tmpfile);
			free(tmp);
			return -1;
		}
		close(fd);
		written = bled_uncompress(src, tmpfile, type);
		if (written < 0) {
			unlink(tmpfile);
			free(tmpfile);
			free(tmp);
			return -1;
		}
		f = fopen(tmpfile, "rb");
		if (f != NULL) {
			size_t n = fread(buf, 1, size, f);
			fclose(f);
			written = (int64_t)n;
		}
		unlink(tmpfile);
		free(tmpfile);
	}
	free(tmp);
	return written;
}

int64_t bled_uncompress_from_buffer_to_buffer(const char* src, const size_t src_len, char* dst,
	const size_t dst_len, int type)
{
	(void)src; (void)src_len; (void)dst; (void)dst_len; (void)type;
	return -1;
}

int64_t bled_uncompress_with_handles(HANDLE hSrc, HANDLE hDst, int type)
{
	(void)hSrc; (void)hDst; (void)type;
	return -1;
}

/*
 * Extract an archive to a directory. ZIP archives are handled by the
 * "unzip" command, which is the standard tool on Linux.
 */
int64_t bled_uncompress_to_dir(const char* src, const char* dir, int type)
{
	pid_t pid;
	int status;
	char* argv[8];
	int argc = 0;

	if (type != BLED_COMPRESSION_ZIP)
		return -1;

	argv[argc++] = (char*)"unzip";
	argv[argc++] = (char*)"-o";
	argv[argc++] = (char*)"-q";
	argv[argc++] = (char*)"-qq";
	argv[argc++] = (char*)src;
	argv[argc++] = (char*)"-d";
	argv[argc++] = (char*)dir;
	argv[argc] = NULL;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execvp("unzip", argv);
		_exit(127);
	}
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	return -1;
}
