/* terminal.c — VGA texto 80x25 */
#include <kernel/types.h>
#include <drivers/terminal.h>

#define VGA_W   80
#define VGA_H   25
#define VGA_MEM ((u16*)0xB8000)

static u32 col=0, row=0;
static u8  cur_color = 0x0F; /* branco em preto */

static void vga_putentry(char c, u8 color, u32 x, u32 y){
    VGA_MEM[y*VGA_W+x] = (u16)c | ((u16)color<<8);
}
static void scroll(void){
    for(u32 r=0;r<VGA_H-1;r++)
        for(u32 c2=0;c2<VGA_W;c2++)
            VGA_MEM[r*VGA_W+c2]=VGA_MEM[(r+1)*VGA_W+c2];
    for(u32 c2=0;c2<VGA_W;c2++)
        VGA_MEM[(VGA_H-1)*VGA_W+c2]=(u16)' '|((u16)cur_color<<8);
    row=VGA_H-1;
}
void term_init(void){
    cur_color=0x0F; col=0; row=0;
    for(u32 r=0;r<VGA_H;r++)
        for(u32 c2=0;c2<VGA_W;c2++)
            vga_putentry(' ',cur_color,c2,r);
}
void term_write_char(char c){
    if(c=='\n'){ col=0; if(++row>=VGA_H) scroll(); return; }
    if(c=='\r'){ col=0; return; }
    if(c=='\b'){ if(col>0){ col--; vga_putentry(' ',cur_color,col,row); } return; }
    if(col>=VGA_W){ col=0; if(++row>=VGA_H) scroll(); }
    vga_putentry(c,cur_color,col++,row);
}
void term_write(const char* s){ while(*s) term_write_char(*s++); }
void term_clear(void){
    col=0; row=0;
    for(u32 r=0;r<VGA_H;r++)
        for(u32 c2=0;c2<VGA_W;c2++)
            vga_putentry(' ',cur_color,c2,r);
}
void term_set_color(u8 fg, u8 bg){ cur_color=(u8)((bg<<4)|fg); }
void term_move_cursor(u32 r, u32 c2){ row=r; col=c2; }
