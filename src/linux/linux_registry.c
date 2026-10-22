/*
 * Rufus: The Reliable USB Formatting Utility
 * Linux port - registry emulation.
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
 * Windows stores its settings in the registry. On Linux, we use a simple
 * INI style file instead, located in the user's configuration directory:
 *
 *   $XDG_CONFIG_HOME/rufus/registry.ini
 *
 * The file is organised as follows:
 *
 *   [HKEY_CURRENT_USER\Software\Akeo Consulting\Rufus]
 *   "Language"=0x409
 *
 * Note that this is a best effort emulation: only the subset of the
 * registry API that Rufus actually uses is implemented.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "linux_api.h"
#include "posix/win_compat.h"

typedef struct reg_value {
	char* key;		/* Full path of the key, e.g. "HKCU\Software\..." */
	char* name;		/* Value name ("" for the default value) */
	DWORD type;
	uint8_t* data;
	DWORD size;
	struct reg_value* next;
} reg_value;

typedef struct reg_key {
	DWORD root;
	char subkey[256];
} reg_key;

static reg_value* reg_values = NULL;
static int reg_loaded = 0;
static char reg_path[PATH_MAX];

#define REG_ROOT_HKCU 1
#define REG_ROOT_HKLM 2

static const char* get_registry_path(void)
{
	const char* config = getenv("XDG_CONFIG_HOME");
	const char* home = getenv("HOME");

	if (reg_path[0] != 0)
		return reg_path;
	if (config != NULL && config[0] != 0)
		snprintf(reg_path, sizeof(reg_path), "%s/rufus/registry.ini", config);
	else if (home != NULL && home[0] != 0)
		snprintf(reg_path, sizeof(reg_path), "%s/.config/rufus/registry.ini", home);
	else
		snprintf(reg_path, sizeof(reg_path), "/tmp/rufus-registry.ini");
	return reg_path;
}

static void reg_load(void)
{
	FILE* f;
	char line[1024];

	if (reg_loaded)
		return;
	reg_loaded = 1;
	f = fopen(get_registry_path(), "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char* p = line;
		char* end;
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = 0;
		if (len == 0)
			continue;
		if (line[0] == '[') {
			char* close = strchr(line, ']');
			if (close != NULL)
				*close = 0;
			p = &line[1];
			continue;
		}
		/* The current key is the last section header we saw */
		{
			static char current_key[512];
			char* eq = strchr(p, '=');
			char* name;
			uint64_t value;
			reg_value* v;
			if (eq == NULL)
				continue;
			name = p;
			*eq = 0;
			value = strtoull(eq + 1, &end, 0);
			/* Trim the quotes and spaces around the name */
			while (*name == ' ' || *name == '"')
				name++;
			{
				char* e = name + strlen(name);
				while (e > name && (e[-1] == ' ' || e[-1] == '"'))
					*--e = 0;
			}
			v = (reg_value*)calloc(1, sizeof(reg_value));
			if (v == NULL)
				continue;
			v->key = strdup(current_key);
			v->name = strdup(name);
			v->type = REG_DWORD;
			v->size = (DWORD)sizeof(uint32_t);
			v->data = (uint8_t*)malloc(v->size);
			if (v->data != NULL) {
				uint32_t dword = (uint32_t)value;
				memcpy(v->data, &dword, sizeof(dword));
			}
			v->next = reg_values;
			reg_values = v;
		}
	}
	fclose(f);
}

static void reg_save(void)
{
	FILE* f;
	reg_value* v;
	char dir[PATH_MAX];

	snprintf(dir, sizeof(dir), "%s", get_registry_path());
	{
		char* slash = strrchr(dir, '/');
		if (slash != NULL) {
			*slash = 0;
			mkdir(dir, 0755);
			*slash = '/';
		}
	}
	f = fopen(get_registry_path(), "w");
	if (f == NULL)
		return;
	fprintf(f, "# Rufus settings (Linux port)\n");
	for (v = reg_values; v != NULL; v = v->next) {
		if (v->type == REG_DWORD && v->size == sizeof(uint32_t)) {
			uint32_t dword;
			memcpy(&dword, v->data, sizeof(dword));
			fprintf(f, "[%s]\n\"%s\"=0x%x\n", v->key, v->name, dword);
		} else if (v->type == REG_SZ && v->size > 0) {
			fprintf(f, "[%s]\n\"%s\"=\"%s\"\n", v->key, v->name, (char*)v->data);
		}
	}
	fclose(f);
}

static reg_value* reg_find(const char* key, const char* name)
{
	reg_value* v;
	reg_load();
	for (v = reg_values; v != NULL; v = v->next) {
		if (strcmp(v->key, key) == 0 && strcmp(v->name, name) == 0)
			return v;
	}
	return NULL;
}

static reg_key* reg_key_from(HKEY hKey)
{
	return (reg_key*)hKey;
}

static void reg_full_path(HKEY hKey, const char* subkey, char* buffer, size_t size)
{
	reg_key* key = reg_key_from(hKey);
	if (key == NULL) {
		snprintf(buffer, size, "%s", subkey != NULL ? subkey : "");
		return;
	}
	if (subkey != NULL && subkey[0] != 0)
		snprintf(buffer, size, "%s\\%s", key->subkey, subkey);
	else
		snprintf(buffer, size, "%s", key->subkey);
}

LONG WINAPI RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, ULONG ulOptions, REGSAM samDesired, HKEY* phkResult)
{
	char path[512];
	reg_key* key;

	(void)ulOptions; (void)samDesired;
	reg_load();
	reg_full_path(hKey, lpSubKey, path, sizeof(path));
	if (reg_find(path, "") == NULL && reg_find(path, "") == NULL) {
		/* The key may exist without a value; look for any value under it */
		reg_value* v;
		BOOL found = FALSE;
		for (v = reg_values; v != NULL; v = v->next) {
			if (strncmp(v->key, path, strlen(path)) == 0 &&
				(v->key[strlen(path)] == '\\' || v->key[strlen(path)] == 0)) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			return ERROR_FILE_NOT_FOUND;
	}
	key = (reg_key*)calloc(1, sizeof(reg_key));
	if (key == NULL)
		return ERROR_NOT_ENOUGH_MEMORY;
	key->root = (reg_key_from(hKey) != NULL) ? reg_key_from(hKey)->root : REG_ROOT_HKCU;
	snprintf(key->subkey, sizeof(key->subkey), "%s", path);
	if (phkResult != NULL)
		*phkResult = (HKEY)key;
	return ERROR_SUCCESS;
}

LONG WINAPI RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions,
	REGSAM samDesired, LPSECURITY_ATTRIBUTES lpSecurityAttributes, HKEY* phkResult, LPDWORD lpdwDisposition)
{
	char path[512];
	reg_key* key;

	(void)Reserved; (void)lpClass; (void)dwOptions; (void)samDesired; (void)lpSecurityAttributes;
	reg_load();
	reg_full_path(hKey, lpSubKey, path, sizeof(path));
	key = (reg_key*)calloc(1, sizeof(reg_key));
	if (key == NULL)
		return ERROR_NOT_ENOUGH_MEMORY;
	key->root = (reg_key_from(hKey) != NULL) ? reg_key_from(hKey)->root : REG_ROOT_HKCU;
	snprintf(key->subkey, sizeof(key->subkey), "%s", path);
	if (phkResult != NULL)
		*phkResult = (HKEY)key;
	if (lpdwDisposition != NULL)
		*lpdwDisposition = 2; /* REG_OPTION_NON_VOLATILE */
	return ERROR_SUCCESS;
}

LONG WINAPI RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
	LPBYTE lpData, LPDWORD lpcbData)
{
	char path[512];
	reg_value* v;

	(void)lpReserved;
	reg_load();
	reg_full_path(hKey, NULL, path, sizeof(path));
	v = reg_find(path, lpValueName != NULL ? lpValueName : "");
	if (v == NULL)
		return ERROR_FILE_NOT_FOUND;
	if (lpType != NULL)
		*lpType = v->type;
	if (lpData == NULL) {
		if (lpcbData != NULL)
			*lpcbData = v->size;
		return ERROR_SUCCESS;
	}
	if (lpcbData != NULL && *lpcbData < v->size) {
		*lpcbData = v->size;
		return ERROR_MORE_DATA;
	}
	if (lpData != NULL)
		memcpy(lpData, v->data, v->size);
	if (lpcbData != NULL)
		*lpcbData = v->size;
	return ERROR_SUCCESS;
}

LONG WINAPI RegSetValueExA(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType,
	const BYTE *lpData, DWORD cbData)
{
	char path[512];
	reg_value* v;

	(void)Reserved;
	reg_load();
	reg_full_path(hKey, NULL, path, sizeof(path));
	v = reg_find(path, lpValueName != NULL ? lpValueName : "");
	if (v == NULL) {
		v = (reg_value*)calloc(1, sizeof(reg_value));
		if (v == NULL)
			return ERROR_NOT_ENOUGH_MEMORY;
		v->key = strdup(path);
		v->name = strdup(lpValueName != NULL ? lpValueName : "");
		v->next = reg_values;
		reg_values = v;
	}
	free(v->data);
	v->type = dwType;
	v->size = cbData;
	v->data = (uint8_t*)malloc(cbData ? cbData : 1);
	if (v->data != NULL && lpData != NULL)
		memcpy(v->data, lpData, cbData);
	reg_save();
	return ERROR_SUCCESS;
}

LONG WINAPI RegDeleteValueA(HKEY hKey, LPCSTR lpValueName)
{
	char path[512];
	reg_value* v;
	reg_value* prev = NULL;

	reg_load();
	reg_full_path(hKey, NULL, path, sizeof(path));
	for (v = reg_values; v != NULL; prev = v, v = v->next) {
		if (strcmp(v->key, path) == 0 && strcmp(v->name, lpValueName != NULL ? lpValueName : "") == 0) {
			if (prev != NULL)
				prev->next = v->next;
			else
				reg_values = v->next;
			free(v->key);
			free(v->name);
			free(v->data);
			free(v);
			reg_save();
			return ERROR_SUCCESS;
		}
	}
	return ERROR_FILE_NOT_FOUND;
}

LONG WINAPI RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey)
{
	char path[512];
	reg_value* v;
	reg_value* prev = NULL;
	BOOL deleted = FALSE;

	reg_load();
	reg_full_path(hKey, lpSubKey, path, sizeof(path));
	for (v = reg_values; v != NULL; prev = v, v = v->next) {
		if (strncmp(v->key, path, strlen(path)) == 0) {
			reg_value* next = v->next;
			if (prev != NULL)
				prev->next = next;
			else
				reg_values = next;
			free(v->key);
			free(v->name);
			free(v->data);
			free(v);
			v = next;
			prev = NULL;
			deleted = TRUE;
			continue;
		}
	}
	if (deleted)
		reg_save();
	return deleted ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

LONG WINAPI RegDeleteKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD dwReserved, DWORD dwOptions)
{
	(void)dwReserved; (void)dwOptions;
	return RegDeleteKeyA(hKey, lpSubKey);
}

LONG WINAPI RegCloseKey(HKEY hKey)
{
	if (hKey != NULL && hKey != HKEY_CURRENT_USER && hKey != HKEY_LOCAL_MACHINE &&
		hKey != HKEY_CLASSES_ROOT)
		free(reg_key_from(hKey));
	return ERROR_SUCCESS;
}

LONG WINAPI RegEnumKeyExA(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName,
	LPDWORD lpReserved, LPDWORD lpClass, LPDWORD lpcchClass, LPDWORD lpftLastWriteTime)
{
	char path[512];
	reg_value* v;
	DWORD index = 0;

	(void)lpReserved; (void)lpClass; (void)lpcchClass; (void)lpftLastWriteTime;
	reg_load();
	reg_full_path(hKey, NULL, path, sizeof(path));
	for (v = reg_values; v != NULL; v = v->next) {
		if (strcmp(v->key, path) != 0)
			continue;
		if (index++ != dwIndex)
			continue;
		if (lpName != NULL && lpcchName != NULL) {
			snprintf(lpName, *lpcchName, "%s", v->name);
			*lpcchName = (DWORD)strlen(v->name);
		}
		return ERROR_SUCCESS;
	}
	return ERROR_NO_MORE_ITEMS;
}
