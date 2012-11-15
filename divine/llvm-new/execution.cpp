//===-- Execution.cpp - Implement code to simulate the program ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file contains the actual instruction interpreter.
//
//===----------------------------------------------------------------------===//

#include <divine/llvm-new/interpreter.h>

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Analysis/DebugInfo.h"
#include <algorithm>
#include <cmath>
#include <wibble/test.h>
#include <wibble/sys/mutex.h>

using namespace llvm;
using namespace divine::llvm2;

struct Nil {};

template< int N > struct ConsAt;

template<>
struct ConsAt< 0 > {
    template< typename Cons >
    static auto get( Cons &c ) -> decltype( c.car ) {
        return c.car;
    }
};

template< int N >
struct ConsAt {
    template< typename Cons >
    static auto get( Cons &c ) -> decltype( ConsAt< N - 1 >::get( c.cdr ) )
    {
        return ConsAt< N - 1 >::get( c.cdr );
    }
};

template< typename A, typename B >
struct Cons {
    typedef A Car;
    typedef B Cdr;
    A car;
    B cdr;

    template< int N >
    auto get() -> decltype( ConsAt< N >::get( *this ) )
    {
        return ConsAt< N >::get( *this );
    }
};

template< typename A, typename B >
Cons< A, B > cons( A a, B b ) {
    Cons< A, B > r = { .car = a, .cdr = b };
    return r;
}

template< typename As, typename A, typename B >
Cons< As *, B > consPtr( A *a, B b ) {
    Cons< As *, B > r;
    r.car = reinterpret_cast< As * >( a );
    r.cdr = b;
    return r;
}

template< typename X >
struct UnPtr {};
template< typename X >
struct UnPtr< X * > { typedef X T; };

template< int I, typename Cons >
auto decons( Cons &c ) -> typename UnPtr< decltype( ConsAt< I >::get( c ) ) >::T &
{
    return *c.template get< I >();
}

#define MATCH(expr...) template< typename F, typename X > \
    auto match( wibble::Preferred, F f, X &&x ) -> decltype( f( expr ) ) { return f( expr ); }

MATCH( decons< 0 >( x ) )
MATCH( decons< 1 >( x ), decons< 0 >( x ) )
MATCH( decons< 2 >( x ), decons< 1 >( x ), decons< 0 >( x ) )
MATCH( decons< 3 >( x ), decons< 2 >( x ), decons< 1 >( x ), decons< 0 >( x ) )

#undef MATCH

template< typename F, typename X >
typename F::T match( wibble::NotPreferred, F f, X &&x )
{
    assert_die();
}

bool isSignedOp( ProgramInfo::Instruction i ) {
    return true; // XXX
}

template< typename Fun, typename Cons >
typename Fun::T Interpreter::implement( ProgramInfo::Instruction i, Cons list )
{
    Fun instance;
    instance._interpreter = this;
    instance.instruction = i;
    return match( wibble::Preferred(), instance, list );
}

template< typename Fun, typename Cons, typename... Args >
typename Fun::T Interpreter::implement( ProgramInfo::Instruction i, Cons list,
                                        std::pair< Type *, char * > arg, Args... args )
{
    Type *ty = arg.first;
    int width = TD.getTypeAllocSize( ty ); /* bytes */

    if ( ty->isIntegerTy() ) {
        if ( isSignedOp( i ) ) {
            switch ( width ) {
                case 1: return implement< Fun >( i, consPtr<  int8_t >( arg.second, list ), args... );
                case 2: return implement< Fun >( i, consPtr< int16_t >( arg.second, list ), args... );
                case 4: return implement< Fun >( i, consPtr< int32_t >( arg.second, list ), args... );
                case 8: return implement< Fun >( i, consPtr< int64_t >( arg.second, list ), args... );
            }
        }
    }

    if ( ty->isFloatTy() )
        return implement< Fun >( i, consPtr< float /* TODO float32_t */ >( arg.second, list ), args... );
    if ( ty->isDoubleTy() )
        return implement< Fun >( i, consPtr< double /* TODO float64_t */ >( arg.second, list ), args... );
    assert_die();
}

template< typename Fun, typename... Args >
typename Fun::T Interpreter::implementN( Args... args )
{
    return implement< Fun >( ProgramInfo::Instruction(), Nil(), args... );
}

template< typename I >
void Interpreter::implement1( ProgramInfo::Instruction i )
{
    Type *ty = i.op->getOperand(0)->getType();
    implement< I >( i, Nil(),
                    dereference( ty, i.result ),
                    dereference( ty, i.operands[ 0 ] ) );
}

template< typename I >
void Interpreter::implement2( ProgramInfo::Instruction i )
{
    Type *ty = i.op->getOperand(0)->getType();
    implement< I >( i, Nil(),
                    dereference( ty, i.result ),
                    dereference( ty, i.operands[ 0 ] ),
                    dereference( ty, i.operands[ 1 ] ) );
}

template< typename I >
void Interpreter::implement3( ProgramInfo::Instruction i )
{
    Type *ty = i.op->getOperand(0)->getType();
    implement< I >( i, Nil(),
                    dereference( ty, i.result ),
                    dereference( ty, i.operands[ 0 ] ),
                    dereference( ty, i.operands[ 1 ] ),
                    dereference( ty, i.operands[ 2 ] ) );
}

struct Implementation {
    typedef void T;
    ProgramInfo::Instruction instruction;
    Interpreter *_interpreter;
    ProgramInfo::Instruction i() { return instruction; }
    Interpreter &interpreter() { return *_interpreter; }
    TargetData &TD() { return interpreter().TD; }
};

template< typename... X >
static void declcheck( X... ) {}

struct Arithmetic : Implementation {
    template< typename X >
    auto operator()( X &r, X &a, X &b ) -> decltype( declcheck( a % b ) )
    {
        switch( i().op->getOpcode() ) {
            case Instruction::FAdd:
            case Instruction::Add: r = a + b; return;
            case Instruction::FSub:
            case Instruction::Sub: r = a - b; return;
            case Instruction::FMul:
            case Instruction::Mul: r = a * b; return;
            case Instruction::FDiv:
            case Instruction::SDiv:
            case Instruction::UDiv: r = a / b; return;
            case Instruction::FRem: r = std::fmod( a, b ); return;
            case Instruction::URem:
            case Instruction::SRem: r = a % b; return;
            case Instruction::And:  r = a & b; return;
            case Instruction::Or:   r = a | b; return;
            case Instruction::Xor:  r = a ^ b; return;
            case Instruction::Shl:  r = a << b; return;
            case Instruction::AShr:  // XXX?
            case Instruction::LShr:  r = a >> b; return;
        }
        assert_die();
    }
};

struct Select : Implementation {
    template< typename R, typename C >
    auto operator()( R &r, C &a, R &b, R &c ) -> decltype( declcheck( r = a ? b : c ) )
    {
        r = a ? b : c;
    }
};

struct ICmp : Implementation {
    template< typename R, typename X >
    auto operator()( R &r, X &a, X &b ) -> decltype( declcheck( r = a < b ) ) {
        switch (dyn_cast< ICmpInst >( i().op )->getPredicate()) {
            case ICmpInst::ICMP_EQ:  r = a == b; return;
            case ICmpInst::ICMP_NE:  r = a != b; return;
            case ICmpInst::ICMP_ULT:
            case ICmpInst::ICMP_SLT: r = a < b; return;
            case ICmpInst::ICMP_UGT:
            case ICmpInst::ICMP_SGT: r = a > b; return;
            case ICmpInst::ICMP_ULE:
            case ICmpInst::ICMP_SLE: r = a <= b; return;
            case ICmpInst::ICMP_UGE:
            case ICmpInst::ICMP_SGE: r = a >= b; return;
            default: assert_die();
        }
    }
};

struct FCmp : Implementation {
    template< typename R, typename X >
    auto operator()( R &r, X &a, X &b ) ->
        decltype( declcheck( r = isnan( a ) && isnan( b ) ) )
    {
        switch ( dyn_cast< FCmpInst >( i().op )->getPredicate() ) {
            case FCmpInst::FCMP_FALSE: r = false; return;
            case FCmpInst::FCMP_TRUE:  r = true;  return;
            case FCmpInst::FCMP_ORD:   r = !isnan( a ) && !isnan( b ); return;
            case FCmpInst::FCMP_UNO:   r = isnan( a ) || isnan( b );   return;

            case FCmpInst::FCMP_UEQ:
            case FCmpInst::FCMP_UNE:
            case FCmpInst::FCMP_UGE:
            case FCmpInst::FCMP_ULE:
            case FCmpInst::FCMP_ULT:
            case FCmpInst::FCMP_UGT:
                if ( isnan( a ) || isnan( b ) ) {
                    r = true;
                    return;
                }
                break;
            default: assert_die();
        }

        switch ( dyn_cast< FCmpInst >( i().op )->getPredicate() ) {
            case FCmpInst::FCMP_OEQ:
            case FCmpInst::FCMP_UEQ: r = a == b; return;
            case FCmpInst::FCMP_ONE:
            case FCmpInst::FCMP_UNE: r = a != b; return;

            case FCmpInst::FCMP_OLT:
            case FCmpInst::FCMP_ULT: r = a < b; return;

            case FCmpInst::FCMP_OGT:
            case FCmpInst::FCMP_UGT: r = a > b; return;

            case FCmpInst::FCMP_OLE:
            case FCmpInst::FCMP_ULE: r = a <= b; return;

            case FCmpInst::FCMP_OGE:
            case FCmpInst::FCMP_UGE: r = a >= b; return;
            default: assert_die();
        }
    }
};

/* Bit lifting. */

void Interpreter::visitFCmpInst(FCmpInst &) {
    implement2< FCmp >( instruction() );
}

void Interpreter::visitICmpInst(ICmpInst &) {
    implement2< ICmp >( instruction() );
}

void Interpreter::visitBinaryOperator(BinaryOperator &) {
    implement2< Arithmetic >( instruction() );
}

void Interpreter::visitSelectInst(SelectInst &) {
    implement3< Select >( instruction() );
}

/* Control flow. */

struct Copy : Implementation {
    template< typename X >
    void operator()( X &r, X &l )
    {
        r = l;
    }
};

struct BitCast : Implementation {
    template< typename R, typename L >
    void operator()( R &r, L &l ) {
        char *from = reinterpret_cast< char * >( &l );
        char *to = reinterpret_cast< char * >( &r );
        assert_eq( sizeof( R ), sizeof( L ) );
        std::copy( from, from + sizeof( R ), to );
    }
};

template< typename _T >
struct Get {
    struct I : Implementation {
        typedef _T T;

        template< typename X >
        auto operator()( X &l ) -> decltype( static_cast< T >( l ) )
        {
            return static_cast< T >( l );
        }
    };
};

typedef Get< bool >::I IsTrue;

void Interpreter::leaveFrame()
{
    state.leave();
}

void Interpreter::leaveFrame( Type *ty, ProgramInfo::Value result ) {

    /* Handle the return value first. */
    if ( state.stack().get().length() == 1 ) {
        /* TODO handle exit codes (?) */
    } else {
        /* Find the call instruction we are going back to. */
        auto i = info.instruction( state.frame( -1, 1 ).pc );
        /* Copy the return value. */
        implementN< Copy >( dereference( ty, i.result, -1 , 1 ),
                            dereference( ty, result ) );

        /* If this was an invoke, run the non-error target. */
        if ( InvokeInst *II = dyn_cast< InvokeInst >( i.op ) )
            switchBB( II->getNormalDest() );
    }

    leaveFrame(); /* Actually pop the stack. */
}

void Interpreter::visitReturnInst(ReturnInst &I) {
    if ( I.getNumOperands() )
        leaveFrame( I.getReturnValue()->getType(), instruction().operands[ 0 ] );
    else
        leaveFrame();
}

void Interpreter::checkJump( BasicBlock *Dest )
{
    PC from = pc();
    PC to = info.pcmap[ &*(Dest->begin()) ]; /* jump! */
    if ( from.function != to.function )
        throw wibble::exception::Consistency(
            "Interpreter::checkJump",
            "Can't deal with cross-function jumps." );
}

void Interpreter::visitBranchInst(BranchInst &I)
{
    if ( I.isUnconditional() ) {
        checkJump( I.getSuccessor( 0 ) );
        switchBB( I.getSuccessor( 0 ) );
    } else {
        if ( implementN< IsTrue >( dereferenceOperand( instruction(), 0 ) ) ) {
            checkJump( I.getSuccessor( 0 ) );
            switchBB( I.getSuccessor( 0 ) );
        } else {
            checkJump( I.getSuccessor( 1 ) );
            switchBB( I.getSuccessor( 1 ) );
        }
    }
}

void Interpreter::visitSwitchInst(SwitchInst &I) {
    assert_die();
#if 0
    GenericValue CondVal = getOperandValue(I.getOperand(0), SF());
    Type *ElTy = I.getOperand(0)->getType();

    // Check to see if any of the cases match...
    BasicBlock *Dest = 0;
    for (unsigned i = 2, e = I.getNumOperands(); i != e; i += 2)
        if (executeICMP_EQ(CondVal, getOperandValue(I.getOperand(i), SF()), ElTy)
            .IntVal != 0) {
            Dest = cast<BasicBlock>(I.getOperand(i+1));
            break;
        }

    if (!Dest) Dest = I.getDefaultDest();   // No cases matched: use default
    SwitchToNewBasicBlock(Dest, SF());
#endif
}

void Interpreter::visitIndirectBrInst(IndirectBrInst &I) {
    Pointer target = implementN< Get< Pointer >::I >( dereferenceOperand( instruction(), 0 ) );
    assert_die(); // switchBB( dereferenceBB( target ) );
}

// SwitchToNewBasicBlock - This method is used to jump to a new basic block.
// This function handles the actual updating of block and instruction iterators
// as well as execution of all of the PHI nodes in the destination block.
//
// This method does this because all of the PHI nodes must be executed
// atomically, reading their inputs before any of the results are updated.  Not
// doing this can cause problems if the PHI nodes depend on other PHI nodes for
// their inputs.  If the input PHI node is updated before it is read, incorrect
// results can happen.  Thus we use a two phase approach.
//
void Interpreter::switchBB( BasicBlock *Dest )
{
    jumped = true;

    if ( Dest ) /* PC already updated if Dest is NULL */
        pc() = info.pcmap[ &*(Dest->begin()) ]; /* jump! */
    else
        Dest = instruction().op->getParent();

    if ( !isa<PHINode>( Dest->begin() ) ) return;  // Nothing fancy to do

    assert_die();
#if 0
    // Loop over all of the PHI nodes in the current block, reading their inputs.
    std::vector<GenericValue> ResultValues;

    for (; PHINode *PN = dyn_cast<PHINode>(next.insn); ++next.insn) {
        // Search for the value corresponding to this previous bb...
        int i = PN->getBasicBlockIndex(previous.block);
        assert(i != -1 && "PHINode doesn't contain entry for predecessor??");
        Value *IncomingValue = PN->getIncomingValue(i);

        // Save the incoming value for this PHI node...
        ResultValues.push_back(getOperandValue(IncomingValue, SF));
    }

    // Now loop over all of the PHI nodes setting their values...
    next.insn = next.block->begin();
    for (unsigned i = 0; isa<PHINode>(next.insn); ++next.insn, ++i) {
        PHINode *PN = cast<PHINode>(next.insn);
        SetValue(PN, ResultValues[i], SF);
    }
    setLocation( SF, next );
#endif
}

//===----------------------------------------------------------------------===//
//                     Memory Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::visitAllocaInst(AllocaInst &I) {
    Type *ty = I.getType()->getElementType();  // Type to be allocated

    int count = implementN< Get< int >::I >( dereferenceOperand( instruction(), 0 ) );
    int size = TD.getTypeAllocSize(ty);

    // Avoid malloc-ing zero bytes, use max()...
    unsigned alloc = std::max( 1, count * size );

    assert_eq( alloc, 0 ); // bang. To be implemented.

#if 0
    // Allocate enough memory to hold the type...
    Arena::Index Memory = arena.allocate(alloc);

    GenericValue Result;
    Result.PointerVal = reinterpret_cast< void * >( intptr_t( Memory ) );
    Result.IntVal = APInt(2, 0); // XXX, not very clean; marks an alloca for cloning by detach()
    assert(Result.PointerVal != 0 && "Null pointer returned by malloc!");
    SetValue(&I, Result, SF());

    if (I.getOpcode() == Instruction::Alloca)
        SF().allocas.push_back(Memory);
#endif
}

struct GetElement : Implementation {
    void operator()( Pointer &r, Pointer &p ) {
        int total = 0;

        gep_type_iterator I = gep_type_begin( i().op ),
                          E = gep_type_end( i().op );

        int meh = 1;
        for (; I != E; ++I, ++meh) {
            if (StructType *STy = dyn_cast<StructType>(*I)) {
                const StructLayout *SLO = TD().getStructLayout(STy);
                const ConstantInt *CPU = cast<ConstantInt>(I.getOperand()); /* meh */
                int index = CPU->getZExtValue();
                total += SLO->getElementOffset( index );
            } else {
                const SequentialType *ST = cast<SequentialType>(*I);
                int index = interpreter().implementN< Get< int >::I >(
                    interpreter().dereferenceOperand( i(), meh ) );
                total += index * TD().getTypeAllocSize( ST->getElementType() );
            }
        }

        r = p + total;
    }
};

void Interpreter::visitGetElementPtrInst(GetElementPtrInst &I) {
    assert(I.getPointerOperand()->getType()->isPointerTy() &&
           "Cannot getElementOffset of a nonpointer type!");
    implement2< GetElement >( instruction() );
}

struct Load : Implementation {
    template< typename R >
    void operator()( R &r, Pointer p ) {
        assert_die(); /*
        decons< 1 >( c ) = *reinterpret_cast< decltype( &decons< 1 >( c ) ) >(
            interpreter().dereferencePointer( decons< 0 >( c ) ) );
                      */
    }
};

struct Store : Implementation {
    template< typename L >
    void operator()( Pointer r, L &l ) {
        assert_die(); /*
        *reinterpret_cast< decltype( &decons< 0 >( c ) ) >(
            interpreter().dereferencePointer( decons< 1 >( c ) ) ) = decons< 0 >( c );
                      */
    }
};

void Interpreter::visitLoadInst(LoadInst &I) {
    implement1< Load >( instruction() );
}

void Interpreter::visitStoreInst(StoreInst &I) {
    implement1< Store >( instruction() );
}

//===----------------------------------------------------------------------===//
//                 Miscellaneous Instruction Implementations
//===----------------------------------------------------------------------===//

void Interpreter::visitFenceInst(FenceInst &I) {
    // do nothing for now
}

void Interpreter::visitCallSite(CallSite CS) {
    ProgramInfo::Instruction insn = instruction();
    // Check to see if this is an intrinsic function call...
    Function *F = CS.getCalledFunction();

    if ( F && F->isDeclaration() )
        switch (F->getIntrinsicID()) {
            case Intrinsic::not_intrinsic:
                break;
            case Intrinsic::vastart: { // va_start
                assert_die(); /*
                GenericValue ArgIndex;
                ArgIndex.UIntPairVal.first = stack().size() - 1;
                ArgIndex.UIntPairVal.second = 0;
                SetValue(CS.getInstruction(), ArgIndex, SF()); */
                return;
            }
            // noops
            case Intrinsic::dbg_declare:
            case Intrinsic::dbg_value:
                return;
            case Intrinsic::trap:
                assert_die();
                /* while (!stack().empty()) // get us out
                    leave(); */
                return;

            case Intrinsic::vaend:    // va_end is a noop for the interpreter
                return;
            case Intrinsic::vacopy:   // va_copy: dest = src
                assert_die();
                // SetValue(CS.getInstruction(), getOperandValue(*CS.arg_begin(), SF()), SF());
                return;
            default:
                // If it is an unknown intrinsic function, use the intrinsic lowering
                // class to transform it into hopefully tasty LLVM code.
                //
                // dbgs() << "FATAL: Can't lower:" << *CS.getInstruction() << "\n";
                assert_die(); /* TODO: the new locations need to be indexed */
                /* BasicBlock::iterator me(CS.getInstruction());
                BasicBlock *Parent = CS.getInstruction()->getParent();
                bool atBegin(Parent->begin() == me);
                if (!atBegin)
                    --me;
                IL->LowerIntrinsicCall(cast<CallInst>(CS.getInstruction()));

                // Restore the CurInst pointer to the first instruction newly inserted, if
                // any.
                if (atBegin) {
                    setInstruction( SF(), Parent->begin() );
                } else {
                    BasicBlock::iterator me_next = me;
                    ++ me_next;
                    setInstruction( SF(), me_next );
                }
                return; */
        }

    // Special handling for external functions.
    if (F->isDeclaration()) {
        /* This traps into the "externals": functions that may be provided by
         * our own runtime (these may be nondeterministic), or, possibly (TODO)
         * into external, native library code  */
        assert_die();
    }

    int functionid = info.functionmap[ F ];
    state.enter( functionid );

    ProgramInfo::Function function = info.function( pc() );

    for ( int i = 0; i < CS.arg_size(); ++i )
    {
        Type *ty = CS.getArgument( i )->getType();
        implementN< Copy >( dereference( ty, function.values[ i ] ),
                            dereferenceOperand( insn, i + 1, 1 ) );
    }

    /* TODO function entry blocks probably can't have PHI nodes in them, so
     * this is actually redundant. */
    switchBB();

#if 0
    Location loc = location( SF() );
    loc.insn = CS.getInstruction();
    SF().caller = locationIndex.left( loc );
    std::vector<GenericValue> ArgVals;
    const unsigned NumArgs = CS.arg_size();
    ArgVals.reserve(NumArgs);
    uint16_t pNum = 1;
    for (CallSite::arg_iterator i = CS.arg_begin(),
                                e = CS.arg_end(); i != e; ++i, ++pNum) {
        Value *V = *i;
        ArgVals.push_back(getOperandValue(V, SF()));
    }

    // To handle indirect calls, we must get the pointer value from the argument
    // and treat it as a function pointer.
    GenericValue SRC = getOperandValue(CS.getCalledValue(), SF());
    Function *fun = functionIndex.right(int(intptr_t(GVTOP(SRC))));
    callFunction(fun, ArgVals);
#endif
}

// FPTo*I should round

void Interpreter::visitTruncInst(TruncInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitSExtInst(SExtInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitZExtInst(ZExtInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitFPTruncInst(FPTruncInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitFPExtInst(FPExtInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitUIToFPInst(UIToFPInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitSIToFPInst(SIToFPInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitFPToUIInst(FPToUIInst &I) {
    implement2< Copy >( instruction() ); // XXX rounding?
}

void Interpreter::visitFPToSIInst(FPToSIInst &I) {
    implement2< Copy >( instruction() ); // XXX rounding?
}

void Interpreter::visitPtrToIntInst(PtrToIntInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitIntToPtrInst(IntToPtrInst &I) {
    implement2< Copy >( instruction() );
}

void Interpreter::visitBitCastInst(BitCastInst &I) {
    implement2< BitCast >( instruction() ); // XXX?
}

