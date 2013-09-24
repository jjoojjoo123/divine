#include <divine/llvm/execution.h>
#include <divine/llvm/program.h>
#include <divine/llvm/wrap/GlobalVariable.h>

using namespace divine::llvm;

void ProgramInfo::storeConstant( ProgramInfo::Value v, ::llvm::Constant *C, bool global )
{
    GlobalContext econtext( *this, TD, global );
    if ( auto CE = dyn_cast< ::llvm::ConstantExpr >( C ) ) {
        ControlContext ccontext;
        Evaluator< GlobalContext, ControlContext > eval( *this, econtext, ccontext );

        Instruction &comp = eval.instruction;
        comp.op = CE;
        comp.opcode = CE->getOpcode();
        comp.values.push_back( v ); /* the result comes first */
        for ( int i = 0; i < int( CE->getNumOperands() ); ++i ) // now the operands
            comp.values.push_back( insert( 0, CE->getOperand( i ) ) );
        eval.run(); /* compute and write out the value */
    } else if ( dyn_cast< ::llvm::GlobalVariable >( C ) ) {
        char *address = econtext.dereference( insert( 0, C ) ); // insert will call us recursively as needed
        std::copy( address, address + v.width, econtext.dereference( v ) );
    } else if ( isa< ::llvm::UndefValue >( C ) ) {
        /* nothing to do (for now; we don't track uninitialised values yet) */
    } else if ( auto I = dyn_cast< ::llvm::ConstantInt >( C ) ) {
        const uint8_t *mem = reinterpret_cast< const uint8_t * >( I->getValue().getRawData() );
        std::copy( mem, mem + v.width, econtext.dereference( v ) );
    } else if ( auto FP = dyn_cast< ::llvm::ConstantFP >( C ) ) {
        float fl = FP->getValueAPF().convertToFloat();
        double dbl = FP->getValueAPF().convertToDouble();
        const uint8_t *mem;
        switch ( v.width ) {
            case sizeof( float ): mem = reinterpret_cast< uint8_t * >( &fl ); break;
            case sizeof( double ): mem = reinterpret_cast< uint8_t * >( &dbl ); break;
            default: assert_unreachable( "non-double, non-float FP constant" );
        }
        std::copy( mem, mem + v.width, econtext.dereference( v ) );
    } else if ( isa< ::llvm::ConstantPointerNull >( C ) ) {
        /* nothing to do, everything is zeroed by default */
    } else if ( isa< ::llvm::ConstantAggregateZero >( C ) ) {
        /* nothing to do, everything is zeroed by default */
    } else if ( isCodePointer( C ) ) {
        *reinterpret_cast< PC * >( econtext.dereference( v ) ) =
            getCodePointer( C );
    } else if ( C->getType()->isPointerTy() ) {
        C->dump();
        assert_unreachable( "unexpected non-zero constant pointer" );
    } else if ( isa< ::llvm::ConstantArray >( C ) || isa< ::llvm::ConstantStruct >( C ) ) {
        int offset = 0;
        for ( int i = 0; i < int( C->getNumOperands() ); ++i ) {
            auto sub = insert( 0, C->getOperand( i ) );
            char *from = econtext.dereference( sub );
            char *to = econtext.dereference( v ) + offset;
            std::copy( from, from + sub.width, to );
            offset += sub.width;
            assert_leq( offset, int( v.width ) );
        }
        /* and padding at the end ... */
    } else if ( auto CDS = dyn_cast< ::llvm::ConstantDataSequential >( C ) ) {
        assert_eq( v.width, CDS->getNumElements() * CDS->getElementByteSize() );
        const char *raw = CDS->getRawDataValues().data();
        std::copy( raw, raw + v.width, econtext.dereference( v ) );
    } else if ( dyn_cast< ::llvm::ConstantVector >( C ) ) {
        assert_unimplemented();
    } else {
        C->dump();
        assert_unreachable( "unknown constant type" );
    }
}

