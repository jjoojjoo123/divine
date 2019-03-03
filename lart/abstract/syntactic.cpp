// -*- C++ -*- (c) 2018 Henrich Lauko <xlauko@mail.muni.cz>
#include <lart/abstract/syntactic.h>

DIVINE_RELAX_WARNINGS
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
DIVINE_UNRELAX_WARNINGS

#include <lart/support/util.h>
#include <lart/abstract/util.h>
#include <lart/abstract/placeholder.h>

namespace lart::abstract
{
    void Syntactic::run( llvm::Module &m ) {
        auto abstract = query::query( meta::enumerate( m ) )
            .map( query::llvmdyncast< llvm::Instruction > )
            .filter( query::notnull )
            .filter( is_transformable )
            // skip alredy processed instructions
            .filter( [] ( auto * inst ) {
                return !meta::has_dual( inst );
            } )
            .freeze();

        APlaceholderBuilder builder;
        for ( const auto &inst : abstract ) {
            if ( llvm::isa< llvm::ReturnInst >( inst ) ) // TODO remove
                continue;
            if ( !llvm::isa< llvm::CallInst >( inst ) ) // TODO remove
                builder.construct( inst );
        }

    }

} // namespace lart::abstract
