#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <devices/shutdown.h>
#include <threads/vaddr.h>
#include <filesys/filesys.h>
#include <string.h>
#include <filesys/file.h>
#include <devices/input.h>
#include <threads/palloc.h>
#include <threads/malloc.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "process.h"
#include "pagedir.h"
#include "syscall.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

// syscall array
syscall_function syscalls[SYSCALL_NUMBER];
struct lock DirOpenLock;

static void syscall_handler (struct intr_frame *);
struct file_node * GetFile(struct thread *t, int fd);
bool DirCreate(const char *name);
char * MakePath(const char *from);
void xstrcpy(char *to,const char *from);
struct dir *OpenDir(char *path,int *pos);
char *GetPwd();

void exit(int exit_status){
  thread_current()->exit_status = exit_status;
  thread_exit ();
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  // initialize the syscalls
  for(int i = 0; i < SYSCALL_NUMBER; i++) syscalls[i] = NULL;
  // bind the syscalls to specific index of the array
  syscalls[SYS_EXIT] = sys_exit;
  syscalls[SYS_HALT] = sys_halt;
  syscalls[SYS_EXEC] = sys_exec;
  syscalls[SYS_WAIT] = sys_wait;
  syscalls[SYS_CREATE] = sys_create;
  syscalls[SYS_REMOVE] = sys_remove;
  syscalls[SYS_OPEN] = sys_open;
  syscalls[SYS_FILESIZE] = sys_filesize;
  syscalls[SYS_READ] = sys_read;
  syscalls[SYS_WRITE] = sys_write;
  syscalls[SYS_SEEK] = sys_seek;
  syscalls[SYS_TELL] = sys_tell;
  syscalls[SYS_CLOSE] = sys_close;
  syscalls[SYS_CHDIR] = IChDir;
  syscalls[SYS_MKDIR] = IMkDir;
  syscalls[SYS_READDIR] = IReadDir;
  syscalls[SYS_ISDIR] = IIsDir;
  syscalls[SYS_INUMBER] = IInumber;

  lock_init(&DirOpenLock);
}

// check whether page p and p+3 has been in kernel virtual memory
void check_page(void *p) {
  void *pagedir = pagedir_get_page(thread_current()->pagedir, p);
  if(pagedir == NULL) exit(-1);
  pagedir = pagedir_get_page(thread_current()->pagedir, p + 3);
  if(pagedir == NULL) exit(-1);
}

// check whether page p and p+3 is a user virtual address
void check_addr(void *p) {
  if(!is_user_vaddr(p)) exit(-1);
  if(!is_user_vaddr(p + 3)) exit(-1);
}

// make check for page p
void check(void *p) {
  if(p == NULL) exit(-1);
  check_addr(p);
  check_page(p);
}

// make check for every function arguments
void check_func_args(void *p, int argc) {
  for(int i = 0; i < argc; i++) {
    check(p);
    p++;
  }
}

// search the file list of the thread_current()
// to get the file has corresponding fd
struct file_node * find_file(struct list *files, int fd){
  struct list_elem *e;
  struct file_node * fn =NULL;
  for (e = list_begin (files); e != list_end (files); e = list_next (e)){
    fn = list_entry (e, struct file_node, file_elem);
    if (fd == fn->fd)
      return fn;
  }
  return NULL;
}

static void
syscall_handler (struct intr_frame *f)
{
  check((void *)f->esp);
  check((void *)(f->esp + 4));
  int num=*((int *)(f->esp));
  // check whether the function is implemented
  if(num < 0 || num >= SYSCALL_NUMBER) exit(-1);
  if(syscalls[num] == NULL) exit(-1);
  syscalls[num](f);
}

void sys_exit(struct intr_frame * f) {
  int *p = f->esp;
  // save exit status
  exit(*(p + 1));
}

void sys_halt(struct intr_frame * f UNUSED) {
  shutdown_power_off();
}

void sys_exec(struct intr_frame * f) {
  int * p =f->esp;
  check((void *)(p + 1));
  check((void *)(*(p + 1)));
  f->eax = process_execute((char*)*(p + 1));
}

void sys_wait(struct intr_frame * f) {
  int * p =f->esp;
  check(p + 1);
  f->eax = process_wait(*(p + 1));
}

void sys_create(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 2);
  check((void *)*(p + 1));

  acquire_file_lock();
  // thread_exit ();
  f->eax = filesys_create(MakePath((const char *)*(p + 1)),*(p + 2));
  release_file_lock();
}

void sys_remove(struct intr_frame * f) {
  int * p =f->esp;
  
  check_func_args((void *)(p + 1), 1);
  check((void*)*(p + 1));

  acquire_file_lock();
  f->eax = filesys_remove((const char *)*(p + 1));
  release_file_lock();
}

void sys_open(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 1);
  check((void*)*(p + 1));

  struct thread * t = thread_current();
  acquire_file_lock();
  struct file * open_f = filesys_open(MakePath((const char *)*(p + 1)));
  release_file_lock();
  // check whether the open file is valid
  if(open_f){
    struct file_node *fn = malloc(sizeof(struct file_node));
    fn->fd = t->max_fd++;
    fn->file = open_f;
    // put in file list of the corresponding thread
    list_push_back(&t->files, &fn->file_elem);
    f->eax = fn->fd;
  } else
    f->eax = -1;
}

void sys_filesize(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 1);
  struct file_node * open_f = find_file(&thread_current()->files, *(p + 1));
  // check whether the write file is valid
  if (open_f){
    acquire_file_lock();
    f->eax = file_length(open_f->file);
    release_file_lock();
  } else
    f->eax = -1;
}

void sys_read(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 3);
  check((void *)*(p + 2));

  int fd = *(p + 1);
  uint8_t * buffer = (uint8_t*)*(p + 2);
  off_t size = *(p + 3);  
  // read from standard input
  if (fd == 0) {
    for (int i=0; i<size; i++)
      buffer[i] = input_getc();
    f->eax = size;
  }
  else{
    struct file_node * open_f = find_file(&thread_current()->files, *(p + 1));
    // check whether the read file is valid
    if (open_f){
      acquire_file_lock();
      f->eax = file_read(open_f->file, buffer, size);
      release_file_lock();
    } else
      f->eax = -1;
  }
}

void sys_write(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 3);
  check((void *)*(p + 2));
  int fd2 = *(p + 1);
  const char * buffer2 = (const char *)*(p + 2);
  off_t size2 = *(p + 3);
  // write to standard output
  if (fd2==1) {
    putbuf(buffer2,size2);
    f->eax = size2;
  }
  else{
    struct file_node * openf = find_file(&thread_current()->files, *(p + 1));
    // check whether the write file is valid
    if (openf){
      if (openf->file->inode->data.isdir)
      {
        f->eax = -1;
      }else{
        acquire_file_lock();
        f->eax = file_write(openf->file, buffer2, size2);
        release_file_lock();
      }
    } else
      f->eax = 0;
  }
}

void sys_seek(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 2);
  struct file_node * openf = find_file(&thread_current()->files, *(p + 1));
  if (openf){
    acquire_file_lock();
    file_seek(openf->file, *(p + 2));
    release_file_lock();
  }
}

void sys_tell(struct intr_frame * f) {
  int * p =f->esp;
  check_func_args((void *)(p + 1), 1);
  struct file_node * open_f = find_file(&thread_current()->files, *(p + 1));
  // check whether the tell file is valid
  if (open_f){
    acquire_file_lock();
    f->eax = file_tell(open_f->file);
    release_file_lock();
  }else
    f->eax = -1;
}

void sys_close(struct intr_frame * f) {
  int *p = f->esp;
  check_func_args((void *)(p + 1), 1);
  struct file_node * openf = find_file(&thread_current()->files, *(p + 1));
  if (openf){
    acquire_file_lock();
    file_close(openf->file);
    release_file_lock();
    // remove file form file list
    list_remove(&openf->file_elem);
    free(openf);
  }
}


void IMkDir(struct intr_frame *f)
{
  int *p = f->esp;
  check_func_args((void *)(p + 1), 1);
  char *DirName=*((int *)f->esp+1);
  int n=strlen(DirName);
  if(DirName[n-1]=='/')
  {
    DirName[n-1]=0;
    n--;
  }
  if(n<=0)
  {
  f->eax=0;
  return;
  }
  f->eax=DirCreate(DirName);
}

void IChDir(struct intr_frame *f)
{
  int *p = f->esp;
  check_func_args((void *)(p + 1), 1);
  char *DirName=*((int *)f->esp+1);
  char *path=MakePath(DirName);
  int n=strlen(path);
  if(path[n-1]!='/')
  {
    path[n++]='/';
    path[n++]='a';
    path[n]=0;
  }
  char *tpath=malloc(n+1);
  ASSERT(tpath!=NULL);
  xstrcpy(tpath,path);
  int cur=0;
  struct dir *dir=OpenDir(path,&cur);
  if(dir==NULL)
  {
    f->eax=0;
    free(path);
    return;
  }
  dir_close(dir);
  while(tpath[n]!='/')
    tpath[n--]=0;
  struct thread *curthr=thread_current();
  if(curthr->pwd!=NULL)
  free(curthr->pwd);
  n=strlen(tpath);
  curthr->pwd=malloc(n+1);
  xstrcpy(thread_current()->pwd,tpath);
  f->eax=1;
  free(path);
  free(tpath);
}

void IReadDir(struct intr_frame *f)
{
  int *p = f->esp;
  check_func_args((void *)(p + 1), 2);
  if((char *)*((unsigned int *)f->esp+2)==NULL)
  {
      f->eax=-1;
  }
  int fd=*((unsigned int *)f->esp+1);
  char *name=*((unsigned int *)f->esp+2);
  struct file_node *fp=GetFile(thread_current(),fd);
  if(fp->file->inode->removed)
  {
    f->eax=0;
    return ;
  }
  struct dir dirx;
  dirx.pos=fp->file->pos;
  dirx.inode=fp->file->inode;
  if(dir_readdir(&dirx,name))
    f->eax=1;
  else
    f->eax=0;
  fp->file->pos=dirx.pos;
}

void IInumber(struct intr_frame *f)
{
  int *p = f->esp;
  check_func_args((void *)(p + 1), 1);
  int fd=*((int *)f->esp+1);
  struct file_node *fn=GetFile(thread_current(),fd);
  f->eax= fn->file->inode->sector;
}

void IIsDir(struct intr_frame *f)
{
  int *p = f->esp;
  check_func_args((void *)(p + 1), 1);
  int fd=*((int *)f->esp+1);
  struct file_node *fn=GetFile(thread_current(),fd);
  f->eax= fn->file->inode->data.isdir;
}

struct file_node *GetFile(struct thread *t, int fd)
{
  struct list_elem *e;
  for (e = list_begin (&t->files); e != list_end (&t->files); e = list_next (e))
  {
    struct file_node *fn = list_entry (e, struct file_node, file_elem);
    if(fn->fd==fd){
        return fn;
    }
  }
  return NULL;
}

bool DirCreate(const char *name) 
{
  block_sector_t inode_sector = 0;
//  struct dir *dir = dir_open_root ();
 char *path=MakePath(name);
 ASSERT(path!=0);
 int cur;
 struct dir *dir=OpenDir(path,&cur);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create_ex (inode_sector, 20*sizeof(struct dir_entry),1)
                  && dir_add(dir,path+cur, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
 free(path);
  return success;
}

void xstrcpy(char *to,const char *from)
{
  while(*to++=*from++);
}

char * MakePath(const char *from)
{
  //ASSERT(to!=NULL);
  const char *pwd=GetPwd();
  char *to;
  int lf=strlen(from);
  int len=lf;
  if(pwd!=NULL)
    len+=strlen(pwd);
  to=(char *)malloc(len+5);//！注意这里，没有释放内存
  if(to==NULL)
  {
    printf("error\n");
    return 0;
  }
  if(pwd==NULL)
    xstrcpy(to,"");
  else
    xstrcpy(to,pwd);  
  int lp=strlen(to);
  int pos=lp-1;
  if(lp > 0 && to[lp-1]!='/')
  {
    to[lp]='/';
    to[lp+1]=0;
  }
  if(from[0]=='/' && lp > 1)
  {
    xstrcpy(to,from+1);
    return to;
    // pos=0;
  }
  pos=0;
  char pre=0;
  int i;
  for(i=0;i<lf;i++)
  {

    if(from[i]=='.'&&pre=='.')
    {
      if(pos>0)pos--;
      while(pos>0&&to[pos]!='/')
        pos--;
    }   
    else if(from[i]=='/')
    {
      if(to[pos]!='/')
        to[++pos]=from[i];
    }
    else if(from[i]=='.'&&pre==0);
       
    else if(pre=='/'&&from[i]=='.');
    else
    to[++pos]=from[i];
    pre=from[i];
  } 
  to[++pos]=0;
  return to;
  
}

struct dir *OpenDir(char *path,int *pos)
{
  lock_acquire(&DirOpenLock);
  struct dir *dir=dir_open_root();
  struct inode *inode=NULL;
  int cur=0,i,n;
  n=strlen(path);
  for(i=0;i<n;i++)
    if(path[i]=='/')
    {
      path[i]=0;
      dir_lookup(dir,path+cur,&inode);
      dir_close(dir);
      if(inode==NULL)
        goto end;
      if(inode->data.isdir!=1)
        goto end;
      dir=dir_open(inode);
      inode=NULL;
      cur=i+1;
    }
  *pos=cur;
  lock_release(&DirOpenLock);
  return dir;

end:
  lock_release(&DirOpenLock);
  return NULL;
}

char *GetPwd()
{
  return thread_current()->pwd;
}
