#ifndef MEMORY_LIB
#define MEMORY_LIB

#include "esp32/himem.h"

#define SPIFFS_BEGIN()		begin_spiffs_access()
#define SPIFFS_END()		end_spiffs_access()
extern TaskHandle_t init_spiffs_handle;

typedef struct {
    esp_himem_handle_t handle;
	esp_himem_rangehandle_t range;
    size_t size;
	size_t offset;
} himem_block_t;

//PSRAM
void printMemorySize(uint32_t size);
void print_heap_info();
int psram_getSize();
int psram_getFree();

void *psram_malloc(size_t size);
void psram_free(void *ptr);
void *psram_realloc(void *ptr, size_t size);

himem_block_t* mm_himem_alloc(size_t size);
void  mm_himem_read(himem_block_t* blk, size_t off, void* dst, size_t len);
void  mm_himem_write(himem_block_t* blk, size_t off, const void* src, size_t len);
void  mm_himem_free(himem_block_t* blk);

//SPIFFS
typedef struct
{
	size_t fs_total;
	size_t fs_used;
	size_t fs_free;
} spiffs_fs_t;

extern spiffs_fs_t fs_memory;

void init_spiffs();
void begin_spiffs_access();
void end_spiffs_access();
void fs_move(const char *src, const char *dst);
void fs_remove(const char *src);
FILE *fs_open(const char *path, const char *mode);
int fs_read(FILE *f, void *buf, size_t size);
int fs_write(FILE *f, const void *buf, size_t size);
char *fs_gets(FILE *f, char *buf, size_t size);
void fs_close(FILE *f);
int fsize(FILE *file);
void *spiffs_readApp(const char *filename, int *out_size);

//LED
void sys_access();
void sys_led_task(void *arg);
#endif