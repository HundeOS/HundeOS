#ifndef __COMMON_TYPES_LINKED_LIST_H
#define __COMMON_TYPES_LINKED_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bstring.h"
#include "optional.h"
#include "result.h"

#ifndef __COMMON_TYPES_COLLECTION_CODE_DEFINED
#define __COMMON_TYPES_COLLECTION_CODE_DEFINED
typedef uint8_t CollectionCode;
ResultDef(CollectionCode);
#endif

#ifndef __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
#define __COMMON_TYPES_EQUAL_FUNCTION_DEFINED
typedef bool(* EqualFunction)(void * a, void * b);
#endif

#ifndef LINKED_LIST_ALLOC
#error "LINKED_LIST_ALLOC(size) must be defined before including common/types/linked_list.h"
#endif

#ifndef LINKED_LIST_FREE
#error "LINKED_LIST_FREE(ptr) must be defined before including common/types/linked_list.h"
#endif

#ifndef LINKED_LIST_ERROR
#define LINKED_LIST_ERROR(message) BString__new(message)
#endif

typedef int ListIndex;

OptionalDef(ListIndex);

typedef enum LinkedListError
{
    LINKED_LIST_ALLOCATION_FAILED = -1,
    LINKED_LIST_EMPTY = -2,
    LINKED_LIST_OUT_OF_BOUNDS = -3,
    LINKED_LIST_ITEM_NOT_FOUND = -4,
} LinkedListError;

#define LINKED_LIST_NEW() { \
    .len = 0, \
    .head = NULL, \
    .tail = NULL, \
}

#define LLOptional(alias) _Optional(alias##__optional)
#define LLPtrOptional(alias) _Optional(alias##__type_ptr__optional)

#define LLNone(alias) None(alias##__optional)
#define LLPtrNone(alias) None(alias##__type_ptr__optional)

#define LLSome(alias, value) Some(alias##__optional, value)
#define LLPtrSome(alias, value) Some(alias##__type_ptr__optional, value)

#define LLFindResult(alias) alias##__FindResult
#define LLFindResultCreate(alias, idx, val, found_node) \
    (LLFindResult(alias)) { .index = idx, .value = val, .node = found_node, } \

#define LLFindResultNone(alias) None(alias##__FindResult)
#define LLFindResultSome(alias, res) Some(alias##__FindResult, res)

#define LLFindResultOptional(alias) _Optional(alias##__FindResult)

#define for_each_ll(alias, list, item, ...) \
    for (alias##__NodePtr item##__node = (list).head; item##__node != NULL; item##__node = item##__node->next) \
    { \
        alias##__type item = item##__node->value; \
        __VA_ARGS__ \
    }

#define for_each_ll_ptr(alias, list, item, ...) \
    for (alias##__NodePtr item##__node = (list)->head; item##__node != NULL; item##__node = item##__node->next) \
    { \
        alias##__type item = item##__node->value; \
        __VA_ARGS__ \
    }

#define LinkedListConstructorPreDefine(alias_name) \
    alias_name alias_name##__new(); \
    alias_name * alias_name##__heap_new(); \

#define LinkedListConstructorDefine(alias_name) \
    alias_name alias_name##__new() \
    { \
        return (alias_name) LINKED_LIST_NEW(); \
    } \
    \
    alias_name * alias_name##__heap_new() \
    { \
        alias_name * list = (alias_name *) LINKED_LIST_ALLOC(sizeof(alias_name)); \
        if (list == NULL) \
        { \
            return NULL; \
        } \
        *list = alias_name##__new(); \
        return list; \
    } \

#define LinkedListLenPreDefine(alias_name) size_t alias_name##__len(alias_name * self);

#define LinkedListLenDefine(alias_name) \
    size_t alias_name##__len(alias_name * self) \
    { \
        return self->len; \
    } \

#define LinkedListPushPreDefine(alias_name) \
    Result(CollectionCode) alias_name##__push(alias_name * self, alias_name##__type value); \

#define LinkedListPushDefine(alias_name) \
    Result(CollectionCode) alias_name##__push(alias_name * self, alias_name##__type value) \
    { \
        alias_name##__NodePtr node = (alias_name##__NodePtr) LINKED_LIST_ALLOC(sizeof(alias_name##__Node)); \
        if (node == NULL) \
        { \
            return Error(CollectionCode, LINKED_LIST_ERROR("linked list allocation failed"), LINKED_LIST_ALLOCATION_FAILED); \
        } \
        *node = (alias_name##__Node) { \
            .value = value, \
            .next = NULL, \
            .prev = self->tail, \
        }; \
        if (self->tail != NULL) \
        { \
            self->tail->next = node; \
        } \
        else \
        { \
            self->head = node; \
        } \
        self->tail = node; \
        self->len++; \
        return Success(CollectionCode, self->len - 1); \
    } \

#define LinkedListAtPreDefine(alias_name) \
    LLOptional(alias_name) alias_name##__at(alias_name * self, int64_t index); \
    LLPtrOptional(alias_name) alias_name##__ptr_at(alias_name * self, int64_t index); \

#define LinkedListAtDefine(alias_name) \
    static alias_name##__NodePtr alias_name##__node_at(alias_name * self, int64_t index) \
    { \
        if (index < 0) \
        { \
            if ((uint64_t)(-index) <= self->len) \
            { \
                index += self->len; \
            } \
            else \
            { \
                return NULL; \
            } \
        } \
        if ((uint64_t) index >= self->len) \
        { \
            return NULL; \
        } \
        alias_name##__NodePtr node = self->head; \
        for (int64_t i = 0; i < index; i++) \
        { \
            node = node->next; \
        } \
        return node; \
    } \
    \
    LLOptional(alias_name) alias_name##__at(alias_name * self, int64_t index) \
    { \
        alias_name##__NodePtr node = alias_name##__node_at(self, index); \
        if (node == NULL) \
        { \
            return LLNone(alias_name); \
        } \
        return LLSome(alias_name, node->value); \
    } \
    \
    LLPtrOptional(alias_name) alias_name##__ptr_at(alias_name * self, int64_t index) \
    { \
        alias_name##__NodePtr node = alias_name##__node_at(self, index); \
        if (node == NULL) \
        { \
            return LLPtrNone(alias_name); \
        } \
        return LLPtrSome(alias_name, &node->value); \
    } \

#define LinkedListFindPreDefine(alias_name) \
    typedef bool(* alias_name##__predicate)(alias_name##__type_ptr value, void * context); \
    LLFindResultOptional(alias_name) alias_name##__find( \
        alias_name * self, alias_name##__predicate predicate, void * context \
    ); \
    Optional(ListIndex) alias_name##__find_item(alias_name * self, alias_name##__type value); \

#define LinkedListFindDefine(alias_name) \
    LLFindResultOptional(alias_name) alias_name##__find( \
        alias_name * self, alias_name##__predicate predicate, void * context \
    ) \
    { \
        ListIndex index = 0; \
        alias_name##__NodePtr node = self->head; \
        while (node != NULL) \
        { \
            if (predicate(&node->value, context)) \
            { \
                LLFindResult(alias_name) res = LLFindResultCreate(alias_name, index, &node->value, node); \
                return LLFindResultSome(alias_name, res); \
            } \
            node = node->next; \
            index++; \
        } \
        return LLFindResultNone(alias_name); \
    } \
    \
    Optional(ListIndex) alias_name##__find_item(alias_name * self, alias_name##__type value) \
    { \
        EqualFunction equals = alias_name##__default__equals; \
        ListIndex index = 0; \
        alias_name##__NodePtr node = self->head; \
        while (node != NULL) \
        { \
            if (equals(&node->value, &value)) \
            { \
                return Some(ListIndex, index); \
            } \
            node = node->next; \
            index++; \
        } \
        return None(ListIndex); \
    } \

#define LinkedListRemovePreDefine(alias_name) \
    LLOptional(alias_name) alias_name##__remove_at(alias_name * self, int64_t index); \
    Result(CollectionCode) alias_name##__remove(alias_name * self, alias_name##__type value); \
    Result(CollectionCode) alias_name##__dispose(alias_name * self); \

#define LinkedListRemoveDefine(alias_name) \
    static LLOptional(alias_name) alias_name##__remove_node(alias_name * self, alias_name##__NodePtr node) \
    { \
        if (node == NULL) \
        { \
            return LLNone(alias_name); \
        } \
        if (node->prev != NULL) \
        { \
            node->prev->next = node->next; \
        } \
        else \
        { \
            self->head = node->next; \
        } \
        if (node->next != NULL) \
        { \
            node->next->prev = node->prev; \
        } \
        else \
        { \
            self->tail = node->prev; \
        } \
        alias_name##__type value = node->value; \
        self->len--; \
        LINKED_LIST_FREE(node); \
        return LLSome(alias_name, value); \
    } \
    \
    LLOptional(alias_name) alias_name##__remove_at(alias_name * self, int64_t index) \
    { \
        return alias_name##__remove_node(self, alias_name##__node_at(self, index)); \
    } \
    \
    Result(CollectionCode) alias_name##__remove(alias_name * self, alias_name##__type value) \
    { \
        EqualFunction equals = alias_name##__default__equals; \
        alias_name##__NodePtr node = self->head; \
        while (node != NULL) \
        { \
            if (equals(&node->value, &value)) \
            { \
                alias_name##__remove_node(self, node); \
                return Success(CollectionCode, 0); \
            } \
            node = node->next; \
        } \
        return Error(CollectionCode, LINKED_LIST_ERROR("item not found"), LINKED_LIST_ITEM_NOT_FOUND); \
    } \
    \
    Result(CollectionCode) alias_name##__dispose(alias_name * self) \
    { \
        while (self->len > 0) \
        { \
            alias_name##__remove_at(self, 0); \
        } \
        return Success(CollectionCode, 0); \
    } \

#define LinkedListDefine(type, alias_name, equals) \
    AliasOptional(type, alias_name##__optional) \
    \
    typedef struct alias_name##__Node alias_name##__Node; \
    typedef alias_name##__Node * alias_name##__NodePtr; \
    \
    struct alias_name##__Node \
    { \
        type value; \
        alias_name##__NodePtr next; \
        alias_name##__NodePtr prev; \
    }; \
    \
    typedef struct alias_name##__FindResult \
    { \
        type * value; \
        alias_name##__NodePtr node; \
        ListIndex index; \
    } alias_name##__FindResult; \
    \
    OptionalDef(alias_name##__FindResult); \
    \
    static const EqualFunction alias_name##__default__equals = equals; \
    \
    typedef type alias_name##__type; \
    typedef type * alias_name##__type_ptr; \
    \
    OptionalDef(alias_name##__type_ptr) \
    AliasOptional(alias_name##__type_ptr, alias_name##__type_ptr__optional) \
    \
    typedef struct alias_name \
    { \
        uint64_t len; \
        alias_name##__NodePtr head; \
        alias_name##__NodePtr tail; \
    } alias_name; \
    \
    LinkedListConstructorPreDefine(alias_name) \
    LinkedListLenPreDefine(alias_name) \
    LinkedListPushPreDefine(alias_name) \
    LinkedListAtPreDefine(alias_name) \
    LinkedListFindPreDefine(alias_name) \
    LinkedListRemovePreDefine(alias_name) \

#define LinkedListBodyDefine(alias_name) \
    LinkedListConstructorDefine(alias_name) \
    LinkedListLenDefine(alias_name) \
    LinkedListPushDefine(alias_name) \
    LinkedListAtDefine(alias_name) \
    LinkedListFindDefine(alias_name) \
    LinkedListRemoveDefine(alias_name) \

#endif
