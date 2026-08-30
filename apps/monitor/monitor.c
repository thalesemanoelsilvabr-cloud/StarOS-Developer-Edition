/* monitor.c — monitor do sistema (GUI) */
#include <kernel/types.h>
#include <gui/window.h>
#include <gui/widget.h>
#include <gui/framebuffer.h>
extern void kprintf(const char*,...);
extern u32  kmem_used(void);
extern u32  volatile timer_ticks;

void monitor_run(void){
    window_t* win=window_create("Monitor do Sistema",20,40,360,280);
    window_set_bg(win,0x1A0A3A);
    wgt_label(win,10,10,"Memoria:");
    widget_t* mem_bar=wgt_progressbar(win,10,26,320,18);
    wgt_progressbar_set_color(mem_bar,0x7C3AED,0x2D1B69);
    wgt_label(win,10,55,"CPU:");
    widget_t* cpu_bar=wgt_progressbar(win,10,71,320,18);
    wgt_progressbar_set_color(cpu_bar,0x40E0A0,0x2D1B69);
    wgt_label(win,10,100,"Uptime:");
    widget_t* upt=wgt_label(win,80,100,"0s");
    wgt_label_set_color(upt,0x9D80F8);
    wgt_label(win,10,120,"Memoria usada:");
    widget_t* memlbl=wgt_label(win,130,120,"0 KB");
    wgt_label_set_color(memlbl,0xEDE9FE);
    window_render(win);
    for(;;){
        u32 mem=kmem_used()/1024;
        int mpct=(int)(mem*100/8192); /* 8MB heap */
        if(mpct>100) mpct=100;
        wgt_progressbar_set(mem_bar,mpct);
        /* CPU fake — baseado em variacao de ticks */
        static u32 last_t=0;
        u32 dt=timer_ticks-last_t; last_t=timer_ticks;
        int cpu=(int)(100-(int)(dt*10)); if(cpu<0)cpu=0; if(cpu>100)cpu=100;
        wgt_progressbar_set(cpu_bar,cpu);
        /* uptime */
        u32 s=timer_ticks/100;
        char tmp[32];
        char* p=tmp+30; *p=0;
        *--p='s'; u32 sv=s%60;
        if(sv==0){*--p='0';}else{while(sv){*--p='0'+sv%10;sv/=10;}}
        *--p=' '; u32 mv=s/60;
        if(mv){*--p='m'; while(mv){*--p='0'+mv%10;mv/=10;} *--p=' ';}
        wgt_label_set(upt,p);
        /* mem label */
        char mb[16]; char* mp=mb+14; *mp=0;
        *--mp='B'; *--mp='K'; *--mp=' ';
        u32 mv2=mem; if(mv2==0){*--mp='0';}else{while(mv2){*--mp='0'+mv2%10;mv2/=10;}}
        wgt_label_set(memlbl,mp);
        window_render(win);
        /* aguarda 1 s */
        u32 dl=timer_ticks+100;
        while(timer_ticks<dl) __asm__ volatile("hlt");
    }
}
