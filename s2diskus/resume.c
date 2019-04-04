/*
 * resume.c
 *
 * A simple user space resume handler for swsusp.
 *
 * Copyright (C) 2005 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * This file is released under the GPLv2.
 *
 */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <syscall.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/sysmacros.h>
#if (CONFIG_COMPRESS == COMPRESS_LZO)
#include <lzo/lzo1x.h>
#elif (CONFIG_COMPRESS == COMPRESS_IGZIP)
#include "igzip_lib.h"
#elif (CONFIG_COMPRESS == COMPRESS_LZ4)
#include "lz4.h"
#endif

#include "swsusp.h"
#include "memalloc.h"
#include "config_parser.h"
#include "md5.h"
#ifdef CONFIG_SPLASH
#include "splash.h"
#endif
#include "loglevel.h"
#include "earlyapp.h"

static char snapshot_dev_name[MAX_STR_LEN] = SNAPSHOT_DEVICE;
static char resume_dev_name[MAX_STR_LEN] = RESUME_DEVICE;
static loff_t resume_offset;
static int suspend_loglevel = SUSPEND_LOGLEVEL;
static int max_loglevel = MAX_LOGLEVEL;
#ifdef CONFIG_SPLASH
static char splash_param;
#endif
#ifdef CONFIG_FBSPLASH
char fbsplash_theme[MAX_STR_LEN] = "";
#endif
#ifdef CONFIG_COMPRESS
unsigned int compress_buf_size;
#endif
static int use_platform_suspend;
static char load_only;
static char early_app;
char *my_name;

int terminal_fd = -1;
char terminal_buf[1024];

static struct config_par parameters[] = {
	{
		.name = "snapshot device",
		.fmt = "%s",
		.ptr = snapshot_dev_name,
		.len = MAX_STR_LEN
	},
	{
		.name = "resume device",
		.fmt ="%s",
		.ptr = resume_dev_name,
		.len = MAX_STR_LEN
	},
	{
		.name = "resume offset",
		.fmt = "%llu",
		.ptr = &resume_offset,
	},
	{
		.name = "suspend loglevel",
		.fmt = "%d",
		.ptr = &suspend_loglevel,
	},
	{
		.name = "max loglevel",
		.fmt = "%d",
		.ptr = &max_loglevel,
	},
	{
		.name = "image size",
		.fmt = "%lu",
		.ptr = NULL,
	},
	{
		.name = "compute checksum",
		.fmt = "%c",
		.ptr = NULL,
	},
#ifdef CONFIG_COMPRESS
	{
		.name = "compress",
		.fmt = "%c",
		.ptr = NULL,
	},
#endif
#ifdef CONFIG_ENCRYPT
	{
		.name = "encrypt",
		.fmt = "%c",
		.ptr = NULL,
	},
#endif
#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
	{
		.name = "RSA key file",
		.fmt = "%s",
		.ptr = NULL,
	},
#endif
	{
		.name = "early writeout",
		.fmt = "%c",
		.ptr = NULL,
	},
#ifdef CONFIG_SPLASH
	{
		.name = "splash",
		.fmt = "%c",
		.ptr = &splash_param,
	},
#endif
	{
		.name = "shutdown method",
		.fmt = "%s",
		.ptr = NULL,
	},
#ifdef CONFIG_FBSPLASH
	{
		.name = "fbsplash theme",
		.fmt = "%s",
		.ptr = fbsplash_theme,
		.len = MAX_STR_LEN,
	},
#endif
	{
		.name = "resume pause",
		.fmt = "%d",
		.ptr = NULL,
	},
#ifdef CONFIG_THREADS
	{
		.name = "threads",
		.fmt = "%c",
		.ptr = NULL,
	},
#endif
	{
		.name = "debug test file",
		.fmt = "%s",
		.ptr = NULL,
	},
	{
		.name = "debug verify image",
		.fmt = "%c",
		.ptr = NULL,
	},
#ifdef CONFIG_THREADS
	{
		.name = "threads",
		.fmt = "%c",
		.ptr = NULL,
	},
#endif
	{
		.name = "load only",
		.fmt = "%c",
		.ptr = &load_only,
	},
        {
                .name = "early app",
                .fmt = "%c",
                .ptr = &early_app,
        },
	{
		.name = NULL,
		.fmt = NULL,
		.ptr = NULL,
		.len = 0,
	}
};

#undef mprintf
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
        fprintf(stderr, "%s", terminal_buf);\
    }

static inline int atomic_restore(int dev)
{
	mprintf("atomic restore\n");
	return ioctl(dev, SNAPSHOT_ATOMIC_RESTORE, 0);
}

static int open_resume_dev(char *resume_dev_name,
                           struct swsusp_header *swsusp_header)
{
	ssize_t size = sizeof(struct swsusp_header);
	off64_t shift = ((off64_t)resume_offset + 1) * page_size - size;
	ssize_t ret;
	int fd;

	fd = open(resume_dev_name, O_RDWR);
	if (fd < 0) {
		ret = -errno;
		fprintf(stderr, "%s: Could not open the resume device\n",
				my_name);
		return ret;
	}
	if (lseek64(fd, shift, SEEK_SET) != shift)
		return -EIO;
	ret = read(fd, swsusp_header, size);
	if (ret == size) {
		if (memcmp(SWSUSP_SIG, swsusp_header->sig, 10)) {
			close(fd);
			return -ENOMEDIUM;
		}
	} else {
		ret = ret < 0 ? ret : -EIO;
		return ret;
	}

	return fd;
}

static void pause_resume(int pause)
{
	struct termios newtrm, savedtrm;
#ifdef CONFIG_SPLASH
	char message[SPLASH_GENERIC_MESSAGE_SIZE];
	int wait_possible = !splash.prepare_abort(&savedtrm, &newtrm);
#else
	int wait_possible = 0;
#endif

	if (!wait_possible)
		pause = -1;

#ifdef CONFIG_SPLASH
	sprintf(message, "Image successfully loaded\nPress " ENTER_KEY_NAME
			" to continue\n");
	splash.set_caption(message);
	mprintf("%s: %s", my_name, message);
#else
	mprintf("%s: Image successfully loaded\n", my_name);
#endif

	if (pause > 0)
		mprintf("%s: Continuing automatically in %2d seconds",
			my_name, pause);

	while (pause) {
#ifdef CONFIG_SPLASH
		if (splash.key_pressed() == ENTER_KEY_CODE)
			break;
#endif
		sleep(1);
		if (pause > 0)
			mprintf("\b\b\b\b\b\b\b\b\b\b%2d seconds", --pause);
	}
	mprintf("\n");
#ifdef CONFIG_SPLASH
	if (wait_possible)
		splash.restore_abort(&savedtrm);
#endif
}

#ifdef CONFIG_SPLASH
static void reboot_question(char *message)
{
	char c;
	char full_message[SPLASH_GENERIC_MESSAGE_SIZE];
	char *warning =
		"\n\tYou can now boot the system and lose the saved state\n"
		"\tor reboot and try again.\n\n"
	        "\t[Notice that if you decide to reboot, you MUST NOT mount\n"
	        "\tany filesystems before a successful resume.\n"
	        "\tResuming after some filesystems have been mounted\n"
	        "\twill badly damage these filesystems.]\n\n"
		"\tDo you want to continue booting (Y/n)?";

	snprintf(full_message, SPLASH_GENERIC_MESSAGE_SIZE, "%s\n%s",
			message, warning);
	c = splash.dialog(full_message);
	if (c == 'n' || c == 'N') {
		reboot();
		fprintf(stderr, "%s: Reboot failed, please reboot manually.\n",
			my_name);
		while(1)
			sleep(10);
	}
}
#endif

static int read_image(int dev, int fd, loff_t start, char verify_only)
{
	struct image_header_info *header;
	int error;

	header = getmem(page_size);

	error = read_or_verify(dev, fd, header, start, verify_only, 0);
	if (error) {
#ifdef CONFIG_SPLASH
		reboot_question(
			"\nThe system snapshot image could not be read.\n\n"
			"\tThis might be a result of booting a wrong "
			"kernel.\n"
		);
#else
		mprintf("The system snapshot image could not be read.\n");
#endif
	} else {
		if (header->flags & PLATFORM_SUSPEND)
			use_platform_suspend = 1;
	}

	if (error) {
#ifdef CONFIG_SPLASH
		char message[SPLASH_GENERIC_MESSAGE_SIZE];

		sprintf(message, "%s: Error %d loading the image\nPress "
			ENTER_KEY_NAME " to continue\n", my_name, error);
		splash.dialog(message);
#endif
	} else if (header->resume_pause != 0) {
		pause_resume(header->resume_pause);
	} else {
		mprintf("Image successfully loaded\n");
	}

	freemem(header);

	return error;
}

static int reset_signature(int fd, struct swsusp_header *swsusp_header)
{
	ssize_t ret, size = sizeof(struct swsusp_header);
	off64_t shift = ((off64_t)resume_offset + 1) * page_size - size;
	int error = 0;

	/* Reset swap signature now */
	memcpy(swsusp_header->sig, swsusp_header->orig_sig, 10);

	if (lseek64(fd, shift, SEEK_SET) != shift) {
		fprintf(stderr, "%s: Could not lseek() to the swap header",
				my_name);
		return -EIO;
	}

	ret = write(fd, swsusp_header, size);
	if (ret == size) {
		fsync(fd);
	} else {
		fprintf(stderr, "%s: Could not restore the swap header",
				my_name);
		error = -EIO;
	}

	return error;
}

/* Parse the command line and/or configuration file */
static inline int get_config(int argc, char *argv[])
{
	static struct option options[] = {
		   {
		       "help\0\t\t\tthis text",
		       no_argument,		NULL, 'h'
		   },
		   {
		       "version\0\t\t\tversion information",
		       no_argument,		NULL, 'V'
		   },
		   {
		       "config\0\t\talternative configuration file.",
		       required_argument,	NULL, 'f'
		   },
		   {
		       "resume_device\0device that contains swap area",
		       required_argument,	NULL, 'r'
		   },
		   {
		       "resume_offset\0offset of swap file in resume device.",
		       required_argument,	NULL, 'o'
		   },
 		   {
 		       "parameter\0\toverride config file parameter.",
 		       required_argument,	NULL, 'P'
 		   },
		   { NULL,		0,			NULL,  0 }
	};
	int i, error;
	char *conf_name = CONFIG_FILE;
	const char *optstring = "hVf:o:r:P:";
	struct stat stat_buf;
	int fail_missing_config = 0;

	/* parse only config file argument */
	while ((i = getopt_long(argc, argv, optstring, options, NULL)) != -1) {
		switch (i) {
		case 'h':
			usage(my_name, options, optstring);
			exit(EXIT_SUCCESS);
		case 'V':
			version(my_name, NULL);
			exit(EXIT_SUCCESS);
		case 'f':
			conf_name = optarg;
			fail_missing_config = 1;
			break;
		}
	}

	if (stat(conf_name, &stat_buf)) {
		if (fail_missing_config) {
			fprintf(stderr, "%s: Could not stat configuration file\n",
				my_name);
			return -ENOENT;
		}
	}
	else {
		error = parse(my_name, conf_name, parameters);
		if (error) {
			fprintf(stderr, "%s: Could not parse config file\n", my_name);
			return error;
		}
	}

	optind = 0;
	while ((i = getopt_long(argc, argv, optstring, options, NULL)) != -1) {
		switch (i) {
		case 'f':
			/* already handled */
			break;
		case 'o':
			resume_offset = atoll(optarg);
			break;
		case 'r':
			strncpy(resume_dev_name, optarg, MAX_STR_LEN -1);
			break;
 		case 'P':
 			error = parse_line(optarg, parameters);
 			if (error) {
 				fprintf(stderr,
					"%s: Could not parse config string '%s'\n",
						my_name, optarg);
 				return error;
 			}
 			break;
		default:
			usage(my_name, options, optstring);
			return -EINVAL;
		}
	}

	if (optind < argc)
		strncpy(resume_dev_name, argv[optind], MAX_STR_LEN - 1);

	return 0;
}

static int read_config_data(int fd, loff_t offset, void *buf, int size)
{
	int res = 0;
	ssize_t cnt = 0;
    int *resume_off;

	if (lseek(fd, offset, SEEK_SET) == offset)
		cnt = read(fd, buf, size);
	if (cnt < (ssize_t)size)
		res = -EIO;

    resume_off = (int *)buf;
	return res;
}

#define SWAP_CONFIG_FILE  "/vendor/oem_config/swapinfo.txt"
#define CONFIG_OFFSET     0

static int read_config(void *buf, int size)
{
	int res;
	int cfg_fd;

	cfg_fd = open(SWAP_CONFIG_FILE, O_RDONLY);
	if (cfg_fd < 0) {
		mprintf("##2# open '%s' failed\n", SWAP_CONFIG_FILE);
		return -1;
	}

	res = read_config_data(cfg_fd, CONFIG_OFFSET, buf, size);
	if (res < 0) {
		mprintf("##2# read config data from '%s' failed\n", SWAP_CONFIG_FILE);
		size = 0;
	}

	close(cfg_fd);
	remove(SWAP_CONFIG_FILE);
	return size;
}

int main(int argc, char *argv[])
{
	unsigned int mem_size;
	struct stat stat_buf;
	int dev, resume_dev;
	int n, error, orig_loglevel;
	static struct swsusp_header swsusp_header;
	my_name = basename(argv[0]);

	suspend_loglevel = 7;
	set_kernel_console_loglevel(suspend_loglevel);
	for(n=0; n<argc; n++)
	{
		mprintf("### argv[%d] = %s\n", n, argv[n]);
	}

#ifdef CONFIG_SPLASH
	if (splash_param != 'y' && splash_param != 'Y')
		splash_param = 0;
	else
		splash_param = SPL_RESUME;
#endif
	if (load_only == 'y' || load_only == 'Y')
		load_only = 1;
	else
		load_only = 0;
        if (early_app == 'y' || early_app == 'Y')
                early_app = 1;
        else
                early_app = 0;

	get_page_and_buffer_sizes();

	mem_size = 2 * page_size + buffer_size;
        mprintf("Total buffer size %d bytes, total mem size %d bytes\n", buffer_size, mem_size);

#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
	mprintf("%s: libgcrypt version: %s\n", my_name,
		gcry_check_version(NULL));
	gcry_control(GCRYCTL_INIT_SECMEM, page_size, 0);
	mem_size += page_size;
#elif (CONFIG_ENCRYPT == ENCRYPT_ISAL)
	/* cannot init till get iv and key from header */
	mem_size += page_size;
	mprintf("Encrypt buffer size %d Bytes, total mem size %d bytes\n", (int)page_size, (int)mem_size);
#endif
#if (CONFIG_COMPRESS == COMPRESS_LZO)
	/*
	 * The formula below follows from the worst-case expansion calculation
	 * for LZO1 (size / 16 + 67) and the fact that the size of the
	 * compressed data must be stored in the buffer (sizeof(size_t)).
	 */
	compress_buf_size = buffer_size +
			round_up_page_size((buffer_size >> 4) + 67 +
						sizeof(size_t));
	mem_size += compress_buf_size +
			round_up_page_size(LZO1X_1_MEM_COMPRESS);
#elif (CONFIG_COMPRESS == COMPRESS_IGZIP)
	/* TODO: to fix the igzip worse case estimation */
	compress_buf_size = buffer_size +
		round_up_page_size((buffer_size >> 4) + 67 +
						sizeof(size_t));
	mem_size += compress_buf_size;// +
		//round_up_page_size(ISAL_DEF_LVL1_DEFAULT);
#elif (CONFIG_COMPRESS == COMPRESS_LZ4)
	compress_buf_size = round_up_page_size(LZ4_compressBound(buffer_size) + sizeof(size_t));
	mem_size += compress_buf_size;
#endif
        mprintf("Compressed buffer size %d Bytes, total mem size %d bytes\n", compress_buf_size, mem_size);

	error = init_memalloc(page_size, mem_size);
	if (error) {
		fprintf(stderr, "%s: Could not allocate memory\n", my_name);
		return error;
	}

	open_printk();
	orig_loglevel = get_kernel_console_loglevel();
	set_kernel_console_loglevel(suspend_loglevel);

	int ret;

	ret = read_config(&resume_offset, sizeof(resume_offset));
	if (ret != sizeof(resume_offset))
	{
		resume_offset = 0;
	}

	mprintf("####4 resume_offset = 0x%lX\n", resume_offset);
	if(resume_offset != 0)
	{
		resume_dev = makedev(259, 3);
		mprintf("####a3 resume_dev=0x%X\n", resume_dev);
	}
	else
	{
		resume_dev = makedev(259, 3);
		mprintf("####a5 resume_dev=0x%X\n", resume_dev);
	}

	strcpy(resume_dev_name, "/dev/dev_resume");

	/* the partition where image is stored, hard coded */
        if(stat(resume_dev_name, &stat_buf)) {
            ret = mknod(resume_dev_name, S_IFBLK | 0600, resume_dev);
            if(ret != 0) {
		mprintf("mknod failure %s errno: %d\n", resume_dev_name, errno);
		goto Free;
            }
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		error = errno;
		fprintf(stderr, "%s: Could not lock myself\n", my_name);
		goto Free;
	}

        if(stat(snapshot_dev_name, &stat_buf)) {
            /* hardcoded */
            ret = mknod(snapshot_dev_name, S_IFCHR | 0600, makedev(10, 231));
	    if (ret !=0 ) {
		mprintf("mknod failure %s errno: %d\n", snapshot_dev_name, errno);
            }
	}

	dev = open(snapshot_dev_name, O_WRONLY);
	if (dev < 0) {
		error = ENOENT;
		goto Free;
	}

	resume_dev = open_resume_dev(resume_dev_name, &swsusp_header);
	if (resume_dev == -ENOMEDIUM) {
		mprintf("Error No valid image found in resume device %s\n", resume_dev_name);
		error = 0;
		goto Close;
	} else if (resume_dev < 0) {
		error = -resume_dev;
		mprintf("Error open or read header from resume device %s\n", resume_dev_name);
		goto Close;
	}
#ifdef CONFIG_SPLASH
	splash_prepare(&splash, splash_param);
	splash.progress(5);
#endif

	/* Found valid iamge, lauch ioc_slcand and evs now */
	/* necessary dev for ioc_slcand. No error handling at all :) */
        if(stat("/dev/ttyS1", &stat_buf)) {
            ret = mknod("/dev/ttyS1", S_IFCHR | 0600, makedev(4, 65));
            if (ret != 0) {
                mprintf("mknod failure /dev/ttyS1 errno: %d\n", errno);
                goto Free;
            }
        }
        if(stat("/dev/ttyS2", &stat_buf)) {
            ret = mknod("/dev/ttyS2", S_IFCHR | 0600, makedev(4, 66));
            if (ret != 0) {
                mprintf("mknod failure /dev/ttyS2 errno: %d\n", errno);
                goto Free;
            }
        }

        pid_t child_pid = -1;
        if(early_app) {
            child_pid = earlyapp_start();
            if(child_pid == -1) {
                mprintf("failure start early app.\n");
            }
        }

	error = read_image(dev, resume_dev, swsusp_header.image, load_only);
	if (error) {
		error = -error;
		fprintf(stderr, "%s: Could not read the image\n", my_name);
	} else if (earlyapp_stop(child_pid)) {
	        /* Wait for child (evs) to exit */
		mprintf("wait early app exit failure\n");
	} else if (freeze(dev)) {
		error = errno;
#ifdef CONFIG_SPLASH
		reboot_question("Processes could not be frozen, "
				"cannot continue resuming.\n");
#else
		mprintf("Processes could not be frozen, cannot continue resuming.\n");
#endif
	}

	if (reset_signature(resume_dev, &swsusp_header))
		fprintf(stderr, "%s: Swap signature has not been restored.\n"
			"\tRun mkswap on the resume partition/file.\n",
			my_name);

	close(resume_dev);

	if (error)
		goto Close_splash;

	if (use_platform_suspend) {
		int err = platform_prepare(dev);

		if (err) {
			fprintf(stderr, "%s: Unable to use platform "
					"hibernation support, error code %d\n",
					my_name, err);
			use_platform_suspend = 0;
		}
	}
	atomic_restore(dev);
	/* We only get here if the atomic restore fails.  Clean up. */
	if (use_platform_suspend)
		platform_finish(dev);

	unfreeze(dev);

Close_splash:
#ifdef CONFIG_SPLASH
	splash.finish();
#endif
Close:
	/* unlink below dev, otherwise surfaceflinger will fail */
        if(early_app) {
            earlyapp_cleanup();
        }

	close(dev);
        if (terminal_fd >= 0)
                close(terminal_fd);

Free:
	if (error)
	    set_kernel_console_loglevel(max_loglevel);
	else if (orig_loglevel >= 0)
	    set_kernel_console_loglevel(orig_loglevel);

	close_printk();

	free_memalloc();

	return error;
}
