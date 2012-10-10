1 S2E中的插件(作者：曹鼎；邮箱：caoding8483@163.com)
=================================================

1.1           S2E中插件的通信机制
----------------------------------------

在S2E构造函数中，会调用initPlugins()来初始化各个插件，其主要作用是这个函数主要是读取配置文件中的plugins域中设置的插件，并且检查这些插件是否依赖于其他插件。initPlugins()函数的实现如下：
   
void S2E::initPlugins()    
{    
        m_pluginsFactory = new PluginsFactory();
        
        //CorePlugin必不可少
        m_corePlugin = dynamic_cast<CorePlugin*>(
        m_pluginsFactory->createPlugin(this, "CorePlugin"));
        assert(m_corePlugin);
        
        m_activePluginsList.push_back(m_corePlugin);
        m_activePluginsMap.insert(
        make_pair(m_corePlugin->getPluginInfo()->name, m_corePlugin));
        if(!m_corePlugin->getPluginInfo()->functionName.empty())
        m_activePluginsMap.insert(
        make_pair(m_corePlugin->getPluginInfo()->functionName, m_corePlugin));
        
        //读取配置文件中的plugins域
        vector<string> pluginNames = getConfig()->getStringList("plugins");
        
        //对每个插件进行检查和装载
        /* Check and load plugins */
        foreach(const string& pluginName, pluginNames) {
                const PluginInfo* pluginInfo = m_pluginsFactory->getPluginInfo(pluginName);
                if(!pluginInfo) {
                        std::cerr << "ERROR: plugin '" << pluginName
                        << "' does not exists in this S2E installation" << std::endl;
                        exit(1);
                } else if(getPlugin(pluginInfo->name)) {
                        std::cerr << "ERROR: plugin '" << pluginInfo->name
                        << "' was already loaded "
                        << "(is it enabled multiple times ?)" << std::endl;
                        exit(1);
                } else if(!pluginInfo->functionName.empty() &&
                getPlugin(pluginInfo->functionName)) {
                        std::cerr << "ERROR: plugin '" << pluginInfo->name
                        << "' with function '" << pluginInfo->functionName
                        << "' can not be loaded because" << std::endl
                        <<  "    this function is already provided by '"
                        << getPlugin(pluginInfo->functionName)->getPluginInfo()->name
                        << "' plugin" << std::endl;
                        exit(1);
                } else {
                        Plugin* plugin = m_pluginsFactory->createPlugin(this, pluginName);
                        assert(plugin);
                        
                        m_activePluginsList.push_back(plugin);
                        m_activePluginsMap.insert(
                        make_pair(plugin->getPluginInfo()->name, plugin));
                        if(!plugin->getPluginInfo()->functionName.empty())
                        m_activePluginsMap.insert(
                        make_pair(plugin->getPluginInfo()->functionName, plugin));
                }
        }
        
        //检查插件是否有依赖
        /* Check dependencies */
        foreach(Plugin* p, m_activePluginsList) {
                foreach(const string& name, p->getPluginInfo()->dependencies) {
                        if(!getPlugin(name)) {
                                std::cerr << "ERROR: plugin '" << p->getPluginInfo()->name
                                << "' depends on plugin '" << name
                                << "' which is not enabled in config" << std::endl;
                                exit(1);
                        }
                }
        }
        
        //对每个插件进行初始化，这里的初始化函数initialize()是虚函数
        /* Initialize plugins */
        foreach(Plugin* p, m_activePluginsList) {
                p->initialize();
        }
}

不可缺少的是CorePlugin插件，它是所有插件的基础。其中定义了一系列信号，用于在检测到指令时给插件们发送信号（emit），相应的插件会进行连接（connect）并且调用相应的函数。CorePlugin中定义的信号在CorePlugin.h中：

/** Signal that is emitted on begining and end of code generation
for each QEMU translation block.
在qemu将每一个tb翻译为tcg的时候，都会发送这个信号
*/
sigc::signal<void, ExecutionSignal*,    
S2EExecutionState*,   
TranslationBlock*,   
uint64_t /* block PC */>   
onTranslateBlockStart;   

/** Signal that is emitted upon end of translation block    
在一个tb结束的时候（翻译跳转指令）的时候，发送这个信号   
*/   
sigc::signal<void, ExecutionSignal*,    
S2EExecutionState*,   
TranslationBlock*,   
uint64_t /* ending instruction pc */,   
bool /* static target is valid */,   
uint64_t /* static target pc */>    
onTranslateBlockEnd;      


/** Signal that is emitted on code generation for each instruction */   
sigc::signal<void, ExecutionSignal*,   
S2EExecutionState*,   
TranslationBlock*,      
uint64_t /* instruction PC */>   
onTranslateInstructionStart, onTranslateInstructionEnd;   

/**   
*  Triggered *after* each instruction is translated to notify   
*  plugins of which registers are used by the instruction.   
*  Each bit of the mask corresponds to one of the registers of   
*  the architecture (e.g., R_EAX, R_ECX, etc).         
*/      
sigc::signal<void,   
ExecutionSignal*,   
S2EExecutionState* /* current state */,   
TranslationBlock*,   
uint64_t /* program counter of the instruction */,   
uint64_t /* registers read by the instruction */,   
uint64_t /* registers written by the instruction */,   
bool /* instruction accesses memory */>   
onTranslateRegisterAccessEnd;   
   
/** Signal that is emitted on code generation for each jump instruction */   
sigc::signal<void, ExecutionSignal*,   
S2EExecutionState*,   
TranslationBlock*,   
uint64_t /* instruction PC */,   
int /* jump_type */>   
onTranslateJumpStart;   
   
/** Signal that is emitted upon exception */   
sigc::signal<void, S2EExecutionState*,    
unsigned /* Exception Index */,   
uint64_t /* pc */>   
onException;   
   
/** Signal that is emitted when custom opcode is detected    
在遇到s2e_op时，发送这个信号   
*/   
sigc::signal<void, S2EExecutionState*,    
uint64_t  /* arg */   
>   
onCustomInstruction;   
   
/** Signal that is emitted on each memory access */   
/* XXX: this signal is still not emmited for code */   
sigc::signal<void, S2EExecutionState*,   
klee::ref<klee::Expr> /* virtualAddress */,   
klee::ref<klee::Expr> /* hostAddress */,   
klee::ref<klee::Expr> /* value */,   
bool /* isWrite */, bool /* isIO */>   
onDataMemoryAccess;   
   
/** Signal that is emitted on each port access */   
sigc::signal<void, S2EExecutionState*,   
klee::ref<klee::Expr> /* port */,   
klee::ref<klee::Expr> /* value */,   
bool /* isWrite */>   
onPortAccess;   
   
sigc::signal<void> onTimer;   
   
/** Signal emitted when the state is forked */   
sigc::signal<void, S2EExecutionState* /* originalState */,   
const std::vector<S2EExecutionState*>& /* newStates */,   
const std::vector<klee::ref<klee::Expr> >& /* newConditions */>   
onStateFork;   
   
sigc::signal<void,   
S2EExecutionState*, /* currentState */   
S2EExecutionState*> /* nextState */   
onStateSwitch;   
   
/** Signal emitted when spawning a new S2E process */   
sigc::signal<void, bool /* prefork */,   
bool /* ischild */,   
unsigned /* parentProcId */> onProcessFork;   
   
/**   
* Signal emitted when a new S2E process was spawned and all   
* parent states were removed from the child and child states   
* removed from the parent.   
*/   
sigc::signal<void, bool /* isChild */> onProcessForkComplete;   
   
   
/** Signal that is emitted upon TLB miss */   
sigc::signal<void, S2EExecutionState*, uint64_t, bool> onTlbMiss;   
   
/** Signal that is emitted upon page fault */   
sigc::signal<void, S2EExecutionState*, uint64_t, bool> onPageFault;   
   
/** Signal emitted when QEMU is ready to accept registration of new devices */   
sigc::signal<void> onDeviceRegistration;   
   
/** Signal emitted when QEMU is ready to activate registered devices */   
sigc::signal<void, struct PCIBus*> onDeviceActivation;   
   
/**   
* The current execution privilege level was changed (e.g., kernel-mode=>user-mode)   
* previous and current are privilege levels. The meaning of the value may   
* depend on the architecture.   
在处理中断或者执行返回指令时，特权级发生变化时发送这个信号   
*/   
sigc::signal<void,   
S2EExecutionState* /* current state */,   
unsigned /* previous level */,   
unsigned /* current level */>   
onPrivilegeChange;   
   
/**   
* The current page directory was changed.   
* This may occur, e.g., when the OS swaps address spaces.   
* The addresses correspond to physical addresses.   
在页目录发生变化时，发送这个信号   
*/   
sigc::signal<void,   
S2EExecutionState* /* current state */,   
uint64_t /* previous page directory base */,   
uint64_t /* current page directory base */>   
onPageDirectoryChange;   
   
/**   
* S2E completed initialization and is about to enter   
* the main execution loop for the first time.   
*/   
sigc::signal<void,   
S2EExecutionState* /* current state */>   
onInitializationComplete;   
在由guest binary转化为tcg ir时，遇到s2e_op（0x13f）时会进行如下处理：   
case 0x13f: /* s2e_op */   
{   
        #ifdef CONFIG_S2E   
        uint64_t arg = ldq_code(s->pc);   
        s2e_tcg_emit_custom_instruction(g_s2e, arg);   
        #else   
        /* Simply skip the S2E opcodes when building vanilla qemu */   
        ldq_code(s->pc);   
        #endif   
        s->pc+=8;   
        break;   
           
}   
其中调用了s2e_tcg_emit_custom_instruction(g_s2e, arg)：    
void s2e_tcg_emit_custom_instruction(S2E*, uint64_t arg)   
{   
        TCGv_ptr t0 = tcg_temp_new_i64();   
        tcg_gen_movi_i64(t0, arg);   
           
        TCGArg args[1];   
        args[0] = GET_TCGV_I64(t0);   
        tcg_gen_helperN((void*) s2e_tcg_custom_instruction_handler,   
        0, 2, TCG_CALL_DUMMY_ARG, 1, args);   
           
        tcg_temp_free_i64(t0);   
}   
   
void s2e_tcg_custom_instruction_handler(uint64_t arg)   
{   
        assert(!g_s2e->getCorePlugin()->onCustomInstruction.empty());   
           
        try {   
                g_s2e->getCorePlugin()->onCustomInstruction.emit(g_s2e_state, arg);   
        } catch(s2e::CpuExitException&) {   
                s2e_longjmp(env->jmp_env, 1);   
        }   
}   
   
这里CorePlugin会发送onCustomInstruction信号，定义的每个插件会在相应的初始化函数中进行connect操作，完成相应的功能。   
   
最后会对每一个插件调用initialize()方法进行初始化，而这个initialize()是一个虚函数，在每一个插件中都有自己的初始化函数。如BaseInstruction中，初始化函数如下定义：   
   
void BaseInstructions::initialize()   
{   
        s2e()->getCorePlugin()->onCustomInstruction.connect(   
        sigc::mem_fun(*this, &BaseInstructions::onCustomInstruction));   
           
}   
   
当插件BaseInstruction接收到Guest发来的消息时，就会调用onCustomInstruction来对每一个命令进行相应的处理。