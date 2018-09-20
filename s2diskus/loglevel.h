/* loglevel.h - routines to modify kernel console loglevel
 *
 * Released under GPL v2.
 * (c) 2007 Tim Dijkstra
 */
#define TERMINAL "/dev/kmsg"  /* GP tty */

/* return ms */
inline unsigned long get_timer(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec/1000;
};

#define mprintf(format, args...) \
    {\
	unsigned long current = get_timer();\
        if(terminal_fd < 0) {\
                terminal_fd = open(TERMINAL, O_WRONLY);\
        }\
        if(terminal_fd >=0) {\
                sprintf(terminal_buf,"[%lu] %s: ", current, my_name);\
                sprintf(terminal_buf+33, format, ##args);\
                write(terminal_fd, terminal_buf, strlen(terminal_buf));\
        } else {\
                printf("cannot open terminal, terminal_fd %d, errno %s\n", terminal_fd, strerror(errno));\
        }\
        printf("%s", terminal_buf);\
    }

void open_printk(void);
int get_kernel_console_loglevel(void);
void set_kernel_console_loglevel(int level);
void close_printk(void);


