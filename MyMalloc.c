/*
 * CS252: MyMalloc Project
 *
 * The current implementation gets memory from the OS
 * every time memory is requested and never frees memory.
 *
 * You will implement the allocator as indicated in the handout,
 * as well as the deallocator.
 *
 * You will also need to add the necessary locking mechanisms to
 * support multi-threaded programs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "MyMalloc.h"

static pthread_mutex_t mutex;

const int arenaSize = 2097152;
const int objectHeaderSize = sizeof(ObjectHeader); 

void increaseMallocCalls()  { _mallocCalls++; }

void increaseReallocCalls() { _reallocCalls++; }

void increaseCallocCalls()  { _callocCalls++; }

void increaseFreeCalls()    { _freeCalls++; }

extern void atExitHandlerInC()
{
    atExitHandler();
}

/* 
 * Initial setup of allocator. First chunk is retrieved from the OS,
 * and the fence posts and freeList are initialized.
 */
void initialize()
{
    // Environment var VERBOSE prints stats at end and turns on debugging
    // Default is on
    _verbose = 1;
    const char *envverbose = getenv("MALLOCVERBOSE");
    if (envverbose && !strcmp(envverbose, "NO")) {
        _verbose = 0;
    }

    pthread_mutex_init(&mutex, NULL);
    void *_mem = getMemoryFromOS(arenaSize);

    // In verbose mode register also printing statistics at exit
    atexit(atExitHandlerInC);
	
    //Establish FencePosts
    establishFencePosts(_mem);

    // Set up the sentinel as the start of the freeList
    _freeList = &_freeListSentinel;

    // Initialize the list to point to the _mem
    char *temp = (char *)_mem + objectHeaderSize;
    ObjectHeader *currentHeader = (ObjectHeader *)temp;
    currentHeader->_objectSize = arenaSize - (2*objectHeaderSize); // ~2MB
    currentHeader->_leftObjectSize = 0;
    currentHeader->_allocated = 0;
    currentHeader->_listNext = _freeList;
    currentHeader->_listPrev = _freeList;
    _freeList->_listNext = currentHeader;
    _freeList->_listPrev = currentHeader;

    // Set the start of the allocated memory
    _memStart = (char *)currentHeader;

    _initialized = 1;
}

void establishFencePosts(char * _mem) {

    ObjectHeader * fencePostHead = (ObjectHeader *)_mem;
    fencePostHead->_allocated = 1;
    fencePostHead->_objectSize = 0;
    fencePostHead->_listNext = fencePostHead->_listPrev = NULL;

    char *temp = (char *)_mem + arenaSize - objectHeaderSize;
    ObjectHeader * fencePostFoot = (ObjectHeader *)temp;
    fencePostFoot->_allocated = 1;
    fencePostFoot->_objectSize = 0;
    fencePostFoot->_listNext = fencePostFoot->_listPrev = NULL;

}

/* 
 * TODO: In allocateObject you will handle retrieving new memory for the malloc
 * request. The current implementation simply pulls from the OS for every
 * request.
 *
 * @param: amount of memory requested
 * @return: pointer to start of useable memory
 */
void * allocateObject(size_t size)
{
    // Make sure that allocator is initialized
    if (!_initialized)
        initialize();

    /* Add the ObjectHeader to the size and round the total size up to a 
     * multiple of 8 bytes for alignment.
     */
    size_t roundedSize = (size + objectHeaderSize + 7) & ~7;
    
    //TEMP make sure request size is not too large
    if (roundedSize >= arenaSize - 4*objectHeaderSize - 8) return NULL;


    //find the first block large enough to roundedSize, returns if found
    //else allocate a new block below loop
    ObjectHeader *curr = _freeList->_listNext;
    for (; curr != _freeList; curr = curr->_listNext) {
	int currSizeOffset = curr->_objectSize - roundedSize;
	//if block large enough to be split (enough for obj header plus 8 bytes)
	if (!curr->_allocated && currSizeOffset > 0) {
		if (currSizeOffset > (objectHeaderSize + 7)) {
			//create new header to split block and set attrib
			ObjectHeader *new = (ObjectHeader *)((char *)curr + currSizeOffset); //highest memory is returned
			new->_objectSize = roundedSize;
			new->_leftObjectSize = currSizeOffset;
			new->_allocated = 1;
			new->_listNext = new->_listPrev = NULL;

			//update curr object size
			curr->_objectSize = currSizeOffset;
    			((ObjectHeader *)((char *)new + new->_objectSize))->_leftObjectSize = new->_objectSize;
			
        		pthread_mutex_unlock(&mutex);
			return (void *)((char *)new + objectHeaderSize);
		} else {
			//if block not large enough to split then
			//remove curr from list and return it
			curr->_listPrev->_listNext = curr->_listNext;
			curr->_listNext->_listPrev = curr->_listPrev;
			curr->_listNext = curr->_listPrev = NULL;
			curr->_allocated = 1;
        		
			pthread_mutex_unlock(&mutex);
			return (void *)((char *)curr + objectHeaderSize);
		}
	}
    }
    
    //Allocate a new block of 2MB
    void *_mem = getMemoryFromOS(arenaSize); 
    
    // Establish fence posts
    establishFencePosts(_mem);

    // Create new headers
    ObjectHeader *o = (ObjectHeader *)((char*)_mem + objectHeaderSize); //skip dummy header
    ObjectHeader *new = (ObjectHeader *)((char*)_mem + arenaSize - roundedSize - objectHeaderSize);
    
    // set attrib for o
    o->_objectSize = (arenaSize - roundedSize - 2*objectHeaderSize);
    o->_leftObjectSize = 0;
    o->_allocated = 0;
    o->_listNext = _freeList->_listNext;
    o->_listPrev = _freeList;

    // set attrib for new
    new->_objectSize = roundedSize;
    new->_leftObjectSize = (arenaSize - roundedSize - 2*objectHeaderSize);
    new->_allocated = 1;
    new->_listNext = new->_listPrev = 0;
    
    // include o in freelist
    _freeList->_listNext->_listPrev = o;
    
    // update _freeList back pointer
    _freeList->_listNext = o;
    
    pthread_mutex_unlock(&mutex);

    // Return a pointer to useable memory
    return (void *)((char *)new + objectHeaderSize);
}

/* 
 * TODO: In freeObject you will implement returning memory back to the free
 * list, and coalescing the block with surrounding free blocks if possible.
 *
 * @param: pointer to the beginning of the block to be returned
 * Note: ptr points to beginning of useable memory, not the block's header
 */
void freeObject(void *ptr)
{
    ObjectHeader *ptrHeader = (ObjectHeader *)((char *)ptr - objectHeaderSize);
    ObjectHeader *leftHeader = (ObjectHeader *)((char *)ptrHeader - ptrHeader->_leftObjectSize);
    ObjectHeader *rightHeader = (ObjectHeader *)((char *)ptrHeader +  ptrHeader->_objectSize);

    //Check if adj blocks are free else add ptr to the front of _freeList
    //Note: can seperate checks as in the case both
    //are free then they will point to eachother
    if (leftHeader->_allocated == 0 && rightHeader->_allocated == 0) {
    	//update left header size
	int newSize =  ptrHeader->_objectSize + rightHeader->_objectSize;
    	leftHeader->_objectSize += newSize;
	
	//remove right header from _freeList
	rightHeader->_listNext->_listPrev = rightHeader->_listPrev;
	rightHeader->_listPrev->_listNext = rightHeader->_listNext;
	rightHeader->_listNext = rightHeader->_listPrev = NULL;
	
	//update block to right of rightHeader _leftObjectSize
    	((ObjectHeader *)((char *)rightHeader + rightHeader->_objectSize))->_leftObjectSize = newSize;
    } else if (leftHeader->_allocated == 0) {
    	//update objectSize
    	int newSize = leftHeader->_objectSize + ptrHeader->_objectSize;
	leftHeader->_objectSize = newSize;
	
	//update rightHeader _leftObjectSize
    	rightHeader->_leftObjectSize = newSize;
    } else if (rightHeader->_allocated == 0) {
    	//update objectSize
    	int newSize = rightHeader->_objectSize + ptrHeader->_objectSize;
	ptrHeader->_objectSize = newSize;
	
	//update pointers
	ptrHeader->_listNext = rightHeader->_listNext;
	ptrHeader->_listPrev = rightHeader->_listPrev;

	//update block right of rightHeader _leftObjectSize
	((ObjectHeader *)((char *)rightHeader + rightHeader->_objectSize))->_leftObjectSize = newSize;
	ptrHeader->_allocated = 0;
    } else {
    	ptrHeader->_listNext = _freeList->_listNext;
	ptrHeader->_listPrev = _freeList;
	_freeList->_listNext->_listPrev = ptrHeader;
	_freeList->_listNext = ptrHeader;
	ptrHeader->_allocated = 0;
    }
    pthread_mutex_unlock(&mutex);
    return;
}

/* 
 * Prints the current state of the heap.
 */
void print()
{
    printf("\n-------------------\n");

    printf("HeapSize:\t%zd bytes\n", _heapSize );
    printf("# mallocs:\t%d\n", _mallocCalls );
    printf("# reallocs:\t%d\n", _reallocCalls );
    printf("# callocs:\t%d\n", _callocCalls );
    printf("# frees:\t%d\n", _freeCalls );

    printf("\n-------------------\n");
}

/* 
 * Prints the current state of the freeList
 */
void print_list() {
    printf("FreeList: ");
    if (!_initialized) 
        initialize();

    ObjectHeader * ptr = _freeList->_listNext;

    while (ptr != _freeList) {
        long offset = (long)ptr - (long)_memStart;
        printf("[offset:%ld,size:%zd]", offset, ptr->_objectSize);
        ptr = ptr->_listNext;
        if (ptr != NULL)
            printf("->");
    }
    printf("\n");
}

/* 
 * This function employs the actual system call, sbrk, that retrieves memory
 * from the OS.
 *
 * @param: the chunk size that is requested from the OS
 * @return: pointer to the beginning of the chunk retrieved from the OS
 */
void * getMemoryFromOS(size_t size)
{
    _heapSize += size;

    // Use sbrk() to get memory from OS
    void *_mem = sbrk(size);

    // if the list hasn't been initialized, initialize memStart to mem
    if (!_initialized)
        _memStart = _mem;

    return _mem;
}

void atExitHandler()
{
    // Print statistics when exit
    if (_verbose)
        print();
}

/*
 * C interface
 */

extern void * malloc(size_t size)
{
    pthread_mutex_lock(&mutex);
    increaseMallocCalls();

    return allocateObject(size);
}

extern void free(void *ptr)
{
    pthread_mutex_lock(&mutex);
    increaseFreeCalls();

    if (ptr == 0) {
        // No object to free
        pthread_mutex_unlock(&mutex);
        return;
    }

    freeObject(ptr);
}

extern void * realloc(void *ptr, size_t size)
{
    pthread_mutex_lock(&mutex);
    increaseReallocCalls();

    // Allocate new object
    void *newptr = allocateObject(size);

    // Copy old object only if ptr != 0
    if (ptr != 0) {

        // copy only the minimum number of bytes
        ObjectHeader* hdr = (ObjectHeader *)((char *) ptr - objectHeaderSize);
        size_t sizeToCopy =  hdr->_objectSize;
        if (sizeToCopy > size)
            sizeToCopy = size;

        memcpy(newptr, ptr, sizeToCopy);

        //Free old object
        freeObject(ptr);
    }

    return newptr;
}

extern void * calloc(size_t nelem, size_t elsize)
{
    pthread_mutex_lock(&mutex);
    increaseCallocCalls();

    // calloc allocates and initializes
    size_t size = nelem *elsize;

    void *ptr = allocateObject(size);

    if (ptr) {
        // No error; initialize chunk with 0s
        memset(ptr, 0, size);
    }

    return ptr;
}

