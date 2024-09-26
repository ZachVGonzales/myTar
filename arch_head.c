#include <time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>


#define UIDMAX 0x1FFFFF
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

#ifndef HEADER
#define HEADER
typedef struct __attribute__((__packed__)) header {
  uint8_t name[100];
  uint8_t mode[8];
  uint8_t uid[8];
  uint8_t gid[8];
  uint8_t size[12];
  uint8_t mtime[12];
  uint8_t chksum[8];
  uint8_t typeflag[1];
  uint8_t linkname[100];
  uint8_t magic[6];
  uint8_t version[2];
  uint8_t uname[32];
  uint8_t gname[32];
  uint8_t devmajor[8];
  uint8_t devminor[8];
  uint8_t prefix[155];
  uint8_t pad[8];
} header;
#endif



// init name and prefix using path and header
// return header on success NULL on failure
header *init_name_pre(char *path, header *h) {
  int len;
  if ((len = strlen(path)) > 256) {
    fprintf(stderr, "file path %s too long", path);
    return NULL;
  }

  if (len <= 100) {
    memcpy(h -> name, (uint8_t *) path, len);
  } else {
    memcpy(h -> prefix, (uint8_t *) path, len - 100);
    memcpy(h -> name, (uint8_t *) path + (len - 100), 100);
  }
  return h;
}

uint32_t extract_special_int(char *where, int len) {
  /* For interoperability with GNU tar. GNU seems to
   * set the high–order bit of the first byte, then
   * treat the rest of the field as a binary integer
   * in network byte order.
   * I don’t know for sure if it’s a 32 or 64–bit int, but for
   * this version, we’ll only support 32. (well, 31)
   * returns the integer on success, –1 on failure.
   * In spite of the name of htonl(), it converts int32 t
   */
  int32_t val = -1;
  if ((len >= sizeof(val)) && (where[0] & 0x80)) {
    /* the top bit is set and we have space
     * extract the last four bytes */
    val = *(int32_t *)(where+len-sizeof(val));
    val = ntohl(val); /* convert to host byte order */
  }
  return val;
}


int insert_special_int(char *where, size_t size, int32_t val) {
  /* For interoperability with GNU tar. GNU seems to
   * set the high–order bit of the first byte, then
   * treat the rest of the field as a binary integer
   * in network byte order.
   * Insert the given integer into the given field
   * using this technique. Returns 0 on success, nonzero
   * otherwise
   */
  int err=0;
  if ( val < 0 || ( size < sizeof(val)) ) {
    /* if it’s negative, bit 31 is set and we can’t use the flag
     * if len is too small, we can’t write it. Either way, we’re
     * done.
     */
    err++;
  } else {
    /* game on....*/
    memset(where, 0, size); /* Clear out the buffer */
    *(int32_t *)(where+size-sizeof(val)) = htonl(val); /* place the int*/
    *where |= 0x80; /* set that high–order bit */
  }
  return err;
}


// init stat given fields for header and others that use said fields
// return header on success NULL on failure
header *init_stat(char *path, header *h, uint8_t params) {
  // init stat-given fields
  struct stat st;
  if (lstat(path, &st)) {
    perror("stat");
    return NULL;
  }
  
  // mode
  mode_t valid_mode;
  valid_mode = st.st_mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRUSR | S_IWUSR | S_IXUSR | 
			     S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
  if (snprintf((char *) (h -> mode), 8, "%07o", valid_mode) < 0) {
    perror("snprintf");
    return NULL;
  }
  
  // uid
  if (st.st_uid > UIDMAX) {
    if (params & SMASK) {
      fprintf(stderr, "UID too long, unable to create conforming header, skipping...\n");
      return NULL;
    }
    if (insert_special_int((char *) (h -> uid), 8, st.st_uid)) {
      fprintf(stderr, "insert_special_int");
      return NULL;
    }
  } else {
    if (snprintf((char *) (h -> uid), 8, "%07o", st.st_uid) < 0) {
      perror("snprintf");
      return NULL;
    }
  }
  
  // gid
  if (snprintf((char *) (h -> gid), 8, "%07o", st.st_gid) < 0) {
    perror("snprintf");
    return NULL;
  }

  // size
  if (S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
    strcat((char *) (h -> size), "00000000000");
  } else if (snprintf((char *) (h -> size), 12, "%011o", st.st_size) < 0) {
    perror("snprintf");
    return NULL;
  }

  // mtime
  if (snprintf((char *) (h -> mtime), 12, "%o%o", st.st_mtim.tv_sec, st.st_mtim.tv_nsec) < 0) {
    perror("snprintf");
    return NULL;
  }

  // typeflag and linkname if needed
  if (S_ISREG(st.st_mode)) {
    (h -> typeflag)[0] = (uint8_t) '0';
  } else if (S_ISLNK(st.st_mode)) {
    (h -> typeflag)[0] = (uint8_t) '2';
    if (readlink(path, (char *) (h -> linkname), 100) == -1) {
      perror("readlink");
      return NULL;
    }
  } else if (S_ISDIR(st.st_mode)) {
    (h -> typeflag)[0] = (uint8_t) '5';
  }

  // uname
  struct passwd *pw;
  if ((pw = getpwuid(st.st_uid)) == NULL) {
    perror("getpwuid");
    return NULL;
  }
  memcpy(h -> uname, pw -> pw_name, strlen(pw -> pw_name));

  // gname 
  struct group *gr;
  if ((gr = getgrgid(st.st_gid)) == NULL) {
    perror("getgrgid");
    return NULL;
  }
  memcpy(h -> gname, gr -> gr_name, strlen(gr -> gr_name));

  // dev major and minor
  if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
    if (snprintf((char *) (h -> devmajor), 8, "%o", major(st.st_dev)) < 0) {
      perror("snprintf");
      return NULL;
    }
    if (snprintf((char *) (h -> devminor), 8, "%o", minor(st.st_dev)) < 0) {
      perror("snprintf");
      return NULL;
    }
  }
  
  return h;
}


int sum_of_member(uint8_t member[], int size) {
  int i, sum;
  for (i = 0, sum = 0; i < size; i++) {
    sum += member[i];
  }
  return sum;
} 

// init checksum field of a header
// add all bytes and then 256 more for "spaces" 
header *init_chksum(header *h) {
  int sum;
  sum = 0;
  sum += sum_of_member(h -> name, 100);
  sum += sum_of_member(h -> mode, 8);
  sum += sum_of_member(h -> uid, 8);
  sum += sum_of_member(h -> gid, 8);
  sum += sum_of_member(h -> size, 12);
  sum += sum_of_member(h -> mtime, 12);
  sum += sum_of_member(h -> typeflag, 1);
  sum += sum_of_member(h -> linkname, 100);
  sum += sum_of_member(h -> magic, 6);
  sum += sum_of_member(h -> version, 2);
  sum += sum_of_member(h -> uname, 32);
  sum += sum_of_member(h -> gname, 32);
  sum += sum_of_member(h -> devmajor, 8);
  sum += sum_of_member(h -> devminor, 8);
  sum += sum_of_member(h -> prefix, 155);
  sum += 256;
  

  if (snprintf((char *) (h -> chksum), 8, "%07o", sum) < 0) {
    perror("snprintf");
    return NULL;
  }

  return h;
}


// returns NULL on failure header on success
header *create_header(char *fname, char *path, uint8_t params) {
  header *h = malloc(sizeof(header));
  // init everything to nul first
  memset(h -> name, '\0', 100);
  memset(h -> mode, '\0', 8);
  memset(h -> uid, '\0', 8);
  memset(h -> gid, '\0', 8);
  memset(h -> size, '\0', 12);
  memset(h -> mtime, '\0', 12);
  memset(h -> chksum, '\0', 8);
  memset(h -> typeflag, '\0', 1);
  memset(h -> linkname, '\0', 100);
  memset(h -> magic, '\0', 6);
  memset(h -> version, '\0', 2);
  memset(h -> uname, '\0', 32);
  memset(h -> gname, '\0', 32);
  memset(h -> devmajor, '\0', 8);
  memset(h -> devminor, '\0', 8);
  memset(h -> prefix, '\0', 155);
  memset(h -> pad, '\0', 8);
    
  // name and prefix
  if (init_name_pre(path, h) == NULL) {
    return NULL;
  }
  
  // fields that use stat values
  if (init_stat(fname, h, params) == NULL) {
    return NULL;
  }

  // magic
  strcat((char *) (h -> magic), "ustar");
  
  // version
  (h -> version)[0] = '0';
  (h -> version)[1] = '0';
  
  // now check sum
  init_chksum(h);
  
  return h;
}

/* check if header is valid, valid -> return 0
 * else return -> -1 */
int check_valid(header *h, uint8_t params) {
  int sum;
  sum = 0;
  sum += sum_of_member(h -> name, 100);
  sum += sum_of_member(h -> mode, 8);
  sum += sum_of_member(h -> uid, 8);
  sum += sum_of_member(h -> gid, 8);
  sum += sum_of_member(h -> size, 12);
  sum += sum_of_member(h -> mtime, 12);
  sum += sum_of_member(h -> typeflag, 1);
  sum += sum_of_member(h -> linkname, 100);
  sum += sum_of_member(h -> magic, 6);
  sum += sum_of_member(h -> version, 2);
  sum += sum_of_member(h -> uname, 32);
  sum += sum_of_member(h -> gname, 32);
  sum += sum_of_member(h -> devmajor, 8);
  sum += sum_of_member(h -> devminor, 8);
  sum += sum_of_member(h -> prefix, 155);
  sum += 256;

  /* check sum and magic number */
  if (strtol((char *) (h -> chksum), NULL, 8) != sum) {
    return -1;
  }
  char *magic;
  magic = (char *)(h -> magic);
  if (magic[0] != 'u' || magic[1] != 's' || magic[2] != 't' || magic[3] != 'a' || magic[4] != 'r') {
    return -1;
  }
  
  /* if 'S' option selected then be strict about conformance */
  if (params & SMASK) {
    if (extract_special_int((char *)(h -> uid), 8) != -1) {
      return -1;
    }
    if (magic[5] != '\0') {
      return -1;
    }
    if (((h -> version)[0] != '0') || ((h -> version)[1] != '0')) {
      return -1;
    }
  }
  return 0;
}


char *get_str_perm(header *h) {
  static char perms[11];
  int mode = strtol((char *) (h -> mode), NULL, 8);
  /* type */
  if ((char)(h -> typeflag)[0] == '5') {
    perms[0] = 'd';
  } else if ((char)(h -> typeflag)[0] == '2') {
    perms[0] = 'l';
  } else {
    perms[0] = '-';
  }

  /* user permisions */
  if (mode & S_IRUSR) {
    perms[1] = 'r';
  } else {
    perms[1] = '-';
  }
  if (mode & S_IWUSR) {
    perms[2] = 'w';
  } else {
    perms[2] = '-';
  }
  if (mode & S_IXUSR) {
    perms[3] = 'x';
  } else {
    perms[3] = '-';
  }

  /* group permisions */
  if (mode & S_IRGRP) {
    perms[4] = 'r';
  } else {
    perms[4] = '-';
  }
  if (mode & S_IWGRP) {
    perms[5] = 'w';
  } else {
    perms[5] = '-';
  }
  if (mode & S_IXGRP) {
    perms[6] = 'x';
  } else {
    perms[6] = '-';
  }

  /* other permisions */
  if (mode & S_IROTH) {
    perms[7] = 'r';
  } else {
    perms[7] = '-';
  }
  if (mode & S_IWOTH) {
    perms[8] = 'w';
  } else {
    perms[8] = '-';
  }
  if (mode & S_IXOTH) {
    perms[9] = 'x';
  } else {
    perms[9] = '-';
  }

  perms[10] = '\0';
  return perms;
}

/* get user and groupname as one string */
char *get_str_ugname(header *h) {
  static char ugname[64];
  memset(ugname, '\0', 64);
  strcat(ugname, (char *) (h -> uname));
  strcat(ugname, "/");
  strcat(ugname, (char *) (h -> gname));
  return ugname;
}

/* get the string of the time given nanoseconds in header */
char *get_str_mtime(header *h) {
  time_t seconds;
  struct tm *time;
  static char mtime[17];
  seconds = (time_t) strtol((char *) (h -> mtime), NULL, 8);
  time = localtime(&seconds);
  strftime(mtime, sizeof(mtime), "%Y-%m-%d %H:%M", time);
  return mtime;
}

/* gets the filename and returns as a string */
char *get_str_fname(header *h) {
  static char fname[PATHMAX];
  memset(fname, '\0', PATHMAX);
  strcat(fname, (char *) (h -> prefix));
  strcat(fname, (char *) (h -> name));
  return fname;
}


/* main used for testing header creation */
/*int main(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt(argc, argv, ":")) != -1) {
    if (opt == '?') {
      fprintf(stderr, "usage: header file");
      exit(EXIT_FAILURE);
    }
  }
  
  header *h;
  int fd;
  fd = open(argv[optind+1], O_WRONLY | O_CREAT | O_TRUNC);
  h = create_header(argv[optind]);
  write(fd, h, sizeof(header));
  return 0;
}
*/
