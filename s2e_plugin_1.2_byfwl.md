2.1????????????????? 栈的检查
-----------------------------

为了检查在memcpy函数中是否存在缓冲区溢出，开发VulMining插件用来对输入数据符号化和断言，调用StackMoniotr插件和StackChecker插件的相关函数，完成对memcpy函数的检查。

### 2.1.1 总体思路

1、调用StackMonitor插件得到进程的栈空间以及各个栈针。（进程相关）

?

2、定位memcpy函数。(两种编译方式：内联和库调用)

?

3、根据memcpy函数中第一个参数（目的地址指针）调用修改后的StackChecker插件来判断处于栈空间的哪一个栈针中，得到该栈针的大小。

?

4、结合memcpy断言，对memcpy函数的地三个参数（拷贝缓冲区的大小）来判断是否存在缓冲区溢出漏洞。（符号信息直接传递）????????

### 2.1.2 使用方法

需要用到VulMining插件、StackMonitor及StackChecker插件。

StackMonitor?
插件以及StackChecker插件的用法前面已经介绍过。这里对其进行了一些修改。

### 2.1.3 动态得到栈针大小实现方法

1、在VulMining插件中定义一个onStackCheck信号，用来在检测到memcpy指令时，通知StackChecker插件查找指针所处栈针，并且得到该栈针的大小。

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
sigc::signal<void,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
S2EExecutionState *,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
uint64_t /* virtual address */,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
uint64_t /* size */,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
StackFrameInfo * /* stackframe */>
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
onStackCheck;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

2、修改StackChecker插件，不使用MemoryChecker插件的onPostCheck信号，改用VulMining插件中定义的onStackCheck信号。在StackChecker插件初始化时连接AssertExpert插件的onStackCheck信号，完成相应的处理。

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
void StackChecker::initialize()
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????...
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? m_vulMining->onStackCheck.connect(
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? sigc::mem_fun(*this, &StackChecker::onMemoryAccess));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
}
~~~~

3、修改StackChecker插件的onMemoryAccess函数，maxSize参数为address对应栈针的大小，如果address不在栈空间内，或者没有得到address所对应的栈针，则将maxSize设置为固定值0x20。onMemoryAccess函数的声明如下：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
void StackChecker::onMemoryAccess(S2EExecutionState *state, uint64_t address,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
uint64_t size, uint64_t *maxSize)
~~~~

得到maxSize之后，交给memcpy断言，来判断是否存在栈溢出(拷贝长度是否大于栈帧大小)。

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
klee::ref<klee::Expr> cond = klee::SgtExpr::create(symValue, klee::ConstantExpr::create(/*0x20*/maxSize, symValue.get()->getWidth()));
~~~~

?

2.2????????????????? 堆的检查
-----------------------------

借鉴栈溢出检测插件的思想，写出HeapMonitor和HeapChecker插件，用来动态得到进程堆内存情况，检测堆溢出。

### 2.2.1 前置知识

堆是一种在程序运行时动态分配的内存，由程序员在使用堆时根据需要用专门的函数，如malloc等，进行动态申请。为了高效的管理堆内存，现代操作系统的堆数据结构一般包括堆块和堆表两类，如图1所示。

![说明:
http://192.168.5.132/cgi-bin/twiki/viewfile/Main/S2E%e4%b8%ad%e5%a0%86%e7%9a%84%e6%a3%80%e6%9f%a5---%e9%92%88%e5%af%b9memcpy%e5%87%bd%e6%95%b0%e7%9a%84%e6%a3%80%e6%9f%a5?rev=1;filename=%e5%a0%86%e7%9a%84%e5%86%85%e5%ad%98%e7%bb%84%e7%bb%87.png](pluginv0.2.files/image009.jpg)

图1

?

堆块：堆区的内存按照不同大小组织成块，以堆块为单位进行标识。每个堆块包括块首和块身，块首记录了堆块自身信息，例如本块的大小、空闲还是占用等信息；块身紧随其后，也是最终分配给用户使用的数据区。堆块的结构如图2所示。

![说明:
http://192.168.5.132/cgi-bin/twiki/viewfile/Main/S2E%e4%b8%ad%e5%a0%86%e7%9a%84%e6%a3%80%e6%9f%a5---%e9%92%88%e5%af%b9memcpy%e5%87%bd%e6%95%b0%e7%9a%84%e6%a3%80%e6%9f%a5?rev=1;filename=%e5%a0%86%e5%9d%97%e6%95%b0%e6%8d%ae%e7%bb%93%e6%9e%84.png](pluginv0.2.files/image011.jpg)

图2

?

?

堆表：堆表一般位于堆区的起始位置，用于索引堆区中所有堆块的重要信息。在windows中，占用态的堆块被使用它的程序索引，而堆表只索引所有空闲的堆块。

?

空闲堆块由双向链表组织，按照堆块大小的不同，空表分为128条。如图3所示。空闲堆块的大小=索引项（ID）\*8（字节）。如free[1]标识了堆区中所有大小为8字节的空闲堆块。需要注意的是free[0]相对比较特殊，这条双向列表标识了所有大于等于1024字节的堆块。

?

?

![说明:
http://192.168.5.132/cgi-bin/twiki/viewfile/Main/S2E%e4%b8%ad%e5%a0%86%e7%9a%84%e6%a3%80%e6%9f%a5---%e9%92%88%e5%af%b9memcpy%e5%87%bd%e6%95%b0%e7%9a%84%e6%a3%80%e6%9f%a5?rev=1;filename=%e7%a9%ba%e9%97%b2%e5%8f%8c%e5%90%91%e9%93%be%e8%a1%a8.png](pluginv0.2.files/image013.jpg)

图3

?

堆管理系统的三类操作：堆快分配、堆块释放、堆块合并归根结底都是堆链表的修改，分配就是将堆块从空表中“卸下”，释放就是把堆块“链入”空表，合并可以看作是把若干个堆块先从空表中“卸下”，修改块首信息，之后把更新后的新块“链入”空表。所有”卸下“和”链入“堆快的工作都发生在链表中，如果能够伪造链表节点的指针，在”卸下“和”链入“的过程中就有可能获得以此读写内存的机会。
堆溢出原理如图4所示。

![说明:
http://192.168.5.132/cgi-bin/twiki/viewfile/Main/S2E%e4%b8%ad%e5%a0%86%e7%9a%84%e6%a3%80%e6%9f%a5---%e9%92%88%e5%af%b9memcpy%e5%87%bd%e6%95%b0%e7%9a%84%e6%a3%80%e6%9f%a5?rev=1;filename=%e5%a0%86%e6%ba%a2%e5%87%ba%e5%8e%9f%e7%90%86.png](pluginv0.2.files/image015.jpg)

图4

?

堆溢出利用的精髓就是用精心构造的数据去溢出下一个堆块的块首，改写块首中的前向指针和后向指针，然后在分配、释放、合并等操作发生时伺机获得一次向内存任意地址写入任意数据的机会。

?

由上面的知识，我们检测堆溢出的思路如下：

?

1、对”卸下“操作，即内存分配。对计算出能够以超长字节覆盖的堆块，判断其物理相连的下一个堆块是否是空闲堆块，如果满足既是空闲堆块，又在程序后期运行过程中分配出去，则可能发生可利用的堆溢出。

?

2、对”链入“操作，即内存释放。对计算出能够以超长字节覆盖的堆块，判断其物理相连的下一个堆块是否是占用堆块，如果满足既是占用堆块，又在程序后期运行过程中释放，则可能发生可利用的堆溢出。

?

（暂时没有考虑合并操作）

?

### 2.2.2 整体思路

1、利用HeapMonitor插件得到进程的堆区及各个堆块。（进程相关）

?

2、定位memcpy函数。(两种编译方式：内联和库调用)

?

3、根据memcpy函数中第一个参数（目的地址指针），利用HeapChecker插件找到目的地址处于哪一个堆块，得到该堆块的大小。

?

4、结合memcpy断言，对memcpy函数的地三个参数（拷贝缓冲区的大小）来判断是否存在缓冲区溢出漏洞。（符号信息直接传递）

### 2.2.3 HeapMonitor[?](http://192.168.5.132/cgi-bin/twiki/edit/Main/HeapMonitor?topicparent=Main.S2E中堆的检查---针对memcpy函数的检查 "创建这个主题") 插件设计思路及实现

整体思路:

动态对进程堆区和堆块进行建模。堆区的获得通过PEB结构和HEAP结构获取；堆块的获得通过挂钩RtlAllocateHeap函数获取。

?

2.2.3.1??????? 数据结构

?

堆区描述：

每个进程都有自己独立的若干个堆区，使用如下数据结构记录每个进程的每个堆区：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
typedef std::map<PidHeapBase , Heap> Heaps;
~~~~

其中PidHeapBase是std::pair类型，是进程的PID号和每个堆区基址的键值对，定义如下：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
typedef std::pair<uint64_t , uint64_t> PidHeapBase;
~~~~

Heap是一个类，记录了一个堆区的信息（主要是堆区基址以及堆区大小），定义了一些方法（主要包括堆块的更新和新堆块的生成），主要定义如下：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
class Heap {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? public:
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint64_t m_heapBase;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint64_t m_heapSize;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????HeapBlocks m_blocks;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????public:
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? //堆的构造函数
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? Heap(S2EExecutionState *state,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? HeapMonitorState *plgState,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint64_t pc,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint64_t base, uint64_t size) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????m_heapBase = base;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? m_heapSize = size;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????const ModuleDescriptor *module = plgState->m_detector->getModule(state, pc);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? assert(module && "BUG: HeapMonitor should only track configured modules");
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????uint64_t getHeapBase() const {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return m_heapBase;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????uint64_t getHeapSize() const {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return m_heapSize;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????/** 遇到RtlAllocateHeap函数时调用此函数，新生成一个堆块 */
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? void newBlock(S2EExecutionState *state, unsigned currentModuleId, uint64_t pc, uint64_t blockAddress , uint32_t size) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????assert(blockAddress >= m_heapBase && blockAddress < (m_heapBase + m_heapSize));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????HeapBlock block;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? block.moduleId = currentModuleId;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? block.BlockAddress = blockAddress;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? block.BlockSize = size;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? m_blocks.push_back(block);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????/*遇到free函数调用此函数，释放掉一个堆块*/
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? void update(S2EExecutionState *state, unsigned currentModuleId, uint64_t blockAddress , uint32_t size) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? assert(!m_blocks.empty());
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? assert(blockAddress >= m_heapBase && blockAddress < (m_heapBase + m_heapSize));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????HeapBlock p;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? p.BlockAddress = blockAddress;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? ????????p.BlockSize = size;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? p.moduleId = currentModuleId;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????HeapBlocks::iterator it = m_blocks.begin();
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????unsigned i = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? while (i < m_blocks.size()) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? if ((m_blocks[i].moduleId == p.moduleId) && (m_blocks[i].BlockAddress == p.BlockAddress) && (m_blocks[i].BlockSize == p.BlockSize)) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? m_blocks.erase(it + i);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? break;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?? ?????????????????????} else {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? ++i;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????bool empty() const {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return m_blocks.empty();
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????bool getBlock(uint64_t blockAddress, bool &blockValid, HeapBlock &blockInfo) const {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? if (blockAddress < m_heapBase? || (blockAddress >= m_heapBase + m_heapSize)) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? return false;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????blockValid = false;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????//Look for the right frame
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //XXX: Use binary search?
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? foreach2(it, m_blocks.begin(), m_blocks.end()) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? const HeapBlock &block= *it;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? if (blockAddress != block.BlockAddress) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? continue;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????????????blockValid = true;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? blockInfo = block;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?????????? ?????????????break;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????return true;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????friend std::ostream& operator<<(std::ostream &os, const Heap &stack);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
};
~~~~

2.2.3.2??????? 获得堆区的实现

?

堆区基址以及大小的动态获得的过程如图5所示。

?

![说明:
http://192.168.5.132/cgi-bin/twiki/viewfile/Main/S2E%e4%b8%ad%e5%a0%86%e7%9a%84%e6%a3%80%e6%9f%a5---%e9%92%88%e5%af%b9memcpy%e5%87%bd%e6%95%b0%e7%9a%84%e6%a3%80%e6%9f%a5?rev=1;filename=%e5%a0%86%e5%8c%ba%e5%9f%ba%e5%9d%80%e8%8e%b7%e5%be%97.png](pluginv0.2.files/image017.jpg)

?

图5

?

?

每个进程都有PEB结构，位于TIB结构的0x30偏移处，PEB结构中记录了有关该进程各个堆区的情况。为了得到堆区，定义结构体PEB\_HEAP如下（仅定义了与堆信息相关的字段）：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
typedef struct _PEB_HEAP {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? PEB32 peb32;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t Unk1[30];? //
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t NumberOfHeaps;???????????????????????????? // 88h, 指出当前进程中存在多少个堆区
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t MaximumNumberOfHeaps;????????????????????? // 8Ch， 指出当前进程最多可以存在多少个堆区
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t ProcessHeaps;??????????????????????????? // 90h， 指向存储当前进程所有堆区句柄（基址）的内存区域
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
}__attribute__((packed))PEB_HEAP;
~~~~

?

循环读取ProcessHeaps字段指向的各个堆区基址，每个基址都指向一个堆结构，堆结构的定义如下（仅定义了与堆区大小相关的字段）：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
typedef struct _HEAP32 {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t Unk1[6];
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t SegmentReserve;? //堆区保留大小（最大大小）
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t SegmentCommit;??????? //堆区提交大小（已经提交大小，可用大小）
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
}__attribute__((packed))HEAP32;
~~~~

堆区可能会随着程序的运行而扩展大小，通过这个结构可以动态获得每个堆区的大小。

?

获得堆区的具体函数实现如下：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
void HeapMonitorState::updateHeapArea(S2EExecutionState *state, uint64_t pc)
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? const ModuleDescriptor *module = m_detector->getModule(state, pc);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? assert(module && "BUG: unknown module");
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????uint32_t numberOfHeaps = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t processHeaps = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? m_monitor->getNumberOfHeaps(state , &numberOfHeaps);????????? //通过PEB_HEAP结构得到某个进程中堆区的个数
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? m_monitor->getProcessHeaps(state , &processHeaps);???????????????? //通过PEB_HEAP结构得到某个进程中的ProcessHeaps结构的地址。
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????for(int i = 0; i < numberOfHeaps ; i++){
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? uint32_t each_heap = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? s2e::windows::HEAP32 Heap32;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????state->readMemoryConcrete(processHeaps + i*sizeof(uint32_t), &each_heap, sizeof(uint32_t));????? //读出每个堆区的基址
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? state->readMemoryConcrete(each_heap , &Heap32 , sizeof(Heap32));???????????????????????????????????????????? //读出每个堆区的结构
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????uint64_t pid = m_monitor->getPid(state, pc);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //如果是不同的堆区
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? if ((pid != m_pid) || !(each_heap >= m_cachedHeapBase && each_heap < (m_cachedHeapBase + m_cachedHeapSize))) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? m_pid = pid;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????????????m_cachedHeapBase = each_heap;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????? ??????????????????m_cachedHeapSize = Heap32.SegmentCommit;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????//加入m_heaps向量中
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? PidHeapBase p = std::make_pair(pid, m_cachedHeapBase);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????//加入到m_heaps向量中
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? Heaps::iterator heapit = m_heaps.find(p);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? if (heapit == m_heaps.end()) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? Heap heap(state, this, pc, m_cachedHeapBase, m_cachedHeapSize);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? m_heaps.insert(std::make_pair(p, heap));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? heapit = m_heaps.find(p);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
}
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

?

?

2.2.3.3??????? 获得堆块的实现

?

堆块的分配函数malloc(), LocalAlloc? (), GlobalAlloc? (), HeapAlloc?
()等函数最终都是通过ntdll.dll中的RtlAllocateHeap()函数实现的，因此，我们监控这个函数，得到其各个参数，就能得到所申请的每个堆块的基址和大小了。

监控堆快的释放free()函数, 将释放的堆块从向量中删除。

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
void HeapMonitorState::updateHeapBlocks(S2EExecutionState *state, uint64_t pc , bool isFree)
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? g_s2e->getMessagesStream() << "HeapMonitorState::updateHeapBlocks at pc:" << hexval(pc) << "\n";
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????const ModuleDescriptor *module = m_detector->getModule(state, pc);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? assert(module && "BUG: unknown module");
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//EAX寄存器中存放的是申请的堆块的基址
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint32_t eax = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? state->readCpuRegisterConcrete(offsetof(CPUState, regs[R_EAX]), &eax, sizeof(eax));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? g_s2e->getMessagesStream() << "HeapMonitorState::eax :" << hexval(eax) << "\n";
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? if(eax >= 0x80000000){
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????uint64_t sp , size , hHeap = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? sp = state->getSp();
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????? ??//ESP-0x4存放的是申请的内存大小
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? state->readMemoryConcrete(sp - 0x4, &size, sizeof(uint32_t));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? //ESP-0xc存放的是申请的内存在哪个堆句柄中
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? state->readMemoryConcrete(sp - 0xc, &hHeap, sizeof(uint32_t));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? g_s2e->getMessagesStream() << "HeapMonitorState::hHeap :" << hexval(hHeap) << "\n";
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????uint64_t pid = m_monitor->getPid(state, pc);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//added by cdboot 20120910
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? //动态得到当前各个堆区的信息
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? updateHeapArea(state , pc);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//得到eax即新申请到的堆块的地址所在的堆区的基址
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?? ?????uint64_t tmpHeapBase = getHeapBase(state , pid , eax);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//查看新申请到的堆块是否
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? PidHeapBase p = std::make_pair(pid, tmpHeapBase);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? Heaps::iterator heapit = m_heaps.find(p);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? assert((heapit != m_heaps.end()) && "BUG: not in any heap area!");
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????Heap &heap = (*heapit).second;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????unsigned moduleId = m_moduleCache.getId(*module);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????if(hHeap != tmpHeapBase){
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????if (!isFree) {??? //是申请内存操作，则新建一个堆块
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? heap.newBlock(state, moduleId, pc, eax , size);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? } else {??????????? //是释放内存操作，则将该堆块从向量中删除
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //note2：free()函数还没有处理
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? (*heapit).second.update(state, moduleId, eax , size);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??? ????}
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????if ( m_debugMessages) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? g_s2e->getDebugStream() << (*heapit).second << "\n";
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
}
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

### 2.2.4 HeapChecker[?](http://192.168.5.132/cgi-bin/twiki/edit/Main/HeapChecker?topicparent=Main.S2E中堆的检查---针对memcpy函数的检查 "创建这个主题") 插件设计思路及实现

整体思路：

?

接收处理不安全函数时（memcpy函数）的信号，根据拷贝的目的地址找到目的地址所在的堆区，判断与其物理相邻的堆区的状态（空闲或占用），之后根据2.2.1中的两种情况分别判断是否会发生堆溢出。

?

现在的版本较为简单，需要改进。

?

检测堆溢出的代码如下：

?

?

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
void HeapChecker::onMemoryAccess(S2EExecutionState *state, uint64_t address,
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
uint64_t size, HeapBlockInfo *info)
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????uint64_t heapBase = 0, heapSize = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? uint64_t pid = state->getPid();
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????heapBase = m_heapMonitor->getHeapBase(state , pid , address);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? heapSize = m_heapMonitor->getHeapSize(state , pid , heapBase);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//判断是DEBUG版本还是RELEASE版本
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? bool isDebug = m_heapMonitor->isBuild(state , pid);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//粗略判断当前地址是否在堆空间内
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? if (!(address >= heapBase && (address < heapBase + heapSize))) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //??????? *maxSize = 0x20;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //modified by cdboot 20120604
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? info->HeapAreaSize = 0x00000000;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????bool onTheHeap = false;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? //查找是否有当前地址所对应的堆区
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? bool res = m_heapMonitor->getBlockInfo(state, address, onTheHeap, *info);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//??? *maxSize = 0x20;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????//如果不再堆空间内，返回
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? if (!onTheHeap) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????? ??????m_heapMonitor->dump(state);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //modified by cdboot 20120604
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? info->BlockSize = 0x00000000;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????if (!res) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? std::stringstream err;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????info->BlockSize = 0x00000000;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? else{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //??? ???????? *maxSize = info.FrameSize;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //判断address之后物理相连的地址是否是空闲堆块
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? //modified by cdboot 20120903
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? uint64_t nextAddress = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? uint16_t allocateSize = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? if(!isDebug){
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? state->readMemoryConcrete(address - 8 , &allocateSize , sizeof(uint16_t));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? nextAddress = address - 8 + allocateSize*8;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }else{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? state->readMemoryConcrete(address - 20 , &allocateSize , sizeof(uint8_t));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? nextAddress = address - 20 + allocateSize*8;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????uint8_t flag = 0;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????if(address >= heapBase && (address < heapBase + heapSize))
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? if(!isDebug){
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? state->readMemoryConcrete(nextAddress + 5 , &flag , sizeof(uint8_t));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? }else{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? state->readMemoryConcrete(nextAddress + 5 , &flag , sizeof(uint8_t));
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????????????????????bool isBusy = flag & 0x1;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? if(isBusy){
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? info->BlockSize = 0x00000000;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }else{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? info->BlockSize = 0x00000000;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????????????? return;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

?

其中需要注意的是：release版本和debug版本编译的程序堆块结构稍有不同，所以增加了isBuildDebug()函数来判断，isBuilDebug()函数定义在WindowsMonitor.cpp中，主要是从程序的头部中读取相应字段判断是否是debug版本，代码如下：

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
bool WindowsMonitor::isBuildDebug(S2EExecutionState *s, const ModuleDescriptor &desc)
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
{
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? if (desc.Pid && s->getPid() != desc.Pid) {
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????????????? return false;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? }
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? 
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
????????WindowsImage Img(s, desc.LoadBase);
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? bool result = Img.getDebugFlag() & 0x0100;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
??????? return result;
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
}
~~~~

~~~~ {style="background:ivory;mso-layout-grid-align:none"}
?
~~~~

### 2.2.5 不足和改进

1、堆溢出中的几种情况需要进一步详细完善；

2、间接符号传递的问题暂时还没有解决；

3、插件与操作系统相关，需要在不同操作系统平台之间迁移。