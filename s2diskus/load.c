/*
 * load.c
 *
 * Image loading for s2disk/s2both and resume.
 *
 * Copyright (C) 2008 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * This file is released under the GPLv2.
 */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <syscall.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if (CONFIG_COMPRESS == COMPRESS_LZO)
#include <lzo/lzo1x.h>
#elif (CONFIG_COMPRESS == COMPRESS_IGZIP)
#include "igzip_lib.h"
#elif (CONFIG_COMPRESS == COMPRESS_LZ4)
#include "lz4.h"
#endif

#include "swsusp.h"
#include "memalloc.h"
#include "md5.h"
#ifdef CONFIG_SPLASH
#include "splash.h"
#endif
#include "loglevel.h"
extern char *my_name;
extern int terminal_fd;
extern char terminal_buf[1024];

#ifdef CONFIG_VERITY
static char verify_checksum;
#else
#define verify_checksum 0
#endif
#ifdef CONFIG_COMPRESS
unsigned int compress_buf_size;
static char do_decompress;
#if (CONFIG_COMPRESS == COMPRESS_IGZIP)
struct inflate_state stream;
#endif
#else
#define do_decompress 0
#endif
#ifdef CONFIG_ENCRYPT
static char do_decrypt;
#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
static char password[PASSBUF_SIZE];
#elif (CONFIG_ENCRYPT == ENCRYPT_ISAL)
static struct key_data key_data;
#endif /* ENCRYPT_ISAL */
#else
#define do_decrypt 0
#endif

/**
 *	read_page - Read data from a swap location
 *	@fd:		File handle of the resume partition.
 *	@buf:		Pointer to the area we're reading into.
 *	@offset:	Swap offset of the place to read from.
 */
static int read_page(int fd, void *buf, loff_t offset)
{
	int res = 0;
	ssize_t cnt = 0;

	if (!offset)
		return 0;

	if (lseek(fd, offset, SEEK_SET) == offset)
		cnt = read(fd, buf, page_size);
	if (cnt < (ssize_t)page_size)
		res = -EIO;

	return res;
}

/*
 * The swap_reader structure is used for handling swap in a file-alike way.
 *
 * @extents:	Array of extents used for trackig swap allocations.  It is
 *		page_size bytes large and holds at most
 *		(page_size / sizeof(struct extent) - 1) extents.  The last slot
 *		must be all zeros and is the end marker.
 *
 * @cur_extent:		The extent currently used as the source of swap pages.
 *
 * @cur_offset:		The offset of the swap page that will be used next.
 *
 * @total_size:		The amount of data to read.
 *
 * @buffer:		Buffer used for storing image data pages.
 *
 * @read_buffer:	If compression is used, the compressed contents of
 *			@buffer are stored here.  Otherwise, it is equal to
 *			@buffer.
 *
 * @fd:			File handle associated with the swap.
 *
 * @ctx:		Used for checksum computing, if so configured.
 *
 * @lzo_work_buffer:	Work buffer used for decompression.
 *
 * @decrypt_buffer:	Buffer for storing encrypted pages (page_size bytes).
 */
struct swap_reader {
	struct extent *extents;
	struct extent *cur_extent;
	loff_t cur_offset;
	loff_t next_extents;
	loff_t total_size;
	void *buffer;
	void *read_buffer;
	int fd;
	struct md5_ctx ctx;
	void *lzo_work_buffer;
	char *decrypt_buffer;
};

/**
 *	load_extents_page - load the array of extents
 *	handle:	Structure holding the pointer to the array of extents etc.
 *
 *	Read the table of extents from the swap location pointed to by
 *	@handle->next_extents and store it at the address in @handle->extents.
 *	Read the swap location of the next array of extents from the last
 *	element of the array and fill this element with zeros.  Initialize
 *	@handle->cur_extent and @handle->cur_offset as appropriate.
 */
static int load_extents_page(struct swap_reader *handle)
{
	int error, n;

	error = read_page(handle->fd, handle->extents, handle->next_extents);
	if (error)
		return error;
	n = page_size / sizeof(struct extent) - 1;
	handle->next_extents = handle->extents[n].start;
	memset(handle->extents + n, 0, sizeof(struct extent));
	handle->cur_extent = handle->extents;
	handle->cur_offset = handle->cur_extent->start;
	if (posix_fadvise(handle->fd, handle->cur_offset,
			handle->cur_extent->end - handle->cur_offset,
			POSIX_FADV_NOREUSE))
		perror("posix_fadvise");
	return 0;
}

/**
 *	free_swap_reader - free memory allocated for loading the image
 *	@handle:	Structure containing pointers to memory buffers to free.
 */
static void free_swap_reader(struct swap_reader *handle)
{
	if (do_decompress) {
		freemem(handle->lzo_work_buffer);
		freemem(handle->read_buffer);
	}
	if (do_decrypt)
		 freemem(handle->decrypt_buffer);
	freemem(handle->buffer);
	freemem(handle->extents);
}

/**
 *	init_swap_reader - initialize the structure used for loading the image
 *	@handle:	Structure to initialize.
 *	@fd:		File descriptor associated with the swap.
 *	@start:		Swap location (offset) of the first image page.
 *	@image_size:	Total size of the image data.
 *
 *	Initialize buffers and related fields of @handle and load the first
 *	array of extents.
 */
static int init_swap_reader(struct swap_reader *handle, int fd, loff_t start,
                            loff_t image_size)
{
	int error;

	if (!start)
		return -EINVAL;

	handle->fd = fd;
	handle->total_size = image_size;

	handle->extents = getmem(page_size);

	handle->buffer = getmem(buffer_size);
	handle->read_buffer = handle->buffer;

	if (do_decrypt)
		handle->decrypt_buffer = getmem(page_size);

	if (do_decompress) {
		handle->read_buffer = getmem(compress_buf_size);
#if (CONFIG_COMPRESS == COMPRESS_LZO)
		handle->lzo_work_buffer = getmem(LZO1X_1_MEM_COMPRESS);
#elif (CONFIG_COMPRESS == COMPRESS_IGZIP)
		//handle->lzo_work_buffer = getmem(ISAL_DEF_LVL1_DEFAULT);
#endif
	}

	/* Read the table of extents */
	handle->next_extents = start;
	error = load_extents_page(handle);
	if (error) {
		free_swap_reader(handle);
		return error;
	}

	if (verify_checksum)
		md5_init_ctx(&handle->ctx);

	return 0;
}

/**
 *	find_next_image_page - find the next swap location holding image data
 */
static void find_next_image_page(struct swap_reader *handle)
{
	int error;

	handle->cur_offset += page_size;
	if (handle->cur_offset < handle->cur_extent->end)
		return;
	/* We have exhausted the current extent.  Forward to the next one */
	handle->cur_extent++;
	if (handle->cur_extent->start < handle->cur_extent->end) {
		handle->cur_offset = handle->cur_extent->start;
		if (posix_fadvise(handle->fd, handle->cur_offset,
				handle->cur_extent->end - handle->cur_offset,
				POSIX_FADV_NOREUSE))
			perror("posix_fadvise");
		return;
	}
	/* No more extents.  Load the next extents page. */
	error = load_extents_page(handle);
	if (error)
		handle->cur_offset = 0;
}

/**
 *	load_and_decrypt_page - load a page of data from swap and decrypt it,
 *			if necessary.
 */
static int load_and_decrypt_page(struct swap_reader *handle, void *dst)
{
	//static int page_count = 0;
	int error;
	void *buf = dst;
	char ch;

	if (!handle->cur_offset)
		return -EINVAL;

	if (do_decrypt)
		buf = handle->decrypt_buffer;

	error = read_page(handle->fd, buf, handle->cur_offset);
	if (!error && do_decrypt) {
#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
		error = gcry_cipher_decrypt(cipher_handle, dst, page_size,
							buf, page_size);
#elif (CONFIG_ENCRYPT == ENCRYPT_ISAL)
		aes_gcm_dec_128_update(&key_data.gkey, &key_data.gctx, dst, buf, page_size);
/*
		if(page_count == 0) {
			unsigned char *src_p = (unsigned char*)buf;
			unsigned char *dst_p = (unsigned char*)dst;
			mprintf("original  block first 16 bytes: %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x\n",
				 src_p[0], src_p[1], src_p[2], src_p[3], src_p[4], src_p[5], src_p[6], src_p[7], 
				 src_p[8], src_p[9], src_p[10], src_p[11], src_p[12], src_p[13], src_p[14], src_p[15]);
			mprintf("encrypted block first 16 bytes: %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x\n",
				 dst_p[0], dst_p[1], dst_p[2], dst_p[3], dst_p[4], dst_p[5], dst_p[6], dst_p[7], 
				 dst_p[8], dst_p[9], dst_p[10], dst_p[11], dst_p[12], dst_p[13], dst_p[14], dst_p[15]);
			page_count ++;
		}
*/
#endif
	}
	if (!error) {
		handle->total_size -= page_size;
		find_next_image_page(handle);
	}
	return error;
}

/**
 *	load_buffer - load (and decrypt, if necessary) a block od data from the
 *			swap, decompress it if necessary and store it in the
 *			image data buffer.
 */
static ssize_t load_buffer(struct swap_reader *handle)
{
	void *dst;
	ssize_t size;
	int error;

#ifdef CONFIG_COMPRESS
	if (do_decompress) {
		struct buf_block *block = handle->read_buffer;

		/* Read the block size from the first block page. */
		error = load_and_decrypt_page(handle, block);
		if (error)
			return 0;
		size = page_size;
		dst = block;
		dst = (void*)((unsigned long)dst + page_size);
		/* Load the rest of the block pages */
		while ((unsigned long)size < block->size + sizeof(size_t)) {
			error = load_and_decrypt_page(handle, dst);
			if (error)
				return 0;
			size += page_size;
			dst = (void*)((unsigned long)dst + page_size);
		}
		/* Decompress block */
#if (CONFIG_COMPRESS == COMPRESS_LZO)
                {
			lzo_uint cnt;
			error = lzo1x_decompress((lzo_bytep)block->data, block->size,
						handle->buffer, &cnt,
						handle->lzo_work_buffer);
			if (error)
				return 0;
			size = cnt;
			//mprintf("lzo1x_decompress src %p (%d bytes) -> dst %p (%d bytes)\n", block->data, (int)block->size, handle->buffer, (int)size);
		}
#elif (CONFIG_COMPRESS == COMPRESS_IGZIP)
		stream.avail_in = block->size;
		stream.next_in = (uint8_t*)block->data;
		stream.avail_out = buffer_size;//compress_buf_size;
		stream.next_out = (uint8_t*)handle->buffer;
		error = isal_inflate_stateless(&stream);
		size = (unsigned long)stream.next_out - (unsigned long)handle->buffer;
		//mprintf("isal_inflate_stateless src %p (%d bytes) -> dst %p (%d bytes)\n", block->data, (int)block->size, handle->buffer, (int)size);
		if (error != COMP_OK) {
			mprintf("error isal_inflate_stateless ret %d\n", error);
			return 0;
		}
#elif (CONFIG_COMPRESS == COMPRESS_LZ4)
		error = LZ4_decompress_safe(block->data, handle->buffer, block->size, buffer_size);
		//mprintf("LZ4_decompress_safe src %p (%d bytes) -> dst %p (%d bytes)\n", block->data, (int)block->size, handle->buffer, (int)error);
		if(error <= 0) {
			mprintf("error LZ4_decompress_fast ret %d\n", error);
			return 0;
		}
		size = error;
#endif
		goto Checksum;
	}
#endif
	dst = handle->buffer;
	size = 0;
	while (size < buffer_size && handle->total_size > 0) {
		error = load_and_decrypt_page(handle, dst);
		if (error)
			return 0;
		size += page_size;
		dst = (void*)((unsigned long)dst + page_size);
	}

 Checksum:
	if (verify_checksum)
		md5_process_block(handle->buffer, size, &handle->ctx);

	return size;
}

/**
 *	load_image - load a hibernation image
 *	@handle:	Structure containing image information.
 *	@dev:		Special device file to write image data pages to.
 *	@nr_pages:	Number of image data pages.
 */
static int load_image(struct swap_reader *handle, int dev,
                      unsigned int nr_pages, int verify_only)
{
	unsigned int m, n;
	ssize_t buf_size;
	ssize_t ret = 0;
	void *buf = 0;
	int error = 0;
#ifdef CONFIG_SPLASH
	char message[SPLASH_GENERIC_MESSAGE_SIZE];

	sprintf(message, "Loading image data pages (%u pages)...", nr_pages);
	splash.set_caption(message);
#else
	mprintf("Loading image data pages (%u pages) %s\n", nr_pages, verify_only?"(verify only)":"");
#endif

	m = nr_pages / 100;
	if (!m)
		m = 1;
	n = 0;
	buf_size = 0;
	do {
		if (buf_size <= 0) {
			buf_size = load_buffer(handle);
			if (buf_size <= 0) {
				//mprintf("\n");
				return -EIO;
			}
			buf = handle->buffer;
		}
		ret = verify_only ? page_size : write(dev, buf, page_size);

		if (ret < page_size) {
			if (ret < 0)
				perror("\nError while writing an image page");
			else {
				//mprintf("\n");
                        }
			return -EIO;
		}
		buf = (void*)((unsigned long)buf + page_size);
		buf_size -= page_size;
		if (!(n % m)) {
#ifdef CONFIG_SPLASH
			mprintf("\b\b\b\b%3d%%", n / m);
			if (n / m > 15)
				splash.progress(n / m);
#endif
		}
		n++;
	} while (n < nr_pages);
	if (!error) {
		//printf(" done\n");
	}
	return error;
}

static char *print_checksum(char * buf, unsigned char *checksum)
{
	int j;

	for (j = 0; j < 16; j++)
		buf += sprintf(buf, "%02hhx ", checksum[j]);

	return buf;
}

#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
static int decrypt_key(struct image_header_info *header, unsigned char *key,
			unsigned char *ivec)
{
	gcry_ac_handle_t rsa_hd;
	gcry_ac_data_t rsa_data_set, key_set;
	gcry_ac_key_t rsa_priv;
	gcry_mpi_t mpi;
	unsigned char *buf, *out, *key_buf, *ivec_buf;
	struct md5_ctx ctx;
	struct RSA_data *rsa;
	gcry_cipher_hd_t sym_hd;
	int j, ret;

	rsa = &header->rsa;

	ret = gcry_ac_open(&rsa_hd, GCRY_AC_RSA, 0);
	if (ret)
		return ret;

	ret = gcry_cipher_open(&sym_hd, PK_CIPHER, GCRY_CIPHER_MODE_CFB,
					GCRY_CIPHER_SECURE);
	if (ret)
		goto Free_rsa;

	key_buf = getmem(PK_KEY_SIZE);
	ivec_buf = getmem(PK_CIPHER_BLOCK);
	out = getmem(KEY_TEST_SIZE);
	do {
#ifdef CONFIG_SPLASH
		splash.read_password(password, 0);
#endif
		memset(ivec_buf, 0, PK_CIPHER_BLOCK);
		strncpy(ivec_buf, password, PK_CIPHER_BLOCK);
		md5_init_ctx(&ctx);
		md5_process_bytes(password, strlen(password), &ctx);
		md5_finish_ctx(&ctx, key_buf);
		ret = gcry_cipher_setkey(sym_hd, key_buf, PK_KEY_SIZE);
		if (!ret)
			ret = gcry_cipher_setiv(sym_hd, ivec_buf,
						PK_CIPHER_BLOCK);

		if (!ret)
			ret = gcry_cipher_encrypt(sym_hd,
						out, KEY_TEST_SIZE,
						KEY_TEST_DATA, KEY_TEST_SIZE);

		if (ret) {
			fprintf(stderr, "%s: libgcrypt error: %s\n", my_name,
					gcry_strerror(ret));
			break;
		}

		ret = memcmp(out, rsa->key_test, KEY_TEST_SIZE);

		if (ret)
			printf("%s: Wrong passphrase, try again.\n", my_name);
	} while (ret);

	gcry_cipher_close(sym_hd);
	if (!ret)
		ret = gcry_cipher_open(&sym_hd, PK_CIPHER,
				GCRY_CIPHER_MODE_CFB, GCRY_CIPHER_SECURE);
	if (ret)
		goto Free_buffers;

	ret = gcry_cipher_setkey(sym_hd, key_buf, PK_KEY_SIZE);
	if (!ret)
		ret = gcry_cipher_setiv(sym_hd, ivec_buf, PK_CIPHER_BLOCK);
	if (!ret)
		ret = gcry_ac_data_new(&rsa_data_set);
	if (ret)
		goto Close_cypher;

	buf = rsa->data;
	for (j = 0; j < RSA_FIELDS; j++) {
		size_t s = rsa->size[j];

		/* We need to decrypt some components */
		if (j >= RSA_FIELDS_PUB) {
			/* We use the in-place decryption */
			ret = gcry_cipher_decrypt(sym_hd, buf, s, NULL, 0);
			if (ret)
				break;
		}

		gcry_mpi_scan(&mpi, GCRYMPI_FMT_USG, buf, s, NULL);
		ret = gcry_ac_data_set(rsa_data_set, GCRY_AC_FLAG_COPY,
					rsa->field[j], mpi);
		gcry_mpi_release(mpi);
		if (ret)
			break;

		buf += s;
	}
	if (!ret)
		ret = gcry_ac_key_init(&rsa_priv, rsa_hd,
					GCRY_AC_KEY_SECRET, rsa_data_set);
	if (ret)
		goto Destroy_data_set;

	ret = gcry_ac_data_new(&key_set);
	if (ret)
		goto Destroy_key;

	gcry_mpi_scan(&mpi, GCRYMPI_FMT_USG, header->key.data,
			header->key.size, NULL);
	ret = gcry_ac_data_set(key_set, GCRY_AC_FLAG_COPY, "a", mpi);
	if (ret)
		goto Destroy_key_set;

	gcry_mpi_release(mpi);
	ret = gcry_ac_data_decrypt(rsa_hd, 0, rsa_priv, &mpi, key_set);
	if (!ret) {
		unsigned char *res;
		size_t s;

		gcry_mpi_aprint(GCRYMPI_FMT_USG, &res, &s, mpi);
		if (s == KEY_SIZE + CIPHER_BLOCK) {
			memcpy(key, res, KEY_SIZE);
			memcpy(ivec, res + KEY_SIZE, CIPHER_BLOCK);
		} else {
			ret = -ENODATA;
		}
		gcry_free(res);
	}

Destroy_key_set:
	gcry_mpi_release(mpi);
	gcry_ac_data_destroy(key_set);

Destroy_key:
	gcry_ac_key_destroy(rsa_priv);

Destroy_data_set:
	gcry_ac_data_destroy(rsa_data_set);

Close_cypher:
	gcry_cipher_close(sym_hd);

Free_buffers:
	freemem(out);
	freemem(ivec_buf);
	freemem(key_buf);

Free_rsa:
	gcry_ac_close(rsa_hd);

	return ret;
}

static int restore_key(struct image_header_info *header)
{
	static unsigned char key[KEY_SIZE], ivec[CIPHER_BLOCK];
	int error;

	if (header->flags & IMAGE_USE_RSA) {
		error = decrypt_key(header, key, ivec);
	} else {
		int j;
#ifdef CONFIG_SPLASH
		splash.read_password(password, 0);
#endif
		encrypt_init(key, ivec, password);
		for (j = 0; j < CIPHER_BLOCK; j++)
			ivec[j] ^= header->salt[j];
	}
	if (!error)
		error = gcry_cipher_open(&cipher_handle,
					IMAGE_CIPHER, GCRY_CIPHER_MODE_CFB,
					GCRY_CIPHER_SECURE);
	if (!error) {
		error = gcry_cipher_setkey(cipher_handle, key, KEY_SIZE);
		if (!error)
			error = gcry_cipher_setiv(cipher_handle, ivec,
							CIPHER_BLOCK);
		if (error)
			gcry_cipher_close(cipher_handle);
	}
	return error;
}
/* end of ENCRYPT_GCRYPT */
#elif (CONFIG_ENCRYPT == ENCRYPT_ISAL)
static int restore_key(struct image_header_info *header)
{
	memset(&key_data.gctx, 0, sizeof(struct gcm_context_data));
	memcpy(key_data.key, header->key, KEY_SIZE);
	memcpy(key_data.ivec, header->iv, IV_SIZE);
	aes_gcm_pre_128(key_data.key, &key_data.gkey);
	aes_gcm_init_128(&key_data.gkey, &key_data.gctx, key_data.ivec, NULL, 0);
	mprintf("Random key %d bytes restored : %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x\n", KEY_SIZE, 
		key_data.key[0], key_data.key[1], key_data.key[2],  key_data.key[3],  key_data.key[4],  key_data.key[5],  key_data.key[6],  key_data.key[7], 
		key_data.key[8], key_data.key[9], key_data.key[10], key_data.key[11], key_data.key[12], key_data.key[13], key_data.key[14], key_data.key[15]);
	mprintf("IV %d bytes restored : %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x %2x\n", IV_SIZE, 
		key_data.ivec[0], key_data.ivec[1], key_data.ivec[2],  key_data.ivec[3],  key_data.ivec[4],  key_data.ivec[5],  key_data.ivec[6],  key_data.ivec[7], 
		key_data.ivec[8], key_data.ivec[9], key_data.ivec[10], key_data.ivec[11]);
	return 0;
}

#endif /* CONFIG_ENCRYPT */

int read_or_verify(int dev, int fd, struct image_header_info *header,
                   loff_t start, int verify, int test)
{
	static struct swap_reader handle;
	static unsigned char orig_checksum[16], checksum[16];
	static char csum_buf[48];
	int error = 0, test_mode = (verify || test);

	error = read_page(fd, header, start);
	if (error)
		return error;
#ifdef CONFIG_VERITY
	if ((header->flags & IMAGE_CHECKSUM) || verify) {
		memcpy(orig_checksum, header->checksum, 16);
		print_checksum(csum_buf, orig_checksum);
		printf("%s: MD5 checksum %s\n", my_name, csum_buf);
		verify_checksum = 1;
	}
#endif
#ifdef CONFIG_SPLASH
	splash.progress(10);
#endif
	if (header->flags & IMAGE_COMPRESSED) {
#if (CONFIG_COMPRESS == COMPRESS_LZO)
		if (lzo_init() == LZO_E_OK) {
			do_decompress = 1;
		} else {
			fprintf(stderr, "%s: Failed to initialize LZO\n",
					my_name);
			error = -EFAULT;
		}
#elif (CONFIG_COMPRESS == COMPRESS_IGZIP) || (CONFIG_COMPRESS == COMPRESS_LZ4)
		do_decompress = 1;
#else
		fprintf(stderr, "%s: Compression not supported\n", my_name);
		error = -EINVAL;
#endif
	}
	if (error)
		return error;

	if (header->flags & IMAGE_ENCRYPTED) {
		mprintf("%s: Encrypted image\n", my_name);
#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
		error = test_mode ?
			gcry_cipher_setiv(cipher_handle, key_data.ivec,
						CIPHER_BLOCK) :
			restore_key(header);
		if (error) {
			fprintf(stderr, "%s: libgcrypt error: %s\n", my_name,
					gcry_strerror(error));
		} else {
			do_decrypt = 1;
			splash.progress(15);
		}
#elif (CONFIG_ENCRYPT == ENCRYPT_ISAL)
		do_decrypt = 1;
		restore_key(header);
#else
		fprintf(stderr, "%s: Encryption not supported\n", my_name);
		error = -EINVAL;
#endif
	}
	if (error)
		goto Exit_encrypt;

	error = init_swap_reader(&handle, fd, header->map_start,
					header->image_data_size);
	if (!error) {
		struct timeval begin, end;
		double delta, mb;

		gettimeofday(&begin, NULL);
		error = load_image(&handle, dev, header->pages, test_mode);
#if (CONFIG_ENCRYPT==ENCRYPT_ISAL)
		aes_gcm_dec_128_finalize(&key_data.gkey, &key_data.gctx, key_data.tag, TAG_SIZE);
		error = memcmp(key_data.tag, header->tag, TAG_SIZE);
		if(error) {
			mprintf("Authentication error, unexpected tag.\n");
			error = -EINVAL;
		} else {
			mprintf("Authentication success\n");
		}
#endif
#ifdef CONFIG_VERITY
		if (!error && verify_checksum) {
			md5_finish_ctx(&handle.ctx, checksum);
			if (memcmp(orig_checksum, checksum, 16)) {
				fprintf(stderr,
					"%s: MD5 checksum does not match\n",
					my_name);
				print_checksum(csum_buf, checksum);
				fprintf(stderr,
					"%s: Computed MD5 checksum %s\n",
					my_name, csum_buf);
				error = -EINVAL;
			}
		}
#endif
		free_swap_reader(&handle);
		if (error)
			goto Exit_encrypt;
		gettimeofday(&end, NULL);

		timersub(&end, &begin, &end);
		delta = end.tv_usec / 1000000.0 + end.tv_sec;
		mb = (header->pages * (page_size / 1024.0)) / 1024.0;

		mprintf("wrote %0.1lf MB in %0.1lf seconds (%0.1lf MB/s)\n",
			mb, header->writeout_time, mb / header->writeout_time);

		mprintf("read %0.1lf MB in %0.1lf seconds (%0.1lf MB/s)\n",
			mb, delta, mb / delta);

		mb *= 2.0;
		delta += header->writeout_time;
		mprintf("total image i/o: %0.1lf MB in %0.1lf seconds "
			"(%0.1lf MB/s)\n", mb, delta, mb / delta);

		if (do_decompress) {
			double real_size = header->image_data_size;

			mprintf("Compression ratio %4.2lf\n",
				real_size / (header->pages * page_size));
			real_size /= (1024.0 * 1024.0);
			delta -= header->writeout_time;

			mprintf("wrote %0.1lf MB of compressed data in %0.1lf "
				"seconds (%0.1lf MB/s)\n", real_size,
				header->writeout_time,
				real_size / header->writeout_time);

			mprintf("read %0.1lf MB of compressed data in %0.1lf "
				"seconds (%0.1lf MB/s)\n", real_size,
				delta, real_size / delta);

			real_size *= 2.0;
			delta += header->writeout_time;
			mprintf("total compressed data i/o: %0.1lf MB in %0.1lf "
				"seconds (%0.1lf MB/s)\n", real_size, delta,
				real_size / delta);
		}
	}

 Exit_encrypt:
	if (do_decrypt && !test_mode) {
#if (CONFIG_ENCRYPT == ENCRYPT_GCRYPT)
		gcry_cipher_close(cipher_handle);
#endif
        }

	return error;
}

