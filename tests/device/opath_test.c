#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd;

	if (argc != 2)
		return 2;
	fd = open(argv[1], O_PATH | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	printf("OPEN_OK %d\n", fd);
	close(fd);
	return 0;
}
