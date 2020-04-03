/*
 * budalloc is a space efficient buddy allocator that only holds the state
 * in a bitfield. For example to have 16 different levels of halving allocation accurancy,
 * it would require around 16kbytes. the state of each cell is described by 2 bits, which 
 * can be free (00), split (10) or full (11). Split means there is a subdivision of that space
 * further down the tree. The bitree is walked with recursive algorithms for freeing and allocing.
 * On freeing if succesfull the result is bubbled up the recursion with the hope that continuous 
 * address space will be merged. No addresses are held, there is no other state apart 
 * from the bittree. Also a small crude cli tool is provided to perform fake allocations, deallocs
 * and examine the allocator state. Please don't be that person that gives non page aligned space to 
 * the allocator. noone likes that person.
 *
 * 16/11/19 spiros thanasoulas <dsp@2f30.org>
 */

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* 
 * if you change this you need to also calculate the number of bytes 
 * for the bitfields using (TOTCELLSFORLVL/4)+1 
 */
#define TOTLVLS 4 
#define BITFIELDBYTES    4 /*for totlvs 4*/
#define TOTCELLSFORLVL(x, y) { int _x = 1; int _cnt = 0; (y) = 1; \
				 while(++_cnt < (x)) {_x<<=1; (y) += _x; }}
/*
 * for 16 levels we need 65535 cells, with 2 bits per cell we need 16384 bytes 
 * #define TOTLVLS 16
 * #define BITFIELDBYTES 16384
 */
#define SETBIT(A,k)      ((A)[((k)/8)] |= (1 << ((k)%8)))
#define CLEARBIT(A,k)    ((A)[((k)/8)] &= ~(1 << ((k)%8)))            
#define TESTBIT(A,k)     ((A)[((k)/8)] & (1 << ((k)%8)))
#define FREECELL(A,c)    (CLEARBIT((A),(2*(c)-1)) && CLEARBIT((A), (2*(c)-2))) /* 00 means free */
#define ALLOCSPLIT(A,c)  (SETBIT((A),(2*(c)-1)) && CLEARBIT((A), (2*(c)-2)))  /* 10 means split */
#define ALLOCCELL(A,c)   (SETBIT((A),(2*(c)-1)) && SETBIT((A), (2*(c)-2))) /* 11 means full */
#define ISFULL(A,c)      (TESTBIT((A),(2*(c)-1)) && TESTBIT((A), (2*(c)-2))) /* tests for 11 */
#define ISSPLIT(A,c)     (TESTBIT((A),(2*(c)-1)) && !TESTBIT((A), (2*(c)-2))) /* tests for 10 */
#define ISFREE(A,c)      (!TESTBIT((A),(2*(c)-1)) && !TESTBIT((A), (2*(c)-2))) /* tests for 00 */
#define LEFTCHILD(c)     (2 * (c))
#define RIGHTCHILD(c)    ((2 * (c)) + 1)

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0') 

const char separator[] = "----------------------------------------------------";

#ifdef DEBUG
#define DTREEPRINTF(l, f, v...) do { printf("%.*s"f, l, separator, v);} while(0)
#define DTREEPRINT(l, f) do { printf("%.*s"f, l, separator);} while(0)
#else
#define DTREEPRINTF(l, f, v...)
#define DTREEPRINT(l, f)
#endif

typedef struct buddy_allocator {
	void *memstart;
	size_t memsz;
	size_t inuse, unused, requested;
	unsigned char bits[BITFIELDBYTES];
} buddy_allocator_t;

buddy_allocator_t *
buddy_allocator_create(void *raw_mem, size_t memsz)
{
	buddy_allocator_t *ret = malloc(sizeof(buddy_allocator_t));
	if (ret == NULL) {
		printf("failed to allocate memory for buddy allocator\n");
	} else {
		ret->memstart = raw_mem;
		ret->memsz = memsz;
		ret->unused = memsz;
	}
	return ret;
}

void
buddy_allocator_destroy(buddy_allocator_t *balloc)
{
	if (balloc != NULL) {
		free(balloc);
	}
	return;
}


struct allocationInfo {
	bool success;
	size_t offset;
};

/* 
 * recursivelly go down the tree till you get to the correct level
 * and try to allocate there.  
 * if it returns false it means allocation failed, otherwise the offset
 * will be added to the base pointer.
 */
struct allocationInfo
allocRecurse(buddy_allocator_t *b, size_t hm, int lvl, long cell)
{
	struct allocationInfo ret, childret;
	size_t minAlloc, maxAlloc;
	ret.success = false;
	ret.offset = 0;
	childret.offset = 0;
	if (lvl > TOTLVLS || hm == 0) {
		DTREEPRINT(lvl, "terminating recursion return 0\n");
		ret.success = false;
		return ret;
	}
	maxAlloc = (b->memsz)/(1<<(lvl-1));
	minAlloc = (b->memsz)/((1<<(lvl-1))<<1);
	DTREEPRINTF(lvl, "lvl %d at cell:%ld max alloc sz:%zd  min alloc sz:%zd"
			 "want to alloc:%zd\n", lvl, cell, maxAlloc, minAlloc, hm);
	if ((minAlloc < hm  && hm <= maxAlloc) || 
	    (lvl == TOTLVLS && hm <= minAlloc)) { 
		/* this is the lvl where we should place it */
		DTREEPRINT(lvl, "want to alloc here\n");
		/* if we're free mark us alloced (11) */
		if (ISFULL(b->bits, cell)) {
			DTREEPRINT(lvl, " cell full.\n");
			ret.success = false;
			return ret;
		} else {
			ALLOCCELL(b->bits, cell);
			DTREEPRINT(lvl, " alloced the cell. returning success\n");
			ret.success = true;
			b->requested += hm;
			b->inuse += maxAlloc;
			b->unused -= maxAlloc;
			return ret ;
		}
	}
	childret = allocRecurse(b, hm, lvl+1, LEFTCHILD(cell));
	if (!childret.success) {
		DTREEPRINTF(lvl, "left failed %d going right\n", ret.success);
		childret = allocRecurse(b, hm, lvl+1, RIGHTCHILD(cell));
		if (!childret.success) {
			DTREEPRINTF(lvl, "right failed too %d\n", ret.success);
			return childret;
		}
		ALLOCSPLIT(b->bits, cell);
		/* 
 		 * if we just allocated a right child, 
		 * add the offset of the min alloc at his lvl 
		 */
		childret.offset += (b->memsz)/(1<<lvl);
		DTREEPRINTF(lvl, "offset is now at :%zd\n", childret.offset);
		return childret;
	} 
	ALLOCSPLIT(b->bits, cell);
	return childret;
}

/* just a placeholder with a boolean that could hold more info in the future */
struct freeInfo {
	bool success;
};

struct freeInfo
freeRecurse(buddy_allocator_t *b, size_t off, int lvl, long cell)
{
	struct freeInfo childret, ret;
	size_t minAlloc, maxAlloc;
	/* 
	 * if the offset is less than the minlvl recurse. 
 	 * if you go right, remove from the offset 
 	 * */
	if (lvl > TOTLVLS) {
		DTREEPRINT(lvl, "terminating recursion return false\n");
		ret.success = false;
		return ret;
	}
	minAlloc = (b->memsz)/(1<<(lvl-1)<<1);
	maxAlloc = (b->memsz)/(1<<(lvl-1));
	DTREEPRINTF(lvl, "lvl %d at cell:%ld max alloc sz:%zd  min alloc sz:%zd" 
			 " free offset:%zd\n", lvl, cell, maxAlloc, minAlloc, off);
	/* the modulo on the following makes sure we don't free inbetweens */
	if (off >= 0 && off <= maxAlloc && off > minAlloc && off % minAlloc == 0) { /* it could be on that level */
		if (ISFULL(b->bits, cell)) {
			DTREEPRINT(lvl, "freeing it.\n");
			FREECELL(b->bits, cell);
			ret.success = true;
			b->inuse -= maxAlloc;
			b->unused += maxAlloc;
			return ret;
		}
		/* else it could be deeper */
		DTREEPRINTF(lvl, "trying left with offset:%zd\n", off);
		childret = freeRecurse(b, off, lvl+1, LEFTCHILD(cell));
		if (!childret.success && off >= minAlloc) {
			DTREEPRINTF(lvl, "trying left failed, trying right"
					 " with offset:%zd\n", off-minAlloc);
			childret = freeRecurse(b, off-minAlloc, lvl+1, RIGHTCHILD(cell));
		}
		if (childret.success) {
			DTREEPRINT(lvl, "child successfully freed. trying to merge\n"); 
			if (ISFREE(b->bits, LEFTCHILD(cell)) && ISFREE(b->bits, RIGHTCHILD(cell))) {
				DTREEPRINT(lvl, "merged\n");
				FREECELL(b->bits, cell);
			}
		}
		return childret;
	}
	if (ISSPLIT(b->bits, cell) || ISFULL(b->bits, cell)) {
		/* check left */
		DTREEPRINTF(lvl, "offset is off. recursing left with offset:%zd\n", off);
		childret = freeRecurse(b, off, lvl+1, LEFTCHILD(cell));
		if (!childret.success && off >= minAlloc ) { 
			/* check right but only go if the offset is larger than the min alloc */
			DTREEPRINTF(lvl, "left failed, recursing right"
					 " with offset:%zd\n", off-minAlloc);
			childret = freeRecurse(b, off-minAlloc, lvl+1, RIGHTCHILD(cell));
		}
		if (childret.success) {
			DTREEPRINT(lvl, "child successfully freed. trying to merge\n"); 
			if (ISFREE(b->bits, LEFTCHILD(cell)) && ISFREE(b->bits, RIGHTCHILD(cell))) {
				DTREEPRINT(lvl, "merged\n");
				FREECELL(b->bits, cell);
			}
		}
		return childret;

	}
}

void *
buddy_allocator_alloc(buddy_allocator_t *b, size_t sz)
{
	struct allocationInfo ret;
	ret = allocRecurse(b, sz, 1, 1);
	if (ret.success) {
		return b->memstart + ret.offset;
	}
	return NULL;
}

void
buddy_allocator_free(buddy_allocator_t *b, void *ptr)
{
	struct freeInfo ret;
	if (ptr == NULL) {
		fprintf(stderr, "free on null requested\n");
		return;
	} else if (ptr < b->memstart || ptr >= b->memstart+b->memsz){
		fprintf(stderr, "free on range not belonging to the allocator");
		return;
	}
	freeRecurse(b, ptr-b->memstart, 1, 1);
} 

void
buddy_allocator_print(buddy_allocator_t *balloc)
{
	int bi;
	printf("start @%p\tsize:%zd\tinuse:%zd\trequessted:%zd\tfree:%zd\n",
		balloc->memstart, balloc->memsz, balloc->inuse, balloc->requested, balloc->unused);
	for (bi = BITFIELDBYTES-1; bi >= 0; bi--) {
		printf("["BYTE_TO_BINARY_PATTERN"],", 
			BYTE_TO_BINARY(balloc->bits[bi]));
	} 
	printf("\n");
		
}

void
repl(buddy_allocator_t *b)
{
	char cmd;
	long val;
	void *tofree;
	long cells;
	TOTCELLSFORLVL(TOTLVLS,cells);
	printf("compiled for %d levels which provides %ld allocation cells\n", TOTLVLS, cells);
	for (;;) {
		printf(">");
		scanf(" %c", &cmd);
		switch(cmd) {
		case 'Q':
			return;
		case 'A':
			printf("how many?\n>");
			scanf(" %zd", &val);
			printf("Alloc @ %p\n", buddy_allocator_alloc(b, val));
			break;
		case 'F':
			printf("which addr?\n>");
			scanf(" %p", &tofree);
			buddy_allocator_free(b, tofree);
			break;
		case 'P':
			buddy_allocator_print(b);
			break;
		default:
			printf("Q to quit, A to allocate, F to free, P to print\n"); 
			break;
		}
	}
	
}

void
usage()
{
	fprintf(stderr, "usage:budalloc bytenumber\n");
}

int
main(int argc, char *argv[])
{
	int res;
	long long in;
	char *ep;
	buddy_allocator_t *b;
	if (argc != 2) {
		usage();
		return EXIT_FAILURE;
	}
	in = strtoll(argv[1], &ep, 10);
        if (argv[1][0] == '\0' || *ep != '\0') {
		usage();
	}
        if (errno == ERANGE && (in == LLONG_MAX || in == LLONG_MIN)) {
		warnx("num range");
	}

	if (in > SIZE_MAX || in <= 0) {
		warnx("invalid arena size requested\n");
	}
	void *arena = malloc(in);
	if (arena == NULL) {
		warnx("failed to allocate %lld bytes\n", in);
	}
	b = buddy_allocator_create(arena, in);
	repl(b);
	buddy_allocator_destroy(b);
	free(arena);
	return EXIT_SUCCESS;
}
