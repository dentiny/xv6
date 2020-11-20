/*
 * This file provides the functionality of namespace.
 * command line usage:
 * cont create <cont name>
 * cont start <cont name> prog [arg ...]
 * cont pause <cont name>
 * cont resume <cont name>
 * cont stop <cont name>
 */

#include "fcntl.h" 
#include "path_util.h"
#include "types.h"
#include "user.h"
#define MAX_ARG 10
#define BUFFER_SIZE 1024
#define MAX_PATH_LEN 512
#define MAX_CONT_NAME_LEN 15

void usage(char *usage) {
  printf(2, "usage: %s\n", usage);
  exit();
}

void get_filepath(char *fpath, char *rootdir, char *filename) {
  int len1 = strlen(rootdir);
  int len2 = strlen(filename);
  memmove(fpath, rootdir, len1);
  fpath[len1] = '/';
  memmove(fpath + len1 + 1, filename, len2);
  fpath[len1 + len2 + 1] = '\0';
}

int cp_file_to_rootdir(char *rootdir, char *filename) {
  char fpath[MAX_PATH_LEN];
  get_filepath(fpath, rootdir, filename);

  int src_fd = -1;
  int dst_fd = -1;
  if ((src_fd = open(filename, O_RDONLY)) < 0) {
    printf(2, "Open src file %s error.\n", filename);
    goto bad;
  }
  if ((dst_fd = open(fpath, O_CREATE | O_WRONLY)) < 0) {
    printf(2, "Open dst file %s error.\n", fpath);
    goto bad;
  }

  int sz = 0;
  char buffer[BUFFER_SIZE];
  while ((sz = read(src_fd, buffer, BUFFER_SIZE)) != 0) {
		write(dst_fd, buffer, sz);
  }

  close(src_fd);
  close(dst_fd);
  return 0;

bad:
  if (src_fd > 0) {
    close(src_fd);
  }
  if (dst_fd > 0) {
    close(dst_fd);
  }
  return -1;
}

int get_cont_fullpath(char *fpath, char *cont_name) {
  // Get current working directory as prefix.
  char cur_path[MAX_PATH_LEN];
  memset(cur_path, '\0', MAX_PATH_LEN);
  if (getcwd(cur_path) != 0) {
    printf(2, "Get current working directory error\n");
    return -1;
  }
  printf(1, "Current working directory is %s\n", cur_path);
  concatenate_path(fpath, cur_path, cont_name);
  return 0;
}

// Start and resume container needs path checking: current working directory
// to be rootdir for container.
int can_start_container(char *cont_name) {
  char cwd[MAX_PATH_LEN];
  memset(cwd, 0, MAX_PATH_LEN);
  if (getcwd(cwd) != 0) {
    printf(2, "Getting working directory when starting container error\n");
    exit();
  }

  char crootdir[MAX_PATH_LEN];
  memset(crootdir, 0, MAX_PATH_LEN);
  if (getcontrootdir(cont_name, crootdir) != 0) {
    printf(2, "Get root directory for container %s error\n", cont_name);
    exit();
  }

  printf(1, "Current working directory is %s\n", cwd);
  printf(1, "Root directory for container %s is %s\n", cont_name, crootdir);
  return is_prefix_path(cwd, crootdir);
}

void cont_create(int argc, char **argv) {
  if (argc != 3) {
    usage("cont create <cont name>\n");
  } 

  // Check container name is within length limit.
  char *cont_name = argv[2];
  if (strlen(cont_name) > MAX_CONT_NAME_LEN) {
    printf(2, "Container name shouldn't exceed 15 bytes\n");
    exit();
  }

  // Get full path for container.
  char fpath[MAX_PATH_LEN];
  memset(fpath, '\0', MAX_PATH_LEN);
  if (get_cont_fullpath(fpath, cont_name) != 0) {
    exit();
  }
  printf(1, "full path is : %s \n", fpath);

  // Create root directory for the container.
  if (mkdir(fpath) != 0) {
    printf(2, "Create directory as the rootdir %s for container fail.\n", fpath);
    exit();
  }
  printf(1, "directory %s has been created successfully\n", fpath);

  // Create container.
  if (ccreate(fpath) == 0) {
    printf(1, "Container %s created at %s successfully.\n", cont_name, fpath);
  } else {
    if (unlink(fpath) != 0) {
      printf(2, "Remove root directory %s fail.\n", fpath);
    }
    printf(2, "Create container %s at %s fails.\n", cont_name, fpath);
    exit();
  }
}

void cont_start(int argc, char **argv) {
  if (argc < 4 || argc > MAX_ARG + 3) {
    usage("cont start <cont name> prog [arg..]\n");
  }

  // Starting container has to check current working directory: has to be root
  // directory for running container.
  char *cont_name = argv[2];
  if (!can_start_container(cont_name)) {
    printf(2, "Error, starting container has to be in its root directory\n");
    exit();
  }

  // Set the container status as CRUNNABLE.
  int cid = -1;
  if ((cid = cstart(cont_name)) < 0) {
    printf(2, "Start container %s fails.\n");
    exit();
  }
  printf(1, "Start container %s with cid %d succeeds.\n", cont_name, cid);

  // Get process and its argument, ready to execute.
  char *args[MAX_ARG];
  int ii = 3; // index of argv
  int jj = 0; // index of args
  for (; ii < argc; ++ii, ++jj) {
    char *arg = argv[ii];
    int len = strlen(arg);
    args[jj] = malloc(len + 1);
    memmove(args[jj], argv[ii], len);
    args[jj][len] = '\0';
  }
  for (; ii < MAX_ARG; ++ii) {
    args[ii] = 0;
  }

  int pid = cfork(cid);
  if (pid == 0) {
    exec(args[0], args);
    printf(2, "Execute process fails.\n");
    cstop(cont_name);
    exit();
  }
}

void cont_pause(int argc, char **argv) {
  if (argc < 3) {
    usage("cont pause <cont name>\n");
  }

  char *cont_name = argv[2];
  if (cpause(cont_name) != 0) {
    printf(2, "Container %s pause error\n", cont_name);
  } else {
    printf(1, "Container %s pause succeeds\n", cont_name);
  }
}

void cont_resume(int argc, char **argv) {
  if (argc < 3) {
    usage("cont resume <cont name>\n");
  }

  // Starting container has to check current working directory: has to be root
  // directory for running container.
  char *cont_name = argv[2];
  if (!can_start_container(cont_name)) {
    printf(2, "Error, resuming container has to be in its root directory\n");
    exit();
  }

  if (cresume(cont_name) != 0) {
    printf(2, "Container %s resume error\n", cont_name);
  } else {
    printf(1, "Container %s resume succeeds\n", cont_name);
  }
}

void cont_stop(int argc, char **argv) {
  if (argc != 3) {
    usage("cont stop <cont name>\n");
  }

  char *name = argv[2];
  if (cstop(name) != 0) {
    printf(2, "Container %s stop fails.\n", name);
  } else {
    printf(1, "Container %s stop succeeds.\n", name);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf(2, "cont <cmd> [arg...]\n");
    exit();
  }

  if (strcmp(argv[1], "create") == 0) {
    cont_create(argc, argv);
  } else if (strcmp(argv[1], "stop") == 0) {
    cont_stop(argc, argv);
  } else if (strcmp(argv[1], "resume") == 0) {
    cont_resume(argc, argv);
  } else if (strcmp(argv[1], "pause") == 0) {
    cont_pause(argc, argv);
  } else if (strcmp(argv[1], "start") == 0) {
    cont_start(argc, argv);
  } else {
    printf(2, "Command option cannot be identified\n");
  }

  exit();
}