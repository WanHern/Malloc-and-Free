// COMP1521 19t2 ... Assignment 2: heap management system

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "myHeap.h"

/** minimum total space for heap */
#define MIN_HEAP 4096
/** minimum amount of space for a free Chunk (excludes Header) */
#define MIN_CHUNK 32


#define ALLOC 0x55555555
#define FREE  0xAAAAAAAA

/// Types:

typedef unsigned int  uint;
typedef unsigned char byte;

typedef uintptr_t     addr; // an address as a numeric type

/** The header for a chunk. */
typedef struct header {
	uint status;    /**< the chunk's status -- ALLOC or FREE */
	uint size;      /**< number of bytes, including header */
	byte data[];    /**< the chunk's data -- not interesting to us */
} header;

/** The heap's state */
struct heap {
	void  *heapMem;     /**< space allocated for Heap */
	uint   heapSize;    /**< number of bytes in heapMem */
	void **freeList;    /**< array of pointers to free chunks */
	uint   freeElems;   /**< number of elements in freeList[] */
	uint   nFree;       /**< number of free chunks */
};


/// Variables:

/** The heap proper. */
static struct heap Heap;


/// Functions:

static addr heapMaxAddr (void);

static int roundMultiple4 (int);
static void *smallestFreeChunk(int);
static void deleteFromList(void *);
static void insertToList(void *);
static int search(void *, int, int, void **);
static void mergeFreeChunks();
static void mergeTwoChunks(void *, void *);

/** Initialise the Heap. */
int initHeap (int size)
{
	// Round up size to a multiple of 4
	size = roundMultiple4(size);

	// Check that if size < MIN_HEAP, size = MIN_HEAP
	if (size < MIN_HEAP) {
		size = MIN_HEAP;
	}

	// Calloc for initial chunk
	// Initialise status and size
	header *chunk = calloc(size, sizeof(*chunk));
	if (chunk == NULL) {
		return -1;
	}
	chunk->status = FREE;
	chunk->size = size;

	// Heap.heapMem = pointer to first byte of calloced space
	Heap.heapMem = chunk;

	// Heap.heapSize = newly malloced size
	Heap.heapSize = size;

	// Heap.nFree contains the current number of elements in Heap.freeList
	Heap.nFree = 1;

	// Heap.freeElems =  the maximum number of chunks in the Heap
	// That will be the maximum number of elements in Heap.freeList
	Heap.freeElems = size/MIN_CHUNK;

	// Malloc for Heap.freeList
	// Set first element in Heap.freeList to chunk
	Heap.freeList = malloc(Heap.freeElems*sizeof(void *));
	if (Heap.freeList == NULL) {
		return -1;
	}
	Heap.freeList[0] = chunk;

	// Success
	return 0;
}

/** Release resources associated with the heap. */
void freeHeap (void)
{
	free (Heap.heapMem);
	free (Heap.freeList);
}

/** Allocate a chunk of memory large enough to store `size' bytes. */
void *myMalloc (int size)
{
	// Size check
	if (size < 1) {
		return NULL;
	}

	// Round up size to multiple of 4
	size = roundMultiple4(size);

	// Find header of smallest free chunk
	header *smallestChunk = smallestFreeChunk(size);
	// Size of smallest free chunk
	int smallest_size = smallestChunk->size;

	// If smallest_size - size - headerSize - MIN_CHUNK < 0
	// That means that there is not enough space to split into two chunks
	// So we allocate the whole chunk
	if (smallest_size - size - (int)sizeof(header) - MIN_CHUNK < 0) {
		smallestChunk->status = ALLOC;
		smallestChunk->size = smallest_size;
		deleteFromList(smallestChunk);
	}
	// Else we allocate the lower chunk, upper chunk becomes new free chunk
	else {
		if (smallestChunk->status == FREE) {
			deleteFromList(smallestChunk);
		}
		smallestChunk->status = ALLOC;
		smallestChunk->size = size+sizeof(header);
		// Upper chunk is a new free chunk
		void *newFree = (void *)smallestChunk + smallestChunk->size;
		header *chunk = (header *)newFree;
		chunk->status = FREE;
		chunk->size = smallest_size - smallestChunk->size;
		insertToList(chunk);
	}
	smallestChunk++;

	return smallestChunk;
}

/** Deallocate a chunk of memory. */
void myFree (void *obj)
{
	header *chunk = (header *)obj;

	// If unable to get a valid chunk
	// Print to stderr and exit
	if (chunk == NULL) {
		fprintf(stderr, "Attempt to free unallocated chunk\n");
		exit(1);
	}

	chunk--;
	// If the argument is an address somewhere in the middle of an allocated chunk
	// Print to stderr and exit
	if(chunk->status != ALLOC){
		fprintf(stderr, "Attempt to free unallocated chunk\n");
		exit(1);
	}
	chunk->status = FREE;
	insertToList(chunk);
	mergeFreeChunks();
}

/** Return the first address beyond the range of the heap. */
static addr heapMaxAddr (void)
{
	return (addr) Heap.heapMem + Heap.heapSize;
}

/** Convert a pointer to an offset in the heap. */
int heapOffset (void *obj)
{
	addr objAddr = (addr) obj;
	addr heapMin = (addr) Heap.heapMem;
	addr heapMax =        heapMaxAddr ();
	if (obj == NULL || !(heapMin <= objAddr && objAddr < heapMax))
		return -1;
	else
		return (int) (objAddr - heapMin);
}

/** Dump the contents of the heap (for testing/debugging). */
void dumpHeap (void)
{
	int onRow = 0;

	// We iterate over the heap, chunk by chunk; we assume that the
	// first chunk is at the first location in the heap, and move along
	// by the size the chunk claims to be.
	addr curr = (addr) Heap.heapMem;
	while (curr < heapMaxAddr ()) {
		header *chunk = (header *) curr;

		char stat;
		switch (chunk->status) {
		case FREE:  stat = 'F'; break;
		case ALLOC: stat = 'A'; break;
		default:
			fprintf (
				stderr,
				"myHeap: corrupted heap: chunk status %08x\n",
				chunk->status
			);
			exit (1);
		}

		printf (
			"+%05d (%c,%5d)%c",
			heapOffset ((void *) curr),
			stat, chunk->size,
			(++onRow % 5 == 0) ? '\n' : ' '
		);

		curr += chunk->size;
	}

	if (onRow % 5 > 0)
		printf ("\n");
}

//////////////////////
// Helper functions //
//////////////////////

// Rounds up size to nearest multiple of 4
static int roundMultiple4 (int size) {
	int remainder = size % 4;
	if (remainder == 0) {
		return size;
	}
	else {
		return size + 4 - remainder;
	}

}

// Finds smallest free chunk >= N + headerSize
static void *smallestFreeChunk(int size) {

	int found = 0;
	void *returnAddress = NULL;
	int smallest_size = Heap.heapSize;

	// Loop through Heap.freeList and look at each chunk
	// To find the smallest one that is >= size + sizeof(header *)
	for (int i = 0; i < Heap.nFree; i++) {
		void *curr_address = Heap.freeList[i];
		header *curr_chunk = (header *)curr_address;
		if (curr_chunk->size >= size + sizeof(header *) && curr_chunk->size <= smallest_size && curr_chunk->status == FREE) {
			smallest_size = curr_chunk->size;
			returnAddress = curr_address;
			found = 1;
		}
	}
	
	// Did not find a suitable chunk (i.e no free space)
	// I don't think its in the spec, but it might be good to exit when there isn't
	// enough space to myMalloc() the full size
	if (found == 0) {
		fprintf(stderr,"Error: Not enough space to allocate a block of %d bytes\n", size);
		exit(1);
	}

	// Return address of smallest free chunk
	return returnAddress;
}

// Deletes an element from Heap.freeList
static void deleteFromList(void *addr) {

	// Index of element to be deleted
	int index = search(addr, 0, Heap.nFree, Heap.freeList);

	// Element not found
	if (index == -1){
		printf("Error: Element not found\n");
		return;
	}

	// Delete element
	for (int i = index; i < Heap.nFree - 1; i++){
		Heap.freeList[i] = Heap.freeList[i+1];
	}

	Heap.nFree--;
}

// Inserts an element into Heap.freeList
static void insertToList(void * addr) {

	int i;
	for (i = Heap.nFree-1; (i >= 0 && Heap.freeList[i] > addr); i--) {
		Heap.freeList[i+1] = Heap.freeList[i];
	}

	Heap.freeList[i+1] = addr;

	Heap.nFree++;
}

// Binary search to find index of an address stored in Heap.freeList
static int search(void *address, int lo, int hi, void **freeList) {

	// Index could not be found
	if (hi < lo) {
		return -1;
	}

	int mid = (lo + hi)/2;
	// Index found
	if (address == Heap.freeList[mid]) {
		return mid;
	}

	// Recursive cases
	if (address > Heap.freeList[mid]) {
		return search(address, mid+1, hi, Heap.freeList);
	}

	return search(address, lo, mid-1, Heap.freeList);
}

// Scans Heap.freeList to merge all free adjacent chunks
static void mergeFreeChunks() {

	for (int i = 0; i < Heap.nFree; i++) {
		// Find the address of curr_chunk we are on
		void *curr_address = Heap.freeList[i];
		header *curr_chunk = (header *)curr_address;
		int curr_size = curr_chunk->size;

		// If not at the end of the list
		if (i < Heap.nFree-1) {
			// Find the address of next_chunk
			// And the size between start of curr_chunk and start of next_chunk
			void *next_address = Heap.freeList[i+1];
			header *next_chunk = (header *)next_address;
			int sizeBetweenChunks = (int)(next_address - curr_address);
			// If size between chunks == size of curr_chunk and they are both free,
			// Then we know they are adjacent free chunks and we may merge
			if (sizeBetweenChunks == curr_size && curr_chunk->status == FREE && next_chunk->status == FREE) {
				mergeTwoChunks(Heap.freeList[i], Heap.freeList[i+1]);
				deleteFromList(Heap.freeList[i+1]);
				i--;
			}
		}
	}
}

// Simply merges two chunks
static void mergeTwoChunks(void *a1, void *a2) {
	header *c1 = (header *) a1;
	header *c2 = (header *) a2;
	c1->size += c2->size;
}

///////////////////////////////////////////////////////////////////////////////////////