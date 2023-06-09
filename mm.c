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

// 주희언니는 12 : 테스트 했을때 제일 점수 잘 나오는 수였음 현지언니는 32 32인 이유 : 32bit 기준 메모리의 최댓값이 4GB라서!
// 12하니까 오히려 82가 됨, 15는 83 16일때 84
#define NUM_INDEX   16

static char *heap_listp; // 전역변수로 heap_listp void형 포인터 선언
static char *free_list[NUM_INDEX]; // segregated의 인덱스를 저장하는 배열
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static int find_size(size_t asize); //segregated 추가 함수

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
    "",
    /* Second member's email address (leave blank if none) */
    ""
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
    // printf("mm_init()");
    for (int i = 0; i < NUM_INDEX; i++)
    {
        free_list[i] = NULL; // 처음에는 NULL로 초기화해줘야함
    }

    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) 
    {
        return -1;
    }
    PUT(heap_listp, 0); // unused 패딩 값, 사용하지 않음 정렬조건을 위해서 앞에 넣어줌
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer, header와 동일
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1)); // epilogue header
    heap_listp += DSIZE;
    // heap_listp += (2 * WSIZE); //prologue의 header와 footer 사이를 가리킴
    // free_listp = heap_listp + DSIZE;
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
    // printf("mm_malloc()");

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
    // printf("extend_heap\n");
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
    // printf("coalesce\n");

    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록의 할당 비트의 값
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음(이후) 블록의 할당 비트의 값
    size_t size = GET_SIZE(HDRP(bp));

    // //case 1 : 이전, 다음 블록 모두 할당상태 > 연결 불가, 그냥 bp 반환
    // if (prev_alloc && next_alloc) {
    //     put_new_free(bp);
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
        // printf("%x\n", bp);
        // printf("%x\n", PREV_BLKP(bp));
        
        remove_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);

    }

    //case 4 : 둘다 free 상태 > 모두 연결하고 header는 이전 블록 bp 가리킴
    else if (!prev_alloc && !next_alloc) {
        remove_block(PREV_BLKP(bp));
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); ////////////////
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);

    }
    put_new_free(bp);
    return bp;
}

// first fit 기준
// explicit : freelist중에서 처음부터 탐색해야함 lifo 구조
// find_index로 인덱스를 찾은 후, 인덱스를 이용해서 탐색해야함
static void *find_fit(size_t asize)
{
    void *bp;
    int index = find_size(asize);
    for (int i = index; i < NUM_INDEX; i++) 
    {
        //bp가 null이 아닐때까지 bp를 다음 블록으로 넘기며 탐색
        for (bp = free_list[i]; bp != NULL; bp = NEXT_FREE_BLKP(bp))
        {
            if (GET_SIZE(HDRP(bp)) >= asize) //size가 잘 맞으면
                return bp;
        }
    }
    return NULL;
}
    
    // for (bp = free_listp; GET_ALLOC(HDRP(bp)) != 1; bp = NEXT_FREE_BLKP(bp)) // free_listp에서부터 alloc이 미할당인 애들 next로 넘어가며 탐색
    // {
    //     if (GET_SIZE(HDRP(bp)) >= asize) //size가 잘 맞으면
    //     {
    //         return bp;
    //     }
    // }
    // return NULL; //맞는 게 없으면 null

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

// 할당할 블록의 사이즈에 따라 free_list에서 적합한 크기의 인덱스를 찾아 블록 삽입(LIFO)
// LIFO 구조이기 때문에 앞에 있는 애를 밀어내야함!
// case 1 : 해당 인덱스에 아무것도 없었다면 -> 첫 삽입이라면 해당 인덱스의 free_list가 bp를 가리키게 하면 됨
// 경우 1을 하는 이유 : 첫 초기화할때 prev, next가 아니라 null이므로 따로 나눠서 처리해줘야함
// case 2 : 원래 가용 블록이 있다면 -> 밀어내고 첫번쨰에 삽입
void put_new_free(void *bp) {
    // 현재 bp는 새로 들어가는 가용 블록의 bp임
    int i = find_size(GET_SIZE(HDRP(bp)));
    // case 1
    if (free_list[i] == NULL)
    {
        PREV_FREE_BLKP(bp) = NULL;
        NEXT_FREE_BLKP(bp) = NULL;
        free_list[i] = bp;
    }
    else
    {
        PREV_FREE_BLKP(bp) = NULL; // 맨 앞에 들어가므로 prev는 null
        NEXT_FREE_BLKP(bp) = free_list[i]; // next는 원래 맨 앞에 있던 블록의 bp를 가리킴
        PREV_FREE_BLKP(free_list[i]) = bp;// 원래 들어있던 애의 prev는 지금 들어오는 bp를 가리킴
        free_list[i] = bp; // free_list가 현재 bp를 가리키게 함
    }
} 

// 가용블록이 할당되었을때, 가용블록리스트에서 제거하는 함수
// 할당될 인덱스로 찾아가서 제거해줘야함
// 맨 앞 블럭 삭제 : 걍 삭제 free_list[i] = bp
//else : free_list[i] != bp
// 중간 블럭 삭제 : 원래 로직 적용
// 뒷블럭 삭제 : next가 null인 블럭 삭제 후 그 전 블럭의 next를 null로 만들어줌

void remove_block(void *bp) {
    int i = find_size(GET_SIZE(HDRP(bp))); // 인덱스 찾아오는 설정을 해줌
    // 1 > 2 > 3
    //case 1 : 1번 블럭을 삭제
    if (free_list[i] == bp)
    {
        if (NEXT_FREE_BLKP(bp) == NULL) // 1번 블럭이 마지막 블럭일때
        {
            free_list[i] = NULL; //해당 인덱스는 가용 블럭이 없어짐
        }
        else // 뒤에 다른 블럭이 이어져 있다면
        {
            PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = NULL; // 2번 블럭이 첫 블록이 되므로 2번의 prevp가 null
            free_list[i] = NEXT_FREE_BLKP(bp); // free_list는 2번 블럭의 bp를 가리킴
        }
    } 
    else //free_list[i] != bp 즉, 중간이거나 뒷블럭일때
    {
        if (NEXT_FREE_BLKP(bp) == NULL) // 제일 끝 블럭일때
        {
            NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) = NULL; // 
        }
        else
        {
            NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) =  NEXT_FREE_BLKP(bp);
            PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) =  PREV_FREE_BLKP(bp);
        }
    }

}

// 분리 가용 리스트에서 할당 받을 asize를 인자로 받아 어느 인덱스의 사이즈에 들어가는지 찾아주는 함수
static int find_size(size_t asize)
{
    int index = 0;
    // 인덱스가 인덱스의 최댓값보다 작고, 사이즈가 2^index보다 크다면
    while ((index < NUM_INDEX -1 ) && !(asize <= (1 << index)))
    {
        index++;
    }
    return index;
}