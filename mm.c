/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*기본 상수와 매크로*/
#define WSIZE   4 // word size
#define DSIZE   8 // double word size
#define CHUNKSIZE   (1<<12) // heap을 확장할 때 확장할 최소 크기

#define MAX(x, y)   ((x) > (y) ? (x) : (y)) // max 값 반환

/*Pack a size and allocated bit into a word*/
#define PACK(size, alloc)   ((size) | (alloc)) // header에 들어갈 값

/* 포인터 p가 가르키는 워드의 값을 읽거나, p가 가르키는 워드에 값을 적는 매크로 */
#define GET(p)  (*(unsigned int *)(p)) // p는 보통 void *로 선언 되어 역참조 불가, 주소값에서 값 읽어옴
#define PUT(p, val) (*(unsigned int *) (p) = (val)) // 주소값에다 값을 씀

/* header 혹은 footer의 값인 size or allocated 여부를 가져오는 매크로*/
#define GET_SIZE(p) (GET(p) & ~0x7) // 헤더에서 사이즈 값만 가져오기 위해 하위 3비트를 0으로 만드는 비트연산 진행
#define GET_ALLOC(p)    (GET(p) & 0x1) //할당 비트의 값을 읽어오기 위한 비트연산

/* 블록 포인터 bp를 바탕으로 블록의 header와 footer의 주소를 반환해주는 매크로 */
#define HDRP(bp)    ((char *)(bp) - WSIZE)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* bp를 바탕으로 이전, 이후 블록의 payload를 가리키는 주소를 반환하는 매크로*/
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE)) // 이후 블록의 bp
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) // 이전 블록의 bp

/* explicit를 위한 free block의 payload 공간에 들어가는 블록포인터 매크로*/
#define PREV_FREE_BLKP(bp)   (*(char **)(bp))
#define NEXT_FREE_BLKP(bp)   (*(char **)(bp + WSIZE))

static char *heap_listp; //전역변수로 heap_listp void형 포인터 선언
static char *free_listp; //제일 늦게 들어온 = 새 가용 블록을 가리키는 bp 주소
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

void put_new_free(void *bp); // 새 가용블록을 넣는 함수
void remove_block(void *bp); // 가용블록이 할당되었을때, 가용블록리스트에서 제거하는 함수

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "week06_team04",
    /* First member's full name */
    "Jeongyoung Park",
    /* First member's email address */
    "jeongyoungp@gmail.com",
    /* Second member's full name (leave blank if none) */
    "dongjin kim",
    /* Second member's email address (leave blank if none) */
    "haebin cho"
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) 
    {
        return -1;
    }
    PUT(heap_listp, 0); // unused 패딩 값, 사용하지 않음 정렬조건을 위해서 앞에 넣어줌
    PUT(heap_listp + (1 * WSIZE), PACK(2 * DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), NULL); // prev pointer
    PUT(heap_listp + (3 * WSIZE), NULL); // next pointer
    PUT(heap_listp + (4 * WSIZE), PACK(2 * DSIZE, 1)); // prologue footer, header와 동일
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1)); // epilogue header
    heap_listp += (2 * WSIZE); //prologue의 header와 footer 사이를 가리킴
    free_listp = heap_listp + DSIZE;
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;


    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize; // 수정된 블록의 크기
    size_t extendsize; // 맞는 블록의 크기가 없을때 확장하는 사이즈
    char *bp;

    if (size == 0)
        return NULL;
    
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE); //8의 배수를 만들기 위함
    
    // 알맞는 블록을 검색하는 조건문
    if ((bp = find_fit(asize)) != NULL) 
    {
        place(bp, asize);
        return bp;
    }

    // 알맞은 블록의 크기를 찾지 못했을때, 메모리를 더 요청하고, 배치함
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;

    // int newsize = ALIGN(size + SIZE_T_SIZE);
    // void *p = mem_sbrk(newsize);
    // if (p == (void *)-1)
	// return NULL;
    // else {
    //     *(size_t *)p = size;
    //     return (void *)((char *)p + SIZE_T_SIZE);
    // }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp) //raw : ptr
{
    size_t size = GET_SIZE(HDRP(bp)); //bp가 가리키는 블록의 사이즈

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    //copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

// word단위로 메모리를 인자로 받아 힙을 늘려주는 함수
// 용도 : (1) 힙 초기화 (2) mm_ 
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
    {
        return NULL;
    }

    // 새로운 free 블록 초기화
    PUT(HDRP(bp), PACK(size, 0)); // free block header
    PUT(FTRP(bp), PACK(size, 0)); // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue header

    // 앞 뒤 블록 free 블록이면 연결하고 bp를 반환
    return coalesce(bp);
}


// 연결
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록의 할당 비트의 값
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음(이후) 블록의 할당 비트의 값
    size_t size = GET_SIZE(HDRP(bp));

    // //case 1 : 이전, 다음 블록 모두 할당상태 > 연결 불가, 그냥 bp 반환
    // if (prev_alloc && next_alloc) {
    //     return bp;
    // }

    //case 2 : 이전 할당, 다음 블록 free > 다음 블록과 연결시키고 현재 bp 반환
    if (prev_alloc && !next_alloc) { // if문에 들어오기 위해 !(0) 시킴
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    //case 3 : 이전 free, 다음 블록 할당 > 이전 블록과 연결시키고 이전 블록 bp 반환
    else if (!prev_alloc && next_alloc) {
        printf("%x\n", bp);
        printf("%x\n", PREV_BLKP(bp));
        
        remove_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    //case 4 : 둘다 free 상태 > 모두 연결하고 header는 이전 블록 bp 가리킴
    else {
        remove_block(PREV_BLKP(bp));
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); ////////////////
        bp = PREV_BLKP(bp);
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    }
    put_new_free(bp);
    return bp;
}

//first fit 기준
//freelist중에서 처음부터 탐색해야함 lifo 구조
static void *find_fit(size_t asize)
{
    void *bp;
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) != 1; bp = NEXT_FREE_BLKP(bp)) // free_listp에서부터 alloc이 미할당인 애들 next로 넘어가며 탐색
    {
        if (GET_SIZE(HDRP(bp)) >= asize) //size가 잘 맞으면
        {
            return bp;
        }
    }
    return NULL; //맞는 게 없으면 null
}

// 할당 블록 배치하고 남은 블록 분할하는 함수
static void place(void *bp, size_t asize)
{
    size_t cur_size = GET_SIZE(HDRP(bp)); //현재 할당할 블록
    remove_block(bp); //할당할 블록이니까 freelist에서 없애줘야함

    if ((cur_size - asize) >= (2 * DSIZE)) //남은 블록 다시 분할
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp); //bp 이동, 배치
        PUT(HDRP(bp), PACK(cur_size - asize, 0));
        PUT(FTRP(bp), PACK(cur_size - asize, 0));
        put_new_free(bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(cur_size, 1));
        PUT(FTRP(bp), PACK(cur_size, 1));
    }
    
}

// 새 가용블록을 freelist에 넣을때 next, prev, free_listp 포인터를 연결해주는 함수
void put_new_free(void *bp) {
    // free_listp는 언제나 가장 늦게 들어온 free block을 가르켜야함
    // LIFO 구조이기 때문에 새로운 가용 블록은 늘 맨 앞에 위치함

    NEXT_FREE_BLKP(bp) = free_listp; // free_listp는 현재 freelist에서의 제일 앞 부분을 가리키고 있으므로 그 값을 넣는다
    PREV_FREE_BLKP(bp) = NULL; // 새 가용 블록은 항상 맨앞
    PREV_FREE_BLKP(free_listp) = bp; // free_listp가 가리키고 있는 prev 블록에 bp값을 갱신함
    free_listp = bp; // free_listp의 값을 새로운 가용 블록의 bp로 바꿈
}

// 가용블록이 할당되었을때, 가용블록리스트에서 제거하는 함수
void remove_block(void *bp) {
    // 2가지 경우 : 1. 맨 앞 가용 블럭이 할당되었을경우, 2. freelist 중 중간 블럭이 할당되었을 경우
    if (free_listp == bp) // case 1
    {
        // 1 > 2 > 3 순으로 연결되어있는데 1번 제거
        // 1번 블록의 다음 2번 블록은 어디에 저장? > 1번 블록의 next값
        PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = NULL; // 1번 블록의 다음 2번 블록에 담긴 prevp를 null로 만듦 > 연결 끊기
        free_listp = NEXT_FREE_BLKP(bp);// free_listp가 다음 2번 블록을 가리킨다
    }
    else // case 2
    {
        // 1 > 2 > 3 순으로 연결 되어있는데 2번 제거
        // 1번 블록에서 2번에 연결된 next를 지우고, 3번 블록의 bp를 가리켜야함
        // 3번 블록에서 2번에 연결된 prev를 지우고, 1번 블록의 bp를 가리켜야함
       NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) =  NEXT_FREE_BLKP(bp); // 지금 2번 블록의 bp임... prev(bp) = 1번 블록의 주소값 / 3번의 주소값은? 현재 bp의 next값
       PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) =  PREV_FREE_BLKP(bp); //2번의 next값은 3번 
    }
}