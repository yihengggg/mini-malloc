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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*  EMPTY BLOCK
 *  -----------------------------------------------*
 *  |HEADER:    block size   |     |     |alloc bit|
 *  |----------------------------------------------|
 *  | pointer to prev free block in this size list |
 *  |----------------------------------------------|
 *  | pointer to next free block in this size list |
 *  |----------------------------------------------|
 *  |FOOTER:    block size   |     |     |alloc bit|
 *  ------------------------------------------------
 */

/*  Allocated BLOCK
 *   -----------------------------------------------*
 *   |HEADER:    block size   |     |     |alloc bit|
 *   |----------------------------------------------|
 *   |               Data                           |
 *   |----------------------------------------------|
 *   |               Data                           |
 *   |----------------------------------------------|
 *   |FOOTER:    block size   |     |     |alloc bit|
 *   ------------------------------------------------
 */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4             // 字的大小
#define DSIZE 8             // 双字的大小
#define CHUNKSIZE (1 << 12) // 初始空闲块的大小和扩展堆时的默认大小

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
// 将大小和已分配位结合起来并返回一个值，可以把它存放在头部或者脚部中。
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
// 读取和返回参数 p 引用的字
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
// 从地址 p 处的头部或者脚部分别返回大小和已分配位
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
// 返回指向这个块的头部和脚部的指针
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
// 分别返回指向后面的块和前面的块的块指针
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

/* Get and set prev or next pointer from address p */
#define GET_PREV(p) (*(unsigned int *)(p))
#define SET_PREV(p, prev) (*(unsigned int *)(p) = (prev))
#define GET_NEXT(p) (*((unsigned int *)(p)+1))
#define SET_NEXT(p, val) (*((unsigned int *)(p)+1) = (val))

// 全局变量
static char *heap_listp = 0;  // 隐式空闲链表
static char *pre_listp = 0;  // 用于next fit
static char *free_list_head = 0; // 显式空闲链表

/*
 * insert_to_free_list - 从空闲链表中插入空闲块
 */
static void insert_to_free_list(void *bp) {
  if (bp == NULL)
    return;

  if (free_list_head == NULL) {
    free_list_head = bp;
    return;
  }
  // 头插法
  SET_NEXT(bp, free_list_head);
  SET_PREV(free_list_head, bp);
  // 更新链表头节点
  free_list_head = bp;
}

/*
 * remove_from_free_list - 从空闲链表中移除空闲块
 */
static void remove_from_free_list(void *bp) {
  if (bp == NULL || GET_ALLOC(HDRP(bp)))
    return;

  void *prev = GET_PREV(bp);
  void *next = GET_NEXT(bp);

  SET_PREV(bp, 0);
  SET_NEXT(bp, 0);

  // 修改该空闲块在空闲链表中前后两个块的相关指针
  if (prev == NULL && next == NULL) { // 空闲链表中只有一个空闲块
    free_list_head = NULL;
  } else if (prev == NULL) {  // 被删除的是头结点
    SET_PREV(next, 0);
    free_list_head = next;
  } else if (next == NULL) {  // 被删除的是尾节点
    SET_NEXT(prev, 0);
  } else {
    SET_NEXT(prev, next);
    SET_PREV(next, prev);
  }
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
  /* Create the initial empty heap */
  //   申请四个字的空间
  if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) -1)
    return -1;
  PUT(heap_listp, 0);                            // 对齐
  PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 序言块
  PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // 序言块
  PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // 结尾块
  heap_listp += (2 * WSIZE);
  pre_listp = heap_listp;
  free_list_head = NULL;

  // 扩展空闲空间
  if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    return -1;
  return 0;
}

// 用一个新的空闲块扩展堆
static void *extend_heap(size_t words) {
  char *bp;
  size_t size;

  /* Allocate an even number of words to maintain alignment */
  // 对齐
  size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

  // 向操作系统额外分配内存
  if ((long) (bp = mem_sbrk(size)) == -1)    // 返回旧heap尾部指针
    return NULL;

  /* Initialize free block header/footer and the epilogue header */
  // 设置头部
  PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
  // 设置脚部
  PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */

  SET_PREV(bp, 0);
  SET_NEXT(bp, 0);

  // 设置新结尾块的头部
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

  /* Coalesce if the previous block was free */
  // 如果前一个块是空闲状态则执行合并操作
  return coalesce(bp);
}

/*
 * mm_malloc - 分配内存块
 *
 */
void *mm_malloc(size_t size) {
  size_t asize;      /* Adjusted block size */
  size_t extendsize; /* Amount to extend heap if no fit */
  char *bp;

  /* Ignore spurious requests */
  if (size == 0)
    return NULL;

  /* Adjust block size to include overhead and alignment reqs. */
  if (size <= DSIZE)
    asize = 2 * DSIZE;
  else
    asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE); // 对齐

  /* Search the free list for a fit */
  // 首先尝试在空闲链表上搜索合适的空闲块
  if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }

  /* No fit found. Get more memory and place the block */
  // 其次才向操作系统获取额外的堆内存
  extendsize = MAX(asize, CHUNKSIZE);
  if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
    return NULL;
  place(bp, asize);
  return bp;
}

/*
 * find_fit - 放置策略
 *
 * 如何操作和遍历块
 */
static void *find_fit(size_t asize) {
  /* First-fit search */
  return first_fit(asize);
//  return next_fit(asize);
  // return best_fit(asize);
}

/*
 * first_fit - 首次匹配
 */
void *first_fit(size_t asize) {
  char *bp;
  // 遍历空闲链表，找到合适的空闲块就停止遍历
  for (bp = free_list_head; GET_SIZE(HDRP(bp)) > 0; bp = GET_NEXT(bp)) {
    if (asize <= GET_SIZE(HDRP(bp))) {
      return bp;
    }
  }
  return NULL;
}

/*
 * next_fit - 下一次匹配
 */
void *next_fit(size_t asize) {
  // 从上一次查找的地方开始
  for (char *bp = pre_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      pre_listp = bp;
      return bp;
    }
  }
  // 若遍历完空闲链表都没找到，就从头开始
  for (char *bp = heap_listp; bp != pre_listp; bp = NEXT_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      pre_listp = bp;
      return bp;
    }
  }
  return NULL;
}

/*
 * best_fit - 最优匹配
 */
void *best_fit(size_t asize) {
  char *bp;
  char *best_bp = NULL;
  size_t min_size = 0;
  // 遍历完空闲链表，找到最合适的空闲块
  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if ((GET_SIZE(HDRP(bp)) >= asize) && (!GET_ALLOC(HDRP(bp)))) {
      if (min_size == 0 || min_size > GET_SIZE(HDRP(bp))) {
        min_size = GET_SIZE(HDRP(bp));
        best_bp = bp;
      }
    }
  }
  return best_bp;
}

/*
 * place - 分割空闲块
 *
 * 将请求块放置在空闲块的起始位置，
 * 只有当剩余部分的大小等于或者超出最小块的大小时，才进行分割。
 */
static void place(void *bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));  // 获取空闲块的大小
  remove_from_free_list(bp);

  if ((csize - asize) >= (2 * DSIZE)) { // 判断分割后剩余空闲块大小是否超过最小块大小
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    void *new_bp = NEXT_BLKP(bp);
    PUT(HDRP(new_bp), PACK(csize - asize, 0));
    PUT(FTRP(new_bp), PACK(csize - asize, 0));
    // 分割后的剩余空闲块加入空闲链表
    SET_PREV(new_bp, 0);
    SET_NEXT(new_bp, 0);
    coalesce(new_bp);
  } else {  // 否则将剩余部分设置为已分配块的填充
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
  if (ptr == 0) {
    return;
  }
  size_t size = GET_SIZE(HDRP(ptr));

  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));

  SET_PREV(bp, 0);
  SET_NEXT(bp, 0);
  pre_listp = coalesce(ptr);
}

/*
 * coalesce - 合并当前块及其前后空闲块
 *
 * 分四种情况
 */
static void *coalesce(void *bp) {
  void *prev_bp = PREV_BLKP(bp);
  void *next_bp = NEXT_BLKP(bp);
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 前一个块的分配位
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 后一个块的分配位
  size_t size = GET_SIZE(HDRP(bp)); // 当前块大小

  if (prev_alloc && next_alloc) { // 情况1：前后两个块都是已分配
//    return bp;  // 不合并，直接返回
  } else if (prev_alloc && !next_alloc) { // 情况2：前一个块已分配，后一个块空闲
    remove_from_free_list(next_bp);
    // 设置当前块的头部和后一个块的脚部
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  } else if (!prev_alloc && next_alloc) { // 情况3：前一个块空闲，后一个块已分配
    remove_from_free_list(prev_bp);

    // 设置当前块的脚部和前一个块的头部
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

    // 更新当前块的指针
    bp = PREV_BLKP(bp);
  } else { // 情况4：前后两个块都是空闲
    remove_from_free_list(prev_bp);
    remove_from_free_list(next_bp);

    // 设置前一个块的头部和后一个块的脚部
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));

    // 更新当前块的指针
    bp = PREV_BLKP(bp);
  }

  insert_to_free_list(bp);
  return bp;
}

/*
 * mm_realloc - 改变一个已分配块的大小
 */
void *mm_realloc(void *ptr, size_t size) {
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;

  // 使用 mm_malloc 申请一个新内存块
  newptr = mm_malloc(size);
  if (newptr == NULL)
    return NULL;

  size = GET_SIZE(HDRP(oldptr));
  copySize = GET_SIZE(HDRP(newptr));
  if (size < copySize)
    copySize = size;

  // 将旧内存块的内容复制到新内存块上
  memcpy(newptr, oldptr, copySize - WSIZE);

  // 释放旧内存块
  mm_free(oldptr);
  return newptr;
}

/*
 * mm_check - 检查堆的一致性
 */
static int mm_check(void) {
  return 1;
}