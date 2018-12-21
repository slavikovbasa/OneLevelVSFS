#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "vsfs.h"
#include "vsfs-errors.h"

const char *start_marker = "VSFSIMG\0";

struct header {
    int dev_size;
    int block_size;
    int nblocks;
    int nfiles_max;
};

int dev_id;
struct header h;
int descrs_tab[MAX_FILES_OPENED];

int next_descriptor();
int next_free_block();
int occupy_block(int i);

int occupy_next_block();
int get_fstattab_offset();
int get_dirtab_offset();
int get_blocks_offset();
int read_dirtab(struct dir_rec *dirtab);
int read_fstattab(struct fstat *fstattab);
int write_fstat(struct fstat *stat, int id);
int write_dir_rec(struct dir_rec *dirrec, int i);
int get_block_id(struct fstat *stat, int block_offset, int create);
int free_block(int blockid);
int free_all_under_from(int blockid, int start);


int vs_mkfs(char *filename, int dev_size){
    dev_id = open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);

    if (dev_id < 0) return -CREATE_ERR;

    if (write(dev_id, start_marker, sizeof(start_marker)) < 0)
        return -WRITE_ERR;

    /*
      Total number or blocks for files in an image for chosen dev_size
      Considering that image consists of the:
      marker, header, free blocks bitmap, inode table, root directory table and file blocks
      max number of files is taken as equal to nblocks/2
    */
    int nblocks = (dev_size - sizeof(start_marker) - sizeof(struct header) - sizeof(struct dir_rec))
        / (BLOCK_SIZE + sizeof(char) + sizeof(struct fstat)/2 + sizeof(struct dir_rec)/2);

    if (nblocks < 2) return -SIZE_ERR;

    h.dev_size = dev_size;
    h.block_size = BLOCK_SIZE;
    h.nblocks = nblocks;
    h.nfiles_max = nblocks / 2;
    if (write(dev_id, &h, sizeof(h)) < 0)
        return -WRITE_ERR;

    char *bitmap_buf = malloc(nblocks * sizeof(char));
    memset(bitmap_buf, 0, nblocks);
    if (write(dev_id, bitmap_buf, nblocks) < 0) {
        free(bitmap_buf);
        return -WRITE_ERR;
    }
    free(bitmap_buf);

    int fstattab_size = h.nfiles_max * sizeof(struct fstat);
    int dirtab_size = (h.nfiles_max + 1) * sizeof(struct dir_rec);
    struct fstat *fstat_buf = malloc(fstattab_size);
    struct dir_rec *dirtab_buf = malloc(dirtab_size);
    int i;
    for (i = 0; i < h.nfiles_max; i++) {
            fstat_buf[i].ftype = -1;
            fstat_buf[i].nlinks = 0;
            fstat_buf[i].size = 0;
            for (int j = 0; j < FILE_BLOCKS; j++)
                fstat_buf[i].blocks_map[j] = -1;

            dirtab_buf[i].id = -1;
            for (int j = 0; j < MAX_NAMESIZE; j++)
                dirtab_buf[i].name[j] = 0;
    }
    dirtab_buf[i].id = END_ID;
    for (int j = 0; j < MAX_NAMESIZE; j++)
        dirtab_buf[i].name[j] = 0;

    if (write(dev_id, fstat_buf, fstattab_size) < 0 ||
            write(dev_id, dirtab_buf, dirtab_size) < 0) {
        free(fstat_buf);
        free(dirtab_buf);
        return -WRITE_ERR;
    }
    free(fstat_buf);
    free(dirtab_buf);

    int nblocks_size = h.block_size * h.nblocks;
    char *blocks_buf = calloc(nblocks_size, 1);
    if (write(dev_id, blocks_buf, nblocks_size) < 0) {
        free(blocks_buf);
        return -WRITE_ERR;
    }
    free(blocks_buf);
    return 0;
}

int vs_mount(char *filename) {
    dev_id = open(filename, O_RDWR, S_IRWXU);
    if (dev_id < 0) return -OPEN_ERR;

    int marker_size = sizeof(start_marker);
    char *marker = malloc(marker_size);

    if (read(dev_id, marker, marker_size) < 0) {
        free(marker);
        return -READ_ERR;
    }

    if (0 != strcmp(marker, start_marker)) {
        free(marker);
        return -MARKER_ERR;
    }
    free(marker);

    if (read(dev_id, &h, sizeof(struct header)) < 0)
        return -READ_ERR;

    for (int i = 0; i < MAX_FILES_OPENED; i++)
        descrs_tab[i] = -1;

    return 0;
}

int vs_umount() {
    h.dev_size = -1;
    h.block_size = -1;
    h.nblocks = -1;
    h.nfiles_max = -1;
    for (int i = 0; i < MAX_FILES_OPENED; i++)
        if (descrs_tab[i] >= 0) vs_close(i);

    if (close(dev_id) < 0) return -CLOSE_ERR;
    dev_id = -1;

    return 0;
}

int vs_getstat(int id, struct fstat *stat) {
    int fstat_offset =
                get_fstattab_offset() + id * sizeof(struct fstat);

    if (lseek(dev_id, fstat_offset, SEEK_SET) < 0)
        return -READ_ERR;

    if (read(dev_id, stat, sizeof(struct fstat)) < 0)
        return -READ_ERR;

    return 0;
}

//if next == 0 then first record is read, 
//else all the records are read sequentially
int vs_readdir(struct dir_rec *dir_rec, int next) {
    if (!next) {    
        if (lseek(dev_id, get_dirtab_offset(), SEEK_SET) < 0)
            return -READ_ERR;
    }

    if (read(dev_id, dir_rec, sizeof(struct dir_rec)) < 0)
        return -READ_ERR;
    
    return 0;
}

int vs_create(char *pathname) {
    int dirtab_size = sizeof(struct dir_rec) * (h.nfiles_max + 1);
    struct dir_rec *dirtab = malloc(dirtab_size);
    if (read_dirtab(dirtab) < 0) {
        free(dirtab);
        return -READ_ERR;
    }

    int i;
    for (i = 0; i < h.nfiles_max && dirtab[i].id >= 0; i++) {
        if (0 == strcmp(dirtab[i].name, pathname)) {
            free(dirtab);
            return -EXIST_ERR;
        }
    }
    free(dirtab);
    if (i >= h.nfiles_max) return -MAXFILES_ERR;
    
    int fstattab_size = sizeof(struct fstat) * h.nfiles_max;
    struct fstat *fstattab = malloc(fstattab_size);
    if (read_fstattab(fstattab) < 0) {
        free(fstattab);
        return -READ_ERR;
    }
    
    int id;
    for (id = 0; id < h.nfiles_max && fstattab[id].nlinks > 0; id++)
        ;
    free(fstattab);
    if (id >= h.nfiles_max) return -MAXFILES_ERR;
    
    struct fstat stat = {
        .ftype = 0,
        .nlinks = 1,
        .size = 0
    };
    for (int j = 0; j < FILE_BLOCKS; j++)
        stat.blocks_map[j] = -1;

    struct dir_rec dirrec = {
        .id = id
    };
    int j;
    for (j = 0; j < MAX_NAMESIZE && pathname[j] > 0; j++)
        dirrec.name[j] = pathname[j];

    for (; j < MAX_NAMESIZE; j++)
        dirrec.name[j] = 0;

    if (write_fstat(&stat, id) < 0)
        return -WRITE_ERR;

    if (write_dir_rec(&dirrec, i) < 0)
        return -WRITE_ERR;
    
    return 0;
}

int vs_open(char *pathname) {
    int dirtab_size = sizeof(struct dir_rec) * (h.nfiles_max + 1);
    struct dir_rec *dirtab_buf = malloc(dirtab_size);

    if (read_dirtab(dirtab_buf) < 0) {
        free(dirtab_buf);
        return -READ_ERR;
    }

    for (int i = 0; i < h.nfiles_max; i++) {
        if (0 == strcmp(dirtab_buf[i].name, pathname)) {
            int desc = next_descriptor();
            if (desc < 0) {
                free(dirtab_buf);
                return -MAX_FOPENED_ERR;
            }
            descrs_tab[desc] = dirtab_buf[i].id;
            free(dirtab_buf);
            return desc;
        }
    }
    free(dirtab_buf);
    return -NOTEXIST_ERR;
}

int vs_close(int fd) {
    if (fd < 0 || fd >= MAX_FILES_OPENED || descrs_tab[fd] == -1)
        return -BADDESC_ERR;

    descrs_tab[fd] = -1;
    return 0;
}

int vs_read(int fd, int offset, int size, char *buffer) {
    if (fd < 0 || fd >= MAX_FILES_OPENED || descrs_tab[fd] == -1)
        return -BADDESC_ERR;

    int id = descrs_tab[fd];
    struct fstat *stat = malloc(sizeof(struct fstat));
    if (vs_getstat(id, stat) < 0) {
        free(stat);
        return -READ_ERR;
    }
    
    if (offset >= stat->size) {
        free(stat);
        return 0;
    }

    int block_offset = offset / h.block_size;
    int byte_offset = offset - block_offset * h.block_size;

    int full_size = size;

    while (size > 0) {
        int blockid = get_block_id(stat, block_offset, 0);

        if (blockid < 0) {
            free(stat);
            return full_size - size;
        }
        int read_offset = get_blocks_offset()
                          + blockid * h.block_size
                          + byte_offset;

        if (lseek(dev_id, read_offset, SEEK_SET) < 0) {
            free(stat);
            return -READ_ERR;
        }
        
        int rem = h.block_size - byte_offset;
        if (rem >= size) {
            int rsize;
            if ((rsize = read(dev_id, buffer, size)) < 0){
                free(stat);
                return -READ_ERR;
            }
            size -= rsize;
            free(stat);
            return full_size - size;
        } else {
            int rsize;
            if ((rsize = read(dev_id, buffer, rem)) < 0) {
                free(stat);
                return -READ_ERR;
            }
            if (rsize == 0) {
                free(stat);
                return full_size - size;
            }
            buffer += rsize;
            size -= rsize;
            byte_offset = 0;
            block_offset++;
        }
    }
    free(stat);
    return full_size;
}

int vs_write(int fd, int offset, int size, char *buffer) {
    if (fd < 0 || fd >= MAX_FILES_OPENED || descrs_tab[fd] == -1)
        return -BADDESC_ERR;

    int id = descrs_tab[fd];
    struct fstat *stat = malloc(sizeof(struct fstat));

    int fstat_offset = get_fstattab_offset()
                       + id * sizeof(struct fstat);

    if (vs_getstat(id, stat) < 0) {
        free(stat);
        return -READ_ERR;
    }

    int writesize = size;
    int null_size = 0;
    if (offset > stat->size) {
        null_size = offset - stat->size;
        writesize += null_size;
        offset = stat->size;
    }
    char *writebuf = malloc(writesize * sizeof(char));
    int i;
    for (i = 0; i < null_size; i++)
        writebuf[i] = '\0';
    for (; i < writesize; i++)
        writebuf[i] = buffer[i];

    int block_offset = offset / h.block_size;
    int byte_offset = offset - block_offset * h.block_size;

    int full_size = writesize;

    while (writesize > 0) {
        int blockid = get_block_id(stat, block_offset, 1);


        if (blockid < 0) {
            if (blockid == -EOF_ERR 
                || blockid == -1) return full_size - writesize;
            else return blockid;
        }
        
        int write_offset = get_blocks_offset()
                           + blockid * h.block_size
                           + byte_offset;

        if (lseek(dev_id, write_offset, SEEK_SET) < 0) {
            free(stat);
            free(writebuf);
            return -WRITE_ERR;
        }

        int rem = h.block_size - byte_offset;
        if (rem >= writesize) {
            int wsize;
            if ((wsize = write(dev_id, writebuf, writesize)) < 0){
                free(stat);
                free(writebuf);
                return -WRITE_ERR;
            }
            if (stat->size < offset + wsize) {
                stat->size = offset + wsize;
                if (write_fstat(stat, id)) {
                    free(stat);
                    free(writebuf);
                    return -WRITE_ERR;
                }
            }
            writesize -= wsize;
            free(stat);
            free(writebuf);
            return full_size - writesize;
        } else {
            int wsize;
            if ((wsize = write(dev_id, writebuf, rem)) < 0) {
                free(stat);
                free(writebuf);
                return -WRITE_ERR;
            }
            if (wsize == 0) {
                free(stat);
                free(writebuf);
                return full_size - writesize;
            }
            writebuf += wsize;
            writesize -= wsize;
            byte_offset = 0;
            block_offset++;
            offset += h.block_size;
            if (stat->size < offset + wsize) {
                stat->size = offset + wsize;
                if (write_fstat(stat, id)) {
                    free(stat);
                    free(writebuf);
                    return -WRITE_ERR;
                }
            }
        }
    }
    free(stat);
    return full_size;
}

int vs_link(char *src_pathname, char *dest_pathname) {
    int dirtab_size = sizeof(struct dir_rec) * (h.nfiles_max + 1);
    struct dir_rec *dirtab_buf = malloc(dirtab_size);
    if (read_dirtab(dirtab_buf) < 0) {
        free(dirtab_buf);
        return -READ_ERR;
    }

    struct fstat *stat = malloc(sizeof(struct fstat));
    int id = -1;
    int i = -1;
    for (int j = 0; j < h.nfiles_max; j++) {
        if (i == -1 && dirtab_buf[j].id == -1) i = j;

        if (0 == strcmp(dirtab_buf[j].name, src_pathname)) {
            if (vs_getstat(dirtab_buf[j].id, stat) < 0) {
                free(dirtab_buf);
                free(stat);
                return -READ_ERR;
            }
            id = dirtab_buf[j].id;
        }
        if (0 == strcmp(dirtab_buf[j].name, dest_pathname)){
            free(dirtab_buf);
            free(stat);
            return -EXIST_ERR;
        }
    }
    free(dirtab_buf);
    if (id < 0) { 
        free(stat);
        return -NOTEXIST_ERR;
    }
    struct dir_rec newrec = {
        .id = id
    };
    int j;
    for (j = 0; j < MAX_NAMESIZE && dest_pathname[j] > 0; j++)
        newrec.name[j] = dest_pathname[j];

    for (; j < MAX_NAMESIZE; j++)
        newrec.name[j] = 0;

    stat->nlinks += 1;
    
    if (write_fstat(stat, id) < 0) {
        free(stat);
        return -WRITE_ERR;
    }

    if (write_dir_rec(&newrec, i) < 0) {
        free(stat);
        return -WRITE_ERR;
    }
        
    free(stat);
    return 0;
}

int vs_unlink(char *pathname) {
    int dirtab_size = sizeof(struct dir_rec) * (h.nfiles_max + 1);
    struct dir_rec *dirtab_buf = malloc(dirtab_size);
    if (read_dirtab(dirtab_buf) < 0) {
        free(dirtab_buf);
        return -READ_ERR;
    }

    struct fstat *stat = malloc(sizeof(struct fstat));
    for (int i = 0; i < h.nfiles_max; i++) {
        if (0 == strcmp(dirtab_buf[i].name, pathname)) {
            if (vs_getstat(dirtab_buf[i].id, stat) < 0) {
                free(dirtab_buf);
                free(stat);
                return -READ_ERR;
            }
            int id = dirtab_buf[i].id;
            for (int j = 0; j < MAX_NAMESIZE; j++)
                dirtab_buf[i].name[j] = '\0';

            dirtab_buf[i].id = -1;
            if (write_dir_rec(&dirtab_buf[i], i) < 0) {
                free(dirtab_buf);
                free(stat);
                return -WRITE_ERR;
            }
            free(dirtab_buf);
            stat->nlinks--;
            if (stat->nlinks == 0) {
                stat->ftype = -1;
                stat->size = 0;
                for (int j= 0; j < FILE_BLOCKS-1; j++) {
                    free_block(stat->blocks_map[j]);
                    stat->blocks_map[j] = -1;
                }

                if (stat->blocks_map[FILE_BLOCKS-1] >= 0) {
                    int *blocks = malloc(h.block_size);
                    if (free_all_under_from(stat->blocks_map[FILE_BLOCKS-1], 0) < 0) {
                        free(blocks);
                        free(stat);
                        return -WRITE_ERR;
                    }
                    free(blocks);
                    stat->blocks_map[FILE_BLOCKS-1] = -1;
                }
            }
            if (write_fstat(stat, id) < 0) {
                free(stat);
                return -WRITE_ERR;
            }
            free(stat);
            return 0;
        }
    }
    free(dirtab_buf);
    free(stat);
    return -NOTEXIST_ERR;
}

int vs_truncate(char *pathname, int size) {
    int dirtab_size = sizeof(struct dir_rec) * (h.nfiles_max + 1);
    struct dir_rec *dirtab_buf = malloc(dirtab_size);
    if (read_dirtab(dirtab_buf) < 0) {
        free(dirtab_buf);
        return -READ_ERR;
    }

    struct fstat *stat = malloc(sizeof(struct fstat));
    for (int i = 0; i < h.nfiles_max; i++) {
        if (0 == strcmp(dirtab_buf[i].name, pathname)) {
            if (vs_getstat(dirtab_buf[i].id, stat) < 0) {
                free(dirtab_buf);
                free(stat);
                return -READ_ERR;
            }
            if (size < stat->size) {
                int new_block_size = size / h.block_size;
                int old_block_size = stat->size / h.block_size;

                if (old_block_size < FILE_BLOCKS-1) {
                    for (int i = new_block_size + 1; i < FILE_BLOCKS-1; i++) {
                        if (stat->blocks_map[i] == -1) break;
                        free_block(stat->blocks_map[i]);
                        stat->blocks_map[i] = -1;
                    }
                } else {
                    if (new_block_size < FILE_BLOCKS-1) {
                        for (int i = new_block_size + 1; i < FILE_BLOCKS-1; i++) {
                            free_block(stat->blocks_map[i]);
                            stat->blocks_map[i] = -1;
                        }
                        free_all_under_from(stat->blocks_map[FILE_BLOCKS-1], 0);
                        stat->blocks_map[FILE_BLOCKS-1] = -1;
                    } else {
                        free_all_under_from(stat->blocks_map[FILE_BLOCKS-1], new_block_size-FILE_BLOCKS+2);
                    }
                }
                stat->size = size;
                write_fstat(stat, dirtab_buf[i].id);
                free(dirtab_buf);
                free(stat);
                return 0;
            } else if (size > stat->size) {
                int fd = vs_open(pathname);
                if (fd < 0 || vs_write(fd, size - stat->size, 0, NULL) < 0) {
                    free(dirtab_buf);
                    free(stat);
                    vs_close(fd);
                    return -WRITE_ERR;
                }
                vs_close(fd);
                free(dirtab_buf);
                free(stat);
                return 0;
            } else {
                free(dirtab_buf);
                free(stat);
                return 0;
            }
        }
    }
    free(dirtab_buf);
    free(stat);
    return -NOTEXIST_ERR;
}


int next_descriptor() {
    int i = 0;
    for (i = 0; i < MAX_FILES_OPENED && descrs_tab[i] != -1; i++)
        ;
    if (i >= MAX_FILES_OPENED) return -1;
    else return i;
}

int next_free_block() {
    int read_offset = sizeof(start_marker) + sizeof(struct header);

    if (lseek(dev_id, read_offset, SEEK_SET) < 0)
        return -READ_ERR;
    

    char *blocks_bitmap = malloc(h.nblocks * sizeof(char));
    if (read(dev_id, blocks_bitmap, h.nblocks * sizeof(char)) < 0) {
        free(blocks_bitmap);
        return -READ_ERR;
    }
    int i;
    for (i = 0; i < h.nblocks && blocks_bitmap[i] != 0; i++)
        ;
    free(blocks_bitmap);
    if (i >= h.nblocks) return -1;
    else return i;
}

int occupy_block(int i) {
    int write_offset = sizeof(start_marker)
                       + sizeof(struct header)
                       + i * sizeof(char);
    char c;
    if (lseek(dev_id, write_offset, SEEK_SET) < 0)
        return -READ_ERR;
    if (read(dev_id, &c, 1) < 0)
        return -READ_ERR;

    if (c) return -WRITE_ERR;

    char one = 1;

    if (lseek(dev_id, write_offset, SEEK_SET) < 0)
        return -WRITE_ERR;
    if (write(dev_id, &one, 1) < 0)
        return -WRITE_ERR;
    
    return 0;
}

int occupy_next_block() {
    int new_blockid = next_free_block();
    if (new_blockid < 0 || occupy_block(new_blockid) < 0)
        return -EOF_ERR;
                
    return new_blockid;
}

int get_fstattab_offset() {
    return sizeof(start_marker) 
            + sizeof(struct header) 
            + h.nblocks * sizeof(char);
}

int get_dirtab_offset() {
    return sizeof(start_marker)
            + sizeof(struct header)
            + h.nblocks * sizeof(char)
            + h.nfiles_max * sizeof(struct fstat);
}

int get_blocks_offset() {
    return sizeof(start_marker)
            + sizeof(struct header)
            + h.nblocks * sizeof(char)
            + h.nfiles_max * sizeof(struct fstat)
            + (h.nfiles_max + 1) * sizeof(struct dir_rec);
}

int read_dirtab(struct dir_rec *dirtab) {
    int dirtab_size = sizeof(struct dir_rec) * (h.nfiles_max + 1);

    if (lseek(dev_id, get_dirtab_offset(), SEEK_SET) < 0)
        return -READ_ERR;
    if (read(dev_id, dirtab, dirtab_size) < 0)
        return -READ_ERR;
    return 0;
}

int read_fstattab(struct fstat *fstattab) {
    int fstattab_size = sizeof(struct fstat) * h.nfiles_max;

    if (lseek(dev_id, get_fstattab_offset(), SEEK_SET) < 0)
        return -READ_ERR;

    if (read(dev_id, fstattab, fstattab_size) < 0)
        return -READ_ERR;

    return 0;
}

int write_fstat(struct fstat *stat, int id) {
    if (lseek(dev_id, get_fstattab_offset() + id * sizeof(struct fstat), SEEK_SET) < 0
            || write(dev_id, stat, sizeof(struct fstat)) < 0)
        return -WRITE_ERR;
    
    return 0;
}

int write_dir_rec(struct dir_rec *dirrec, int i) {
    if (lseek(dev_id, get_dirtab_offset() + i*sizeof(struct dir_rec), SEEK_SET) < 0
            || write(dev_id, dirrec, sizeof(struct dir_rec)) < 0)
        return -WRITE_ERR;

    return 0;
}

int get_block_id(struct fstat *stat, int block_offset, int create) {
    if (block_offset < FILE_BLOCKS-1) {
        int id = stat->blocks_map[block_offset];
        if (id >= 0 || !create) {
            return id;
        } else {
            int new_blockid = occupy_next_block();
            if (new_blockid < 0) return -EOF_ERR;
            stat->blocks_map[block_offset] = new_blockid;
            return new_blockid;
        }
    } else {
        if (block_offset-FILE_BLOCKS+1 >= h.block_size / sizeof(int))
            return -EOF_ERR;
            
        if (stat->blocks_map[FILE_BLOCKS-1] < 0) {
            if (!create) {
                return -EOF_ERR;
            } else {
                int new_blockid = occupy_next_block();
                if (new_blockid < 0) return -EOF_ERR;
                stat->blocks_map[FILE_BLOCKS-1] = new_blockid;
                int *blocks = malloc(h.block_size);
                blocks[0] = occupy_next_block();
                for (int i = 1; i < h.block_size/sizeof(int); i++)
                    blocks[i] = -1;
                
                if (lseek(dev_id, get_blocks_offset() + new_blockid*h.block_size, SEEK_SET) < 0
                        || write(dev_id, blocks, h.block_size) < 0) {
                    free(blocks);
                    return -WRITE_ERR;
                }
                free(blocks);
            }
        }

        int *blocks = malloc(h.block_size);
        int read_offset = 
                get_blocks_offset() + stat->blocks_map[FILE_BLOCKS-1] * h.block_size;
        if (lseek(dev_id, read_offset, SEEK_SET) < 0 
                || read(dev_id, blocks, h.block_size) < 0) {
            free(blocks);
            return -READ_ERR;
        }
        int id = blocks[block_offset-FILE_BLOCKS+1];
        if (id >= 0 || !create) {
            free(blocks);
            return id;
        } else {
            int new_blockid = occupy_next_block();
            blocks[block_offset-FILE_BLOCKS+1] = new_blockid;
            if (lseek(dev_id, get_blocks_offset()+stat->blocks_map[FILE_BLOCKS-1]*h.block_size, SEEK_SET) < 0
                    || write(dev_id, blocks, h.block_size) < 0) {
                free(blocks);
                return -WRITE_ERR;
            }
            free(blocks);
            return new_blockid;
        }
    }
}

int free_block(int blockid) {
    if (blockid < 0) {
        return 0;
    }
    int write_offset = sizeof(start_marker)
                        + sizeof(struct header)
                        + blockid * sizeof(char);
    char c = 0;
    if (lseek(dev_id, write_offset, SEEK_SET) < 0 || write(dev_id, &c, 1) < 0)
        return -WRITE_ERR;
    return 0;
}

int free_all_under_from(int blockid, int start) {
    int *blocks = malloc(h.block_size);
    if (lseek(dev_id, get_blocks_offset() + blockid*h.block_size, SEEK_SET) < 0
                || read(dev_id, blocks, h.block_size) < 0) {
        free(blocks);
        return -WRITE_ERR;
    }
    for (int i = start; i < h.block_size/sizeof(int); i++) {
        int block_id = blocks[i];
        if (block_id < 0)
            break;
        
        blocks[i] = -1;
        if (free_block(block_id) < 0) {
            free(blocks);
            return -WRITE_ERR;
        }
    }
    if (lseek(dev_id, get_blocks_offset() + blockid*h.block_size, SEEK_SET) < 0
                || write(dev_id, blocks, h.block_size) < 0) {
        free(blocks);
        return -WRITE_ERR;
    }
    free(blocks);
    return 0;
}
