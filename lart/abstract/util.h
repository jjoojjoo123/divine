// -*- C++ -*- (c) 2017 Henrich Lauko <xlauko@mail.muni.cz>
#pragma once

DIVINE_RELAX_WARNINGS
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
DIVINE_UNRELAX_WARNINGS

#include <lart/support/query.h>

#include <lart/abstract/domains/domains.h>

namespace lart {
namespace abstract {

using Types = std::vector< llvm::Type * >;
using Values = std::vector< llvm::Value * >;
using Functions = std::vector< llvm::Function * >;

template< typename Values >
Types types_of( const Values & vs ) {
    return query::query( vs ).map( [] ( const auto & v ) {
        return v->getType();
    } ).freeze();
}

template< typename... Ts >
bool is_one_of( llvm::Value *v ) {
    return ( llvm::isa< Ts >( v ) || ... );
}

bool is_intr( llvm::CallInst *intr, std::string name );
bool is_lift( llvm::CallInst *intr );
bool is_lower( llvm::CallInst *intr );
bool is_assume( llvm::CallInst *intr );
bool is_rep( llvm::CallInst *intr );
bool is_unrep( llvm::CallInst *intr );
bool is_tobool( llvm::CallInst *intr );
bool is_cast( llvm::CallInst *intr );

std::string llvm_name( llvm::Type *type );

Values taints( llvm::Module &m );

llvm::Function* get_function( llvm::Argument *a );
llvm::Function* get_function( llvm::Instruction *i );
llvm::Function* get_function( llvm::Value *v );

llvm::Function* get_or_insert_function( llvm::Module*, llvm::FunctionType*, llvm::StringRef );

llvm::Module* get_module( llvm::Value *val );

llvm::Type* abstract_type( llvm::Type *orig, Domain dom );

llvm::Value* placeholder( llvm::Value *val );

bool is_base_type( llvm::Type *type );

} // namespace abstract
} // namespace lart
