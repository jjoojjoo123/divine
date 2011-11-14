// -*- C++ -*- (c) 2011 Petr Rockai <me@mornfall.net>
// Implementation of "external" functions that the LLVM bytecode may want to call.

#ifdef HAVE_LLVM

#include <divine/llvm/interpreter.h>

using namespace llvm;
using namespace divine::llvm;

typedef std::vector<GenericValue> Args;

typedef GenericValue (*Builtin)(Interpreter *i,
                                const FunctionType *,
                                const Args &);

static GenericValue builtin_exit(Interpreter *, const FunctionType *, const Args &)
{
    // XXX mark the open state as final
    assert_die();
}

static GenericValue builtin_assert(Interpreter *interp, const FunctionType *, const Args &args)
{
    if (!args[0].IntVal)
        interp->flags.assert = true;
    return GenericValue();
}

static GenericValue builtin_malloc_guaranteed(Interpreter *interp, const FunctionType *, const Args &args)
{
    int size = args[0].IntVal.getZExtValue();
    Arena::Index mem = interp->arena.allocate(size);
    GenericValue GV;
    GV.PointerVal = reinterpret_cast< void * >( intptr_t( mem ) );
    GV.IntVal = APInt(3, 0); // XXX hack.
    return GV;
}

static GenericValue builtin_malloc(Interpreter *interp, const FunctionType *ty, const Args &args)
{
    // This yields two different successors, one with a NULL return and another
    // where the memory was actually allocated (the alloca/malloc arenas are
    // actually shared, (TODO) at least for now)
    switch (interp->_alternative) {
        case 0:
            return builtin_malloc_guaranteed( interp, ty, args );
        case 1:
            return PTOGV( 0 );
    }
    assert_die();
}

static GenericValue builtin_amb(Interpreter *interp, const FunctionType *, const Args &args)
{
    GenericValue GV;
    GV.IntVal = APInt(32, interp->_alternative);
    return GV;
}

static GenericValue builtin_free(Interpreter *interp, const FunctionType *, const Args &args)
{
    Arena::Index idx = intptr_t(GVTOP(args[0]));
    interp->arena.free(idx);
    return GenericValue();
}

static GenericValue builtin_thread_create(Interpreter *interp, const FunctionType *ty,
                                          const Args &args)
{
    GenericValue New, Old;
    New.IntVal = APInt( 32, 1 );
    Old.IntVal = APInt( 32, 0 );

    int old = interp->_context;
    int nieuw = interp->stacks.size();

    // Fork off a new thread, as a clone of the current thread.
    interp->stacks.push_back( interp->stack( old ) );
    interp->detach( interp->stack( nieuw ) );

    interp->_context = nieuw; // switch to the new thread
    // simulate a return in the new thread, yielding 1
    interp->popStackAndReturnValueToCaller(ty->getReturnType(), New);
    interp->_context = old;
    return Old; // and in the current thread, return 0 and continue as usual
}

static GenericValue builtin_thread_stop(Interpreter *interp, const FunctionType *ty,
                                        const Args &args)
{
    while (!interp->stack().empty())
        interp->leave();
    return GenericValue();
}

static GenericValue builtin_fork(Interpreter *, const FunctionType *, const Args &args)
{
    // XXX this should fork off a new process; again as with threads, this
    // needs substantial rigging in the interpreter code and data structures;
    // process interleaving may be much coarser than with threads, since there
    // is no shared memory between processes (message passing may need to be
    // implemented too, to make this useful at all, though)
    assert_die();
}

// From LLVM.
// int sprintf(char *, const char *, ...)
static GenericValue builtin_sprintf(Interpreter *interp, const FunctionType *FT,
                                    const std::vector<GenericValue> &Args)
{
  char *OutputBuffer = (char *)GVTOP(Args[0]);
  const char *FmtStr = (const char *)GVTOP(Args[1]);
  unsigned ArgNo = 2;

  // printf should return # chars printed.  This is completely incorrect, but
  // close enough for now.
  GenericValue GV;
  GV.IntVal = APInt(32, strlen(FmtStr));
  while (1) {
    switch (*FmtStr) {
    case 0: return GV;             // Null terminator...
    default:                       // Normal nonspecial character
      sprintf(OutputBuffer++, "%c", *FmtStr++);
      break;
    case '\\': {                   // Handle escape codes
      sprintf(OutputBuffer, "%c%c", *FmtStr, *(FmtStr+1));
      FmtStr += 2; OutputBuffer += 2;
      break;
    }
    case '%': {                    // Handle format specifiers
      char FmtBuf[100] = "", Buffer[1000] = "";
      char *FB = FmtBuf;
      *FB++ = *FmtStr++;
      char Last = *FB++ = *FmtStr++;
      unsigned HowLong = 0;
      while (Last != 'c' && Last != 'd' && Last != 'i' && Last != 'u' &&
             Last != 'o' && Last != 'x' && Last != 'X' && Last != 'e' &&
             Last != 'E' && Last != 'g' && Last != 'G' && Last != 'f' &&
             Last != 'p' && Last != 's' && Last != '%') {
        if (Last == 'l' || Last == 'L') HowLong++;  // Keep track of l's
        Last = *FB++ = *FmtStr++;
      }
      *FB = 0;

      switch (Last) {
      case '%':
        memcpy(Buffer, "%", 2); break;
      case 'c':
        sprintf(Buffer, FmtBuf, uint32_t(Args[ArgNo++].IntVal.getZExtValue()));
        break;
      case 'd': case 'i':
      case 'u': case 'o':
      case 'x': case 'X':
        if (HowLong >= 1) {
          if (HowLong == 1 &&
              interp->getTargetData()->getPointerSizeInBits() == 64 &&
              sizeof(long) < sizeof(int64_t)) {
            // Make sure we use %lld with a 64 bit argument because we might be
            // compiling LLI on a 32 bit compiler.
            unsigned Size = strlen(FmtBuf);
            FmtBuf[Size] = FmtBuf[Size-1];
            FmtBuf[Size+1] = 0;
            FmtBuf[Size-1] = 'l';
          }
          sprintf(Buffer, FmtBuf, Args[ArgNo++].IntVal.getZExtValue());
        } else
          sprintf(Buffer, FmtBuf,uint32_t(Args[ArgNo++].IntVal.getZExtValue()));
        break;
      case 'e': case 'E': case 'g': case 'G': case 'f':
        sprintf(Buffer, FmtBuf, Args[ArgNo++].DoubleVal); break;
      case 'p':
        sprintf(Buffer, FmtBuf, (void*)GVTOP(Args[ArgNo++])); break;
      case 's':
        sprintf(Buffer, FmtBuf, (char*)GVTOP(Args[ArgNo++])); break;
      default:
        errs() << "<unknown printf code '" << *FmtStr << "'!>";
        ArgNo++; break;
      }
      size_t Len = strlen(Buffer);
      memcpy(OutputBuffer, Buffer, Len + 1);
      OutputBuffer += Len;
      }
      break;
    }
  }
  return GV;
}

// A limited version of printf, might be useful for last-resort model
// debugging. Traces are shown whenever the model checker visits the given
// state.
static GenericValue builtin_trace(Interpreter *interp, const FunctionType *FT,
                                  const std::vector<GenericValue> &args)
{
    char buffer[10000];
    Args newargs;
    newargs.push_back(PTOGV((void*)&buffer[0]));
    std::copy( args.begin(), args.end(), std::back_inserter( newargs ) );
    builtin_sprintf( interp, FT, newargs );
    std::cerr << "TRACE: " << buffer << std::endl;
    return GenericValue();
}

static struct {
    const char *name;
    Builtin fun;
} builtins[] = {
    { "__divine_builtin_amb", builtin_amb },
    { "__divine_builtin_exit", builtin_exit },
    { "__divine_builtin_trace", builtin_trace },
    { "__divine_builtin_assert", builtin_assert },
    { "__divine_builtin_malloc", builtin_malloc },
    { "__divine_builtin_malloc_guaranteed", builtin_malloc_guaranteed },
    { "__divine_builtin_free", builtin_free },
    { "__divine_builtin_thread_create", builtin_thread_create },
    { "__divine_builtin_thread_stop", builtin_thread_stop },
    { "__divine_builtin_fork", builtin_fork },
    { 0, 0 }
};

Builtin findBuiltin( Function *F ) {
    std::string plain = F->getNameStr();
    std::string divine = "__divine_builtin_" + plain;

    for ( int i = 0; builtins[i].name; ++i ) {
        if ( plain == builtins[i].name )
            return builtins[i].fun;
    }

    for ( int i = 0; builtins[i].name; ++i ) {
        if ( divine == builtins[i].name )
            return builtins[i].fun;
    }

    std::cerr << "WARNING: failed to resolve symbol " << plain << std::endl;
    return NULL;
}

GenericValue Interpreter::callExternalFunction(
    Function *F, const std::vector<GenericValue> &ArgVals)
{
    Builtin fun = findBuiltin( F );
    if ( fun )
        return fun( this, F->getFunctionType(), ArgVals );
    return GenericValue();
}


bool Interpreter::viable( int ctx, int alt )
{
    _context = ctx;

    if ( _context >= stacks.size() )
        return false;
    if ( done( ctx ) )
        return false;

    if ( flags.null_dereference || flags.invalid_dereference )
        return false;

    Function *F;
    Instruction &I = nextInstruction();

    /* non-call/invoke insns are deterministic */
    if (!isa<CallInst>(I) && !isa<InvokeInst>(I))
        return alt < 1;
    else { // extract the function
        CallSite cs(&I);
        F = cs.getCalledFunction();
    }

    assert( F );
    if (!F->isDeclaration())
        return alt < 1; // not a builtin, deterministic

    std::string plain = F->getNameStr();

    if ( std::string( plain, 0, 4 ) == "llvm" )
        return alt < 1;

    Builtin fun = findBuiltin( F );

    if ( fun == builtin_malloc )
        return alt < 2; /* malloc has 2 different returns */
    if ( fun == builtin_amb )
        return alt < 2; /* amb has 2 different returns (0 and 1) */

    // everything else is deterministic as well
    return alt < 1;
}

bool Interpreter::isTau( int ctx )
{
    _context = ctx;

    if ( _context >= stacks.size() )
        return false;
    if ( done( ctx ) )
        return false;

    // we probably shouldn't slip out of bad states
    if ( flags.null_dereference || flags.invalid_dereference )
        return false;

    Instruction &I = nextInstruction();

    if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
        CallSite cs(&I);
        Function *F = cs.getCalledFunction();
        if (!F || !F->isDeclaration())
            return false; // not a builtin, might recurse

        // FIXME SLOW
        std::string plain = F->getNameStr();
        if ( std::string( plain, 0, 8 ) == "llvm.dbg" )
            return true; // debug builtins can be ignored

        return false; // calls to other builtins are invisible
    }

    if (isa<LoadInst>(I) ||
        isa<StoreInst>(I) ||
        isa<IndirectBrInst>(I) ||
        isa<BranchInst>(I))
        return false;

    return true; // anything else is tau...
}

#endif
