#ifndef __DIVINE_ENTRY_H__
#define __DIVINE_ENTRY_H__

extern "C" const _VM_Env *__sys_env_get() __attribute__((__annotate__("brick.llvm.prune.root")));

#endif // __DIVINE_ENTRY_H__
