#include"cache.h"
#include<string.h>
#include"devices/block.h"
#include"threads/synch.h"
struct lock CacheLock;
struct Cache
{
	size_t SecNo;
	bool Use;
	unsigned Num;
	bool Dirty;
	//bool Locked;
	struct lock Lock;
};
struct Sec
{
	unsigned char data[512];
};
unsigned int PassTime=0;
bool Inited=false;
struct Sec SecArr[CacheSize];
struct Cache cache[CacheSize];
void cache_init(void)
{
	int i;
	for(i=0;i<CacheSize;i++)
	{
		cache[i].SecNo=0;
		cache[i].Use=false;
		cache[i].Num=0;
		cache[i].Dirty=false;
		lock_init(&cache[i].Lock);
	}
	PassTime=0;
	lock_init(&CacheLock);
	Inited=true;
}

void cache_read(block_sector_t sector,void *buffer)
{	
	int n=in_cache(sector);
	if(n==-1)
		n=Fetch(sector);
	lock_acquire(&cache[n].Lock);
	ASSERT(n!=-1);
	memcpy(buffer,SecArr[n].data,BLOCK_SECTOR_SIZE);
	hit_count(n);
	lock_release(&cache[n].Lock);
//	Fetch(sector+1);
}
void cache_write(block_sector_t sector,const void *buffer)
{
	int n=in_cache(sector);
	if(n==-1)
		n=Fetch(sector);
	lock_acquire(&cache[n].Lock);
	memcpy(SecArr[n].data,buffer,BLOCK_SECTOR_SIZE);
	cache[n].Dirty=true;
	hit_count(n);
	lock_release(&cache[n].Lock);
}
int Fetch(block_sector_t sector)
{
	lock_acquire(&CacheLock);
	int i,n=-1;
	for(i=0;i<CacheSize;i++)
	if(cache[i].Use==false)
	{
		n=i;
		break;
	}
	if(n==-1)
		n=Evict();
	fs_device->ops->read(fs_device->aux,sector,SecArr[n].data);
	fs_device->read_cnt++;
	cache[n].Use=true;
	cache[n].SecNo=sector;
	cache[n].Num=0;
	cache[n].Dirty=false;
	lock_release(&CacheLock);
//	printf("Fetch run\n");
	return n;
}
int in_cache(block_sector_t sector)
{
	int i;
	for(i=0;i<CacheSize;i++)
		if(cache[i].Use==true && cache[i].SecNo==sector)
			return i;
	return -1;
}



int Evict()
{
	int i,n=-1;
	unsigned int maxn=0;
	for(i=0;i<CacheSize;i++)
	{
	if(cache[i].Use==true&&cache[i].Num>=maxn)
		{
			maxn=cache[i].Num;
			n=i;
		}
	}
	lock_acquire(&cache[n].Lock);
	ASSERT(n!=-1);
	write_back(n);
	cache[n].Use=false;
	lock_release(&cache[n].Lock);
//	printf("Evict run\n");
	return n;
}
void write_back(int n)
{
	fs_device->ops->write(fs_device->aux,cache[n].SecNo,SecArr[n].data);
	fs_device->write_cnt++;
	cache[n].Dirty=false;
}
void cache_close(void)
{
	write_back_all();
}
void hit_count(int n)
{
	int i;
	for(i=0;i<CacheSize;i++)
		if(cache[i].Use==true)
			cache[i].Num++;
	cache[n].Num=0;
}
void write_back_all(void)
{
	int i;
	for(i=0;i<CacheSize;i++)
		if(cache[i].Use==true&&cache[i].Dirty==true)
			write_back(i);
}