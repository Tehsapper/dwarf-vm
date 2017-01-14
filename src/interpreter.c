#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#include <common.h>
#include <function.h>
#include <vm.h>
#include <inttypes.h>
#include <instructions.h>

static vm_val val_at( void* ptr ) {return *( (vm_val*)ptr ); }

static const bool DEBUG = 1;

/** @addtogroup interpreter 
 * @{ */

#define OPCODE_HANDLER(OPCODE) \
    __attribute__((always_inline)) \
static void interpret_##OPCODE(struct vm_machine* vm, FILE *debug)

#define OPCODE_HANDLER_UNIMPLEMENTED(OPCODE) \
    OPCODE_HANDLER(OPCODE) { \
        (void) vm; \
        fprintf( debug, "Opcode " #OPCODE " is not implemented.\n" ); \
        exit(2); \
    }

OPCODE_HANDLER(INVALID) {
    (void) vm; fprintf( debug, "Invalid operation\n" ); exit(2);
}

OPCODE_HANDLER(LOAD) {

        vm->data_stack_ptr --;
        *(vm->data_stack_ptr) = val_at( vm-> instr_ptr+1 );
        if (DEBUG) fprintf( debug, "\tvalue: %d", *(vm->data_stack_ptr) );
    }

OPCODE_HANDLER(LOADS) {
        vm->data_stack_ptr--;
        const vm_val id = val_at( vm-> instr_ptr+1 ) ;
        vm->data_stack_ptr->as_ptr = (void*) 
            vm->prog.consts.by_id[id.as_int] ;
    }

/** Create an interpreter case to perform a binary operator on stack elements
 *
 * @param mnemonic Bytecode mnemonic
 * @param type     Either `double` or `int64_t` depending on number formats
 * @param action   Binary infix operand such as `+` or `/`
 */ 
#define DEFINE_BINOP(mnemonic, type, action)\
OPCODE_HANDLER(mnemonic) {\
        const vm_val* x = vm->data_stack_ptr ;\
        const vm_val* y = vm->data_stack_ptr + 1;\
        vm->data_stack_ptr++;\
        vm->data_stack_ptr->as_##type = x->as_##type action y->as_##type; \
    }

DEFINE_BINOP( DADD, double,  + )
DEFINE_BINOP( DMUL, double,  * )
DEFINE_BINOP( DSUB, double,  - )
DEFINE_BINOP( DDIV, double,  / )

DEFINE_BINOP( IADD, int,  + )
DEFINE_BINOP( IMUL, int,  * )
DEFINE_BINOP( ISUB, int,  - )
DEFINE_BINOP( IDIV, int,  / )
DEFINE_BINOP( IMOD, int,  % )

#undef DEFINE_BINOP

OPCODE_HANDLER(DNEG) {
    vm->data_stack_ptr->as_double =  - vm->data_stack_ptr->as_double; 
    }

OPCODE_HANDLER(INEG) {
    vm->data_stack_ptr->as_int =  - vm->data_stack_ptr->as_int ; 
   }

OPCODE_HANDLER(IPRINT) {
        printf("%"PRIi64, vm->data_stack_ptr->as_int); 
        vm->data_stack_ptr++; 
    }

OPCODE_HANDLER(DPRINT) {
        printf("%lf", vm->data_stack_ptr->as_double );
        vm->data_stack_ptr++; 
    }

OPCODE_HANDLER(SPRINT) {
        fputs( vm->data_stack_ptr->as_ptr, stdout );
        vm->data_stack_ptr++; 
    }

OPCODE_HANDLER_UNIMPLEMENTED(I2D);
OPCODE_HANDLER_UNIMPLEMENTED(D2I);
OPCODE_HANDLER_UNIMPLEMENTED(S2I);
OPCODE_HANDLER_UNIMPLEMENTED(SWAP);
OPCODE_HANDLER_UNIMPLEMENTED(POP);

OPCODE_HANDLER(LOADVAR) {
        uint32_t id = *(uint32_t*)( vm->instr_ptr + 1);
        vm->data_stack_ptr--;
        *(vm->data_stack_ptr) = vm->ctx_stack_ptr->locals[ id ];
    }

OPCODE_HANDLER_UNIMPLEMENTED(LOADSVAR);

OPCODE_HANDLER(STOREVAR) {
        uint32_t id = *(uint32_t*)( vm->instr_ptr + 1);
        vm->ctx_stack_ptr->locals[ id ] = *(vm->data_stack_ptr);
        vm->data_stack_ptr++;
    }

OPCODE_HANDLER_UNIMPLEMENTED(LOADCTXVAR);
OPCODE_HANDLER_UNIMPLEMENTED(STORECTXVAR);
OPCODE_HANDLER_UNIMPLEMENTED(STORECTXSVAR);
OPCODE_HANDLER_UNIMPLEMENTED(DCMP);
OPCODE_HANDLER_UNIMPLEMENTED(ICMP);

OPCODE_HANDLER(JA) {
        int16_t offset = *(int16_t*)( vm->instr_ptr + 1);
        vm->instr_ptr += offset;
    }

OPCODE_HANDLER_UNIMPLEMENTED(IFICMPNE);
OPCODE_HANDLER_UNIMPLEMENTED(IFICMPE);
OPCODE_HANDLER_UNIMPLEMENTED(IFICMPG);
OPCODE_HANDLER_UNIMPLEMENTED(IFICMPGE);
OPCODE_HANDLER_UNIMPLEMENTED(IFICMPL);

OPCODE_HANDLER(IFICMPLE) {
        int16_t offset = *(int16_t*)( vm->instr_ptr + 1);
        vm_val right = *vm->data_stack_ptr++;
        vm_val left = *vm->data_stack_ptr++;
        if (DEBUG) fprintf( debug, "\tleft: %d right: %d", left.as_int, right.as_int );
        if ( left.as_int <= right.as_int ) vm->instr_ptr += offset;
}
OPCODE_HANDLER_UNIMPLEMENTED(DUMP);

OPCODE_HANDLER(STOP) {exit(0); }


__attribute__((always_inline))
static void ctx_push( struct vm_machine* vm, void* ret_addr, struct vm_fun* fun) {

        const size_t ctx_size = sizeof( struct vm_ctx ) + fun->meta.locals_count * sizeof( vm_val );

        struct vm_ctx* const new_ctx = (void*)vm-> ctx_stack_ptr - ctx_size;
        struct vm_ctx* const old_ctx = vm->ctx_stack_ptr;

        new_ctx-> ret_addr = ret_addr,
        new_ctx-> ctx_prev = old_ctx;
        new_ctx-> ctx_prev_same_id = fun->meta.topmost_present;
        new_ctx-> fun = fun;
        fun->meta.topmost_present = new_ctx;

        vm->ctx_stack_ptr = new_ctx;
    }

__attribute__((always_inline))
static void ctx_pop( struct vm_machine* vm) {
        vm-> ctx_stack_ptr->fun->meta.topmost_present =
            vm-> ctx_stack_ptr->ctx_prev_same_id;
        vm-> ctx_stack_ptr = vm->ctx_stack_ptr->ctx_prev;
    }

OPCODE_HANDLER(CALL) {
        vm_val fun_id = val_at( vm->instr_ptr + 1);
        struct vm_fun* const fun = vm->prog.funs.by_id + fun_id.as_int;
       
        ctx_push( vm, vm-> instr_ptr + 9, fun );
        vm->instr_ptr = fun->code - 9;
    }

OPCODE_HANDLER_UNIMPLEMENTED(CALLNATIVE);

OPCODE_HANDLER(RETURN) {
        vm->instr_ptr = vm->ctx_stack_ptr->ret_addr - 1;
        ctx_pop( vm );
    }

OPCODE_HANDLER_UNIMPLEMENTED(BREAK);


    void interpret( struct vm_machine* vm, uint64_t id, FILE* debug ) {
        vm->instr_ptr = vm->prog.funs.by_id[id].code;
        ctx_push ( vm, NULL, &vm->prog.funs.by_id[id] );

#define LABEL_NAME_ARRAY(n,d,l) &&_##n,

        const void* const labels[] = {
            FOR_BYTECODES( LABEL_NAME_ARRAY ) 
                0 };

#undef LABEL_NAME_ARRAY

        goto *labels[ *vm->instr_ptr ];

#define INTERPRETER_CASE(n, d, l) \
        _##n: \
        if (DEBUG) fprintf( debug, "IP: %p " #n, vm->instr_ptr ); \
        interpret_##n( vm, debug ); \
        if (DEBUG) fprintf( debug, "\n" ); \
        vm->instr_ptr+=l; \
        goto *labels[ *vm->instr_ptr ];

        FOR_BYTECODES( INTERPRETER_CASE )

#undef INTERPRETER_CASE
    }


/** @} */
