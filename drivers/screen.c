#include <screen.h>
#include <printk.h>
#include <os/string.h>
#include <os/sched.h>
#include <os/irq.h>
#include <os/smp.h>
#include <os/kernel.h>

#define SCREEN_WIDTH    80
#define SCREEN_HEIGHT   50
#define SCREEN_CMD_BEGIN 21
#define SCREEN_LOC(x, y) ((y) * SCREEN_WIDTH + (x))

/* screen buffer */
char new_screen[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};
char old_screen[SCREEN_HEIGHT * SCREEN_WIDTH] = {0};

/* cursor position */
static void vt100_move_cursor(int x, int y)
{
    // \033[y;xH
    printv("%c[%d;%dH", 27, y, x);
}

/* clear screen */
static void vt100_clear()
{
    // \033[2J
    printv("%c[2J", 27);
}

/* hidden cursor */
static void vt100_hidden_cursor()
{
    // \033[?25l
    printv("%c[?25l", 27);
}

/* write a char */
static void screen_write_ch(char ch)
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    if (ch == '\n')
    {
        current_running->cursor_x = 0;
        current_running->cursor_y++;
        if(current_running->cursor_y == SCREEN_HEIGHT)
        {
            for(int i = SCREEN_CMD_BEGIN; i < SCREEN_HEIGHT-1; ++i)
                strncpy(new_screen + SCREEN_LOC(0,i), new_screen + SCREEN_LOC(0,i+1), SCREEN_WIDTH);
            --(current_running->cursor_y);
            memset(new_screen + SCREEN_LOC(0,SCREEN_HEIGHT-1), ' ', SCREEN_WIDTH);
                
        }
    }
    else if(ch == '\b' || ch == 127){
        --(current_running->cursor_x);
        new_screen[SCREEN_LOC(current_running->cursor_x, current_running->cursor_y)] = ' ';
    }
    else{
        new_screen[SCREEN_LOC(current_running->cursor_x, current_running->cursor_y)] = ch;
        current_running->cursor_x++;
    }
}

void init_screen(void)
{
    vt100_hidden_cursor();
    vt100_clear();
    screen_clear();
}

void screen_clear(void)
{
    int i, j;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    for (i = 0; i < SCREEN_HEIGHT; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            new_screen[SCREEN_LOC(j, i)] = ' ';
        }
    }
    current_running->cursor_x = 0;
    current_running->cursor_y = 0;
    screen_reflush();
}

void screen_move_cursor(int x, int y)
{
    pcb_t *current_running = core_running[get_current_cpu_id()];
    current_running->cursor_x = x;
    current_running->cursor_y = y;
    vt100_move_cursor(x, y);
}

void screen_write(char *buff)
{
    int i = 0;
    int l = strlen(buff);

    for (i = 0; i < l; i++)
    {
        screen_write_ch(buff[i]);
    }
}

/*
 * This function is used to print the serial port when the clock
 * interrupt is triggered. However, we need to pay attention to
 * the fact that in order to speed up printing, we only refresh
 * the characters that have been modified since this time.
 */
void screen_reflush(void)
{
    int i, j;
    pcb_t *current_running = core_running[get_current_cpu_id()];
    /* here to reflush screen buffer to serial port */
    for (i = 0; i < SCREEN_HEIGHT; i++)
    {
        for (j = 0; j < SCREEN_WIDTH; j++)
        {
            /* We only print the data of the modified location. */
            if (new_screen[SCREEN_LOC(j, i)] != old_screen[SCREEN_LOC(j, i)])
            {
                vt100_move_cursor(j + 1, i + 1);
                bios_putchar(new_screen[SCREEN_LOC(j, i)]);
                old_screen[SCREEN_LOC(j, i)] = new_screen[SCREEN_LOC(j, i)];
            }
        }
    }

    /* recover cursor position */
    vt100_move_cursor(current_running->cursor_x, current_running->cursor_y);
}
