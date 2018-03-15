// -*- C++ -*- (c) 2018 Henrich Lauko <xlauko@mail.muni.cz>
#include <lart/abstract/util.h>

DIVINE_RELAX_WARNINGS
#include <llvm/IR/Type.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
DIVINE_UNRELAX_WARNINGS

#include <unordered_map>

namespace lart {
namespace abstract {

using namespace llvm;

std::string llvm_name( Type *type ) {
    std::string buffer;
    raw_string_ostream rso( buffer );
    type->print( rso );
    return rso.str();
}

Values taints( Module &m ) {
    Values res;
    for ( auto &fn : m )
        if ( fn.getName().startswith( "__vm_test_taint" ) )
            for ( auto u : fn.users() )
                res.push_back( u );
    return res;
}

Function* get_function( Argument *a ) {
    return a->getParent();
}

Function* get_function( Instruction *i ) {
    return i->getParent()->getParent();
}

Function* get_function( Value *v ) {
    if ( auto arg = dyn_cast< Argument >( v ) )
        return get_function( arg );
    return get_function( cast< Instruction >( v ) );
}

Module* get_module( llvm::Value *v ) {
    return get_function( v )->getParent();
}

} // namespace abstract
} // namespace lart
