/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - file system creation.
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
 * On Linux, the file systems are created with the standard tools:
 *
 *   FAT12/16/32 : mkfs.vfat  (dosfstools)
 *   exFAT        : mkfs.exfat (exfatprogs)
 *   NTFS         : mkfs.ntfs  (ntfs-3g)
 *   UDF          : mkfs.udf   (udftools)
 *   ext2/3/4     : the vendored copy of e2fsprogs (see format_ext.c)
 *
 * The ext file systems are created in-process, using the same code as the
 * Windows build, so that they behave identically.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linux_api.h"
#include "linux_headers.h"
#include "posix/win_compat.h"

/* The file system labels used by Rufus */
extern const char* FileSystemLabel[FS_MAX];

/*
 * Run a command, capturing its output. Returns the exit status.
 */
static int run_command(const char* command, char* const argv[])
{
	pid_t pid;
	int status = -1;
	int pipefd[2];
	FILE* f;

	uprintf("Executing: %s", command);

	if (pipe(pipefd) < 0)
		return -1;
	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pipefd[1]);
	f = fdopen(pipefd[0], "r");
	if (f != NULL) {
		char line[512];
		while (fgets(line, sizeof(line), f) != NULL) {
			size_t len = strlen(line);
			while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
				line[--len] = 0;
			if (len > 0)
				uprintf("%s", line);
		}
		fclose(f);
	} else {
		close(pipefd[0]);
	}
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return -1;
}

/*
 * Check whether a program is available in the PATH.
 */
static BOOL has_program(const char* name)
{
	char* path = getenv("PATH");
	char* copy;
	char* token;
	char* saveptr = NULL;
	BOOL found = FALSE;

	if (path == NULL)
		path = (char*)"/usr/local/bin:/usr/bin:/bin:/sbin:/usr/sbin";
	copy = strdup(path);
	if (copy == NULL)
		return FALSE;
	for (token = strtok_r(copy, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)) {
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", token, name);
		if (access(full, X_OK) == 0) {
			found = TRUE;
			break;
		}
	}
	free(copy);
	return found;
}

/*
 * Retrieve the device path of a partition.
 */
char* GetExtPartitionName(DWORD DriveIndex, uint64_t PartitionOffset)
{
	const char* device = GetPhysicalName(DriveIndex);
	char sysfs_path[PATH_MAX];
	DIR* dir;
	struct dirent* entry;
	char* result = NULL;

	if (device == NULL)
		return NULL;
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/block");
	dir = opendir(sysfs_path);
	if (dir == NULL)
		return NULL;
	while ((entry = readdir(dir)) != NULL) {
		char path[PATH_MAX], buffer[64];
		long long start;
		if (strncmp(entry->d_name, rufus_drive[DriveIndex].id, strlen(rufus_drive[DriveIndex].id)) != 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/block/%s/start", entry->d_name);
		if (!read_sysfs_value(path, buffer, sizeof(buffer)))
			continue;
		start = strtoll(buffer, NULL, 10);
		if ((uint64_t)start * 512 == PartitionOffset) {
			result = malloc(PATH_MAX);
			if (result != NULL)
				snprintf(result, PATH_MAX, "/dev/%s", entry->d_name);
			break;
		}
	}
	closedir(dir);
	return result;
}

/*
 * Format a partition with the requested file system.
 */
BOOL FormatPartition(DWORD DriveIndex, uint64_t PartitionOffset, DWORD UnitAllocationSize,
	USHORT FSType, LPCSTR Label, DWORD Flags)
{
	char* device = GetExtPartitionName(DriveIndex, PartitionOffset);
	char label[32];
	int status;
	BOOL r = FALSE;

	(void)UnitAllocationSize;
	(void)Flags;
	if (device == NULL) {
		uprintf("Could not locate the partition to format");
		ErrorStatus = RUFUS_ERROR(ERROR_FILE_NOT_FOUND);
		return FALSE;
	}
	if (Label != NULL)
		snprintf(label, sizeof(label), "%s", Label);
	else
		label[0] = 0;

	uprintf("Formatting %s (%s)...", device, GetFsName(FSType, 0));

	switch (FSType) {
	case FS_FAT16:
	case FS_FAT32: {
		char* argv[8];
		const char* program = "mkfs.vfat";
		if (!has_program(program)) {
			uprintf("ERROR: '%s' is not installed. Please install the 'dosfstools' package.", program);
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
			goto out;
		}
		argv[0] = (char*)program;
		argv[1] = (char*)"-F";
		argv[2] = (FSType == FS_FAT16) ? (char*)"16" : (char*)"32";
		if (label[0] != 0) {
			argv[3] = (char*)"-n";
			argv[4] = label;
		} else {
			argv[3] = NULL;
			argv[4] = NULL;
		}
		argv[5] = device;
		argv[6] = NULL;
		status = run_command("mkfs.vfat", argv);
		break;
	}
	case FS_EXFAT: {
		char* argv[6];
		const char* program = "mkfs.exfat";
		if (!has_program(program)) {
			uprintf("ERROR: '%s' is not installed. Please install the 'exfatprogs' package.", program);
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
			goto out;
		}
		argv[0] = (char*)program;
		argv[1] = (char*)"-n";
		argv[2] = (char*)label;
		argv[3] = device;
		argv[4] = NULL;
		status = run_command("mkfs.exfat", argv);
		break;
	}
	case FS_NTFS: {
		char* argv[8];
		const char* program = "mkfs.ntfs";
		if (!has_program(program)) {
			uprintf("ERROR: '%s' is not installed. Please install the 'ntfs-3g' package.", program);
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
			goto out;
		}
		argv[0] = (char*)program;
		argv[1] = (char*)"-F";
		argv[2] = (char*)"-f";
		argv[3] = (char*)"-L";
		argv[4] = (char*)label;
		argv[5] = device;
		argv[6] = NULL;
		status = run_command("mkfs.ntfs", argv);
		break;
	}
	case FS_UDF: {
		char* argv[6];
		const char* program = "mkfs.udf";
		if (!has_program(program)) {
			uprintf("ERROR: '%s' is not installed. Please install the 'udftools' package.", program);
			ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
			goto out;
		}
		argv[0] = (char*)program;
		argv[1] = (char*)"--label";
		argv[2] = (char*)label;
		argv[3] = device;
		argv[4] = NULL;
		status = run_command("mkfs.udf", argv);
		break;
	}
	case FS_EXT2:
	case FS_EXT3:
	case FS_EXT4: {
		const char* fsname = (FSType == FS_EXT2) ? "ext2" : (FSType == FS_EXT3) ? "ext3" : "ext4";
		if (!FormatExtFs(DriveIndex, PartitionOffset, UnitAllocationSize, fsname, label, Flags))
			goto out;
		status = 0;
		break;
	}
	default:
		uprintf("ERROR: Unsupported file system");
		ErrorStatus = RUFUS_ERROR(ERROR_NOT_SUPPORTED);
		goto out;
	}

	if (status != 0) {
		uprintf("ERROR: Could not format the partition (exit code %d)", status);
		ErrorStatus = RUFUS_ERROR(ERROR_WRITE_FAULT);
		goto out;
	}
	r = TRUE;
out:
	free(device);
	return r;
}
