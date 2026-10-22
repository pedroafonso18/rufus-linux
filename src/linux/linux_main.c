/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - main program and workflow.
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

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "linux_api.h"
#include "linux_headers.h"
#include "linux_workflow.h"
#include "localization.h"
#include "ms-sys/inc/br.h"
#include "ms-sys/inc/file.h"
#include "posix/win_compat.h"

/* ------------------------------------------------------------------------ */
/* Globals expected by the shared Rufus code                                 */
/* ------------------------------------------------------------------------ */

/* Note: the globals that the shared Rufus code defines itself (img_report,
 * WindowsVersion, ubuffer, hStatus, ...) are not repeated here. */
RUFUS_UPDATE update = { 0 };
HWND hMainDialog = NULL, hLogDialog = NULL, hDeviceList = NULL;
HWND hCapacity = NULL, hImageOption = NULL, hPartitionScheme = NULL, hTargetSystem = NULL;
HWND hFileSystem = NULL, hClusterSize = NULL, hLabel = NULL, hBootType = NULL;
HWND hNBPasses = NULL, hLog = NULL, hInfo = NULL, hProgress = NULL;
DWORD ErrorStatus = 0, DownloadStatus = 0, MainThreadId = 0, LastWriteError = 0;
uint8_t image_options = 0;
float fScale = 1.0f;

HINSTANCE hMainInstance = NULL;
WORD selected_langid = 0;
BOOL use_own_c32[NB_OLD_C32] = { 0 }, detect_fakes = FALSE, op_in_progress = FALSE;
BOOL right_to_left_mode = FALSE, allow_dual_uefi_bios = FALSE, large_drive = FALSE;
BOOL usb_debug = FALSE, op_in_progress_lock = FALSE;
uint16_t rufus_version[3] = { 4, 16, 0 }, embedded_sl_version[2] = { 0 };
uint64_t persistence_size = 0;
const int nb_steps[FS_MAX] = { 5, 5, 12, 1, 10, 1, 1, 1, 1 };
sbat_entry_t* sbat_entries = NULL;
thumbprint_list_t* sb_active_certs = NULL;
thumbprint_list_t* sb_revoked_certs = NULL;
int dialog_showing = 0, force_update = 0, fs_type = FS_FAT32, boot_type = BT_IMAGE;
int partition_type = PARTITION_STYLE_GPT, target_type = TT_UEFI;
unsigned long syslinux_ldlinux_len[2] = { 0, 0 };
char embedded_sl_version_str[2][12] = { "?.??", "?.??" };
char szFolderPath[MAX_PATH], app_dir[MAX_PATH], temp_dir[MAX_PATH], system_dir[MAX_PATH];
char sysnative_dir[MAX_PATH], app_data_dir[MAX_PATH];
char* image_path = NULL;
const char* FileSystemLabel[FS_MAX] = {
	"FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS", "ext2", "ext3", "ext4"
};

/* Progress reporting hooks, implemented by the front-end */
void (*linux_progress_callback)(int percent, const char* text) = NULL;
void (*linux_log_callback)(const char* text) = NULL;

/* ------------------------------------------------------------------------ */
/* Resource lookup                                                          */
/* ------------------------------------------------------------------------ */

/*
 * Locate one of the Rufus data files. We look in the following places:
 *  - the directory of the executable
 *  - the "res" directory of the source tree
 *  - /usr/share/rufus
 */
char* linux_find_data_file(const char* name)
{
	static char path[PATH_MAX];
	const char* candidates[] = {
		"",
		"/res/",
		"/../res/",
		"/../share/rufus/",
		"/share/rufus/",
		"/../share/rufus-linux/",
	};
	const char* absolute[] = {
		"/usr/share/rufus/",
		"/usr/local/share/rufus/",
		"/usr/share/rufus-linux/",
	};
	size_t i;

	for (i = 0; i < ARRAYSIZE(candidates); i++) {
		snprintf(path, sizeof(path), "%s%s%s", app_dir, candidates[i], name);
		if (access(path, R_OK) == 0)
			return path;
	}
	for (i = 0; i < ARRAYSIZE(absolute); i++) {
		snprintf(path, sizeof(path), "%s%s", absolute[i], name);
		if (access(path, R_OK) == 0)
			return path;
	}
	/* Also try the current working directory */
	snprintf(path, sizeof(path), "res/%s", name);
	if (access(path, R_OK) == 0)
		return path;
	return NULL;
}

const char* linux_resource_path(const char* name)
{
	return linux_find_data_file(name);
}

/* ------------------------------------------------------------------------ */
/* Entry point                                                               */
/* ------------------------------------------------------------------------ */

static void print_usage(const char* name)
{
	printf("Usage: %s [options] -i <image> -d <device>\n", name);
	printf("\n");
	printf("Rufus: The Reliable USB Formatting Utility (Linux port)\n");
	printf("\n");
	printf("Options:\n");
	printf("  -i, --image <file>       ISO or disk image to write to the device\n");
	printf("  -d, --device <device>    target device (e.g. sdb or /dev/sdb)\n");
	printf("  -l, --list               list the candidate devices and exit\n");
	printf("  -a, --list-all           list all the block devices and exit\n");
	printf("  -x, --extract <dir>      extract the image to a directory instead of a device\n");
	printf("  -L, --label <label>      volume label\n");
	printf("  -f, --fs <type>         file system: fat16, fat32, ntfs, exfat, udf, ext2, ext3, ext4\n");
	printf("  -p, --partition-style    partition table style: mbr or gpt (default: gpt)\n");
	printf("      --bios               install a BIOS (Syslinux) bootloader\n");
	printf("      --uefi               install a UEFI bootloader (default)\n");
	printf("      --no-uefi            do not set up a UEFI bootloader\n");
	printf("      --dry-run            show what would be done, without doing it\n");
	printf("  -v, --verbose            verbose output\n");
	printf("  -h, --help               show this help\n");
	printf("\n");
	printf("Note: writing to a device requires root privileges.\n");
}

int main(int argc, char** argv)
{
	linux_options opts;
	int i;
	int drive_index;
	int rc = 0;

	memset(&opts, 0, sizeof(opts));
	opts.use_uefi = TRUE;
	opts.partition_style = "gpt";
	opts.fs = "fat32";
	opts.label = NULL;

	/* Initialize the localization */
	init_localization();

	/* Locate the application directory, used to find the data files */
	{
		char path[PATH_MAX];
		ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
		if (len > 0) {
			char* slash;
			path[len] = 0;
			slash = strrchr(path, '/');
			if (slash != NULL)
				*slash = 0;
			snprintf(app_dir, sizeof(app_dir), "%s", path);
		} else {
			snprintf(app_dir, sizeof(app_dir), ".");
		}
	}

	for (i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		} else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list") == 0) {
			linux_refresh_devices();
			linux_list_devices(stdout, FALSE);
			return 0;
		} else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--list-all") == 0) {
			enable_HDDs = TRUE;
			linux_refresh_devices();
			linux_list_devices(stdout, TRUE);
			return 0;
		} else if (strcmp(arg, "--selftest") == 0) {
			const char* path = (++i < argc) ? argv[i] : "/tmp/rufus-selftest.img";
			return linux_selftest(path) ? 0 : 1;
		} else if (strcmp(arg, "--selftest-mbr") == 0) {
			const char* path = (++i < argc) ? argv[i] : "/tmp/rufus-selftest-mbr.img";
			return linux_selftest_mbr(path) ? 0 : 1;
		} else if (strcmp(arg, "-x") == 0 || strcmp(arg, "--extract") == 0) {
			if (++i >= argc) { print_usage(argv[0]); return 1; }
			opts.extract_to = argv[i];
		} else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--image") == 0) {
			if (++i >= argc) { print_usage(argv[0]); return 1; }
			opts.image = argv[i];
		} else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--device") == 0) {
			if (++i >= argc) { print_usage(argv[0]); return 1; }
			opts.device = argv[i];
		} else if (strcmp(arg, "-L") == 0 || strcmp(arg, "--label") == 0) {
			if (++i >= argc) { print_usage(argv[0]); return 1; }
			opts.label = argv[i];
		} else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--fs") == 0) {
			if (++i >= argc) { print_usage(argv[0]); return 1; }
			opts.fs = argv[i];
		} else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--partition-style") == 0) {
			if (++i >= argc) { print_usage(argv[0]); return 1; }
			opts.partition_style = argv[i];
		} else if (strcmp(arg, "--bios") == 0) {
			opts.use_bios = TRUE;
		} else if (strcmp(arg, "--uefi") == 0) {
			opts.use_uefi = TRUE;
		} else if (strcmp(arg, "--no-uefi") == 0) {
			opts.use_uefi = FALSE;
		} else if (strcmp(arg, "--dry-run") == 0) {
			opts.dry_run = TRUE;
		} else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
			opts.verbose = TRUE;
		} else {
			fprintf(stderr, "Unknown option: %s\n", arg);
			print_usage(argv[0]);
			return 1;
		}
	}

	/*
	 * When no action was requested, start the graphical interface (if it is
	 * available and we have a display), otherwise print the usage.
	 */
	if (opts.image == NULL && opts.device == NULL && opts.extract_to == NULL) {
		if (linux_ui_available() && linux_ui_main(argc, argv) == 0)
			return 0;
		print_usage(argv[0]);
		return 1;
	}

	if (opts.image == NULL) {
		print_usage(argv[0]);
		return 1;
	}

	win_log_verbose = opts.verbose ? 1 : 0;

	/* Load the localization data */
	{
		char* loc_file = linux_find_data_file("loc/rufus.loc");
		loc_cmd* selected_locale;
		if (loc_file == NULL)
			loc_file = linux_find_data_file("rufus.loc");
		if (loc_file == NULL) {
			fprintf(stderr, "ERROR: Could not find the localization data (rufus.loc).\n");
			return 1;
		}
		if (!get_supported_locales(loc_file)) {
			fprintf(stderr, "ERROR: Could not access the locale data.\n");
			return 1;
		}
		selected_locale = get_locale_from_name("en-US", TRUE);
		if (selected_locale == NULL) {
			fprintf(stderr, "ERROR: Could not find a suitable locale.\n");
			return 1;
		}
		selected_langid = get_language_id(selected_locale);
		if (!get_loc_data_file(loc_file, selected_locale)) {
			fprintf(stderr, "ERROR: Could not load the locale data.\n");
			return 1;
		}
		right_to_left_mode = ((selected_locale->ctrl_id) & LOC_RIGHT_TO_LEFT) ? TRUE : FALSE;
	}

	/* Extraction mode: only the image is needed */
	if (opts.extract_to != NULL)
		return linux_extract_image(opts.image, opts.extract_to) ? 0 : 1;

	if (opts.device == NULL) {
		print_usage(argv[0]);
		return 1;
	}
	if (linux_parse_fs(opts.fs) < 0) {
		fprintf(stderr, "Unknown file system: %s\n", opts.fs);
		return 1;
	}

	/* The command line interface is explicit, so we list all the devices */
	enable_HDDs = TRUE;
	linux_refresh_devices();
	drive_index = linux_find_drive_index(opts.device);
	if (drive_index < 0) {
		fprintf(stderr, "ERROR: The device '%s' was not found.\n\n", opts.device);
		linux_list_devices(stdout, TRUE);
		return 1;
	}

	if (geteuid() != 0 && !opts.dry_run) {
		fprintf(stderr, "ERROR: Writing to a device requires root privileges.\n");
		fprintf(stderr, "       Use 'sudo' or run this program as root.\n");
		return 1;
	}

	if (!linux_write_image(&opts))
		rc = 1;

	return rc;
}
