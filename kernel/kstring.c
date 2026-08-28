/* kstring.c — implementação das funções de string do kernel */
#include <kernel/types.h>

int kstrlen(const char* s){
    int n = 0;
    while(s[n]) n++;
    return n;
}

void kstrcpy(char* d, const char* s){
    while((*d++ = *s++));
}

void kstrncpy(char* d, const char* s, u32 n){
    u32 i = 0;
    while(i < n - 1 && s[i]){ d[i] = s[i]; i++; }
    d[i] = 0;
}

int kstrcmp(const char* a, const char* b){
    while(*a && *a == *b){ a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char* a, const char* b, u32 n){
    while(n && *a && *a == *b){ a++; b++; n--; }
    if(!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* kstrchr(const char* s, int c){
    while(*s){ if(*s == (char)c) return (char*)s; s++; }
    if(c == 0) return (char*)s;
    return (char*)0;
}

void kstrcat(char* dst, const char* src){
    int n = kstrlen(dst);
    kstrcpy(dst + n, src);
}

void kmemcpy(void* d, const void* s, u32 n){
    u8* dd = (u8*)d;
    const u8* ss = (const u8*)s;
    while(n--) *dd++ = *ss++;
}

void kmemset(void* d, u8 v, u32 n){
    u8* dd = (u8*)d;
    while(n--) *dd++ = v;
}

int kmemcmp(const void* a, const void* b, u32 n){
    const u8* aa = (const u8*)a;
    const u8* bb = (const u8*)b;
    while(n--){
        if(*aa != *bb) return *aa - *bb;
        aa++; bb++;
    }
    return 0;
}