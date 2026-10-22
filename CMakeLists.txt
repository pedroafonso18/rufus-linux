# Rufus: The Reliable USB Formatting Utility
# Linux port - CMake build script
#
# Copyright © 2011-2026 Pete Batard <pete@akeo.ie>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)
project(rufus-linux VERSION 4.16 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(RUFUS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(RUFUS_RES ${CMAKE_CURRENT_SOURCE_DIR}/res)

add_compile_options(
  -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
  -Wno-unused-but-set-variable -Wno-register -Wno-address-of-packed-member
  -Wno-implicit-fallthrough -Wno-stringop-truncation -Wno-format-truncation
  -Wno-incompatible-pointer-types -Wno-deprecated-declarations)

#
# Optional dependencies
#
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(LIBCURL QUIET libcurl)
  pkg_check_modules(OPENSSL QUIET libcrypto)
  pkg_check_modules(GTK QUIET IMPORTED_TARGET gtk+-3.0)
  pkg_check_modules(LIBUDEV QUIET libudev)
  pkg_check_modules(LIBMOUNT QUIET libmount)
  pkg_check_modules(ZSTD QUIET libzstd)
endif()

find_package(ZLIB REQUIRED)
find_package(LibLZMA REQUIRED)

include_directories(${RUFUS_SRC})
include_directories(${RUFUS_SRC}/posix)
include_directories(${RUFUS_SRC}/ms-sys/inc)
include_directories(${RUFUS_SRC}/syslinux/libfat)
include_directories(${RUFUS_SRC}/syslinux/libinstaller)
include_directories(${RUFUS_SRC}/libcdio)
include_directories(${RUFUS_SRC}/wimlib)
include_directories(${RUFUS_RES})

#
# The Win32 compatibility layer.
#
add_library(wincompat STATIC
  ${RUFUS_SRC}/posix/win_compat.c
  ${RUFUS_SRC}/posix/win_gui.c)
target_include_directories(wincompat PUBLIC ${RUFUS_SRC}/posix)
target_link_libraries(wincompat PUBLIC pthread dl)

#
# Vendored, platform independent libraries.
#

# libcdio (ISO-9660/UDF reader). The POSIX config.h must come first.
add_library(libcdio_driver STATIC
  ${RUFUS_SRC}/libcdio/driver/disc.c
  ${RUFUS_SRC}/libcdio/driver/ds.c
  ${RUFUS_SRC}/libcdio/driver/logging.c
  ${RUFUS_SRC}/libcdio/driver/memory.c
  ${RUFUS_SRC}/libcdio/driver/read.c
  ${RUFUS_SRC}/libcdio/driver/sector.c
  ${RUFUS_SRC}/libcdio/driver/track.c
  ${RUFUS_SRC}/libcdio/driver/util.c
  ${RUFUS_SRC}/libcdio/driver/_cdio_stdio.c
  ${RUFUS_SRC}/libcdio/driver/_cdio_stream.c
  ${RUFUS_SRC}/libcdio/driver/utf8.c)
target_include_directories(libcdio_driver BEFORE PUBLIC
  ${RUFUS_SRC}/posix/config/libcdio
  ${RUFUS_SRC}/libcdio
  ${RUFUS_SRC}/libcdio/driver)
target_compile_definitions(libcdio_driver PUBLIC HAVE_CONFIG_H _LARGEFILE64_SOURCE)

add_library(libcdio_iso9660 STATIC
  ${RUFUS_SRC}/libcdio/iso9660/iso9660.c
  ${RUFUS_SRC}/libcdio/iso9660/iso9660_fs.c
  ${RUFUS_SRC}/libcdio/iso9660/rock.c
  ${RUFUS_SRC}/libcdio/iso9660/xa.c)
target_include_directories(libcdio_iso9660 BEFORE PUBLIC
  ${RUFUS_SRC}/posix/config/libcdio
  ${RUFUS_SRC}/libcdio
  ${RUFUS_SRC}/libcdio/driver)
target_compile_definitions(libcdio_iso9660 PUBLIC HAVE_CONFIG_H)

add_library(libcdio_udf STATIC
  ${RUFUS_SRC}/libcdio/udf/udf.c
  ${RUFUS_SRC}/libcdio/udf/udf_file.c
  ${RUFUS_SRC}/libcdio/udf/udf_fs.c
  ${RUFUS_SRC}/libcdio/udf/udf_time.c
  ${RUFUS_SRC}/libcdio/udf/filemode.c)
target_include_directories(libcdio_udf BEFORE PUBLIC
  ${RUFUS_SRC}/posix/config/libcdio
  ${RUFUS_SRC}/libcdio
  ${RUFUS_SRC}/libcdio/driver)
target_compile_definitions(libcdio_udf PUBLIC HAVE_CONFIG_H)
target_link_libraries(libcdio_udf PUBLIC libcdio_driver)

# wimlib (WIM extraction). The Windows specific translation units are excluded.
file(GLOB WIMLIB_SOURCES ${RUFUS_SRC}/wimlib/*.c)
list(FILTER WIMLIB_SOURCES EXCLUDE REGEX "/win32_.*\\.c$")
add_library(libwim STATIC ${WIMLIB_SOURCES})
target_include_directories(libwim BEFORE PUBLIC
  ${RUFUS_SRC}/posix/config/wimlib
  ${RUFUS_SRC}/wimlib
  ${RUFUS_SRC}/libcdio)
target_compile_definitions(libwim PUBLIC HAVE_CONFIG_H _RUFUS _POSIX _POSIX_THREAD_SAFE_FUNCTIONS
  _lseeki64=lseek64)
target_link_libraries(libwim PUBLIC libcdio_driver)

# e2fsprogs (ext2/3/4 formatting). nt_io.c is replaced by unix_io.c.
file(GLOB EXT2FS_SOURCES ${RUFUS_SRC}/ext2fs/*.c)
list(FILTER EXT2FS_SOURCES EXCLUDE REGEX "/nt_io\\.c$")
add_library(libext2fs STATIC ${EXT2FS_SOURCES} ${RUFUS_SRC}/ext2fs/unix_io.c)
target_include_directories(libext2fs BEFORE PUBLIC ${RUFUS_SRC}/ext2fs)
target_compile_definitions(libext2fs PUBLIC EXT2_FLAT_INCLUDES=0 HAVE_CONFIG_H)

# ms-sys (boot record synthesis and partition info parsing)
add_library(libmssys STATIC
  ${RUFUS_SRC}/ms-sys/fat12.c
  ${RUFUS_SRC}/ms-sys/fat16.c
  ${RUFUS_SRC}/ms-sys/fat32.c
  ${RUFUS_SRC}/ms-sys/ntfs.c
  ${RUFUS_SRC}/ms-sys/partition_info.c
  ${RUFUS_SRC}/ms-sys/br.c
  ${RUFUS_SRC}/ms-sys/file.c)
target_include_directories(libmssys PUBLIC ${RUFUS_SRC}/ms-sys/inc)
target_compile_options(libmssys PRIVATE -include ${RUFUS_SRC}/posix/malloc_compat.h)

# Syslinux libfat (FAT access for the Syslinux installer)
add_library(libfat STATIC
  ${RUFUS_SRC}/syslinux/libfat/cache.c
  ${RUFUS_SRC}/syslinux/libfat/fatchain.c
  ${RUFUS_SRC}/syslinux/libfat/open.c
  ${RUFUS_SRC}/syslinux/libfat/searchdir.c
  ${RUFUS_SRC}/syslinux/libfat/dumpdir.c)
target_include_directories(libfat PUBLIC ${RUFUS_SRC}/syslinux/libfat)
target_compile_options(libfat PRIVATE -include ${RUFUS_SRC}/posix/malloc_compat.h)

# Syslinux libinstaller
add_library(libinstaller STATIC
  ${RUFUS_SRC}/syslinux/libinstaller/fs.c
  ${RUFUS_SRC}/syslinux/libinstaller/setadv.c
  ${RUFUS_SRC}/syslinux/libinstaller/syslxmod.c)
target_include_directories(libinstaller PUBLIC ${RUFUS_SRC}/syslinux/libinstaller)
target_compile_options(libinstaller PRIVATE -include ${RUFUS_SRC}/posix/malloc_compat.h)

#
# The Rufus core: the platform independent logic, shared with the Windows
# build. On Linux it is compiled against the Win32 compatibility layer.
#
add_library(rufuscore OBJECT
  ${RUFUS_SRC}/badblocks.c
  ${RUFUS_SRC}/cregex_compile.c
  ${RUFUS_SRC}/cregex_parse.c
  ${RUFUS_SRC}/cregex_vm.c
  ${RUFUS_SRC}/dos.c
  ${RUFUS_SRC}/dos_locale.c
  ${RUFUS_SRC}/format_ext.c
  ${RUFUS_SRC}/hash.c
  ${RUFUS_SRC}/icon.c
  ${RUFUS_SRC}/iso.c
  ${RUFUS_SRC}/localization.c
  ${RUFUS_SRC}/parser.c
  ${RUFUS_SRC}/stdio.c
  ${RUFUS_SRC}/stdfn.c
  ${RUFUS_SRC}/xml.c)
target_compile_definitions(rufuscore PUBLIC _RUFUS HAVE_CONFIG_H)
target_include_directories(rufuscore PUBLIC
  ${RUFUS_SRC}
  ${RUFUS_SRC}/posix
  ${RUFUS_SRC}/ms-sys/inc
  ${RUFUS_SRC}/syslinux/libfat
  ${RUFUS_SRC}/syslinux/libinstaller
  ${RUFUS_SRC}/libcdio
  ${RUFUS_SRC}/wimlib
  ${RUFUS_RES})
target_link_libraries(rufuscore PUBLIC
  wincompat
  libcdio_iso9660
  libcdio_udf
  libext2fs
  libmssys
  libfat
  libinstaller
  libwim)

#
# The Linux platform layer.
#
add_library(linuxcore OBJECT
  ${RUFUS_SRC}/linux/linux_dev.c
  ${RUFUS_SRC}/linux/linux_drive.c
  ${RUFUS_SRC}/linux/linux_format.c
  ${RUFUS_SRC}/linux/linux_glue.c
  ${RUFUS_SRC}/linux/linux_workflow.c
  ${RUFUS_SRC}/linux/linux_bled.c
  ${RUFUS_SRC}/linux/linux_registry.c)
target_link_libraries(linuxcore PUBLIC rufuscore)

#
# The front-end. The graphical interface is compiled in when GTK 3 is
# available; the command line interface is always available.
#
set(RUFUS_GUI_SOURCES ${RUFUS_SRC}/linux/ui_linux.c)
if(NOT GTK_FOUND)
  set(RUFUS_GUI_SOURCES ${RUFUS_SRC}/linux/ui_stub.c)
endif()

add_executable(rufus-linux
  ${RUFUS_SRC}/linux/linux_main.c
  ${RUFUS_GUI_SOURCES}
  $<TARGET_OBJECTS:rufuscore>
  $<TARGET_OBJECTS:linuxcore>)
target_link_libraries(rufus-linux PRIVATE
  $<LINK_GROUP:RESCAN,wincompat,libwim,libcdio_driver,libcdio_iso9660,libcdio_udf,libext2fs,libmssys,libfat,libinstaller>
  pthread dl m ZLIB::ZLIB LibLZMA::LibLZMA ${ZSTD_LIBRARIES})
if(GTK_FOUND)
  target_link_libraries(rufus-linux PRIVATE PkgConfig::GTK)
endif()

#
# Installation
#
include(GNUInstallDirs)
install(TARGETS rufus-linux RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${RUFUS_RES}/loc/rufus.loc
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux/loc)
install(DIRECTORY ${RUFUS_RES}/syslinux
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux
        FILES_MATCHING PATTERN "ldlinux_v*.sys" PATTERN "ldlinux_v*.bss" PATTERN "mboot.c32")
install(DIRECTORY ${RUFUS_RES}/md5
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux
        FILES_MATCHING PATTERN "*.efi")
install(DIRECTORY ${RUFUS_RES}/grub
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux
        FILES_MATCHING PATTERN "grldr.mbr")
install(DIRECTORY ${RUFUS_RES}/grub2
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux
        FILES_MATCHING PATTERN "*.img")
install(DIRECTORY ${RUFUS_RES}/dbx
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux
        FILES_MATCHING PATTERN "*.efiauth2")
install(FILES ${RUFUS_RES}/uefi/uefi-ntfs.img
        DESTINATION ${CMAKE_INSTALL_DATADIR}/rufus-linux/uefi OPTIONAL)

