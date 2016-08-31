// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2016 Petr Ročkai <code@fixp.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

DIVINE_RELAX_WARNINGS
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/IntrinsicInst.h>
DIVINE_UNRELAX_WARNINGS

#include <divine/vm/eval.hpp>
#include <divine/vm/print.hpp>
#include <divine/cc/runtime.hpp>
#include <brick-string>

namespace divine {
namespace vm {

enum class DNKind { Frame, Object };

static std::ostream &operator<<( std::ostream &o, DNKind dnk )
{
    switch ( dnk )
    {
        case DNKind::Frame: return o << "frame";
        case DNKind::Object: return o << "object";
    }
}

std::pair< std::string, int > fileline( const llvm::Instruction &insn );
std::string location( const llvm::Instruction &insn );

template< typename Context >
struct DebugNode
{
    using Heap = typename Context::Heap;
    using Program = typename Context::Program;
    using Eval = Eval< Program, ConstContext< Program, Heap >, value::Void >;
    using Snapshot = typename Heap::Snapshot;

    ConstContext< Program, Heap > _ctx;

    GenericPointer _address;
    int _offset;
    Snapshot _snapshot;
    DNKind _kind;

    /* applies only to Objects */
    llvm::Type *_type;
    llvm::DIType *_di_type;

    using PointerV = value::Pointer;

    int size()
    {
        int sz = INT_MAX;
        Eval eval( _ctx.program(), _ctx );
        if ( _type )
            sz = _ctx.program().TD.getTypeAllocSize( _type );
        if ( !_address.null() )
            sz = std::min( sz, eval.ptr2sz( PointerV( _address ) ) - _offset );
        return sz;
    }

    llvm::DIDerivedType *di_derived( uint64_t tag )
    {
        if ( !_di_type )
            return nullptr;
        auto derived = llvm::dyn_cast< llvm::DIDerivedType >( _di_type );
        if ( derived && derived->getTag() == tag )
            return derived;
        return nullptr;
    }

    llvm::DIDerivedType *di_member() { return di_derived( llvm::dwarf::DW_TAG_member ); }
    llvm::DIDerivedType *di_pointer() { return di_derived( llvm::dwarf::DW_TAG_pointer_type ); }

    llvm::DIType *di_base()
    {
        if ( di_member() )
            return di_member()->getBaseType().resolve( _ctx.program().ditypemap );
        if ( di_pointer() )
            return di_pointer()->getBaseType().resolve( _ctx.program().ditypemap );
        return _di_type;
    }

    llvm::DICompositeType *di_composite()
    {
        return llvm::dyn_cast< llvm::DICompositeType >( di_base() );
    }

    int width()
    {
        if ( di_member() )
            return di_member()->getSizeInBits();
        return 8 * size();
    }

    int bitoffset()
    {
        int rv = 0;
        if ( di_member() )
            rv = di_member()->getOffsetInBits() - 8 * _offset;
        ASSERT_LEQ( rv, 8 * size() );
        ASSERT_LEQ( rv + width(), 8 * size() );
        return rv;
    }

    void init()
    {
        if ( _kind == DNKind::Frame )
            _ctx.set( _VM_CR_Frame, _address );
    }

    DebugNode( Context ctx, Snapshot s, GenericPointer l, int off,
               DNKind k, llvm::Type *t, llvm::DIType *dit )
        : _ctx( ctx.program(), ctx.heap() ), _address( l ), _offset( off ),
          _snapshot( s ), _kind( k ), _type( t ), _di_type( dit )
    {
        _ctx.heap().restore( s );
        _ctx.set( _VM_CR_Globals, ctx.globals() );
        _ctx.set( _VM_CR_Constants, ctx.constants() );
        init();
    }

    DebugNode( ConstContext< Program, Heap > ctx, Snapshot s,
               GenericPointer l, int off, DNKind k, llvm::Type *t, llvm::DIType *dit )
        : _ctx( ctx ), _address( l ), _offset( off ),
          _snapshot( s ), _kind( k ), _type( t ), _di_type( dit )
    {
        init();
    }

    DebugNode( const DebugNode &o ) = default;

    void relocate( typename Heap::Snapshot s )
    {
        _ctx.heap().restore( s );
    }

    DNKind kind() { return _kind; }
    GenericPointer address() { return _address; }
    Snapshot snapshot() { return _snapshot; }
    std::string raw();

    bool valid()
    {
        if ( _address.null() )
            return false;
        if ( _address.type() == PointerType::Heap && !_ctx.heap().valid( _address ) )
            return false;
        return true;
    }

    template< typename Y >
    void value( Y yield, Eval &eval )
    {
        if ( _type && _type->isIntegerTy() )
            eval.template type_dispatch< IsIntegral >(
                _type->getPrimitiveSizeInBits(), Program::Slot::Integer,
                [&]( auto v )
                {
                    auto raw = v.get( PointerV( _address + _offset ) );
                    using V = decltype( raw );
                    if ( bitoffset() || width() != size() * 8 )
                    {
                        yield( "@raw_value", brick::string::fmt( raw ) );
                        auto val = raw >> value::Int< 32 >( bitoffset() );
                        val = val & V( bitlevel::ones< typename V::Raw >( width() ) );
                        ASSERT_LEQ( bitoffset() + width(), size() * 8 );
                        yield( "@value", brick::string::fmt( val ) );
                    }
                    else
                        yield( "@value", brick::string::fmt( raw ) );
                } );
    }

    template< typename Y >
    void attributes( Y yield )
    {
        Eval eval( _ctx.program(), _ctx );
        Program &program = _ctx.program();

        yield( "@address", brick::string::fmt( PointerV( _address ) ) );

        if ( _di_type )
            yield( "@type", di_base()->getName().str() );

        if ( !valid() )
            return;

        auto hloc = eval.ptr2h( PointerV( _address ) );
        value( yield, eval );

        yield( "@raw", print::raw( _ctx.heap(), hloc + _offset, size() ) );

        if ( _address.type() == PointerType::Const || _address.type() == PointerType::Global )
            yield( "@slot", brick::string::fmt( eval.ptr2s( _address ) ) );

        if ( _kind == DNKind::Frame )
        {
            if ( pc().null() )
                return;
            auto *insn = &program.instruction( pc() );
            if ( insn->op )
            {
                eval._instruction = insn;
                yield( "@instruction", print::instruction( eval ) );
            }
            if ( !insn->op )
                insn = &program.instruction( pc() + 1 );
            ASSERT( insn->op );
            auto op = llvm::cast< llvm::Instruction >( insn->op );
            yield( "@location", location( *op ) );

            auto sym = op->getParent()->getParent()->getName().str();
            yield( "@symbol", print::demangle( sym ) );
        }
    }

    CodePointer pc()
    {
        ASSERT_EQ( kind(), DNKind::Frame );
        PointerV pc;
        _ctx.heap().read( _address, pc );
        return pc.cooked();
    }

    llvm::Function *llvmfunction()
    {
        ASSERT_EQ( kind(), DNKind::Frame );
        return _ctx.program().llvmfunction( pc() );
    }

    llvm::DISubprogram *subprogram()
    {
        return llvm::getDISubprogram( llvmfunction() );
    }

    void bitcode( std::ostream &out )
    {
        ASSERT_EQ( _kind, DNKind::Frame );
        Eval eval( _ctx.program(), _ctx );
        CodePointer iter = pc();
        iter.instruction( 0 );
        auto &instructions = _ctx.program().function( iter ).instructions;
        for ( auto &i : instructions )
        {
            eval._instruction = &i;
            out << ( iter == pc() ? ">>" : "  " );
            if ( i.op )
                out << print::instruction( eval, 2 ) << std::endl;
            else
            {
                auto iop = llvm::cast< llvm::Instruction >( instructions[ iter.instruction() + 1 ].op );
                auto name = iop->getParent()->getName().str();
                out << "label %"
                    << ( name.empty() ? brick::string::fmt( iter.instruction() ) : name )
                    << ":" << std::endl;
            }
            iter = iter + 1;
        }
    }

    void source( std::ostream &out )
    {
        ASSERT_EQ( _kind, DNKind::Frame );
        auto di = subprogram();
        out << print::source( subprogram(), _ctx.program(), pc() );
    }

    template< typename Y >
    void related( Y yield )
    {
        if ( !valid() )
            return;

        Eval eval( _ctx.program(), _ctx );

        PointerV ptr;
        auto hloc = eval.ptr2h( PointerV( _address ) );
        int hoff = hloc.offset();

        std::set< GenericPointer > ptrs;

        if ( _kind == DNKind::Frame )
            framevars( yield, eval, ptrs );

        if ( _type && _di_type && _type->isPointerTy() )
        {
            PointerV addr;
            _ctx.heap().read( hloc + _offset, addr );
            ptrs.insert( addr.cooked() );
            yield( "@deref", DebugNode( _ctx, _snapshot, addr.cooked(), 0, DNKind::Object,
                                        _type->getPointerElementType(), di_base() ) );
        }

        if ( _type && _di_type && _type->isStructTy() )
            struct_fields( hloc, yield, ptrs );

        for ( auto ptroff : _ctx.heap().pointers( hloc, hoff + _offset, size() ) )
        {
            hloc.offset( hoff + _offset + ptroff->offset() );
            _ctx.heap().read( hloc, ptr );
            auto pp = ptr.cooked();
            if ( pp.type() == PointerType::Code || ptrs.find( pp ) != ptrs.end() )
                continue;
            pp.offset( 0 );
            yield( "@" + brick::string::fmt( ptroff->offset() ),
                   DebugNode( _ctx, _snapshot, pp, 0, DNKind::Object, nullptr, nullptr ) );
        }
    }

    template< typename Y >
    void struct_fields( HeapPointer hloc, Y yield, std::set< GenericPointer > &ptrs )
    {
        auto CT = llvm::dyn_cast< llvm::DICompositeType >( _di_type );
        if ( !CT )
        {
            auto DIT = llvm::cast< llvm::DIDerivedType >( _di_type );
            auto base = DIT->getBaseType().resolve( _ctx.program().ditypemap );
            CT = llvm::dyn_cast< llvm::DICompositeType >( base );
            ASSERT( CT );
        }
        auto ST = llvm::cast< llvm::StructType >( _type );
        auto STE = ST->element_begin();
        auto SLO = _ctx.program().TD.getStructLayout( ST );
        int idx = 0;
        for ( auto subtype : CT->getElements() )
            if ( auto CTE = llvm::dyn_cast< llvm::DIDerivedType >( subtype ) )
            {
                if ( idx + 1 < int( ST->getNumElements() ) &&
                     CTE->getOffsetInBits() >= 8 * SLO->getElementOffset( idx + 1 ) )
                    idx ++, STE ++;

                int offset = SLO->getElementOffset( idx );
                if ( (*STE)->isPointerTy() )
                {
                    PointerV ptr;
                    _ctx.heap().read( hloc + offset, ptr );
                    ptrs.insert( ptr.cooked() );
                }

                yield( CTE->getName().str(),
                       DebugNode( _ctx, _snapshot, _address, offset,
                                  DNKind::Object, *STE, CTE ) );
            }
    }

    template< typename Y >
    void localvar( Y yield, Eval &eval, llvm::DbgDeclareInst *DDI, std::set< GenericPointer > &ptrs )
    {
        auto divar = DDI->getVariable();
        auto ditype = divar->getType().resolve( _ctx.program().ditypemap );
        auto var = DDI->getAddress();
        auto &vmap = _ctx.program().valuemap;
        if ( vmap.find( var ) == vmap.end() )
            return;

        PointerV ptr;
        _ctx.heap().read( eval.s2ptr( _ctx.program().valuemap[ var ].slot ), ptr );
        ptrs.insert( ptr.cooked() );

        auto type = var->getType()->getPointerElementType();
        yield( divar->getName().str(),
               DebugNode( _ctx, _snapshot, ptr.cooked(), 0, DNKind::Object, type, ditype ) );
    }

    template< typename Y >
    void framevars( Y yield, Eval &eval, std::set< GenericPointer > &ptrs )
    {
        PointerV fr( _address );
        _ctx.heap().skip( fr, PointerBytes );
        _ctx.heap().read( fr.cooked(), fr );
        if ( !fr.cooked().null() )
        {
            ptrs.insert( fr.cooked() );
            yield( "@parent", DebugNode( _ctx, _snapshot, fr.cooked(), 0,
                                         DNKind::Frame, nullptr, nullptr ) );
        }

        auto *insn = &_ctx.program().instruction( pc() );
        if ( !insn->op )
            insn = &_ctx.program().instruction( pc() + 1 );
        auto op = llvm::cast< llvm::Instruction >( insn->op );
        auto F = op->getParent()->getParent();

        for ( auto &BB : *F )
            for ( auto &I : BB )
                if ( auto DDI = llvm::dyn_cast< llvm::DbgDeclareInst >( &I ) )
                    localvar( yield, eval, DDI, ptrs );
    }

    void dump( std::ostream &o );
    void dot( std::ostream &o );
};

}
}
