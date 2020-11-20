#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PATH_LEN 200

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

// Single concatenation.
void test1() {
	char base[PATH_LEN] = "/hao";
	char subdirectory[PATH_LEN] = "jiang";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/hao/jiang") == 0);
}

// Composite concatenation.
void test2() {
	char base[PATH_LEN] = "/hao/jiang";
	char subdirectory[PATH_LEN] = "jiang/hao";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/hao/jiang/jiang/hao") == 0);
}

// Subdirectory is absolute path.
void test3() {
	char base[PATH_LEN] = "/hao/jiang";
	char subdirectory[PATH_LEN] = "/jiang/hao";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/jiang/hao") == 0);	
}

// Base path is root directory.
void test4() {
	char base[PATH_LEN] = "/";
	char subdirectory[PATH_LEN] = "jiang/hao";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/jiang/hao") == 0);	
}

// Subdirectory contains '.' and '..'.
void test5() {
	char base[PATH_LEN] = "/jiang/hao";
	char subdirectory[PATH_LEN] = "././hao/../jiang";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/jiang/hao/jiang") == 0);	
}

// Roll back to or beyond root directory.
void test6() {
	char base[PATH_LEN] = "/jiang/hao";
	char subdirectory[PATH_LEN] = "../..";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/") == 0);	
}

void test7() {
	char base[PATH_LEN] = "/jiang/hao";
	char subdirectory[PATH_LEN] = "../.././hao/.././../jiang/.././jiang";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/jiang") == 0);	
}

void test8() {
	char base[PATH_LEN] = "/jiang/hao";
	char subdirectory[PATH_LEN] = "../../../.././../../../././././../../..";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/") == 0);
}

void test9() {
	char base[PATH_LEN] = "/jiang/hao";
	char subdirectory[PATH_LEN] = "../jiang/../hao/.././../jiang/jiang/jiang/../././hao/../../././jiang/..";
	char fpath[PATH_LEN];
	memset(fpath, 0, PATH_LEN);
	concatenate_path(fpath, base, subdirectory);
	assert(strcmp(fpath, "/jiang") == 0);	
}

int main() {
	test1();
	test2();
	test3();
	test4();
	test5();
	test6();
	test7();
	test8();
	test9();
}
