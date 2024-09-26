#include <math.h>
#include <fcntl.h>
#include <stdint.h>
#include "arch_head.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

// for the current working directory absolute path
#define CWD_PATHMAX 2048
#define BLOCK_SIZE 512
#define PATHMAX 256

#ifndef BITMASKS
#define BITMASKS
#define CMASK 0x20
#define TMASK 0x10
#define XMASK 0x08
#define VMASK 0x04
#define SMASK 0x02
#define FMASK 0x01
#endif

int ceiling(float num) {
  float whole_num;
  float frac_num;
  frac_num = modff(num, &whole_num);
  
  if (frac_num > 0) {
    return (int) (whole_num + 1);
  } else {
    return (int) whole_num;
  }
}

uint8_t get_param_mask(char* params) {
  if (!params) {
    printf("usage: mytar [ctxvS]f tarfile [ path [ ... ] ]");
    exit(EXIT_FAILURE);
  }
  uint8_t mask;
  int i;
  for (i = 0; params[i]; i++) {
    switch (params[i]) {
    case 'c': mask = mask | CMASK;
      break;
    case 't': mask = mask | TMASK;
      break;
    case 'x': mask = mask | XMASK;
      break;
    case 'v': mask = mask | VMASK;
      break;
    case 'S': mask = mask | SMASK;
      break;
    case 'f': mask = mask | FMASK;
      break;
    default: 
      printf("usage: mytar [ctxvS]f tarfile [ path [ ... ] ]");
      exit(EXIT_FAILURE);
      break;
    }
  }
  if (!(mask & 0x01)) {
    printf("usage: mytar [ctxvS]f tarfile [ path [ ... ] ]");
    exit(EXIT_FAILURE);
  }
  return mask;
}


/* sets end of directory name to '/' regardless if already present
 * or not */
char *set_dir_name(char dir_name[]) {
  int len;
  len = strlen(dir_name);
  if(len >= PATHMAX) {
    fprintf(stderr, "dir_name too long");
    return NULL;
  } else {
    if (dir_name[len-1] == '/') {
      return dir_name;
    } else {
      dir_name[len] = '/';
      dir_name[len+1] = '\0';
      return dir_name;
    }
  }
}

/* 0 on success, -1 on failure */
int append_file(char *fname, char *path, int arch_fd, uint8_t params) {
  header *h;
  if ((h = create_header(fname, path, params)) == NULL) {
    return -1;
  }
  write(arch_fd, h, sizeof(header));
  
  /* if verbose print name */
  if (params & VMASK) {
    printf("%s\n", path);
  }

  struct stat st;
  if (stat(fname, &st)) {
    perror("stat");
    return -1;
  }

  /* if not a regular file then done since not
   * writing anything else */
  if (!(S_ISREG(st.st_mode))) {
    free(h);
    printf("fname: %s\n", fname);
    return 0;
  }
  
  /* write contents to file */
  char buff[BLOCK_SIZE];
  int num_read, num_write, src_fd;
  memset(buff, '\0', BLOCK_SIZE);
  if ((src_fd = open(fname, O_RDONLY)) == -1) {
    perror("open src");
    return -1;
  }
  
  while((num_read = read(src_fd, buff, BLOCK_SIZE)) > 0) {
    if ((num_write = write(arch_fd, buff, BLOCK_SIZE)) == -1) {
      perror("write");
      return -1;
    }
    memset(buff, '\0', BLOCK_SIZE);
  }
  
  if (num_read == -1) {
    perror("read src");
    return -1;
  }

  free(h);
  close(src_fd);  
  return 0;
}


int input_DIR(char *dir_name, char *path,int arch_fd, uint8_t params) {
  //add the current file to tht archive (print if verbose)
  if (strlen(path) < PATHMAX) {
    dir_name = set_dir_name(dir_name);
    path = set_dir_name(path);
    append_file(dir_name, path, arch_fd, params);
  } else {
    fprintf(stderr, "path excedes PATHMAX (256) chars");
    return -1;
  }
  
  // save the cwd so can get back to it at the end
  char cwd[CWD_PATHMAX];
  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("input_DIR; getcwd");
    return -1;
  }

  DIR *dir;
  struct dirent *entry;
  if(chdir(dir_name)) {
    perror("input_DIR; chdir failure");
    return -1;  // return -1 since file was unwritable (skip and continue)
  }
  
  if((dir = opendir(".")) == NULL) {
    perror("input_DIR; opendir failure");
    return -1;
  } 

  struct stat st;
  char fpath[PATHMAX];
  char dname[PATHMAX];
  while ((entry = readdir(dir))) {
    if ((strcmp(entry -> d_name, ".") != 0) && (strcmp(entry -> d_name, "..") != 0)) {
      if ((strlen(entry -> d_name) + strlen(path)) < PATHMAX) {
	memset(fpath, '\0', PATHMAX);
	strcat(fpath, path);
	strcat(fpath, entry -> d_name);
      } else {
	fprintf(stderr, "path excedes PATHMAX (256) chars");
	return -1;
      }
      if (lstat(entry -> d_name, &st)) {
	perror("create_arch lstat failure"); //ask about this (need to skip this or just give up)
      } else if (S_ISDIR(st.st_mode)) {
	//traverse directory and input files in preorder DSF
	memset(dname, '\0', PATHMAX);
	strcat(dname, entry -> d_name);
        input_DIR(dname, fpath, arch_fd, params);
      } else if (S_ISREG(st.st_mode)) {
	//input file into arhive
	append_file(entry -> d_name, fpath, arch_fd, params);
      } else if (S_ISLNK(st.st_mode)) {
	//input symlink into archive
	append_file(entry -> d_name, fpath, arch_fd, params);
      }
    }
  }

  // if this fails give up "were lost :("
  if(chdir(cwd)) {
    perror("input_DIR; chdir failure");
    exit(EXIT_FAILURE);  
  }

  return 0;
}

int insert_EOA(int arch_fd) {
  char buff[BLOCK_SIZE];
  memset(buff, '\0', BLOCK_SIZE);
  if (write(arch_fd, buff, BLOCK_SIZE) != BLOCK_SIZE) {
    perror("write EOA");
    return -1;
  }
  if (write(arch_fd, buff, BLOCK_SIZE) != BLOCK_SIZE) {
    perror("write EOA");
    return -1;
  }
  return 0;
}

/* return fd of new arhive on success, -1 on failure */
int create_arch(char *archname, uint8_t param_mask, char *argv[]) {
  int arch_fd;
  if ((arch_fd = open(archname, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | 
		      S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)) == -1) {
    perror("create_arch");
    exit(EXIT_FAILURE);
  }
  
  struct stat st;
  char path[PATHMAX];
  char dname[PATHMAX];
  for (; argv[optind]; optind++) {
    if (strlen(argv[optind]) < PATHMAX) {
      memset(path, '\0', PATHMAX);
      strcat(path, argv[optind]);
    } else {
      fprintf(stderr, "path excedes PATHMAX (256) chars");
      return -1;
    }
    if (lstat(argv[optind], &st)) {
      perror("create_arch lstat failure"); //ask about this (need to skip this or just give up)
    } else if (S_ISDIR(st.st_mode)) {
      //traverse directory and input files in preorder DSF
      memcpy(dname, path, PATHMAX);
      input_DIR(dname, path, arch_fd, param_mask);
    } else if (S_ISREG(st.st_mode)) {
      //input file into arhive
      append_file(argv[optind], path, arch_fd, param_mask);
    } else if (S_ISLNK(st.st_mode)) {
      //input symlink into archive
      append_file(argv[optind], path, arch_fd, param_mask);
    }
  }
  
  if (insert_EOA(arch_fd) == -1) {
    return -1;
  }

  return arch_fd;
}

/* searches a list of strings for a matching string returns true 
 * if there is a string that matches false otherwise */
int search_str_list(char *str, char **list, int size) {
  int i;
  char *str_dir = malloc(PATHMAX*sizeof(char));
  char *list_dir = malloc(PATHMAX*sizeof(char));
  for (i = 0; i < size; i++) {
    if (strcmp(str, list[i]) == 0) {
      free(list_dir);
      free(str_dir);
      return 1;
    }
    str_dir = strcpy(str_dir, str);
    str_dir = set_dir_name(str_dir);
    list_dir = strcpy(list_dir, list[i]);
    list_dir = set_dir_name(list_dir);
    if (strstr(str_dir, list_dir) != NULL) {
      free(list_dir);
      free(str_dir);
      return 1;
    }
  }
  free(list_dir);
  free(str_dir);
  return 0;
}

/* list only the contents of the archive given as parameters 
 * and the decendents of said parameters */
int list_arch_sel(char *arch_name, uint8_t params, char *argv[]) {
  int arch_fd;
  if ((arch_fd = open(arch_name, O_RDONLY)) == -1) {
    perror("list open");
    return -1;
  }

  /* init list of all files that need to be checked for */
  int size = 10; // arbitrary selection for size of list of files
  int i;
  char **LOF = malloc(size*sizeof(char *));
  for (i = 0; argv[optind] != NULL; i++) {
    LOF[i] = argv[optind++];
    if (i == size) {
      size += 10;
      LOF = realloc(LOF, size*sizeof(char *));
    }
  }
  size = i;
  LOF = realloc(LOF, size*sizeof(char *));
  
  uint8_t *nul_block = calloc(sizeof(header), sizeof(uint8_t));
  char *perm_str, *ugname_str, *mtime_str, *fname_str;
  int num_read;
  float offset;
  header *h = malloc(sizeof(header));
  while((num_read = read(arch_fd, h, BLOCK_SIZE)) > 0) {
    if (num_read == -1) {
      perror("read");
      return -1;
    }
    
    /* if we are at EOA then quit successfully */
    if (memcmp(h, nul_block, BLOCK_SIZE) == 0) {
      if (read(arch_fd, h, BLOCK_SIZE) == -1) {
	perror("read");
	return -1;
      } 
      if (memcmp(h, nul_block, BLOCK_SIZE) == 0) {
	free(nul_block);
	free(h);
	close(arch_fd);
	return 0;
      } else {
	fprintf(stderr, "bad header: lost and quiting...\n");
	exit(EXIT_FAILURE);
      }
    }
    
    /* if bad header then we are lost so quit */
    if (check_valid(h, params)) {
      fprintf(stderr, "bad header: lost and quiting...\n");
      exit(EXIT_FAILURE);
    }
    
    /* check if this is one of the files in LOF or if any of the 
     * file in the LOF are a substring of the current file and if 
     * yes then list said file*/
    fname_str = get_str_fname(h);
    if (search_str_list(fname_str, LOF, size)) {
      /* if verbose then talk more otherwise bare minimum */
      if (params & VMASK) {
	perm_str = get_str_perm(h);
	ugname_str = get_str_ugname(h);
	mtime_str = get_str_mtime(h);
	printf("%10s %17s %8ld %16s %s\n", perm_str, ugname_str, strtol((char *) (h -> size), NULL, 8), 
	       mtime_str, fname_str);
      } else {
	printf("%s\n", fname_str);
      }
    }    
    
    /* seek to the next header */
    offset = (float) strtol((char *) (h -> size), NULL, 8);
    offset = lseek(arch_fd, ceiling(offset / BLOCK_SIZE) * BLOCK_SIZE, SEEK_CUR);
  }
  
  fprintf(stderr, "arch_list error, should not quit here");
  free(nul_block);
  free(h);
  close(arch_fd);
  return -1;
}


/* list all the contents of a given archive */
int list_arch(char *arch_name, uint8_t params) {
  int arch_fd;
  if ((arch_fd = open(arch_name, O_RDONLY)) == -1) {
    perror("list open");
    return -1;
  }
  
  uint8_t *nul_block = calloc(sizeof(header), sizeof(uint8_t));
  char *perm_str, *ugname_str, *mtime_str, *fname_str;
  int num_read;
  float offset;
  header *h = malloc(sizeof(header));
  memset(h, '\0', sizeof(header));
  while((num_read = read(arch_fd, h, BLOCK_SIZE)) > 0) {
    /* if we are at EOA then quit successfully */
    if (memcmp(h, nul_block, BLOCK_SIZE) == 0) {
      if (read(arch_fd, h, BLOCK_SIZE) == -1) {
	perror("read");
	return -1;
      } 
      if (memcmp(h, nul_block, BLOCK_SIZE) == 0) {
	free(nul_block);
	free(h);
	close(arch_fd);
	return 0;
      } else {
	fprintf(stderr, "bad header: lost and quiting...\n");
	exit(EXIT_FAILURE);
      }
    }
    
    /* if bad header then we are lost so quit */
    if (check_valid(h, params)) {
      fprintf(stderr, "bad header: lost and quiting...\n");
      exit(EXIT_FAILURE);
    }
    /* if verbose then talk more otherwise bare minimum */
    if (params & VMASK) {
      perm_str = get_str_perm(h);
      ugname_str = get_str_ugname(h);
      mtime_str = get_str_mtime(h);
      fname_str = get_str_fname(h);
      printf("%10s %17s %8ld %16s %s\n", perm_str, ugname_str, strtol((char *) (h -> size), NULL, 8), 
	     mtime_str, fname_str);
    } else {
      printf("%s\n", get_str_fname(h));
    }
    /* seek to the next header */
    offset = (float) strtol((char *) (h -> size), NULL, 8);
    offset = lseek(arch_fd, ceiling(offset / BLOCK_SIZE) * BLOCK_SIZE, SEEK_CUR);
  }
  
  fprintf(stderr, "arch_list error, should not quit here");
  free(nul_block);
  free(h);
  close(arch_fd);
  return -1;
}

/* extract all files in tar file */
int extract_arch(int arch_fd, uint8_t params) {
  
}


/* main descrip here... */
int main(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt(argc, argv, ":")) != -1) {
    if (opt == '?') {
      fprintf(stderr, "usage: mytar [ctxvS]f tarfile [ path [ ... ] ]\n");
      exit(EXIT_FAILURE);
    }
  }
  
  uint8_t param_mask;
  param_mask = get_param_mask(argv[optind++]);
  char* archive_name;
  if (!(archive_name = argv[optind++])) {
    fprintf(stderr, "usage: mytar [ctxvS]f tarfile [ path [ ... ] ]\n");
    exit(EXIT_FAILURE);
  }
  
  int arch_fd;
  if ((param_mask & CMASK)) {
    if ((arch_fd = create_arch(archive_name, param_mask, argv)) == -1) {
      fprintf(stderr, "error creating archive\n");
      exit(EXIT_FAILURE);
    }
  } else if ((param_mask & TMASK)) {
    if (argv[optind] == NULL) {
      if (list_arch(archive_name, param_mask) == -1) {
	fprintf(stderr, "error listing archive\n");
	exit(EXIT_FAILURE);
      }
    } else {
      if (list_arch_sel(archive_name, param_mask, argv) == -1) {
	fprintf(stderr, "error listing archive\n");
	exit(EXIT_FAILURE);
      }
    }
  } else if ((param_mask & XMASK)) {
    //extract_arch(archive_name, param_mask);
  }

  return 0;
}
