/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>

#define SHELL_BEGIN 20
#define SHELL_BOTTOM 50
#define BUFFER_LEN 80
#define BASH_NUM 18

typedef enum {
    ENTER,
    DELETE,
    LEGAL,
    IILLEGAL,
} char_mode_t;

// int pos_y = SHELL_BEGIN;// 维护一个全局的终端打印y

pid_t shell_exec(char *name, int argc, char *argv[]);
int shell_waitpid(char *pid);
int shell_kill(char *pid);
void shell_exit();
void shell_taskset(char *arg1, int , char *[]);
void shell_clear(void);
void shell_ps(void);
void shell_mkfs();
void shell_statfs();
void shell_cd(char *);
void shell_mkdir(char *);
void shell_rmdir(char *);
void shell_ls(int argc, char *argv[]);
void shell_touch(char *name);
void shell_cat(char *name);
void shell_rmfile(char *name);
void shell_ln(char *src, char *dst);

struct{
    char name[9];
    int para_num; 
} bash_table[] = 
{
    {"exec"   ,  3},
    {"wait"   ,  1},
    {"kill"   ,  1},
    {"exit"   ,  0},
    {"clear"  ,  0},
    {"ps"     ,  0},
    {"taskset",  3},
    {"mkfs"   ,  0},
    {"statfs" ,  0},
    {"ls"     ,  0},
    {"mkdir"  ,  1},
    {"rmdir"  ,  1},
    {"cd"     ,  1},
    {"touch"  ,  1},
    {"cat"    ,  1},
    {"rm"     ,  1},
    {"ln"     ,  2},
};

// int shell_x, shell_y;

// declare function
char_mode_t char_parse(char);
void shell_parse(char*);
int get_cmd_id(char*);

int main(void)
{
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    printf("> root@UCAS_OS: "); // 16 chars
    char buffer[BUFFER_LEN] = {0};
    int index = 0;
    char c;
    // for(int i = 0; i < BASH_NUM; ++i){
    //     printf("%d with %s\n", i, bash_table[i].name);
    // }
    while (1)
    {
        // TODO [P3-task1]: call syscall to read UART port
        c = getchar();
        // TODO [P3-task1]: parse input
        switch(char_parse(c))
        {
        case ENTER:
            printf("\n");
            buffer[index] = '\0';
            shell_parse(buffer); // shell_x由此处负责，而shell_y由具体shell指令复杂。
            bzero(buffer, index);
            index = 0;      
            break;
        // note: backspace maybe 8('\b') or 127(delete)
        case DELETE:
            if(index != 0)
            {
                //assert(index >0);
                buffer[--index] = 0;
                // putchar(8);// delete
                // putchar(' ');
                // putchar(8);
                printf("\b");

            }
            break;
        
        case LEGAL:
            buffer[index++] = c;
            //putchar(c);
            //sys_move_cursor(shell_x, shell_y);
            printf("%c",c);
            break;
        
        case IILLEGAL:
            break;
        
        default:
            break;
        }
        
    }

    return 0;
}

char_mode_t char_parse(char c)
{// 先考虑简单一点的，什么tab啊，不考虑，剩下全部非法
    if(c == '\r' || c == '\n'){
        return ENTER;
    }else if(c == 8 || c == 127){
        return DELETE;
    }
    else if(c > 31 && c < 127){
        return LEGAL;
    }else{
        return IILLEGAL;
    }
}

void shell_parse(char* cmd_name)
{
    // printf("\nnow parsing %s\n", cmd_name);
    char arg[6][16] = {0};
    int cmd_id;
    int i = 0;
    char *parse = cmd_name;
    int j = 0, k = 0;
    char *argv[6] = {arg[0], arg[1], 
                    arg[2], arg[3], arg[4], arg[5]};
    // 分解参数
    while(parse[j] != '\0'){
        // 提取出参数 为cmd param1 param2
        // 最后i是多少，参数就有多少
        if(parse[j] != ' ')
            arg[i][k++] = parse[j];
        else{
            arg[i][k] = '\0';
            ++i;
            k = 0;
        }
        ++j;
    }
    arg[i][k] = '\0';
    // 寻找对应的命令
    //printf("\nnow parsing %s\n", arg[0]);
    cmd_id = get_cmd_id(arg[0]);

    if(cmd_id < 0){
        printf("ERROR: unknown command %s\n", arg[0]);
        printf("> root@UCAS_OS: ");
    }else if(bash_table[cmd_id].para_num != i && cmd_id && cmd_id != 6 && cmd_id != 9){
        printf("ERROR: unmatched parameters %s\n", arg[0]);
        printf("> root@UCAS_OS: ");
    }else{
        // TODO [P3-task1]: ps, exec, kill, clear      
        // TODO [P6-task1]: mkfs, statfs, cd, mkdir, rmdir, ls

        // TODO [P6-task2]: touch, cat, ln, ls -l, rm  
        
        switch(cmd_id){ // TODO!! 暂时没考虑更多&的情况
            case 0:
                shell_exec(argv[1], i, &argv[1]);
                break;
            case 1:
                shell_waitpid(argv[1]);
                break;
            case 2:
                shell_kill(argv[1]);
                break;
            case 3:
                shell_exit();
                break;
            case 4:
                shell_clear();
                break;
            case 5:
                shell_ps();
                break;
            case 6:
                shell_taskset(argv[1], i, &argv[1]);
                break;
            case 7:
                shell_mkfs();
                break;
            case 8:
                shell_statfs();
                break;
            case 9:
                shell_ls(i, &argv[1]);
                break;
            case 10:
                shell_mkdir(argv[1]);
                break;
            case 11:
                shell_rmdir(argv[1]);
                break;
            case 12:
                shell_cd(argv[1]);
                break;
            case 13:
                shell_touch(argv[1]);
                break;
            case 14:
                shell_cat(argv[1]);
                break;
            case 15:
                shell_rm(argv[1]);
                break;
            case 16:
                shell_ln(argv[1], argv[2]);
                break;

            default:
                printf("sorry, no support for %s inst now\n", argv[0]);
                break;
        }

        printf("> root@UCAS_OS: ");
    }
}

int get_cmd_id(char *sh_name){
    int i;
    for(i = 0; i < BASH_NUM; ++i){
        //printf("%s:%d with %s\n",sh_name, i, bash_table[i].name);
        if(strcmp(sh_name, bash_table[i].name) == 0){       
            return i;
        }
    }
    return -1;
}


pid_t shell_exec(char *name, int argc, char *argv[]){
    
    pid_t pid;
    int wait = 0;
    if(argc > 1 && strcmp(argv[argc-1], "&") == 0 )
        wait = 1;

    if( wait )
    {
        pid = sys_exec(name, argc-1, argv);
    }else{
        pid = sys_exec(name, argc, argv);
        sys_waitpid(pid);
    }
    
    printf("exec process[%d]\n", pid);
    return pid;

    return 0;
}

int shell_waitpid(char *pid){
    pid_t wait = (pid_t)atoi(pid);
    int waiting;
    waiting = sys_waitpid(wait);
    if(waiting){
        printf("waiting for process[%d]\n", wait);
    }
    else{
        printf("ERROR: process[%d] not found.\n", wait);
    }
}

int shell_kill(char *pid){
    pid_t kill = atoi(pid);
    int killed;
    killed = sys_kill(kill);
    if(killed){
        printf("kill process[%d]\n", kill);
    }
    else{
        printf("ERROR: process[%d] not found.\n", kill);
    }
    return killed;
}

void shell_exit(){
    sys_exit();
}

void shell_taskset(char *arg1, int argc, char *argv[]){
    pid_t pid;
    int mask;
    if(strcmp(arg1, "-p") == 0){
        //* 设置pid的
        if(argc != 3){
            printf("wrong param numbers!!\n");
            return ;
        }
        mask = atoi(argv[1]);
        pid = atoi(argv[2]);
        sys_set_pid(pid, mask);
        printf("taskset %s %s %s\n", argv[0], argv[1], argv[2]);
    }else{
        mask = atoi(argv[0]);
        sys_taskset(argv[1], argc-1, &argv[1], mask);
        printf("taskset %s %s\n", argv[0], argv[1]);
    }
}

void shell_clear(void){
    sys_screen_clear();
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
}

void shell_ps(void){
    sys_ps();
}

void shell_mkfs(){
    sys_mkfs();
}

void shell_statfs(){
    sys_statfs();
}

void shell_ls(int argc, char *argv[]){
    // int option;
    switch(argc){
    case 0: // 只有ls一个
        sys_ls(argv[0], 0);
        break;
    case 1: // 可能是-l, 可能是路径
        if(!(strcmp(argv[0], "-l"))){
            sys_ls(argv[0], 2);
        }else if(argv[0][0] != '-'){
            sys_ls(argv[0], 1);
        }else{
            printf("Error: argument fault\n");
        }
        break;
    case 2:
        if(!(strcmp(argv[0], "-l")) && argv[1][0] != '-'){
            sys_ls(argv[1], 3);
        }else{
            printf("Error: argument fault\n");
        }
        break;

    default:
        printf("Error: argument fault\n");
        break;
    }   
}

void shell_mkdir(char *path){
    sys_mkdir(path);
}

void shell_rmdir(char *path){
    sys_rmdir(path);
}

void shell_cd(char *path){
    // 在这里做探查, 那里就简单一点
    if(path != 0 && path[0] != 0)
        sys_cd(path);
    else{
        printf("Error: please input path\n");
    }
}

void shell_touch(char *path){
    sys_touch(path);
}

void shell_cat(char *path){
    sys_cat(path);
}

void shell_rm(char *path){
    sys_rm(path);
}

void shell_ln(char *src, char *dst){
    sys_ln(src, dst);
}