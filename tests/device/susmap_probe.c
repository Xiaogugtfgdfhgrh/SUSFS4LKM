#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 2;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        return 1;
    }
    size_t length = st.st_size > 0 ? (size_t)st.st_size : 4096;
    void *mapped = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    volatile char touch = ((const char *)mapped)[0];
    (void)touch;

    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        perror("maps");
        return 1;
    }
    char line[4096];
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, argv[1])) {
            fputs(line, stdout);
            found = 1;
        }
    }
    fclose(maps);
    printf("MAP_RESULT %s\n", found ? "VISIBLE" : "HIDDEN");
    return 0;
}
