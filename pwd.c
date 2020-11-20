/*
 * Get current directory inside of the container.
 */

#include "path_util.h"

int extract_cont_path(char *cont_path, char *fpath, char *cont_rootdir) {
  int idx = 0; // start index of relative path inside container
  for (; fpath[idx] != '\0' && cont_rootdir[idx] != '\0'; ++idx) {
    // Error case: Container root directory is not the prefix of full path.
    if (fpath[idx] != cont_rootdir[idx]) {
      printf(2, "Container root directory is not the prefix of full path.\n");
      return -1;
    }
  }

  // Error case: Container root directory is longer than full path.
  if (fpath[idx] == '\0' && cont_rootdir[idx] != '\0') {
    printf(2, "Container root directory is longer than full path.\n");
    return -1;
  }

  // Corner case: container root directory and full path are of the same.
  if (fpath[idx] == '\0' && cont_rootdir[idx] == '\0') {
    cont_path[0] = '/';
    return 0;
  }

  for (int fidx = 0; fpath[idx] != '\0'; ++idx, ++fidx) {
    cont_path[fidx] = fpath[idx];
  }
  return 0;
}

int main(int argc, char **argv) {
  // Get current working directory.
  char fpath[MAX_PATH_LEN];
  memset(fpath, '\0', MAX_PATH_LEN);
  if (getcwd(fpath) != 0) {
    printf(2, "Getting current working directory error\n");
    exit();
  }
  printf(1, "Full path is %s\n", fpath);

  // Get root directory for the CRUNNING container.
  char cont_rootdir[200];
  memset(cont_rootdir, '\0', 200);
  if (cgetrootdir(cont_rootdir) != 0) {
    printf(2, "Getting root directory of current running container error\n");
    exit();
  }
  printf(1, "Current running directory is %s\n", cont_rootdir);

  // Extract the relative path inside the container.
  char cont_path[MAX_PATH_LEN];
  memset(cont_path, '\0', MAX_PATH_LEN);
  if (extract_cont_path(cont_path, fpath, cont_rootdir) != 0) {
    printf(2, "Extracting relative path inside the container error\n");
    exit();
  }

  printf(1, "%s\n", cont_path);
  exit();
}