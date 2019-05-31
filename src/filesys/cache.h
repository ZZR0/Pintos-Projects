#ifndef __CACHE_H__
#define __CACHE_H__
#include<stdio.h>
#include<string.h>
#include"threads/malloc.h"
#include"filesys/filesys.h"
#define CacheSize 64 //64 sectors used 32KB 8Pages
#include"devices/block.h"
extern unsigned int PassTime;
extern bool Inited;
void cache_init(void);
void cache_read(block_sector_t sector,void *buffer);
void cache_write(block_sector_t,const void *buffer);
int Fetch(block_sector_t sector);
int in_cache(block_sector_t sector);
int Evict(void);
void write_back(int n);
void cache_close(void);
void hit_count(int n);
void write_back_all(void);
#endif