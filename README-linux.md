Rufus for Linux
==============

This is a port of [Rufus](https://github.com/pbatard/rufus), the *Reliable USB
Formatting Utility*, to Linux.

The goal of this port is to reproduce the behaviour of the Windows version on
Linux, using the native tools and APIs of the platform instead of the Win32
ones:

* Block devices are enumerated through **sysfs** (and the USB information
  through the `/sys/bus/usb` device tree).
* **MBR** and **GPT** partition tables are created directly by Rufus, in the
  same way the Windows version does it through VDS.
* File systems are created with the standard tools: `mkfs.vfat`,
  `mkfs.exfat`, `mkfs.ntfs`, `mkfs.udf`, and — for ext2/ext3/ext4 — with the
  vendored copy of e2fsprogs, exactly like the Windows version does.
* The ISO9660/UDF reader, the WIM reader, the boot record generator and the
  FAT library are the same vendored libraries that the Windows build uses.

The graphical interface is provided by **GTK 3**, and mirrors the layout of the
Windows application.

Building
--------

```
$ ./build-linux.sh
```

or, equivalently:

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build -j$(nproc)
```

The following packages are required:

| Package      | Purpose                                     |
|--------------|---------------------------------------------|
| `gtk+-3.0`   | Graphical interface (optional, but recommended) |
| `zlib`       | Compression library                         |
| `liblzma`    | Compression library                         |
| `libzstd`    | Compression library                         |

The following external programs are used at runtime, depending on the
operations that are performed:

| Program      | Purpose                                     |
|--------------|---------------------------------------------|
| `mkfs.vfat`  | FAT12/FAT16/FAT32 file systems (dosfstools) |
| `mkfs.exfat` | exFAT file systems (exfatprogs)             |
| `mkfs.ntfs`  | NTFS file systems (ntfs-3g)                 |
| `mkfs.udf`   | UDF file systems (udftools)                 |
| `unzip`      | ZIP archives                                |

Usage
-----

Running the program without any argument starts the graphical interface:

```
$ rufus-linux
```

The command line interface can be used for unattended operations:

```
$ rufus-linux --list                     # list the candidate devices
$ rufus-linux --list-all                 # list all the block devices
$ rufus-linux -i image.iso -d /dev/sdb   # write an image to a USB drive
$ rufus-linux -i image.iso -x /mnt/dir   # extract an image to a directory
```

Run `rufus-linux --help` for the complete list of options.

Writing to a device requires root privileges, since the kernel only lets
privileged processes write to raw block devices:

```
$ sudo rufus-linux -i ubuntu-24.04.iso -d /dev/sdb
```

How the port is organised
--------------------------

The bulk of the Rufus code base is platform independent, and is reused
as-is. The port is made of the following pieces:

| Directory            | Contents                                                            |
|----------------------|---------------------------------------------------------------------|
| `src/posix/`         | The Win32 compatibility layer (`windows.h`, `msapi_utf8.h`, `winio.h`, the registry emulation, ...) |
| `src/linux/`         | The Linux implementation of the device, drive, format and user interface layers |
| `src/posix/config/`  | The POSIX variants of the configuration headers of the vendored libraries |

The Win32 compatibility layer implements the subset of the Win32 API that the
platform independent Rufus logic relies upon: file I/O, threading, timers,
processes, modules, the registry, and the window message pump. The window
message pump is routed to the front-end through the `win_dispatch` structure,
which is how the same code drives the GTK interface and the command line
interface.

The files that could not be reused are those that are inherently Windows
specific (VDS, SetupAPI, the Windows certificate store, the Windows kernel
drivers, the Syslinux NTFS installer, ...). Those have been replaced by POSIX
equivalents in `src/linux/`.

Notes on the vendored libraries
-------------------------------

A few adjustments were needed in the vendored libraries:

* **libcdio**: the configuration header was adapted for POSIX, and the
  handling of empty Joliet file names was made consistent with the Windows
  implementation (the iconv based code path did not reject empty strings,
  which made the `.` and `..` entries of a Joliet directory be treated as
  regular files).
* **wimlib**: the configuration header was adapted for POSIX (pthreads,
  glob, ...) and the Rufus specific UTF-16 wrappers were made a no-op on
  platforms where the `tchar` type is already UTF-8.
* **e2fsprogs**: the Windows NT I/O back-end (`nt_io.c`) was replaced by a
  POSIX implementation (`unix_io.c`).

Limitations
-----------

The following Windows specific features are not available on Linux:

* Windows To Go and the Windows installation customisations (OOBE,
  unattend.xml, ...) — they only apply to Windows installation media.
* The Secure Boot revocation database (DBX) handling, which relies on the
  Windows certificate store.
* NTFS file system creation is delegated to `ntfs-3g`, so the "large NTFS"
  and "quick format" options of the Windows version are not offered.
* The Syslinux installer does not create the Advanced Data (ADV) file, which
  is only relevant for NTFS volumes.
