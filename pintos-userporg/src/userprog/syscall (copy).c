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

typedef void (*CALL_PROC)(struct intr_frame*);
CALL_PROC syscall_proc[CALL_NUM];

void WRITE(struct intr_frame*);
void EXIT(struct intr_frame *f);
void ExitStatus(int status);
void CREATE(struct intr_frame *f);
void OPEN(struct intr_frame *f);
void CLOSE(struct intr_frame *f);
void READ(struct intr_frame *f);
void FILESIZE(struct intr_frame *f);
void EXEC(struct intr_frame *f);
void WAIT(struct intr_frame *f);
void SEEK(struct intr_frame *f);
void REMOVE(struct intr_frame *f);
void TELL(struct intr_frame *f);
void HALT(struct intr_frame *f);
struct file_node *GetFile(struct thread *t,int fd);
void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  for(int i=0;i<CALL_NUM;i++)
    syscall_proc[i]=NULL;

  syscall_proc[SYS_WRITE]=WRITE;
  syscall_proc[SYS_EXIT]=EXIT;
  syscall_proc[SYS_CREATE]=CREATE;
  syscall_proc[SYS_OPEN]=OPEN;
  syscall_proc[SYS_CLOSE]=CLOSE;
  syscall_proc[SYS_READ]=READ;
  syscall_proc[SYS_FILESIZE]=FILESIZE;
  syscall_proc[SYS_EXEC]=EXEC;
  syscall_proc[SYS_WAIT]=WAIT;
  syscall_proc[SYS_SEEK]=SEEK;
  syscall_proc[SYS_REMOVE]=REMOVE;
  syscall_proc[SYS_TELL]=TELL;
  syscall_proc[SYS_HALT]=HALT;
}

static void
syscall_handler (struct intr_frame *f)
{
    if(!is_user_vaddr(f->esp))
      ExitStatus(-1);
   int No=*((int *)(f->esp));

   if(No>=CALL_NUM||CALL_NUM<0)
   {
       printf("We don't have this System Call!\n");
       ExitStatus(-1);
   }
   if(syscall_proc[No]==NULL)
   {
       printf("this System Call %d not Implement!\n",No);
       ExitStatus(-1);
   }
   syscall_proc[No](f);
}
void WRITE(struct intr_frame *f)  //三个参数
{

    int *esp=(int *)f->esp;
      if(!is_user_vaddr(esp+3))
        ExitStatus(-1);
    int fd=*(esp+1);              //文件句柄
    char *buffer=(char *)*(esp+2); //要输出人缓冲
    unsigned size=*(esp+3);       //输出内容大小。
    // printf("%d\n", size);
    // printf("%s\n", buffer);

    if(fd==STDOUT_FILENO)        //标准输出设备
    {
        putbuf (buffer, size);
        f->eax=0;
    }
    else                        //文件
    {
        struct thread *cur=thread_current();
        struct file_node *fn=GetFile(cur,fd); //获取文件指针
        if(fn==NULL)
        {
            f->eax=0;
            return;
        }

        f->eax=file_write(fn->f,buffer,size);//写文件

    }

}
void EXIT(struct intr_frame *f)  //一个参数  正常退出时使用
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    cur->ret=*((int *)f->esp+1);
    f->eax=0;
    thread_exit();

}
void ExitStatus(int status)      //非正常退出时使用
{
    struct thread *cur=thread_current();
    cur->ret=status;
    thread_exit();
}
void CREATE(struct intr_frame *f)  //两个参数
{
    // printf("CREATE\n");
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    if((const char *)*((unsigned int *)f->esp+1)==NULL)
        {
            f->eax=-1;
            ExitStatus(-1);
        }
    bool ret=filesys_create((const char *)*((unsigned int *)f->esp+1),*((unsigned int *)f->esp+2));
    f->eax=ret;
}
void OPEN(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    const char *FileName=(char *)*((int *)f->esp+1);
    if(FileName==NULL)
    {
        f->eax=-1;
        ExitStatus(-1);
    }
    struct file_node *fn=(struct file_node *)malloc(sizeof(struct file_node));

    fn->f=filesys_open(FileName);
    if(fn->f==NULL)//
        fn->fd=-1;
    else
        fn->fd=++cur->maxfd;

    f->eax=fn->fd;
    if(fn->fd==-1)
        free(fn);
    else
    {
        list_push_back(&cur->file_list,&fn->elem);
    }
}
void CLOSE(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    int fd=*((int *)f->esp+1);
    f->eax=thread_close_file(cur,fd,false);

}
int CloseFile(struct thread *t,int fd,int bAll)
{
    struct list_elem *e,*p;
    if(bAll)
    {
        while(!list_empty(&t->file_list))
        {
            struct file_node *fn = list_entry (list_pop_front(&t->file_list), struct file_node, elem);
            file_close(fn->f);
             free(fn);
        }
        return 0;
    }

  for (e = list_begin (&t->file_list); e != list_end (&t->file_list);)
  {
       struct file_node *fn = list_entry (e, struct file_node, elem);
            if(fn->fd==fd)
            {
                list_remove(e);
                if(fd==t->maxfd)
                    t->maxfd--;
                file_close(fn->f);
               free(fn);

                return 0;
            }
  }

}

void READ(struct intr_frame *f)
{
    int *esp=(int *)f->esp;
    if(!is_user_vaddr(esp+3))
        ExitStatus(-1);
    int fd=*(esp+1);
    char *buffer=(char *)*(esp+2);
    unsigned size=*(esp+3);

    if(buffer==NULL||!is_user_vaddr(buffer+size))
    {
        f->eax=-1;
        ExitStatus(-1);
    }

    struct thread *cur=thread_current();
    struct file_node *fn=NULL;
    unsigned int i;
    if(fd==STDIN_FILENO)               //从标准输入设备读
    {
        for(i=0;i<size;i++)
            buffer[i]=input_getc();

    }
    else                            //从文件读
    {
        fn=GetFile(cur,fd);         //获取文件指针
        if(fn==NULL)
        {
            f->eax=-1;
            return;
        }
        f->eax=file_read(fn->f,buffer,size);
    }
}

struct file_node *GetFile(struct thread *t,int fd)   //依据文件句柄从进程打开文件表中找到文件指针
{
    struct list_elem *e;

    for (e = list_begin (&t->file_list); e != list_end (&t->file_list);e=list_next (e))
    {
       struct file_node *fn = list_entry (e, struct file_node, elem);
       if(fn->fd==fd)
            return fn;
    }
    return NULL;
}

void FILESIZE(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    struct thread *cur=thread_current();
    int fd=*((int *)f->esp+1);
    struct file_node *fn=GetFile(cur,fd);
    if(fn==NULL)
    {
        f->eax=-1;
        return;
    }
    f->eax=file_length (fn->f);
}
void EXEC(struct intr_frame *f)
{
     if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    const char *file=(char*)*((int *)f->esp+1);
    tid_t tid=-1;
    if(file==NULL)
    {
        f->eax=-1;
        return;
    }
    char *newfile=(char *)malloc(sizeof(char)*(strlen(file)+1));

    memcpy(newfile,file,strlen(file)+1);
    tid=process_execute (newfile);
    struct thread *t=get_thread_by_tid(tid);
    sema_down(&t->SemaWaitSuccess);
    f->eax=t->tid;
    t->father->sons++;
    free(newfile);
    sema_up(&t->SemaWaitSuccess);
}

void WAIT(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    tid_t tid=*((int *)f->esp+1);
    if(tid!=-1)
    {
        f->eax=process_wait(tid);

    }
    else
        f->eax=-1;
}

void SEEK(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+2))
      ExitStatus(-1);

    int fd=*((int *)f->esp+1);
    unsigned int pos=*((unsigned int *)f->esp+2);
    struct file_node *fl=GetFile(thread_current(),fd);
    file_seek (fl->f,pos);
}

void REMOVE(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    char *fl=(char *)*((int *)f->esp+1);
    f->eax=filesys_remove (fl);
}

void TELL(struct intr_frame *f)
{
    if(!is_user_vaddr(((int *)f->esp)+1))
      ExitStatus(-1);
    int fd=*((int *)f->esp+1);
     struct file_node *fl=GetFile(thread_current(),fd);
     if(fl==NULL||fl->f==NULL)
     {
         f->eax=-1;
         return;
     }
    f->eax=file_tell (fl->f);
}
void HALT(struct intr_frame *f)
{
    shutdown_power_off();
    f->eax=0;
}