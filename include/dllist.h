#ifndef _H_DLLIST_
#define _H_DLLIST_

typedef struct dllist dllist_t;
typedef struct dllist_element dllist_element_t;

typedef enum iteration_result {
    ITERATION_CONTINUE,
    ITERATION_BREAK
} iteration_result_e;

typedef void (*free_function_t)(void*);
typedef int (*dllist_find_predicate_t)(void *value, void *args);
typedef iteration_result_e (*dllist_iterator_callback_t)(dllist_element_t *element, void *value);
typedef iteration_result_e (*dllist_iterator_with_args_callback_t)(dllist_element_t *element, void *value, void *args);

dllist_t *dllist_create(free_function_t free_value);
int dllist_push_back(dllist_t *, void *value);
void dllist_iterate_with_args(dllist_t *, dllist_iterator_with_args_callback_t cb, void *args);
dllist_element_t *dllist_find_by_value(dllist_t *, void *value);
dllist_element_t *dllist_find_by_predicate(dllist_t *, dllist_find_predicate_t predicate, void *args);
void dllist_remove(dllist_t *, dllist_element_t *element);
void dllist_iterate(dllist_t *, dllist_iterator_callback_t cb);
int dllist_is_empty(dllist_t *);
void dllist_destroy(dllist_t *);

#endif
