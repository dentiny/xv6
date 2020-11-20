/*
 * This file provides path-related operations.
 * The called should guarentee passed-in char array has sufficient capacity 
 * and initialized.
 */

#ifndef _PATH_UTIL_H_
#define _PATH_UTIL_H_

#include "types.h"
#include "fcntl.h"
#include "fs.h"
#include "stat.h"
#include "user.h"
#define MAX_PATH_LEN 512
#define NULL ((void*)0)

// Look-up directory via fd, append dirent name at the end.
static int get_subdirectory(int fd, int ino, char *path) {
  struct dirent de;
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == ino) {
      int len = strlen(de.name);
      memmove(path, de.name, len);
      return len;
    }
  }
  return -1;
}

// Search upwards till the roodir.
static char* search_upward(int ino, char *cur_path, char *path) {
  // Recursively get to the parent directory.
  strcpy(cur_path + strlen(cur_path), "/..");
  struct stat statbuf;
  if (stat(cur_path, &statbuf) < 0) {
    return NULL;
  }

  // Reach rootdir, no need to search upward again.
  if (statbuf.ino == ino) {
    return path;
  }

  char *p = NULL; // Used to record the end of result.
  int fd = open(cur_path, O_RDONLY);
  if (fd >= 0) {
    p = search_upward(statbuf.ino, cur_path, path);
    if (p != NULL) {
      // Append current subdirectory to the end.
      strcpy(p++, "/");
      int len = get_subdirectory(fd, ino, p);
      if (len < 0) {
        return NULL;
      }
      p += len;
    }
    close(fd);
  }
  return p;
}

int getcwd(char *path) {
  char cur_path[MAX_PATH_LEN];
  memset(cur_path, '\0', MAX_PATH_LEN);
  cur_path[0] = '.';

  struct stat statbuf;
  if (stat(cur_path, &statbuf) < 0) {
    return -1;
  }

  // Search upwards towards the rootdir.
  char* p = search_upward(statbuf.ino, cur_path, path);
  if (p == NULL) {
    return -1;
  }
  if (path[0] == '\0') {
    path[0] = '/';
  }
  return 0;
}

void parse_subdirectory(char *fpath, char *subdirectory) {
  int len = 0; // length of subdirectory
  int idx = 0; // index of last slash
  for (char *ptr = fpath; *ptr != '\0'; ++ptr) {
    if (*ptr == '/') {
      len = 1;
      idx = ptr - fpath;
    } else {
      ++len;
    }
  }
  memmove(subdirectory, fpath + idx, len);
  subdirectory[len] = '\0';
}

// Filter full path, interpret '.' and '..'.
static void filter_path(char *fpath) {
  int idx1 = 0; // length of result full path, the next index to fill in
  for (int idx2 = 0; fpath[idx2] != '\0'; ++idx2) {
    // Skip '.', which represents the current path.
    if (fpath[idx2] == '/' && fpath[idx2 + 1] == '.' && (fpath[idx2 + 2] == '/' || fpath[idx2 + 2] == '\0')) {
      ++idx2;
    }

    // '..' rolls back to previous subdirectory.
    else if (fpath[idx2] == '/' && fpath[idx2 + 1] == '.' && fpath[idx2 + 2] == '.') {
      while (idx1 > 0 && fpath[idx1 - 1] != '/') {
        --idx1;
      }
      if (idx1 > 1) {
        --idx1;
      }
      idx2 += 2;
    }

    // Rolling back to root directory could lead to situation: idx1 = 1 && fpath[0] = '/'. 
    else if (idx1 > 0 && fpath[idx1 - 1] == '/' && fpath[idx2] == '/') {
      continue;
    }

    else {
      fpath[idx1++] = fpath[idx2]; 
    }
  }
	fpath[idx1] = '\0';
}

// Passed-in subdirectory can either be abspath or relative path. Base path is
// guarenteed to start with '/'.
void concatenate_path(char *dst, char *base_path, char *subdirectory) {
  int len1 = strlen(base_path);
  int len2 = strlen(subdirectory);

  // Subdirectory is absolute path already, no need to concatenate.
  if (subdirectory[0] == '/') {
    memmove(dst, subdirectory, len2);
  }

  else {
    memmove(dst, base_path, len1);

    // If base path is root directory.
    if (len1 == 1) {
      memmove(dst + 1, subdirectory, len2);
    } 
    // Otherwise, concatenate two substrings with '/'.
    else {
      dst[len1] = '/';
      memmove(dst + len1 + 1, subdirectory, len2);
    }
  }  
  
  // Filter out '.' and '..'.
  filter_path(dst);
}

// Used to decide whether container path is the prefix for full path.
int is_prefix_path(char *fpath, char *contpath) {
  // If container's root directory is the root for OS.
  if (contpath[0] == '/' && contpath[1] == '\0') {
    return 1;
  }

  int idx = 0;
  while (fpath[idx] != 0 && contpath[idx] != 0) {
    if (fpath[idx] != contpath[idx]) {
      return 0;
    }
    ++idx;
  }
  return contpath[idx] == 0 && (fpath[idx] == 0 || fpath[idx] == '/');
}

#endif // _PATH_UTIL_H_