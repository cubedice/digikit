// Read-only descriptor exported by experimental michael/loop-storage (DT2 1.17).
// One shared lease: use one capture client. Validate magic/version/capacity
// before accessing audio, and initialize every frame before publishing it.
.EXTERN _loop_buffer_info;
#define DK_LOOP_STORAGE_MAGIC 0x4c425546
#define DK_LOOP_STORAGE_VERSION 1
#define DK_LOOP_STORAGE_W_MAGIC 0
#define DK_LOOP_STORAGE_W_VERSION 1
#define DK_LOOP_STORAGE_W_BASE 2
#define DK_LOOP_STORAGE_W_BYTES 3
#define DK_LOOP_STORAGE_WORDS 4
