#ifndef MACRO_H
#define MACRO_H
#define UNUSED(x)     ((void)(x))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ALIGN(v,a)    (((v)+(a)-1)&~((a)-1))
#define MIN(a,b)      ((a)<(b)?(a):(b))
#define MAX(a,b)      ((a)>(b)?(a):(b))
#define BIT(n)        (1u<<(n))
#define KB(n)         ((n)*1024u)
#define MB(n)         ((n)*1024u*1024u)
#define PACKED        __attribute__((packed))
#define NORETURN      __attribute__((noreturn))
#endif
