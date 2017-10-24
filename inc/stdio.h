#ifndef JOS_INC_STDIO_H
#define JOS_INC_STDIO_H

#include <inc/stdarg.h>

#ifndef NULL
#define NULL    ((void *) 0)
#endif /* !NULL */

/* lib/stdio.c */
void cputchar(int c);
int getchar(void);
int iscons(int fd);

/* lib/printfmt.c */
void printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...);
void vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt,
        va_list);
int snprintf(char *str, int size, const char *fmt, ...);
int vsnprintf(char *str, int size, const char *fmt, va_list);

/* lib/printf.c */
int cprintf(const char *fmt, ...);
int vcprintf(const char *fmt, va_list);

/* lib/fprintf.c */
int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
int vfprintf(int fd, const char *fmt, va_list);

/* lib/readline.c */
char *readline(const char *prompt);

static char loglevel = 1;

/* Debug print */
#define KNRM  "\x1B[0m"
#define KBLK  "\x1B[30m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"
#define KBSD  "\x1B[44m"

#define TRAPPRINT 0
#ifndef DEBUGPRINT
#define DEBUGPRINT 1
#endif

#define dprintf(fmt, ...) \
        {if (DEBUGPRINT && loglevel >= 1) cprintf("%s[%s%d%s|%s%s%s]%s " fmt, KBLU, KRED, cpunum(), KBLU, KYEL, __func__, KBLU, KGRN, ##__VA_ARGS__);}
#define ddprintf(fmt, ...) \
        {if (DEBUGPRINT && loglevel >= 2) cprintf("%s[%s%d%s|%s%s%s]%s " fmt, KBLU, KRED, cpunum(), KBLU, KYEL, __func__, KBLU, KGRN, ##__VA_ARGS__);}
#define dddprintf(fmt, ...) \
        {if (DEBUGPRINT && loglevel >= 3) cprintf("%s[%s%d%s|%s%s%s]%s " fmt, KBLU, KRED, cpunum(), KBLU, KYEL, __func__, KBLU, KGRN, ##__VA_ARGS__);}
#define eprintf(fmt, ...) \
        cprintf("%s[%s%d%s|%s%s%s]%s " KRED fmt, KBLU, KRED, cpunum(), KBLU, KYEL, __func__, KBLU, KGRN, ##__VA_ARGS__)

#endif /* !JOS_INC_STDIO_H */
