#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char buff[64];

int main(void)
{
    int fd = sys_fopen("seek.txt", O_WRONLY);
    uint32_t offset = (1<<20); // ! 1MB

    // write 'hello world!' * 10
    for (int i = 0; i < 10; i++)
    {
        sys_fwrite(fd, "hello world!\n", 13);
        sys_lseek(fd, offset, SEEK_END);
    }
    sys_fclose(fd);

    fd = sys_fopen("seek.txt", O_RDONLY);
    sys_lseek(fd, 0, SEEK_SET);

    // read
    for (int i = 0; i < 10; i++)
    {
        sys_fread(fd, buff, 13);
        sys_lseek(fd, offset, SEEK_CUR);
        for (int j = 0; j < 13; j++)
        {
            printf("%c", buff[j]);
        }
    }

    sys_fclose(fd);

    return 0;
}