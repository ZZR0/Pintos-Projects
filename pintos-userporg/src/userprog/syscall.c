#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include  "process.h"
#include <string.h>
#include "devices/shutdown.h"


static void syscall_handler (struct intr_frame *);

void (*syscall_proc[CALL_NUM])(struct intr_frame*);

void HALT(struct intr_frame *f);
void EXIT(struct intr_frame *f);
void EXEC(struct intr_frame *f);
void WAIT(struct intr_frame *f);
void CREATE(struct intr_frame *f);
void REMOVE(struct intr_frame *f);
void OPEN(struct intr_frame *f);
void FILESIZE(struct intr_frame *f);
void READ(struct intr_frame *f);
void WRITE(struct intr_frame*);
void SEEK(struct intr_frame *f);
void TELL(struct intr_frame *f);
void CLOSE(struct intr_frame *f);

void abnormal_exit();

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  syscall_proc[SYS_HALT]=HALT;
  syscall_proc[SYS_EXIT]=EXIT;
  syscall_proc[SYS_EXEC]=EXEC;
  syscall_proc[SYS_WAIT]=WAIT;
  syscall_proc[SYS_CREATE]=CREATE;
  syscall_proc[SYS_REMOVE]=REMOVE;
  syscall_proc[SYS_OPEN]=OPEN;
  syscall_proc[SYS_FILESIZE]=FILESIZE;
  syscall_proc[SYS_READ]=READ;
  syscall_proc[SYS_WRITE]=WRITE;
  syscall_proc[SYS_SEEK]=SEEK;
  syscall_proc[SYS_TELL]=TELL;
  syscall_proc[SYS_CLOSE]=CLOSE;
  
}

static void
syscall_handler (struct intr_frame *f)
{

  int Num=*((int*)(f->esp));
  syscall_proc[Num](f);

}

void HALT(struct intr_frame *f)
{
  shutdown_power_off();
  f->eax=0;
}

void EXIT(struct intr_frame *f)
{
  if(!is_user_vaddr(((int*)f->esp)+1))
    abnormal_exit();

  thread_current()->ret=*((int*)f->esp+1);
  f->eax=0;
  thread_exit();
}

void EXEC(struct intr_frame *f)
{

  const char *file_name = (char*)*((int*)f->esp+1);

  if(file_name == NULL)
  {
      f->eax=-1;
      return;
  }
  
  tid_t tid=process_execute (file_name);
  struct thread *t=get_thread_by_tid(tid);
  sema_down(&t->load_sema);
  f->eax=t->tid;
    
}

void WAIT(struct intr_frame *f)
{

  tid_t tid = *((int*)f->esp+1);
  if(tid == -1)
    f->eax = tid;
  else
    f->eax = process_wait(tid);

}

void CREATE(struct intr_frame *f)
{
  // printf("CREATE\n");


  const char *file_name = (char*)*((int*)f->esp+1);
  off_t size = *((unsigned int*)f->esp+2);

  if(file_name == NULL)
  {
    f->eax = -1;
    abnormal_exit();
  }

  f->eax = filesys_create(file_name, size);
}

void REMOVE(struct intr_frame *f)
{


  const char *file_name = (char *)*((int*)f->esp+1);
  f->eax = filesys_remove (file_name);
}

void OPEN(struct intr_frame *f)
{
  
  const char *file_name = (char *)*((int*)f->esp+1);
  if(file_name == NULL)
  {
    f->eax = -1;
    abnormal_exit();
  }
  struct thread *cur = thread_current();
  struct file_node *fn = (struct file_node *)malloc(sizeof(struct file_node));
  fn->f = filesys_open(file_name);
  if(fn->f == NULL)//
    fn->fd = -1;
  else
    fn->fd = thread_allocate_fd(cur);

  f->eax=fn->fd;
  if(fn->fd == -1)
    free(fn);
  else
    list_push_back(&cur->file_list,&fn->elem);
}

void FILESIZE(struct intr_frame *f)
{
  int fd = *((int*)f->esp+1);
  struct thread *cur = thread_current();
  struct file_node *fn = thread_get_file_node(cur,fd);

  if(fn != NULL)
    f->eax=file_length (fn->f);
  else
    f->eax = -1;
}

void READ(struct intr_frame *f)
{
  int *esp = (int*)f->esp;
  int fd=*(esp+1);
  char *buffer=(char *)*(esp+2);
  unsigned size=*(esp+3);

  if(buffer==NULL || !is_user_vaddr(buffer+size))
  {
    f->eax=-1;
    abnormal_exit();
  }

  struct thread *cur=thread_current();
  struct file_node *fn=NULL;

  if(fd==STDIN_FILENO)              
  {
    for(int i=0;i<size;i++)
      buffer[i] = input_getc();
  }
  else                            
  {
    fn= thread_get_file_node(cur,fd);         

    if(fn != NULL)
      f->eax=file_read(fn->f,buffer,size);
    else
      f->eax=-1;
  }
}

void WRITE(struct intr_frame *f) 
{

  int *esp = (int*)f->esp;

  int fd = *(esp+1);             
  char *buffer = (char *)*(esp+2); 
  off_t size = *(esp+3);       
  // printf("%d\n", size);
  // printf("%s\n", buffer);

  if(fd==STDOUT_FILENO)        
  {
    putbuf (buffer, size);
    f->eax=0;
    return;
  }

  struct thread *cur=thread_current();
  struct file_node *fn = thread_get_file_node(cur,fd); 

  if(fn != NULL)
    f->eax=file_write(fn->f,buffer,size);
  else
    f->eax=0;

}

void SEEK(struct intr_frame *f)
{


  int fd=*((int*)f->esp+1);
  off_t pos=*((unsigned int*)f->esp+2);

  struct thread *cur=thread_current();
  struct file_node *fn = thread_get_file_node(cur,fd);

  file_seek (fn->f,pos);
}

void TELL(struct intr_frame *f)
{

  int fd=*((int*)f->esp+1);
  
  struct thread *cur=thread_current();
  struct file_node *fn = thread_get_file_node(cur,fd);

  if(fn != NULL && fn->f != NULL)
    f->eax=file_tell (fn->f);
  else
    f->eax=-1;
}

void CLOSE(struct intr_frame *f)
{
  struct thread *cur=thread_current();
  int fd=*((int*)f->esp+1);

  f->eax=thread_close_file(cur,fd);
}

void abnormal_exit()    
{
  thread_current()->ret = -1;
  thread_exit();
}

