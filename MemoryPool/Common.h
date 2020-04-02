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

// ���ڻ�ȡ��һ���ڵ�
// 64λ��ָ���СΪ8, �ʷ�����С�ռ�ӦΪ8
inline void*& NextObj(void* obj)
{
	// objΪvoid*ָ��
	// ��Ҫ�õ�����ĵ�ַ����ָ��ռ�ĵ�ַ
	return *((void**)obj);
}

class FreeList
{
public:
	void Push(void* obj)
	{
		// ͷ��
		NextObj(obj) = _freelist;
		_freelist = obj;
		++_num;
	}

	void* Pop()
	{
		// ͷɾ
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
		size_t actualNum = 0; // ʵ�ʷ��صĳ���, ��Ϊʵ�ʷ��صĿ��ܲ���
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
	// ����һ��������, ��Ҫ���ڴ�Խ�������Խ��, ��ֹ���ڴ���������
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
		// ����, ȡģ����Ч�ʵ�, �ο�ԭ��: ʹ����λ����

		// ����һ���ʺϵĶ�����, ���ڷ���(�����ڲ���Ƭ��Ҫ̫��)

		// [1,128]				8byte����	 freelist[0,16)
		// [129,1024]			16byte����	 freelist[16,72)
		// [1025,8*1024]		128byte����  freelist[72,128)
		// [8*1024+1,64*1024]	1024byte���� freelist[128,184)
		// ƽ�������ڴ���Ƭ��10%
		// ȫ����8����, ���������̫����
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

	// ����һ��ȥ�ĸ������±�
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

		// �ο�ԭ��, һ�����ӿ��ٵķ�ʽ, ���ٳ�ģ����
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

	// [2,512]��֮��
	// NumMoveSize�õ�Ҫ����ռ�ĸ���, ���ȡһЩ
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
	PAGE_ID  _pageid = 0;		// ҳ��
	PAGE_ID  _pagesize = 0;		// ҳ������
	FreeList _freeList;			// ������������
	size_t	 _objSize = 0;		// ������������С
 	int		 _usecount = 0;		// �ڴ�����ʹ�ü���
	Span*	 _next = nullptr;
	Span*	 _prev = nullptr;
};

// ʹ��ͷ������˫��ѭ������, ���Ч��
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

// �����ϵͳ������ҳΪ��λ�Ŀռ�
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