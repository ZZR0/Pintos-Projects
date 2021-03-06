#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define direct (12*512)
#define indirect1 (direct+128*512)
#define indirect2 (indirect1+128*128*512)
#define indirect3 (indirect2+128*128*128*512)

struct PosInfo
{
  uint16_t lev;
  uint32_t off;

  uint32_t sn[4];
  uint32_t np[4];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}



/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector,off_t length)
{
  return inode_create_extend(sector,length,0);
}

bool inode_create_extend (block_sector_t sector, off_t length,uint32_t isdir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    size_t sectors = bytes_to_sectors (length);
    disk_inode->length = length;
    disk_inode->isdir=isdir;
    disk_inode->magic = INODE_MAGIC;
    for(int i=0;i<BLOCK_NUM;i++)    
      disk_inode->blocks[i]=0;
    success=true;
    static char zeros[BLOCK_SECTOR_SIZE];
    struct inode ie;
    ie.deny_write_cnt=0;
    ie.data=*disk_inode;
    ie.open_cnt=0;
    for (int i = 0; i < sectors; i++)
    {
      size_t  minb=length<512?length:512;
      inode_write_at(&ie,zeros,minb,i*512);  
      length-=512;             
    } 
    *disk_inode=ie.data;
    cache_write (sector, disk_inode); 
    free (disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */

void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
      int i;
      for(i=0;i<12;i++)
        if(inode->data.blocks[i]!=0)
          free_map_release(inode->data.blocks[i],1);
      if(inode->data.blocks[12]!=0)
      {
        unsigned *arr=(unsigned *)malloc(512);
        ASSERT(arr!=NULL);
        cache_read(inode->data.blocks[12],arr);
        int j;
        for(j=0;j<128&&arr[j]!=0;j++)
            free_map_release(arr[j],1);
        free(arr);
        free_map_release(inode->data.blocks[12],1);
      }
      if(inode->data.blocks[13]!=0)
      {
        unsigned *arr=(unsigned *)malloc(512);
        unsigned *brr=(unsigned *)malloc(512);
        ASSERT(arr!=NULL&&brr!=NULL)
        cache_read(inode->data.blocks[13],arr);
        int j,k;
        for(j=0;j<128&&arr[j]!=0;j++)
        {
          cache_read(arr[j],brr);
          for(k=0;k<128&&brr[k]!=0;k++)
              free_map_release(brr[k],1); 
          free_map_release(arr[j],1);
        }
        free_map_release(inode->data.blocks[13],1);
        free(arr);
        free(brr);
      }
      if(inode->data.blocks[14]!=0)
      {
        unsigned *arr=(unsigned *)malloc(512);
        unsigned *brr=(unsigned *)malloc(512);
        unsigned *crr=(unsigned *)malloc(512);
        ASSERT(arr!=0&&brr!=0&&crr!=0);
        cache_read(inode->data.blocks[14],arr);
        int j,k,l;
        for(j=0;j<128&&arr[j]!=0;j++)
        {
          cache_read(arr[j],brr);
          
            for(k=0;k<128&&brr[k]!=0;k++)
            {
              cache_read(brr[k],crr);
              for(l=0;l<128&&crr[l]!=0;l++)
                free_map_release(crr[l],1);
              free_map_release(brr[k],1);
            }
          
          free_map_release(arr[j],1);
        }
        free_map_release(inode->data.blocks[14],1);
        free(arr);
        free(brr);
        free(crr);
      }
    free_map_release(inode->sector,1);
    }
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  off_t begin_offset=offset;
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  struct PosInfo pi;
  uint32_t *arr=NULL;
      arr=(uint32_t *)malloc(BLOCK_SECTOR_SIZE);
  off_t cur_read;
  while(size>0)
  {

    if(get_pos(&pi,offset)!=0)
      goto end;
    cur_read= 512-pi.off<size?512-pi.off:size;
    if(pi.lev>=0)
    {
      uint32_t nsec=inode->data.blocks[pi.np[0]];
      if(nsec!=0)
      {
        pi.sn[0]=nsec;
        cache_read(nsec,(void *)arr);
      }
      else
        goto end;
    }
    if(pi.lev>=1)
    {
      uint32_t nsec=arr[pi.np[1]];
      if(nsec!=0)
      {
          pi.sn[1]=nsec;
        cache_read(nsec,(void *)arr);
      }
      else
        goto end;
      
    }
    if(pi.lev>=2)
    {
      uint32_t nsec=arr[pi.np[2]];
      if(nsec!=0)
      {
        pi.sn[2]=nsec;
        cache_read(nsec,(void *)arr);
      }
      else goto end;
    }
    if(pi.lev>=3)
    {
      uint32_t nsec=arr[pi.np[3]];
      if(nsec!=0)
      {
        pi.sn[3]=nsec;
        cache_read(nsec,(void*)arr);
      }
      else
        goto end;
    }
    ASSERT(pi.lev<4);
    memcpy(buffer+bytes_read,(void *)arr+pi.off,cur_read);
    size-=cur_read;
    offset+=cur_read;
    bytes_read+=cur_read;
    continue;
    end:
      memset(buffer+bytes_read,0,cur_read);
      bytes_read+=cur_read;
      size-=cur_read;
      offset+=cur_read;
  }
  free(arr);
  off_t ShouldRead=inode->data.length-begin_offset;
  if(ShouldRead<0)
    ShouldRead=0;
  if(ShouldRead<bytes_read)
    bytes_read=ShouldRead;
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  struct PosInfo pi;
  uint32_t *arr=NULL;
  //while(arr==NULL)
 arr=(uint32_t *)malloc(BLOCK_SECTOR_SIZE);
  uint32_t cur_write;
  while(size>0)
  {
    get_pos(&pi,offset);
    cur_write=512-pi.off<size?512-pi.off:size;
    if(pi.lev>=0)
    {
      uint32_t nsec=inode->data.blocks[pi.np[0]];
      if(nsec==0)
      {
          if (!free_map_allocate (1, &nsec))
        goto end;
      inode->data.blocks[pi.np[0]]=nsec;
      memset(arr,0,512);
      }
      else
        cache_read(nsec,arr);
      pi.sn[0]=nsec;
    }
    if(pi.lev>=1)
    {
      uint32_t nsec=arr[pi.np[1]];
      if(nsec==0)
      {
        if(!free_map_allocate(1,&nsec))
          goto end;
        arr[pi.np[1]]=nsec;
        cache_write(pi.sn[0],arr);
        memset(arr,0,512);
      }
      else
        cache_read(nsec,arr);
      pi.sn[1]=nsec;
    }
    if(pi.lev>=2)
    {
      uint32_t nsec=arr[pi.np[2]];
      if(nsec==0)
      {
        if(!free_map_allocate(1,&nsec))
          goto end;
        arr[pi.np[2]]=nsec;
        cache_write(pi.sn[1],arr);
        memset(arr,0,512);
      }
      else
        cache_read(nsec,arr);
      pi.sn[2]=nsec;
    }
    if(pi.lev>=3)
    {
      uint32_t nsec=arr[pi.np[3]];
      if(nsec==0)
      {
        if(!free_map_allocate(1,&nsec))
          goto end;
        arr[pi.np[3]]=nsec;
        cache_write(pi.sn[2],arr);
        memset(arr,0,512);
      }
      else
        cache_read(nsec,arr);
      pi.sn[3]=nsec;
    }
    if(pi.lev>=4)
      goto end;
    memcpy((void *)arr+pi.off,buffer+bytes_written,cur_write);
    cache_write(pi.sn[pi.lev],arr);
    bytes_written+=cur_write;
    offset+=cur_write;
    size-=cur_write;
  }
 end:
  if(offset > inode->data.length)
  {
    inode->data.length=offset;
  cache_write(inode->sector,&inode->data);
  } 
  //lock_release(&inode->xlock);
//  lock_release(&inode->slock);
  free(arr);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}


int get_pos(struct PosInfo *pi,off_t off)
{
  if(off<0||off>=indirect3) return 1;
  if(off>=indirect2)
  {
    off-=indirect2;
    pi->lev=3;
    
    pi->sn[0]=0;
    pi->np[0]=14;

    pi->sn[1]=0;
    pi->np[1]=off/(128*128*512);
    off-=pi->np[1]*(128*128*512);

    pi->sn[2]=0;
    pi->np[2]=off/(128*512);
    off-=pi->np[2]*(128*512);

    pi->sn[3]=0;
    pi->np[3]=off/512;
    pi->off=off%512;
  }
  else if(off>=indirect1)
  {
    off-=indirect1;
    pi->lev=2;

    pi->sn[0]=0;
    pi->np[0]=13;

    pi->sn[1]=0;
    pi->np[1]=off/(512*128);

    off-=pi->np[1]*(512*128);

    pi->sn[2]=0;
    pi->np[2]=off/512;

    pi->off=off%512;
  }
  else if(off>=direct)
  {
    off-=direct;
    pi->lev=1;

    pi->sn[0]=0;
    pi->np[0]=12;

    pi->sn[1]=0;
    pi->np[1]=off/512;

    pi->off=off%512;
  }
  else
  {
    pi->lev=0;
    pi->sn[0]=0;
    pi->np[0]=off/512;  
    pi->off=off%512;
  }
  return 0;
}