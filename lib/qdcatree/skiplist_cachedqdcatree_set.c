#include "skiplist_cachedqdcatree_set.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "misc/thread_includes.h"
#include "misc/padded_types.h"
#include "locks/locks.h"
#include "feseq_skiplist.h"
#include "gc/ptst.h"

static int gc_id;

extern __thread ptst_t *ptst;

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
/*
 * ========================
 * Internal data structures
 * ========================
 */

#define DEBUG_PRINT(x)
//printf x
#define CSLCATREE_MAX_CONTENTION_STATISTICS 1000
#define CSLCATREE_MIN_CONTENTION_STATISTICS -1000
#define CSLCATREE_LOCK_SUCCESS_STATS_CONTRIB -1
#define CSLCATREE_LOCK_FAILURE_STATS_CONTRIB 250
#define CSLCATREE_STACK_NEED 50
#define CSLCATREE_FREE_LIST_SIZE 4096

#ifndef LL_LOCK_TYPE
#define   LL_LOCK_TYPE QDLock
#endif

typedef struct {
    LL_LOCK_TYPE lock;
    int statistics;
    //int validation;
} CATreeLock;

typedef struct {
    CATreeLock lock;
    Skiplist * root;
    bool valid;
} CATreeBaseNode;

typedef struct {
    unsigned long key;
    void * left;
    void * right;
    bool valid;
    TATASLock lock;
} CATreeRouteNode;

typedef union {
    CATreeRouteNode route;
    CATreeBaseNode base;
} CATreeBaseOrRouteNodeContainer;

typedef struct {
    bool isBaseNode;
    CATreeBaseOrRouteNodeContainer baseOrRoute;
} CATreeBaseOrRouteNode;

typedef struct csl_catree_set {
    //DRMCSLock globalLock;
    char pad1[CACHE_LINE_SIZE*2];    
    CATreeBaseOrRouteNode * root;
    char pad2[CACHE_LINE_SIZE*2];
    QDLock delete_min_lock;
    TATASLock freeingFreeListLock;
    volatile atomic_intptr_t freeList[CSLCATREE_FREE_LIST_SIZE];
    LLPaddedULong freeListIndexCounter;
} CSLCATreeSet;

#define SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE 64
typedef struct {
    char pad1[128];
    CSLCATreeSet * set;
    CATreeBaseOrRouteNode * current_help_node;
    CATreeBaseOrRouteNode * last_locked_node;
    CATreeBaseOrRouteNode * last_locked_node_parent;
    int flush_stack_pos;
    CATreeBaseOrRouteNode * flush_stack[SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE];
    char pad2[128 - (4*sizeof(CATreeBaseOrRouteNode *))];
} chelp_info_type;

_Alignas(CACHE_LINE_SIZE)
__thread chelp_info_type chelp_info = {.flush_stack_pos = 0};
/*
 * ==================
 * Internal functions
 * ==================
 */

static inline
void catree_lock_init(CATreeLock * lock){
    lock->statistics = 0;
    //lock->validation = 0;
    LL_initialize(&lock->lock);
}

static inline
void catree_lock(CATreeLock * lock){
    if(LL_try_lock(&lock->lock)){
        /* lock->validation++; */
        /* assert(lock->validation == 1); */
        lock->statistics += CSLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
        return;
    }
    LL_lock(&lock->lock);
    /* lock->validation++; */
    /* assert(lock->validation == 1); */
    lock->statistics += CSLCATREE_LOCK_FAILURE_STATS_CONTRIB;
}

static inline
bool catree_lock_is_contended(CATreeLock * lock){
    if(LL_try_lock(&lock->lock)){
        /* lock->validation++; */
        /* assert(lock->validation == 1); */
        return false;
    }
    LL_lock(&lock->lock);
    /* lock->validation++; */
    /* assert(lock->validation == 1); */
    return true;
}


static inline
bool catree_trylock(CATreeLock * lock){
    if(LL_try_lock(&lock->lock)){
        /* lock->validation++; */
        /* assert(lock->validation == 1); */
        return true;
    }else{
        return false;
    }
}


static inline
void catree_unlock(CATreeLock * lock){
    /* lock->validation--; */
    /* if(lock->validation != 0){ */
    /*     printf("THE LOCK VAL IS %d\n", lock->validation); */
    /* } */
    /* assert(lock->validation == 0); */
    LL_unlock(&lock->lock);
}

static inline
CATreeBaseOrRouteNode * allocate_base_node(Skiplist * root){
    CATreeBaseOrRouteNode * newBase = gc_alloc(ptst, gc_id);
    newBase->isBaseNode = true;
    catree_lock_init(&newBase->baseOrRoute.base.lock);
    newBase->baseOrRoute.base.root = root;
    newBase->baseOrRoute.base.valid = true;   
    return newBase;
}

static inline
CATreeBaseOrRouteNode * allocate_route_node(unsigned long key,
                                            CATreeBaseOrRouteNode * newBaseLeft,
                                            CATreeBaseOrRouteNode * newBaseRight){
    CATreeBaseOrRouteNode * newRoute = gc_alloc(ptst, gc_id);
    newRoute->isBaseNode = false;
    LL_initialize(&newRoute->baseOrRoute.route.lock);
    newRoute->baseOrRoute.route.valid = true;
    newRoute->baseOrRoute.route.left = newBaseLeft;
    newRoute->baseOrRoute.route.right = newBaseRight;
    newRoute->baseOrRoute.route.key = key;
    return newRoute;
}




/* static void enqueue_in_free_list(CSLCATreeSet * set, CATreeBaseOrRouteNode * toEnqueue){ */
/*     while(true){ */
/*         unsigned long slot = atomic_fetch_add(&set->freeListIndexCounter.value, 1); */
/*         if(slot == CSLCATREE_FREE_LIST_SIZE){ */
/*             // We are responsible for doing the freeing */
/*             // Ensure that no one still is accessing old data */
/*             CATreeBaseOrRouteNode * elementsToFree[CSLCATREE_FREE_LIST_SIZE]; */
/*             drmcs_runlock(&set->globalLock); */
/*             drmcs_lock(&set->globalLock); */
/*             drmcs_unlock(&set->globalLock); */
/*             drmcs_rlock(&set->globalLock); */
/*             for(int i = 0; i < CSLCATREE_FREE_LIST_SIZE; i++){ */
/*                 CATreeBaseOrRouteNode * elementToFree; */
/*                 while((elementToFree = (CATreeBaseOrRouteNode *)atomic_load(&set->freeList[i])) == NULL){ */
/*                     thread_yield();//This should not happen */
/*                 } */
/*                 elementsToFree[i] = elementToFree; */
/*                 atomic_store_explicit(&set->freeList[i], (intptr_t)NULL, memory_order_relaxed); */
/*             } */
/*             //Enqueue our element */
/*             atomic_store_explicit(&set->freeList[0], (intptr_t)toEnqueue, memory_order_relaxed); */
/*             //Let other threads start to enqueue */
/*             atomic_store(&set->freeListIndexCounter.value, 1); */
/*             for(int i = 0; i < CSLCATREE_FREE_LIST_SIZE; i++){ */
/*                 CATreeBaseOrRouteNode * elementToFree = elementsToFree[i]; */
/*                 if(elementToFree->isBaseNode){ */
/*                     LL_destroy(&elementToFree->baseOrRoute.base.lock.lock); //Free deleted base */
/*                     CSLCATREE_FREE(elementToFree); */
/*                 }else{ */
/*                     LL_destroy(&elementToFree->baseOrRoute.route.lock); //Free deleted route */
/*                     CSLCATREE_FREE(elementToFree);    */
/*                 } */

/*             } */
/*             return; */
/*         }else if (slot > CSLCATREE_FREE_LIST_SIZE){ */
/*             //We need to wait then retry */
/*             drmcs_runlock(&set->globalLock); */
/*             while(atomic_load(&set->freeListIndexCounter.value) > CSLCATREE_FREE_LIST_SIZE){ */
/*                 thread_yield(); */
/*             } */
/*             drmcs_rlock(&set->globalLock); */
/*         } else { */
/*             // Just write our node to our slot */
/*             atomic_store_explicit(&set->freeList[slot], (intptr_t)toEnqueue, memory_order_relaxed); */
/*             return; */
/*         } */
/*     } */
/* } */


static inline
void plain_cslcatree_set_init(CSLCATreeSet * set){
    //    drmcs_initialize(&set->globalLock);
    gc_id = gc_add_allocator(sizeof(CATreeBaseOrRouteNode));
    critical_enter();
    set->root = allocate_base_node(new_skiplist());
    critical_exit();
}


static inline
void cslcatree_set_print_helper_helper(Skiplist * node){
    printf("SL");
}

static inline
void cslcatree_set_print_helper(CATreeBaseOrRouteNode * root){
    if(root->isBaseNode){
        printf("B(");
        cslcatree_set_print_helper_helper(root->baseOrRoute.base.root);
        printf(")");
    }else{
        CATreeRouteNode * route = &root->baseOrRoute.route;
        printf("R(");
        printf("%lu", route->key);
        printf(",");
        printf("%p", root);
        printf(",");
        cslcatree_set_print_helper(route->left);
        printf(",");
        cslcatree_set_print_helper(route->right);
        printf(")");
    }
}

static inline
void cslcatree_set_print_helper_helper_dot(Skiplist * node){
    printf("SL\n");
}

static inline
void cslcatree_set_print_helper_dot(CATreeBaseOrRouteNode * root){
    if(root->isBaseNode){
        printf("B(\n");
        printf("digraph G{\n");
        printf("  graph [ordering=\"out\"];\n");
        cslcatree_set_print_helper_helper_dot(root->baseOrRoute.base.root);
        printf("}\n");
        printf("\n)");
    }else{
        CATreeRouteNode * route = &root->baseOrRoute.route;
        printf("R(");
        printf("%lu", route->key);
        printf(",");
        cslcatree_set_print_helper_dot(route->left);
        printf(",");
        cslcatree_set_print_helper_dot(route->right);
        printf(")");
    }
}


static inline
void cslcatree_set_print(void * setParam){
    CSLCATreeSet * set = (CSLCATreeSet *)setParam;
    cslcatree_set_print_helper(set->root);
    printf("\n");
    /* printf("\n\nDOT:\n\n"); */
    /* cslcatree_set_print_helper_dot(set->root); */
    /* printf("\n\n"); */
}

static inline
void cslcatree_set_free_helper(CATreeBaseOrRouteNode * root){
    if(root->isBaseNode){
        skiplist_delete(root->baseOrRoute.base.root);
        CSLCATREE_FREE(root);
    }else{
        CATreeRouteNode * route = &root->baseOrRoute.route;
        cslcatree_set_free_helper(route->left);
        cslcatree_set_free_helper(route->right);
        LL_destroy(&route->lock);
        CSLCATREE_FREE(root);
    }
}

static inline
void cslcatree_set_destroy(void * setParam){
    CSLCATreeSet * set = (CSLCATreeSet *)setParam;
    for(int i = 0; i < CSLCATREE_FREE_LIST_SIZE; i++){
        CATreeBaseOrRouteNode * elementToFree = (CATreeBaseOrRouteNode *)atomic_load(&set->freeList[i]);
        if(elementToFree != NULL){
            if(elementToFree->isBaseNode){
                LL_destroy(&elementToFree->baseOrRoute.base.lock.lock); //Free deleted base
                CSLCATREE_FREE(elementToFree);
            }else{
                LL_destroy(&elementToFree->baseOrRoute.route.lock); //Free deleted route
                CSLCATREE_FREE(elementToFree);   
            }
        }
    }
    cslcatree_set_free_helper(set->root);
}

static inline
bool contention_reduce_split(CATreeBaseOrRouteNode *  baseNodeContainer,
                             void ** parent,
                             CSLCATreeSet * set,
                             CATreeBaseOrRouteNode *  parentNode) {
    CATreeBaseNode * node = &baseNodeContainer->baseOrRoute.base;
    if(!skiplist_more_than_two_keys(node->root)){
        node->lock.statistics = 0;
        return false;
    }
    /* Do split */

    Skiplist * newLeft;
    Skiplist * newRight;
    
    unsigned long splitKey = skiplist_split(node->root,
                                            &newLeft,
                                            &newRight);
    
    CATreeBaseOrRouteNode * newRoute = allocate_route_node(splitKey,
                                                           allocate_base_node(newLeft),
                                                           allocate_base_node(newRight));
    //Link in new route
    *parent = newRoute;
    //Unlock threads waiting for old node
    node->valid = false;
    DEBUG_PRINT(("split unlock %p\n", baseNodeContainer));
    catree_unlock(&node->lock);
    //Done!
    //enqueue_in_free_list(set, baseNodeContainer);
    gc_free(ptst, baseNodeContainer, gc_id);
    return true;
}




static inline CATreeBaseOrRouteNode * find_leftmost_base_node(CATreeBaseOrRouteNode *  baseOrRouteParam,
                                                              CATreeBaseOrRouteNode ** parentRouteWriteBack){
    CATreeBaseOrRouteNode *  baseOrRoute = baseOrRouteParam; 
    CATreeBaseOrRouteNode *  prevNode = NULL;
    while(!baseOrRoute->isBaseNode){
        prevNode = baseOrRoute;
        baseOrRoute = baseOrRoute->baseOrRoute.route.left;
    }
    *parentRouteWriteBack = prevNode;
    return baseOrRoute;
}

static inline CATreeBaseOrRouteNode * find_rightmost_base_node(CATreeBaseOrRouteNode *  baseOrRouteParam,
                                                               CATreeBaseOrRouteNode ** parentRouteWriteBack){
    CATreeBaseOrRouteNode *  baseOrRoute = baseOrRouteParam; 
    CATreeBaseOrRouteNode *  prevNode = NULL;
    while(!baseOrRoute->isBaseNode){
        prevNode = baseOrRoute;
        baseOrRoute = baseOrRoute->baseOrRoute.route.right;
    }
    *parentRouteWriteBack = prevNode;
    return baseOrRoute;
}

static inline CATreeBaseOrRouteNode * find_parent_of(CATreeBaseOrRouteNode * container,
                                                     unsigned long key,
                                                     CSLCATreeSet * set){
    if(set->root == container){
        return NULL;
    }
    CATreeBaseOrRouteNode * currentNode = set->root;
    CATreeBaseOrRouteNode * prevNode = NULL;
    while(currentNode != container){
        unsigned long nodeKey = currentNode->baseOrRoute.route.key;
        prevNode = currentNode;
        if(key < nodeKey) {
            currentNode = currentNode->baseOrRoute.route.left;
        } else {
            currentNode = currentNode->baseOrRoute.route.right;
        }
    }
    return prevNode;
}


static inline
bool low_contention_join(CATreeBaseOrRouteNode *  baseNodeContainer,
                        CATreeBaseOrRouteNode * parentRoute,
                        CSLCATreeSet * set) {

    CATreeBaseNode * node = &baseNodeContainer->baseOrRoute.base;
    if(parentRoute == NULL){
        node->lock.statistics = 0;
        return false;
    }
    CATreeRouteNode * parent = &parentRoute->baseOrRoute.route;
    CATreeBaseNode * neighbourBase;
    bool neighbourLeft = true;
    CATreeBaseOrRouteNode * neighbourBaseContainer;
    CATreeBaseOrRouteNode * neighbourBaseParentRoute = NULL;
    if(baseNodeContainer == parent->left){
        neighbourLeft = false;
        neighbourBaseContainer = find_leftmost_base_node(parent->right, &neighbourBaseParentRoute);
    } else {
        neighbourBaseContainer = find_rightmost_base_node(parent->left, &neighbourBaseParentRoute);
    }
    if(neighbourBaseParentRoute == NULL){
        neighbourBaseParentRoute = parentRoute;
    }
    neighbourBase = &neighbourBaseContainer->baseOrRoute.base;
    if(!catree_trylock(&neighbourBase->lock)){
        node->lock.statistics = 0;
        return false; // Lets retry later
    }
    
    if(!neighbourBase->valid){
        DEBUG_PRINT(("join not valid unlock %p\n", neighbourBaseContainer));
        catree_unlock(&neighbourBase->lock);
        node->lock.statistics = 0;
        return false; // Lets retry later
    }
    assert(parent->left == baseNodeContainer  || parent->right == baseNodeContainer);
    // Ready to do the merge
    CATreeBaseOrRouteNode * newNeighbourBaseContainer = allocate_base_node(NULL);
    CATreeBaseNode * newNeighbourBase = &newNeighbourBaseContainer->baseOrRoute.base;
    if(neighbourBase->root == NULL){
        newNeighbourBase->root = node->root;
    }else if(node->root != NULL){
        CATreeBaseNode * leftBase;
        CATreeBaseNode * rightBase;
        if(neighbourLeft){
            leftBase = neighbourBase;
            rightBase = node;
        }else{
            leftBase = node;
            rightBase = neighbourBase;
        }
        newNeighbourBase->root = skiplist_join(leftBase->root,
                                               rightBase->root);
    }
    //Take out the node
    //Lock parent
    LL_lock(&parent->lock);
    //Find parent of parent and lock if posible
    CATreeBaseOrRouteNode * parentOfParentRoute = NULL;
    do{
        if(parentOfParentRoute != NULL){
            LL_unlock(&parentOfParentRoute->baseOrRoute.route.lock);
        }
        parentOfParentRoute = 
            find_parent_of(parentRoute,
                           parent->key,
                           set);
        if(parentOfParentRoute == NULL){
            //Parent of parent is root, we are safe becaue it can't be deleted
            break;
        }
        LL_lock(&parentOfParentRoute->baseOrRoute.route.lock);
    }while(!parentOfParentRoute->baseOrRoute.route.valid);
    //Parent and parent of parent is safe
    //We can now unlink the parent route 
    CATreeBaseOrRouteNode ** parentInLinkPtrPtr = NULL;
    if(parentOfParentRoute == NULL){
        parentInLinkPtrPtr = &set->root;
    }else if(parentOfParentRoute->baseOrRoute.route.left == parentRoute){
        parentInLinkPtrPtr = (CATreeBaseOrRouteNode **)&parentOfParentRoute->baseOrRoute.route.left;
    }else{
        parentInLinkPtrPtr = (CATreeBaseOrRouteNode **)&parentOfParentRoute->baseOrRoute.route.right;
    }
    if(neighbourLeft){
        *parentInLinkPtrPtr = parent->left;
    }else{
        *parentInLinkPtrPtr = parent->right;
    }
    //Unlink should have happened YEY
    //Unlock the locks
    parent->valid = false;
    LL_unlock(&parent->lock);
    if(parentOfParentRoute != NULL){
        LL_unlock(&parentOfParentRoute->baseOrRoute.route.lock);
    }
    //Link in new neighbour base
    if(*parentInLinkPtrPtr == neighbourBaseContainer){
        *parentInLinkPtrPtr = newNeighbourBaseContainer;
    }else if(neighbourBaseParentRoute->baseOrRoute.route.left == neighbourBaseContainer){
        neighbourBaseParentRoute->baseOrRoute.route.left = newNeighbourBaseContainer;
    }else{
        neighbourBaseParentRoute->baseOrRoute.route.right = newNeighbourBaseContainer;
    }
    neighbourBase->valid = false;
    DEBUG_PRINT(("join unlock nb %p\n", neighbourBaseContainer));
    catree_unlock(&neighbourBase->lock);
    node->valid = false;
    DEBUG_PRINT(("join unlock node %p\n", baseNodeContainer));
    catree_unlock(&node->lock);
    //TODO SKIP FREEING!
    gc_free(ptst, baseNodeContainer, gc_id);
    gc_free(ptst, neighbourBaseContainer, gc_id);
    gc_free(ptst, parentRoute, gc_id);
    //enqueue_in_free_list(set, baseNodeContainer);
    //enqueue_in_free_list(set, neighbourBaseContainer);
    //enqueue_in_free_list(set, parentRoute);
#ifdef CSLCATREE_COUNT_ROUTE_NODES
    int count = atomic_fetch_sub(&set->nrOfRouteNodes, 1);
    printf("%d\n", count);
#endif
    return true;
}


/*This function assumes that the parent is not the root and that the base node is the left child
 It returns the new locked base node*/
static inline
CATreeBaseOrRouteNode *
low_contention_join_force_left_child(CATreeBaseOrRouteNode *  baseNodeContainer,
                                     CATreeBaseOrRouteNode * parentRoute,
                                     CSLCATreeSet * set,
                                     CATreeBaseOrRouteNode ** parentRouteWriteback) {
    //assert(0);
    CATreeBaseNode * node;
 retry_from_here:
    node = &baseNodeContainer->baseOrRoute.base;

    CATreeRouteNode * parent = &parentRoute->baseOrRoute.route;
    CATreeBaseNode * neighbourBase;
    //bool neighbourLeft = true;
    CATreeBaseOrRouteNode * neighbourBaseContainer;
    CATreeBaseOrRouteNode * neighbourBaseParentRoute = NULL;
    /* if(baseNodeContainer == parent->left){ */
    /*     neighbourLeft = false; */
    neighbourBaseContainer = find_leftmost_base_node(parent->right, &neighbourBaseParentRoute);
    /* } else { */
    /*     neighbourBaseContainer = find_rightmost_base_node(parent->left, &neighbourBaseParentRoute); */
    /* } */
    if(neighbourBaseParentRoute == NULL){
        neighbourBaseParentRoute = parentRoute;
    }
    neighbourBase = &neighbourBaseContainer->baseOrRoute.base;
    
    catree_lock(&neighbourBase->lock);
    DEBUG_PRINT(("force locked neighbour %p\n", neighbourBaseContainer));
    if(!neighbourBase->valid){
        DEBUG_PRINT(("force not valid unlock %p\n", neighbourBaseContainer));
        catree_unlock(&neighbourBase->lock);
        goto retry_from_here;
    }
    // Ready to do the merge
    CATreeBaseOrRouteNode * newNeighbourBaseContainer = allocate_base_node(NULL);
    catree_lock(&newNeighbourBaseContainer->baseOrRoute.base.lock);
    CATreeBaseNode * newNeighbourBase = &newNeighbourBaseContainer->baseOrRoute.base;
    if(SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE > chelp_info.flush_stack_pos){
      //printf("L STACK POS %d\n", chelp_info.flush_stack_pos);
      LL_open_delegation_queue(&newNeighbourBase->lock.lock);
      chelp_info.flush_stack[chelp_info.flush_stack_pos] = newNeighbourBaseContainer;
    }
    chelp_info.flush_stack_pos++;
    /* if(neighbourBase->root == NULL){ */
    /*     newNeighbourBase->root = node->root; */
    /* }else if(node->root != NULL){ */
        CATreeBaseNode * leftBase;
        CATreeBaseNode * rightBase;
        /* if(neighbourLeft){ */
        /*     leftBase = neighbourBase; */
        /*     rightBase = node; */
        /* }else{ */
            leftBase = node;
            rightBase = neighbourBase;
        /* } */
        newNeighbourBase->root = skiplist_join(leftBase->root,
                                               rightBase->root);
    /* } */
    //Take out the node
    //Lock parent
    LL_lock(&parent->lock);
    //Find parent of parent and lock if posible
    CATreeBaseOrRouteNode * parentOfParentRoute = NULL;
    do{
        if(parentOfParentRoute != NULL){
            LL_unlock(&parentOfParentRoute->baseOrRoute.route.lock);
        }
        parentOfParentRoute = 
            find_parent_of(parentRoute,
                           parent->key,
                           set);
        if(parentOfParentRoute == NULL){
            //Parent of parent is root, we are safe becaue it can't be deleted
            break;
        }
        LL_lock(&parentOfParentRoute->baseOrRoute.route.lock);
    }while(!parentOfParentRoute->baseOrRoute.route.valid);
    //Parent and parent of parent is safe
    //We can now unlink the parent route 
    CATreeBaseOrRouteNode ** parentInLinkPtrPtr = NULL;
    if(parentOfParentRoute == NULL){
        parentInLinkPtrPtr = &set->root;
    }else if(parentOfParentRoute->baseOrRoute.route.left == parentRoute){
        parentInLinkPtrPtr = (CATreeBaseOrRouteNode **)&parentOfParentRoute->baseOrRoute.route.left;
    }else{
        parentInLinkPtrPtr = (CATreeBaseOrRouteNode **)&parentOfParentRoute->baseOrRoute.route.right;
    }
    /* if(neighbourLeft){ */
    /*     *parentInLinkPtrPtr = parent->left; */
    /* }else{ */
        *parentInLinkPtrPtr = parent->right;
    /* } */
    //Unlink should have happened YEY
    //Unlock the locks
    parent->valid = false;
    LL_unlock(&parent->lock);
    if(parentOfParentRoute != NULL){
        LL_unlock(&parentOfParentRoute->baseOrRoute.route.lock);
    }
    //Link in new neighbour base which should be locked first:
                DEBUG_PRINT(("FORCE LOCK NEW NE base %p\n", newNeighbourBaseContainer));
    if(*parentInLinkPtrPtr == neighbourBaseContainer){
        *parentInLinkPtrPtr = newNeighbourBaseContainer;
    }else if(neighbourBaseParentRoute->baseOrRoute.route.left == neighbourBaseContainer){
        neighbourBaseParentRoute->baseOrRoute.route.left = newNeighbourBaseContainer;
    }else{
        neighbourBaseParentRoute->baseOrRoute.route.right = newNeighbourBaseContainer;
    }
    neighbourBase->valid = false;
    catree_unlock(&neighbourBase->lock);
    node->valid = false;
    if(SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE < chelp_info.flush_stack_pos){
        DEBUG_PRINT(("force unlock node %p\n", baseNodeContainer));
        catree_unlock(&node->lock);
    }
    gc_free(ptst, baseNodeContainer, gc_id);
    gc_free(ptst, neighbourBaseContainer, gc_id);
    gc_free(ptst, parentRoute, gc_id);
    //enqueue_in_free_list(set, baseNodeContainer);
    //enqueue_in_free_list(set, neighbourBaseContainer);
    //enqueue_in_free_list(set, parentRoute);
#ifdef CSLCATREE_COUNT_ROUTE_NODES
    int count = atomic_fetch_sub(&set->nrOfRouteNodes, 1);
    printf("%d\n", count);
#endif
    if(parentRoute == neighbourBaseParentRoute){
        *parentRouteWriteback = parentOfParentRoute;
    }else{
        *parentRouteWriteback = neighbourBaseParentRoute;
    }
    return newNeighbourBaseContainer;
}




static inline void adaptAndUnlock(CSLCATreeSet * set,
                                  CATreeBaseOrRouteNode * currentNode,
                                  CATreeBaseOrRouteNode * prevNode){
    CATreeBaseNode * node = &currentNode->baseOrRoute.base;
    CATreeLock * lock = &node->lock;
#ifndef NO_CA_TREE_ADAPTION
    CATreeRouteNode * currentCATreeRouteNode;
    void ** parent = NULL;
    bool high_contention = false;
    bool low_contention = false;
    if(prevNode == NULL){
        currentCATreeRouteNode = NULL;
    }else{
        currentCATreeRouteNode = &prevNode->baseOrRoute.route;
    }
    if(lock->statistics > CSLCATREE_MAX_CONTENTION_STATISTICS){
        high_contention = true;
    } else if(lock->statistics < CSLCATREE_MIN_CONTENTION_STATISTICS){
        low_contention = true;
    }
    if(high_contention || low_contention){
        if(currentCATreeRouteNode == NULL){
            parent = (void**)&set->root;
        }else if(currentCATreeRouteNode->left == currentNode){
            parent = (void**)&currentCATreeRouteNode->left;
        }else{
            parent = (void**)&currentCATreeRouteNode->right;
        }
        if(high_contention){
            if(!contention_reduce_split(currentNode,
                                        parent,
                                        set,
                                        prevNode)){
                DEBUG_PRINT(("fail split unlock %p\n", currentNode));
                catree_unlock(lock);
            }
        }else{
            if(!low_contention_join(currentNode,
                                    prevNode,
                                    set)){
                DEBUG_PRINT(("fail join unlock %p\n", currentNode));
                catree_unlock(lock);
            }
        }
        return;
    }
    DEBUG_PRINT(("no adapt unlock %p\n", currentNode));
#endif
    catree_unlock(lock);
}

typedef struct {
    unsigned int pos;          /* Current position on stack */
    unsigned int slot;         /* "Slot number" of top element or 0 if not set */
    CATreeBaseOrRouteNode** array; /* The stack */
} CATreeRoutingNodeStack;


#define REMOVE_MIN_CACHE_ARRAY_SIZE (256/8-2)
#define REMOVE_MIN_CACHE_SIZE (REMOVE_MIN_CACHE_ARRAY_SIZE/2)

typedef struct {
    char pad1[128];
    unsigned long key_value_array[REMOVE_MIN_CACHE_ARRAY_SIZE];
    unsigned long size;
    volatile atomic_ulong pos;
    char pad2[128];
} cdelete_min_write_back_mem_type;

_Alignas(128)
__thread cdelete_min_write_back_mem_type cdelete_min_write_back_mem = {.size = 0, .pos = ATOMIC_VAR_INIT(1)};

static inline
void perform_remove_min_with_lock(cdelete_min_write_back_mem_type * mem){
    while(chelp_info.last_locked_node_parent != NULL && skiplist_is_empty(chelp_info.last_locked_node->baseOrRoute.base.root)){
        //Merge!
        chelp_info.last_locked_node =
            low_contention_join_force_left_child(chelp_info.last_locked_node,
                                                 chelp_info.last_locked_node_parent,
                                                 chelp_info.set,
                                                 &chelp_info.last_locked_node_parent);
    }
    CATreeBaseNode * base_node = &chelp_info.last_locked_node->baseOrRoute.base;
    unsigned long key_value;
    unsigned long result_value = skiplist_remove_min(base_node->root, &key_value);
    unsigned long pos = 1;
    mem->key_value_array[0] = key_value;
    mem->key_value_array[1] = result_value;
    //printf("INSERT TO CACHE POS: %lu KEY: %lu\n", 0, key_value);
    while(key_value != ((unsigned long)-1) && pos < REMOVE_MIN_CACHE_SIZE){
        result_value = skiplist_remove_min(base_node->root, &key_value);
        if(key_value == ((unsigned long)-1)){
            break;
        }
        //printf("INSERT TO CACHE POS: %lu KEY: %lu\n", pos, key_value);
        mem->key_value_array[pos*2] = key_value;
        mem->key_value_array[pos*2+1] = result_value;
        pos++;
    }
    mem->size = pos;
    atomic_store_explicit(&mem->pos, 0, memory_order_release);
    //printf("CACHE SIZE %lu\n", pos);
}


static inline void delegate_perform_remove_min_with_lock(unsigned int msgSize, void * msgParam){
    cdelete_min_write_back_mem_type * mem = *((cdelete_min_write_back_mem_type **)msgParam);
    //drmcs_rlock(&set->globalLock);
    perform_remove_min_with_lock(mem);
    chelp_info.last_locked_node->baseOrRoute.base.lock.statistics += CSLCATREE_LOCK_FAILURE_STATS_CONTRIB;
}


/*
 *=================
 * Public interface
 *=================
 */


#define MAX_PUT_RELAXATION 128

typedef struct {
    unsigned long key;
    unsigned long value;
} put_buffer_elem_type;

typedef struct {
    char pad1[120];
    unsigned long current_index;
    put_buffer_elem_type buffer[MAX_PUT_RELAXATION];
    unsigned long message_preparation_puffer[MAX_PUT_RELAXATION*2];
    char pad2[128];
} put_buffer_type;

_Alignas(128)
__thread put_buffer_type put_buffer = {.current_index = 0};

static inline void delegate_perform_put_with_lock(unsigned int msgSize, void * msgParam){
    unsigned long * msg = msgParam;
    unsigned long key = msg[0];
    unsigned int nr_of_values = msgSize/sizeof(unsigned long) -1;
    for(int i = 0; i< nr_of_values; i++){
        skiplist_put(chelp_info.last_locked_node->baseOrRoute.base.root, key, msg[i + 1] );
    }
    chelp_info.last_locked_node->baseOrRoute.base.lock.statistics += CSLCATREE_LOCK_FAILURE_STATS_CONTRIB;
}

// From https://rosettacode.org/wiki/Sorting_algorithms/Insertion_sort#C
static inline void insertion_sort(put_buffer_elem_type *a, unsigned long n) {
    for(unsigned long i = 1; i < n; ++i) {
        put_buffer_elem_type tmp = a[i];
        unsigned long j = i;
        while(j > 0 && tmp.key < a[j - 1].key) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = tmp;
    }
}
static inline sort_put_buffer(){
    insertion_sort(&put_buffer.buffer, put_buffer.current_index);
}

void cslqdcatree_put_flush(CSLCATreeSet * set){
    unsigned long put_buffer_size = put_buffer.current_index;
    if(put_buffer.current_index == 0){
        //printf("put buffer size %lu\n", 0);
        return;
    }
    //printf("put buffer size %lu\n", put_buffer_size);
    sort_put_buffer();
    critical_enter();
    unsigned long current_pos = 0;
    while(current_pos < put_buffer_size){
        //Group together all elements with the same key
        int items_in_message_buffer = 1;
        unsigned long key = put_buffer.buffer[current_pos].key;
        put_buffer.message_preparation_puffer[0] = key;
        put_buffer.message_preparation_puffer[1] = put_buffer.buffer[current_pos].value;
        unsigned long current_pos_tmp = current_pos + 1;
        while(current_pos_tmp < put_buffer_size && key == put_buffer.buffer[current_pos_tmp].key){
            put_buffer.message_preparation_puffer[items_in_message_buffer+1] = put_buffer.buffer[current_pos_tmp].value;
            items_in_message_buffer = items_in_message_buffer + 1;
            current_pos_tmp = current_pos_tmp + 1; 
        }
        //Find base node
        CATreeBaseOrRouteNode * currentNode;
    insert_opt_start:
        currentNode = set->root;
        CATreeBaseOrRouteNode * prevNode = NULL;
        CATreeRouteNode * currentCATreeRouteNode = NULL;
        int pathLength = 0;
        while(!currentNode->isBaseNode){
            currentCATreeRouteNode = &currentNode->baseOrRoute.route;
            unsigned long nodeKey = currentCATreeRouteNode->key;
            prevNode = currentNode;
            if(key < nodeKey) {
                currentNode = currentCATreeRouteNode->left;
            } else {
                currentNode = currentCATreeRouteNode->right;
            }
            pathLength++;
        }

        //Lock and handle contention
        CATreeBaseNode * node = &currentNode->baseOrRoute.base;
        CATreeLock * lock = &node->lock;
        DEBUG_PRINT(("put lock %p\n", currentNode));
        //catree_lock(lock);
        bool contended;
        void * buff = LL_delegate_or_lock_keep_closed(&node->lock.lock, (1+items_in_message_buffer)*sizeof(unsigned long), &contended);
        if(buff == NULL){
            // Got the lock
            if(!node->valid){
                //Retry
                DEBUG_PRINT(("put  invalid unlock %p\n", currentNode));
                catree_unlock(lock);
                //drmcs_runlock(&set->globalLock);
                //drmcs_rlock(&set->globalLock);
                goto insert_opt_start;
            }
        }else{
            //Successfully delegated the operation.
            memcpy(buff, put_buffer.message_preparation_puffer, (1+items_in_message_buffer)*sizeof(unsigned long));
            LL_close_delegate_buffer(&node->lock.lock,
                                     buff,
                                     delegate_perform_put_with_lock);
            current_pos = current_pos_tmp;
            continue;
        }
        if(contended){
            //printf("P STACK POS %d\n", chelp_info.flush_stack_pos);
            //fflush(stdout);
            LL_open_delegation_queue(&node->lock.lock);
            chelp_info.flush_stack[chelp_info.flush_stack_pos] = currentNode;
            chelp_info.flush_stack_pos++;
        }else{
            chelp_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
        }
        chelp_info.set = set;
        chelp_info.current_help_node = currentNode;
        chelp_info.last_locked_node = currentNode;
        chelp_info.last_locked_node_parent = prevNode;
        skiplist_put(node->root, put_buffer.buffer[current_pos].key, put_buffer.buffer[current_pos].value);
        //printf("CONCRETE PUT first %lu\n", put_buffer.buffer[current_pos].key);
        current_pos = current_pos +1;
        unsigned long max_key = skiplist_max_key(node->root);
        while(current_pos < put_buffer_size && put_buffer.buffer[current_pos].key <= max_key){
            skiplist_put(node->root, put_buffer.buffer[current_pos].key, put_buffer.buffer[current_pos].value);
            //printf("CONCRETE PUT %lu\n", put_buffer.buffer[current_pos].key);
            current_pos = current_pos +1;
        }
        if(contended){
            chelp_info.last_locked_node->baseOrRoute.base.lock.statistics += CSLCATREE_LOCK_FAILURE_STATS_CONTRIB;
            int current_stack_pos = 0;
            while(current_stack_pos < SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE &&
                  current_stack_pos < chelp_info.flush_stack_pos){
                //printf("PF STACK POS %d\n", current_stack_pos);
                CATreeBaseNode * n =&chelp_info.flush_stack[current_stack_pos]->baseOrRoute.base;
                LL_flush_delegation_queue(&n->lock.lock);
                current_stack_pos++;
                if(current_stack_pos<chelp_info.flush_stack_pos){
                    DEBUG_PRINT(("not same as last node min unlock %p\n", current_node));
                    LL_unlock(&n->lock.lock);
                }
            }
        }else{
            chelp_info.last_locked_node->baseOrRoute.base.lock.statistics += CSLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
        }
        /* if(chelp_info.last_locked_node != chelp_info.current_help_node){ */
        /*     DEBUG_PRINT(("not same as last node min unlock %p\n", currentNode)); */
        /*     LL_unlock(&node->lock.lock); */
        /* } */
        chelp_info.flush_stack_pos = 0;    
        adaptAndUnlock(set,
                       chelp_info.last_locked_node,
                       chelp_info.last_locked_node_parent);
        continue;
    }
    put_buffer.current_index = 0;
    critical_exit();
}


void cslqdcatree_put(CSLCATreeSet * set,
                    unsigned long key,
                    unsigned long value){
    put_buffer.buffer[put_buffer.current_index].key = key;
    put_buffer.buffer[put_buffer.current_index].value = value;
    put_buffer.current_index++;
    //printf("put  %lu %lu\n", key, value);
    if(put_buffer.current_index == MAX_PUT_RELAXATION){
        cslqdcatree_put_flush(set);
    }
    return;


}


unsigned long cslqdcatree_remove_min(CSLCATreeSet * set, unsigned long * key_write_back){
    //printf("REMOVE MIN TO HAPPEN\n");
    cslqdcatree_put_flush(set);
    //Try to find the node in the cache first
    unsigned long pos = atomic_load_explicit(&cdelete_min_write_back_mem.pos, memory_order_relaxed);
    unsigned long cache_size = cdelete_min_write_back_mem.size;
    if(pos < cache_size){
        unsigned long key = cdelete_min_write_back_mem.key_value_array[pos*2];
        unsigned long value = cdelete_min_write_back_mem.key_value_array[pos*2+1];
        atomic_store_explicit(&cdelete_min_write_back_mem.pos, pos + 1, memory_order_relaxed);
        //printf("REMOVE FROM CACHE: %lu pos: %lu\n", key, pos);
        *key_write_back = key;
        //printf("remove (cached) key %lu\n", key);
        return value;
    }
    critical_enter();
    //Find leftmost routing node
    int retry;
    CATreeBaseOrRouteNode * current_node;
    CATreeBaseOrRouteNode * prev_node;
    CATreeRouteNode * current_route_node = NULL;
    CATreeBaseNode * base_node;
    bool contended;
    do {
        retry = 0;
        prev_node = NULL;
        current_node = ACCESS_ONCE(set->root);
        current_route_node = NULL;
        while( ! current_node->isBaseNode ){
            current_route_node = &current_node->baseOrRoute.route;
            prev_node = current_node;
            current_node = current_route_node->left;
        }
        base_node = &current_node->baseOrRoute.base;
        DEBUG_PRINT(("remove min lock %p\n", current_node));
        void * buff = LL_delegate_or_lock_keep_closed(&base_node->lock.lock, sizeof(cdelete_min_write_back_mem_type*), &contended);
        if(buff == NULL){
            // Got the lock
            if( ! base_node->valid ){
                /* Retry */
                DEBUG_PRINT(("remove min invalid unlock %p\n", current_node));
                catree_unlock(&base_node->lock);
                //drmcs_runlock(&set->globalLock);
                //drmcs_rlock(&set->globalLock);
                retry = 1;
            }
        }else{
            //Successfully delegated the operation.
            //Write it to the queue
            *(cdelete_min_write_back_mem_type **)buff = &cdelete_min_write_back_mem;
            LL_close_delegate_buffer(&base_node->lock.lock,
                                     buff,
                                     delegate_perform_remove_min_with_lock);
            while(0 != atomic_load_explicit(&cdelete_min_write_back_mem.pos,  memory_order_acquire)){
                // Spin wait
                asm("pause");
            }
            unsigned long key = cdelete_min_write_back_mem.key_value_array[0];
            unsigned long value = cdelete_min_write_back_mem.key_value_array[1];
            atomic_store_explicit(&cdelete_min_write_back_mem.pos, 1, memory_order_relaxed);
            *key_write_back = key;
            critical_exit();
            return value;
        }
    } while(retry);
    if(contended){
      //printf("R STACK POS %d\n", chelp_info.flush_stack_pos);
      LL_open_delegation_queue(&base_node->lock.lock);
      chelp_info.flush_stack[chelp_info.flush_stack_pos] = current_node;
      chelp_info.flush_stack_pos++;
    }else{
      //printf("NOT CONT\n");
      chelp_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    }
    //printf("GL RM %p %p %lu\n",prev_node,ACCESS_ONCE(set->root), pthread_self());
    chelp_info.set = set;
    chelp_info.current_help_node = current_node;
    chelp_info.last_locked_node = current_node;
    chelp_info.last_locked_node_parent = prev_node; 
    if(contended){
      chelp_info.last_locked_node->baseOrRoute.base.lock.statistics += CSLCATREE_LOCK_FAILURE_STATS_CONTRIB;
      int current_stack_pos = 0;
      while(current_stack_pos < SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE &&
	    current_stack_pos < chelp_info.flush_stack_pos){
	CATreeBaseNode * n =&chelp_info.flush_stack[current_stack_pos]->baseOrRoute.base;
	LL_flush_delegation_queue(&n->lock.lock);
	//printf("FR STACK POS %d\n", current_stack_pos);
	current_stack_pos++;
	if(current_stack_pos<chelp_info.flush_stack_pos){
	  DEBUG_PRINT(("not same as last node min unlock %p\n", current_node));
	  LL_unlock(&n->lock.lock);
	}
      }
    }else{
      chelp_info.last_locked_node->baseOrRoute.base.lock.statistics += CSLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
    }
      /* if(chelp_info.last_locked_node != chelp_info.current_help_node){ */
      /*   DEBUG_PRINT(("not same as last node min unlock %p\n", current_node)); */
      /*   LL_unlock(&base_node->lock.lock); */
      /* } */
    chelp_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    perform_remove_min_with_lock(&cdelete_min_write_back_mem);
    //Read the first from the cache
    unsigned long key = cdelete_min_write_back_mem.key_value_array[0];
    unsigned long value = cdelete_min_write_back_mem.key_value_array[1];
    //printf("remove  key %lu\n", key);
    //printf("REMOVE FROM CACHE AFTER FILL: %lu pos: %lu\n", key, 0);
    atomic_store_explicit(&cdelete_min_write_back_mem.pos, 1, memory_order_relaxed);
    *key_write_back = key;
    chelp_info.flush_stack_pos = 0;
    //printf("RL RM %lu \n", pthread_self());
    adaptAndUnlock(set,
                   chelp_info.last_locked_node,
                   chelp_info.last_locked_node_parent);

    critical_exit();
    return value;
}

void cslqdcatree_delete(CSLCATreeSet * setParam){
    cslcatree_set_destroy(setParam);
    CSLCATREE_FREE(setParam);
}


CSLCATreeSet * cslqdcatree_new(){
    CSLCATreeSet * set = CSLCATREE_MALLOC(sizeof(CSLCATreeSet));
    plain_cslcatree_set_init(set);
    return set;
}

/* int main(){ */
/*     printf("THIS IS A TEST FUNCTION 2\n"); */
/*     CSLCATreeSet * l =  cslcatree_new(); */
/*     for(unsigned long val = 0; val < 20; val++){ */
/*         cslcatree_put(l, val, val); */
/*         printf("INSERTED %ul\n", val); */
/*     } */

/*     for(unsigned long val = 0; val < 20; val++){ */
/*         cslcatree_put(l, val, val); */
/*         printf("INSERTED %ul\n", val); */
/*     } */

/*     for(int i = 0; i < 41; i++){ */
/*         unsigned long key = 44; */
/*         unsigned long val = cslcatree_remove_min(l, &key); */
/*         printf("RETURNED %ul %ul\n", val, key); */
/*     }     */
/*     cslcatree_delete(l); */
/* } */