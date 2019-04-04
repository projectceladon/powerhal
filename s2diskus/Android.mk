LOCAL_PATH := $(call my-dir)

#################################
# lzo static lib
#################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	lzo/src/lzo1x_1.c   \
	lzo/src/lzo1x_1o.c  \
	lzo/src/lzo1x_d2.c  \
	lzo/src/lzo_crc.c   \
	lzo/src/lzo_str.c   \
	lzo/src/lzo1x_1k.c  \
	lzo/src/lzo1x_9x.c  \
	lzo/src/lzo1x_d3.c  \
	lzo/src/lzo_init.c  \
	lzo/src/lzo_util.c  \
	lzo/src/lzo1x_1l.c  \
	lzo/src/lzo1x_d1.c  \
	lzo/src/lzo1x_o.c   \
	lzo/src/lzo_ptr.c

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/lzo/include \
        $(LOCAL_PATH)/lzo/src

LOCAL_MODULE := liblzo

include $(BUILD_STATIC_LIBRARY)

##################################
# lz4 static lib (including lz4_hc
##################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	lz4/lib/lz4.c   \
	lz4/lib/lz4frame.c   \
	lz4/lib/lz4hc.c   \
	lz4/lib/xxhash.c

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/lz4/lib

LOCAL_MODULE := liblz4_static

include $(BUILD_STATIC_LIBRARY)


#################################
# isa-l igzip static lib
#################################
include $(CLEAR_VARS)

ISAL_SRC := \
	isal/igzip/igzip.c \
	isal/igzip/hufftables_c.c \
	isal/igzip/igzip_base.c \
	isal/igzip/igzip_icf_base.c \
	isal/igzip/crc32_gzip_base.c \
	isal/igzip/flatten_ll.c \
	isal/igzip/encode_df.c \
	isal/igzip/igzip_icf_body.c \
	isal/igzip/huff_codes.c \
	isal/igzip/igzip_inflate.c

ISAL_SRC_x86 := \
	isal/igzip/igzip_base_aliases.c \
	isal/igzip/proc_heap_base.c

ISAL_SRC_x86_64 := \
	isal/igzip/adler32_avx2_4.asm  \
	isal/igzip/igzip_decode_block_stateless_01.asm  \
	isal/igzip/igzip_decode_block_stateless_04.asm  \
	isal/igzip/igzip_deflate_hash.asm \
	isal/igzip/igzip_multibinary.asm \
	isal/igzip/adler32_sse.asm \
        isal/igzip/igzip_finish.asm  \
        isal/igzip/igzip_set_long_icf_fg_04.asm \
        isal/igzip/igzip_set_long_icf_fg_06.asm \
        isal/igzip/igzip_body.asm \
        isal/igzip/igzip_gen_icf_map_lh1_04.asm \
        isal/igzip/igzip_gen_icf_map_lh1_06.asm \
        isal/igzip/proc_heap.asm \
	isal/igzip/crc32_gzip.asm \
        isal/igzip/igzip_update_histogram_01.asm  \
        isal/igzip/rfc1951_lookup.asm \
	isal/igzip/igzip_icf_body_h1_gr_bt.asm \
        isal/igzip/igzip_update_histogram_04.asm  \
	isal/igzip/encode_df_04.asm  \
	isal/igzip/encode_df_06.asm  \
	isal/igzip/igzip_icf_finish.asm \
	isal/igzip/igzip_inflate_multibinary.asm


LOCAL_SRC_FILES := $(ISAL_SRC)
LOCAL_SRC_FILES_x86 += $(ISAL_SRC_x86)
LOCAL_SRC_FILES_x86_64 += $(ISAL_SRC_x86_64)

LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/isal/igzip \
        $(LOCAL_PATH)/isal/include

LOCAL_MODULE := libisal_igzip

include $(BUILD_STATIC_LIBRARY)


#################################
# isa-l_crypto aes_gcm static lib
#################################
include $(CLEAR_VARS)

ISAL_CRYPTO_SRC := \
	isal_crypto/aes/gcm_pre.c

ISAL_CRYPTO_SRC_x86 :=

ISAL_CRYPTO_SRC_x86_64 := \
	isal_crypto/aes/keyexp_multibinary.asm \
	isal_crypto/aes/keyexp_128.asm \
	isal_crypto/aes/keyexp_192.asm \
	isal_crypto/aes/keyexp_256.asm \
	isal_crypto/aes/gcm_multibinary.asm \
	isal_crypto/aes/gcm_multibinary_nt.asm \
	isal_crypto/aes/gcm128_avx_gen2.asm  \
	isal_crypto/aes/gcm128_avx_gen2_nt.asm  \
	isal_crypto/aes/gcm128_avx_gen4.asm  \
	isal_crypto/aes/gcm128_avx_gen4_nt.asm  \
	isal_crypto/aes/gcm256_avx_gen2.asm  \
	isal_crypto/aes/gcm256_avx_gen2_nt.asm  \
	isal_crypto/aes/gcm256_avx_gen4.asm  \
	isal_crypto/aes/gcm256_avx_gen4_nt.asm  \
	isal_crypto/aes/gcm128_sse.asm  \
	isal_crypto/aes/gcm128_sse_nt.asm  \
	isal_crypto/aes/gcm256_sse.asm  \
	isal_crypto/aes/gcm256_sse_nt.asm

LOCAL_SRC_FILES := $(ISAL_CRYPTO_SRC)
LOCAL_SRC_FILES_x86 += $(ISAL_CRYPTO_SRC_x86)
LOCAL_SRC_FILES_x86_64 += $(ISAL_CRYPTO_SRC_x86_64)

LOCAL_C_INCLUDES := \
        $(LOCAL_PATH)/isal_crypto/aes \
        $(LOCAL_PATH)/isal_crypto/include

LOCAL_MODULE := libisal_crypto

include $(BUILD_STATIC_LIBRARY)


#######################################
# suspend from suspend-util 1.0
#######################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := suspend.c memalloc.c config_parser.c loglevel.c
LOCAL_STATIC_LIBRARIES := \
        liblzo \
	liblz4_static \
	libisal_igzip \
	libisal_crypto
LOCAL_MODULE := suspend_to_disk
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/lzo/include \
	$(LOCAL_PATH)/lz4/lib \
	$(LOCAL_PATH)/isal/include \
	$(LOCAL_PATH)/isal_crypto/include

#LOCAL_CFLAGS += -DCONFIG_COMPRESS=COMPRESS_LZO -DCONFIG_ENCRYPT=ENCRYPT_ISAL
#LOCAL_CFLAGS += -DCONFIG_COMPRESS=COMPRESS_IGZIP -DCONFIG_ENCRYPT=ENCRYPT_ISAL
LOCAL_CFLAGS += -DCONFIG_COMPRESS=COMPRESS_LZ4 -DCONFIG_ENCRYPT=ENCRYPT_ISAL

include $(BUILD_EXECUTABLE)


#######################################
# resume from suspend-util 1.0
#######################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := resume.c memalloc.c config_parser.c loglevel.c load.c earlyapp.c
LOCAL_STATIC_LIBRARIES := \
        liblzo \
	liblz4_static \
	libisal_igzip \
	libisal_crypto
LOCAL_MODULE := resume_from_disk
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/lzo/include \
	$(LOCAL_PATH)/lz4/lib \
	$(LOCAL_PATH)/isal/include \
	$(LOCAL_PATH)/isal_crypto/include

#LOCAL_CFLAGS += -DCONFIG_COMPRESS=COMPRESS_LZO -DCONFIG_ENCRYPT=ENCRYPT_ISAL
#LOCAL_CFLAGS += -DCONFIG_COMPRESS=COMPRESS_IGZIP -DCONFIG_ENCRYPT=ENCRYPT_ISAL
LOCAL_CFLAGS += -DCONFIG_COMPRESS=COMPRESS_LZ4 -DCONFIG_ENCRYPT=ENCRYPT_ISAL

include $(BUILD_EXECUTABLE)

#######################################
# build swapoffset
#######################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := swap-offset.c
LOCAL_MODULE := swap_offset

include $(BUILD_EXECUTABLE)
