/*
 * GLOBAL TERM/TYPE DATABASE
 */

/*
 * This module implements the term and type construction API defined in yices.h.
 * It also implements the functions defined in yices_extensions.h for managing 
 * buffers and converting buffers to terms.
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "refcount_strings.h"
#include "int_array_sort.h"
#include "dl_lists.h"
#include "int_vectors.h"
#include "bit_tricks.h"
#include "bv64_constants.h"

#include "types.h"
#include "pprod_table.h"
#include "bit_expr.h"
#include "terms.h"
#include "term_utils.h"

#include "bit_term_conversion.h"
#include "bvlogic_buffers.h"
#include "arith_buffer_terms.h"
#include "bvarith_buffer_terms.h"
#include "bvarith64_buffer_terms.h"

#include "yices.h"
#include "yices_extensions.h"
#include "yices_globals.h"
#include "yices_parser.h"
#include "yices_pp.h"



/*
 * Visibility control: all extern functions declared here are in libyices's API
 *
 * Other extern functions should have visibility=hidden (cf. Makefile).
 *
 * Note: adding __declspec(dllexport) should have the same effect
 * on cygwin or mingw but that does not seem to work.
 */
#if defined(CYGWIN) || defined(MINGW)
#define EXPORTED __declspec(dllexport)
#else
#define EXPORTED __attribute__((visibility("default")))
#endif



/*
 * Global tables:
 * - type table
 * - term table
 * - power product table
 * - node table
 *
 * Auxiliary structures:
 * - object stores used by the arithmetic and bit-vector 
 *   arithmetic buffers
 */
// global tables
static type_table_t types;
static term_table_t terms;
static pprod_table_t pprods;
static node_table_t nodes;

// arithmetic stores
static object_store_t arith_store;
static object_store_t bvarith_store;
static object_store_t bvarith64_store;

// error report
static error_report_t error;

// auxiliary rationals
static rational_t r0;
static rational_t r1;

// auxiliary bitvector constants
static bvconstant_t bv0;
static bvconstant_t bv1;
static bvconstant_t bv2;

// generic integer vector
static ivector_t vector0;

// parser, lexer, term stack: all are allocated on demand
static parser_t *parser;
static lexer_t *lexer;
static tstack_t *tstack;



/*
 * Initial sizes of the type and term tables.
 */
#define INIT_TYPE_SIZE  16
#define INIT_TERM_SIZE  64


/*
 * Doubly-linked list of arithmetic buffers
 */
typedef struct {
  dl_list_t header;
  arith_buffer_t buffer;
} arith_buffer_elem_t;

static dl_list_t arith_buffer_list;


/*
 * Doubly-linked list of bitvector arithmetic buffers
 */
typedef struct {
  dl_list_t header;
  bvarith_buffer_t buffer;
} bvarith_buffer_elem_t;

static dl_list_t bvarith_buffer_list;


/*
 * Variant: 64bit buffers
 */
typedef struct {
  dl_list_t header;
  bvarith64_buffer_t buffer;
} bvarith64_buffer_elem_t;

static dl_list_t bvarith64_buffer_list;


/*
 * Doubly-linked list of bitvector buffers
 */
typedef struct {
  dl_list_t header;
  bvlogic_buffer_t buffer;
} bvlogic_buffer_elem_t;

static dl_list_t bvlogic_buffer_list;


/*
 * Auxiliary buffer for internal conversion of equality and
 * disequalities to arithmetic atoms.
 */
static arith_buffer_t *internal_arith_buffer;

/*
 * Auxiliary buffers for bitvector polynomials.
 */
static bvarith_buffer_t *internal_bvarith_buffer;
static bvarith64_buffer_t *internal_bvarith64_buffer;

/*
 * Auxiliary bitvector buffer.
 */
static bvlogic_buffer_t *internal_bvlogic_buffer;


/*
 * Global table; initially all 7 pointers are NULL
 */
yices_globals_t __yices_globals = {
  NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};




/**********************************
 *  ARITHMETIC-BUFFER ALLOCATION  *
 *********************************/

/*
 * Get header of buffer b, assuming b is embedded into an arith_buffer_elem
 */
static inline dl_list_t *arith_buffer_header(arith_buffer_t *b) {
  return (dl_list_t *)(((char *)b) - offsetof(arith_buffer_elem_t, buffer));
}

/*
 * Get buffer of header l
 */
static inline arith_buffer_t *arith_buffer(dl_list_t *l) {
  return (arith_buffer_t *)(((char *) l) + offsetof(arith_buffer_elem_t, buffer));
}

/*
 * Allocate an arithmetic buffer and insert it into the list
 */
static inline arith_buffer_t *alloc_arith_buffer(void) {
  arith_buffer_elem_t *new_elem;

  new_elem = (arith_buffer_elem_t *) safe_malloc(sizeof(arith_buffer_elem_t));
  list_insert_next(&arith_buffer_list, &new_elem->header);
  return &new_elem->buffer;
}

/*
 * Remove b from the list and free b
 */
static inline void free_arith_buffer(arith_buffer_t *b) {
  dl_list_t *elem;

  elem = arith_buffer_header(b);
  list_remove(elem);
  safe_free(elem);
}

/*
 * Clean up the arith buffer list: free all elements and empty the list
 */
static void free_arith_buffer_list(void) {
  dl_list_t *elem, *aux;

  elem = arith_buffer_list.next;
  while (elem != &arith_buffer_list) {
    aux = elem->next;
    delete_arith_buffer(arith_buffer(elem));
    safe_free(elem);
    elem = aux;
  }

  clear_list(&arith_buffer_list);
}



/********************************************
 *  BITVECTOR ARITHMETIC BUFFER ALLOCATION  *
 *******************************************/

/*
 * Get header of buffer b, assuming b is embedded into an bvarith_buffer_elem
 */
static inline dl_list_t *bvarith_buffer_header(bvarith_buffer_t *b) {
  return (dl_list_t *)(((char *)b) - offsetof(bvarith_buffer_elem_t, buffer));
}

/*
 * Get buffer of header l
 */
static inline bvarith_buffer_t *bvarith_buffer(dl_list_t *l) {
  return (bvarith_buffer_t *)(((char *) l) + offsetof(bvarith_buffer_elem_t, buffer));
}

/*
 * Allocate a bv-arithmetic buffer and insert it into the list
 */
static inline bvarith_buffer_t *alloc_bvarith_buffer(void) {
  bvarith_buffer_elem_t *new_elem;

  new_elem = (bvarith_buffer_elem_t *) safe_malloc(sizeof(bvarith_buffer_elem_t));
  list_insert_next(&bvarith_buffer_list, &new_elem->header);
  return &new_elem->buffer;
}

/*
 * Remove b from the list and free b
 */
static inline void free_bvarith_buffer(bvarith_buffer_t *b) {
  dl_list_t *elem;

  elem = bvarith_buffer_header(b);
  list_remove(elem);
  safe_free(elem);
}

/*
 * Clean up the arith buffer list: free all elements and empty the list
 */
static void free_bvarith_buffer_list(void) {
  dl_list_t *elem, *aux;

  elem = bvarith_buffer_list.next;
  while (elem != &bvarith_buffer_list) {
    aux = elem->next;
    delete_bvarith_buffer(bvarith_buffer(elem));
    safe_free(elem);
    elem = aux;
  }

  clear_list(&bvarith_buffer_list);
}



/*********************************
 *  BVARITH64 BUFFER ALLOCATION  *
 ********************************/

/*
 * Get header of buffer b, assuming b is embedded into an bvarith64_buffer_elem
 */
static inline dl_list_t *bvarith64_buffer_header(bvarith64_buffer_t *b) {
  return (dl_list_t *)(((char *)b) - offsetof(bvarith64_buffer_elem_t, buffer));
}

/*
 * Get buffer of header l
 */
static inline bvarith64_buffer_t *bvarith64_buffer(dl_list_t *l) {
  return (bvarith64_buffer_t *)(((char *) l) + offsetof(bvarith64_buffer_elem_t, buffer));
}

/*
 * Allocate a bv-arithmetic buffer and insert it into the list
 */
static inline bvarith64_buffer_t *alloc_bvarith64_buffer(void) {
  bvarith64_buffer_elem_t *new_elem;

  new_elem = (bvarith64_buffer_elem_t *) safe_malloc(sizeof(bvarith64_buffer_elem_t));
  list_insert_next(&bvarith64_buffer_list, &new_elem->header);
  return &new_elem->buffer;
}

/*
 * Remove b from the list and free b
 */
static inline void free_bvarith64_buffer(bvarith64_buffer_t *b) {
  dl_list_t *elem;

  elem = bvarith64_buffer_header(b);
  list_remove(elem);
  safe_free(elem);
}

/*
 * Clean up the buffer list: free all elements and empty the list
 */
static void free_bvarith64_buffer_list(void) {
  dl_list_t *elem, *aux;

  elem = bvarith64_buffer_list.next;
  while (elem != &bvarith64_buffer_list) {
    aux = elem->next;
    delete_bvarith64_buffer(bvarith64_buffer(elem));
    safe_free(elem);
    elem = aux;
  }

  clear_list(&bvarith64_buffer_list);
}



/*****************************
 *  LOGIC BUFFER ALLOCATION  *
 ****************************/

/*
 * Get header of buffer b, assuming b is embedded into an bvlogic_buffer_elem
 */
static inline dl_list_t *bvlogic_buffer_header(bvlogic_buffer_t *b) {
  return (dl_list_t *)(((char *)b) - offsetof(bvlogic_buffer_elem_t, buffer));
}

/*
 * Get buffer of header l
 */
static inline bvlogic_buffer_t *bvlogic_buffer(dl_list_t *l) {
  return (bvlogic_buffer_t *)(((char *) l) + offsetof(bvlogic_buffer_elem_t, buffer));
}

/*
 * Allocate an arithmetic buffer and insert it into the list
 */
static inline bvlogic_buffer_t *alloc_bvlogic_buffer(void) {
  bvlogic_buffer_elem_t *new_elem;

  new_elem = (bvlogic_buffer_elem_t *) safe_malloc(sizeof(bvlogic_buffer_elem_t));
  list_insert_next(&bvlogic_buffer_list, &new_elem->header);
  return &new_elem->buffer;
}

/*
 * Remove b from the list and free b
 */
static inline void free_bvlogic_buffer(bvlogic_buffer_t *b) {
  dl_list_t *elem;

  elem = bvlogic_buffer_header(b);
  list_remove(elem);
  safe_free(elem);
}

/*
 * Clean up the arith buffer list: free all elements and empty the list
 */
static void free_bvlogic_buffer_list(void) {
  dl_list_t *elem, *aux;

  elem = bvlogic_buffer_list.next;
  while (elem != &bvlogic_buffer_list) {
    aux = elem->next;
    delete_bvlogic_buffer(bvlogic_buffer(elem));
    safe_free(elem);    
    elem = aux;
  }

  clear_list(&bvlogic_buffer_list);
}






/***********************
 *  INTERNAL BUFFERS   *
 **********************/

/*
 * Return the internal arithmetic buffer
 * - allocate it if needed
 */
static arith_buffer_t *get_internal_arith_buffer(void) {
  arith_buffer_t *b;

  b = internal_arith_buffer;
  if (b == NULL) {
    b = alloc_arith_buffer();
    init_arith_buffer(b, &pprods, &arith_store);
    internal_arith_buffer = b;
  }

  return b;
}


/*
 * Same thing: internal_bvarith buffer
 */
static bvarith_buffer_t *get_internal_bvarith_buffer(void) {
  bvarith_buffer_t *b;

  b = internal_bvarith_buffer;
  if (b == NULL) {
    b = alloc_bvarith_buffer();
    init_bvarith_buffer(b, &pprods, &bvarith_store);
    internal_bvarith_buffer = b;
  }

  return b;
}


/*
 * Same thing: internal_bvarith64 buffer
 */
static bvarith64_buffer_t *get_internal_bvarith64_buffer(void) {
  bvarith64_buffer_t *b;

  b = internal_bvarith64_buffer;
  if (b == NULL) {
    b = alloc_bvarith64_buffer();
    init_bvarith64_buffer(b, &pprods, &bvarith64_store);
    internal_bvarith64_buffer = b;
  }

  return b;
}


/*
 * Same thing: internal_bvlogic buffer
 */
static bvlogic_buffer_t *get_internal_bvlogic_buffer(void) {
  bvlogic_buffer_t *b;

  b = internal_bvlogic_buffer;
  if (b == NULL) {
    b = alloc_bvlogic_buffer();
    init_bvlogic_buffer(b, &nodes);
    internal_bvlogic_buffer = b;
  }

  return b;
}



/***********************************
 *  PARSER AND RELATED STRUCTURES  *
 **********************************/

/*
 * Return the internal parser 
 * - with the given string as input
 * - s must be non-NULL, terminated by '\0'
 */
static parser_t *get_parser(char *s) {
  if (parser == NULL) {
    assert(lexer == NULL && tstack == NULL);
    tstack = (tstack_t *) safe_malloc(sizeof(tstack_t));
    init_tstack(tstack);

    lexer = (lexer_t *) safe_malloc(sizeof(lexer_t));
    init_string_lexer(lexer, s, "yices");
    
    parser = (parser_t *) safe_malloc(sizeof(parser_t));
    init_parser(parser, lexer, tstack);
  } else {
    // reset the input string
    assert(lexer != NULL && tstack != NULL);
    reset_string_lexer(lexer, s);
  }

  return parser;
}



/*
 * Delete the internal parser, lexer, term stack
 * (it they exist)
 */
static void delete_parsing_objects(void) {
  if (parser != NULL) {
    assert(lexer != NULL && tstack != NULL);
    delete_parser(parser);
    safe_free(parser);
    parser = NULL;

    close_lexer(lexer);
    safe_free(lexer);
    lexer = NULL;

    delete_tstack(tstack);
    safe_free(tstack);
    tstack = NULL;
  }
}




/***************************************
 *  GLOBAL INITIALIZATION AND CLEANUP  *
 **************************************/

/*
 * Initialize the table of global objects
 */
static void init_globals(yices_globals_t *glob) {
  glob->types = &types;
  glob->terms = &terms;
  glob->pprods = &pprods;
  glob->nodes = &nodes;
  glob->arith_store = &arith_store;
  glob->bvarith_store = &bvarith_store;
  glob->bvarith64_store = &bvarith64_store;
  glob->error = &error;
}


/*
 * Reset all to NULL
 */
static void clear_globals(yices_globals_t *glob) {
  glob->types = NULL;
  glob->terms = NULL;
  glob->pprods = NULL;
  glob->nodes = NULL;
  glob->arith_store = NULL;
  glob->bvarith_store = NULL;
  glob->bvarith64_store = NULL;
  glob->error = NULL;
}


/*
 * Initialize all global objects
 */
EXPORTED void yices_init(void) {
  error.code = NO_ERROR;

  init_yices_pp_tables();

  init_bvconstants();
  init_bvconstant(&bv0);
  init_bvconstant(&bv1);
  init_bvconstant(&bv2);

  init_rationals();
  q_init(&r0);
  q_init(&r1);

  init_ivector(&vector0, 10);

  // tables
  init_type_table(&types, INIT_TYPE_SIZE);
  init_pprod_table(&pprods, 0);
  init_node_table(&nodes, 0);
  init_term_table(&terms, INIT_TERM_SIZE, &types, &pprods);

  // object stores
  init_mlist_store(&arith_store);
  init_bvmlist_store(&bvarith_store);
  init_bvmlist64_store(&bvarith64_store);

  // buffer lists
  clear_list(&arith_buffer_list);
  clear_list(&bvarith_buffer_list);
  clear_list(&bvarith64_buffer_list);
  clear_list(&bvlogic_buffer_list);

  // internal buffers (allocated on demand)
  internal_arith_buffer = NULL;
  internal_bvarith_buffer = NULL;
  internal_bvarith64_buffer = NULL;
  internal_bvlogic_buffer = NULL;

  // parser etc.
  parser = NULL;
  lexer = NULL;
  tstack = NULL;

  // prepare the global table
  init_globals(&__yices_globals);
}


/*
 * Cleanup: delete all tables and internal data structures
 */
EXPORTED void yices_cleanup(void) {
  clear_globals(&__yices_globals);

  // parser etc.
  delete_parsing_objects();

  // internal buffers will be freed via free_arith_buffer_list,
  // free_bvarith_buffer_list, and free_bvlogic_buffer_list
  internal_arith_buffer = NULL;
  internal_bvarith_buffer = NULL;
  internal_bvarith64_buffer = NULL;
  internal_bvlogic_buffer = NULL;

  free_bvlogic_buffer_list();
  free_bvarith_buffer_list();
  free_bvarith64_buffer_list();
  free_arith_buffer_list();

  delete_term_table(&terms);
  delete_node_table(&nodes);
  delete_pprod_table(&pprods);
  delete_type_table(&types);

  delete_mlist_store(&arith_store);
  delete_bvmlist_store(&bvarith_store);
  delete_bvmlist64_store(&bvarith64_store);

  delete_ivector(&vector0);

  q_clear(&r0); // not necessary
  q_clear(&r1);
  cleanup_rationals();

  delete_bvconstant(&bv2);
  delete_bvconstant(&bv1);
  delete_bvconstant(&bv0);
  cleanup_bvconstants();
}



/*
 * Get the last error report
 */
EXPORTED error_report_t *yices_error_report(void) {
  return &error;
}


/*
 * Get the last error code
 */
EXPORTED error_code_t yices_error_code(void) {
  return error.code;
}

/*
 * Clear the last error report
 */
EXPORTED void yices_clear_error(void) {
  error.code = NO_ERROR;
}




/***********************
 *  BUFFER ALLOCATION  *
 **********************/

/*
 * These functions are not part of the API.
 * They are exported to be used by other yices modules.
 */

/*
 * Allocate an arithmetic buffer, initialized to the zero polynomial.
 * Add it to the buffer list
 */
arith_buffer_t *yices_new_arith_buffer(void) {
  arith_buffer_t *b;
  
  b = alloc_arith_buffer();
  init_arith_buffer(b, &pprods, &arith_store);
  return b;
}


/*
 * Free an allocated buffer
 */
void yices_free_arith_buffer(arith_buffer_t *b) {
  delete_arith_buffer(b);
  free_arith_buffer(b);
}


/*
 * Allocate and initialize a bvarith_buffer
 * - the buffer is initialized to 0b0...0 (with n bits)
 * - n must be positive and no more than YICES_MAX_BVSIZE
 */
bvarith_buffer_t *yices_new_bvarith_buffer(uint32_t n) {
  bvarith_buffer_t *b;

  b = alloc_bvarith_buffer();
  init_bvarith_buffer(b, &pprods, &bvarith_store);
  bvarith_buffer_prepare(b, n);

  return b;
}


/*
 * Free an allocated bvarith_buffer
 */
void yices_free_bvarith_buffer(bvarith_buffer_t *b) {
  delete_bvarith_buffer(b);
  free_bvarith_buffer(b);
}


/*
 * Allocate and initialize a bvarith64_buffer
 * - the buffer is initialized to 0b0000..0 (with n bits)
 * - n must be between 1 and 64
 */
bvarith64_buffer_t *yices_new_bvarith64_buffer(uint32_t n) {
  bvarith64_buffer_t *b;

  b = alloc_bvarith64_buffer();
  init_bvarith64_buffer(b, &pprods, &bvarith64_store);
  bvarith64_buffer_prepare(b, n);

  return b;
}


/*
 * Free an allocated bvarith64_buffer
 */
void yices_free_bvarith64_buffer(bvarith64_buffer_t *b) {
  delete_bvarith64_buffer(b);
  free_bvarith64_buffer(b);
}


/*
 * Allocate and initialize a bvlogic buffer
 * - the buffer is empty (bitsize = 0)
 */
bvlogic_buffer_t *yices_new_bvlogic_buffer(void) {
  bvlogic_buffer_t *b;

  b = alloc_bvlogic_buffer();
  init_bvlogic_buffer(b, &nodes);

  return b;
}


/*
 * Free buffer b allocated by the previous function
 */
void yices_free_bvlogic_buffer(bvlogic_buffer_t *b) {
  bvlogic_buffer_clear(b);
  delete_bvlogic_buffer(b);
  free_bvlogic_buffer(b);
}



/***********************************************
 *  CONVERSION OF ARITHMETIC BUFFERS TO TERMS  *
 **********************************************/

/*
 * These functions are not part of the API.
 * They are exported to be used by other yices modules.
 */

/*
 * Convert b to a term and reset b.
 *
 * Normalize b first then apply the following simplification rules:
 * 1) if b is a constant, then a constant rational is created
 * 2) if b is of the form 1.t then t is returned
 * 3) if b is of the form 1.t_1^d_1 x ... x t_n^d_n, then a power product is returned
 * 4) otherwise, a polynomial term is returned
 */
term_t arith_buffer_get_term(arith_buffer_t *b) {
  mlist_t *m;
  pprod_t *r;
  uint32_t n;
  term_t t;

  assert(b->ptbl == &pprods);

  arith_buffer_normalize(b);

  n = b->nterms;
  if (n == 0) {
    t = zero_term;
  } else if (n == 1) {
    m = b->list; // unique monomial of b
    r = m->prod;
    if (r == empty_pp) {
      // constant polynomial
      t = arith_constant(&terms, &m->coeff);
    } else if (q_is_one(&m->coeff)) {
      // term or power product
      t =  pp_is_var(r) ? var_of_pp(r) : pprod_term(&terms, r);
    } else {
    // can't simplify
      t = arith_poly(&terms, b);
    }
  } else {
    t = arith_poly(&terms, b);
  }

  arith_buffer_reset(b);
  assert(good_term(&terms, t) && is_arithmetic_term(&terms, t));

  return t;
}


/*
 * Auxiliary function: built binary equality (t1 == t2)
 * for two arithmetic terms t1 and t2.
 * - normalize first
 */
static term_t mk_arith_bineq_atom(term_t t1, term_t t2) {
  term_t aux;

  assert(is_arithmetic_term(&terms, t1) && is_arithmetic_term(&terms, t2));

  // normalize: put the smallest term on the left
  if (t1 > t2) {
    aux = t1; t1 = t2; t2 = aux;
  }

  return arith_bineq_atom(&terms, t1, t2);  
}

/*
 * Construct the atom (b == 0) then reset b.
 *
 * Normalize b first.
 * - simplify to true if b is the zero polynomial
 * - simplify to false if b is constant and non-zero
 * - rewrite to (t1 == t2) if that's possible.
 * - otherwise, create a polynomial term t from b
 *   and return the atom (t == 0).
 */
term_t arith_buffer_get_eq0_atom(arith_buffer_t *b) {
  mlist_t *m1, *m2;
  pprod_t *r1, *r2;
  term_t t1, t2, t;
  uint32_t n;

  assert(b->ptbl == &pprods);

  arith_buffer_normalize(b);

  n = b->nterms;
  if (n == 0) {
    // b is zero
    t = true_term; 

  } else if (n == 1) {
    /*
     * b is a1 * r1 with a_1 != 0
     * (a1 * r1 == 0) is false if r1 is the empty product
     * (a1 * r1 == 0) simplifies to (r1 == 0) otherwisw
     */
    m1 = b->list; 
    r1 = m1->prod;  
    assert(q_is_nonzero(&m1->coeff));
    if (r1 == empty_pp) {
      t = false_term;
    } else {
      t1 = pp_is_var(r1) ? var_of_pp(r1) : pprod_term(&terms, r1);
      t = mk_arith_bineq_atom(zero_term, t1); // atom r1 = 0
    }

  } else if (n == 2) {
    /*
     * b is a1 * r1 + a2 * r2
     * Simplifications:
     * - rewrite (b == 0) to (r2 == -a2/a1) if r1 is the empty product
     * - rewrite (b == 0) to (r1 == r2) is a1 + a2 = 0
     */
    m1 = b->list;
    r1 = m1->prod;
    m2 = m1->next;
    r2 = m2->prod;
    assert(q_is_nonzero(&m1->coeff) && q_is_nonzero(&m2->coeff));

    if (r1 == empty_pp) {
      q_set_neg(&r0, &m2->coeff);
      q_div(&r0, &m1->coeff);  // r0 is -a2/a1
      t1 = arith_constant(&terms, &r0);
      t2 = pp_is_var(r2) ? var_of_pp(r2) : pprod_term(&terms, r2);
      t = mk_arith_bineq_atom(t1, t2);

    } else {
      q_set(&r0, &m1->coeff);
      q_add(&r0, &m2->coeff);
      if (q_is_zero(&r0)) {
	t1 = pp_is_var(r1) ? var_of_pp(r1) : pprod_term(&terms, r1);
	t2 = pp_is_var(r2) ? var_of_pp(r2) : pprod_term(&terms, r2);
	t = mk_arith_bineq_atom(t1, t2);

      } else {
	// no simplification
	t = arith_poly(&terms, b);
	t = arith_eq_atom(&terms, t);
      }
    }

  } else {
    /*
     * more than 2 monomials: don't simplify
     */
    t = arith_poly(&terms, b);
    t = arith_eq_atom(&terms, t);    
  }


  arith_buffer_reset(b);
  assert(good_term(&terms, t) && is_boolean_term(&terms, t));

  return t;
}


/*
 * Construct the atom (b >= 0) then reset b.
 *
 * Normalize b first then check for simplifications.
 * - simplify to true or false if b is a constant
 * - otherwise term t from b and return the atom (t >= 0)
 */
term_t arith_buffer_get_geq0_atom(arith_buffer_t *b) {
  mlist_t *m;
  pprod_t *r;
  term_t t;
  uint32_t n;

  assert(b->ptbl == &pprods);

  arith_buffer_normalize(b);

  n = b->nterms;
  if (n == 0) {
    // b is zero
    t = true_term;
  } else if (n == 1) {
    /*
     * b is a * r with a != 0
     * if r is the empty product, (b >= 0) simplifies to true or false
     * otherwise, (b >= 0) simplfies either to r >= 0 or -r >= 0
     */
    m = b->list;
    r = m->prod;
    if (q_is_pos(&m->coeff)) {
      // a > 0
      if (r == empty_pp) {
	t = true_term;
      } else {
	t = pp_is_var(r) ? var_of_pp(r) : pprod_term(&terms, r);
	t = arith_geq_atom(&terms, t); // r >= 0
      }
    } else {
      // a < 0
      if (r == empty_pp) {
	t = false_term;
      } else {
	q_set_minus_one(&m->coeff); // force a := -1
	t = arith_poly(&terms, b);
	t = arith_geq_atom(&terms, t);
      }
    }

  } else {
    // no simplification (for now).
    // could try to reduce the coefficients?
    t = arith_poly(&terms, b);
    t = arith_geq_atom(&terms, t);    
  }

  arith_buffer_reset(b);
  assert(good_term(&terms, t) && is_boolean_term(&terms, t));

  return t;
}


/*
 * Atom (b <= 0): rewritten to (-b >= 0)
 */
term_t arith_buffer_get_leq0_atom(arith_buffer_t *b) {
  assert(b->ptbl == &pprods);

  arith_buffer_negate(b);
  return arith_buffer_get_geq0_atom(b);
}


/*
 * Atom (b > 0): rewritten to (not (b <= 0))
 */
term_t arith_buffer_get_gt0_atom(arith_buffer_t *b) {
  term_t t;

  t = arith_buffer_get_leq0_atom(b);
#ifndef NDEBUG
  return not_term(&terms, t);
#else 
  return opposite_term(t);
#endif
}


/*
 * Atom (b < 0): rewritten to (not (b >= 0))
 */
term_t arith_buffer_get_lt0_atom(arith_buffer_t *b) {
  term_t t;

  t = arith_buffer_get_geq0_atom(b);
#ifndef NDEBUG
  return not_term(&terms, t);
#else 
  return opposite_term(t);
#endif
}




/********************************************
 *  CONVERSION OF BVLOGIC BUFFERS TO TERMS  *
 *******************************************/

/*
 * These functions are not part of the API.
 * They are exported to be used by other yices modules.
 */

/*
 * Convert buffer b to a bv_constant term
 * - side effect: use bv0
 */
static term_t bvlogic_buffer_get_bvconst(bvlogic_buffer_t *b) {
  assert(bvlogic_buffer_is_constant(b));

  bvlogic_buffer_get_constant(b, &bv0);
  return bvconst_term(&terms, bv0.bitsize, bv0.data);
}


/*
 * Convert buffer b to a bv-array term
 * - side effect: use vector0
 */
static term_t bvlogic_buffer_get_bvarray(bvlogic_buffer_t *b) {
  uint32_t i, n;

  assert(b->nodes == &nodes);

  // translate each bit of b into a boolean term
  // we store the translation in b->bit
  n = b->bitsize;
  for (i=0; i<n; i++) {
    b->bit[i] = convert_bit_to_term(&terms, &nodes, b->bit[i]);
  }

  // build the term (bvarray b->bit[0] ... b->bit[n-1])
  return bvarray_term(&terms, n, b->bit);
}


/*
 * Convert b to a term then reset b.
 * - b must not be empty.
 * - build a bitvector constant if possible
 * - if b is of the form (select 0 t) ... (select k t) and t has bitsize (k+1)
 *   then return t
 * - otherwise build a bitarray term
 */
term_t bvlogic_buffer_get_term(bvlogic_buffer_t *b) {
  term_t t;
  uint32_t n;

  n = b->bitsize;
  assert(n > 0);
  if (bvlogic_buffer_is_constant(b)) {
    if (n <= 64) {
      // small constant
      t = bv64_constant(&terms, n, bvlogic_buffer_get_constant64(b));
    } else {
      // wide constant
      t = bvlogic_buffer_get_bvconst(b);
    }

  } else {
    t = bvlogic_buffer_get_var(b);
    if (t < 0 || term_bitsize(&terms, t) != n) {
      // not a variable
      t = bvlogic_buffer_get_bvarray(b);
    }
  }

  assert(is_bitvector_term(&terms, t) && term_bitsize(&terms, t) == n);

  bvlogic_buffer_clear(b);
  
  return t;
}




/********************************************
 *  CONVERSION OF BVARITH BUFFERS TO TERMS  *
 *******************************************/

/*
 * Store array [false_term, ..., false_term] into vector v
 */
static void bvarray_set_zero_bv(ivector_t *v, uint32_t n) {
  uint32_t i;

  assert(0 < n && n <= YICES_MAX_BVSIZE);
  resize_ivector(v, n);
  for (i=0; i<n; i++) {
    v->data[i] = false_term;
  }
}

/*
 * Store constant c into vector v
 */
static void bvarray_copy_constant(ivector_t *v, uint32_t n, uint32_t *c) {
  uint32_t i;

  assert(0 < n && n <= YICES_MAX_BVSIZE);
  resize_ivector(v, n);
  for (i=0; i<n; i++) {
    v->data[i] = bool2term(bvconst_tst_bit(c, i));
  }
}

/*
 * Same thing for a small constant c
 */
static void bvarray_copy_constant64(ivector_t *v, uint32_t n, uint64_t c) {
  uint32_t i;

  assert(0 < n && n <= 64);
  resize_ivector(v, n);
  for (i=0; i<n; i++) {
    v->data[i] = bool2term(tst_bit64(c, i));
  }
}


/*
 * Check whether v + a * x can be converted to (v | (x << k))  for some k
 * - a must be an array of n boolean terms
 * - return true if that can be done and update v to (v | (x << k))
 * - otherwise, return false and keep v unchanged
 */
static bool bvarray_check_addmul(ivector_t *v, uint32_t n, uint32_t *c, term_t *a) {
  uint32_t i, w;
  int32_t k;

  w = (n + 31) >> 5; // number of words in c
  if (bvconst_is_zero(c, w)) {
    return true;
  }

  k = bvconst_is_power_of_two(c, w);
  if (k < 0) {
    return false;
  }

  // c is 2^k check whether v + (a << k) is equal to v | (a << k)
  assert(0 <= k && k < n);
  for (i=k; i<n; i++) {
    if (v->data[i] != false_term && a[i-k] != false_term) {
      return false;
    }
  }

  // update v here
  for (i=k; i<n; i++) {
    if (a[i-k] != false_term) {
      assert(v->data[i] == false_term);
      v->data[i] = a[i-k];
    }
  }

  return true;
}


/*
 * Same thing for c stored as a small constant (64 bits at most)
 */
static bool bvarray_check_addmul64(ivector_t *v, uint32_t n, uint64_t c, term_t *a) {
  uint32_t i, k;

  assert(0 < n && n <= 64 && c == norm64(c, n));

  if (c == 0) {
    return true;
  }

  k = ctz64(c); // k = index of the rightmost 1 in c
  assert(0 <= k && k <= 63);
  if (c != (((uint64_t) 1) << k)) {
    // c != 2^k
    return false;
  }

  // c is 2^k check whether v + (a << k) is equal to v | (a << k)
  assert(0 <= k && k < n);
  for (i=k; i<n; i++) {
    if (v->data[i] != false_term && a[i-k] != false_term) {
      return false;
    }
  }

  // update v here
  for (i=k; i<n; i++) {
    if (a[i-k] != false_term) {
      assert(v->data[i] == false_term);
      v->data[i] = a[i-k];
    }
  }

  return true;
}




/*
 * Check whether power product r is equal to a bit-array term t
 * - if so return t's descriptor, otherwise return NULL
 */
static composite_term_t *pprod_get_bvarray(pprod_t *r) {  
  composite_term_t *bv;
  term_t t;

  bv = NULL;
  if (pp_is_var(r)) {
    t = var_of_pp(r);
    if (term_kind(&terms, t) == BV_ARRAY) {
      bv = composite_for_idx(&terms, index_of(t));
    }
  }

  return bv;
}

/*
 * Attempt to convert a bvarith buffer to a bv-array term
 * - b = bvarith buffer (list of monomials)
 * - return NULL_TERM if the conversion fails
 * - return a term t if the conversion succeeds.
 * - side effect: use vector0
 */
static term_t convert_bvarith_to_bvarray(bvarith_buffer_t *b) {
  composite_term_t *bv;
  bvmlist_t *m;
  uint32_t n;

  n = b->bitsize;
  m = b->list; // first monomial
  if (m->prod == empty_pp) {
    // copy constant into vector0
    bvarray_copy_constant(&vector0, n, m->coeff);
    m = m->next;
  } else {
    // initialze vector0 to 0
    bvarray_set_zero_bv(&vector0, n);
  }

  while (m->next != NULL) {
    bv = pprod_get_bvarray(m->prod);
    if (bv == NULL) return NULL_TERM;

    assert(bv->arity == n);

    // try to convert coeff * v into shift + bitwise or 
    if (! bvarray_check_addmul(&vector0, n, m->coeff, bv->arg)) {
      return NULL_TERM;  // conversion failed
    }
    m = m->next;
  }

  // Success: construct a bit array from log_buffer
  return bvarray_term(&terms, n, vector0.data);
}


/*
 * Attempt to convert a bvarith64 buffer to a bv-array term
 * - b = bvarith buffer (list of monomials)
 * - return NULL_TERM if the conversion fails
 * - return a term t if the conversion succeeds.
 * - side effect: use vector0
 */
static term_t convert_bvarith64_to_bvarray(bvarith64_buffer_t *b) {
  composite_term_t *bv;
  bvmlist64_t *m;
  uint32_t n;

  n = b->bitsize;
  m = b->list; // first monomial
  if (m->prod == empty_pp) {
    // copy constant into vector0
    bvarray_copy_constant64(&vector0, n, m->coeff);
    m = m->next;
  } else {
    // initialze vector0 to 0
    bvarray_set_zero_bv(&vector0, n);
  }

  while (m->next != NULL) {
    bv = pprod_get_bvarray(m->prod);
    if (bv == NULL) return NULL_TERM;

    assert(bv->arity == n);

    // try to convert coeff * v into shift + bitwise or 
    if (! bvarray_check_addmul64(&vector0, n, m->coeff, bv->arg)) {
      return NULL_TERM;  // conversion failed
    }
    m = m->next;
  }

  // Success: construct a bit array from log_buffer
  return bvarray_term(&terms, n, vector0.data);
}


/*
 * Constant bitvector with all bits 0
 * - n = bitsize (must satisfy 0 < n && n <= YICES_MAX_BVSIZE)
 * - side effect: modify bv0
 */
static term_t make_zero_bv(uint32_t n) {
  assert(0 < n && n <= YICES_MAX_BVSIZE);
  if (n > 64) {
    bvconstant_set_all_zero(&bv0, n);
    return bvconst_term(&terms, bv0.bitsize, bv0.data);
  } else {
    return bv64_constant(&terms, n, 0);
  }
}


/*
 * These functions are not part of the API.
 * They are exported to be used by other yices modules.
 */

/*
 * Normalize b then convert it to a term and reset b
 *
 * if b is reduced to a single variable x, return the term attached to x
 * if b is reduced to a power product, return that
 * if b is constant, build a BV_CONSTANT term
 * if b can be converted to a BV_ARRAY term do it
 * otherwise construct a BV_POLY
 */
term_t bvarith_buffer_get_term(bvarith_buffer_t *b) {
  bvmlist_t *m;
  pprod_t *r;
  uint32_t n, p, k;
  term_t t;

  assert(b->bitsize > 0);
  
  bvarith_buffer_normalize(b);

  n = b->bitsize;
  k = (n + 31) >> 5;
  p = b->nterms;
  if (p == 0) {
    // zero 
    t = make_zero_bv(n);
    goto done;
  }

  if (p == 1) {
    m = b->list; // unique monomial of b
    r = m->prod;
    if (r == empty_pp) {
      // constant 
      t = bvconst_term(&terms, n, m->coeff);
      goto done;
    }
    if (bvconst_is_one(m->coeff, k)) {
      // power product
      t = pp_is_var(r) ? var_of_pp(r) : pprod_term(&terms, r);
      goto done;
    } 
  }

  // try to convert to a bvarray term
  t = convert_bvarith_to_bvarray(b);
  if (t == NULL_TERM) {
    // conversion failed: build a bvpoly
    t = bv_poly(&terms, b);
  }

 done:
  bvarith_buffer_prepare(b, 32); // reset b, any positive n would do
  assert(is_bitvector_term(&terms, t) && term_bitsize(&terms, t) == n);

  return t;  
}



/*
 * Normalize b then convert it to a term and reset b
 *
 * if b is reduced to a single variable x, return the term attached to x
 * if b is reduced to a power product, return that
 * if b is constant, build a BV64_CONSTANT term
 * if b can be converted to a BV_ARRAY term do it
 * otherwise construct a BV64_POLY
 */
term_t bvarith64_buffer_get_term(bvarith64_buffer_t *b) {
  bvmlist64_t *m;
  pprod_t *r;
  uint32_t n, p;
  term_t t;

  assert(b->bitsize > 0);
  
  bvarith64_buffer_normalize(b);

  n = b->bitsize;
  p = b->nterms;
  if (p == 0) {
    // zero 
    t = make_zero_bv(n);
    goto done;
  }

  if (p == 1) {
    m = b->list; // unique monomial of b
    r = m->prod;
    if (r == empty_pp) {
      // constant 
      t = bv64_constant(&terms, n, m->coeff);
      goto done;
    }
    if (m->coeff == 1) {
      // power product
      t = pp_is_var(r) ? var_of_pp(r) : pprod_term(&terms, r);
      goto done;
    }
  }

  // try to convert to a bvarray term
  t = convert_bvarith64_to_bvarray(b);
  if (t == NULL_TERM) {
    // conversion failed: build a bvpoly
    t = bv64_poly(&terms, b);
  }

 done:
  bvarith64_buffer_prepare(b, 32); // reset b, any positive n would do
  assert(is_bitvector_term(&terms, t) && term_bitsize(&terms, t) == n);

  return t;  
}



/*
 * CONVERT BIT-VECTOR AND RATIONAL CONSTANTS TO TERMS
 */

/*
 * Convert a 64bit constant to a term
 * - n = bitsize (must be positive and no more than 64)
 * - c = constant value (must be normalized modulo 2^n)
 */
term_t yices_bvconst64_term(uint32_t n, uint64_t c) {
  assert(1 <= n && n <= 64 && c == norm64(c, n));
  return bv64_constant(&terms, n, c);
}


/*
 * Convert a bitvector constant to a term
 * - n = bitsize (must be more than 64 and at most YICES_MAX_BVSIZE)
 * - v = value as an array of k words (k = ceil(n/32)).
 * - v must be normalized modulo 2^n
 */
term_t yices_bvconst_term(uint32_t n, uint32_t *v) {
  assert(64 < n && n <= YICES_MAX_BVSIZE);
  return bvconst_term(&terms, n, v);
}


/*
 * Convert rational q to a term
 */
term_t yices_rational_term(rational_t *q) {
  return arith_constant(&terms, q);
}




/*******************************
 *  BOOLEAN-TERM CONSTRUCTORS  *
 ******************************/

/*
 * BINARY OR/AND/IMPLIES
 */

/*
 * Simplifications:
 *   x or x       --> x
 *   x or true    --> true
 *   x or false   --> x
 *   x or (not x) --> true
 *
 * Normalization: put smaller index first
 */
static term_t mk_binary_or(term_table_t *tbl, term_t x, term_t y) {
  term_t aux[2];
  
  if (x == y) return x;
  if (x == true_term) return x;
  if (y == true_term) return y;
  if (x == false_term) return y;
  if (y == false_term) return x;
  if (opposite_bool_terms(x, y)) return true_term;

  if (x < y) {
    aux[0] = x; aux[1] = y;
  } else {
    aux[0] = y; aux[1] = x;
  }

  return or_term(tbl, 2, aux);
}


/*
 * Rewrite (and x y)  to  (not (or (not x) (not y)))
 */
static term_t mk_binary_and(term_table_t *tbl, term_t x, term_t y) {
  return opposite_term(mk_binary_or(tbl, opposite_term(x), opposite_term(y)));
}


/*
 * Rewrite (implies x y) to (or (not x) y)
 */
static term_t mk_implies(term_table_t *tbl, term_t x, term_t y) {
  return mk_binary_or(tbl, opposite_term(x), y);
}



/*
 * IFF AND BINARY XOR
 */

/*
 * Check whether x is uninterpreted or the negation of an uninterpreted boolean term
 */
static inline bool is_literal(term_table_t *tbl, term_t x) {
  return kind_for_idx(tbl, index_of(x)) == UNINTERPRETED_TERM;
}


/*
 * Simplifications:
 *    iff x x       --> true
 *    iff x true    --> x
 *    iff x false   --> not x
 *    iff x (not x) --> false
 * 
 *    iff (not x) (not y) --> eq x y 
 *
 * Optional simplification:
 *    iff (not x) y       --> not (eq x y)  (NOT USED ANYMORE)
 *
 * Smaller index is on the left-hand-side of eq
 */
static term_t mk_iff(term_table_t *tbl, term_t x, term_t y) {
  term_t aux;

  if (x == y) return true_term;
  if (x == true_term) return y;
  if (y == true_term) return x;
  if (x == false_term) return opposite_term(y);
  if (y == false_term) return opposite_term(x);
  if (opposite_bool_terms(x, y)) return false_term;


  /*
   * swap if x > y
   */
  if (x > y) {
    aux = x; x = y; y = aux;
  }

  /*
   * - rewrite (iff (not x) (not y)) to (eq x y)
   * - rewrite (iff (not x) y)       to (eq x (not y)) 
   *   unless y is uninterpreted and x is not
   */
  if (is_neg_term(x) && (is_neg_term(y) || is_literal(tbl, x) || !is_literal(tbl, y))) {
    x = opposite_term(x);
    y = opposite_term(y); 
  }

  return eq_term(tbl, x, y);
}


/*
 * Rewrite (xor x y) to (iff (not x) y)
 */
static term_t mk_binary_xor(term_table_t *tbl, term_t x, term_t y) {
  return mk_iff(tbl, opposite_term(x), y);
}




/*
 * N-ARY OR/AND
 */

/*
 * Construct (or a[0] ... a[n-1])
 * - all terms are assumed valid and boolean
 * - array a is modified (sorted)
 * - n must be positive
 */
static term_t mk_or(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i, j;
  term_t x, y;

  /*
   * Sorting the terms ensure:
   * - true_term shows up first if it's present in a
   *   then false_term if it's present
   *   then all the other boolean terms.
   * - if x and (not x) are both in a, then they occur
   *   at successive positions in a after sorting.
   */
  assert(n > 0);
  int_array_sort(a, n);

  x = a[0];
  if (x == true_term) {
    return true_term;
  }

  j = 0;
  if (x != false_term) {
    a[j] = x;
    j ++;
  }

  // remove duplicates and check for x/not x in succession
  for (i=1; i<n; i++) {
    y = a[i];
    if (x != y) {
      if (y == opposite_term(x)) {
	return true_term;
      }
      assert(y != false_term && y != true_term);
      x = y;
      a[j] = x;
      j ++;
    }
  }

  if (j <= 1) { 
    // if j = 0, then x = false_term and all elements of a are false
    // if j = 1, then x is the unique non-false term in a
    return x;
  } else {
    return or_term(tbl, j, a);
  }
}


/*
 * Construct (and a[0] ... a[n-1])
 * - n must be positive
 * - a is modified
 */
static term_t mk_and(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i;
  
  for (i=0; i<n; i++) {
    a[i] = opposite_term(a[i]);
  }

  return opposite_term(mk_or(tbl,n, a));
}





/*
 * N-ARY XOR
 */

/*
 * Construct (xor a[0] ... a[n-1])
 * - n must be positive
 * - all terms in a must be valid and boolean
 * - a is modified
 */
static term_t mk_xor(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i, j;
  term_t x, y;
  bool negate;


  /*
   * First pass: remove true_term/false_term and 
   * replace negative terms by their opposite
   */
  negate = false;
  j = 0;
  for (i=0; i<n; i++) {
    x = a[i];
    if (index_of(x) == bool_const) {
      assert(x == true_term || x == false_term);
      negate ^= is_pos_term(x); // flip sign if x is true
    } else {
      assert(x != true_term && x != false_term);
      // apply rule (xor ... (not x) ...) = (not (xor ... x ...))
      negate ^= is_neg_term(x);    // flip sign for (not x) 
      x = unsigned_term(x);   // turn (not x) into x
      a[j] = x;
      j ++;
    }
  }

  /*
   * Second pass: remove duplicates (i.e., apply the rule (xor x x) --> false
   */
  n = j;
  int_array_sort(a, n);
  j = 0;
  i = 0;
  while (i+1 < n) {
    x = a[i];
    if (x == a[i+1]) {
      i += 2;
    } else {
      a[j] = x;
      j ++;
      i ++;
    }
  }
  assert(i == n || i + 1 == n);
  if (i+1 == n) {
    a[j] = a[i];
    j ++;
  }


  /*
   * Build the result: (xor negate (xor a[0] ... a[j-1]))
   */
  if (j == 0) {
    return bool2term(negate);
  }

  if (j == 1) {
    return negate ^ a[0];
  }

  if (j == 2) {
    x = a[0];
    y = a[1];
    assert(is_pos_term(x) && is_pos_term(y) && x < y);
    if (negate) {
      /*
       * to be consistent with mk_iff:
       * not (xor x y) --> (eq (not x) y) if y is uninterpreted and x is not
       * not (xor x y) --> (eq x (not y)) otherwise
       */
      if (is_literal(tbl, y) && !is_literal(tbl, x)) {
	x = opposite_term(x); 
      } else {
	y = opposite_term(y);
      }
    } 
    return eq_term(tbl, x, y);
  }

  // general case: j >= 3
  x = xor_term(tbl, j, a);
  if (negate) {
    x = opposite_term(x);
  }

  return x;
}




/******************
 *  IF-THEN-ELSE  *
 *****************/

/*
 * BOOLEAN IF-THEN-ELSE
 */

/*
 * Build (bv-eq x (ite c y z))
 * - c not true/false
 */
static term_t mk_bveq_ite(term_table_t *tbl, term_t c, term_t x, term_t y, term_t z) {
  term_t ite, aux;

  assert(term_type(tbl, x) == term_type(tbl, y) && term_type(tbl, x) == term_type(tbl, z));

  ite = ite_term(tbl, term_type(tbl, y), c, y, z);

  // normalize (bveq x ite): put smaller index on the left
  if (x > ite) {
    aux = x; x = ite; ite = aux;
  }

  return bveq_atom(tbl, x, ite);
}


/*
 * Special constructor for (ite c (bveq x y) (bveq z u))
 * 
 * Apply lift-if rule:
 * (ite c (bveq x y) (bveq x u))  ---> (bveq x (ite c y u))
 */
static term_t mk_lifted_ite_bveq(term_table_t *tbl, term_t c, term_t t, term_t e) {
  composite_term_t *eq1, *eq2;
  term_t x;

  assert(is_pos_term(t) && is_pos_term(e) && 
	 term_kind(tbl, t) == BV_EQ_ATOM && term_kind(tbl, e) == BV_EQ_ATOM);

  eq1 = composite_for_idx(tbl, index_of(t));
  eq2 = composite_for_idx(tbl, index_of(e));

  assert(eq1->arity == 2 && eq2->arity == 2);

  x = eq1->arg[0];
  if (x == eq2->arg[0]) return mk_bveq_ite(tbl, c, x, eq1->arg[1], eq2->arg[1]);
  if (x == eq2->arg[1]) return mk_bveq_ite(tbl, c, x, eq1->arg[1], eq2->arg[0]);

  x = eq1->arg[1];
  if (x == eq2->arg[0]) return mk_bveq_ite(tbl, c, x, eq1->arg[0], eq2->arg[1]);
  if (x == eq2->arg[1]) return mk_bveq_ite(tbl, c, x, eq1->arg[0], eq2->arg[0]);

  return ite_term(tbl, bool_type(tbl->types), c, t, e);
}


/*
 * Simplifications:
 *  ite c x x        --> x
 *  ite true x y     --> x
 *  ite false x y    --> y
 *
 *  ite c x (not x)  --> (c == x)
 *
 *  ite c c y        --> c or y
 *  ite c x c        --> c and x
 *  ite c (not c) y  --> (not c) and y
 *  ite c x (not c)  --> (not c) or x
 *
 *  ite c true y     --> c or y
 *  ite c x false    --> c and x
 *  ite c false y    --> (not c) and y
 *  ite c x true     --> (not c) or x
 *
 * Otherwise:
 *  ite (not c) x y  --> ite c y x
 */
static term_t mk_bool_ite(term_table_t *tbl, term_t c, term_t x, term_t y) {
  term_t aux;

  if (x == y) return x;
  if (c == true_term) return x;
  if (c == false_term) return y;

  if (opposite_bool_terms(x, y)) return mk_iff(tbl, c, x);
  
  if (c == x) return mk_binary_or(tbl, c, y);
  if (c == y) return mk_binary_and(tbl, c, x);
  if (opposite_bool_terms(c, x)) return mk_binary_and(tbl, x, y);
  if (opposite_bool_terms(c, y)) return mk_binary_or(tbl, x, y);

  if (x == true_term) return mk_binary_or(tbl, c, y);
  if (y == false_term) return mk_binary_and(tbl, c, x);
  if (x == false_term) return mk_binary_and(tbl, opposite_term(c), y);
  if (y == true_term) return mk_binary_or(tbl, opposite_term(c), x);


  if (is_neg_term(c)) {
    c = opposite_term(c);
    aux = x; x = y; y = aux;
  }

  if (term_kind(tbl, x) == BV_EQ_ATOM && term_kind(tbl, y) == BV_EQ_ATOM) {
    return mk_lifted_ite_bveq(tbl, c, x, y);
  }

  return ite_term(tbl, bool_type(tbl->types), c, x, y);
}




/*
 * BIT-VECTOR IF-THEN-ELSE
 */

/*
 * Build (ite c x y) when both x and y are boolean constants.
 */
static term_t const_ite_simplify(term_t c, term_t x, term_t y) {
  assert(x == true_term || x == false_term);
  assert(y == true_term || y == false_term);

  if (x == y) return x;
  if (x == true_term) {
    assert(y == false_term);
    return c;
  } 

  assert(x == false_term && y == true_term);
  return opposite_term(c);
}


/*
 * Convert (ite c u v) into a bvarray term:
 * - c is a boolean
 * - u and v are two bv64 constants
 *
 * Side effect: use vector0 as a buffer
 */
static term_t mk_bvconst64_ite(term_table_t *tbl, term_t c, bvconst64_term_t *u, bvconst64_term_t *v) {
  uint32_t i, n;  
  term_t bu, bv;
  term_t *a;

  n = u->bitsize;
  assert(v->bitsize == n);
  resize_ivector(&vector0, n);
  a = vector0.data;

  for (i=0; i<n; i++) {
    bu = bool2term(tst_bit64(u->value, i)); // bit i of u
    bv = bool2term(tst_bit64(v->value, i)); // bit i of v

    a[i] = const_ite_simplify(c, bu, bv); // a[i] = (ite c bu bv)
  }

  return bvarray_term(tbl, n, a);
}


/*
 * Same thing with u and v two generic bv constants
 */
static term_t mk_bvconst_ite(term_table_t *tbl, term_t c, bvconst_term_t *u, bvconst_term_t *v) {
  uint32_t i, n;
  term_t bu, bv;
  term_t *a;

  n = u->bitsize;
  assert(v->bitsize == n);
  resize_ivector(&vector0, n);
  a = vector0.data;

  for (i=0; i<n; i++) {
    bu = bool2term(bvconst_tst_bit(u->data, i));
    bv = bool2term(bvconst_tst_bit(v->data, i));

    a[i] = const_ite_simplify(c, bu, bv);
  }

  return bvarray_term(tbl, n, a);
}



/*
 * Given three boolean terms c, x, and y, check whether (ite c x y)
 * simplifies and if so return the result.
 * - return NULL_TERM if no simplification is found.
 * - the function assumes c is not a boolean constant
 */
static term_t check_ite_simplifies(term_t c, term_t x, term_t y) {
  assert(c != true_term && c != false_term);

  // (ite c x y) --> (ite c true y)  if c == x
  // (ite c x y) --> (ite c false y) if c == not x
  if (c == x) {
    x = true_term;
  } else if (opposite_bool_terms(c, x)) {
    x = false_term;
  }

  // (ite c x y) --> (ite c x false) if c == y
  // (ite c x y) --> (ite c x true)  if c == not y
  if (c == y) {
    y = false_term;
  } else if (opposite_bool_terms(c, y)) {
    y = true_term;
  }

  // (ite c x x) --> x
  // (ite c true false) --> c
  // (ite c false true) --> not c
  if (x == y) return x;
  if (x == true_term && y == false_term) return c;
  if (x == false_term && y == true_term) return opposite_term(c);

  return NULL_TERM;
}


/*
 * Attempt to convert (ite c u v) into a bvarray term:
 * - u is a bitvector constant of no more than 64 bits
 * - v is a bvarray term
 * Return NULL_TERM if the simplifications fail.
 */
static term_t check_ite_bvconst64(term_table_t *tbl, term_t c, bvconst64_term_t *u, composite_term_t *v) {
  uint32_t i, n;
  term_t b;
  term_t *a;

  n = u->bitsize;
  assert(n == v->arity);
  resize_ivector(&vector0, n);
  a = vector0.data;

  for (i=0; i<n; i++) {
    b = bool2term(tst_bit64(u->value, i)); // bit i of u
    b = check_ite_simplifies(c, b, v->arg[i]);

    if (b == NULL_TERM) {
      return NULL_TERM;
    }
    a[i] = b;
  }

  return bvarray_term(tbl, n, a);
}


/*
 * Same thing for a generic constant u
 */
static term_t check_ite_bvconst(term_table_t *tbl, term_t c, bvconst_term_t *u, composite_term_t *v) {
  uint32_t i, n;
  term_t b;
  term_t *a;

  n = u->bitsize;
  assert(n == v->arity);
  resize_ivector(&vector0, n);
  a = vector0.data;

  for (i=0; i<n; i++) {
    b = bool2term(bvconst_tst_bit(u->data, i)); // bit i of u
    b = check_ite_simplifies(c, b, v->arg[i]);

    if (b == NULL_TERM) {
      return NULL_TERM;
    }
    a[i] = b;
  }

  return bvarray_term(tbl, n, a);
}


/*
 * Same thing when both u and v are bvarray terms.
 */
static term_t check_ite_bvarray(term_table_t *tbl, term_t c, composite_term_t *u, composite_term_t *v) {
  uint32_t i, n;
  term_t b;
  term_t *a;

  n = u->arity;
  assert(n == v->arity);
  resize_ivector(&vector0, n);
  a = vector0.data;

  for (i=0; i<n; i++) {
    b = check_ite_simplifies(c, u->arg[i], v->arg[i]);

    if (b == NULL_TERM) {
      return NULL_TERM;
    }
    a[i] = b;
  }

  return bvarray_term(tbl, n, a);
}




/*
 * Build (ite c x y) c is boolean, x and y are bitvector terms
 * Use vector0 as a buffer.
 */
static term_t mk_bv_ite(term_table_t *tbl, term_t c, term_t x, term_t y) {    
  term_kind_t kind_x, kind_y;
  term_t aux;

  assert(term_type(tbl, x) == term_type(tbl, y) && is_bitvector_term(tbl, x) && 
	 is_boolean_term(tbl, c));

  // Try generic simplification first
  if (x == y) return x;
  if (c == true_term) return x;
  if (c == false_term) return y;


  // Check whether (ite c x y) simplifies to a bv_array term
  kind_x = term_kind(tbl, x);
  kind_y = term_kind(tbl, y);
  aux = NULL_TERM;
  switch (kind_x) {
  case BV64_CONSTANT:
    assert(kind_y != BV_CONSTANT);
    if (kind_y == BV64_CONSTANT) {
      return mk_bvconst64_ite(tbl, c, bvconst64_term_desc(tbl, x), bvconst64_term_desc(tbl, y));
    }
    if (kind_y == BV_ARRAY) {
      aux = check_ite_bvconst64(tbl, c, bvconst64_term_desc(tbl, x), bvarray_term_desc(tbl, y));
    }
    break;

  case BV_CONSTANT:
    assert(kind_y != BV64_CONSTANT);
    if (kind_y == BV_CONSTANT) {
      return mk_bvconst_ite(tbl, c, bvconst_term_desc(tbl, x), bvconst_term_desc(tbl, y));
    }
    if (kind_y == BV_ARRAY) {
      aux = check_ite_bvconst(tbl, c, bvconst_term_desc(tbl, x), bvarray_term_desc(tbl, y));
    }
    break;

  case BV_ARRAY:
    if (kind_y == BV64_CONSTANT) {
      aux = check_ite_bvconst64(tbl, c, bvconst64_term_desc(tbl, y), bvarray_term_desc(tbl, x));
    } else if (kind_y == BV_CONSTANT) {
      aux = check_ite_bvconst(tbl, c, bvconst_term_desc(tbl, y), bvarray_term_desc(tbl, x));      
    } else if (kind_y == BV_ARRAY) {
      aux = check_ite_bvarray(tbl, c, bvarray_term_desc(tbl, y), bvarray_term_desc(tbl, x));
    }
    break;

  default:
    break;
  }

  if (aux != NULL_TERM) {
    return aux;
  }


  /*
   * No simplification found: build a standard ite.
   * Normalize first: (ite (not c) x y) --> (ite c y x)
   */
  if (is_neg_term(c)) {
    c = opposite_term(c);
    aux = x; x = y; y = aux;
  }

  return ite_term(tbl, term_type(tbl, x), c, x, y);
}


/*
 * LIFT IF
 */

/*
 * Cheap lift-if decomposition:
 * - decompose (ite c x y) (ite c z u) ---> [c, x, z, y, u]
 * - decompose (ite c x y) z           ---> [c, x, z, y, z]
 * - decompose x (ite c y z)           ---> [c, x, y, x, z]
 *
 * The result is stored into the lift_result_t object:
 * - for example: [c, x, z, y, u] is stored as
 *    cond = c,  left1 = x, left2 = z,  right1 = y, right2 = u
 * - the function return true if the decomposition succeeds, false otherwise
 */
typedef struct lift_result_s {
  term_t cond;
  term_t left1, left2;
  term_t right1, right2;
} lift_result_t;


static bool check_for_lift_if(term_table_t *tbl, term_t t1, term_t t2, lift_result_t *d) {
  composite_term_t *ite1, *ite2;
  term_t cond;

  if (term_kind(tbl, t1) == ITE_TERM) {
    if (term_kind(tbl, t2) == ITE_TERM) {
      // both are (if-then-else ..) 
      ite1 = ite_term_desc(tbl, t1);
      ite2 = ite_term_desc(tbl, t2);
      
      cond = ite1->arg[0];
      if (cond == ite2->arg[0]) {
	d->cond = cond;
	d->left1 = ite1->arg[1];
	d->left2 = ite2->arg[1];
	d->right1 = ite1->arg[2];
	d->right2 = ite2->arg[2];
	return true;
      } 

    } else {
      // t1 is (if-then-else ..) t2 is not
      ite1 = ite_term_desc(tbl, t1);
      d->cond = ite1->arg[0];
      d->left1 = ite1->arg[1];
      d->left2 = t2;
      d->right1 = ite1->arg[2];
      d->right2 = t2;
      return true;
      
    }
  } else if (term_kind(tbl, t2) == ITE_TERM) {
    // t2 is (if-then-else ..) t1 is not

    ite2 = ite_term_desc(tbl, t2);
    d->cond = ite2->arg[0];
    d->left1 = t1;
    d->left2 = ite2->arg[1];
    d->right1 = t1;
    d->right2 = ite2->arg[2];
    return true;
  }
 
 return false;  
}




/*
 * Attempt to factor out the common factors in t and e.
 *
 * If t and e are polynomials with integer variables
 * then we can write t as (a * t') and e as (a * e')
 * where a = gcd of coefficients of t and e.
 * Then (ite c t e) can be rewritten to a * (ite c t' e')
 *
 * This function attempt to rewrite (ite c t e) to a * (ite c t' e')
 * if the common factor is 1, it just construct (ite c t e).
 */
static term_t mk_integer_polynomial_ite(term_table_t *tbl, term_t c, term_t t, term_t e) {
  polynomial_t *p, *q;
  arith_buffer_t *b;

  assert(is_integer_term(tbl, t) && is_integer_term(tbl, e));

  p = poly_term_desc(tbl, t);  // then part
  q = poly_term_desc(tbl, e);  // else part

  if (!polynomial_is_zero(p) && !polynomial_is_zero(q)) {
    monarray_common_factor(p->mono, &r0); // r0 = gcd of coefficients of p
    monarray_common_factor(q->mono, &r1); // r1 = gcd of coefficients of q
    q_gcd(&r0, &r1);  // r0 = common factor a above

    assert(q_is_pos(&r0) && q_is_integer(&r0));
    if (! q_is_one(&r0)) {
      // use internal arith buffer for operations
      b = get_internal_arith_buffer();

      // construct p' := 1/r0 * p
      arith_buffer_reset(b);
      arith_buffer_add_monarray(b, p->mono, pprods_for_poly(tbl, p));
      term_table_reset_pbuffer(tbl);
      arith_buffer_div_const(b, &r0);
      t = arith_poly(tbl, b);

      // construct q' := 1/r0 * q
      arith_buffer_reset(b);
      arith_buffer_add_monarray(b, q->mono, pprods_for_poly(tbl, q));
      term_table_reset_pbuffer(tbl);
      arith_buffer_div_const(b, &r0);
      e = arith_poly(tbl, b);

      // (ite c p' q')
      t = ite_term(tbl, int_type(tbl->types), c, t, e);

      // built r0 * t
      arith_buffer_reset(b);
      arith_buffer_add_varmono(b, &r0, t);
      return arith_poly(tbl, b);
    }
  }
  
  // no common factor to lift
  return ite_term(tbl, int_type(tbl->types), c, t, e);
}




/**********************
 *  ARITHMETIC ATOMS  *
 *********************/

/*
 * Store t1 - t2 in internal_arith_buffer.
 */
static void mk_arith_diff(term_t t1, term_t t2) {
  arith_buffer_t *b;
  
  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t1);
  arith_buffer_sub_term(b, &terms, t2);
}


/*
 * Build the term (ite c (aritheq t1 t2) (aritheq t3 t4))
 * - c is a boolean term
 * - t1, t2, t3, t4 are all arithmetic terms
 */
static term_t mk_lifted_aritheq(term_t c, term_t t1, term_t t2, term_t t3, term_t t4) {
  term_t left, right;

  mk_arith_diff(t1, t2);
  left = arith_buffer_get_eq0_atom(internal_arith_buffer);
  mk_arith_diff(t3, t4);
  right = arith_buffer_get_eq0_atom(internal_arith_buffer);

  return mk_bool_ite(&terms, c, left, right);
}

/*
 * Build the term (ite c (arithge t1 t2) (arithge t3 t4))
 * - c is a boolean term
 * - t1, t2, t3, t4 are all arithmetic terms
 */
static term_t mk_lifted_arithgeq(term_t c, term_t t1, term_t t2, term_t t3, term_t t4) {
  term_t left, right;

  mk_arith_diff(t1, t2);
  left = arith_buffer_get_geq0_atom(internal_arith_buffer);
  mk_arith_diff(t3, t4);
  right = arith_buffer_get_geq0_atom(internal_arith_buffer);

  return mk_bool_ite(&terms, c, left, right);
}



/*
 * Equality term (aritheq t1 t2)
 *
 * Apply the cheap lift-if rules
 *  (eq x (ite c y z))  ---> (ite c (eq x y) (eq x z)) provided x is not an if term
 *  (eq (ite c x y) z)) ---> (ite c (eq x z) (eq y z)) provided z is not an if term
 *  (eq (ite c x y) (ite c z u)) --> (ite c (eq x z) (eq y u))
 *
 */
static term_t mk_aritheq(term_t t1, term_t t2) {
  lift_result_t tmp;

  assert(is_arithmetic_term(&terms, t1) && is_arithmetic_term(&terms, t2));

  if (check_for_lift_if(&terms, t1, t2, &tmp)) {
    return mk_lifted_aritheq(tmp.cond, tmp.left1, tmp.left2, tmp.right1, tmp.right2);
  } 

  mk_arith_diff(t1, t2);
  return arith_buffer_get_eq0_atom(internal_arith_buffer);
}


/*
 * Inequality: (arithgewq t1 t2)
 *
 * Try the cheap lift-if rules. 
 */
static term_t mk_arithgeq(term_t t1, term_t t2) {
  lift_result_t tmp;

  assert(is_arithmetic_term(&terms, t1) && is_arithmetic_term(&terms, t2));

  if (check_for_lift_if(&terms, t1, t2, &tmp)) {
    return mk_lifted_arithgeq(tmp.cond, tmp.left1, tmp.left2, tmp.right1, tmp.right2);
  } 

  mk_arith_diff(t1, t2);
  return arith_buffer_get_geq0_atom(internal_arith_buffer);  
}


static inline term_t mk_arithneq(term_t t1, term_t t2) {
  return opposite_term(mk_aritheq(t1, t2));
}




/*************************
 *  BIT-VECTOR EQUALITY  *
 ************************/

/*
 * Check whether (eq b c) simplifies and if so returns the result.
 * - b and c must be boolean terms (assumed not opposite of each other).
 * - return NULL_TERM if no simplification is found
 *
 * Rules:
 *   (eq b b)     --> true
 *   (eq b true)  --> b
 *   (eq b false) --> (not b)
 * + symmetric cases for the last two rules
 */
static term_t check_biteq_simplifies(term_t b, term_t c) {
  assert(! opposite_bool_terms(b, c));

  if (b == c) return true_term;

  if (b == true_term)  return c;
  if (b == false_term) return opposite_term(c); // not c
  if (c == true_term)  return b;
  if (c == false_term) return opposite_term(b);

  return NULL_TERM;
}


/*
 * Check whether (and a (eq b c)) simplifies and, if so, returns the result.
 * - a, b, and c are three boolean terms.
 * - return NULL_TERM if no cheap simplification is found
 *
 * We assume that the cheaper simplification tests have been tried before:
 * (i.e., we assume a != false and  b != (not c)).
 */
static term_t check_accu_biteq_simplifies(term_t a, term_t b, term_t c) {
  term_t eq;


  // first check whether (eq b c) simplifies
  eq = check_biteq_simplifies(b, c);
  if (eq == NULL_TERM) return NULL_TERM;

  /*
   * try to simplify (and a eq)
   */
  assert(a != false_term && eq != false_term);

  if (a == eq) return a;
  if (opposite_bool_terms(a, eq)) return false_term;

  if (a == true_term) return eq;
  if (eq == true_term) return a;

  return NULL_TERM;
}



/*
 * Check whether (bveq u v) simplifies:
 * - u is a bitvector constant of no more than 64 bits
 * - v is a bv_array term
 *
 * Return NULL_TERM if no cheap simplification is found.
 */
static term_t check_eq_bvconst64(bvconst64_term_t *u, composite_term_t *v) {
  uint32_t i, n;
  term_t accu, b;

  n = u->bitsize;
  assert(n == v->arity);
  accu = true_term;

  for (i=0; i<n; i++) {
    b = bool2term(tst_bit64(u->value, i)); // bit i of u
    accu = check_accu_biteq_simplifies(accu, b, v->arg[i]);
    if (accu == NULL_TERM || accu == false_term) {
      break;
    }
  }

  return accu;
}


/*
 * Same thing for a generic constant u.
 */
static term_t check_eq_bvconst(bvconst_term_t *u, composite_term_t *v) {
  uint32_t i, n;
  term_t accu, b;

  n = u->bitsize;
  assert(n == v->arity);
  accu = true_term;

  for (i=0; i<n; i++) {
    b = bool2term(bvconst_tst_bit(u->data, i)); // bit i of u
    accu = check_accu_biteq_simplifies(accu, b, v->arg[i]);
    if (accu == NULL_TERM || accu == false_term) {
      break;
    }
  }

  return accu;
}


/*
 * Same thing for two bv_array terms
 */
static term_t check_eq_bvarray(composite_term_t *u, composite_term_t *v) {
  uint32_t i, n;
  term_t accu;

  n = u->arity;
  assert(n == v->arity);
  accu = true_term;

  for (i=0; i<n; i++) {
    accu = check_accu_biteq_simplifies(accu, u->arg[i], v->arg[i]);
    if (accu == NULL_TERM || accu == false_term) {
      break;
    }
  }

  return accu;
}



/*
 * Build (bveq t1 t2)
 * - try to simplify to true or false
 * - attempt to simplify the equality if it's between bit-arrays or bit-arrays and constant
 * - build an atom if no simplification works
 */
static term_t mk_bveq(term_t t1, term_t t2) {
  term_kind_t k1, k2;
  term_t aux;

  if (t1 == t2) return true_term;
  if (disequal_bitvector_terms(&terms, t1, t2)) {
    return false_term;
  }

  /*
   * Try simplifications.  We know that t1 and t2 are not both constant
   * (because disequal_bitvector_terms returned false).
   */
  k1 = term_kind(&terms, t1);
  k2 = term_kind(&terms, t2);
  aux = NULL_TERM;
  switch (k1) {
  case BV64_CONSTANT:
    if (k2 == BV_ARRAY) {
      aux = check_eq_bvconst64(bvconst64_term_desc(&terms, t1), bvarray_term_desc(&terms, t2));
    }
    break;

  case BV_CONSTANT:
    if (k2 == BV_ARRAY) {
      aux = check_eq_bvconst(bvconst_term_desc(&terms, t1), bvarray_term_desc(&terms, t2));
    }
    break;

  case BV_ARRAY:
    if (k2 == BV64_CONSTANT) {
      aux = check_eq_bvconst64(bvconst64_term_desc(&terms, t2), bvarray_term_desc(&terms, t1));
    } else if (k2 == BV_CONSTANT) {
      aux = check_eq_bvconst(bvconst_term_desc(&terms, t2), bvarray_term_desc(&terms, t1));
    } else if (k2 == BV_ARRAY) {
      aux = check_eq_bvarray(bvarray_term_desc(&terms, t1), bvarray_term_desc(&terms, t2));
    }
    break;

  default:
    break;
  }

  if (aux != NULL_TERM) {
    // Simplification worked
    return aux;
  }


  /*
   * Default: normalize then build a bveq_atom
   */
  if (t1 > t2) {
    aux = t1; t1 = t2; t2 = aux;
  }

  return bveq_atom(&terms, t1, t2);
}


static inline term_t mk_bvneq(term_t t1, term_t t2) {
  return opposite_term(mk_bveq(t1, t2));
}




/******************
 *  TYPECHECKING  *
 *****************/

/*
 * All check_ functions return true if the check succeeds.
 * Otherwise they return false and set the error code and diagnostic data.
 */

// Check whether n is positive
static bool check_positive(uint32_t n) {
  if (n == 0) {
    error.code = POS_INT_REQUIRED;
    error.badval = n;
    return false;
  }
  return true;
}

// Check whether n is less than YICES_MAX_ARITY
static bool check_arity(uint32_t n) {
  if (n > YICES_MAX_ARITY) {
    error.code = TOO_MANY_ARGUMENTS;
    error.badval = n;
    return false;
  }
  return true;
}

// Check whether n is less than YICES_MAX_VARS
static bool check_maxvars(uint32_t n) {
  if (n > YICES_MAX_VARS) {
    error.code = TOO_MANY_VARS;
    error.badval = n;
    return false;
  }
  return true;
}

// Check whether n is less than YICES_MAX_BVSIZE
static bool check_maxbvsize(uint32_t n) {
  if (n > YICES_MAX_BVSIZE) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = n;
    return false;
  }
  return true;
}

// Check whether d is no more than YICES_MAX_DEGREE
static bool check_maxdegree(uint32_t d) {
  if (d > YICES_MAX_DEGREE) {
    error.code = DEGREE_OVERFLOW;
    error.badval = d;
    return false;
  }
  return true;
}

// Check whether i is a valid variable index (i.e., whether i >= 0)
static bool check_good_var_index(int32_t i) {
  if (i < 0) {
    error.code = INVALID_VAR_INDEX;
    error.badval = i;
    return false;
  }

  return true;
}

// Check whether tau is a valid type
static bool check_good_type(type_table_t *tbl, type_t tau) {
  if (bad_type(tbl, tau)) { 
    error.code = INVALID_TYPE;
    error.type1 = tau;
    return false;
  }
  return true;
}

// Check whether all types in a[0 ... n-1] are valid
static bool check_good_types(type_table_t *tbl, uint32_t n, type_t *a) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (bad_type(tbl, a[i])) {
      error.code = INVALID_TYPE;
      error.type1 = a[i];
      return false;
    }
  }
  return true;
}

// Check whether tau is uninterpreted or scalar and whether 
// i a valid constant index for type tau.
static bool check_good_constant(type_table_t *tbl, type_t tau, int32_t i) {
  type_kind_t kind;

  if (bad_type(tbl, tau)) {
    error.code = INVALID_TYPE;
    error.type1 = tau;
    return false;
  }
  kind = type_kind(tbl, tau);
  if (kind != UNINTERPRETED_TYPE && kind != SCALAR_TYPE) {
    error.code = SCALAR_OR_UTYPE_REQUIRED;
    error.type1 = tau;
    return false;
  }
  if (i < 0 || 
      (kind == SCALAR_TYPE && i >= scalar_type_cardinal(tbl, tau))) {
    error.code = INVALID_CONSTANT_INDEX;
    error.type1 = tau;
    error.badval = i;
    return false;
  }
  return true;
}

// Check whether t is a valid term
static bool check_good_term(term_table_t *tbl, term_t t) {
  if (bad_term(tbl, t)) {
    error.code = INVALID_TERM;
    error.term1 = t;
    return false;
  }
  return true;
}

// Check that terms in a[0 ... n-1] are valid
static bool check_good_terms(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (bad_term(tbl, a[i])) {
      error.code = INVALID_TERM;
      error.term1 = a[i];
      return false;
    }
  }
  return true;
}

// check that terms a[0 ... n-1] have types that match tau[0 ... n-1].
static bool check_arg_types(term_table_t *tbl, uint32_t n, term_t *a, type_t *tau) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (! is_subtype(tbl->types, term_type(tbl, a[i]), tau[i])) {
      error.code = TYPE_MISMATCH;
      error.term1 = a[i];
      error.type1 = tau[i];
      return false;
    }
  }

  return true;
}

// check whether (f a[0] ... a[n-1]) is typecorrect
static bool check_good_application(term_table_t *tbl, term_t f, uint32_t n, term_t *a) {
  function_type_t *ft;

  if (! check_positive(n) || 
      ! check_good_term(tbl, f) || 
      ! check_good_terms(tbl, n, a)) {
    return false;
  }

  if (! is_function_term(tbl, f)) {
    error.code = FUNCTION_REQUIRED;
    error.term1 = f;
    return false;
  }  

  ft = function_type_desc(tbl->types, term_type(tbl, f));
  if (n != ft->ndom) {
    error.code = WRONG_NUMBER_OF_ARGUMENTS;
    error.type1 = term_type(tbl, f);
    error.badval = n;
    return false;
  }

  return check_arg_types(tbl, n, a, ft->domain);
}

// Check whether t is a boolean term. t must be a valid term
static bool check_boolean_term(term_table_t *tbl, term_t t) {
  if (! is_boolean_term(tbl, t)) {
    error.code = TYPE_MISMATCH;
    error.term1 = t;
    error.type1 = bool_type(tbl->types);
    return false;
  }
  return true;
}

// Check whether t is an arithmetic term, t must be valid.
static bool check_arith_term(term_table_t *tbl, term_t t) {
  if (! is_arithmetic_term(tbl, t)) {
    error.code = ARITHTERM_REQUIRED;
    error.term1 = t;
    return false;
  }
  return true;
}

// Check whether t is a bitvector term, t must be valid
static bool check_bitvector_term(term_table_t *tbl, term_t t) {
  if (! is_bitvector_term(tbl, t)) {
    error.code = BITVECTOR_REQUIRED;
    error.term1 = t;
    return false;
  }
  return true;
}

// Check whether t1 and t2 have compatible types (i.e., (= t1 t2) is well-typed)
// t1 and t2 must both be valid
static bool check_compatible_terms(term_table_t *tbl, term_t t1, term_t t2) {
  type_t tau1, tau2;

  tau1 = term_type(tbl, t1);
  tau2 = term_type(tbl, t2);
  if (! compatible_types(tbl->types, tau1, tau2)) {
    error.code = INCOMPATIBLE_TYPES;
    error.term1 = t1;
    error.type1 = tau1;
    error.term2 = t2;
    error.type2 = tau2;
    return false;
  }

  return true;
}

// Check whether (= t1 t2) is type correct
static bool check_good_eq(term_table_t *tbl, term_t t1, term_t t2) {
  return check_good_term(tbl, t1) && check_good_term(tbl, t2) &&
    check_compatible_terms(tbl, t1, t2);
}

// Check whether t1 and t2 are two valid arithmetic terms
static bool check_both_arith_terms(term_table_t *tbl, term_t t1, term_t t2) {
  return check_good_term(tbl, t1) && check_good_term(tbl, t2) &&
    check_arith_term(tbl, t1) && check_arith_term(tbl, t2);
}

// Check that t1 and t2 are bitvectors of the same size
static bool check_compatible_bv_terms(term_table_t *tbl, term_t t1, term_t t2) {
  return check_good_term(tbl, t1) && check_good_term(tbl, t2)
    && check_bitvector_term(tbl, t1) && check_bitvector_term(tbl, t2) 
    && check_compatible_terms(tbl, t1, t2);
}

// Check whether terms a[0 ... n-1] are all boolean
static bool check_boolean_args(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (! is_boolean_term(tbl, a[i])) {
      error.code = TYPE_MISMATCH;
      error.term1 = a[i];
      error.type1 = bool_type(tbl->types);
      return false;
    }
  }

  return true;
}

// Check whether terms a[0 ... n-1] are all arithmetic terms
static bool check_arithmetic_args(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (! is_arithmetic_term(tbl, a[i])) {
      error.code = ARITHTERM_REQUIRED;
      error.term1 = a[i];
      return false;
    }
  }

  return true;
}


// Check wether all numbers den[0 ... n-1] are positive
static bool check_denominators32(uint32_t n, uint32_t *den) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (den[i] == 0) {
      error.code = DIVISION_BY_ZERO;
      return false;
    }
  }

  return true;
}


static bool check_denominators64(uint32_t n, uint64_t *den) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (den[i] == 0) {
      error.code = DIVISION_BY_ZERO;
      return false;
    }
  }

  return true;
}


// Check whether (tuple-select i t) is well-typed
static bool check_good_select(term_table_t *tbl, uint32_t i, term_t t) {
  type_t tau;

  if (! check_good_term(tbl, t)) {
    return false;
  }

  tau = term_type(tbl, t);
  if (type_kind(tbl->types, tau) != TUPLE_TYPE) {
    error.code = TUPLE_REQUIRED;
    error.term1 = t;
    return false;
  }

  if (i >= tuple_type_arity(tbl->types, tau)) {
    error.code = INVALID_TUPLE_INDEX;
    error.type1 = tau;
    error.badval = i;
    return false;
  }
  
  return true;
}

// Check that (update f (a_1 ... a_n) v) is well typed
static bool check_good_update(term_table_t *tbl, term_t f, uint32_t n, term_t *a, term_t v) {
  function_type_t *ft;

  if (! check_positive(n) || 
      ! check_good_term(tbl, f) ||
      ! check_good_term(tbl, v) ||
      ! check_good_terms(tbl, n, a)) {
    return false;
  }

  if (! is_function_term(tbl, f)) {
    error.code = FUNCTION_REQUIRED;
    error.term1 = f;
    return false;
  }  

  ft = function_type_desc(tbl->types, term_type(tbl, f));
  if (n != ft->ndom) {
    error.code = WRONG_NUMBER_OF_ARGUMENTS;
    error.type1 = term_type(tbl, f);
    error.badval = n;
    return false;
  }

  if (! is_subtype(tbl->types, term_type(tbl, v), ft->range)) {
    error.code = TYPE_MISMATCH;
    error.term1 = v;
    error.type1 = ft->range;
    return false;
  }

  return check_arg_types(tbl, n, a, ft->domain);
}

// Check (distinct t_1 ... t_n)
static bool check_good_distinct_term(term_table_t *tbl, uint32_t n, term_t *a) {
  uint32_t i;
  type_t tau;

  if (! check_positive(n) ||
      ! check_arity(n) ||
      ! check_good_terms(tbl, n, a)) {
    return false;
  }

  tau = term_type(tbl, a[0]); 
  for (i=1; i<n; i++) {
    tau = super_type(tbl->types, tau, term_type(tbl, a[i]));
    if (tau == NULL_TYPE) {
      error.code = INCOMPATIBLE_TYPES;
      error.term1 = a[0];
      error.type1 = term_type(tbl, a[0]);
      error.term2 = a[i];
      error.type2 = term_type(tbl, a[i]);
      return false;
    }
  }

  return true;
}

// Check quantified formula (FORALL/EXISTS (v_1 ... v_n) body)
// v must be sorted.
static bool check_good_quantified_term(term_table_t *tbl, uint32_t n, term_t *v, term_t body) {
  int32_t i;

  if (! check_positive(n) ||
      ! check_maxvars(n) ||
      ! check_good_term(tbl, body) ||
      ! check_good_terms(tbl, n, v) ||
      ! check_boolean_term(tbl, body)) {
    return false;
  }

  for (i=0; i<n; i++) {
    if (term_kind(tbl, v[i]) != VARIABLE) {      
      error.code = VARIABLE_REQUIRED;
      error.term1 = v[i];
      return false;
    }
  }

  for (i=1; i<n; i++) {
    if (v[i-1] == v[i]) {
      error.code = DUPLICATE_VARIABLE;
      error.term1 = v[i];
      return false;
    }
  }

  return true;
}

// Check whether (tuple-select i t v) is well-typed
static bool check_good_tuple_update(term_table_t *tbl, uint32_t i, term_t t, term_t v) {
  type_t tau;
  tuple_type_t *desc;

  if (! check_good_term(tbl, t) ||
      ! check_good_term(tbl, v)) {
    return false;
  }

  tau = term_type(tbl, t);
  if (type_kind(tbl->types, tau) != TUPLE_TYPE) {
    error.code = TUPLE_REQUIRED;
    error.term1 = t;
    return false;
  }

  desc = tuple_type_desc(tbl->types, tau);
  if (i >= desc->nelem) {
    error.code = INVALID_TUPLE_INDEX;
    error.type1 = tau;
    error.badval = i;
    return false;
  }

  if (! is_subtype(tbl->types, term_type(tbl, v), desc->elem[i])) {
    error.code = TYPE_MISMATCH;
    error.term1 = v;
    error.type1 = desc->elem[i];
    return false;
  }
  
  return true;
}

// Check that the degree of t1 * t2 is at most MAX_DEGREE
static bool check_product_degree(term_table_t *tbl, term_t t1, term_t t2) {
  uint32_t d1, d2;

  d1 = term_degree(tbl, t1);
  d2 = term_degree(tbl, t2);
  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);
}


// Check that the degree of t^2 does not overflow
static bool check_square_degree(term_table_t *tbl, term_t t) {
  uint32_t d;

  d = term_degree(tbl, t);
  assert(d <= YICES_MAX_DEGREE);

  return check_maxdegree(d + d);
}


// Check whether i is a valid shift for bitvectors of size n
static bool check_bitshift(uint32_t i, uint32_t n) {
  if (i > n) {
    error.code = INVALID_BITSHIFT;
    error.badval = i;
    return false;
  }

  return true;
}

// Check whether [i, j] is a valid segment for bitvectors of size n
static bool check_bitextract(uint32_t i, uint32_t j, uint32_t n) {
  if (i > j || j >= n) {
    error.code = INVALID_BVEXTRACT;
    return false;
  }
  return true;
}



/***********************
 *  TYPE CONSTRUCTORS  *
 **********************/

EXPORTED type_t yices_bool_type(void) {
  return bool_type(&types);
}

EXPORTED type_t yices_int_type(void) {
  return int_type(&types);
}

EXPORTED type_t yices_real_type(void) {
  return real_type(&types);
}

EXPORTED type_t yices_bv_type(uint32_t size) {
  if (! check_positive(size) || ! check_maxbvsize(size)) {
    return NULL_TYPE;
  }
  return bv_type(&types, size);
}

EXPORTED type_t yices_new_uninterpreted_type(void) {
  return new_uninterpreted_type(&types);
}

EXPORTED type_t yices_new_scalar_type(uint32_t card) {
  if (! check_positive(card)) {
    return NULL_TYPE;
  }
  return new_scalar_type(&types, card);
}

EXPORTED type_t yices_tuple_type(uint32_t n, type_t elem[]) {
  if (! check_positive(n) || 
      ! check_arity(n) ||
      ! check_good_types(&types, n, elem)) {
    return NULL_TYPE;
  }
  return tuple_type(&types, n, elem);
}

EXPORTED type_t yices_function_type(uint32_t n, type_t dom[], type_t range) {
  if (! check_positive(n) || 
      ! check_arity(n) ||
      ! check_good_type(&types, range) || 
      ! check_good_types(&types, n, dom)) {
    return NULL_TYPE;
  }
  return function_type(&types, range, n, dom);
}






/***********************
 *  TERM CONSTRUCTORS  *
 **********************/

/*
 * When constructing a term to singleton type tau, we always
 * return the representative for tau (except for variables).
 */
EXPORTED term_t yices_true(void) {
  return true_term;
}

EXPORTED term_t yices_false(void) {
  return false_term;
}

EXPORTED term_t yices_constant(type_t tau, int32_t index) {
  term_t t;

  if (! check_good_constant(&types, tau, index)) {
    return NULL_TERM;
  }

  t = constant_term(&terms, tau, index);
  if (is_unit_type(&types, tau)) {
    store_unit_type_rep(&terms, tau, t);
  }

  return t;
}

EXPORTED term_t yices_new_uninterpreted_term(type_t tau) {
  if (! check_good_type(&types, tau)) {
    return NULL_TERM;
  }

  if (is_unit_type(&types, tau)) {
    return get_unit_type_rep(&terms, tau);
  }

  return new_uninterpreted_term(&terms, tau);
}

EXPORTED term_t yices_variable(type_t tau, int32_t index) {
  if (! check_good_var_index(index) || 
      ! check_good_type(&types, tau)) {
    return NULL_TERM;
  }
  return variable(&terms, tau, index);
}


/*
 * Auxiliary function: check whether terms a[0...n-1] and b[0 .. n-1] are equal
 */
static bool equal_term_arrays(uint32_t n, term_t *a, term_t *b) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

/*
 * Simplifications if fun is an update term:
 *   ((update f (a_1 ... a_n) v) a_1 ... a_n)   -->  v
 *   ((update f (a_1 ... a_n) v) x_1 ... x_n)   -->  (f x_1 ... x_n)
 *         if x_i must disequal a_i
 */
EXPORTED term_t yices_application(term_t fun, uint32_t n, term_t arg[]) {
  composite_term_t *update;
  type_t tau;

  if (! check_good_application(&terms, fun, n, arg)) {
    return NULL_TERM;
  }

  // singleton function type
  tau = term_type(&terms, fun);
  if (is_unit_type(&types, tau)) {
    return get_unit_type_rep(&terms, tau);
  }

  while (term_kind(&terms, fun) == UPDATE_TERM) {
    // fun is (update f (a_1 ... a_n) v)
    update = update_term_desc(&terms, fun);
    assert(update->arity == n+2);

    /*
     * update->arg[0] is f
     * update->arg[1] to update->arg[n] = a_1 to a_n
     * update->arg[n+1] is v
     */

    if (equal_term_arrays(n, update->arg + 1, arg)) {
      return update->arg[n+1];
    }
    
    if (disequal_term_arrays(&terms, n, update->arg + 1, arg)) {
      // ((update f (a_1 ... a_n) v) x_1 ... x_n) ---> (f x_1 ... x_n)
      // repeat simplification if f is an update term again
      fun = update->arg[0];
    } else {
      break;
    }
  }

  return app_term(&terms, fun, n, arg);
}


/*
 * Simplifications
 *    ite true x y   --> x
 *    ite false x y  --> y
 *    ite c x x      --> x
 *
 * Otherwise:
 *    ite (not c) x y --> ite c y x
 *
 * Plus special trick for integer polynomials:
 *    ite c (d * p1) (d * p2) --> d * (ite c p1 p2)
 */
EXPORTED term_t yices_ite(term_t cond, term_t then_term, term_t else_term) {
  term_t aux;
  type_t tau;

  // Check type correctness: first steps
  if (! check_good_term(&terms, cond) || 
      ! check_good_term(&terms, then_term) ||
      ! check_good_term(&terms, else_term) || 
      ! check_boolean_term(&terms, cond)) {
    return NULL_TERM;
  }

  // Check whether then/else are compatible and get the supertype
  tau = super_type(&types, term_type(&terms, then_term), term_type(&terms, else_term));

  if (tau == NULL_TYPE) {
    // type error
    error.code = INCOMPATIBLE_TYPES;
    error.term1 = then_term;
    error.type1 = term_type(&terms, then_term);
    error.term2 = else_term;
    error.type2 = term_type(&terms, else_term);
    return NULL_TERM;
  }

  // boolean ite
  if (is_boolean_term(&terms, then_term)) {
    assert(is_boolean_term(&terms, else_term));
    return mk_bool_ite(&terms, cond, then_term, else_term);
  }

  // bit-vector ite
  if (is_bitvector_term(&terms, then_term)) {
    assert(is_bitvector_term(&terms, else_term));
    return mk_bv_ite(&terms, cond, then_term, else_term);
  }

  // non-boolean:
  if (then_term == else_term) return then_term;
  if (cond == true_term) return then_term;
  if (cond == false_term) return else_term;

  if (is_neg_term(cond)) {
    // ite (not c) x y  --> ite c y x
    cond = opposite_term(cond);
    aux = then_term; then_term = else_term; else_term = aux;
  }

#if 1
  // DISABLE THIS FOR BASELINE TESTING
  // check whether both sides are integer polynomials
  if (is_integer_type(tau) 
      && term_kind(&terms, then_term) == ARITH_POLY 
      && term_kind(&terms, else_term) == ARITH_POLY) {
    return mk_integer_polynomial_ite(&terms, cond, then_term, else_term);
  }
#endif

  return ite_term(&terms, tau, cond, then_term, else_term);
}


/*
 * Equality: convert to boolean, arithmetic, or bitvector equality
 */
EXPORTED term_t yices_eq(term_t left, term_t right) {
  term_t aux;

  if (! check_good_eq(&terms, left, right)) {
    return NULL_TERM;
  }

  if (is_boolean_term(&terms, left)) {
    assert(is_boolean_term(&terms, right));
    return mk_iff(&terms, left, right);
  }

  if (is_arithmetic_term(&terms, left)) {
    assert(is_arithmetic_term(&terms, right));
    return mk_aritheq(left, right);
  }

  if (is_bitvector_term(&terms, left)) {
    assert(is_bitvector_term(&terms, right));
    return mk_bveq(left, right);
  }

  // general case
  if (left == right) return true_term;
  if (disequal_terms(&terms, left, right)) {
    return false_term;
  }

  // put smaller index on the left
  if (left > right) {
    aux = left; left = right; right = aux;
  }

  return eq_term(&terms, left, right);
}


/*
 * Disequality
 */
EXPORTED term_t yices_neq(term_t left, term_t right) {
  term_t aux;

  if (! check_good_eq(&terms, left, right)) {
    return NULL_TERM;
  }

  if (is_boolean_term(&terms, left)) {
    assert(is_boolean_term(&terms, right));
    return mk_binary_xor(&terms, left, right);
  }

  if (is_arithmetic_term(&terms, left)) {
    assert(is_arithmetic_term(&terms, right));
    return mk_arithneq(left, right);
  }

  if (is_bitvector_term(&terms, left)) {
    assert(is_bitvector_term(&terms, right));
    return mk_bvneq(left, right);
  }

  // non-boolean
  if (left == right) return false_term;
  if (disequal_terms(&terms, left, right)) {
    return true_term;
  }

  // put smaller index on the left
  if (left > right) {
    aux = left; left = right; right = aux;
  }

  return opposite_term(eq_term(&terms, left, right));
}


/*
 * BOOLEAN NEGATION
 */
EXPORTED term_t yices_not(term_t arg) {
  if (! check_good_term(&terms, arg) || 
      ! check_boolean_term(&terms, arg)) {
    return NULL_TERM;
  }

  return opposite_term(arg);
}



/*
 * OR, AND, and XOR may modify arg
 */
EXPORTED term_t yices_or(uint32_t n, term_t arg[]) {
  if (! check_arity(n) ||
      ! check_good_terms(&terms, n, arg) || 
      ! check_boolean_args(&terms, n, arg)) {
    return NULL_TERM;
  }

  switch (n) {
  case 0:
    return false_term;
  case 1:
    return arg[0];
  case 2:
    return mk_binary_or(&terms, arg[0], arg[1]);
  default:
    return mk_or(&terms, n, arg);
  }
}

EXPORTED term_t yices_and(uint32_t n, term_t arg[]) {
  if (! check_arity(n) ||
      ! check_good_terms(&terms, n, arg) || 
      ! check_boolean_args(&terms, n, arg)) {
    return NULL_TERM;
  }

  switch (n) {
  case 0:
    return true_term;
  case 1:
    return arg[0];
  case 2:
    return mk_binary_and(&terms, arg[0], arg[1]);
  default:
    return mk_and(&terms, n, arg);
  }
}

EXPORTED term_t yices_xor(uint32_t n, term_t arg[]) {
  if (! check_arity(n) ||
      ! check_good_terms(&terms, n, arg) || 
      ! check_boolean_args(&terms, n, arg)) {
    return NULL_TERM;
  }

  switch (n) {
  case 0:
    return false_term;
  case 1:
    return arg[0];
  case 2:
    return mk_binary_xor(&terms, arg[0], arg[1]);
  default:
    return mk_xor(&terms, n, arg);
  }
}


/*
 * BINARY VERSIONS OF OR/AND/XOR
 */
EXPORTED term_t yices_or2(term_t left, term_t right) {
  if (! check_good_term(&terms, left) ||
      ! check_good_term(&terms, right) || 
      ! check_boolean_term(&terms, left) || 
      ! check_boolean_term(&terms, right)) {
    return NULL_TERM;
  }

  return mk_binary_or(&terms, left, right);
}

EXPORTED term_t yices_and2(term_t left, term_t right) {
  if (! check_good_term(&terms, left) ||
      ! check_good_term(&terms, right) || 
      ! check_boolean_term(&terms, left) || 
      ! check_boolean_term(&terms, right)) {
    return NULL_TERM;
  }

  return mk_binary_and(&terms, left, right);
}

EXPORTED term_t yices_xor2(term_t left, term_t right) {
  if (! check_good_term(&terms, left) ||
      ! check_good_term(&terms, right) || 
      ! check_boolean_term(&terms, left) || 
      ! check_boolean_term(&terms, right)) {
    return NULL_TERM;
  }

  return mk_binary_xor(&terms, left, right);
}

EXPORTED term_t yices_iff(term_t left, term_t right) {
  if (! check_good_term(&terms, left) ||
      ! check_good_term(&terms, right) || 
      ! check_boolean_term(&terms, left) || 
      ! check_boolean_term(&terms, right)) {
    return NULL_TERM;
  }

  return mk_iff(&terms, left, right);
}

EXPORTED term_t yices_implies(term_t left, term_t right) {
  if (! check_good_term(&terms, left) ||
      ! check_good_term(&terms, right) || 
      ! check_boolean_term(&terms, left) || 
      ! check_boolean_term(&terms, right)) {
    return NULL_TERM;
  }

  return mk_implies(&terms, left, right);
}


/*
 * Simplification:
 *   (mk_tuple (select 0 x) ... (select n-1 x)) --> x
 */
EXPORTED term_t yices_tuple(uint32_t n, term_t arg[]) {
  uint32_t i;
  term_t x, a;
  type_t tau;

  if (! check_positive(n) || 
      ! check_arity(n) ||
      ! check_good_terms(&terms, n, arg)) {
    return NULL_TERM;
  }

  a = arg[0];
  if (term_kind(&terms, a) == SELECT_TERM && select_term_index(&terms, a) == 0) {
    x = select_term_arg(&terms, a);
    for (i = 1; i<n; i++) {
      a = arg[i];
      if (term_kind(&terms, a) != SELECT_TERM ||
	  select_term_index(&terms, a) != i ||
	  select_term_arg(&terms, a) != x) {
	return tuple_term(&terms, n, arg);
      }
    }
    return x;
  }

  // check whether x is unique element of its type
  x = tuple_term(&terms, n, arg);
  tau = term_type(&terms, x);
  if (is_unit_type(&types, tau)) {
    store_unit_type_rep(&terms, tau, x);
  }

  return x;
}


/*
 * Simplification: (select i (mk_tuple x_1 ... x_n))  --> x_i
 */
EXPORTED term_t yices_select(uint32_t index, term_t tuple) {
  if (! check_good_select(&terms, index, tuple)) {
    return NULL_TERM;
  }

  // simplify
  if (term_kind(&terms, tuple) == TUPLE_TERM) {
    return composite_term_arg(&terms, tuple, index);
  } else {
    return select_term(&terms, index, tuple);
  }
}



/*
 * Simplification: 
 *  (update (update f (a_1 ... a_n) v) (a_1 ... a_n) v') --> (update f (a_1 ... a_n) v')
 * TBD
 */
EXPORTED term_t yices_update(term_t fun, uint32_t n, term_t arg[], term_t new_v) {
  composite_term_t *update;
  type_t tau;

  if (! check_good_update(&terms, fun, n, arg, new_v)) {
    return NULL_TERM;
  }

  // singleton function type
  tau = term_type(&terms, fun);
  if (is_unit_type(&types, tau)) {
    assert(unit_type_rep(&terms, tau) == fun);
    return fun;
  }

  // try simplification
  while (term_kind(&terms, fun) == UPDATE_TERM) {
    // fun is (update f b_1 ... b_n v)
    update = update_term_desc(&terms, fun);
    assert(update->arity == n+2);

    if (equal_term_arrays(n, update->arg + 1, arg)) {
      // b_1 = a_1, ..., b_n = a_n so
      // (update (update fun b_1 ... b_n v0) a_1 ... a_n new_v)) --> (update fun (a_1 ... a_n) new_v)
      fun = update->arg[0];
    } else {
      break;
    }
  }

  return update_term(&terms, fun, n, arg, new_v);
}



/*
 * (distinct t1 ... t_n):
 *
 * if n == 1 --> true
 * if n == 2 --> (diseq t1 t2)
 * if t_i and t_j are equal --> false
 * if all are disequal --> true
 *
 * More simplifications uses type information,
 *  (distinct f g h) --> false if f g h are boolean.
 */
EXPORTED term_t yices_distinct(uint32_t n, term_t arg[]) {
  uint32_t i;
  type_t tau;

  if (n == 2) {
    return yices_neq(arg[0], arg[1]);
  }
  
  if (! check_positive(n) ||
      ! check_arity(n) ||
      ! check_good_distinct_term(&terms, n, arg)) {
    return NULL_TERM;
  }

  if (n == 1) {
    return true_term;
  }

  // check for finite types
  tau = term_type(&terms, arg[0]);
  if (type_card(&types, tau) < n && type_card_is_exact(&types, tau)) {
    // card exact implies that tau is finite (and small)
    return false_term;
  }

  
  // check if two of the terms are equal
  int_array_sort(arg, n);
  for (i=1; i<n; i++) {
    if (arg[i] == arg[i-1]) {
      return false_term;
    }
  }

  // WARNING: THIS CAN BE EXPENSIVE
  if (pairwise_disequal_terms(&terms, n, arg)) {
    return true_term;
  }

  return distinct_term(&terms, n, arg);
}


/*
 * (tuple-update tuple index new_v) is (tuple with component i set to new_v)
 *
 * If tuple is (mk-tuple x_0 ... x_i ... x_n-1) then 
 *  (tuple-update t i v) is (mk-tuple x_0 ... v ... x_n-1)
 * 
 * Otherwise, 
 *  (tuple-update t i v) is (mk-tuple (select t 0) ... v  ... (select t n-1))
 *              
 */
static term_t mk_tuple_aux(term_table_t *tbl, term_t tuple, uint32_t n, uint32_t i, term_t v) {
  composite_term_t *desc;
  term_t *a;
  term_t t;
  uint32_t j;

  // use vector0 as buffer:
  resize_ivector(&vector0, n);
  a = vector0.data;

  if (term_kind(tbl, tuple) == TUPLE_TERM) {
    desc = tuple_term_desc(tbl, tuple);
    for (j=0; j<n; j++) {
      if (i == j) {
	a[j] = v;
      } else {
	a[j] = desc->arg[j];
      }
    }
  } else {
    for (j=0; j<n; j++) {
      if (i == j) {
	a[j] = v;
      } else {
	a[j] = select_term(tbl, j, tuple);
      }
    }    
  }

  t = tuple_term(tbl, n, a);

  // cleanup
  ivector_reset(&vector0);

  return t;
}


EXPORTED term_t yices_tuple_update(term_t tuple, uint32_t index, term_t new_v) {
  uint32_t n;
  type_t tau;

  if (! check_good_tuple_update(&terms, index, tuple, new_v)) {
    return NULL_TERM;
  }

  // singleton type
  tau = term_type(&terms, tuple);
  if (is_unit_type(&types, tau)) {
    assert(unit_type_rep(&terms, tau) == tuple);
    return tuple;
  }

  n = tuple_type_arity(&types, tau);
  return mk_tuple_aux(&terms, tuple, n, index, new_v);
}



/*
 * Sort variables in increasing order (of term index)
 *
 * Simplification
 *  (forall (x_1::t_1 ... x_n::t_n) true) --> true
 *  (forall (x_1::t_1 ... x_n::t_n) false) --> false (types are nonempty)
 *
 *  (exists (x_1::t_1 ... x_n::t_n) true) --> true
 *  (exists (x_1::t_1 ... x_n::t_n) false) --> false (types are nonempty)
 */
EXPORTED term_t yices_forall(uint32_t n, term_t var[], term_t body) {
  if (n > 1) { 
    int_array_sort(var, n);    
  }

  if (! check_good_quantified_term(&terms, n, var, body)) {
    return NULL_TERM;
  }

  if (body == true_term) return body;
  if (body == false_term) return body;

  return forall_term(&terms, n, var, body);
}

EXPORTED term_t yices_exists(uint32_t n, term_t var[], term_t body) {
  if (n > 1) { 
    int_array_sort(var, n);    
  }

  if (! check_good_quantified_term(&terms, n, var, body)) {
    return NULL_TERM;
  }

  if (body == true_term) return body;
  if (body == false_term) return body;

  // (not (forall ... (not body))
  return opposite_term(forall_term(&terms, n, var, opposite_term(body)));
}




/*************************
 *  RATIONAL CONSTANTS   *
 ************************/

/*
 * Integer constants
 */
EXPORTED term_t yices_zero(void) {
  return zero_term;
}

EXPORTED term_t yices_int32(int32_t val) {
  q_set32(&r0, val);
  return arith_constant(&terms, &r0);
}

EXPORTED term_t yices_int64(int64_t val) {
  q_set64(&r0, val);
  return arith_constant(&terms, &r0);
}


/*
 * Rational constants
 */
EXPORTED term_t yices_rational32(int32_t num, uint32_t den) {
  if (den == 0) {
    error.code = DIVISION_BY_ZERO;
    return NULL_TERM;
  }

  q_set_int32(&r0, num, den);
  return arith_constant(&terms, &r0);
}

EXPORTED term_t yices_rational64(int64_t num, uint64_t den) {
  if (den == 0) {
    error.code = DIVISION_BY_ZERO;
    return NULL_TERM;
  }

  q_set_int64(&r0, num, den);
  return arith_constant(&terms, &r0);
}


/*
 * Constant from GMP integers or rationals
 */
EXPORTED term_t yices_mpz(mpz_t z) {
  term_t t;

  q_set_mpz(&r0, z);
  t = arith_constant(&terms, &r0);
  q_clear(&r0);

  return t;
}

EXPORTED term_t yices_mpq(mpq_t q) {
  term_t t;

  q_set_mpq(&r0, q);
  t = arith_constant(&terms, &r0);
  q_clear(&r0);

  return t;
}


/*
 * Convert a string to a rational or integer term. 
 * The string format is
 *     <optional_sign> <numerator>/<denominator>
 *  or <optional_sign> <numerator>
 *
 * where <optional_sign> is + or - or nothing
 * <numerator> and <denominator> are sequences of 
 * decimal digits.
 *
 * Error report:
 *    code = INVALID_RATIONAL_FORMAT
 * or code = DIVISION_BY_ZERO
 */
EXPORTED term_t yices_parse_rational(char *s) {
  int32_t code;
  term_t t;

  code = q_set_from_string(&r0, s);
  if (code < 0) {
    if (code == -1) {
      // wrong format
      error.code = INVALID_RATIONAL_FORMAT;
    } else {
      // denominator is 0
      error.code = DIVISION_BY_ZERO;
    }
    return NULL_TERM;
  }

  t = arith_constant(&terms, &r0);
  q_clear(&r0);

  return t;
}


/*
 * Convert a string in floating point format to a rational
 * The string must be in one of the following formats:
 *   <optional sign> <integer part> . <fractional part>
 *   <optional sign> <integer part> <exp> <optional sign> <integer>
 *   <optional sign> <integer part> . <fractional part> <exp> <optional sign> <integer>
 * 
 * where <optional sign> is + or - or nothing
 *       <exp> is either 'e' or 'E'
 *
 * Error report:
 * code = INVALID_FLOAT_FORMAT
 */
EXPORTED term_t yices_parse_float(char *s) {
  term_t t;

  if (q_set_from_float_string(&r0, s) < 0) {
    // wrong format
    error.code = INVALID_FLOAT_FORMAT;
    return NULL_TERM;
  }

  t = arith_constant(&terms, &r0);
  q_clear(&r0);

  return t;
}


/***************************
 *  ARITHMETIC OPERATIONS  *
 **************************/

/*
 * Add t1 and t2
 */
EXPORTED term_t yices_add(term_t t1, term_t t2) {
  arith_buffer_t *b;

  if (! check_both_arith_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t1);
  arith_buffer_add_term(b, &terms, t2);

  return arith_buffer_get_term(b);
}


/*
 * Subtract t2 from t1
 */
EXPORTED term_t yices_sub(term_t t1, term_t t2) {
  arith_buffer_t *b;

  if (! check_both_arith_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t1);
  arith_buffer_sub_term(b, &terms, t2);

  return arith_buffer_get_term(b);
}



/*
 * Negate t1
 */
EXPORTED term_t yices_neg(term_t t1) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t1) || 
      ! check_arith_term(&terms, t1)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_sub_term(b, &terms, t1);

  return arith_buffer_get_term(b);
}




/*
 * Multiply t1 and t2
 */
EXPORTED term_t yices_mul(term_t t1, term_t t2) {
  arith_buffer_t *b;

  if (! check_both_arith_terms(&terms, t1, t2) ||
      ! check_product_degree(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t1);
  arith_buffer_mul_term(b, &terms, t2);

  return arith_buffer_get_term(b);
}


/*
 * Compute the square of t1
 */
EXPORTED term_t yices_square(term_t t1) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t1) || 
      ! check_arith_term(&terms, t1) ||
      ! check_square_degree(&terms, t1)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t1);
  arith_buffer_mul_term(b, &terms, t1);

  return arith_buffer_get_term(b);
}




/*******************
 *   POLYNOMIALS   *
 ******************/

/*
 * integer coefficients
 */
EXPORTED term_t yices_poly_int32(uint32_t n, int32_t a[], term_t t[]) {
  arith_buffer_t *b;
  uint32_t i;

  if (! check_good_terms(&terms, n, t) ||
      ! check_arithmetic_args(&terms, n, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  for (i=0; i<n; i++) {
    q_set32(&r0, a[i]);
    arith_buffer_add_const_times_term(b, &terms, &r0, t[i]);
  }

  return arith_buffer_get_term(b);  
}

EXPORTED term_t yices_poly_int64(uint32_t n, int64_t a[], term_t t[]) {
  arith_buffer_t *b;
  uint32_t i;

  if (! check_good_terms(&terms, n, t) ||
      ! check_arithmetic_args(&terms, n, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  for (i=0; i<n; i++) {
    q_set64(&r0, a[i]);
    arith_buffer_add_const_times_term(b, &terms, &r0, t[i]);
  }

  return arith_buffer_get_term(b);  
}


/*
 * Polynomial with rational coefficients
 * - den, num, and t must be arrays of size n
 * - the coefficient a_i is den[i]/num[i]
 * 
 * Error report:
 * if num[i] is 0
 *   code = DIVISION_BY_ZERO
 */
EXPORTED term_t yices_poly_rational32(uint32_t n, int32_t num[], uint32_t den[], term_t t[]) {
  arith_buffer_t *b;
  uint32_t i;

  if (! check_good_terms(&terms, n, t) ||
      ! check_arithmetic_args(&terms, n, t) || 
      ! check_denominators32(n, den)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  for (i=0; i<n; i++) {
    q_set_int32(&r0, num[i], den[i]);
    arith_buffer_add_const_times_term(b, &terms, &r0, t[i]);
  }

  return arith_buffer_get_term(b);  
}

EXPORTED term_t yices_poly_rational64(uint32_t n, int64_t num[], uint64_t den[], term_t t[]) {
  arith_buffer_t *b;
  uint32_t i;

  if (! check_good_terms(&terms, n, t) ||
      ! check_arithmetic_args(&terms, n, t) ||
      ! check_denominators64(n, den)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  for (i=0; i<n; i++) {
    q_set_int64(&r0, num[i], den[i]);
    arith_buffer_add_const_times_term(b, &terms, &r0, t[i]);
  }

  return arith_buffer_get_term(b);  
}


/*
 * GMP integers and rationals
 */
EXPORTED term_t yices_poly_mpz(uint32_t n, mpz_t z[], term_t t[]) {
  arith_buffer_t *b;
  uint32_t i;

  if (! check_good_terms(&terms, n, t) ||
      ! check_arithmetic_args(&terms, n, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  for (i=0; i<n; i++) {
    q_set_mpz(&r0, z[i]);
    arith_buffer_add_const_times_term(b, &terms, &r0, t[i]);
  }

  q_clear(&r0);

  return arith_buffer_get_term(b);  
}


EXPORTED term_t yices_poly_mpq(uint32_t n, mpq_t q[], term_t t[]) {
  arith_buffer_t *b;
  uint32_t i;

  if (! check_good_terms(&terms, n, t) ||
      ! check_arithmetic_args(&terms, n, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  for (i=0; i<n; i++) {
    q_set_mpq(&r0, q[i]);
    arith_buffer_add_const_times_term(b, &terms, &r0, t[i]);
  }

  q_clear(&r0);

  return arith_buffer_get_term(b);  
}








/**********************
 *  ARITHMETIC ATOMS  *
 *********************/

// t1 == t2
EXPORTED term_t yices_arith_eq_atom(term_t t1, term_t t2) {
  if (! check_both_arith_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  return mk_aritheq(t1, t2);
}

// (t1 != t2) is not (t1 == t2)
EXPORTED term_t yices_arith_neq_atom(term_t t1, term_t t2) {
  if (! check_both_arith_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  return mk_arithneq(t1, t2);
}

// build t1 >= t2
EXPORTED term_t yices_arith_geq_atom(term_t t1, term_t t2) {
  if (! check_both_arith_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  return mk_arithgeq(t1, t2);
}

// (t1 < t2) is (not (t1 >= t2))
EXPORTED term_t yices_arith_lt_atom(term_t t1, term_t t2) {
  if (! check_both_arith_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  return opposite_term(mk_arithgeq(t1, t2));
}

// (t1 > t2) is (t2 < t1)
EXPORTED term_t yices_arith_gt_atom(term_t t1, term_t t2) {
  return yices_arith_lt_atom(t2, t1);
}

// (t1 <= t2) is (t2 >= t1)
EXPORTED term_t yices_arith_leq_atom(term_t t1, term_t t2) {
  return yices_arith_geq_atom(t2, t1);
}



/*
 * Comparison with 0:
 *
 * Return NULL_TERM if there's an error.
 *
 * Error report:
 * if t is not valid:
 *   code = INVALID_TERM
 *   term1 = t
 *   index = -1
 * if t is not an arithmetic term
 *   code = ARITH_TERM_REQUIRES
 *   term1 = t
 *   index = -1
 */
EXPORTED term_t yices_arith_eq0_atom(term_t t) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t) || 
      ! check_arith_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t);

  return arith_buffer_get_eq0_atom(b);
}

EXPORTED term_t yices_arith_neq0_atom(term_t t) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t) || 
      ! check_arith_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t);

  return opposite_term(arith_buffer_get_eq0_atom(b));
}

EXPORTED term_t yices_arith_geq0_atom(term_t t) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t) || 
      ! check_arith_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t);

  return arith_buffer_get_geq0_atom(b);
}

EXPORTED term_t yices_arith_leq0_atom(term_t t) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t) || 
      ! check_arith_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t);

  return arith_buffer_get_leq0_atom(b);
}

EXPORTED term_t yices_arith_gt0_atom(term_t t) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t) || 
      ! check_arith_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t);

  return arith_buffer_get_gt0_atom(b);
}

EXPORTED term_t yices_arith_lt0_atom(term_t t) {
  arith_buffer_t *b;

  if (! check_good_term(&terms, t) || 
      ! check_arith_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_arith_buffer();
  arith_buffer_reset(b);
  arith_buffer_add_term(b, &terms, t);

  return arith_buffer_get_lt0_atom(b);
}



/**************************
 *  BITVECTOR CONSTANTS   *
 *************************/

/*
 * Auxiliary function: build a constant bitvector term from c
 * - normalize c first
 * - create a bv64_constant if c->size <= 64
 * - create a bv_constant otherwise
 */
static term_t bvconstant_get_term(bvconstant_t *c) {  
  uint32_t n;
  uint64_t x;
  term_t t;

  assert(c->bitsize > 0);

  n = c->bitsize;
  bvconst_normalize(c->data, n);

  if (n <= 64) {
    if (n <= 32) {
      x = bvconst_get32(c->data);
    } else {
      x = bvconst_get64(c->data);
    }
    t = bv64_constant(&terms, n, x);
  } else {
    t = bvconst_term(&terms, n, c->data);
  }

  return t;
}

/*
 * Initialize bv0 from 32 or 64 bit integer x, or from a GMP integer
 * n = number of bits must be positive.
 * if n>32 or n>64, the highorder bits are 0
 */
EXPORTED term_t yices_bvconst_uint32(uint32_t n, uint32_t x) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_bitsize(&bv0, n);
  bvconst_set32(bv0.data, bv0.width, x);

  return bvconstant_get_term(&bv0);
}

EXPORTED term_t yices_bvconst_uint64(uint32_t n, uint64_t x) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_bitsize(&bv0, n);
  bvconst_set64(bv0.data, bv0.width, x);

  return bvconstant_get_term(&bv0);
}

EXPORTED term_t yices_bvconst_mpz(uint32_t n, mpz_t x) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_bitsize(&bv0, n);
  bvconst_set_mpz(bv0.data, bv0.width, x);

  return bvconstant_get_term(&bv0);
}


/*
 * bvconst_zero: set all bits to 0
 * bvconst_one: set low-order bit to 1, all the others to 0
 * bvconst_minus_one: set all bits to 1
 */
EXPORTED term_t yices_bvconst_zero(uint32_t n) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_all_zero(&bv0, n);

  return bvconstant_get_term(&bv0);
}

EXPORTED term_t yices_bvconst_one(uint32_t n) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }
  
  bvconstant_set_bitsize(&bv0, n);
  bvconst_set_one(bv0.data, bv0.width);

  return bvconstant_get_term(&bv0);
}

EXPORTED term_t yices_bvconst_minus_one(uint32_t n) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_all_one(&bv0, n);

  return bvconstant_get_term(&bv0);
}


/*
 * Convert an integer array to a bit constant
 * - a[i] =  0 --> bit i = 0
 * - a[i] != 0 --> bit i = 1
 */
EXPORTED term_t yices_bvconst_from_array(uint32_t n, int32_t a[]) {
  if (!check_positive(n) || !check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_bitsize(&bv0, n);
  bvconst_set_array(bv0.data, a, n);

  return bvconstant_get_term(&bv0);
}



/*
 * Parse a string of '0' and '1' and convert to a bit constant
 * - the number of bits is the length of s
 * - the string is in read in big-endian format: the first character
 *   is the high-order bit.
 */
EXPORTED term_t yices_parse_bvbin(char *s) {
  uint32_t n;
  int32_t code;

  n = strlen(s);
  if (n == 0) {
    error.code = INVALID_BVBIN_FORMAT;
    return NULL_TERM;
  }

  if (! check_maxbvsize(n)) {
    return NULL_TERM;
  }

  bvconstant_set_bitsize(&bv0, n);
  code = bvconst_set_from_string(bv0.data, n, s);
  if (code < 0) {
    error.code = INVALID_BVBIN_FORMAT;
    return NULL_TERM;    
  }

  return bvconstant_get_term(&bv0);
}


/*
 * Parse a string of hexa decimal digits and convert it to a bit constant
 * - return NULL_TERM if there's a format error 
 * - the number of bits is four times the length of s
 * - the string is read in big-endian format (the first character defines
 *   the four high-order bits).
 */
EXPORTED term_t yices_parse_bvhex(char *s) {
  uint32_t n;
  int32_t code;

  n = strlen(s);
  if (n == 0) {
    error.code = INVALID_BVHEX_FORMAT;
    return NULL_TERM;
  }

  if (n > YICES_MAX_BVSIZE/4) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = ((uint64_t) n) * 4; // badval is 64bits
    return NULL_TERM;
  }

  bvconstant_set_bitsize(&bv0, 4 * n);
  code = bvconst_set_from_hexa_string(bv0.data, n, s);
  if (code < 0) {
    error.code = INVALID_BVHEX_FORMAT;
    return NULL_TERM;    
  }  

  return bvconstant_get_term(&bv0);
}







/***************************
 *  BIT-VECTOR ARITHMETIC  *
 ***************************/

/*
 * Every operation: add/sub/neg/mul/square has two variants
 * - one for bitvectors of small size (1 to 64 bits)
 * - one for bitvectors of more than 64 bits
 */
static term_t mk_bvadd64(term_t t1, term_t t2) {
  bvarith64_buffer_t *b;

  b = get_internal_bvarith64_buffer();
  bvarith64_buffer_set_term(b, &terms, t1);
  bvarith64_buffer_add_term(b, &terms, t2);

  return bvarith64_buffer_get_term(b);
}

static term_t mk_bvadd(term_t t1, term_t t2) {
  bvarith_buffer_t *b;

  b = get_internal_bvarith_buffer();
  bvarith_buffer_set_term(b, &terms, t1);
  bvarith_buffer_add_term(b, &terms, t2);

  return bvarith_buffer_get_term(b);
}

EXPORTED term_t yices_bvadd(term_t t1, term_t t2) {  
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  if (term_bitsize(&terms, t1) <= 64) {
    return mk_bvadd64(t1, t2);
  } else {
    return mk_bvadd(t1, t2);
  }
}


static term_t mk_bvsub64(term_t t1, term_t t2) {
  bvarith64_buffer_t *b;

  b = get_internal_bvarith64_buffer();
  bvarith64_buffer_set_term(b, &terms, t1);
  bvarith64_buffer_sub_term(b, &terms, t2);

  return bvarith64_buffer_get_term(b);
}

static term_t mk_bvsub(term_t t1, term_t t2) {
  bvarith_buffer_t *b;

  b = get_internal_bvarith_buffer();
  bvarith_buffer_set_term(b, &terms, t1);
  bvarith_buffer_sub_term(b, &terms, t2);

  return bvarith_buffer_get_term(b);
}

EXPORTED term_t yices_bvsub(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  if (term_bitsize(&terms, t1) <= 64) {
    return mk_bvsub64(t1, t2);
  } else {
    return mk_bvsub(t1, t2);
  }
}


static term_t mk_bvneg64(term_t t1) {
  bvarith64_buffer_t *b;

  b = get_internal_bvarith64_buffer();
  bvarith64_buffer_set_term(b, &terms, t1);
  bvarith64_buffer_negate(b);

  return bvarith64_buffer_get_term(b);
}

static term_t mk_bvneg(term_t t1) {
  bvarith_buffer_t *b;

  b = get_internal_bvarith_buffer();
  bvarith_buffer_set_term(b, &terms, t1);
  bvarith_buffer_negate(b);

  return bvarith_buffer_get_term(b);
}

EXPORTED term_t yices_bvneg(term_t t1) {
  if (! check_good_term(&terms, t1) ||
      ! check_bitvector_term(&terms, t1)) {
    return NULL_TERM;
  }

  if (term_bitsize(&terms, t1) <= 64) {
    return mk_bvneg64(t1);
  } else {
    return mk_bvneg(t1);
  }
}


static term_t mk_bvmul64(term_t t1, term_t t2) {
  bvarith64_buffer_t *b;

  b = get_internal_bvarith64_buffer();
  bvarith64_buffer_set_term(b, &terms, t1);
  bvarith64_buffer_mul_term(b, &terms, t2);

  return bvarith64_buffer_get_term(b);
}

static term_t mk_bvmul(term_t t1, term_t t2) {
  bvarith_buffer_t *b;

  b = get_internal_bvarith_buffer();
  bvarith_buffer_set_term(b, &terms, t1);
  bvarith_buffer_mul_term(b, &terms, t2);

  return bvarith_buffer_get_term(b);
}

EXPORTED term_t yices_bvmul(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2) || 
      ! check_product_degree(&terms, t1, t2)) {
    return NULL_TERM;
  }

  if (term_bitsize(&terms, t1) <= 64) {
    return mk_bvmul64(t1, t2);
  } else {
    return mk_bvmul(t1, t2);
  }
}


static term_t mk_bvsquare64(term_t t1) {
  bvarith64_buffer_t *b;

  b = get_internal_bvarith64_buffer();
  bvarith64_buffer_set_term(b, &terms, t1);
  bvarith64_buffer_square(b);

  return bvarith64_buffer_get_term(b);
}

static term_t mk_bvsquare(term_t t1) {
  bvarith_buffer_t *b;

  b = get_internal_bvarith_buffer();
  bvarith_buffer_set_term(b, &terms, t1);
  bvarith_buffer_square(b);

  return bvarith_buffer_get_term(b);
}

EXPORTED term_t yices_bvsquare(term_t t1) {
  if (! check_good_term(&terms, t1) ||
      ! check_bitvector_term(&terms, t1) || 
      ! check_square_degree(&terms, t1)) {
    return NULL_TERM;
  }

  if (term_bitsize(&terms, t1) <= 64) {
    return mk_bvsquare64(t1);
  } else {
    return mk_bvsquare(t1);
  }
}



/***********************************
 *  BITWISE BIT-VECTOR OPERATIONS  *
 **********************************/

EXPORTED term_t yices_bvnot(term_t t1) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t1) ||
      ! check_bitvector_term(&terms, t1)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_not(b);

  return bvlogic_buffer_get_term(b);
}


EXPORTED term_t yices_bvand(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_and_term(b, &terms, t2);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_bvor(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b ,&terms, t1);
  bvlogic_buffer_or_term(b, &terms, t2);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_bvxor(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;
  
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_xor_term(b, &terms, t2);

  return bvlogic_buffer_get_term(b);
}


EXPORTED term_t yices_bvnand(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_and_term(b, &terms, t2);
  bvlogic_buffer_not(b);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_bvnor(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_or_term(b, &terms, t2);
  bvlogic_buffer_not(b);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_bvxnor(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_xor_term(b, &terms, t2);
  bvlogic_buffer_not(b);

  return bvlogic_buffer_get_term(b);
}




/*********************************************
 *   BITVECTOR SHIFT/ROTATION BY A CONSTANT  *
 ********************************************/

/*
 * Shift or rotation by an integer constant n
 * - shift_left0 sets the low-order bits to zero
 * - shift_left1 sets the low-order bits to one
 * - shift_rigth0 sets the high-order bits to zero
 * - shift_right1 sets the high-order bits to one
 * - ashift_right is arithmetic shift, it copies the sign bit &
 * - rotate_left: circular rotation
 * - rotate_right: circular rotation 
 *
 * If t is a vector of m bits, then n must satisfy 0 <= n <= m.
 *
 * The functions return NULL_TERM (-1) if there's an error.
 *
 * Error reports:
 * if t is not valid
 *   code = INVALID_TERM
 *   term1 = t
 * if t is not a bitvector term
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 * if n > size of t
 *   code = INVALID_BITSHIFT
 *   badval = n
 */
EXPORTED term_t yices_shift_left0(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_shift_left0(b, n);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_shift_left1(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_shift_left1(b, n);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_shift_right0(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_shift_right0(b, n);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_shift_right1(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_shift_right1(b, n);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_ashift_right(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_ashift_right(b, n);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_rotate_left(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  if (n < b->bitsize) {
    bvlogic_buffer_rotate_left(b, n);
  }

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_rotate_right(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) || 
      ! check_bitshift(n, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }
  
  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  if (n < b->bitsize) {
    bvlogic_buffer_rotate_right(b, n);
  }

  return bvlogic_buffer_get_term(b);
}



/****************************************
 *  BITVECTOR EXTRACTION/CONCATENATION  *
 ***************************************/

/*
 * Extract a subvector of t
 * - t must be a bitvector term of size m
 * - i and j must satisfy 0 <= i <= j <= m-1
 * The result is the bits i to j of t.
 *
 * Return NULL_TERM (-1) if there's an error.
 *
 * Error reports:
 * if t is not valid
 *   code = INVALID_TERM
 *   term1 = t
 * if t is not a bitvector term
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 * if 0 <= i <= j <= m-1 does not hold
 *   code = INVALID_BVEXTRACT
 */
EXPORTED term_t yices_bvextract(term_t t, uint32_t i, uint32_t j) {
  bvlogic_buffer_t *b;
  uint32_t n;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t)) {
    return NULL_TERM;
  }

  n = term_bitsize(&terms, t);
  if (! check_bitextract(i, j, n)) {
    return NULL_TERM;
  }

  if (i == 0 && j == n-1) {
    return t;
  } else {
    b = get_internal_bvlogic_buffer();
    bvlogic_buffer_set_slice_term(b, &terms, i, j, t);
    return bvlogic_buffer_get_term(b);
  }
}


/*
 * Concatenation
 * - t1 and t2 must be bitvector terms
 *
 * Return NULL_TERM (-1) if there's an error.
 *
 * Error reports
 * if t1 or t2 is not a valid term
 *   code = INVALID_TERM
 *   term1 = t1 or t2
 * if t1 or t2 is not a bitvector term
 *   code = BITVECTOR_REQUIRED
 *   term1 = t1 or t2
 * if the size of the result would be larger than MAX_BVSIZE
 *   code = MAX_BVSIZE_EXCEEDED
 *   badval = n1 + n2 (n1 = size of t1, n2 = size of t2)
 */
EXPORTED term_t yices_bvconcat(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t1) ||
      ! check_good_term(&terms, t2) ||
      ! check_bitvector_term(&terms, t1) ||
      ! check_bitvector_term(&terms, t2) || 
      ! check_maxbvsize(term_bitsize(&terms, t1) + term_bitsize(&terms, t2))) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t2);
  bvlogic_buffer_concat_left_term(b, &terms, t1);

  return bvlogic_buffer_get_term(b);
}


/*
 * Repeated concatenation:
 * - make n copies of t and concatenate them
 * - n must be positive
 *
 * Return NULL_TERM (-1) if there's an error
 *
 * Error report:
 * if t is not valid
 *   code = INVALID_TERM
 *   term1 = t
 * if t is not a bitvector term
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 * if n <= 0
 *   code = POSINT_REQUIRED
 *   badval = n
 * if size of the result would be more than MAX_BVSIZE
 *   code = MAX_BVSIZE_EXCEEDED
 *   badval = n * bitsize of t
 */
EXPORTED term_t yices_bvrepeat(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;
  uint64_t m;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) ||
      ! check_positive(n)) {
    return NULL_TERM;
  }

  // check size
  m = ((uint64_t) n) * term_bitsize(&terms, t);
  if (m > (uint64_t) YICES_MAX_BVSIZE) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = m;
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_repeat_concat(b, n);

  return bvlogic_buffer_get_term(b);
}


/*
 * Sign extension
 * - add n copies of t's sign bit
 * - n must be non-negative
 *
 * Return NULL_TERM if there's an error.
 *
 * Error reports:
 * if t is invalid
 *   code = INVALID_TERM
 *   term1 = t
 * if t is not a bitvector
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 * if n + bitsize of t is too large:
 *   code = MAX_BVSIZE_EXCEEDED
 *   badval = n + bitsize of t
 */
EXPORTED term_t yices_sign_extend(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;
  uint64_t m;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t)) {
    return NULL_TERM;
  }

  // check size
  m = ((uint64_t) n) + term_bitsize(&terms, t);
  if (m > (uint64_t) YICES_MAX_BVSIZE) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = m;
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_sign_extend(b, b->bitsize + n);

  return bvlogic_buffer_get_term(b);
}


/*
 * Zero extension
 * - add n zeros to t
 * - n must be non-negative
 *
 * Return NULL_TERM if there's an error.
 *
 * Error reports:
 * if t is invalid
 *   code = INVALID_TERM
 *   term1 = t
 * if t is not a bitvector
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 */
EXPORTED term_t yices_zero_extend(term_t t, uint32_t n) {
  bvlogic_buffer_t *b;
  uint64_t m;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t)) {
    return NULL_TERM;
  }

  // check size
  m = ((uint64_t) n) + term_bitsize(&terms, t);
  if (m > (uint64_t) YICES_MAX_BVSIZE) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = m;
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_zero_extend(b, b->bitsize + n);

  return bvlogic_buffer_get_term(b);
}



/*
 * AND-reduction: 
 * if t is b[m-1] ... b[0], then the result is a bit-vector of 1 bit
 * equal to the conjunction of all bits of t (i.e., (and b[0] ... b[m-1])
 *
 * OR-reduction: compute (or b[0] ... b[m-1])
 *
 * Return NULL_TERM if there's an error
 *
 * Error reports:
 * if t is invalid
 *   code = INVALID_TERM
 *   term1 = t
 * if t is not a bitvector
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 */
EXPORTED term_t yices_redand(term_t t) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_redand(b);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_redor(term_t t) {
  bvlogic_buffer_t *b;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t);
  bvlogic_buffer_redor(b);

  return bvlogic_buffer_get_term(b);
}


/*
 * Bitwise equality comparison: if t1 and t2 are bitvectors of size n,
 * construct (bvand (bvxnor t1 t2))
 *
 * Return NULL_TERM if there's an error
 *
 * Error reports:
 * if t1 or t2 is not valid
 *   code = INVALID_TERM
 *   term1 = t1 or t2
 *   index = -1
 * if t1 or t2 is not a bitvector term
 *   code = BITVECTOR_REQUIRED
 *   term1 = t1 or t2
 * if t1 and t2 do not have the same bitvector type
 *   code = INCOMPATIBLE_TYPES
 *   term1 = t1
 *   type1 = type of t1
 *   term2 = t2
 *   type2 = type of t2
 */
EXPORTED term_t yices_redcomp(term_t t1, term_t t2) {
  bvlogic_buffer_t *b;

  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_comp_term(b, &terms, t2);

  return bvlogic_buffer_get_term(b);
}




/*******************************
 *  GENERIC BIT-VECTOR SHIFTS  *
 *****************************/

/*
 * All shift operators takes two bit-vector arguments of the same size.
 * The first argument is shifted. The second argument is the shift amount.
 * - bvshl t1 t2: shift left, padding with 0
 * - bvlshr t1 t2: logical shift right (padding with 0)
 * - bvashr t1 t2: arithmetic shift right (copy the sign bit)
 *
 * We check whether t2 is a bit-vector constant and convert to
 * constant bit-shifts in such cases.
 */

// shift left: shift amount given by a small bitvector constant
static term_t mk_bvshl_const64(term_t t1, bvconst64_term_t *c) {
  bvlogic_buffer_t *b;

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_shl_constant64(b, c->bitsize, c->value);

  return bvlogic_buffer_get_term(b);
}

// shift left: amount given by a large bitvector constant
static term_t mk_bvshl_const(term_t t1, bvconst_term_t *c) {
  bvlogic_buffer_t *b;

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_shl_constant(b, c->bitsize, c->data);

  return bvlogic_buffer_get_term(b);
}

// special case: if t1 is 0b000...0 then (bvshl t1 t2) = t2 for any t2
static bool term_is_bvzero(term_t t1) {
  bvconst64_term_t *u;
  bvconst_term_t *v;
  uint32_t k;

  switch (term_kind(&terms, t1)) {
  case BV64_CONSTANT:
    u = bvconst64_term_desc(&terms, t1);
    assert(u->value == norm64(u->value, u->bitsize));
    return u->value == 0;

  case BV_CONSTANT:
    v = bvconst_term_desc(&terms, t1);
    k = (v->bitsize + 31) >> 5; // number of words in v
    return bvconst_is_zero(v->data, k);

  default:
    return false;
  }
}

EXPORTED term_t yices_bvshl(term_t t1, term_t t2) {  
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    return mk_bvshl_const64(t1, bvconst64_term_desc(&terms, t2));

  case BV_CONSTANT:
    return mk_bvshl_const(t1, bvconst_term_desc(&terms, t2));

  default:
    if (term_is_bvzero(t1)) {
      return t1;
    } else {
      return bvshl_term(&terms, t1, t2);
    }
  }
}



// logical shift right: amount given by a small bitvector constant
static term_t mk_bvlshr_const64(term_t t1, bvconst64_term_t *c) {
  bvlogic_buffer_t *b;

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_lshr_constant64(b, c->bitsize, c->value);

  return bvlogic_buffer_get_term(b);
}

// logical shift right: amount given by a large bitvector constant
static term_t mk_bvlshr_const(term_t t1, bvconst_term_t *c) {
  bvlogic_buffer_t *b;

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_lshr_constant(b, c->bitsize, c->data);

  return bvlogic_buffer_get_term(b);
}

EXPORTED term_t yices_bvlshr(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    return mk_bvlshr_const64(t1, bvconst64_term_desc(&terms, t2));

  case BV_CONSTANT:
    return mk_bvlshr_const(t1, bvconst_term_desc(&terms, t2));

  default:
    // as above: if t1 is zero, then shifting does not change it
    if (term_is_bvzero(t1)) {
      return t1;
    } else {
      return bvlshr_term(&terms, t1, t2);
    }
  }
}



// logical shift right: amount given by a small bitvector constant
static term_t mk_bvashr_const64(term_t t1, bvconst64_term_t *c) {
  bvlogic_buffer_t *b;

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_ashr_constant64(b, c->bitsize, c->value);

  return bvlogic_buffer_get_term(b);
}

// logical shift right: amount given by a large bitvector constant
static term_t mk_bvashr_const(term_t t1, bvconst_term_t *c) {
  bvlogic_buffer_t *b;

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term(b, &terms, t1);
  bvlogic_buffer_ashr_constant(b, c->bitsize, c->data);

  return bvlogic_buffer_get_term(b);
}

// special case: if t1 is 0b00...00 or 0b11...11 then arithmetic shift
// leaves t1 unchanged (whatever t2)
static bool term_is_bvashr_invariant(term_t t1) {
  bvconst64_term_t *u;
  bvconst_term_t *v;
  uint32_t k;

  switch (term_kind(&terms, t1)) {
  case BV64_CONSTANT:
    u = bvconst64_term_desc(&terms, t1);
    assert(u->value == norm64(u->value, u->bitsize));
    return u->value == 0 || bvconst64_is_minus_one(u->value, u->bitsize);

  case BV_CONSTANT:
    v = bvconst_term_desc(&terms, t1);
    k = (v->bitsize + 31) >> 5; // number of words in v
    return bvconst_is_zero(v->data, k) || bvconst_is_minus_one(v->data, v->bitsize);

  default:
    return false;
  }

}

EXPORTED term_t yices_bvashr(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    return mk_bvashr_const64(t1, bvconst64_term_desc(&terms, t2));

  case BV_CONSTANT:
    return mk_bvashr_const(t1, bvconst_term_desc(&terms, t2));

  default:
    if (term_is_bvashr_invariant(t1)) {
      return t1;
    } else {
      return bvashr_term(&terms, t1, t2);
    }
  }
}






/**********************************
 *  BITVECTOR DIVISION OPERATORS  *
 *********************************/

/*
 * These are all new SMTLIB division and remainder operators.
 * All are binary operators with two bitvector arguments of the same size.
 * - bvdiv: quotient in unsigned division
 * - bvrem: remainder in unsigned division
 * - bvsdiv: quotient in signed division (rounding toward 0)
 * - bvsrem: remainder in signed division
 * - bvsmod: remainder in floor division (signed division, rounding toward -infinity)
 *
 * TODO: We could convert division/remainder when t2 is a constant powers of two 
 * to shift and bit masking operations?
 */

// (div a b): both constants
static term_t bvdiv_const64(bvconst64_term_t *a, bvconst64_term_t *b) {
  uint64_t x;
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize);
  x = bvconst64_udiv2z(a->value, b->value, n);
  assert(x == norm64(x, n));

  return bv64_constant(&terms, n, x);
}

static term_t bvdiv_const(bvconst_term_t *a, bvconst_term_t *b) {
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize && n > 64);

  bvconstant_set_bitsize(&bv0, n);
  bvconst_udiv2z(bv0.data, n, a->data, b->data);
  bvconst_normalize(bv0.data, n);

  return bvconst_term(&terms, n, bv0.data);
}

EXPORTED term_t yices_bvdiv(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    if (term_kind(&terms, t1) == BV64_CONSTANT) {
      return bvdiv_const64(bvconst64_term_desc(&terms, t1), bvconst64_term_desc(&terms, t2));
    }
    break;

  case BV_CONSTANT:
    if (term_kind(&terms, t1) == BV_CONSTANT) {
      return bvdiv_const(bvconst_term_desc(&terms, t1), bvconst_term_desc(&terms, t2));
    }
    break;

  default:
    break;
  }

  return bvdiv_term(&terms, t1, t2);
}


// (rem a b): both constants
static term_t bvrem_const64(bvconst64_term_t *a, bvconst64_term_t *b) {
  uint64_t x;
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize);
  x = bvconst64_urem2z(a->value, b->value, n);
  assert(x == norm64(x, n));

  return bv64_constant(&terms, n, x);
}

static term_t bvrem_const(bvconst_term_t *a, bvconst_term_t *b) {
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize && n > 64);

  bvconstant_set_bitsize(&bv0, n);
  bvconst_urem2z(bv0.data, n, a->data, b->data);
  bvconst_normalize(bv0.data, n);

  return bvconst_term(&terms, n, bv0.data);
}

EXPORTED term_t yices_bvrem(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    if (term_kind(&terms, t1) == BV64_CONSTANT) {
      return bvrem_const64(bvconst64_term_desc(&terms, t1), bvconst64_term_desc(&terms, t2));
    }
    break;

  case BV_CONSTANT:
    if (term_kind(&terms, t1) == BV_CONSTANT) {
      return bvrem_const(bvconst_term_desc(&terms, t1), bvconst_term_desc(&terms, t2));
    }
    break;

  default:
    break;
  }

  return bvrem_term(&terms, t1, t2);
}


// (sdiv a b): both constants
static term_t bvsdiv_const64(bvconst64_term_t *a, bvconst64_term_t *b) {
  uint64_t x;
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize);
  x = bvconst64_sdiv2z(a->value, b->value, n);
  assert(x == norm64(x, n));

  return bv64_constant(&terms, n, x);
}

static term_t bvsdiv_const(bvconst_term_t *a, bvconst_term_t *b) {
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize && n > 64);

  bvconstant_set_bitsize(&bv0, n);
  bvconst_sdiv2z(bv0.data, n, a->data, b->data);
  bvconst_normalize(bv0.data, n);

  return bvconst_term(&terms, n, bv0.data);
}

EXPORTED term_t yices_bvsdiv(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    if (term_kind(&terms, t1) == BV64_CONSTANT) {
      return bvsdiv_const64(bvconst64_term_desc(&terms, t1), bvconst64_term_desc(&terms, t2));
    }
    break;

  case BV_CONSTANT:
    if (term_kind(&terms, t1) == BV_CONSTANT) {
      return bvsdiv_const(bvconst_term_desc(&terms, t1), bvconst_term_desc(&terms, t2));
    }
    break;

  default:
    break;
  }

  return bvsdiv_term(&terms, t1, t2);
}


// (srem a b): both constants
static term_t bvsrem_const64(bvconst64_term_t *a, bvconst64_term_t *b) {
  uint64_t x;
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize);
  x = bvconst64_srem2z(a->value, b->value, n);
  assert(x == norm64(x, n));

  return bv64_constant(&terms, n, x);
}

static term_t bvsrem_const(bvconst_term_t *a, bvconst_term_t *b) {
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize && n > 64);

  bvconstant_set_bitsize(&bv0, n);
  bvconst_srem2z(bv0.data, n, a->data, b->data);
  bvconst_normalize(bv0.data, n);

  return bvconst_term(&terms, n, bv0.data);
}

EXPORTED term_t yices_bvsrem(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    if (term_kind(&terms, t1) == BV64_CONSTANT) {
      return bvsrem_const64(bvconst64_term_desc(&terms, t1), bvconst64_term_desc(&terms, t2));
    }
    break;

  case BV_CONSTANT:
    if (term_kind(&terms, t1) == BV_CONSTANT) {
      return bvsrem_const(bvconst_term_desc(&terms, t1), bvconst_term_desc(&terms, t2));
    }
    break;

  default:
    break;
  }

  return bvsrem_term(&terms, t1, t2);
}


// (smod a b): both constants
static term_t bvsmod_const64(bvconst64_term_t *a, bvconst64_term_t *b) {
  uint64_t x;
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize);
  x = bvconst64_smod2z(a->value, b->value, n);
  assert(x == norm64(x, n));

  return bv64_constant(&terms, n, x);
}

static term_t bvsmod_const(bvconst_term_t *a, bvconst_term_t *b) {
  uint32_t n;

  n = a->bitsize;
  assert(n == b->bitsize && n > 64);

  bvconstant_set_bitsize(&bv0, n);
  bvconst_smod2z(bv0.data, n, a->data, b->data);
  bvconst_normalize(bv0.data, n);

  return bvconst_term(&terms, n, bv0.data);
}

EXPORTED term_t yices_bvsmod(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t2)) {
  case BV64_CONSTANT:
    if (term_kind(&terms, t1) == BV64_CONSTANT) {
      return bvsmod_const64(bvconst64_term_desc(&terms, t1), bvconst64_term_desc(&terms, t2));
    }
    break;

  case BV_CONSTANT:
    if (term_kind(&terms, t1) == BV_CONSTANT) {
      return bvsmod_const(bvconst_term_desc(&terms, t1), bvconst_term_desc(&terms, t2));
    }
    break;

  default:
    break;
  }

  return bvsmod_term(&terms, t1, t2);
}




/*
 * Convert an array of boolean terms arg[0 ... n-1] into
 * a bitvector term.
 *
 * Error report:
 * if n == 0
 *    code = POSINT_REQUIRED
 *    badval = n
 * if n > YICES_MAX_BVSIZE
 *    code = MAX_BVSIZE_EXCEEDED
 *    badval = size
 * if arg[i] is invalid
 *    code = INVALID_TERM
 *    term1 = arg[i]
 *    index = i
 * if arg[i] is not a boolean
 *    code = TYPE_MISMATCH
 *    term1 = arg[i]
 *    type1 = bool
 *    index = i
 */
EXPORTED term_t yices_bvarray(uint32_t n, term_t arg[]) {
  bvlogic_buffer_t *b;

  if (! check_positive(n) ||
      ! check_maxbvsize(n) ||
      ! check_good_terms(&terms, n, arg) ||
      ! check_boolean_args(&terms, n, arg)) {
    return NULL_TERM;
  }

  b = get_internal_bvlogic_buffer();
  bvlogic_buffer_set_term_array(b, &terms, n, arg);

  return bvlogic_buffer_get_term(b);
}



/*
 * Extract bit i of vector v (as a boolean)
 *
 * Error report:
 * if v is invalid
 *    code = INVALID_TERM
 *    term1 = v
 *    index = -1
 * if v is not a bitvector term
 *    code = BITVECTOR_REQUIRES
 *    term1 = t
 * if i >= v's bitsize
 *    code = INVALID_BVEXTRACT
 */
term_t yices_bitextract(term_t t, uint32_t i) {
  bvconst64_term_t *d;
  bvconst_term_t *c;
  composite_term_t *bv;
  term_t u;

  if (! check_good_term(&terms, t) ||
      ! check_bitvector_term(&terms, t) ||
      ! check_bitextract(i, i, term_bitsize(&terms, t))) {
    return NULL_TERM;
  }

  switch (term_kind(&terms, t)) {
  case BV64_CONSTANT:
    d = bvconst64_term_desc(&terms, t);
    u = bool2term(tst_bit64(d->value, i));
    break;

  case BV_CONSTANT:
    c = bvconst_term_desc(&terms, t);
    u = bool2term(bvconst_tst_bit(c->data, i));
    break;

  case BV_ARRAY:
    bv = bvarray_term_desc(&terms, t);
    u = bv->arg[i];
    break;

  default:
    u = bit_term(&terms, i, t);
    break;
  }

  return u;
}




/*********************
 *  BITVECTOR ATOMS  *
 ********************/

/*
 * EQUALITY AND DISEQUALITY
 */
EXPORTED term_t yices_bveq_atom(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  return mk_bveq(t1, t2);
}


EXPORTED term_t yices_bvneq_atom(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  return mk_bvneq(t1, t2);
}


/*
 * UNSIGNED COMPARISONS
 */

// check whether t1 < t2 holds trivially
static bool must_lt(term_t t1, term_t t2) {
  upper_bound_unsigned(&terms, t1, &bv1); // t1 <= bv1
  lower_bound_unsigned(&terms, t2, &bv2); // bv2 <= t2
  assert(bv1.bitsize == bv2.bitsize);

  return bvconst_lt(bv1.data, bv2.data, bv1.bitsize);
}

// check whether t1 <= t2 holds trivially
static bool must_le(term_t t1, term_t t2) {
  upper_bound_unsigned(&terms, t1, &bv1);
  lower_bound_unsigned(&terms, t2, &bv2);
  assert(bv1.bitsize == bv2.bitsize);

  return bvconst_le(bv1.data, bv2.data, bv1.bitsize);
}

 // t1 >= t2
EXPORTED term_t yices_bvge_atom(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }
  
  if (t1 == t2 || must_le(t2, t1)) {
    return true_term;
  }

  if (must_lt(t1, t2)) {
    return false_term;
  }
  
  return bvge_atom(&terms, t1, t2);
}

// t1 > t2
EXPORTED term_t yices_bvgt_atom(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  if (t1 == t2 || must_le(t1, t2)) {
    return false_term;
  }

  if (must_lt(t2, t1)) {
    return true_term;
  }
  
  return opposite_term(bvge_atom(&terms, t2, t1));
}


// t1 <= t2
EXPORTED term_t yices_bvle_atom(term_t t1, term_t t2) {
  return yices_bvge_atom(t2, t1);
}

// t1 < t2
EXPORTED term_t yices_bvlt_atom(term_t t1, term_t t2) {
  return yices_bvgt_atom(t2, t1);
}





/*
 * SIGNED COMPARISONS
 */

// Check whether t1 < t2 holds trivially 
static bool must_slt(term_t t1, term_t t2) {
  upper_bound_signed(&terms, t1, &bv1);
  lower_bound_signed(&terms, t2, &bv2);
  assert(bv1.bitsize == bv2.bitsize);

  return bvconst_slt(bv1.data, bv2.data, bv1.bitsize);
}

// Check whether t1 <= t2 holds
static bool must_sle(term_t t1, term_t t2) {
  upper_bound_signed(&terms, t1, &bv1);
  lower_bound_signed(&terms, t2, &bv2);
  assert(bv1.bitsize == bv2.bitsize);

  return bvconst_sle(bv1.data, bv2.data, bv1.bitsize);
}

// t1 >= t2
EXPORTED term_t yices_bvsge_atom(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  if (t1 == t2 || must_sle(t2, t1)) {
    return true_term;
  }

  if (must_slt(t1, t2)) {
    return false_term;
  }
  
  return bvsge_atom(&terms, t1, t2);
}

// t1 > t2
EXPORTED term_t yices_bvsgt_atom(term_t t1, term_t t2) {
  if (! check_compatible_bv_terms(&terms, t1, t2)) {
    return NULL_TERM;
  }

  if (t1 == t2 || must_sle(t1, t2)) {
    return false_term;
  }

  if (must_slt(t2, t1)) {
    return true_term;
  }
  
  return opposite_term(bvsge_atom(&terms, t2, t1));
}

// t1 <= t2
EXPORTED term_t yices_bvsle_atom(term_t t1, term_t t2) {
  return yices_bvsge_atom(t2, t1);
}

// t1 < t2
EXPORTED term_t yices_bvslt_atom(term_t t1, term_t t2) {
  return yices_bvsgt_atom(t2, t1);
}






/**************************
 *  SOME CHECKS ON TERMS  *
 *************************/

/*
 * Get the type of term t
 * return NULL_TYPE if t is not a valid term
 * and set the error report:
 *   code = INVALID_TERM
 *   term1 = t
 *   index = -1
 */
type_t yices_type_of_term(term_t t) {
  if (! check_good_term(&terms, t)) {
    return NULL_TYPE;
  }
  return term_type(&terms, t);
}


/*
 * Check the type of a term t:
 * - term_is_arithmetic check whether t's type is either int or real
 * - term_is_real check whether t's type is real (return false if t's type is int)
 * - term_is_int check whether t's type is int 
 * If t is not a valid term, the check functions return false
 * and set the error report as above.
 */
bool yices_term_is_bool(term_t t) {
  return check_good_term(&terms, t) && is_boolean_term(&terms, t);
}

bool yices_term_is_int(term_t t) {
  return check_good_term(&terms, t) && is_integer_term(&terms, t);
}

bool yices_term_is_real(term_t t) {
  return check_good_term(&terms, t) && is_real_term(&terms, t);
}

bool yices_term_is_arithmetic(term_t t) {
  return check_good_term(&terms, t) && is_arithmetic_term(&terms, t);
}

bool yices_term_is_bitvector(term_t t) {
  return check_good_term(&terms, t) && is_bitvector_term(&terms, t);
}

bool yices_term_is_tuple(term_t t) {
  return check_good_term(&terms, t) && is_tuple_term(&terms, t);
}

bool yices_term_is_function(term_t t) {
  return check_good_term(&terms, t) && is_function_term(&terms, t);
}



/*
 * Size of bitvector term t. 
 * return -1 if t is not a bitvector
 */
uint32_t yices_term_bitsize(term_t t) {
  if (! check_bitvector_term(&terms, t)) {
    return 0;
  }
  return term_bitsize(&terms, t);
}



/*
 * SUPPORT FOR TYPE CHECKING
 */

/*
 * Check whether t is a valid arithmetic term
 * - if not set the internal error report:
 *
 * If t is not a valid term:
 *   code = INVALID_TERM 
 *   term1 = t
 *   index = -1
 * If t is not an arithmetic term; 
 *   code = ARITHTERM_REQUIRED
 *   term1 = t
 */
bool yices_check_arith_term(term_t t) {
  return check_good_term(&terms, t) && check_arith_term(&terms, t);
}


/*
 * Check for degree overflow in the product (b * t) 
 * - b must be a buffer obtained via yices_new_arith_buffer().
 * - t must be a valid arithmetic term.
 *
 * Return true if there's no overflow.
 *
 * Return false otherwise and set the error report:
 *   code = DEGREE_OVERFLOW
 *   badval = degree of b + degree of t
 */
bool yices_check_mul_term(arith_buffer_t *b, term_t t) {
  uint32_t d1, d2;

  assert(good_term(&terms, t) && is_arithmetic_term(&terms, t));

  d1 = arith_buffer_degree(b);
  d2 = term_degree(&terms, t);
  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);
}


/*
 * Same thing for the product of two buffers b1 and b2.
 * - both must be buffers allocated using yices_new_arith_buffer().
 */
bool yices_check_mul_buffer(arith_buffer_t *b1, arith_buffer_t *b2) {
  uint32_t d1, d2;

  d1 = arith_buffer_degree(b1);
  d2 = arith_buffer_degree(b2);
  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);  
}



/*
 * Check whether n <= YICES_MAX_BVSIZE and if not set the error report.
 */
bool yices_check_bvsize(uint32_t n) {
  return check_maxbvsize(n);
}


/*
 * Check whether t is a valid bitvector term
 * - if not set the internal error report:
 *
 * If t is not a valid term:
 *   code = INVALID_TERM 
 *   term1 = t
 *   index = -1
 * If t is not an arithmetic term; 
 *   code = BITVECTOR_REQUIRED
 *   term1 = t
 */
bool yices_check_bv_term(term_t t) {
  return check_good_term(&terms, t) && check_bitvector_term(&terms, t);
}


/*
 * Check whether b is non empty
 * Error report:
 *   code = EMPTY_BITVECTOR
 */
bool yices_check_bvlogic_buffer(bvlogic_buffer_t *b) {
  if (bvlogic_buffer_is_empty(b)) {
    error.code = EMPTY_BITVECTOR;
    return false;
  }
  return true;
}


/*
 * Check whether s is a valid shift amount for buffer b:
 * - return true if 0 <= s <= b->bitsize
 * - otherwise set the error report and return false.
 */
bool yices_check_bitshift(bvlogic_buffer_t *b, int32_t s) {
  if (s < 0 || s > bvlogic_buffer_bitsize(b)) {
    error.code = INVALID_BITSHIFT;
    error.badval = s;
    return false;
  }

  return true;
}


/*
 * Check whether [i, j] is a valid segment for a vector of n bits
 * - return true if 0 <= i <= j <= n
 * - otherwise set the error report and return false.
 */
bool yices_check_bvextract(uint32_t n, int32_t i, int32_t j) {
  if (i < 0 || i > j || j >= n) {
    error.code = INVALID_BVEXTRACT;
    return false;
  }

  return true;
}


/*
 * Check whether repeat_concat(b, n) is valid
 * - return true if it is
 * - returm false and set errort report if it's not.
 *
 * Error report:
 * if n <= 0
 *   code = POSINT_REQUIRED
 *   badval = n
 * if size of the result would be more than MAX_BVSIZE
 *   code = MAX_BVSIZE_EXCEEDED
 *   badval = n * bitsize of t
 */
bool yices_check_bvrepeat(bvlogic_buffer_t *b, int32_t n) {
  uint64_t m;

  if (n <= 0) {
    error.code = POS_INT_REQUIRED;
    error.badval = n;
    return false;
  }

  m = ((uint64_t) n) * bvlogic_buffer_bitsize(b);
  if (m > ((uint64_t) YICES_MAX_BVSIZE)) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = m;
    return false;
  }

  return true;
}



/*
 * Check whether zero_extend(b, n) and sign_extend(b, n) are valid;
 * - n is the number of bits to add
 * - return true if 0 <= n, b->bitsize != 0, and (n + b->bitsize) <= MAX_BVSIZE
 * - return false and set an error report otherwise.
 *
 * Error reports:
 * - if b is empty: EMPTY_BITVECTOR
 * - if n < 0: NONNEG_INT_REQUIRED
 * - if n + b->bitsize > MAX_BVSIZE: MAX_BVSIZE_EXCEEDED
 */
bool yices_check_bvextend(bvlogic_buffer_t *b, int32_t n) {
  uint64_t m;

  if (n < 0) {
    error.code = NONNEG_INT_REQUIRED;
    error.badval = n;
    return false;
  }

  m = bvlogic_buffer_bitsize(b);
  if (m == 0) {
    error.code = EMPTY_BITVECTOR;
    return false;
  }
  
  m += n;
  if (m >= ((uint64_t) YICES_MAX_BVSIZE)) {
    error.code = MAX_BVSIZE_EXCEEDED;
    error.badval = m;
    return false;
  }

  return true;
}


/*
 * Checks for degree overflow in bitvector multiplication:
 * - four variants depending on the type of buffer used
 *   and on whether the argument is a term or a buffer
 *
 * In all cases, the function set the error report and
 * return false if there's an overflow:
 *   code = DEGREE_OVEFLOW
 *   badval = degree of the product
 *
 * All return true if there's no overflow.
 */
bool yices_check_bvmul64_term(bvarith64_buffer_t *b, term_t t) {
  uint32_t d1, d2;

  assert(good_term(&terms, t) && is_bitvector_term(&terms, t));

  d1 = bvarith64_buffer_degree(b);
  d2 = term_degree(&terms, t);

  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);
}

bool yices_check_bvmul64_buffer(bvarith64_buffer_t *b1, bvarith64_buffer_t *b2) {
  uint32_t d1, d2;

  d1 = bvarith64_buffer_degree(b1);
  d2 = bvarith64_buffer_degree(b2);

  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);
}

bool yices_check_bvmul_term(bvarith_buffer_t *b, term_t t) {
  uint32_t d1, d2;

  assert(good_term(&terms, t) && is_bitvector_term(&terms, t));

  d1 = bvarith_buffer_degree(b);
  d2 = term_degree(&terms, t);

  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);
}

bool yices_check_bvmul_buffer(bvarith_buffer_t *b1, bvarith_buffer_t *b2) {
  uint32_t d1, d2;

  d1 = bvarith_buffer_degree(b1);
  d2 = bvarith_buffer_degree(b2);

  assert(d1 <= YICES_MAX_DEGREE && d2 <= YICES_MAX_DEGREE);

  return check_maxdegree(d1 + d2);
}




/**************
 *  PARSING   *
 *************/

/*
 * Parse s as a type expression in the Yices language.
 * Return NULL_TYPE if there's an error.
 */
EXPORTED type_t yices_parse_type(char *s) {
  parser_t *p;

  p = get_parser(s);
  return parse_yices_type(p, NULL);
}


/*
 * Parse s as a term in the Yices language.
 * Return NULL_TERM if there's an error.
 */
EXPORTED term_t yices_parse_term(char *s) {
  parser_t *p;

  p = get_parser(s);
  return parse_yices_term(p, NULL);
}



/************
 *  NAMES   *
 ***********/

/*
 * Create mapping (name -> tau) in the type table.
 * If a previous mapping (name -> tau') is in the table, then 
 * it is hidden. 
 *
 * return -1 if tau is invalid and set error report
 * return 0 otherwise.
 */
EXPORTED int32_t yices_set_type_name(type_t tau, char *name) {
  char *clone;

  if (! check_good_type(&types, tau)) {
    return -1;
  }

  // make a copy of name
  clone = clone_string(name);
  set_type_name(&types, tau, clone);

  return 0;
}


/*
 * Create mapping (name -> t) in the term table.
 * If a previous mapping (name -> t') is in the table, then 
 * it is hidden. 
 *
 * return -1 if  is invalid and set error report
 * return 0 otherwise.
 */
EXPORTED int32_t yices_set_term_name(term_t t, char *name) {
  char *clone;

  if (! check_good_term(&terms, t)) {
    return -1;
  }

  // make a copy of name
  clone = clone_string(name);
  set_term_name(&terms, t, clone);

  return 0;
}


/*
 * Remove name from the type table.
 */
EXPORTED void yices_remove_type_name(char *name) {
  remove_type_name(&types, name);
}


/*
 * Remove name from the term table.
 */
EXPORTED void yices_remove_term_name(char *name) {
  remove_term_name(&terms, name);
}


/*
 * Get type of the given name or return NULL_TYPE (-1)
 */
EXPORTED type_t yices_get_type_by_name(char *name) {
  return get_type_by_name(&types, name);
}


/*
 * Get term of the given name or return NULL_TERM
 */
EXPORTED term_t yices_get_term_by_name(char *name) {
  return get_term_by_name(&terms, name);
}


/*
 * Remove the name of type tau (if any)
 * Return -1 if tau is not a valid type and set the error code.
 * Return 0 otherwise.
 */
EXPORTED int32_t yices_clear_type_name(type_t tau) {
  if (! check_good_type(&types, tau)) {
    return -1;
  }

  clear_type_name(&types, tau);
  return 0;
}


/*
 * Remove the name of term t (if any)
 *
 * Return -1 if t is not a valid term (and set the error code)
 * Return 0 otherwise.
 */
EXPORTED int32_t yices_clear_term_name(term_t t) {
  if (! check_good_term(&terms, t)) {
    return -1;
  }

  clear_term_name(&terms, t);
  return 0;
}



