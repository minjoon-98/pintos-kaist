#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* Element type.

   This must be an unsigned integer type at least as wide as int.

   Each bit represents one bit in the bitmap.
   If bit 0 in an element represents bit K in the bitmap,
   then bit 1 in the element represents bit K+1 in the bitmap,
   and so on. */
/* Element type.
이 타입은 최소한 int만큼의 크기를 가지는 unsigned integer 타입이어야 합니다.
각 비트는 비트맵의 한 비트를 나타냅니다.
예를 들어, element의 bit 0이 비트맵의 K번째 비트를 나타내면,
element의 bit 1은 비트맵의 K+1번째 비트를 나타냅니다. */
typedef unsigned long elem_type;

/* Number of bits in an element. */
/* 하나의 element가 가지는 비트 수를 나타냅니다. */
#define ELEM_BITS (sizeof(elem_type) * CHAR_BIT)

/* From the outside, a bitmap is an array of bits.  From the
   inside, it's an array of elem_type (defined above) that
   simulates an array of bits. */
/* 비트맵은 외부에서 볼 때 비트들의 배열입니다.
   내부에서는 elem_type(위에서 정의된)을 요소로 가지는 배열이 되어
   비트의 배열을 시뮬레이트합니다. */
struct bitmap
{
	size_t bit_cnt;	 /* Number of bits. */
	elem_type *bits; /* Elements that represent bits. */
};

/* Returns the index of the element that contains the bit
   numbered BIT_IDX. */
/** BIT_IDX에 해당하는 비트를 포함하는 element의 인덱스를 반환합니다.
 *  @param bit_idx 비트 인덱스
 *  @return element 인덱스
 */
static inline size_t
elem_idx(size_t bit_idx)
{
	return bit_idx / ELEM_BITS;
}

/* Returns an elem_type where only the bit corresponding to
   BIT_IDX is turned on. */
/** BIT_IDX에 해당하는 비트만 켜진 elem_type을 반환합니다.
 *  @param bit_idx 비트 인덱스
 *  @return 비트 마스크
 */
static inline elem_type
bit_mask(size_t bit_idx)
{
	return (elem_type)1 << (bit_idx % ELEM_BITS);
}

/* Returns the number of elements required for BIT_CNT bits. */
/** BIT_CNT 비트를 위해 필요한 element의 수를 반환합니다.
 *  @param bit_cnt 비트 수
 *  @return element 수
 */
static inline size_t
elem_cnt(size_t bit_cnt)
{
	return DIV_ROUND_UP(bit_cnt, ELEM_BITS);
}

/* Returns the number of bytes required for BIT_CNT bits. */
/** BIT_CNT 비트를 위해 필요한 바이트 수를 반환합니다.
 *  @param bit_cnt 비트 수
 *  @return 바이트 수
 */
static inline size_t
byte_cnt(size_t bit_cnt)
{
	return sizeof(elem_type) * elem_cnt(bit_cnt);
}

/* Returns a bit mask in which the bits actually used in the last
   element of B's bits are set to 1 and the rest are set to 0. */
/** 비트맵의 마지막 element에서 실제로 사용된 비트를 1로 설정하고,
 *  나머지 비트를 0으로 설정하는 비트 마스크를 반환합니다.
 *  @param b 비트맵 포인터
 *  @return 비트 마스크
 */
static inline elem_type
last_mask(const struct bitmap *b)
{
	int last_bits = b->bit_cnt % ELEM_BITS;
	return last_bits ? ((elem_type)1 << last_bits) - 1 : (elem_type)-1;
}

/* Creation and destruction. */

/* Initializes B to be a bitmap of BIT_CNT bits
   and sets all of its bits to false.
   Returns true if success, false if memory allocation
   failed. */
/** 비트 수 BIT_CNT를 가지는 비트맵을 초기화하고,
 *  모든 비트를 false로 설정합니다.
 *  성공 시 비트맵 포인터를 반환하고, 메모리 할당 실패 시 NULL을 반환합니다.
 *  @param bit_cnt 비트 수
 *  @return 비트맵 포인터
 */
struct bitmap *
bitmap_create(size_t bit_cnt)
{
	struct bitmap *b = malloc(sizeof *b);
	if (b != NULL)
	{
		b->bit_cnt = bit_cnt;
		b->bits = malloc(byte_cnt(bit_cnt));
		if (b->bits != NULL || bit_cnt == 0)
		{
			bitmap_set_all(b, false);
			return b;
		}
		free(b);
	}
	return NULL;
}

/* Creates and returns a bitmap with BIT_CNT bits in the
   BLOCK_SIZE bytes of storage preallocated at BLOCK.
   BLOCK_SIZE must be at least bitmap_needed_bytes(BIT_CNT). */
/** BLOCK_SIZE 바이트로 사전 할당된 BLOCK에 BIT_CNT 비트를 가지는 비트맵을 생성하고 반환합니다.
 *  BLOCK_SIZE는 최소한 bitmap_needed_bytes(BIT_CNT) 이상이어야 합니다.
 *  @param bit_cnt 비트 수
 *  @param block 블록 포인터
 *  @param block_size 블록 크기
 *  @return 비트맵 포인터
 */
struct bitmap *
bitmap_create_in_buf(size_t bit_cnt, void *block, size_t block_size UNUSED)
{
	struct bitmap *b = block;

	ASSERT(block_size >= bitmap_buf_size(bit_cnt));

	b->bit_cnt = bit_cnt;
	b->bits = (elem_type *)(b + 1);
	bitmap_set_all(b, false);
	return b;
}

/* Returns the number of bytes required to accomodate a bitmap
   with BIT_CNT bits (for use with bitmap_create_in_buf()). */
/** BIT_CNT 비트를 가지는 비트맵을 수용하기 위해 필요한 바이트 수를 반환합니다.
 *  (bitmap_create_in_buf() 사용 시)
 *  @param bit_cnt 비트 수
 *  @return 바이트 수
 */
size_t
bitmap_buf_size(size_t bit_cnt)
{
	return sizeof(struct bitmap) + byte_cnt(bit_cnt);
}

/* Destroys bitmap B, freeing its storage.
   Not for use on bitmaps created by
   bitmap_create_preallocated(). */
/** 비트맵 B를 해제하고, 그 저장 공간을 반환합니다.
 *  bitmap_create_preallocated()에 의해 생성된 비트맵에는 사용하지 않습니다.
 *  @param b 비트맵 포인터
 */
void bitmap_destroy(struct bitmap *b)
{
	if (b != NULL)
	{
		free(b->bits);
		free(b);
	}
}

/* Bitmap size. */

/* Returns the number of bits in B. */
/** 비트맵 B의 비트 수를 반환합니다.
 *  @param b 비트맵 포인터
 *  @return 비트 수
 */
size_t
bitmap_size(const struct bitmap *b)
{
	return b->bit_cnt;
}

/* Setting and testing single bits. */
/* Atomically sets the bit numbered IDX in B to VALUE. */
/** 비트맵 B에서 IDX로 지정된 비트를 VALUE로 설정합니다.
 *  @param b 비트맵 포인터
 *  @param idx 인덱스
 *  @param value 값
 */
void bitmap_set(struct bitmap *b, size_t idx, bool value)
{
	ASSERT(b != NULL);
	ASSERT(idx < b->bit_cnt);
	if (value)
		bitmap_mark(b, idx);
	else
		bitmap_reset(b, idx);
}

/* Atomically sets the bit numbered BIT_IDX in B to true. */
/** 비트맵 B에서 BIT_IDX로 지정된 비트를 true로 설정합니다.
 *  @param b 비트맵 포인터
 *  @param bit_idx 비트 인덱스
 */
void bitmap_mark(struct bitmap *b, size_t bit_idx)
{
	size_t idx = elem_idx(bit_idx);
	elem_type mask = bit_mask(bit_idx);

	/* This is equivalent to `b->bits[idx] |= mask' except that it
	   is guaranteed to be atomic on a uniprocessor machine.  See
	   the description of the OR instruction in [IA32-v2b]. */
	/* 이는 `b->bits[idx] |= mask'와 동일하지만,
	단일 프로세서 머신에서 원자적으로 보장됩니다.
	[IA32-v2b]의 OR 명령어 설명을 참조하십시오. */
	asm("lock orq %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* Atomically sets the bit numbered BIT_IDX in B to false. */
/** 비트맵 B에서 BIT_IDX로 지정된 비트를 false로 설정합니다.
 *  @param b 비트맵 포인터
 *  @param bit_idx 비트 인덱스
 */
void bitmap_reset(struct bitmap *b, size_t bit_idx)
{
	size_t idx = elem_idx(bit_idx);
	elem_type mask = bit_mask(bit_idx);

	/* This is equivalent to `b->bits[idx] &= ~mask' except that it
	   is guaranteed to be atomic on a uniprocessor machine.  See
	   the description of the AND instruction in [IA32-v2a]. */
	/* 이는 `b->bits[idx] &= ~mask'와 동일하지만,
	단일 프로세서 머신에서 원자적으로 보장됩니다.
	[IA32-v2a]의 AND 명령어 설명을 참조하십시오. */
	asm("lock andq %1, %0" : "=m"(b->bits[idx]) : "r"(~mask) : "cc");
}

/* Atomically toggles the bit numbered IDX in B;
   that is, if it is true, makes it false,
   and if it is false, makes it true. */
/** 비트맵 B에서 IDX로 지정된 비트를 토글합니다.
 *  즉, 비트가 true이면 false로, false이면 true로 설정합니다.
 *  @param b 비트맵 포인터
 *  @param bit_idx 비트 인덱스
 */
void bitmap_flip(struct bitmap *b, size_t bit_idx)
{
	size_t idx = elem_idx(bit_idx);
	elem_type mask = bit_mask(bit_idx);

	/* This is equivalent to `b->bits[idx] ^= mask' except that it
	   is guaranteed to be atomic on a uniprocessor machine.  See
	   the description of the XOR instruction in [IA32-v2b]. */
	asm("lock xorq %1, %0" : "=m"(b->bits[idx]) : "r"(mask) : "cc");
}

/* Returns the value of the bit numbered IDX in B. */
/** 비트맵 B에서 IDX로 지정된 비트의 값을 반환합니다.
 *  @param b 비트맵 포인터
 *  @param idx 인덱스
 *  @return 비트 값
 */
bool bitmap_test(const struct bitmap *b, size_t idx)
{
	ASSERT(b != NULL);
	ASSERT(idx < b->bit_cnt);
	return (b->bits[elem_idx(idx)] & bit_mask(idx)) != 0;
}

/* Setting and testing multiple bits. */

/* Sets all bits in B to VALUE. */
/** 비트맵 B의 모든 비트를 VALUE로 설정합니다.
 *  @param b 비트맵 포인터
 *  @param value 값
 */
void bitmap_set_all(struct bitmap *b, bool value)
{
	ASSERT(b != NULL);

	bitmap_set_multiple(b, 0, bitmap_size(b), value);
}

/* Sets the CNT bits starting at START in B to VALUE. */
/** 비트맵 B에서 START로 시작하는 CNT 비트를 VALUE로 설정합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @param value 값
 */
void bitmap_set_multiple(struct bitmap *b, size_t start, size_t cnt, bool value)
{
	size_t i;

	ASSERT(b != NULL);
	ASSERT(start <= b->bit_cnt);
	ASSERT(start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		bitmap_set(b, start + i, value);
}

/* Returns the number of bits in B between START and START + CNT,
   exclusive, that are set to VALUE. */
/** 비트맵 B에서 START부터 START + CNT 사이의 비트 중
 *  VALUE로 설정된 비트의 수를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @param value 값
 *  @return VALUE로 설정된 비트의 수
 */
size_t
bitmap_count(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
	size_t i, value_cnt;

	ASSERT(b != NULL);
	ASSERT(start <= b->bit_cnt);
	ASSERT(start + cnt <= b->bit_cnt);

	value_cnt = 0;
	for (i = 0; i < cnt; i++)
		if (bitmap_test(b, start + i) == value)
			value_cnt++;
	return value_cnt;
}

/* Returns true if any bits in B between START and START + CNT,
   exclusive, are set to VALUE, and false otherwise. */
/** 비트맵 B에서 START부터 START + CNT 사이의 비트 중
 *  VALUE로 설정된 비트가 하나라도 있으면 true를 반환하고,
 *  그렇지 않으면 false를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @param value 값
 *  @return 해당 범위에 VALUE로 설정된 비트가 있으면 true, 그렇지 않으면 false
 */
bool bitmap_contains(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
	size_t i;

	ASSERT(b != NULL);
	ASSERT(start <= b->bit_cnt);
	ASSERT(start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		if (bitmap_test(b, start + i) == value)
			return true;
	return false;
}

/* Returns true if any bits in B between START and START + CNT,
   exclusive, are set to true, and false otherwise.*/
/** 비트맵 B에서 START부터 START + CNT 사이의 비트 중
 *  하나라도 true로 설정된 비트가 있으면 true를 반환하고,
 *  그렇지 않으면 false를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @return 해당 범위에 true로 설정된 비트가 있으면 true, 그렇지 않으면 false
 */
bool bitmap_any(const struct bitmap *b, size_t start, size_t cnt)
{
	return bitmap_contains(b, start, cnt, true);
}

/* Returns true if no bits in B between START and START + CNT,
   exclusive, are set to true, and false otherwise.*/
/** 비트맵 B에서 START부터 START + CNT 사이의 비트 중
 *  true로 설정된 비트가 하나도 없으면 true를 반환하고,
 *  그렇지 않으면 false를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @return 해당 범위에 true로 설정된 비트가 없으면 true, 그렇지 않으면 false
 */
bool bitmap_none(const struct bitmap *b, size_t start, size_t cnt)
{
	return !bitmap_contains(b, start, cnt, true);
}

/* Returns true if every bit in B between START and START + CNT,
   exclusive, is set to true, and false otherwise. */
/** 비트맵 B에서 START부터 START + CNT 사이의 모든 비트가
 *  true로 설정된 경우 true를 반환하고, 그렇지 않으면 false를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @return 해당 범위의 모든 비트가 true로 설정된 경우 true, 그렇지 않으면 false
 */
bool bitmap_all(const struct bitmap *b, size_t start, size_t cnt)
{
	return !bitmap_contains(b, start, cnt, false);
}

/* Finding set or unset bits. */

/* Finds and returns the starting index of the first group of CNT
   consecutive bits in B at or after START that are all set to
   VALUE.
   If there is no such group, returns BITMAP_ERROR. */
/** 비트맵 B에서 START 이후의 CNT 개의 연속된 비트 그룹 중
 *  모든 비트가 VALUE로 설정된 첫 번째 그룹의 시작 인덱스를 반환합니다.
 *  그런 그룹이 없으면 BITMAP_ERROR를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @param value 값
 *  @return 그룹의 시작 인덱스 또는 BITMAP_ERROR
 */
size_t
bitmap_scan(const struct bitmap *b, size_t start, size_t cnt, bool value)
{
	ASSERT(b != NULL);
	ASSERT(start <= b->bit_cnt);

	if (cnt <= b->bit_cnt)
	{
		size_t last = b->bit_cnt - cnt;
		size_t i;
		for (i = start; i <= last; i++)
			if (!bitmap_contains(b, i, cnt, !value))
				return i;
	}
	return BITMAP_ERROR;
}

/* Finds the first group of CNT consecutive bits in B at or after
   START that are all set to VALUE, flips them all to !VALUE,
   and returns the index of the first bit in the group.
   If there is no such group, returns BITMAP_ERROR.
   If CNT is zero, returns 0.
   Bits are set atomically, but testing bits is not atomic with
   setting them. */
/** 비트맵 B에서 START 이후의 CNT 개의 연속된 비트 그룹 중
 *  모든 비트가 VALUE로 설정된 첫 번째 그룹을 찾아,
 *  해당 그룹의 모든 비트를 !VALUE로 변경하고,
 *  그룹의 첫 번째 비트 인덱스를 반환합니다.
 *  그런 그룹이 없으면 BITMAP_ERROR를 반환합니다.
 *  CNT가 0이면 0을 반환합니다.
 *  비트를 설정하는 것은 원자적이지만, 비트를 테스트하는 것은 원자적이지 않습니다.
 *  @param b 비트맵 포인터
 *  @param start 시작 인덱스
 *  @param cnt 비트 수
 *  @param value 값
 *  @return 그룹의 시작 인덱스 또는 BITMAP_ERROR
 */
size_t
bitmap_scan_and_flip(struct bitmap *b, size_t start, size_t cnt, bool value)
{
	size_t idx = bitmap_scan(b, start, cnt, value);
	if (idx != BITMAP_ERROR)
		bitmap_set_multiple(b, idx, cnt, !value);
	return idx;
}

/* File input and output. */

#ifdef FILESYS
/* Returns the number of bytes needed to store B in a file. */
/** 비트맵 B를 파일에 저장하는 데 필요한 바이트 수를 반환합니다.
 *  @param b 비트맵 포인터
 *  @return 바이트 수
 */
size_t
bitmap_file_size(const struct bitmap *b)
{
	return byte_cnt(b->bit_cnt);
}

/* Reads B from FILE.  Returns true if successful, false
   otherwise. */
/** 파일에서 비트맵 B를 읽어옵니다.
 *  성공하면 true를 반환하고, 실패하면 false를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param file 파일 포인터
 *  @return 읽기 성공 여부
 */
bool bitmap_read(struct bitmap *b, struct file *file)
{
	bool success = true;
	if (b->bit_cnt > 0)
	{
		off_t size = byte_cnt(b->bit_cnt);
		success = file_read_at(file, b->bits, size, 0) == size;
		b->bits[elem_cnt(b->bit_cnt) - 1] &= last_mask(b);
	}
	return success;
}

/* Writes B to FILE.  Return true if successful, false
   otherwise. */
/** 비트맵 B를 파일에 씁니다.
 *  성공하면 true를 반환하고, 실패하면 false를 반환합니다.
 *  @param b 비트맵 포인터
 *  @param file 파일 포인터
 *  @return 쓰기 성공 여부
 */
bool bitmap_write(const struct bitmap *b, struct file *file)
{
	off_t size = byte_cnt(b->bit_cnt);
	return file_write_at(file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* Debugging. */

/* Dumps the contents of B to the console as hexadecimal. */
/** 비트맵 B의 내용을 16진수로 콘솔에 덤프합니다.
 *  @param b 비트맵 포인터
 */
void bitmap_dump(const struct bitmap *b)
{
	hex_dump(0, b->bits, byte_cnt(b->bit_cnt), false);
}
