#include <stdio.h>
#include <stdlib.h>

typedef struct ObjectHeader {
    size_t _objectSize;             // Real size of the object.
    int _leftObjectSize;            // Real size of the previous contiguous chunk in memory
    int _allocated;                 // 1 = yes, 0 = no.
    struct ObjectHeader *_listNext; // Points to the next object in the freelist (if free).
    struct ObjectHeader *_listPrev; // Points to the previous object.
} ObjectHeader;

int main() {
	ObjectHeader h;
	/*
	 //ObjectHeader is 32 bytes
	printf("ObjectHeader: %d\n", (int)sizeof(h));
	printf("_objectSize: %d\n", (int)sizeof(h._objectSize));
	printf("_leftObjectSize: %d\n", (int)sizeof(h._leftObjectSize));
	printf("_allocated: %d\n", (int)sizeof(h._allocated));
	printf("_listNext: %d\n", (int)sizeof(h._listNext));
	printf("_listPrev: %d\n", (int)sizeof(h._listPrev));
	*/
	int size = 1;
	size_t roundedSize = (size + sizeof(ObjectHeader) + 7) & ~7;
	printf("%d\n", (int)roundedSize);
	return 0;
}
