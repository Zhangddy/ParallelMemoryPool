#pragma once
#include <iostream>
#include <assert.h>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <windows.h>

using std::endl;
using std::cout;

const size_t MAX_SIZE = 64 * 1024;
const size_t NFREE_LIST = 240;//MAX_SIZE / 8; // 8K
const size_t MAX_PAGES = 129;
const size_t PAGE_SHIFT = 12; 

// 用于获取下一个节点
// 64位下指针大小为8, 故分配最小空间应为8
inline void*& NextObj(void* obj)
{
	// obj为void*指针
	// 需要得到它存的地址的所指向空间的地址
	return *((void**)obj);
}

class FreeList
{
public:
	void Push(void* obj)
	{
		// 头插
		NextObj(obj) = _freelist;
		_freelist = obj;
		++_num;
	}

	void* Pop()
	{
		// 头删
		void* obj = _freelist;
		_freelist = NextObj(obj);
		--_num;
		return obj;
	}

	void PushRange(void* head, void* tail, size_t num)
	{
		NextObj(tail) = _freelist;
		_freelist = head;
		_num += num;
	}

	size_t PopRange(void*& start, void*& end, size_t num)
	{
		size_t actualNum = 0; // 实际返回的长度, 因为实际返回的可能不够
		void* prev = nullptr;
		void* cur = _freelist;
		for (; actualNum < num && cur != nullptr; ++actualNum)
		{
			prev = cur;
			cur = NextObj(cur);
		}
		start = _freelist;
		end = prev;
		_freelist = cur;
		_num -= actualNum;
		return actualNum;
	}

	size_t Num()
	{
		return _num;
	}

	bool Empty()
	{
		return _freelist == nullptr;
	}

	void Clear()
	{
		_freelist = nullptr;
		_num = 0;
	}
private:
	void* _freelist = nullptr;
	size_t _num = 0;
};

class SizeTools
{
public:
	static size_t dealRoundUp(size_t size, size_t alignment)
	{
		return (size + alignment - 1)&(~(alignment - 1));
	}
	// 返回一个对齐结果, 需要的内存越大对齐数越大, 防止大内存的链表过长
	static inline size_t RoundUp(size_t size)
	{
		assert(size <= MAX_SIZE);
		/*
		if (size % 8 != 0)
		{
			return (size / 8 + 1) * 8;
		}
		else
		{
			return size;
		}
		*/
		// 除法, 取模运算效率低, 参考原码: 使用移位运算

		// 返回一个适合的对齐数, 用于分配(控制内部碎片不要太大)

		// [1,128]				8byte对齐	 freelist[0,16)
		// [129,1024]			16byte对齐	 freelist[16,72)
		// [1025,8*1024]		128byte对齐  freelist[72,128)
		// [8*1024+1,64*1024]	1024byte对齐 freelist[128,184)
		// 平均控制内存碎片到10%
		// 全部以8对齐, 后面得链表太长了
		if (size <= 128)
		{
			return dealRoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return dealRoundUp(size, 16);
		}
		else if (size <= 8192)
		{
			return dealRoundUp(size, 128);
		}
		else if (size <= 65536)
		{
			return dealRoundUp(size, 1024);
		}

		return -1;
	}
	 
	static size_t dealListIndex(size_t size, size_t align_shift = 8)
	{
		return ((size + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 返回一个去哪个链的下标
	static size_t ListIndex(size_t size)
	{
		assert(size <= MAX_SIZE);
		/*
		if (size % 8 == 0)
		{
			return size / 8 - 1;
		}
		else
		{
			return size / 8;
		}
		*/

		// 参考原码, 一个更加快速的方式, 减少除模运算
		static int group_array[4] = { 16, 56, 56, 56 };
		if (size <= 128){
			return dealListIndex(size, 3);
		}
		else if (size <= 1024){
			return dealListIndex(size - 128, 4) + group_array[0];
		}
		else if (size <= 8192){
			return dealListIndex(size - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (size <= 65536){
			return dealListIndex(size - 8192, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		return -1;
	}

	// [2,512]个之间
	// NumMoveSize得到要分配空间的个数, 会多取一些
	static size_t NumMoveSize(size_t size)
	{
		if (size == 0)
			return 0;

		int num = MAX_SIZE / size;
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;

		npage >>= 12;
		if (npage == 0)
			npage = 1;

		return npage;
	}
};

#ifdef _WIN32
typedef unsigned int PAGE_ID;
#else
typedef unsigned long long PAGE_ID;
#endif 

struct Span
{
	PAGE_ID  _pageid = 0;		// 页号
	PAGE_ID  _pagesize = 0;		// 页的数量
	FreeList _freeList;			// 对象自由链表
	size_t	 _objSize = 0;		// 自由链表对象大小
 	int		 _usecount = 0;		// 内存块对象使用计数
	Span*	 _next = nullptr;
	Span*	 _prev = nullptr;
};

// 使用头结点操作双向循环链表, 提高效率
class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	void PushFront(Span* newspan)
	{
		Insert(_head->_next, newspan);
	}

	void PopFront()
	{
		Erase(_head->_next);
	}

	void PushBack(Span* newspan)
	{
		Insert(_head, newspan);
	}

	void PopBack()
	{
		Erase(_head->_prev);
	}

	void Insert(Span* pos, Span* newspan)
	{
		Span* prev = pos->_prev;

		prev->_next = newspan;
		newspan->_next = pos;
		pos->_prev = newspan;
		newspan->_prev = prev;
	}

	void Erase(Span* pos)
	{
		assert(pos != _head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	bool Empty()
	{
		return Begin() == End();
	}

	void Lock()
	{
		_mtx.lock();
	}

	void Unlock()
	{
		_mtx.unlock();
	}

private:
	Span* _head;
	std::mutex _mtx;
};

// 向操作系统申请以页为单位的空间
inline static void* SystemAlloc(size_t num_page)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, num_page * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// Linux: brk
	// TODO
#endif
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
	}

	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
#endif
}