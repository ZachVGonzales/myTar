#ifndef ARCH_HEAD
#define ARCH_HEAD

#ifndef HEADER
#define HEADER
typedef struct __attribute__((__packed__)) header {
  uint8_t name[100];
  uint8_t mode[8];
  uint8_t uid[8];
  uint8_t gid[8];
  uint8_t size[12];
  uint8_t mtime[12];
  uint8_t chksum[12];
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

header *create_header(char *fname, char *path, uint8_t params);

int check_valid(header *h, uint8_t params);

char *get_str_perm(header *h);

char *get_str_ugname(header *h);

char *get_str_mtime(header *h);

char *get_str_fname(header *h);
#endif
