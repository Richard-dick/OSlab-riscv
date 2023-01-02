#include <os/string.h>
#include <os/fs.h>
#include <os/mm.h>
#include <pgtable.h>
#include <assert.h>
#include <common.h>


static superblock_t superblock;
static inode_t current_inode;
static fdesc_t fdesc_array[NUM_FDESCS];

static uint8_t inode_map[INODE_MAP_NUM << 9];
static uint8_t block_map[BLOCK_MAP_NUM << 9];

static uint8_t sct_buffer[SECTOR_SIZE];
static uint8_t blk_buffer[BLOCK_SIZE];

int do_mkfs(void)
{
    // TODO [P6-task1]: Implement do_mkfs
    // * 首先处理superblock, 先读入
    read_superblock();
    printk("> [FS] Checking existence of FileSystem!\n");
    int fs_exist = (superblock.magic == SUPERBLOCK_MAGIC);
    if(fs_exist)
    {// 存在, 则输出存在, 打印信息::
        printk("> [FS] FileSystem detected!\n");
    }else{
        printk("> [FS] Start initialize filesystem!\n");
        printk("> [FS] Setting up superblock...\n");
        superblock.magic = SUPERBLOCK_MAGIC;
        superblock.size = FS_SIZE;
        superblock.start = FS_START;
        superblock.block_map_offset = BLOCK_MAP_OFFSET;
        superblock.block_map_num = BLOCK_MAP_NUM;
        superblock.inode_map_offset = INODE_MAP_OFFSET;
        superblock.inode_map_num = INODE_MAP_NUM;
        superblock.inode_block_offset = INODE_BLOCK_OFFSET;
        superblock.inode_block_num = INODE_BLOCK_NUM;
        superblock.data_block_offset = DATA_NUM_OFFSET;
        superblock.data_block_num = DATA_BLOCK_NUM;
        write_superblock();
    }
    printk("> [FS] Checking superblock:\n");
    printk("     magic: 0x%x\n", superblock.magic);
    printk("     num sector: %d, start sector: %d\n", superblock.size, superblock.start);
    printk("     inode map offset: %d, inode map num: %d\n", superblock.inode_map_offset, superblock.inode_map_num);
    printk("     block map offset: %d, block map num: %d\n", superblock.block_map_offset, superblock.block_map_num);
    printk("     inode block offset: %d, inode block num: %d\n", superblock.inode_block_offset, superblock.inode_block_num);
    printk("     data block offset: %d, data block num: %d\n", superblock.data_block_offset, superblock.data_block_num);

    // ! superblock初始化完成, 现在一个文件系统的表面已经有了, 接下来分别初始化blockmap, inodemap和data
reset:
    if(fs_exist)
    {
        printk("the filesystem exists! \nDo you want to reset the inodes and blocks? [Y/N]:");
        int ch = -1;
        while (ch == -1) {
            ch = port_read_ch();
        }
        if(ch == 'y' || ch == 'Y'){
            printk("\n");
        }else if(ch == 'N' || ch == 'n'){
            printk("\n");
            return 0;
        }else{
            printk("\n");
            goto reset;
        }
    }
    
// ! init blockmap
    // ! 以sector为单位, 初始化inode map, block map, inode zone
    // 共计256个sector
    // 每次写进(FS_START + BLOCK_MAP_OFFSET)~(FS_START + BLOCK_MAP_OFFSET+BLOCK_MAP_NUM)
    memset(sct_buffer, 0, SECTOR_SIZE);

    printk("[FS] Setting up inode map...\n");
    for(int i = 0; i != INODE_MAP_NUM; ++i){
        sd_write(kva2pa(sct_buffer), 1, FS_START + INODE_MAP_OFFSET + i);
    }
    printk("[FS] Setting up block map...\n");
    for(int i = 0; i != BLOCK_MAP_NUM; ++i){
        sd_write(kva2pa(sct_buffer), 1, FS_START + BLOCK_MAP_OFFSET + i);
    }
    printk("[FS] Setting up inode block...\n");
    for(int i = 0; i != INODE_BLOCK_NUM; ++i){
        sd_write(kva2pa(sct_buffer), 1, FS_START + INODE_BLOCK_OFFSET + i);
    }

    // ! 初始化根目录的inode信息
    uint32_t root_inode_id, root_dir_block;
    root_inode_id = alloc_inode();
    root_dir_block = alloc_block();

    inode_t* root_inode = create_inode(root_inode_id, root_dir_block, I_DIR, O_RDWR);
    // ? 更新当前目录为根目录
    update_inode(root_inode);
    write_inode(root_inode_id >> 2);
    printk("     inode_num = %d\n", root_inode_id);

    // ! 初始化根目录inode中的表项, 写入data block中
    printk("[FS] Setting up data block(root directory)...\n");
    create_dir_block(root_dir_block, root_inode_id, root_inode_id);
    printk("     block_num = %d\n", root_dir_block);

    printk("[FS] Initialize filesystem finished.\n");

    return 0;
}

int do_statfs(void)
{
    // TODO [P6-task1]: Implement do_statfs
    read_superblock();
    if(superblock.magic == SUPERBLOCK_MAGIC){
        printk("> [FS] FileSystem detected!\n");
        printk("> [FS] Putting superblock:\n");
        printk("     magic: 0x%x\n", superblock.magic);
        printk("     num sector: %d, start sector: %d\n", superblock.size, superblock.start);
        printk("     inode map offset: %d, inode map num: %d\n", superblock.inode_map_offset, superblock.inode_map_num);
        printk("     block map offset: %d, block map num: %d\n", superblock.block_map_offset, superblock.block_map_num);
        printk("     inode block offset: %d, inode block num: %d\n", superblock.inode_block_offset, superblock.inode_block_num);
        printk("     data block offset: %d, data block num: %d\n", superblock.data_block_offset, superblock.data_block_num);
    }else{
        printk("> [FS] Warning: NO FileSystem detected! \n");
        return 0;
    }
    
    // 第二步是打印inode信息和更多信息, 这里把各个区占据情况说一下算了
    get_inode_map();
    uint32_t used_inodes = 0;
    for(uint32_t i = 0; i < SECTOR_SIZE/*(inode_map_num << 9)*/; ++i){
        if(inode_map[i]){
            ++used_inodes;
        }
    }
    printk("    used inodes num: %d\n", used_inodes);
    
    uint32_t block_map_num = (BLOCK_MAP_NUM << 9);
    get_block_map();
    uint32_t used_blocks = 0;
    for(int i = 0; i != block_map_num; ++i){// 每次读入一个sector
        if(block_map[i]){
            ++used_blocks;
        }
    }
    printk("    used blocks num: %d\n", used_blocks);
    return 0;  // do_statfs succeeds
}

int do_cd(char *path)
{
    // TODO [P6-task1]: Implement do_cd
    // ! 正常寻找
    char *subdir, *dir;
    int dir_id = current_inode.inode_id;
    subdir = path;
    do{
        dir = getDir(subdir);// ! 这里必然可以找到
        subdir = getSubdir(subdir);
        dir_id = search_dentry(dir_id, dir, D_DIR);
        if(dir_id == -1){
            printk("Erorr: can find '%s', no such directroy\n", subdir);
            return 0;
        }
    }while(subdir != 0 && subdir[0] != 0);

    inode_t* target_inode = get_inode(dir_id);
    update_inode(target_inode);

    return 0;  // do_cd succeeds
}

int do_mkdir(char *path)
{
    // TODO [P6-task1]: Implement do_mkdir
    // ! 第一步: 分离父目录和子目录
    char *subdir, *dir;
    int dir_id;
    dir_id = current_inode.inode_id;
	dir = getDir(path);
	if(dir == 0) return 0;
	else subdir = getSubdir(path);
	while(subdir != 0 && subdir[0] != 0)
	{
		// printk("%s::%s\n", dir, subdir);
        // ! 查找内容,
        dir_id = search_dentry(dir_id, dir, D_DIR);
        if(dir_id == -1){
            printk("Erorr: can not create '%s' under '%s', no such dir or file\n", dir, subdir);
            return 0;
        }
		dir = getDir(subdir);
		subdir = getSubdir(subdir);
	}

    // ? 出循环时, 必然是dir_id, 为最后一级id, dir中必然是需要创建的
    // 1 检查subdir是否合法
    // * 只能使用大小写字母, -_.和数字这些字符
    if(is_name_illegal(dir)){// ! 不合法则报错, 并返回-1
        printk("Error:: illegal characters in dir name!!\n");
        printk("Please using a~z, A~Z, 0~9, -_.\n");
        return -1;
    }

    uint32_t dentry_id = search_dentry(dir_id, dir, D_DIR);
    if(dentry_id != -1)
    {// 说明找到了
        printk("Error: dir name had existed!!\n");
        return -1;
    }

    // ! 能运行到这儿, 说明是正常运行了
    // 接下来是读出目录项, 然后找到一个空的目录项, 填入该目录, 然后初始化该目录的父目录和自己
    dentry_id = alloc_inode();
    dentry_id = create_dentry(dir_id, dentry_id, dir, D_DIR); // 返回目录的inode号

    // 初始化这个inode
    uint32_t block_id = alloc_block();
    create_inode(dentry_id, block_id, I_DIR, O_RDWR);
    write_inode(dentry_id >> 2);
    create_dir_block(block_id, dir_id, dentry_id);
    return 0;  // do_mkdir succeeds
}

int do_rmdir(char *path)
{
    // TODO [P6-task1]: Implement do_rmdir
    // ! 和mkdir截然相反的操作
    char *subdir, *dir;
    int dir_id;
    dir_id = current_inode.inode_id;
	dir = getDir(path);
	if(dir == 0) return 0;
	else subdir = getSubdir(path);
	while(subdir != 0 && subdir[0] != 0)
	{
		// printk("%s::%s\n", dir, subdir);
        dir_id = search_dentry(dir_id, dir, D_DIR);
        if(dir_id == -1){
            printk("Erorr: can not delete '%s' under '%s', no such dir or file\n", dir, subdir);
            return 0;
        }
		dir = getDir(subdir);
		subdir = getSubdir(subdir);
	}

    // ? 出循环时, 必然是dir_id, 为最后一级id, dir中必然是需要删除的
    // 1 不需要检查是否合法, 直接search就行

    uint32_t dentry_id = search_dentry(dir_id, dir, D_DIR);
    if(dentry_id == -1)
    {// 说明没找到
        printk("Error: cannot remove '%s': No such directory!!\n", dir);
        return -1;
    }

    // ! 能运行到这儿, 说明是找到要删的目录项了
    // 删除目录项
    if(!delete_dentry(dir_id, dentry_id, D_DIR))
    {
        // 删除失败, run!
        return -1;
    }
    // 得到inode信息
    inode_t* to_delete_inode = get_inode(dentry_id);
    // ? 删除其block块和map
    assert(to_delete_inode->type == I_DIR)
    release_block(to_delete_inode->direct_block[0]);

    // ? 删除inode和map
    memset((void*)to_delete_inode, 0, sizeof(inode_t));
    write_inode(dentry_id >> 2);
    free_inode_map(dentry_id);
    
    return 0;  // do_rmdir succeeds
}

int do_ls(char *path, int option)
{
    // TODO [P6-task1]: Implement do_ls
    // Note: argument 'option' serves for 'ls -l' in A-core
    /*
     * option = 0:: 单ls, 输出当前目录下内容即可
     * option = 1:: 指定了路径, 试图查找
     * option = 2:: ls -l, 输出当前目录下完整信息
     * option = 3:: ls -l [], 试图查找路径, 并输出完整信息
     */
    // 初始化all信号和target_inode_id
    int all = option & 0x2;
    int dir_id = current_inode.inode_id;
    
    if(option & 0x1){ // 开始修改目录
        // ? 同样的查找操作, 不过我们只需要找到最后一级的目录即可
        char *subdir, *dir;
        subdir = path;
        do{
            dir = getDir(subdir);// ! 这里必然可以找到
            subdir = getSubdir(subdir);
            dir_id = search_dentry(dir_id, dir, D_DIR);
            if(dir_id == -1){
                printk("Erorr: can find '%s' under '%s', no such directroy\n", dir, subdir);
                return 0;
            }
        }while(subdir != 0 && subdir[0] != 0);
        // ? 出循环时, 必然是dir_id, 为最后一级id, 即将要打印的
    }

    // ! 现在, dir_id和all信号都准备好了, 全部打印需要大量的读写, 可以对get_inode做优化, 记录一下快号, 可以命中一下...以后再说吧
    inode_t* tmp = get_inode(dir_id);
    read_block(tmp->direct_block[0]);
    printk("total: %d\n", tmp->file_num);
    dentry_t *cur_dentry = (dentry_t*)blk_buffer;
    uint32_t i, dentry_num;
    dentry_num = tmp->file_num;
    for(i = 0; i != 128 && dentry_num != 0; ++i){
        if(cur_dentry[i].type)
        {
            if(all){
                tmp = get_inode(cur_dentry[i].inode_id);
                if(tmp->type == I_DIR) printk("d");
                else printk("-");

                printk(" %d ", tmp->inode_id);

                if(tmp->mode & 0x1) printk("r");
                else printk("-");
                if(tmp->mode & 0x2) printk("w");
                else printk("-");

                printk(" %d ", tmp->links_count);

                printk(" %d ", tmp->size);
            }
            printk("%s\n", cur_dentry[i].name);
            dentry_num--;
        }
    }
    return 0;  // do_ls succeeds
}

int do_touch(char *path)
{
    // TODO [P6-task2]: Implement do_touch
    // ! 第一步: 分离父目录和子目录
    char *subdir, *dir;
    int dir_id;
    dir_id = current_inode.inode_id;
	dir = getDir(path);
	if(dir == 0) return 0;
	else subdir = getSubdir(path);
	while(subdir != 0 && subdir[0] != 0)
	{
        // ! 查找内容,
        dir_id = search_dentry(dir_id, dir, D_DIR);
        if(dir_id == -1){
            printk("Erorr: can not create '%s' under '%s', no such directory!\n", dir, subdir);
            return 0;
        }
		dir = getDir(subdir);
		subdir = getSubdir(subdir);
	}

    if(is_name_illegal(dir)){// ! 不合法则报错, 并返回-1
        printk("Error:: illegal characters in file name!!\n");
        printk("Please using a~z, A~Z, 0~9, -_.\n");
        return -1;
    }

    uint32_t dentry_id = search_dentry(dir_id, dir, D_FILE);
    if(dentry_id != -1){// 说明找到了
        printk("Error: filename had existed!!\n");
        return -1;
    }

    // ! 能运行到这儿, 说明是正常运行了
    // 接下来是读出目录项, 然后找到一个空的目录项, 填入该文件, 然后初始化该文件
    dentry_id = alloc_inode();
    dentry_id = create_dentry(dir_id, dentry_id, dir, D_FILE); // 返回文件的inode号

    // 初始化这个inode
    uint32_t block_id = alloc_block();
    create_inode(dentry_id, block_id, I_FILE, O_RDWR);
    write_inode(dentry_id >> 2);
    create_file_block(block_id);
    return dentry_id;  // do_touch succeeds
}

int do_cat(char *path)
{
    // TODO [P6-task2]: Implement do_cat
    // ! 第一步: 分离父目录和子路径, 找到目标文件
    char *subdir, *dir;
    int dir_id;
    dir_id = current_inode.inode_id;
	dir = getDir(path);
	if(dir == 0) return 0;
	else subdir = getSubdir(path);
	while(subdir != 0 && subdir[0] != 0)
	{
        // ! 查找内容,
        dir_id = search_dentry(dir_id, dir, D_DIR);
        if(dir_id == -1){
            printk("Erorr: can not create '%s' under '%s', no such directory!\n", dir, subdir);
            return 0;
        }
		dir = getDir(subdir);
		subdir = getSubdir(subdir);
	}

    uint32_t dentry_id = search_dentry(dir_id, dir, D_FILE);
    if(dentry_id == -1){// 说明没找到
        printk("Error: No such file!!\n");
        return -1;
    }
    inode_t* cat_inode = get_inode(dentry_id);
    uint32_t cat_size = cat_inode->size;
    // TODO: 暂时先不支持过大的输出, 会很难受的
    read_block(cat_inode->direct_block[0]);
    printk("%s\n", blk_buffer);

    return 0;  // do_cat succeeds
}

int do_fopen(char *path, int mode)
{
    // TODO [P6-task2]: Implement do_fopen
    // 根据当前目录来
    // 不会搜索路径, 只会查找一层
    int file_inode_id;
    if((file_inode_id = search_dentry(current_inode.inode_id, path, I_FILE)) == -1){
        // printk("Error: no such file named '%s'\n", path);
        // return -1;
        file_inode_id = do_touch(path);
    }
    // ? 到这里就找到了
    inode_t* file_inode = get_inode(file_inode_id);
    if(!(file_inode->mode & mode)){
        printk("Error: Permission denied\n");
        return -1;
    }
    // ? 而且权限对上了
    int i;
    for(i = 0; i != NUM_FDESCS; ++i){
        if(fdesc_array[i].status == F_AVAILABLE){
            fdesc_array[i].status = F_OCCUPIED;
            file_inode->uid = core_running[get_current_cpu_id()]->pid;
            write_inode(file_inode_id >> 2);
            break;
        }
    }
    assert(i != NUM_FDESCS);
    fdesc_array[i].inode_id = file_inode_id;
    fdesc_array[i].mode = mode;
    fdesc_array[i].rd_offset = 0;
    fdesc_array[i].wr_offset = 0;

    return i;  // return the id of file descriptor
}

int do_fread(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_fread
    fdesc_t* fp = fdesc_array + fd;
    // 检查权限
    if(fp->mode == O_WRONLY) return 0;
    if(fp->status == F_AVAILABLE) return 0;

    inode_t* rd_inode = get_inode(fp->inode_id);
    // 检查大小
    if(rd_inode->size < fp->rd_offset + length) return 0;

    // 开始读入
    uint32_t rd_size, block_id, block_offset, bytes;
    bytes = 0;
    while(length){// 最后会减到0
        if(byte_locate(&block_id, &block_offset, fp->rd_offset, rd_inode) == -1){// 定位不到, 寄了
            return 0;
        }
        rd_size = min(length, BLOCK_SIZE - block_offset);
        read_block(block_id);
        memcpy(buff + bytes, blk_buffer + block_offset, rd_size);

        length -= rd_size;
        bytes += rd_size;
        fp->rd_offset += rd_size;
    }

    rd_inode->size = max(rd_inode->size, fp->rd_offset);
    write_inode(rd_inode->inode_id >> 2);

    return bytes;  // return the length of trully read data
}

int do_fwrite(int fd, char *buff, int length)
{
    // TODO [P6-task2]: Implement do_fwrite
    fdesc_t* fp = fdesc_array + fd;
    // 检查权限
    if(fp->mode == O_RDONLY) return 0;
    if(fp->status == F_AVAILABLE) return 0;
    // 检查大小, 4GB
    if((1ul << 32) < fp->wr_offset + length) return 0;

    inode_t* wr_inode = get_inode(fp->inode_id);
    // 开始读入
    uint32_t wr_size, block_id, block_offset, bytes;
    bytes = 0;
    while(length){// 最后会减到0
        if(byte_locate(&block_id, &block_offset, fp->wr_offset, wr_inode) == -1){// 定位不到, 寄了
            return 0;
        }
        wr_size = min(length, BLOCK_SIZE - block_offset);
        read_block(block_id);
        memcpy(blk_buffer + block_offset, buff + bytes, wr_size);
        write_block(block_id);

        length -= wr_size;
        bytes += wr_size;
        fp->wr_offset += wr_size;
    }

    wr_inode->size = max(wr_inode->size, fp->wr_offset);
    write_inode(wr_inode->inode_id >> 2);
    return bytes;  // return the length of trully written data
}

int do_fclose(int fd)
{
    // TODO [P6-task2]: Implement do_fclose
    memset(fdesc_array + fd, 0, sizeof(fdesc_t));
    return 0;  // do_fclose succeeds
}

int do_ln(char *src_path, char *dst_path)
{
    // TODO [P6-task2]: Implement do_ln
    char *src_subdir, *src_dir, *dst_subdir, *dst_dir;
    int src_dir_id, dst_dir_id;
    // 分别开始查找
    src_dir_id = dst_dir_id = current_inode.inode_id;
	src_dir = getDir(src_path);
	if(src_dir == 0) return 0;
	else src_subdir = getSubdir(src_path);
	while(src_subdir != 0 && src_subdir[0] != 0)
	{
        src_dir_id = search_dentry(src_dir_id, src_dir, D_DIR);
        if(src_dir_id == -1){
            printk("Erorr: can not fine '%s' under '%s', no such dir or file\n", src_subdir, src_dir);
            return 0;
        }
		src_dir = getDir(src_subdir);
		src_subdir = getSubdir(src_subdir);
	}

    dst_dir = getDir(dst_path);
	if(dst_dir == 0) return 0;
	else dst_subdir = getSubdir(dst_path);
	while(dst_subdir != 0 && dst_subdir[0] != 0)
	{
        dst_dir_id = search_dentry(dst_dir_id, dst_dir, D_DIR);
        if(dst_dir_id == -1){
            printk("Erorr: can not fine '%s' under '%s', no such dir or file\n", dst_subdir, dst_dir);
            return 0;
        }
		dst_dir = getDir(dst_subdir);
		dst_subdir = getSubdir(dst_subdir);
	}
    uint32_t src_dentry_id = search_dentry(src_dir_id, src_dir, D_DIR | D_FILE);
    if(src_dentry_id == -1){// 说明没找到
        printk("Error: cannot link '%s': No such file or directory!!\n", src_dir);
        return -1;
    }
    uint32_t dst_dentry_id = search_dentry(dst_dir_id, dst_dir, D_LINK);
    if(dst_dentry_id != -1){// 说明找到了
        printk("Error: cannot link '%s': it has existed!!\n", dst_dir);
        return -1;
    }
    // ! 找到两个路径的最后一级目录
    // 将src_dentry建立一个硬链接, 放到dst_dentry_id下
    // 首先读src_dentry_id
    inode_t* src_inode = get_inode(src_dentry_id);
    src_inode->links_count++;
    write_inode(src_dentry_id >> 2);

    // 然后写入dst_dentry下, 需要修改dst_dir_inode
    dst_dentry_id = create_dentry(dst_dir_id, src_dentry_id, dst_dir, D_LINK);
    
    return 0;  // do_ln succeeds 
}

int do_rm(char *path)
{
    // TODO [P6-task2]: Implement do_rm
    // 和rmdir基本一致, 就是要释放所有文件块, 和cat中打印所有文件块有异曲同工之妙, 再说
    char *subdir, *dir;
    int dir_id;
    dir_id = current_inode.inode_id;
	dir = getDir(path);
	if(dir == 0) return 0;
	else subdir = getSubdir(path);
	while(subdir != 0 && subdir[0] != 0)
	{
		// printk("%s::%s\n", dir, subdir);
        dir_id = search_dentry(dir_id, dir, D_DIR);
        if(dir_id == -1){
            printk("Erorr: can not delete '%s' under '%s', no such dir or file\n", dir, subdir);
            return 0;
        }
		dir = getDir(subdir);
		subdir = getSubdir(subdir);
	}
    uint32_t dentry_id = search_dentry(dir_id, dir, D_FILE);
    if(dentry_id == -1)
    {// 说明没找到
        printk("Error: cannot remove '%s': No such file!!\n", dir);
        return -1;
    }

    // ! 能运行到这儿, 说明是找到要删的目录项了
    // 删除目录项, file貌似不会失败
    // assert(delete_dentry(dir_id, dentry_id, D_FILE));
    if(!delete_dentry(dir_id, dentry_id, D_FILE))
    {
        // 删除失败, run!
        return -1;
    }
 
    // 得到inode信息
    inode_t* to_delete_inode = get_inode(dentry_id);
    // ? 删除其block块和map
    assert(to_delete_inode->type == I_FILE)
    //TODO: 关于文件? 需要释放所有块
    release_block(to_delete_inode->direct_block[0]);

    // ? 删除inode和map
    memset((void*)to_delete_inode, 0, sizeof(inode_t));
    write_inode(dentry_id >> 2);
    free_inode_map(dentry_id);
    return 0;  // do_rm succeeds 
}

int do_lseek(int fd, int offset, int whence)
{
    // TODO [P6-task2]: Implement do_lseek
    // 主要是移动和确定为止位置, 由于读写文件中使用的byte_locate会自动分配块
    // 所以这里也是直接移动, 不会分配中间的块给文件, 等到使用时才会分配,
    // 至于cat, 只会打印第一块的数据, 也许可以做成每次y/n打印下一块, 但没必要.
    fdesc_t* fp = fdesc_array + fd;
    if(fp->status == F_AVAILABLE) return -1;

    uint32_t target_offset;
    inode_t* seek_inode = get_inode(fp->inode_id);
    if(fp->mode & 0x1){
        switch (whence){
            case SEEK_SET:
                target_offset = offset;
                break;
            case SEEK_CUR:
                target_offset = fp->rd_offset + offset;
                break;
            case SEEK_END:
                target_offset = seek_inode->size + offset;
                break;
            default:
                break;
        }
        if(target_offset > seek_inode->size){
            return -1;
        }
        fp->rd_offset = target_offset;
    }
    if(fp->mode & 0x2){
        switch (whence){
            case SEEK_SET:
                target_offset = offset;
                break;
            case SEEK_CUR:
                target_offset = fp->wr_offset + offset;
                break;
            case SEEK_END:
                target_offset = seek_inode->size + offset;
                break;
            default:
                break;
        }
        if(target_offset > seek_inode->size){
            uint32_t block_id;
            uint32_t block_offset;
            byte_locate(&block_id, &block_offset, target_offset, seek_inode);
            seek_inode->size = target_offset;
        }
        fp->wr_offset = target_offset;
    }
    write_inode(fp->inode_id >> 2);


    return 0;  // the resulting offset location from the beginning of the file
}


// ! 大量底层api
// 一组读写superblock
void inline read_superblock(){
    sd_read(kva2pa(&superblock), 1, SUPERBLOCK_START);
}
void inline write_superblock(){
    sd_write(kva2pa(&superblock), 1, SUPERBLOCK_START);
}

// 和inode相关
void inline get_inode_map(){
    sd_read(kva2pa(inode_map), 1, FS_START + INODE_MAP_OFFSET);
}
void inline sync_inode_map(){
    sd_write(kva2pa(inode_map), 1, FS_START + INODE_MAP_OFFSET);
}
uint32_t alloc_inode(){
    get_inode_map();
    for(uint32_t i = 0; i != SECTOR_SIZE; ++i){
        if(inode_map[i] == 0){
            inode_map[i] = 1;
            sync_inode_map();
            return i;
        }
    }
    printk("ERROR: no more available inodes\n");
    assert(0);    
}
void free_inode_map(uint32_t inode_id){
    get_inode_map();
    assert(inode_map[inode_id] == 1);
    inode_map[inode_id] = 0;
    sync_inode_map();
}
void inline read_inode(uint32_t inode_sct_id)
{
    sd_read(kva2pa(sct_buffer), 1, FS_START + INODE_BLOCK_OFFSET + inode_sct_id);
}
void inline write_inode(uint32_t inode_sct_id)
{
    sd_write(kva2pa(sct_buffer), 1, FS_START + INODE_BLOCK_OFFSET + inode_sct_id);
}
inode_t* get_inode(uint32_t search_inode_id)
{
    // if(search_inode_id == current_inode.inode_id)
    // {
    //     return &current_inode; // 笑死, 做了一点点优化, 结果每次取当前目录都不会读, 但最后都会写进去. 要么在update时写入, 要么就... 不如就update时写入把, 
    //     // 但to_node在buffer里....只能省略优化了.
    // }else{
    //     // 读出该inode_id对应的sector到buffer中, 然后记得写入
    //     read_inode(search_inode_id >> 2);
    // }
    read_inode(search_inode_id >> 2);
    inode_t* target_inode = (inode_t*)sct_buffer;
    return &target_inode[search_inode_id % 4];
}
inode_t* create_inode(uint32_t inode_id, uint32_t block_id, uint32_t type, uint32_t mode)
{// 输入一个inode, block和指定的类型权限, 在sec_buffer中生成一个指定的inode结构体
    inode_t* target_inode = get_inode(inode_id);

    // ! 随后可写入相关信息
    target_inode->mode = mode;        // 初始化读写权限
    target_inode->type = type;        // 初始化目录/文件
    target_inode->uid  = -1;          // 无人使用的id
    target_inode->inode_id = inode_id;// inode区索引
    target_inode->links_count = 1;    // 有一个自己的硬链接
    target_inode->blocks = 1;         // 无论是文件还是目录, 都暂时有一个block
    target_inode->direct_block[0] = block_id; // 建立自己的一个block, 4KB存128个32B的目录项

    if(type == I_DIR){
        // 若为目录, 则size = 4096默认, blocks等于1默认
        target_inode->size = 4096;
        target_inode->file_num = 2;
    }else{// 若为文件, size=0暂时, blocks=1暂时
        target_inode->size = 0;
        target_inode->file_num = 0;
    }
    write_inode(inode_id >> 2);
    return target_inode;
}
void inline update_inode(inode_t* to_inode)
{
    memcpy((uint8_t*)&current_inode, (uint8_t*)to_inode, sizeof(inode_t));
}

// 和block相关
void inline get_block_map(){
    sd_read(kva2pa(block_map), 8, FS_START + BLOCK_MAP_OFFSET);
}
void inline sync_block_map(){
    sd_write(kva2pa(block_map), 8, FS_START + BLOCK_MAP_OFFSET);
}
uint32_t alloc_block(){
    get_block_map();
    for(uint32_t i = 0; i != BLOCK_SIZE; ++i){
        if(block_map[i] == 0){
            block_map[i] = 1;
            sync_block_map();
            return i;
        }
    }
    printk("ERROR: no more available blocks\n");
    assert(0);    
}
void inline read_block(uint32_t block_id)
{
    sd_read(kva2pa(blk_buffer), 8, FS_START + DATA_NUM_OFFSET + (block_id << 3));
}
void inline write_block(uint32_t block_id)
{
    sd_write(kva2pa(blk_buffer), 8, FS_START + DATA_NUM_OFFSET + (block_id << 3));
}
void release_block(uint32_t block_id)
{
    get_block_map();
    assert(block_map[block_id] == 1);
    block_map[block_id] = 0;
    sync_block_map();
    // ? 与inode不同的是, 每次更新都会完整重写inode, 但是不一定会写block, 所以需要擦除
    memset(blk_buffer, 0, BLOCK_SIZE);
    write_block(block_id);
}
void create_dir_block(uint32_t dir_block_id, uint32_t parent_inode_id, uint32_t dir_inode_id)
{// 输入一个目录项块的id, 还有其父节点inode和自结点inode, 初始化最基本的两个表项, 并写入
    memset(blk_buffer, 0, BLOCK_SIZE);
    dentry_t *root_dentry = (dentry_t *)blk_buffer;

    memcpy(root_dentry[0].name, ".", 1);
    root_dentry[0].type = D_DIR;
    root_dentry[0].inode_id = dir_inode_id;

    memcpy(root_dentry[1].name, "..", 2);
    root_dentry[1].type = D_DIR;
    root_dentry[1].inode_id = parent_inode_id;

    write_block(dir_block_id);
}
void create_file_block(uint32_t dir_block_id)
{
    memset(blk_buffer, 0, BLOCK_SIZE);
    write_block(dir_block_id);
}




// 和目录相关
int search_dentry(uint32_t search_inode, char *name, uint32_t type)
{
    inode_t *cur_inode;
    int find  = 0;
    cur_inode = get_inode(search_inode);

    // 目录块, 只用第一个做目录项, 其余省略.
    read_block(cur_inode->direct_block[0]);
    dentry_t *cur_dentry = (dentry_t *)blk_buffer;
    for(uint32_t i = 0; i != 128; ++i)
    {
        if(cur_dentry[i].type) find++;
        if((cur_dentry[i].type & type) && !strcmp(cur_dentry[i].name, name)) 
            return cur_dentry[i].inode_id;
        if(find == cur_inode->file_num) break;
    }
    return -1;
}
char* getDir(char * path)
{
    // 将'/'变成'\0'后, 返回path头, 即可将首部提出
    int i = 0;
    for(; i != D_NAME && path[i] != 0; ++i)
    {
        if(path[i] == '/')
        {
            path[i++] = 0;
            return path;
        }
    }
    // ! 出循环就两个结果: 名字超长; 没有找到'/'
    if(i == D_NAME)
    {
        printk("Error: dir name is too long !!\n");
        return 0;
    }// 没有找到0, 其实就是没有最后一个'/', 直接到0的情况
    return path;
}
char* getSubdir(char * path)
{
    for(int i = 0; i != D_NAME; ++i)
    {
        if(path[i] == 0)
        {
            if(++i == D_NAME) return 0;
            else return path+i;
        }
    }
    // ! 出循环就两个结果: 名字超长; 没有找到'/'
    return 0;
}
uint32_t create_dentry(uint32_t dir_id, uint32_t subdir_id, char *name, uint32_t type)
{
    // 在dir_id下创建一个名为name, 类型为type的目录项
    inode_t *cur_inode;
    cur_inode = get_inode(dir_id);
    
    cur_inode->file_num++;
    // 如果是文件的话, 则硬链接数不增加
    if(type == D_DIR)
        cur_inode->links_count++;
    read_block(cur_inode->direct_block[0]);
    dentry_t* cur_dentry = (dentry_t*)blk_buffer;
    uint32_t i;
    for(i = 0; i != 128; ++i)
    {
        if(cur_dentry[i].type == 0)
        {// 有空子
            cur_dentry[i].inode_id = subdir_id;
            cur_dentry[i].type = type;
            memcpy(cur_dentry[i].name, name, strlen(name));
            break;
        }
    }
    assert(i != 128);
    // 更新dir_id对应的inode信息和目录项
    write_inode(dir_id >> 2);
    write_block(cur_inode->direct_block[0]);
    return subdir_id;
}
int delete_dentry(uint32_t dir_id, uint32_t subdir_id, /*char *name,*/ uint32_t type)
{// TODO: 在dir_id中删除subdir_id对应的目录项
    // 这项工作有相当多的操作需要完成
    /* 全流程
     * 检查子目录的目录项是否只有两个, 且硬链接数只有一个, 否则不进行删除
     * 只删掉dir_id中对应的目录项, 其filenum--, links_count--
     */

    // ? 检查子目录的目录项是否只有两个, 且硬链接数只有一个, 否则不进行删除
    int ret = 1;
    inode_t* subdir = get_inode(subdir_id);
    if(type == D_DIR){
        if(!(subdir->file_num == 2 && subdir->links_count == 1)){
            printk("Error: there are links or contents to be deleted first!\n");
            return 0;
        }
    }else{
        // ! 这里进入后都是file或者link了, 则link!=1则直接删掉目录项后返回0即可, 这样就只删除了目录项
        // 否则则返回1, 继续删inode内容
        if(subdir->links_count != 1){ // 说明将删除一个link
            ret = 0; // 最后返回则只删除
            // printk("Error: there are links or contents to be deleted first!\n");
            // return 0;
        }
    }
    

    // ! 清除对应的目录项
    inode_t *dir_inode;
    dir_inode = get_inode(dir_id);
    dir_inode->file_num--;
    dir_inode->links_count--;
    write_inode(dir_id >> 2);

    read_block(dir_inode->direct_block[0]);
    dentry_t* cur_dentry = (dentry_t*)blk_buffer;
    uint32_t i;
    for(i = 0; i != 128; ++i){
        if(cur_dentry[i].inode_id == subdir_id)
        {
            cur_dentry[i].type = 0;
            memset(cur_dentry[i].name, 0 , D_NAME);
            break;
        }
    }
    assert(i != 128);
    write_block(dir_inode->direct_block[0]);
    return ret;
}






// 杂项
int is_name_illegal(char *name)
{
    for(int i = 0; name[i] != 0; ++i)
    {
        if(name[i] == '-' || name[i] == '.' || name[i] == '_' ||
          (name[i] >= '0' && name[i] <= '9') ||
          (name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= 'A' && name[i] <= 'Z'))
        {
            ;
        }else{
            return 1;
        }
    }
    return 0;
}
int byte_locate(uint32_t *block_id, uint32_t *block_offset, uint32_t offset, inode_t *in_inode){
    uint32_t block_level; // ! 这里面由于block和page都是4KB, 所以会混用
    uint32_t page_id = offset >> 12; // 获得offset对应的page号, 根据page索引得到后面的东西
    if(page_id < DIRECT_SIZE){
        block_level = 0;
    }else if(page_id < DIRECT_SIZE + FIRST_LEVEL_SIZE){
        block_level = 1;
        page_id -= (DIRECT_SIZE);
    }else if(page_id < DIRECT_SIZE + FIRST_LEVEL_SIZE + SECOND_LEVEL_SIZE){
        block_level = 2;
        page_id -= (DIRECT_SIZE + FIRST_LEVEL_SIZE);
    }else if(page_id < DIRECT_SIZE + FIRST_LEVEL_SIZE + SECOND_LEVEL_SIZE + THIRD_LEVEL_SIZE){
        block_level = 3;
        page_id -= (DIRECT_SIZE + FIRST_LEVEL_SIZE + SECOND_LEVEL_SIZE);
    }else{
        return -1;// 没那么大
    }
    uint32_t direct_offset, first_offset, second_offset, third_offset;
    uint32_t direct_id, first_id, second_id, third_id;
    uint32_t *block_arr;

    switch(block_level){
        case 0:// 在直接索引块里
            direct_offset = page_id;
            direct_id = in_inode->direct_block[direct_offset];
            if(direct_id == 0){// 除了根目录的block块, 其它都不应该为0, 这里应该是写操作, 需要分配块
                direct_id = alloc_block();
                in_inode->direct_block[direct_offset] = direct_id;
                in_inode->blocks++;
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(direct_id);
            }
            break;
        
        case 1: // 在第一级索引块里
            first_offset = page_id >> 10; // 得到一级块的索引
            first_id = in_inode->first_block[first_offset]; // 得到一级块的id
            if(first_id == 0){ // 除了根目录的block块, 其它都不应该为0, 这里应该是写操作, 需要分配块
                first_id = alloc_block();
                in_inode->first_block[first_offset] = first_id;
                in_inode->blocks++;
                // write_inode(in_inode->inode_id >> 2);

                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(first_id);
            }

            read_block(first_id); // 读入第一块, 开始查找
            block_arr = (uint32_t*)blk_buffer;
            direct_offset = page_id % FIRST_BLOCK_SIZE; // 得到一级块中的偏移
            direct_id = block_arr[direct_offset]; // 读出第一级块中的id
            if(direct_id == 0){// 除了根目录的block块, 其它都不应该为0, 这里应该是写操作, 需要分配块
                direct_id = alloc_block();
                block_arr[direct_offset] = direct_id;
                in_inode->blocks++;
                write_block(first_id);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(direct_id);
            }
            break;

        case 2: // 第二级
            second_offset = page_id >> 20; // 得到二级块的索引
            second_id = in_inode->second_block[second_offset]; // 得到二级块的id
            if(second_id == 0){ // 除了根目录的block块, 其它都不应该为0, 这里应该是写操作, 需要分配块
                second_id = alloc_block();
                in_inode->second_block[second_offset] = second_id;
                in_inode->blocks++;
                // write_inode(in_inode->inode_id >> 2);

                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(second_id);
            }
            page_id = page_id % SECOND_BLOCK_SIZE; // 得到二级块内总偏移, 从而得到一级块索引和id

            read_block(second_id); // 读入二级块, 其内有一级块的id
            block_arr = (uint32_t*)blk_buffer;
            first_offset = page_id >> 10; // 得到二级块内一级块的索引
            first_id = block_arr[first_offset]; // 得到一级块的id
            if(first_id == 0){ // 除了根目录的block块, 其它都不应该为0, 这里应该是写操作, 需要分配块
                first_id = alloc_block();
                block_arr[first_offset] = first_id;
                in_inode->blocks++;
                write_block(second_id);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(first_id);
            }
            page_id = page_id % FIRST_BLOCK_SIZE; // 得到一级块内总偏移, 从而得到一级块索引和id

            read_block(first_id); // 读入一级块, 开始查找直接块的索引
            block_arr = (uint32_t*)blk_buffer;
            direct_offset = page_id; // 得到一级块中直接块的索引
            direct_id = block_arr[direct_offset]; // 得到索引块的id
            if(direct_id == 0){// 除了根目录的block块, 其它都不应该为0, 这里应该是写操作, 需要分配块
                direct_id = alloc_block();
                block_arr[direct_offset] = direct_id;
                in_inode->blocks++;
                write_block(first_id);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(direct_id);
            }
            break;

        case 3:
            third_offset = page_id >> 30; // 得到在三级指针中的索引..其实就一个..
            third_id = in_inode->second_block[third_offset]; // 得到3级块号
            if(third_id == 0){
                third_id = alloc_block();
                in_inode->third_block[third_offset] = third_id;
                in_inode->blocks++;
                // write_inode(in_inode->inode_id >> 2);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(third_id);
            }
            page_id = page_id % THIRD_BLOCK_SIZE;

            read_block(third_id); // 读出三级块, 找二级块的id
            block_arr = (uint32_t*)blk_buffer;
            second_offset = page_id >> 20; // 得到 1024*1024*1024内号在二级块中的偏移
            second_id = block_arr[second_offset]; // 得到在二级块(1024*1024)内的偏移
            if(second_id == 0){
                second_id = alloc_block();
                block_arr[second_offset] = second_id;
                in_inode->blocks++;
                write_block(third_id);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(second_id);
            }
            page_id = page_id % SECOND_BLOCK_SIZE;

            read_block(second_id);// 读出二级块
            block_arr = (uint32_t*)blk_buffer;
            first_offset = page_id >> 10;
            first_id = block_arr[first_offset];
            if(first_id == 0){
                first_id = alloc_block();
                block_arr[first_offset] = first_id;
                in_inode->blocks++;
                write_block(second_id);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(first_id);
            }
            page_id = page_id % FIRST_BLOCK_SIZE;

            read_block(first_id);// 读出一级块
            block_arr = (uint32_t*)blk_buffer;
            direct_offset = page_id;
            direct_id = block_arr[direct_offset];
            if(direct_id == 0){
                direct_id = alloc_block();
                block_arr[direct_offset] = direct_id;
                in_inode->blocks++;
                write_block(first_id);
                memset(blk_buffer, 0, BLOCK_SIZE);
                write_block(direct_id);
            }
            break;


        default:
            break;
    }

    *block_id = direct_id;
    *block_offset = offset % NORMAL_PAGE_SIZE;
    write_inode(in_inode->inode_id >> 2);
    return 0;    
}







