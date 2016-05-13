#include "skiplist_qdcatree_set.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "misc/thread_includes.h"
#include "misc/padded_types.h"
#include "locks/locks.h"
#include "eseq_skiplist.h"

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
/*
 * ========================
 * Internal data structures
 * ========================
 */

#define DEBUG_PRINT(x)
//printf x
#define SLCATREE_MAX_CONTENTION_STATISTICS 1000
#define SLCATREE_MIN_CONTENTION_STATISTICS -1000
#define SLCATREE_LOCK_SUCCESS_STATS_CONTRIB -1
#define SLCATREE_LOCK_FAILURE_STATS_CONTRIB 250
#define SLCATREE_STACK_NEED 50
#define SLCATREE_FREE_LIST_SIZE 4096

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

typedef struct sl_catree_set {
    //DRMCSLock globalLock;
    char pad1[CACHE_LINE_SIZE*2];    
    CATreeBaseOrRouteNode * root;
    char pad2[CACHE_LINE_SIZE*2];
    QDLock delete_min_lock;
    TATASLock freeingFreeListLock;
    volatile atomic_intptr_t freeList[SLCATREE_FREE_LIST_SIZE];
    LLPaddedULong freeListIndexCounter;
} SLCATreeSet;

#define SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE 64
typedef struct {
    char pad1[128];
    SLCATreeSet * set;
    CATreeBaseOrRouteNode * current_help_node;
    CATreeBaseOrRouteNode * last_locked_node;
    CATreeBaseOrRouteNode * last_locked_node_parent;
    int flush_stack_pos;
    CATreeBaseOrRouteNode * flush_stack[SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE];
    char pad2[128 - (4*sizeof(CATreeBaseOrRouteNode *))];
} help_info_type;

_Alignas(CACHE_LINE_SIZE)
__thread help_info_type help_info = {.flush_stack_pos = 0};
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
        lock->statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
        return;
    }
    LL_lock(&lock->lock);
    /* lock->validation++; */
    /* assert(lock->validation == 1); */
    lock->statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
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
    CATreeBaseOrRouteNode * newBase = SLCATREE_MALLOC(sizeof(CATreeBaseOrRouteNode));
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
    CATreeBaseOrRouteNode * newRoute = 
        SLCATREE_MALLOC(sizeof(CATreeBaseOrRouteNode));
    newRoute->isBaseNode = false;
    LL_initialize(&newRoute->baseOrRoute.route.lock);
    newRoute->baseOrRoute.route.valid = true;
    newRoute->baseOrRoute.route.left = newBaseLeft;
    newRoute->baseOrRoute.route.right = newBaseRight;
    newRoute->baseOrRoute.route.key = key;
    return newRoute;
}




/* static void enqueue_in_free_list(SLCATreeSet * set, CATreeBaseOrRouteNode * toEnqueue){ */
/*     while(true){ */
/*         unsigned long slot = atomic_fetch_add(&set->freeListIndexCounter.value, 1); */
/*         if(slot == SLCATREE_FREE_LIST_SIZE){ */
/*             // We are responsible for doing the freeing */
/*             // Ensure that no one still is accessing old data */
/*             CATreeBaseOrRouteNode * elementsToFree[SLCATREE_FREE_LIST_SIZE]; */
/*             drmcs_runlock(&set->globalLock); */
/*             drmcs_lock(&set->globalLock); */
/*             drmcs_unlock(&set->globalLock); */
/*             drmcs_rlock(&set->globalLock); */
/*             for(int i = 0; i < SLCATREE_FREE_LIST_SIZE; i++){ */
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
/*             for(int i = 0; i < SLCATREE_FREE_LIST_SIZE; i++){ */
/*                 CATreeBaseOrRouteNode * elementToFree = elementsToFree[i]; */
/*                 if(elementToFree->isBaseNode){ */
/*                     LL_destroy(&elementToFree->baseOrRoute.base.lock.lock); //Free deleted base */
/*                     SLCATREE_FREE(elementToFree); */
/*                 }else{ */
/*                     LL_destroy(&elementToFree->baseOrRoute.route.lock); //Free deleted route */
/*                     SLCATREE_FREE(elementToFree);    */
/*                 } */

/*             } */
/*             return; */
/*         }else if (slot > SLCATREE_FREE_LIST_SIZE){ */
/*             //We need to wait then retry */
/*             drmcs_runlock(&set->globalLock); */
/*             while(atomic_load(&set->freeListIndexCounter.value) > SLCATREE_FREE_LIST_SIZE){ */
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
void plain_slcatree_set_init(SLCATreeSet * set){
    //    drmcs_initialize(&set->globalLock);
    LL_initialize(&set->delete_min_lock);
    set->root = allocate_base_node(new_skiplist());
    tatas_initialize(&set->freeingFreeListLock);
    for(int i = 0; i < SLCATREE_FREE_LIST_SIZE; i++){
        atomic_init(&set->freeList[i], (intptr_t)NULL);
    }
    atomic_init(&set->freeListIndexCounter.value, 0);
}


static inline
void slcatree_set_print_helper_helper(Skiplist * node){
    printf("SL\n");
}

static inline
void slcatree_set_print_helper(CATreeBaseOrRouteNode * root){
    if(root->isBaseNode){
        printf("B(");
        slcatree_set_print_helper_helper(root->baseOrRoute.base.root);
        printf(")");
    }else{
        CATreeRouteNode * route = &root->baseOrRoute.route;
        printf("R(");
        printf("%lu", route->key);
        printf(",");
        slcatree_set_print_helper(route->left);
        printf(",");
        slcatree_set_print_helper(route->right);
        printf(")");
    }
}

static inline
void slcatree_set_print_helper_helper_dot(Skiplist * node){
    printf("SL\n");
}

static inline
void slcatree_set_print_helper_dot(CATreeBaseOrRouteNode * root){
    if(root->isBaseNode){
        printf("B(\n");
        printf("digraph G{\n");
        printf("  graph [ordering=\"out\"];\n");
        slcatree_set_print_helper_helper_dot(root->baseOrRoute.base.root);
        printf("}\n");
        printf("\n)");
    }else{
        CATreeRouteNode * route = &root->baseOrRoute.route;
        printf("R(");
        printf("%lu", route->key);
        printf(",");
        slcatree_set_print_helper_dot(route->left);
        printf(",");
        slcatree_set_print_helper_dot(route->right);
        printf(")");
    }
}


static inline
void slcatree_set_print(void * setParam){
    SLCATreeSet * set = (SLCATreeSet *)setParam;
    slcatree_set_print_helper(set->root);
    printf("\n\nDOT:\n\n");
    slcatree_set_print_helper_dot(set->root);
    printf("\n\n");
}

static inline
void slcatree_set_free_helper(CATreeBaseOrRouteNode * root){
    if(root->isBaseNode){
        skiplist_delete(root->baseOrRoute.base.root);
        SLCATREE_FREE(root);
    }else{
        CATreeRouteNode * route = &root->baseOrRoute.route;
        slcatree_set_free_helper(route->left);
        slcatree_set_free_helper(route->right);
        LL_destroy(&route->lock);
        SLCATREE_FREE(root);
    }
}

static inline
void slcatree_set_destroy(void * setParam){
    SLCATreeSet * set = (SLCATreeSet *)setParam;
    for(int i = 0; i < SLCATREE_FREE_LIST_SIZE; i++){
        CATreeBaseOrRouteNode * elementToFree = (CATreeBaseOrRouteNode *)atomic_load(&set->freeList[i]);
        if(elementToFree != NULL){
            if(elementToFree->isBaseNode){
                LL_destroy(&elementToFree->baseOrRoute.base.lock.lock); //Free deleted base
                SLCATREE_FREE(elementToFree);
            }else{
                LL_destroy(&elementToFree->baseOrRoute.route.lock); //Free deleted route
                SLCATREE_FREE(elementToFree);   
            }
        }
    }
    slcatree_set_free_helper(set->root);
}

static inline
bool contention_reduce_split(CATreeBaseOrRouteNode *  baseNodeContainer,
                             void ** parent,
                             SLCATreeSet * set,
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
    //TODO Skip freeing
    //enqueue_in_free_list(set, baseNodeContainer);
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
                                                     SLCATreeSet * set){
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
                        SLCATreeSet * set) {
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
    //enqueue_in_free_list(set, baseNodeContainer);
    //enqueue_in_free_list(set, neighbourBaseContainer);
    //enqueue_in_free_list(set, parentRoute);
#ifdef SLCATREE_COUNT_ROUTE_NODES
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
                                     SLCATreeSet * set,
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
    if(SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE > help_info.flush_stack_pos){
      //printf("L STACK POS %d\n", help_info.flush_stack_pos);
      LL_open_delegation_queue(&newNeighbourBase->lock.lock);
      help_info.flush_stack[help_info.flush_stack_pos] = newNeighbourBaseContainer;
    }
    help_info.flush_stack_pos++;
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
    if(SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE < help_info.flush_stack_pos){
        DEBUG_PRINT(("force unlock node %p\n", baseNodeContainer));
        catree_unlock(&node->lock);
    }
    //TODO SKIP FREEING!
    //enqueue_in_free_list(set, baseNodeContainer);
    //enqueue_in_free_list(set, neighbourBaseContainer);
    //enqueue_in_free_list(set, parentRoute);
#ifdef SLCATREE_COUNT_ROUTE_NODES
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




static inline void adaptAndUnlock(SLCATreeSet * set,
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
    if(lock->statistics > SLCATREE_MAX_CONTENTION_STATISTICS){
        high_contention = true;
    } else if(lock->statistics < SLCATREE_MIN_CONTENTION_STATISTICS){
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


/* #define PUSH_NODE(Dtt, Tdt)                     \ */
/*     ((Dtt)->array[(Dtt)->pos++] = Tdt) */

/* static inline void push_node_dyn_array(CATreeRoutingNodeStack * stack, */
/*                                        CATreeBaseOrRouteNode * node){ */
/*     unsigned int i; */
/*     if(stack->pos == stack->slot){ */
/*         CATreeBaseOrRouteNode ** newArray = */
/*             SLCATREE_MALLOC(sizeof(CATreeBaseOrRouteNode*) * (stack->slot*2)); */
/*         for(i = 0; i < stack->pos; i++){ */
/*             newArray[i] = stack->array[i]; */
/*         } */
/*         if(stack->slot > SLCATREE_STACK_NEED){ */
/*             /\* Dynamically allocated array that needs to be deallocated *\/ */
/*             SLCATREE_FREE(stack->array); */
/*         } */
/*         stack->array = newArray; */
/*         stack->slot = stack->slot*2; */
/*     } */
/*     PUSH_NODE(stack, node); */
/* } */

/* #define POP_NODE(Dtt)			\ */
/*      (((Dtt)->pos) ? 			\ */
/*       (Dtt)->array[--((Dtt)->pos)] : NULL) */

/* #define TOP_NODE(Dtt)                   \ */
/*      (((Dtt)->pos) ?                             \ */
/*       (Dtt)->array[(Dtt)->pos - 1] : NULL) */

/* #define EMPTY_NODE(Dtt) (TOP_NODE(Dtt) == NULL) */

/* static inline void init_stack(CATreeRoutingNodeStack *stack, */
/*                               CATreeBaseOrRouteNode ** stack_array, */
/*                               unsigned int init_slot) */
/* { */
/*     stack->array = stack_array; */
/*     stack->pos = 0; */
/*     stack->slot = init_slot; */
/* } */



/* #define DEC_ROUTE_NODE_STACK_AND_ARRAY(STACK_NAME) \ */
/*     CATreeRoutingNodeStack STACK_NAME; \ */
/*     CATreeRoutingNodeStack * STACK_NAME##_ptr = &(STACK_NAME); \ */
/*     CATreeBaseOrRouteNode * STACK_NAME##_array[SLCATREE_STACK_NEED]; */

/* #define DECLARE_AND_INIT_BASE_NODE_SEARCH_STACKS \ */
/* DEC_ROUTE_NODE_STACK_AND_ARRAY(search_stack)  \ */
/* DEC_ROUTE_NODE_STACK_AND_ARRAY(search_stack_copy) \ */
/* DEC_ROUTE_NODE_STACK_AND_ARRAY(locked_base_nodes_stack) \ */
/* init_stack(&search_stack, search_stack_array, 0); \ */
/* init_stack(&search_stack_copy, search_stack_copy_array, 0); \ */
/* init_stack(&locked_base_nodes_stack, locked_base_nodes_stack_array, SLCATREE_STACK_NEED);/\* Abuse as stack array size*\/ */

/* static CATreeBaseOrRouteNode * */
/* erl_db_catree_leftmost_base_node_and_path(CATreeBaseOrRouteNode * root, CATreeRoutingNodeStack * stack){ */
/*     CATreeBaseOrRouteNode *  baseOrRoute = root; */
/*     while(!baseOrRoute->isBaseNode){ */
/*         PUSH_NODE(stack, baseOrRoute); */
/*         baseOrRoute = baseOrRoute->baseOrRoute.route.left; */
/*     } */
/*     return baseOrRoute; */
/* } */

/* static inline CATreeBaseOrRouteNode * */
/* get_next_base_node_and_path(CATreeBaseOrRouteNode * base_node, */
/*                             CATreeRoutingNodeStack * stack) { */
/*     if (EMPTY_NODE(stack)) { // The parent of b is the root */
/*         return NULL; */
/*     } else { */
/*         if (TOP_NODE(stack)->baseOrRoute.route.left == base_node) { */
/*             return erl_db_catree_leftmost_base_node_and_path( */
/*                         TOP_NODE(stack)->baseOrRoute.route.right, */
/*                         stack); */
/*         } else { */
/*             unsigned long pkey = TOP_NODE(stack)->baseOrRoute.route.key; */
/*             POP_NODE(stack); */
/*             while (!EMPTY_NODE(stack)) { */
/*                 if(TOP_NODE(stack)->baseOrRoute.route.valid && pkey < TOP_NODE(stack)->baseOrRoute.route.key){ */
/*                     return erl_db_catree_leftmost_base_node_and_path(TOP_NODE(stack)->baseOrRoute.route.right, stack); */
/*                 } else { */
/*                   POP_NODE(stack); */
/*                 } */
/*             } */
/*         } */
/*         return NULL; */
/*     } */
/* } */

/* static inline void */
/* clone_stack(CATreeRoutingNodeStack * search_stack_ptr, CATreeRoutingNodeStack * search_stack_copy_ptr){ */
/*     unsigned int i; */
/*     search_stack_copy_ptr->pos = search_stack_ptr->pos; */
/*     for(i = 0; i < search_stack_ptr->pos; i++){ */
/*         search_stack_copy_ptr->array[i] = search_stack_ptr->array[i]; */
/*     } */
/* } */

/* static inline CATreeBaseNode * find_and_lock_next_base_node_and_path(CATreeRoutingNodeStack ** search_stack_ptr_ptr, */
/*                                                                      CATreeRoutingNodeStack ** search_stack_copy_ptr_ptr, */
/*                                                                      CATreeRoutingNodeStack * locked_base_nodes_stack_ptr, */
/*                                                                      bool * is_contended) */
/* { */
/*     CATreeBaseOrRouteNode * current_node; */
/*  find_next_try_again: */
/*     current_node = TOP_NODE(locked_base_nodes_stack_ptr); */
/*     CATreeBaseNode * base_node; */
/*     CATreeRoutingNodeStack * tmp_stack_ptr; */
/*     clone_stack(*search_stack_ptr_ptr,*search_stack_copy_ptr_ptr); */
/*     current_node = */
/*         get_next_base_node_and_path(current_node, *search_stack_ptr_ptr); */
/*     if(current_node == NULL){ */
/*         tmp_stack_ptr = *search_stack_ptr_ptr; */
/*         *search_stack_ptr_ptr = *search_stack_copy_ptr_ptr; */
/*         *search_stack_copy_ptr_ptr = tmp_stack_ptr; */
/*         return NULL; */
/*     } */
/*     base_node = &current_node->baseOrRoute.base; */
/*     *is_contended = catree_lock_is_contended(&base_node->lock); */
/*     if( ! base_node->valid ){ */
/*         /\* Retry *\/ */
/*         catree_unlock(&base_node->lock); */
/*         //TODO3 */
/*         /\* drmcs_runlock(&set->globalLock); *\/ */
/*         /\* drmcs_rlock(&set->globalLock); *\/ */
/*         /\* Revere to previos state *\/ */
/*         current_node = TOP_NODE(locked_base_nodes_stack_ptr); */
/*         tmp_stack_ptr = *search_stack_ptr_ptr; */
/*         *search_stack_ptr_ptr = *search_stack_copy_ptr_ptr; */
/*         *search_stack_copy_ptr_ptr = tmp_stack_ptr; */
/*         goto find_next_try_again; */
/*     }else{ */
/*         push_node_dyn_array(locked_base_nodes_stack_ptr, current_node); */
/*     } */
/*     return base_node; */
/* } */
/* #define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x)) */
/* static inline void slcatree_set_print(void * setParam); */
/* static inline CATreeBaseNode * lock_first_base_node(SLCATreeSet * set, */
/*                                                     CATreeRoutingNodeStack * search_stack_ptr, */
/*                                                     CATreeRoutingNodeStack * locked_base_nodes_stack_ptr, */
/*                                                     bool * is_contended){ */
/*     int retry; */
/*     CATreeBaseOrRouteNode * current_node; */
/*     CATreeRouteNode * current_route_node = NULL; */
/*     CATreeBaseNode * base_node; */
/*     int count = 10000000; */
/*     do { */
/*         count--; */
/*         retry = 0; */
/*         current_node = ACCESS_ONCE(set->root); */
/*         current_route_node = NULL; */
/*         while( ! current_node->isBaseNode ){ */
/*             PUSH_NODE(search_stack_ptr, current_node); */
/*             current_route_node = &current_node->baseOrRoute.route; */
/*             current_node = current_route_node->left; */
/*         } */
/*         if(count == 0){ */
/*             printf("STUCK AT %p %d parent %p\n", current_node, pthread_self(), TOP_NODE(search_stack_ptr)); */
/*             printf("WELL COUNT REACHED FUCK!!!\n"); */
/*             assert(0); */
/*         } */
/*         base_node = &current_node->baseOrRoute.base; */
/*         *is_contended = catree_lock_is_contended(&base_node->lock); */
/*         if( ! base_node->valid ){ */
/*             /\* Retry *\/ */
/*             catree_unlock(&base_node->lock); */
/*             drmcs_runlock(&set->globalLock); */
/*             drmcs_rlock(&set->globalLock); */
/*             retry = 1; */
/*             search_stack_ptr->pos = 0; */
/*             search_stack_ptr->slot = 0; */
/*         } */
/*     } while(retry); */
/*     push_node_dyn_array(locked_base_nodes_stack_ptr, current_node); */
/*     return base_node; */
/* } */


/* static inline void */
/* unlock_and_release_locked_base_node_stack(CATreeRoutingNodeStack * locked_base_nodes_stack_ptr, */
/*                                           bool last_contended, */
/*                                           CATreeBaseOrRouteNode * first_locked_parent, */
/*                                           SLCATreeSet * set) */
/* { */
/*     CATreeBaseOrRouteNode * current_node; */
/*     CATreeBaseNode * base_node; */
/*     int i; */
/*     if(locked_base_nodes_stack_ptr->pos == 1){ */
/*         current_node = locked_base_nodes_stack_ptr->array[0]; */
/*         base_node = &current_node->baseOrRoute.base; */
/*         if(last_contended){ */
/*             base_node->lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB; */
/*         }else{ */
/*             base_node->lock.statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB; */
/*         } */
/*     }else{ */
/*         for(i = locked_base_nodes_stack_ptr->pos -1; i >= 0 ; i--){ */

/*             current_node = locked_base_nodes_stack_ptr->array[i]; */
/*             base_node = &current_node->baseOrRoute.base; */
/*             base_node->lock.statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB; */
/*             if(i != 0){ */
/*                 catree_unlock(&base_node->lock); */
/*             } */
/*         } */
/*     } */
/*     if(locked_base_nodes_stack_ptr->pos >= 1){ */
/*         adaptAndUnlock(set, */
/*                        current_node, */
/*                        first_locked_parent); */
/*     } */
/*     if(locked_base_nodes_stack_ptr->slot > SLCATREE_STACK_NEED){ */
/*         SLCATREE_FREE(locked_base_nodes_stack_ptr->array); */
/*     } */
/* } */

/* static inline */
/* unsigned long slcatree_set_delete_min_opt(SLCATreeSet * set, unsigned long * key_write_back){ */
/*     CATreeBaseNode * base_node; */
/*     bool last_contended = false; */
/*     DECLARE_AND_INIT_BASE_NODE_SEARCH_STACKS; */
/*     /\* Find first base node *\/ */
/*     base_node = lock_first_base_node(set, &search_stack, &locked_base_nodes_stack, &last_contended); */
/*     CATreeBaseOrRouteNode * first_locked_parent = TOP_NODE(search_stack_ptr); */
/*     /\* Find next base node until non-empyu base node is found *\/ */
/*     while(base_node != NULL && skiplist_is_empty(base_node->root)){ */
/*         base_node = find_and_lock_next_base_node_and_path(&search_stack_ptr, */
/*                                                           &search_stack_copy_ptr, */
/*                                                           locked_base_nodes_stack_ptr, */
/*                                                           &last_contended); */
/*     } */
/*     /\* Get the return value *\/ */
/*     if(base_node != NULL){ */
/*         unsigned long result_value = skiplist_remove_min(base_node->root, key_write_back); */
/*         unlock_and_release_locked_base_node_stack(locked_base_nodes_stack_ptr, */
/*                                                   last_contended, */
/*                                                   first_locked_parent, */
/*                                                   set); */
/*         return result_value; */
/*     }else{ */
/*         /\* Unlock base nodes and deinitialize search data *\/ */
/*         unlock_and_release_locked_base_node_stack(locked_base_nodes_stack_ptr, */
/*                                                   last_contended, */
/*                                                   first_locked_parent, */
/*                                                   set); */
/*         *key_write_back = -1; */
/*         return 0; */
/*     } */
/* } */

static inline
unsigned long perform_remove_min_with_lock(unsigned long * key_write_back){
    while(help_info.last_locked_node_parent != NULL && skiplist_is_empty(help_info.last_locked_node->baseOrRoute.base.root)){
        //Merge!
        help_info.last_locked_node =
            low_contention_join_force_left_child(help_info.last_locked_node,
                                                 help_info.last_locked_node_parent,
                                                 help_info.set,
                                                 &help_info.last_locked_node_parent);
    }
    CATreeBaseNode * base_node = &help_info.last_locked_node->baseOrRoute.base;
    unsigned long result_value = skiplist_remove_min(base_node->root, key_write_back);
    return result_value;
}

typedef struct {
    char pad1[128];
    volatile atomic_ulong value;
    unsigned long key;
    char pad2[128];
} delete_min_write_back_mem_type;

_Alignas(CACHE_LINE_SIZE)
__thread delete_min_write_back_mem_type delete_min_write_back_mem = {.value = ATOMIC_VAR_INIT(((unsigned long)-1))};


static inline void delegate_perform_remove_min_with_lock(unsigned int msgSize, void * msgParam){
    delete_min_write_back_mem_type * mem = *((delete_min_write_back_mem_type **)msgParam);
    //drmcs_rlock(&set->globalLock);
    unsigned long value = perform_remove_min_with_lock(&mem->key);
    atomic_store_explicit(&mem->value, value, memory_order_release);
    help_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
}


/*
 *=================
 * Public interface
 *=================
 */

unsigned long slqdcatree_remove_min(SLCATreeSet * set, unsigned long * key_write_back){
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
        void * buff = LL_delegate_or_lock_keep_closed(&base_node->lock.lock, sizeof(delete_min_write_back_mem_type*), &contended);
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
            *(delete_min_write_back_mem_type **)buff = &delete_min_write_back_mem;
            LL_close_delegate_buffer(&base_node->lock.lock,
                                     buff,
                                     delegate_perform_remove_min_with_lock);
            unsigned long val;
            while(((unsigned long)-1) ==
                  (val = atomic_load_explicit(&delete_min_write_back_mem.value,  memory_order_acquire))){
                // Spin wait
                asm("pause");
            }
            *key_write_back = delete_min_write_back_mem.key;
            atomic_store_explicit(&delete_min_write_back_mem.value, -1, memory_order_relaxed);
            return val;
        }
    } while(retry);
    if(contended){
      //printf("R STACK POS %d\n", help_info.flush_stack_pos);
      LL_open_delegation_queue(&base_node->lock.lock);
      help_info.flush_stack[help_info.flush_stack_pos] = current_node;
      help_info.flush_stack_pos++;
    }else{
      //printf("NOT CONT\n");
      help_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    }
    help_info.set = set;
    help_info.current_help_node = current_node;
    help_info.last_locked_node = current_node;
    help_info.last_locked_node_parent = prev_node; 
    if(contended){
      help_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
      int current_stack_pos = 0;
      while(current_stack_pos < SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE &&
	    current_stack_pos < help_info.flush_stack_pos){
	CATreeBaseNode * n =&help_info.flush_stack[current_stack_pos]->baseOrRoute.base;
	LL_flush_delegation_queue(&n->lock.lock);
	//printf("FR STACK POS %d\n", current_stack_pos);
	current_stack_pos++;
	if(current_stack_pos<help_info.flush_stack_pos){
	  DEBUG_PRINT(("not same as last node min unlock %p\n", current_node));
	  LL_unlock(&n->lock.lock);
	}
      }
    }else{
      help_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
    }
      /* if(help_info.last_locked_node != help_info.current_help_node){ */
      /*   DEBUG_PRINT(("not same as last node min unlock %p\n", current_node)); */
      /*   LL_unlock(&base_node->lock.lock); */
      /* } */
    help_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    unsigned long return_val = perform_remove_min_with_lock(key_write_back);
    help_info.flush_stack_pos = 0;
    adaptAndUnlock(set,
                   help_info.last_locked_node,
                   help_info.last_locked_node_parent);

    return return_val;
}

typedef struct {
    unsigned long key;
    unsigned long value;
} put_message;

static inline void delegate_perform_put_with_lock(unsigned int msgSize, void * msgParam){
    put_message * msg = msgParam;
    skiplist_put(help_info.last_locked_node->baseOrRoute.base.root, msg->key, msg->value);
    help_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
}

void slqdcatree_put(SLCATreeSet * set,
                    unsigned long key,
                    unsigned long value){

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
    void * buff = LL_delegate_or_lock_keep_closed(&node->lock.lock, sizeof(put_message), &contended);
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
        put_message msg;
        msg.key = key;
        msg.value = value;
        memcpy(buff, &msg, sizeof(put_message));
        LL_close_delegate_buffer(&node->lock.lock,
                                 buff,
                                 delegate_perform_put_with_lock);
        return;
    }
    if(contended){
      //printf("P STACK POS %d\n", help_info.flush_stack_pos);
      fflush(stdout);
      LL_open_delegation_queue(&node->lock.lock);
      help_info.flush_stack[help_info.flush_stack_pos] = currentNode;
      help_info.flush_stack_pos++;
    }else{
      help_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    }
    help_info.set = set;
    help_info.current_help_node = currentNode;
    help_info.last_locked_node = currentNode;
    help_info.last_locked_node_parent = prevNode;
    skiplist_put(node->root, key, value);

    if(contended){
      help_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
      int current_stack_pos = 0;
      while(current_stack_pos < SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE &&
	    current_stack_pos < help_info.flush_stack_pos){
	//printf("PF STACK POS %d\n", current_stack_pos);
	CATreeBaseNode * n =&help_info.flush_stack[current_stack_pos]->baseOrRoute.base;
	LL_flush_delegation_queue(&n->lock.lock);
	current_stack_pos++;
	if(current_stack_pos<help_info.flush_stack_pos){
	  DEBUG_PRINT(("not same as last node min unlock %p\n", current_node));
	  LL_unlock(&n->lock.lock);
	}
      }
    }else{
      help_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
    }
    /* if(help_info.last_locked_node != help_info.current_help_node){ */
    /*     DEBUG_PRINT(("not same as last node min unlock %p\n", currentNode)); */
    /*     LL_unlock(&node->lock.lock); */
    /* } */
    help_info.flush_stack_pos = 0;    
    adaptAndUnlock(set,
                   help_info.last_locked_node,
                   help_info.last_locked_node_parent);
    return;
}


void slqdcatree_delete(SLCATreeSet * setParam){
    slcatree_set_destroy(setParam);
    SLCATREE_FREE(setParam);
}


SLCATreeSet * slqdcatree_new(){
    SLCATreeSet * set = SLCATREE_MALLOC(sizeof(SLCATreeSet));
    plain_slcatree_set_init(set);
    return set;
}

/* int main(){ */
/*     printf("THIS IS A TEST FUNCTION 2\n"); */
/*     SLCATreeSet * l =  slcatree_new(); */
/*     for(unsigned long val = 0; val < 20; val++){ */
/*         slcatree_put(l, val, val); */
/*         printf("INSERTED %ul\n", val); */
/*     } */

/*     for(unsigned long val = 0; val < 20; val++){ */
/*         slcatree_put(l, val, val); */
/*         printf("INSERTED %ul\n", val); */
/*     } */

/*     for(int i = 0; i < 41; i++){ */
/*         unsigned long key = 44; */
/*         unsigned long val = slcatree_remove_min(l, &key); */
/*         printf("RETURNED %ul %ul\n", val, key); */
/*     }     */
/*     slcatree_delete(l); */
/* } */
