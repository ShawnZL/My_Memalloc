# 文件

这个是用来了解程序运行空间

_c是C文件形式

无后缀的是C++版本，同时使用了**mmap与munmap。**

❌**继续理解sbrk/brk mmap**

# 定义

```c++
struct header_t {
	size_t size;
	unsigned is_free;
};
```

记录分配的memory

这样计算总体的大小为`total_size = header_size + size`。and call `sbrk(total_size)`. 

但是我们需要知道下一个分配的header，所以就需要添加指针*next

```c++
struct header_t {
	size_t size;
	unsigned is_free;
	struct header_t *next;
};
// 所以总体定义就可以写成下边
typedef char ALIGN[16];

union header {
	struct {
		size_t size;
		unsigned is_free;
		union header *next;
	} s;
	ALIGN stub; // 用来实现内存对齐
};
typedef union header header_t;
```

## malloc

```c
header_t *get_free_block(size_t size) {
    header_t *curr = head;
    while (curr) {
        if (curr->s.is_free && curr->s.size >= size) {
            return curr;
        }
        curr = curr->s.next;
    }
    return NULL;
}

void *malloc(size_t size) {
    size_t total_size;
    void *block;
    header_t *header;
    if (!size) return NULL;
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    if (header) {
        header->s.is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);
    }
    total_size = sizeof(header_t) + size;
    block = sbrk(total_size);
    if (block == (void*)-1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }
    header = block;
    header->s.is_free = 0;
    header->s.size = size;
    header->s.next = NULL;
    if (!head)
        head = header;
    if (tail)
        tail->s.next = header;
    tail = header;
    pthread_mutex_unlock(&global_malloc_lock);
    return (void*)(header + 1);
}

```

## free

```c
void free(void *block)
{
    // 释放内存的起始位置 block，相当于block之后还有一些内存空间，我们需要释放，然后block作为结尾。
    header_t *header, *tmp;
    /* program break is the end of the process's data segment */
    void *programbreak;

    if (!block)
        return;
    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t*)block - 1; // 通过 (header_t*)block - 1，将内存块指针 block 转换为指向内存块头部的指针，并向前偏移一个 header_t 的大小（即 sizeof(header_t)）。
    /* sbrk(0) gives the current program break address */
    programbreak = sbrk(0);

    /*
       Check if the block to be freed is the last one in the
       linked list. If it is, then we could shrink the size of the
       heap and release memory to OS. Else, we will keep the block
       but mark it as free.
     */
    if ((char*)block + header->s.size == programbreak) {
      // 得到当前的块正好是一个内存空间地址
        if (head == tail) {
            head = tail = NULL;
        } else {
            tmp = head;
            while (tmp) {
                if(tmp->s.next == tail) {
                    tmp->s.next = NULL;
                    tail = tmp;
                }
                tmp = tmp->s.next;
            }
        }
        /*
           sbrk() with a negative argument decrements the program break.
           So memory is released by the program to OS.
        */
        sbrk(0 - header->s.size - sizeof(header_t));
        /* Note: This lock does not really assure thread
           safety, because sbrk() itself is not really
           thread safe. Suppose there occurs a foregin sbrk(N)
           after we find the program break and before we decrement
		   it, then we end up realeasing the memory obtained by
		   the foreign sbrk().
		*/
        pthread_mutex_unlock(&global_malloc_lock);
        return;
    }
    header->s.is_free = 1;
    pthread_mutex_unlock(&global_malloc_lock);
}
```

# 函数定义

```c
//函数原型：
#include<unistd.h>
int brk(void * addr); // 成功返回0，失败返回-1
void * sbrk(intptr_t increment); // sbrk(0) current address of program break.
// 失败返回(void*) -1
/*
brk()和sbrk()改变程序间断点的位置。程序间断点就是程序数据段的结尾。（程序间断点是为初始化数据段的起始位置）.通过增加程序间断点进程可以更有效的申请内存 。当addr参数合理、系统有足够的内存并且不超过最大值时brk()函数将数据段结尾设置为addr,即间断点设置为addr。sbrk()将程序数据空间增加increment字节。当increment为0时则返回程序间断点的当前位置。
*/
```

**brk()与brk()均可分配回收兼职，但是我们一般用sbrk()分配内存，而用brk()回收内存**

```c
int err = brk(old);
// 或者brk(p);效果与sbrk(-MAX*MAX);是一样的，但brk()更方便与清晰明了。
if(-1 == err){
    perror("brk");
    exit(EXIT_FAILURE);
}
```

但是目前更喜欢使用`mmap()`

```c
//函数原型：
#incldue<sys/mman.h>
void * mmap(void * addr, size_t length,int prot,int flags,int fd,off_t offset);
```

**参数：**

**（1）、addr：**
起始地址，置零让系统自行选择并返回即可。

**（2）、length：**
长度，不够一页会自动凑够一页的整数倍，我们可以宏定义#define MIN_LENGTH_MMAP 4096为一页大小。

**（3）、prot：**
读写操作权限，PROT_READ可读、PROT_WRITE可写、PROT_EXEC可执行、PROT_NONE映射区域不能读取。（注意PROT_XXXXX与文件本身的权限不冲突，如果在程序中不设定任何权限，即使本身存在读写权限，该进程也不能对其操作）。

**（4）、flags常用标志：**
① **MAP_SHARED**【share this mapping】、**MAP_PRIVATE**【Create a private copy-on-write mapping】
MAP_SHARED只能设置文件共享，不能地址共享，即使设置了共享，对于两个进程来说，也不会生效。而MAP_PRIVATE则对于文件与内存都可以设置为私有。
② **MAP_ANON**【Deprecated】、**MAP_ANONYMOUS**：匿名映射，如果映射地址需要加该参数，如果不加默认映射文件。MAP_ANON已经过时，只需使用MAP_ANONYMOUS即可。
**（5）、fd：**文件描述符。
**（6）、offset：**文件描述符偏移量。fd和offset对于一般性内存分配来说设置为0即可）

munmap函数：解除映射关系

```c
// addr为mmap函数返回接收的地址，length为请求分配的长度。
int munmap(void * addr, size_t length);
```

