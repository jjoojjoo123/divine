// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4 -*-

/*
 * (c) 2012-2016 Petr Ročkai <code@fixp.eu>
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

#include <divine/vm/eval.hpp>
#include <divine/vm/program.hpp>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

using namespace divine::vm;

ConstContext::PointerV Program::s2hptr( Program::Slot v, int offset )
{
    Eval< Program, ConstContext, value::Void > eval( *this, _ccontext );
    return eval.s2ptr( v, offset );
}

void Program::initStatic( Program::Slot v, llvm::Value *V )
{
    bool done = true;

    auto &heap = _ccontext.heap();
    Eval< Program, ConstContext, value::Void > eval( *this, _ccontext );
    auto ptr = eval.s2ptr( v );
    auto C = dyn_cast< llvm::Constant >( V );

    if ( auto GA = dyn_cast< llvm::GlobalAlias >( V ) ) 
        C = GA->getBaseObject();

    if ( !valuemap.count( V ) )
    {
        V->dump();
        UNREACHABLE( "value not allocated during initialisation" );
    }

    if ( _doneinit.count( V ) )
        return;

    // functions have only personality, it need not be initialized before the
    // function besides, some transformations might add personality even to the
    // personality function, making this go into infinite loop
    if ( C && !isa< llvm::Function >( C ) )
        for ( int i = 0; i < int( C->getNumOperands() ); ++i )
        {
            auto op = C->getOperand( i );
            if ( !_doneinit.count( op ) )
                done = false;
            if ( !isa< llvm::GlobalVariable >( op ) )
                _toinit.emplace_back( [= ]{
                    ASSERT( valuemap.find(op) != valuemap.end() );
                    initStatic( valuemap[ op ].slot, op );
                } );
        }

    if ( auto CE = dyn_cast< llvm::ConstantExpr >( V ) )
    {
        Instruction comp;
        comp.op = CE;
        comp.opcode = CE->getOpcode();
        comp.values.push_back( v ); /* the result comes first */
        for ( int i = 0; i < int( C->getNumOperands() ); ++i ) // now the operands
        {
            if ( !valuemap.count( C->getOperand( i ) ) )
            {
                C->dump();
                C->getOperand( i )->dump();
                UNREACHABLE( "oops" );
            }
            comp.values.push_back( valuemap[ C->getOperand( i ) ].slot );
        }
        eval._instruction = &comp;
        eval.dispatch(); /* compute and write out the value */
    }
    else if ( isa< llvm::UndefValue >( V ) )
    {
        /* nothing to do (for now; we don't track uninitialised values yet) */
    }
    else if ( auto I = dyn_cast< llvm::ConstantInt >( V ) )
    {
        const uint8_t *mem = reinterpret_cast< const uint8_t * >( I->getValue().getRawData() );
        std::for_each( mem, mem + v.width, [&]( char c )
                       {
                           heap.shift( ptr, value::Int< 8 >( c ) );
                       } );
    }
    else if ( auto FP = dyn_cast< llvm::ConstantFP >( V ) )
    {
        switch ( v.width ) {
            case sizeof( float ):
                heap.shift( ptr, value::Float< float >( FP->getValueAPF().convertToFloat() ) );
                break;
            case sizeof( double ):
                heap.shift( ptr, value::Float< double >( FP->getValueAPF().convertToDouble() ) );
                break;
            case sizeof( long double ): {
                bool lossy;
                llvm::APFloat x = FP->getValueAPF();
                x.convert( llvm::APFloat::IEEEdouble, llvm::APFloat::rmNearestTiesToEven, &lossy );
                /* FIXME? */
                heap.shift( ptr, value::Float< long double >( x.convertToDouble() ) );
                break;
            }
            default: UNREACHABLE( "non-double, non-float FP constant" );
        }
    }
    else if ( isa< llvm::ConstantPointerNull >( V ) )
    {
        /* nothing to do, everything is zeroed by default */
    }
    else if ( isa< llvm::GlobalVariable >( V ) )
    {
        UNREACHABLE( "an unexpected llvm::GlobalVariable" );
    }
    else if ( isa< llvm::ConstantAggregateZero >( V ) )
    {
        /* nothing to do, everything is zeroed by default */
    }
    else if ( isCodePointer( V ) )
    {
        heap.shift( ptr, value::Pointer<>( getCodePointer( V ) ) );
    }
    else if ( V->getType()->isPointerTy() )
    {
        V->dump();
        UNREACHABLE( "an unexpected non-zero constant pointer" );
    }
    else if ( isa< llvm::ConstantArray >( V ) || isa< llvm::ConstantStruct >( V ) )
    {
        int offset = 0;
        for ( int i = 0; i < int( C->getNumOperands() ); ++i )
        {
            if ( auto CS = dyn_cast< llvm::ConstantStruct >( C ) )
            {
                const llvm::StructLayout *SLO = TD.getStructLayout(CS->getType());
                offset = SLO->getElementOffset( i );
            }

            auto sub = valuemap[ C->getOperand( i ) ].slot;
            heap.copy( eval.s2ptr( sub ), eval.s2ptr( v, offset ), sub.width );
            offset += sub.width;
            ASSERT_LEQ( offset, int( v.width ) );
        }
        /* and padding at the end ... */
    }
    else if ( auto CDS = dyn_cast< llvm::ConstantDataSequential >( V ) )
    {
        ASSERT_EQ( v.width, CDS->getNumElements() * CDS->getElementByteSize() );
        const char *raw = CDS->getRawDataValues().data();
        std::for_each( raw, raw + v.width, [&]( char c )
                       {
                           heap.shift( ptr, value::Int< 8 >( c ) );
                       } );
    }
    else if ( dyn_cast< llvm::ConstantVector >( V ) )
    {
        NOT_IMPLEMENTED();
    }
    else
    {
        V->dump();
        UNREACHABLE( "unknown constant type" );
    }

    if ( done )
        _doneinit.insert( V );
    else
        _toinit.emplace_back( [=]{ initStatic( v, V ); } );
}
