#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>

#include "vsfs.h"
#include "vsfs-errors.h"

#define PROMPT "\x1b[32m>\x1b[0m"

enum cmds_enum {
    MKFS_CMD,
    MOUNT_CMD, 
    UMOUNT_CMD, 
    FILESTAT_CMD, 
    LS_CMD, 
    CREATE_CMD, 
    OPEN_CMD,
    CLOSE_CMD,
    READ_CMD,
    WRITE_CMD,
    LINK_CMD, 
    UNLINK_CMD, 
    TRUNCATE_CMD
};

char *commands[] = {
    "mkfs",
    "mount", 
    "umount", 
    "filestat", 
    "ls", 
    "create", 
    "open", 
    "close", 
    "read", 
    "write", 
    "link", 
    "unlink", 
    "truncate"
};

int isMounted = 0;

void exec(char *input, int cmd_id);

int main(int argc, char *argv[]) {
    char input[256];
    int num_cmds = sizeof(commands) / sizeof(commands[0]);
    while (1) {
        printf("%s", PROMPT);

        if (NULL != fgets(input, sizeof(input), stdin)) {
            char *cmd;
            for (int i = 0; i < num_cmds; i++) {
                cmd = commands[i];
                int cmdlen = strlen(cmd);
                if (0 == strncmp(input, "exit", strlen("exit"))) {
                    if (isMounted == 1) {
                        exec(input, UMOUNT_CMD);
                    }
                    exit(0);
                    break;
                } else if (0 == strncmp(input, cmd, cmdlen)) {
                    exec(input + cmdlen + 1, i);
                    break;
                }
            }
        }
    }
}

void exec(char *input, int cmd_id) {

    if (!isMounted && cmd_id != MOUNT_CMD && cmd_id != MKFS_CMD) {
        printf("Error: No file system mounted\n");
        return;
    }

    int len = strlen(input);
    input[len-1] = ' ';

    switch (cmd_id) {
        case MKFS_CMD: {
            int size;
            char *filename, *size_str;
            char *context;
            if (NULL == (filename = strtok_r(input, " ", &context)) ||
                NULL == (size_str = strtok_r(NULL, " ", &context))) {

                printf("Error: Missing argument. Usage: mkfs [file_path] [size]\n");
                return;
            }

            if (size_str[0] < '0' || size_str[0] > '9') {
                printf("Error: Bad size format\n");
                return;
            }

            size = atoi(size_str);

            int err;
            if (!(err = vs_mkfs(filename, size))) {
                printf("VSFS successfully created under image %s\n", filename);
            } else {
                if (err == -CREATE_ERR) printf("Error: Unable to create image\n");
                else if (err == -WRITE_ERR) printf("Error: Unable to write in image\n");
                else if (err == -SIZE_ERR) printf("Error: Too small FS image size\n");
                else printf("Error\n");
            }
            break;
        };
        case MOUNT_CMD: {
            if (!isMounted) {
                char *filename;
                if (NULL == (filename = strtok(input, " "))){
                    printf("Error: Missing argument. Usage: mount [fs_file_pathname]\n");
                    return;
                }

                int err;
                if (!(err=vs_mount(filename))) {
                    isMounted = 1;
                    printf("Filesystem successfully mounted\n");
                } else {
                    if (err == -OPEN_ERR) printf("Error: Unable to open image\n");
                    else if (err == -MARKER_ERR) printf("Error: Is not VSFS image\n");
                    else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                    else printf("Error\n");
                    return;
                }
            } else {
                printf("Error: Already mounted. Need to unmount before mounting new file system\n");
            }
            break;
        };
        case UMOUNT_CMD: {
            int err;
            if (!(err=vs_umount())) {
                isMounted = 0;
                printf("Filesystem successfully unmounted\n");
            } else {
                if (err == -CLOSE_ERR) printf("Error: Unable to close image\n");
                else printf("Error\n");
            }
            break;
        }
        case FILESTAT_CMD: {
            int id = 0;
            char *id_str;
            if (NULL == (id_str = strtok(input, " "))) {
                printf("Error: Missing argument. Usage: filestat [file_id]\n");
                return;
            }

            if (id_str[0] < '0' || id_str[0] > '9') {
                printf("Error: Bad id format\n");
                return;
            }

            id = atoi(id_str);
            struct fstat *fstat = malloc(sizeof(struct fstat));
            int err = vs_getstat(id, fstat);

            if(err == -READ_ERR) printf("Error: Unable to read from image\n");
            else if (err < 0) printf("Error\n");

            if (fstat->nlinks != 0) {
                char *ftype_str = (fstat->ftype == 0)
                    ? "Regular file"
                    : "Directory";
                
                printf("id: %d\ntype: %s\nhard links: %d\nsize: %d\n", 
                            id, ftype_str, fstat->nlinks, fstat->size);
            } else {
                printf("Error: no existing file for such id\n");
            }
            free(fstat);
            break;
        }
        case LS_CMD: {
            int size;
            struct dir_rec *dirrec = malloc(sizeof(struct dir_rec));
            int err = vs_readdir(dirrec, 0);

            if (err == -READ_ERR) printf("Error: Unable to read from image\n");
            else if (err < 0) printf("Error\n");

            while (dirrec->id != END_ID) {
                if (dirrec->id != -1) printf("%d  %s\n", dirrec->id, dirrec->name);
                err = vs_readdir(dirrec, 1);
                
                if(err == -READ_ERR) printf("Error: Unable to read from image\n");
                else if (err < 0) printf("Error\n");
            }
            free(dirrec);
            break;
        }
        case CREATE_CMD: {
            char *pathname;
            if (NULL == (pathname = strtok(input, " "))) {
                printf("Error: Missing argument. Usage: create [file_pathname]\n");
                return;
            }
            int err;
            if (!(err=vs_create(pathname))) {
                printf("File %s successfully created\n", pathname);
            } else {
                if (err == -WRITE_ERR) printf("Error: Unable to write to image\n");
                else if (err == -EXIST_ERR) printf("Error: File already exists\n");
                else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                else if (err == -MAXFILES_ERR) printf("Error: Already created maximum number of files\n");
                else printf("Error\n");
            }
            break;
        }        
        case OPEN_CMD: {
            char *pathname;
            if (NULL == (pathname = strtok(input, " "))) {
                printf("Error: Missing argument. Usage: open [file_pathname]\n");
                return;
            }
            int fd = vs_open(pathname);

            if (fd >= 0) {
                printf("File %s successfully opened with descriptor %d\n", pathname, fd);
            } else {
                if (fd == -MAX_FOPENED_ERR) printf("Error: Unable to open more files\n");
                else if (fd == -NOTEXIST_ERR) printf("Error: File doesn't exist\n");
                else if (fd == -READ_ERR) printf("Error: Unable to read from image\n");
                else printf("Error\n");
            }
            break;
        }
        case CLOSE_CMD: {
            int fd = 0;
            char *fd_str;
            if (NULL == (fd_str = strtok(input, " "))) {
                printf("Error: Missing argument. Usage: close [file_descriptor]\n");
                return;
            }

            if (fd_str[0] < '0' || fd_str[0] > '9') {
                printf("Error: Bad fd format\n");
                return;
            }

            fd = atoi(fd_str);

            int err;
            if ((err = vs_close(fd)) >= 0) {
                printf("File descriptor %d successfully closed\n", fd);
            } else {
                if (err == -BADDESC_ERR) printf("Error: Bad descriptor\n");
                else printf("Error\n");
            }
            break;
        }
        case READ_CMD: {
            int fd, offset, size;
            char *fd_str, *offset_str, *size_str;
            char *context;
            if (NULL == (fd_str = strtok_r(input, " ", &context)) || 
                NULL == (offset_str = strtok_r(NULL, " ", &context)) ||
                NULL == (size_str = strtok_r(NULL, " ", &context))) {

                printf("Error: Missing argument. Usage: read [fd] [offset] [size]\n");
                return;
            }


            if (fd_str[0] < '0' || fd_str[0] > '9') {
                printf("Error: Bad fd format\n");
                return;
            }
            if (offset_str[0] < '0' || offset_str[0] > '9') {
                printf("Error: Bad offset format\n");
                return;
            }
            if (size_str[0] < '0' || size_str[0] > '9') {
                printf("Error: Bad size format\n");
                return;
            }

            fd = atoi(fd_str);
            offset = atoi(offset_str);
            size = atoi(size_str);

            char *buffer = calloc(size, sizeof(char));

            int err;
            if ((err=vs_read(fd, offset, size, buffer)) >= 0 ) {
                printf("%s\n", buffer);
            } else {
                if (err == -BADDESC_ERR) printf("Error: Bad descriptor\n");
                else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                else printf("Error\n");
                return;
            }
            free(buffer);
            break;
        }
        case WRITE_CMD: {
            int fd, offset, size;
            char *fd_str, *offset_str, *size_str;
            char *context;
            if (NULL == (fd_str = strtok_r(input, " ", &context)) ||
                NULL == (offset_str = strtok_r(NULL, " ", &context)) ||
                NULL == (size_str = strtok_r(NULL, " ", &context))) {

                printf("Error: Missing argument. Usage: write [fd] [offset] [size]\n");
                return;
            }
            if (fd_str[0] < '0' || fd_str[0] > '9') {
                printf("Error: Bad fd format\n");
                return;
            }
            if (offset_str[0] < '0' || offset_str[0] > '9') {
                printf("Error: Bad offset format\n");
                return;
            }
            if (size_str[0] < '0' || size_str[0] > '9') {
                printf("Error: Bad size format\n");
                return;
            }
            fd = atoi(fd_str);
            offset = atoi(offset_str);
            size = atoi(size_str);

            char *buffer = calloc(size, sizeof(char));

            for (int i = 0; i < size; i++) {
                buffer[i] = getchar();
            }
            int err;
            if ((err=vs_write(fd, offset, size, buffer)) >= 0) {
                printf("File %d successfully written\n", fd);
            } else {
                if (err == -BADDESC_ERR) printf("Error: Bad descriptor\n");
                else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                else if (err == -WRITE_ERR) printf("Error: Unable to write to image\n");
                else printf("Error\n");
                return;
            }

            free(buffer);
            break;
        }
        case LINK_CMD: {
            char *src_str, *dest_str;
            char *context;
            if (NULL == (src_str = strtok_r(input, " ", &context)) ||
                NULL == (dest_str = strtok_r(NULL, " ", &context))) {

                printf("Error: Missing argument. Usage: link [src_pathname] [dest_pathname]\n");
                return;
            }
            
            int err;
            if (!(err=vs_link(src_str, dest_str))) {
                printf("Hard link successfully created\n");
            } else {
                if (err == -NOTEXIST_ERR) printf("Error: Source doesn't exist\n");
                else if (err == -EXIST_ERR) printf("Error: Destination already exists\n");
                else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                else if (err == -WRITE_ERR) printf("Error: Unable to write to image\n");
                else printf("Error\n");
            }
            break;
        }
        case UNLINK_CMD: {
            char *pathname;
            if (NULL == (pathname = strtok(input, " "))) {
                printf("Error: Missing argument. Usage: unlink [file_pathname]\n");
                return;
            }

            int err;
            if (!(err=vs_unlink(pathname))) {
                printf("File %s successfully unlinked\n", pathname);
            } else {
                if (err == -NOTEXIST_ERR) printf("Error: File doesn't exist\n");
                else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                else if (err == -WRITE_ERR) printf("Error: Unable to write to image\n");
                else printf("Error\n");
            }
            break;
        }
        case TRUNCATE_CMD: {
            int new_size;
            char *pathname, *size_str;
            char *context;
            if (NULL == (pathname = strtok_r(input, " ", &context)) ||
                NULL == (size_str = strtok_r(NULL, " ", &context))) {

                printf("Error: Missing argument. Usage: truncate [pathname] [size]\n");
                return;
            }

            if (size_str[0] < '0' || size_str[0] > '9') {
                printf("Error: Bad size format\n");
                return;
            }

            new_size = atoi(size_str);

            int err;
            if (!(err=vs_truncate(pathname, new_size))) {
                printf("File successfully truncated\n");
            } else {
                if (err == -NOTEXIST_ERR) printf("Error: File doesn't exist\n");
                else if (err == -READ_ERR) printf("Error: Unable to read from image\n");
                else if (err == -WRITE_ERR) printf("Error: Unable to write to image\n");
                else printf("Error\n");
            }
            break;
        }
    }
}
