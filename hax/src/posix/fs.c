#include "../common.h"
#include <sys/stat.h>


/**
 * Check if a file path exists.
 *   @path: The path.
 *   &returns: True if the file exists.
 */
bool fs_exists(const char *path)
{
	return access(path, F_OK) != -1;
}

/**
 * Check if a path points to a directory.
 *   @path: The path.
 *   &returns: True if a directory.
 */
bool fs_isdir(const char *path)
{
	struct stat info;

	if(stat(path, &info) < 0)
		return false;

	return S_ISDIR(info.st_mode);
}


/**
 * Create a directory.
 *   @path: the path.
 *   @perm: The permission.
 *   &returns: Error.
 */
char *fs_mkdir(const char *path, uint16_t perm)
{
	if(!fs_trymkdir(path, perm))
		return mprintf("Failed to create directory '%s'. %s.", path, strerror(errno));

	return NULL;
}

/**
 * Attempt to create a directory.
 *   @path: the path.
 *   @perm: The permission.
 *   &returns: True if successful.
 */
bool fs_trymkdir(const char *path, uint16_t perm)
{
	if(mkdir(path, perm) < 0)
		return false;

	if(chmod(path, perm) < 0)
		return false;

	return true;
}
