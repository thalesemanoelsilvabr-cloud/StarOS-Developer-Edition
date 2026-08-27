/* keyboard.c — PS/2 teclado IRQ1 */
#include <kernel/types.h>
#include <drivers/keyboard.h>

static inline u8 inb(u16 p){ u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }

static const char scanmap[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'-',0,0,0,'+',0,0,0,0,0,0,0,0,0,0,0
};
static const char scanmap_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','\"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};

#define KBD_BUF 64
static char kbd_buf[KBD_BUF];
static int  kbd_head=0, kbd_tail=0;
static int  shift=0, ctrl=0, alt=0;

void kbd_irq_handler(void){
    u8 sc = inb(0x60);
    u8 code = sc & 0x7F;
    u8 released = sc & 0x80;
    if(code==0x2A||code==0x36){ shift=!released; return; }
    if(code==0x1D){ ctrl=!released; return; }
    if(code==0x38){ alt=!released; return; }
    if(!released && code<128){
        char c = shift ? scanmap_shift[code] : scanmap[code];
        if(ctrl && c>='a'&&c<='z') c=(char)(c-'a'+1);
        if(c){
            int next=(kbd_head+1)%KBD_BUF;
            if(next!=kbd_tail){ kbd_buf[kbd_head]=c; kbd_head=next; }
        }
    }
}

void kbd_init(void){}

char kbd_getchar(void){
    while(kbd_head==kbd_tail) __asm__ volatile("hlt");
    char c=kbd_buf[kbd_tail];
    kbd_tail=(kbd_tail+1)%KBD_BUF;
    return c;
}

int kbd_haschar(void){ return kbd_head!=kbd_tail; }
void kbd_flush(void){ kbd_head=kbd_tail=0; }
