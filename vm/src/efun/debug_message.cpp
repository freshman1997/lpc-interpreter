#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdarg>


#include "efun/efun.h"

#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

size_t strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0 && --n != 0) {
        do {
            if ((*d++ = *s++) == 0)
                break;
        } while (--n != 0);
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';              /* NUL-terminate dst */
        while (*s++);
    }

    return(s - src - 1);    /* count does not include NUL */
}

//-------------------------------------------------------------------------
//
//-------------------------------------------------------------------------

int _Touch(const char *PathFile)
{
    struct stat st;
    char TempDir[strlen(PathFile) + 1];
    const char *Start,  *Index;
    int fd;
    if (stat(PathFile, &st) !=  - 1)
    {
            return 1;
    }

    Start = PathFile;
    Index = NULL;
    while ( (Index = strchr(Start, '/') ) != NULL)
    {
        strlcpy(TempDir, PathFile, Index - PathFile + 1);
        Start = Index + 1;
        mkdir(TempDir, 0755);
    }

    fd = open(PathFile, O_WRONLY | O_CREAT, 0644);
    if (fd !=  - 1)
    {
        close(fd);
        return 1;
    }
    else
    {
        return 0;
    }
}

char *format_time(time_t tm)
{
    static char strtime[30];
    strftime(strtime, 30, "%Y-%m-%d %H:%M:%S", localtime(&tm) ); //strftime会添加\0
    return strtime;
}

//-------------------------------------------------------------------------
//
//-------------------------------------------------------------------------
#define DEBUG_LOG_FILE     "debug.log"
#define V_START(vlist, last_arg) va_start(vlist, last_arg)
#define V_VAR(type, var, vlist)
#define V_DCL(x)

void debug_message(const char *fmt, ...)
{
    static int append = 0;
    static char deb_buf[100];
    static char *deb = deb_buf;
    time_t tm;
    va_list args;
    FILE *fp = NULL;
    V_DCL(char *fmt);

    if (!append) {
        /*
        * check whether config file specified this option
        */
        if (strlen(DEBUG_LOG_FILE) ) {
                snprintf(deb, sizeof(deb_buf), "%s/%s", "log", DEBUG_LOG_FILE);
        } else {
                snprintf(deb, sizeof(deb_buf), "%s/debug.log", "log");
        }
        while (*deb == '/') {
                deb++;
        }
    }
    _Touch(deb);
    fp = fopen(deb, append ? "a" : "w");

    /*
    * re-use stdout's file descriptor if system or process runs out
    *
    * OS/2 doesn't have ENFILE.
    */
    if (!fp && (errno == EMFILE
#ifdef ENFILE
            || errno == ENFILE
#endif
    ) ) {
        fp = freopen(deb, append ? "a" : "w", stdout);
        append = 2;
    } else {
        append = 1;
    }

    if (!fp) {
        /* darn.  We're in trouble */
        perror(deb);
        abort();
    }
    time(&tm);

    // 写入文件
    fprintf(fp, "%s\t", format_time(tm) );
    V_START(args, fmt);
    V_VAR(char *, fmt, args);
    vfprintf(fp, fmt, args);
    fflush(fp);
    va_end(args);

    // 写到控制台
    V_START(args, fmt);
    V_VAR(char *, fmt, args);
    vfprintf(stderr, fmt, args);
    fflush(stderr);
    va_end(args);

    /*
    * don't close stdout
    */
    if (append != 2) {
        (void)fclose(fp);
    }

    /*
    * append to debug.log next time thru
    */
    if (!append) {
        append = 1;
    }
}
#else 

#define V_DCL(x)
#define V_START(vlist, last_arg) va_start(vlist, last_arg)
#define V_VAR(type, var, vlist)

// win32 实现
void debug_message(const char *fmt, ...)
{
    time_t tm;
    va_list args;
    FILE *fp = NULL;
    V_DCL(char *fmt);
    // 写到控制台
    V_START(args, fmt);
    V_VAR(char *, fmt, args);
    vfprintf(stderr, fmt, args);
    fflush(stderr);
    va_end(args);
}

#endif