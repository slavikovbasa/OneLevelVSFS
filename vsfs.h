#define FILE_BLOCKS 5
#define MAX_NAMESIZE 28
#define BLOCK_SIZE 256
#define MAX_FILES_OPENED 256

struct fstat {
    int ftype;
    int nlinks;
    int size;
    int blocks_map[FILE_BLOCKS];
};

struct dir_rec {
    int id;
    char name[MAX_NAMESIZE];
};

int vs_mkfs(char *filename, int dev_size);
int vs_mount(char *filename);
int vs_umount();
int vs_getstat(int id, struct fstat *stat);
int vs_readdir(struct dir_rec *dir_rec, int next);
int vs_create(char *pathname);
int vs_open(char *pathname);
int vs_close(int fd);
int vs_read(int fd, int offset, int size, char *buffer);
int vs_write(int fd, int offset, int size, char *buffer);
int vs_link(char *src_pathname, char *dest_pathname);
int vs_unlink(char *pathname);
int vs_truncate(char *pathname, int size);