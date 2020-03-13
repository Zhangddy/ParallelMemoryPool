## 项目简介
---
> **在多线程并发场景下, 若果多个线程同时使用动态内存分配malloc函数时, 此时线程是安全. 但是需要注意的是malloc为了保证多线程的线程安全, 会使用锁来保护临界资源. 这样多个线程同时去争抢等待一个锁, 会导致程序效率和并行性的降低.**

> **malloc作为内存池在现在CPU内核数普遍提升的情况下, 并行性是它最弱势的地方. 因此, 本项目通过设置线程局部存储技术, 为每个线程设置自身的小型内存池, 并通过上层对内存空间的的分配合并, 减少了内存碎片. 实现了一个可并行内存池**
## 项目技术
---
* 单例模式
* C++11
* TLS
* 多线程
## 项目实现
---

#### 下层 ThreadCache
**下层是直接与线程交互的一层, 通过TLS(线程局部存储技术). 使得每一线程都拥有自己的ThreadCache. 结构如图所示**
![在这里插入图片描述](https://img-blog.csdnimg.cn/2020031315391327.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L25ld19iZWVfMDE=,size_16,color_FFFFFF,t_70)
**在进行空间分配时, 先将所需要的空间大小(向8对齐的算法)得到, 再映射到相应的地点, 再判断有无可用空间. 有空闲空间则直接返回, 若无可用空间则向中层CentralCache索要空间**
* ThreadCache结构
```cpp
class ThreadCache
{
public:
	// 申请内存和释放内存
	void* Allocte(size_t size);
	void Deallocte(void* ptr, size_t size);

	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index);

	// 如果自由链表中对象超过一定长度就要释放给中心缓存
	void ListTooLong(FreeList& freeList, size_t num, size_t size);
private:
	FreeList _freeLists[NFREE_LIST];

};
// TLS技术, 每一个线程都会有一个
_declspec (thread) static ThreadCache* pThreaCache = nullptr;

```

#### 中层 CentralCache
**CentralCache管理的是更大的空闲数据, 通过定义一个Span的结构体, 这个结构体可以控制若干字节的链表每一个span所管理的数据在未分配的情况下都能凑满整数个页, 同时span中还有一个usecount来记录所管理的页是否有正在被使用的情况, 通过引用计数来判断是否要将这些空间还给上层. .结构如图**
![在这里插入图片描述](https://img-blog.csdnimg.cn/20200313154756584.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L25ld19iZWVfMDE=,size_16,color_FFFFFF,t_70)
**CentralCache的作用是调度调配空间, 当下层进行空间申请时, CentralCache会查找需要申请的空间是否有合适的span去分配, 如果有则将这个span进行剪切并分给底层. 若没有则向上层PageCache去申请.**
* span结构

```cpp
struct Span
{
	PAGE_ID _pageid = 0; // 页号
	PAGE_ID _pagesize = 0;   // 页的数量

	FreeList _freeList;  // 对象自由链表
	size_t _objSize = 0; // 自由链表对象大小
 	int _usecount = 0;   // 内存块对象使用计数

	//size_t objsize;  // 对象大小
	Span* _next = nullptr;
	Span* _prev = nullptr;
};
```

* CentralCache结构
> **CentralCache应该是单例, 并且需要互斥访问**

```cpp
class CentralCache
{
public:
	// 从中心缓存获取一定数量的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t num, size_t size);

	// 将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t size);

	// 从spanlist 或者 page cache获取一个span
	Span* GetOneSpan(size_t size);

	static CentralCache& GetInstance()
	{
		static CentralCache inst;
		return inst;
	}

private:
	CentralCache()
	{}

	CentralCache(const CentralCache&) = delete;

	SpanList _spanLists[NFREE_LIST];
};
```

#### 上层 PageCache
**PageCache是一个以页为单位的span自由链表. 为了保证全局只有唯一的PageCache，这个类被设计成了单例模式**
**当CentralCache向PageCache申请内存时，PageCache先检查对应位置有没有span，如果没有则向更大页寻找一个span，如果找到则分裂成两个。比如：申请的是4page，4page后面没有挂span，则向后面寻找更大的span，假设在10page位置找到一个span，则将10page span分裂为一个4page span和一个6page span.如果找到128 page都没有合适的span，则向系统使用mmap、brk或者是VirtualAlloc等方式申请128page span挂在自由链表中**
![在这里插入图片描述](https://img-blog.csdnimg.cn/2020031316074772.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L25ld19iZWVfMDE=,size_16,color_FFFFFF,t_70)

**如果central cache释放回一个span，则依次寻找span的前后page id的span，看是否可以合并，如果合并继续向前寻找.这样就可以将切小的内存合并收缩成大的span，减少内存碎片**
