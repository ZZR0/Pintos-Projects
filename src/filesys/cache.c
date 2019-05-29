#include"cache.h"
#include<string.h>
#include"devices/block.h"
#include"threads/synch.h"
struct lock CacheLock;
struct BlockCache
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
struct BlockCache ManArr[CacheSize];
void InitCacheMan(void)
{
	int i;
	for(i=0;i<CacheSize;i++)
	{
		ManArr[i].SecNo=0;
		ManArr[i].Use=false;
		ManArr[i].Num=0;
		ManArr[i].Dirty=false;
		lock_init(&ManArr[i].Lock);
	}
	PassTime=0;
	lock_init(&CacheLock);
	Inited=true;
}

void CacheRead(block_sector_t sector,void *buffer)
{	
	int n=InCache(sector);
	if(n==-1)
		n=Fetch(sector);
	lock_acquire(&ManArr[n].Lock);
	ASSERT(n!=-1);
	memcpy(buffer,SecArr[n].data,BLOCK_SECTOR_SIZE);
	CountSec(n);
	lock_release(&ManArr[n].Lock);
//	Fetch(sector+1);
}
void CacheWrite(block_sector_t sector,const void *buffer)
{
	int n=InCache(sector);
	if(n==-1)
		n=Fetch(sector);
	lock_acquire(&ManArr[n].Lock);
	memcpy(SecArr[n].data,buffer,BLOCK_SECTOR_SIZE);
	ManArr[n].Dirty=true;
	CountSec(n);
	lock_release(&ManArr[n].Lock);
//	Fetch(sector+1);
	//WriteBack(n);
}
int Fetch(block_sector_t sector)
{
	lock_acquire(&CacheLock);
	int i,n=-1;
	for(i=0;i<CacheSize;i++)
		if(ManArr[i].Use==false)
		{
			n=i;
			break;
		}
	if(n==-1)
		n=Evict();
	fs_device->ops->read(fs_device->aux,sector,SecArr[n].data);
	fs_device->read_cnt++;
	ManArr[n].Use=true;
	ManArr[n].SecNo=sector;
	ManArr[n].Num=0;
	ManArr[n].Dirty=false;
	lock_release(&CacheLock);
//	printf("Fetch run\n");
	return n;
}
int InCache(block_sector_t sector)
{
	int i;
	for(i=0;i<CacheSize;i++)
		if(ManArr[i].Use==true && ManArr[i].SecNo==sector)
			return i;
	return -1;
}



int Evict()
{
	int i,n=-1;
	unsigned int maxn=0;
	for(i=0;i<CacheSize;i++)
	{
	if(ManArr[i].Use==true&&ManArr[i].Num>=maxn)
		{
			maxn=ManArr[i].Num;
			n=i;
		}
	}
	lock_acquire(&ManArr[n].Lock);
	ASSERT(n!=-1);
	WriteBack(n);
	ManArr[n].Use=false;
	lock_release(&ManArr[n].Lock);
//	printf("Evict run\n");
	return n;
}
void WriteBack(int n)
{
	fs_device->ops->write(fs_device->aux,ManArr[n].SecNo,SecArr[n].data);
	fs_device->write_cnt++;
	ManArr[n].Dirty=false;
}
void DestroyCacheMan(void)
{
	WriteAllBack();
}
void CountSec(int n)
{
	int i;
	for(i=0;i<CacheSize;i++)
		if(ManArr[i].Use==true)
			ManArr[i].Num++;
	ManArr[n].Num=0;
}
void WriteAllBack(void)
{
	int i;
	for(i=0;i<CacheSize;i++)
		if(ManArr[i].Use==true&&ManArr[i].Dirty==true)
			WriteBack(i);
}