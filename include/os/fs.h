#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0x20221205
#define NUM_FDESCS 16

// ! 大量宏定义, 和superblock有关
#define FS_SIZE  1049346 //512B + 128KB + 512B + 256KB + 512MB
#define FS_START (1 << 14) //8MB开始
#define SUPERBLOCK_START   (FS_START)
#define INODE_MAP_OFFSET   1
#define INODE_MAP_NUM      1   //512B
#define BLOCK_MAP_OFFSET   2
#define BLOCK_MAP_NUM      256 //128KB
#define INODE_BLOCK_OFFSET 258
#define INODE_BLOCK_NUM    512 //256KB
#define DATA_NUM_OFFSET    770
#define DATA_BLOCK_NUM     1048576 //512MB

// ! 和inode相关
#define DIRECT_BLOCK_NUM    (9+5)
#define FIRST_BLOCK_NUM     3
#define SECOND_BLOCK_NUM    2
#define THIRD_BLOCK_NUM     1

#define min(a,b) ((a) < (b) ? (a):(b))
#define max(a,b) ((a) > (b) ? (a):(b))

#define SECTOR_SIZE 512
#define BLOCK_SIZE 4096

/* 总计576MB, 实际上扩充是1K=1000, 还是比较奇怪的, 这里按照1024来, 就会略大一点 */
/**************************************************************************************************
Disk layout:
[ boot block | kernel image ] (8MB)
[ Superblock(512B) | block map(128KB) | inode map(512B) | inode blocks(256KB) | data blocks(512MB) ] 

// ! 更详细一点
-------------------------KERNEL RUNNING ENV--------------------------

|    PART NAME   |  SIZE IN SECTOR  |  SECTOR START  |  SECTOR END   |
|   boot block   |        1         |        0       |       0       | 一个sector即可
|  kernel image  |       ~120       |        1       |     2047      | 分配总计1MB, 2048个sector
|    swap area   |      14336       |      2048      |     16383     | 分配总计7MB, 2048*7个sector

----------------------------FILE SYSTEM------------------------------
|    PART NAME   |   SIZE IN BLOCK  |  SECTOR START  |   SECTOR END  |
|   super block  |        1         |     16384      |     16384     | 1个sector, 从8MB开始,
|    inode map   |        1         |     16385      |     16385     | 支持512项inode 
|    block map   |       256        |     16386      |     16641     | 131072项, 128KB, 共256个sector
|      inode     |       512        |     16642      |     17153     | 
|      data      |     1048576      |     17154      |   1,065,729   | 512MB, 共1024*1024个sector

****************************************************************************************************/


// #define sb sizeof(superblock_t)

/* data structures of file system */
typedef struct superblock_t{
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic;
    uint32_t size;
    uint32_t start;

    uint32_t block_map_offset;
    uint32_t block_map_num;

    uint32_t inode_map_offset;
    uint32_t inode_map_num;

    uint32_t inode_block_offset;
    uint32_t inode_block_num;

    uint32_t data_block_offset;
    uint32_t data_block_num;

    uint8_t padding[468];

} superblock_t;

#define D_NAME 24
// #define den_size sizeof(dentry_t)

typedef struct dentry_t{ // 32Bytes
    // TODO [P6-task1]: Implement the data structure of directory entry
    uint32_t type;
    uint32_t inode_id;
    char name[D_NAME];
} dentry_t;

typedef struct inode_t{ // ! 128bytes
    // TODO [P6-task1]: Implement the data structure of inode
    // ? 仿照ext2的结构
    uint32_t mode;  // 权限
    uint32_t type;  // 目录or文件
    int32_t uid;   // 谁拥有该文件
    uint32_t inode_id;
    uint32_t file_num; // 用来记录目录有多少项
    uint32_t size;  // 多少字节
    // ! 省略和时间相关的四个变量
    uint32_t links_count; // 硬链接个数
    uint32_t blocks;      // 分配块数
    // ? 我尾号为9, 所以直接索引有14个, 接下来为3,2,1
    uint32_t direct_block[DIRECT_BLOCK_NUM];
    uint32_t first_block[FIRST_BLOCK_NUM];
    uint32_t second_block[SECOND_BLOCK_NUM];
    uint32_t third_block[THIRD_BLOCK_NUM];

    uint8_t padding[16];
} inode_t;

// #define INODE_SIZE sizeof(inode_t)

typedef struct fdesc_t{
    // TODO [P6-task2]: Implement the data structure of file descriptor
    uint32_t inode_id;
    uint32_t status;
    uint32_t mode;
    uint32_t rd_offset;
    uint32_t wr_offset;
} fdesc_t;

/* modes of do_fopen */
#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

/* types of inode is dir or file */
#define I_DIR   1  /* Inode is dir */
#define I_FILE  2  /* Inode is file */

/* types of dentry is dir or file */
#define D_DIR   I_DIR  /* dentry is dir */
#define D_FILE  I_FILE  /* dentry is file */
#define D_LINK  3

/* types of file is available or busy */
#define F_AVAILABLE   0  /* file is available */
#define F_OCCUPIED  1  /* file is occupied */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define DIRECT_BLOCK_NUM    (9+5)
#define FIRST_BLOCK_NUM     3
#define SECOND_BLOCK_NUM    2
#define THIRD_BLOCK_NUM     1
/* inode块的容量 */
// ! 一项uint32_t, 为4B, 4KB的块可以存储1024(* 1 << 10), 以此类推, 三级块可以存储1024*1024*1024
// blocksize表一块的大小, size表全部大小
#define DIRECT_SIZE     (DIRECT_BLOCK_NUM)
#define FIRST_BLOCK_SIZE      (1 << 10)
#define SECOND_BLOCK_SIZE     (1 << 20)
#define THIRD_BLOCK_SIZE      (1 << 30)
#define FIRST_LEVEL_SIZE      (FIRST_BLOCK_NUM << 10)
#define SECOND_LEVEL_SIZE     (SECOND_BLOCK_NUM << 20)
#define THIRD_LEVEL_SIZE      (THIRD_BLOCK_NUM << 30)


/* fs function declarations */
extern int do_mkfs(void);
extern int do_statfs(void);
extern int do_cd(char *path);
extern int do_mkdir(char *path);
extern int do_rmdir(char *path);
extern int do_ls(char *path, int option);
extern int do_touch(char *path);
extern int do_cat(char *path);
extern int do_fopen(char *path, int mode);
extern int do_fread(int fd, char *buff, int length);
extern int do_fwrite(int fd, char *buff, int length);
extern int do_fclose(int fd);
extern int do_ln(char *src_path, char *dst_path);
extern int do_rm(char *path);
extern int do_lseek(int fd, int offset, int whence);

void read_superblock();
void write_superblock();

void get_inode_map();
void sync_inode_map();
uint32_t alloc_inode();
void free_inode_map(uint32_t);
void read_inode(uint32_t inode_sct_id);
void write_inode(uint32_t inode_sct_id);
inode_t * get_inode(uint32_t search_inode_id);
inode_t* create_inode(uint32_t inode_id, uint32_t block_id, uint32_t type, uint32_t mode);
void update_inode(inode_t* to_inode);

void get_block_map();
void sync_block_map();
uint32_t alloc_block();
void read_block(uint32_t block_id);
void write_block(uint32_t block_id);
void release_block(uint32_t block_id);
void create_dir_block(uint32_t dir_block_id, uint32_t parent_inode_id, uint32_t dir_inode_id);

int search_dentry(uint32_t search_inode, char *name, uint32_t type);
char* getDir(char * path);
char* getSubdir(char * path);
uint32_t create_dentry(uint32_t dir_id, uint32_t subdir_id, char *name, uint32_t type);
int delete_dentry(uint32_t dir_id, uint32_t subdir_id, /*char *name,*/ uint32_t type);




int is_name_illegal(char *name);
int byte_locate(unsigned *locate_block_id, unsigned *locate_byte, unsigned offset, inode_t *in_inode);
#endif