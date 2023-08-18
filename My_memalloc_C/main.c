#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

typedef char ALIGN[16];

union header {
    struct {
        size_t size;
        unsigned is_free; // 0 not free    1 free
        union header* next;
    }s;
    // force the header to be aligned to 16 bytes
    ALIGN stub;
};

typedef union header header_t;

header_t *head = NULL, *tail = NULL;
pthread_mutex_t global_malloc_lock;

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


void free(void *block)
{
    // 释放内存的起始位置 block
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

void *realloc(void *block, size_t size)
{
    header_t *header;
    void *ret;
    if (!block || !size)
        return malloc(size);
    header = (header_t*)block - 1;
    if (header->s.size >= size)
        return block;
    ret = malloc(size);
    if (ret) {
        /* Relocate contents to the new bigger block */
        memcpy(ret, block, header->s.size);
        /* Free the old memory block */
        free(block);
    }
    return ret;
}

/* A debug function to print the entire link list */
void print_mem_list()
{
    header_t *curr = head;
    printf("head = %p, tail = %p \n", (void*)head, (void*)tail);
    while(curr) {
        printf("addr = %p, size = %zu, is_free=%u, next=%p\n",
               (void*)curr, curr->s.size, curr->s.is_free, (void*)curr->s.next);
        curr = curr->s.next;
    }
}

int main() {
    print_mem_list();
    return 0;
}
