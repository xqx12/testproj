1.7 MemoryChecker插件
--------------------------------------

### 1.7.1 前置知识

#### 1.7.1.1 softmmu机制中ld和st操作的函数定义

qemu中实现内存管理时，提供了SoftMMU机制。而在QEMU的softmmu代码中很多函数的功能是相似的，只是处理的数据类型不同，例如定义ld和st操作时，softmmu\_defs.h文件中声明的函数有16个：

  uint8_t __ldb_mmu(target_ulong addr, int mmu_idx);
	void __stb_mmu(target_ulong addr, uint8_t val, int mmu_idx);
	uint16_t __ldw_mmu(target_ulong addr, int mmu_idx);
	void __stw_mmu(target_ulong addr, uint16_t val, int mmu_idx);
	uint32_t __ldl_mmu(target_ulong addr, int mmu_idx);
	void __stl_mmu(target_ulong addr, uint32_t val, int mmu_idx);
	uint64_t __ldq_mmu(target_ulong addr, int mmu_idx);
	void __stq_mmu(target_ulong addr, uint64_t val, int mmu_idx);
  
	uint8_t __ldb_cmmu(target_ulong addr, int mmu_idx);
	void __stb_cmmu(target_ulong addr, uint8_t val, int mmu_idx);
	uint16_t __ldw_cmmu(target_ulong addr, int mmu_idx);
	void __stw_cmmu(target_ulong addr, uint16_t val, int mmu_idx);
	uint32_t __ldl_cmmu(target_ulong addr, int mmu_idx);
	void __stl_cmmu(target_ulong addr, uint32_t val, int mmu_idx);
	uint64_t __ldq_cmmu(target_ulong addr, int mmu_idx);
	void __stq_cmmu(target_ulong addr, uint64_t val, int mmu_idx);


其中以ld操作为例，\_\_ld函数分为mmu和cmmu两类，每种类型又根据返回类型分为4个类型，例如返回值为uint8\_t时，函数名以\_\_ldb开头，表示字节操作。同理，st操作也有相同的特点。根据这个特点，定义这些函数只用了两个函数体，在softmmu\_template.h文件中定义：



	#define DATA_SIZE (1 << SHIFT)
	
	#if DATA_SIZE == 8
	#define SUFFIX q
	#define USUFFIX q
	#define DATA_TYPE uint64_t
	#elif DATA_SIZE == 4
	#define SUFFIX l
	#define USUFFIX l
	#define DATA_TYPE uint32_t
	#elif DATA_SIZE == 2
	#define SUFFIX w
	#define USUFFIX uw
	#define DATA_TYPE uint16_t
	#elif DATA_SIZE == 1
	#define SUFFIX b
	#define USUFFIX ub
	#define DATA_TYPE uint8_t
	#else
	#error unsupported data size
	#endif
	
	static DATA_TYPE glue(glue(slow_ld, SUFFIX), MMUSUFFIX)(target_ulong addr,
														  int mmu_idx,
														void *retaddr)
	{
		...
	}
	
	#ifndef SOFTMMU_CODE_ACCESS
	
	static void glue(glue(slow_st, SUFFIX), MMUSUFFIX)(ENV_PARAM
												   target_ulong addr,
												   DATA_TYPE val,
												   int mmu_idx,
												   void *retaddr)
	{
		...
	}

其中glue是一个宏定义(osdep.h)：

	#define xglue(x, y) x ## y
	#define glue(x, y) xglue(x, y)



在exec.c和op\_helper.c中真正实际定义了这些函数，例如在op\_helper.c中定义了mmu系列的函数：



	#define MMUSUFFIX _mmu
	
	#define SHIFT 0
	#include "softmmu_template.h"
	
	#define SHIFT 1
	#include "softmmu_template.h"
	
	#define SHIFT 2
	#include "softmmu_template.h"
	
	#define SHIFT 3
	#include "softmmu_template.h"



这相当于定义了ld和st操作的所有mmu系列的8个函数。



在由guest binary转化为tcg时，遇到ld和st操作就会调用上述的函数，在这些函数中会调用S2E\_TRACE\_MEMORY()，定义如下：
	
	#ifdef S2E_LLVM_LIB	 //符号执行
	#define S2E_TRACE_MEMORY(vaddr, haddr, value, isWrite, isIO) \
		tcg_llvm_trace_memory_access(vaddr, haddr, \
			value, 8*sizeof(value), isWrite, isIO);
	#define S2E_FORK_AND_CONCRETIZE(val, max) 
			tcg_llvm_fork_and_concretize(val, 0, max)
	
	#else // S2E_LLVM_LIB   //具体执行
	#define S2E_TRACE_MEMORY(vaddr, haddr, value, isWrite, isIO) \
		s2e_trace_memory_access(vaddr, haddr, \
			(uint8_t*) &value, sizeof(value), isWrite, isIO);
	#define S2E_FORK_AND_CONCRETIZE(val, max) (val)
	#endif // S2E_LLVM_LIB



在符号执行时它定义为 tcg\_llvm\_trace\_memory\_access()，在具体执行时，它定义为
s2e\_trace\_memory\_access()。

不管实际调用的是哪个函数，都会发送CorePlugin的onDataMemoryAccess信号。这样，MemoryChecker插件连接这个信号，进行相应的内存检查。


### 1.7.2 初识MemoryChecker插件


检查程序访存错误的插件。需要在程序进行st或者ld操作的时候显式的添加对所操作的内存进行监视。


### 1.7.3 MemoryChecker 的使用


配置文件的写法：

	pluginsConfig.MemoryChecker = {

		checkMemoryErrors = true,
		checkMemoryLeaks  = true,
		checkResourceLeaks = true,
		terminateOnErrors  = true,
		terminateOnLeaks   = true,
		//标记是否需要记录在MemoryTracer插件的log文件中
		traceMemoryAccesses = false,

	}


依赖的插件："ModuleExecutionDetector", "ExecutionTracer", "MemoryTracer", "Interceptor"




### 1.7.4 MemoryChecker插件的分析

####1.7.4.1		初始化

MemoryChecker插件初始化时主要工作是连接ModuleExecutionDetector插件的onModuleTransition信号，和CorePlugin的onStateSwitch和onException信号。

 

**[1]onModuleTransition**



处理onModuleTransition信号的函数是MemoryChecker::onModuleTransition()，这个函数的主要工作是连接CorePlugin的onDataMemoryAccess信号。

 

连接onDataMemoryAccess信号后，调用函数MemoryChecker::onDataMemoryAccess()进行相应处理。这个函数的主要工作如下：

 

a、判断是否进行访存检查，如果不检查则返回；在执行中断或处理异常、对符号内存进行访存时直接返回，不进行访存检查；

 

b、进行预检查，发送OnPreCheck信号；预检查是预留给其他插件的信号，其他插件连接后可以进行更细粒度的检查。[可以编写新的插件]

 

c、调用checkMemoryAccess()函数进行内存访问的简单检查；

 

d、如果内存访问检查有错误，则发送OnPostCheck信号。其他插件可以连接这个信号，进行后续的检查，目前StackChecker插件就是连接这个信号的。[也可以编写新的插件对这个信号连接，检查访存错误]

 

函数中最主要的是调用了checkMemoryAccess()函数对访存错误进行检查，返回标志hasError=true表示有错误，hasError=false表示没有错误。

 

函数主要完成以下工作：

 



a、判断ld或者st的对象值的长度是否是0或者负数；

	if(size != uint64_t(-1) && start + size < start) {
		err << "MemoryChecker::checkMemoryAccess: "
			<< "BUG: freeing region of " << (size == 0 ? "zero" : "negative")
			<< " size!" << std::endl;
		hasError = true;
		break;
	}

b、判断ld或者st指令的操作地址是否在事先定义的需要监视的范围内：（说法不一定准确）

 

MemoryChecker中维持了一个内存区域映射，映射的组织是一个二叉树结构,按照地址大小存储，左节点比右节点的地址小。其中，内存映射MemoryMap的定义：

	typedef klee::ImmutableMap<MemoryRange, const MemoryRegion*,
			MemoryRangeLT> MemoryMap;
	struct MemoryRange {
			uint64_t start;
			uint64_t size;
	};
	struct MemoryRegion {
			MemoryRange range;				 //内存区域
			MemoryChecker::Permissions perms;	//内存区域的访问权限
			uint64_t allocPC;
			std::string type;		  //自己设定的内存区域类型，
			uint64_t id;							   //标志ID，一般用地址
			bool permanent;						//标志是否是永久的
	};
	
	struct MemoryRangeLT {
			bool operator()(const MemoryRange& a, const MemoryRange& b) const {
					return a.start + a.size <= b.start;
			}
	};




MemoryChecker插件在内存映射中查找访存的地址是否存在访存错误。

	MemoryMap &memoryMap = plgState->getMemoryMap();
	MemoryRange range = {start, size};
	//找到内存映射树中的符合要求的节点，lookup_previous()函数的实现见后面
	const MemoryMap::value_type *res = memoryMap.lookup_previous(range);
	
	
	if(!res) {
			//在memory map中找不到比start地址小的地址，也就是说memory tree中没有登记start开始的内存
			err << "MemoryChecker::checkMemoryAccess: "
				<< "BUG: memory range at " << hexval(start) << " of size " << hexval(size)
				<< " cannot be accessed by instruction " << getPrettyCodeLocation(state)
				<< ": it is not mapped!" << std::endl;
			hasError = true;
			break;
	}
	if(res->first.start + res->first.size < start + size) {
			//访存的数据大小大于树中登记的内存
			err << "MemoryChecker::checkMemoryAccess: "
				<< "BUG: memory range at " << hexval(start) << " of size " << hexval(size)
				<< " can not be accessed by instruction " << getPrettyCodeLocation(state)
				<< ": it is not mapped!" << std::endl
  		  		<< "  NOTE: closest allocated memory region: " << *res->second << std::endl;
 		   hasError = true;
			break;
	}
	if((perms & res->second->perms) != perms) {
			//访问权限不匹配
			err << "MemoryChecker::checkMemoryAccess: "
				<< "BUG: memory range at " << hexval(start) << " of size " << hexval(size)
				<< " can not be accessed by instruction " << getPrettyCodeLocation(state)
				<< ": insufficient permissions!" << std::endl
				<< "  NOTE: requested permissions: " << hexval(perms) << std::endl
				<< "  NOTE: closest allocated memory region: " << *res->second << std::endl;
			hasError = true;
			break;
	}




这里首先调用getMemoryMap()来得到内存区域，然后调用lookup\_previous()函数来查找内存映射树中比start地址小的最大的地址的节点。lookup\_previous()代码实现如下：

	//在tree中查找如果能找到K,则返回k；找不到则返回K的前一个节点（树中所有节点中比K小的最大的节点）
	ImmutableTree<K,V,KOV,CMP>::lookup_previous(const key_type &k) const {

		Node *n = node;
		Node *result = 0;
		while (!n->isTerminator()) {
				key_type key = key_of_value()(n->value);
				//key_compare()(k,key)的作用表示，如果k<key，返回true；如果k>key，返回false
				if (key_compare()(k, key)) {
						n = n->left;
				} else if (key_compare()(key, k)) {
						result = n;
						n = n->right;
				} else {
						return &n->value;
				}
		}
		return result ? &result->value : 0;
	}




要想成功使用MemoryChecker插件，需要首先在其他插件中事先在内存映射memoryMap中登记需要监视的内存区域，如果不在其他插件中调用MemoryChecker的相应的函数来设置内存区域，那么树是空的，这就解释了为什么在测试Linux中的echo程序中出现那么多“it
is not mapped”的警告。设置内存区域的函数是setMemoryMap()函数。需要在自己的插件中直接或者间接调用setMemoryMap()函数，来设置需要监视的内存区域。以下列出了MemoryChecker中一些调用setMemoryMap()的函数。一般使用grantMemory()函数登记需要监视的内存区域。下一节会介绍grantMemory()函数。

 

![](plugin.files/image005.jpg)


**[2]onStateSwitch**

 

处理这个信号的函数是MemoryChecker::onStateSwitch()。处理这个信号的过程和处理onModuleTransition信号的过程一样。

 

**[3]onException**

 

处理这个信号的函数MemoryChecker::onException()。这个函数只将与CorePlugin信号的连接切断




####1.7.4.2		其他函数

 

**[1] grantMemory函数**

 

这个函数的作用是可以对我们指定的内存区域进行认证，确保在使用MemoryChecker插件时可以对这些内存区域访问。一般用于申请内存空间时使用。


	void MemoryChecker::grantMemory(S2EExecutionState *state,
					uint64_t start, uint64_t size, Permissions perms,
					const std::string &regionType, uint64_t regionID,
					bool permanent)
	{
			DECLARE_PLUGINSTATE(MemoryCheckerState, state);
			MemoryMap &memoryMap = plgState->getMemoryMap();

			//生成新的MemoryRegion对象，用于存储需要认证的内存区域
			MemoryRegion *region = new MemoryRegion();
			region->range.start = start;
			region->range.size = size;
			region->perms = perms;
			region->allocPC = state->getPc();
			region->type = regionType;
			region->id = regionID;
			region->permanent = permanent;

			s2e()->getDebugStream(state) << "MemoryChecker::grantMemory("
				<< *region << ")" << '\n';
		
			/********************************************/
			/* Write a log entry about the grant event */
			//ExecutionTracer插件为grant事件记录一个log入口，此段关系不大，暂时省略
			/********************************************/
		
			//判断需要认证的内存区域大小是否为0或者负数
			if(size == 0 || start + size < start) {
					s2e()->getWarningsStream(state) << "MemoryChecker::grantMemory: "
						<< "detected region of " << (size == 0 ? "zero" : "negative")
						<< " size!" << std::endl
						<< "This probably means a bug in the OS or S2E API annotations" << std::endl;
					delete region;
					return;
			}
		
			//判断需要认证的内存区域是否与现有的区域重叠
			const MemoryMap::value_type *res = memoryMap.lookup_previous(region->range);
			if (res && res->first.start + res->first.size > start) {
					s2e()->getWarningsStream(state) << "MemoryChecker::grantMemory: "
					<< "detected overlapping ranges!" << '\n'
					<< "This probably means a bug in the OS or S2E API annotations" << '\n'
					<< "NOTE: requested region: " << *region << '\n'
					<< "NOTE: overlapping region: " << *res->second << '\n';
					delete region;
					return;
			}
		
			//将新的MemoryRegion对象存入memoryMap中
			plgState->setMemoryMap(memoryMap.replace(std::make_pair(region->range, region)));
		
	}

