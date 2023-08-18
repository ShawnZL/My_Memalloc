#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#define Page 4096
union header {
    struct {
        size_t size;
        unsigned is_free;
        union header *next;
    }s;
};

typedef union header header_t;

header_t *head = NULL, *tail = NULL;
std::mutex mtx;

header_t *get_free_block(size_t size) {
    header_t *curr = head;
    while(curr) {
        if (curr->s.is_free && curr->s.size >= size) {
            return curr;
        }
        curr = curr->s.next;
    }
    return NULL;
}


void free(void* block) {
    // 给出要释放内存的起始地址
    header *header, *tmp;
    void *programbreak;

    if (!block) {
        return;
    }
    mtx.lock();
    header = (header_t*)block - 1;

    programbreak = sbrk(0);
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
        if (munmap(header, header->s.size + sizeof(header_t)) == -1) {
            std::cerr << "munmap failed" << std::endl;
        }
        mtx.unlock();
        return;
    }
    header->s.is_free = 1;
    mtx.unlock();
}

void *malloc(size_t size) {
    size_t total_size;
    void* block;
    header_t *header;
    if (!size) return NULL;
    mtx.lock();
    header = get_free_block(size);
    if (header) {
        header->s.is_free = 0;
        mtx.unlock();
        return (void*)(header + 1);
    }
    total_size = sizeof(header_t) + size;
    block = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED) {
        std::cerr << "mmap failed" << std::endl;
        return nullptr;
    }
    header = static_cast<header_t*>(block);
    header->s.size = size;
    header->s.is_free = 0;
    header->s.next = NULL;
    if (!head)
        head = header;
    if (tail)
        tail->s.next = header;
    tail = header;
    mtx.unlock();
    return (void*)(header + 1);
}

int main() {
    void* ptr1 = malloc(1024);  // Allocate memory
    void* ptr2 = malloc(2048);

    // Do something with allocated memory...

    free(ptr1);  // Free allocated memory
    free(ptr2);

    std::cout << "Memory allocation and deallocation successful!" << std::endl;
    return 0;
}
