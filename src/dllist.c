#include "dllist.h"
#include <stdlib.h>

struct dllist_element {
    void *value;
    dllist_element_t *next, *previous;
};

struct dllist {
    dllist_element_t *head, *tail;
    free_function_t free_value;
};

dllist_t *dllist_create(free_function_t free_value) {
    dllist_t *list = malloc(sizeof(dllist_t));
    if (list == NULL) {
        return NULL;
    }
    list->head = NULL;
    list->tail = NULL;
    list->free_value = free_value;
    return list;
}

int dllist_push_back(dllist_t *list, void *value) {
    dllist_element_t *new_node = malloc(sizeof(dllist_element_t));
    if (new_node == NULL) {
        return -1;
    }
    new_node->value = value;
    new_node->next = NULL;
    new_node->previous = list->tail;
    if (list->tail) {
        list->tail->next = new_node;
    }
    list->tail = new_node;
    if (list->head == NULL) {
        // This is the first node, so it is also the head
        list->head = new_node;
    }
    return 0;
}

void dllist_iterate_with_args(dllist_t *list, dllist_iterator_with_args_callback_t cb, void *args) {
    for (dllist_element_t *cur = list->head; cur != NULL; cur = cur->next) {
        if (cb(cur, cur->value, args) != ITERATION_CONTINUE) break;
    }
}

void dllist_iterate(dllist_t *list, dllist_iterator_callback_t cb) {
    for (dllist_element_t *cur = list->head; cur != NULL; cur = cur->next) {
        if (cb(cur, cur->value) != ITERATION_CONTINUE) break;
    }
}

dllist_element_t *dllist_find_by_value(dllist_t *list, void *value) {
    for (dllist_element_t *cur = list->head; cur != NULL; cur = cur->next) {
        if (cur->value == value) return cur;
    }
    return NULL;
}

dllist_element_t *dllist_find_by_predicate(dllist_t *list, dllist_find_predicate_t predicate, void *args) {
    for (dllist_element_t *cur = list->head; cur != NULL; cur = cur->next) {
        if (predicate(cur->value, args)) return cur;
    }
    return NULL;
}

void dllist_remove(dllist_t *list, dllist_element_t *element) {
    if (list->head == element) list->head = element->next;
    if (list->tail == element) list->tail = element->previous;
    if (element->previous) element->previous->next = element->next;
    if (element->next) element->next->previous = element->previous;
    if (list->free_value) list->free_value(element->value);
    free(element);
}

int dllist_is_empty(dllist_t *list) {
    return list->head == NULL;
}

void dllist_destroy(dllist_t *list) {
    if (list == NULL) return;

    dllist_element_t *next = NULL;
    for (dllist_element_t *cur = list->head; cur != NULL; cur = next) {
        next = cur->next;
        if (list->free_value) {
            list->free_value(cur->value);
        }
        free(cur);
    }

    free(list);
}
