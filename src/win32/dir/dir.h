 /* POSIX emulation layer for Windows.
 *
 * Copyright (C) 2008-2014 Anope Team <team@anope.org>
 *
 * Please read COPYING and README for further details.
 */

#include <windows.h>

struct dirent
{
	int d_ino;
	char *d_name;
};

struct DIR
{
	dirent ent;
	HANDLE handle;
	WIN32_FIND_DATA data;
	bool read_first;
};
 
DIR *opendir(const char *);
dirent *readdir(DIR *);
int closedir(DIR *);
