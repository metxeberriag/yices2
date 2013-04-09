/*
 * SUPPORT FOR SMT-LIB 2.0: ALL COMMANDS
 */

/*
 * This module implements all the SMT-LIB 2 functions.
 * The intended use is as follows:
 *
 *   yices_init();
 *   init_smt2(flag);
 *   initialize parser + lexer;
 *   while (smt2_active()) {
 *     parse an smt2 command;
 *     // as a result, one of the functions below is called
 *   }
 *   delete parser and lexer;
 *   delete_smt2();
 *   yices_exit();
 *
 * The tstack in smt2_parser will invoke the top-level SMT2 commands
 * and will call the error-reporting functions on a syntax error or
 * exception raised by the term stack.
 */

#ifndef __SMT2_COMMANDS_H
#define __SMT2_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "lexer.h"
#include "term_stack2.h"


/*
 * Initialize all internal structures
 * - benchmark: if true, the input is assumed to be an SMT-LIB 2.0 benchmark
 *  (i.e., a set of assertions followed by a single call to check-sat)
 *  In this mode, destructive simplifications are allowed.
 * - this is called after yices_init so all Yices internals are ready
 */
extern void init_smt2(bool benchmark);


/*
 * Delete all internal structures (called after exit).
 */
extern void delete_smt2(void);


/*
 * Check whether the smt2 solver is ready
 * - this must be true after init_smt2()
 * - this must return false if smt2_exit has been called or after
 *   an unrecoverable error
 */
extern bool smt2_active(void);



/*
 * TOP-LEVEL SMT2 COMMANDS
 */

/*
 * Exit function (also called on end-of-file)
 */
extern void smt2_exit(void);


/*
 * Show all formulas asserted so far
 */
extern void smt2_get_assertions(void);


/*
 * Show the truth value of named Boolean terms 
 * (i.e., those that have a :named attribute)
 */
extern void smt2_get_assignment(void);


/*
 * Show a proof when context is unsat
 */
extern void smt2_get_proof(void);


/*
 * Get the unsat core: subset of :named assertions that form an unsat core
 */
extern void smt2_get_unsat_core(void);


/*
 * Get the values of terms in the model
 * - the terms are listed in array a
 * - n = number of elements in the array
 */
extern void smt2_get_value(term_t *a, uint32_t n);


/*
 * Get the value of an option
 * - name = option name (a keyword)
 */
extern void smt2_get_option(const char *name);


/*
 * Get some info
 * - name = keyword
 */
extern void smt2_get_info(const char *name);


/*
 * Set an option:
 * - name = option name (keyword)
 * - value = value (stored in the attribute_value table)
 *
 * SMT2 allows the syntax (set-option :keyword). In such a case,
 * this function is called with value = NULL_VALUE (i.e., -1).
 */
extern void smt2_set_option(const char *name, aval_t value);


/*
 * Set some info field
 * - same conventions as set_option
 */
extern void smt2_set_info(const char *name, aval_t value);


/*
 * Set the logic:
 * - name = logic name (using the SMT-LIB conventions)
 */
extern void smt2_set_logic(const char *name);


/*
 * Push 
 * - n = number of scopes to push
 * - if n = 0, nothing should be done
 */
extern void smt2_push(uint32_t n);


/*
 * Pop:
 * - n = number of scopes to remove
 * - if n = 0 nothing should be done
 * - if n > total number of scopes then an error should be printed
 *   and nothing done
 */
extern void smt2_pop(uint32_t n);


/*
 * Assert one formula t
 * - if t is a :named assertion then it should be recorded for unsat-core
 */
extern void smt2_assert(term_t t);


/*
 * Check satsifiability of the current set of assertions
 */
extern void smt2_check_sat(void);


/*
 * Declare a new sort:
 * - name = sort name
 * - arity = arity
 *
 * If arity is 0, this defines a new uninterpreted type.
 * Otherwise, this defines a new type constructor.
 */
extern void smt2_declare_sort(const char *name, uint32_t arity);


/*
 * Define a new type macro
 * - name = macro name
 * - n = number of variables
 * - var = array of type variables
 * - body = type expressions
 */
extern void smt2_define_sort(const char *name, uint32_t n, type_t *var, type_t body);


/*
 * Declare a new uninterpreted function symbol
 * - name = function name
 * - n = arity
 * - tau = array of n+1 types
 *
 * If n = 0, this creates an uninterpreted constant of type tau[0]
 * Otherwise, this creates an uninterpreted function of type tau[0] x ... x tau[n-1] to tau[n] 
 */
extern void smt2_declare_fun(const char *name, uint32_t n, type_t *tau);


/*
 * Define a function
 * - name = function name
 * - n = arity
 * - var = array of n term variables
 * - body = term
 * - tau = expected type of body
 *
 * If n = 0, this is the same as (define <name> :: <type> <body> )
 * Otherwise, a lambda term is created.
 */
extern void smt2_define_fun(const char *name, uint32_t n, term_t *var, term_t body, type_t tau);



/*
 * ATTRIBUTES
 */

/*
 * Convert rational q to an attribute value
 * - must return an aval_t index
 * - don't increment its refcounter
 */
extern aval_t smt2_rational_attr(rational_t *q);

/*
 * Convert a bitvector constant to an attribute value
 * - must return an aval_t index
 * - don't increment its refcounter
 *
 * smt2_bv64_attr(n, c)
 * - n = number of bits (with 1 <= n <= 64)
 * - c = constant value (normalized modulo 2^n)
 *
 * smt2_bv_attr(n, c)
 * - n = number of bits (with 64 < n)
 * - c = constant value (normalized modulo 2^n)
 *   c is an array of 32bit words
 */
extern aval_t smt2_bv64_attr(uint32_t n, uint64_t c);
extern aval_t smt_bv_attr(uint32_t n, uint32_t *c);


/*
 * Convert a keyword or symbol to an attribute value
 * - must return an aval_t index
 * - don't increment its refcounter
 */
extern aval_t smt_symbol_attr(const char *name);


/*
 * Same thing for a string
 */
extern aval_t smt_string_attr(const char *name);


/*
 * Build an attribute list (s-expr)
 * - a[0] ... a[n-1] = list elements
 * - each element in the list is an aval_t index
 *   returned by the attr functions
 * - this must return an aval_t index representing the list
 * - don't increment its ref counter
 */
extern aval_t smt2_list_attr(uint32_t n, aval_t *a);


/*
 * Add a :named attribute to term t
 */
extern void smt2_add_name(term_t t, const char *name);


/*
 * Add a :pattern attribute to term t
 * - the pattern is a term p
 */
extern void smt2_add_pattern(term_t t, term_t p);



/*
 * ERROR REPORTING
 */

/*
 * Syntax error
 * - lex = lexer 
 * - expected_token = either an smt2_token or -1
 *
 * lex is as follows: 
 * - current_token(lex) = token that caused the error
 * - current_token_value(lex) = corresponding string
 * - current_token_length(lex) = length of that string
 * - lex->tk_line and lex->tk_column = start of the token in the input
 * - lex->reader.name  = name of the input file (NULL means input is stdin)
 */
extern void smt2_syntax_error(lexer_t *lex, int32_t expected_token);


/*
 * Exception raised by the tstack
 * - reader_name = same as lex->reader.name above
 * - tstack = term stack
 * - exception = error code (defined in term_stack2.h)
 *
 * Error location in the input file is given by 
 * - tstack->error_loc.line
 * - tstack->error_loc.column
 *
 * Extra fields (depending on the exception)
 * - tstack->error_string = erroneous input
 * - tstack->error_op = erroneous operation
 */
extern void smt2_tstack_error(const char *reader_name, tstack_t *tstack, tstack_error_t exception);


#endif /* __SMT2_COMMANDS_H */
