#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::Allocte(size_t size)
{
	// ������������Ҫ���ĸ��±����ҿռ�
	size_t index = SizeClass::ListIndex(size);
	FreeList& freeList = _freeLists[index];
	if (!freeList.Empty())
	{
		return freeList.Pop();
	}
	else
	{
		// RoundUp�������Ŀռ�, ����Ƭ���Ƶ�freeList, �Ա�ϲ�
		return FetchFromCentralCache(SizeClass::RoundUp(size));
	}
}

void ThreadCache::Deallocte(void* ptr, size_t size)
{
	size_t index = SizeClass::ListIndex(size); // ?
	FreeList& freeList = _freeLists[index];
	freeList.Push(ptr);

	// �����������һ������ | �ڴ��С
	size_t num = SizeClass::NumMoveSize(size);
	if (freeList.Num() >= num)
	{
		ListTooLong(freeList, num, size);
	}
}

void ThreadCache::ListTooLong(FreeList& freeList, size_t num, size_t size)
{
	void* start = nullptr, *end = nullptr;
	freeList.PopRange(start, end, num);

	NextObj(end) = nullptr;
	CentralCache::GetInstance().ReleaseListToSpans(start, size);
}

void* ThreadCache::FetchFromCentralCache(size_t size)
{
	// NumMoveSize�õ�Ҫ����ռ�Ĵ�С
	size_t num = SizeClass::NumMoveSize(size);

	void* start = nullptr, *end = nullptr;
	// ��centralCache�еõ�����Ŀռ�
	// ��centralCache�ڱ�����δ����, ����д���, ����ֱ�ӵ����������
	size_t actualNum = CentralCache::GetInstance().FetchRangeObj(start, end, num, size);

	if (actualNum == 1)
	{
		return start;
	}
	else
	{
		size_t index = SizeClass::ListIndex(size);
		FreeList& list = _freeLists[index];
		list.PushRange(NextObj(start), end, actualNum-1);

		return start;
	}
}
