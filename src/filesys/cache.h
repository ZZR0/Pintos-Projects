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
void InitCacheMan(void);
void CacheRead(block_sector_t sector,void *buffer);
void CacheWrite(block_sector_t,const void *buffer);
int Fetch(block_sector_t sector);
int InCache(block_sector_t sector);
int Evict(void);
void WriteBack(int n);
void DestroyCacheMan(void);
void CountSec(int n);
void WriteAllBack(void);
#endif