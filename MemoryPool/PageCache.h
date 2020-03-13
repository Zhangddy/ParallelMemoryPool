#pragma once
#include "Common.h"

class PageCache
{
public:
	// 
	Span* _NewSpan(size_t numpage);
	Span* NewSpan(size_t numpage);
	void ReleaseSpanToPageCache(Span* span);

	Span* GetIdToSpan(PAGE_ID id);

	static PageCache& GetInstance()
	{
		static PageCache PageInst;
		return PageInst;
	}
private:
	// 使用单例模式
	PageCache()
	{ }
	PageCache(const PageCache&) = delete;

	SpanList _spanLists[MAX_PAGES];
	std::unordered_map<PAGE_ID, Span*> _idSpanMap;

	std::mutex _mtx;
};
