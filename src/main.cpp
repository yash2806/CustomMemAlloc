#include<unistd.h>
#include<sys/syscall.h>
#include<iostream>

typedef char ALIGN[16];     //char - 1byte -> ALIGN[16] -> 16bytes

int main(){
    int* ptr = (int*)malloc(sizeof(int)*5);
    std::cout << ptr << std::endl;
}

//store size information to free up memory
//when a program request for memory-> total_size=header_size+size of prog
union header_t{
    struct{
        size_t size;
        bool is_free;
        union header_t* next;          //since we are unsure block of memory is contiguous so will form a linked list
    }s;
    ALIGN stub;
};

header_t *head, *tail;
pthread_mutex_t global_malloc_lock;     //global lock - before acquiring memory you have to acquire the lock, once acquired, you have to release the lock.


void *my_malloc(size_t size){
    size_t total_size;
    void *block;
    header_t* header;

    if(!size)
        return NULL;
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);
    if(header){
        //block acquired, release lock
        header->s.is_free = false;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header+1);
    }


    total_size = sizeof(header_t) + size;
    block = sbrk(total_size);
    if(block == (void*)-1){
        pthread_mutex_unlock(&global_malloc_lock);
        return nullptr;
    }
    header = (header_t*)block;
    header->s.size = size;
	header->s.is_free = 0;
	header->s.next = NULL;
	if (!head)
		head = header;
	if (tail)
		tail->s.next = header;
	tail = header;
	pthread_mutex_unlock(&global_malloc_lock);
	return (void*)(header + 1);
    return block;
}

header_t* get_free_block(size_t t){
    header_t* curr = head;
    while(curr){
        if(curr->s.is_free && curr->s.size > t){
            return curr;
        }
        curr = curr->s.next;
    }
    return nullptr;
}

//if block is at end of ll, release it, else just mark it as free, will try to reuse it later.
void my_free(void* block){
    header_t *header, *tmp;
	void *programbreak;

	if (!block)
		return;
	pthread_mutex_lock(&global_malloc_lock);
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
		sbrk(0 - sizeof(header_t) - header->s.size);
		pthread_mutex_unlock(&global_malloc_lock);
		return;
	}
	header->s.is_free = 1;
	pthread_mutex_unlock(&global_malloc_lock);
}


//check if existing header size if greater than size -> return block
//allocate memory -> copy mem with size and free the block.

void* my_realloc(void* block, size_t size){
    header_t* header;
    block = sbrk(size);
    void* ret;
    if(!block || !size)
        return my_malloc(size);
    header = (header_t*)block - 1;
    if(header->s.size >= size){
        return block;
    }
    ret = my_malloc(size);
    if(ret){
        memcpy(ret, block, header->s.size);
        free(block);
    }
    return ret;
}

void *my_calloc(size_t num, size_t nsize)
{
	size_t size;
	void *block;
	if (!num || !nsize)
		return NULL;
	size = num * nsize;
	
	if (nsize != size / num)
		return NULL;
	block = malloc(size);
	if (!block)
		return NULL;
	memset(block, 0, size);
	return block;
}
