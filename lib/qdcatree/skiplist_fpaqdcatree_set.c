#include "skiplist_fpaqdcatree_set.h"

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
#include "binheap.h"

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

typedef struct fpasl_catree_set {
    //DRMCSLock globalLock;
    char pad1[CACHE_LINE_SIZE*2];    
    CATreeBaseOrRouteNode * root;
    char pad2[CACHE_LINE_SIZE*2];
    int relaxation;
    char pad3[CACHE_LINE_SIZE*2];
} FPACATreeSet;

#define SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE 64
typedef struct {
    char pad1[128];
    FPACATreeSet * set;
    CATreeBaseOrRouteNode * current_help_node;
    CATreeBaseOrRouteNode * last_locked_node;
    CATreeBaseOrRouteNode * last_locked_node_parent;
    int flush_stack_pos;
    CATreeBaseOrRouteNode * flush_stack[SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE];
    int remove_min_contention;
    SkiplistNode * del_min_buffer;
    int put_contention;
    int max_buffered_puts;
    heap_t put_buffer;
    char pad2[128];
} fpahelp_info_type;

_Alignas(CACHE_LINE_SIZE)
__thread fpahelp_info_type fpahelp_info = {.flush_stack_pos = 0,
					   .remove_min_contention=0,
                                           .del_min_buffer=NULL,
                                           .put_contention=0,
                                           .max_buffered_puts=0,
                                           .put_buffer.len = 0,
                                           .put_buffer.size = 0};
/*
 * ==================
 * Internal functions
 * ==================
 */

bool min_from_remove_min_buffer(unsigned long *smallest_key_remove_min_buffer){
    if(fpahelp_info.del_min_buffer == NULL){
        return false;
    }else{
        *smallest_key_remove_min_buffer = fpahelp_info.del_min_buffer->key_values[fpahelp_info.del_min_buffer->first_key_value_pos].key;
        return true;
    }
}
bool remove_min_from_del_min_buffer(unsigned long *key, unsigned long *val){
    if(fpahelp_info.del_min_buffer == NULL){
        return false;
    }else{
        *key = fpahelp_info.del_min_buffer->key_values[fpahelp_info.del_min_buffer->first_key_value_pos].key;
        *val = fpahelp_info.del_min_buffer->key_values[fpahelp_info.del_min_buffer->first_key_value_pos].value;
        fpahelp_info.del_min_buffer->first_key_value_pos++;
        if(fpahelp_info.del_min_buffer->first_key_value_pos >= SKIPLIST_MAX_VALUSES_IN_NODE){
            SkiplistNode * old_buffer = fpahelp_info.del_min_buffer;
            fpahelp_info.del_min_buffer = fpahelp_info.del_min_buffer->lower_lists[fpahelp_info.del_min_buffer->num_of_levels - 1];
            free(old_buffer);
        }
        return true;
    }

}


bool remove_min_from_smallest_buffer(unsigned long *key_write_back, unsigned long *val){
    unsigned long smallest_key_put_buffer;
    if(peek(&fpahelp_info.put_buffer, &smallest_key_put_buffer)){
        unsigned long smallest_key_remove_min_buffer;
        if(min_from_remove_min_buffer(&smallest_key_remove_min_buffer)){
            if(smallest_key_remove_min_buffer < smallest_key_put_buffer){
                return remove_min_from_del_min_buffer(key_write_back, val);
            }else{
                return pop(&fpahelp_info.put_buffer, key_write_back, val);
            }
        }else{
            return pop(&fpahelp_info.put_buffer, key_write_back, val);
        }
    }else{
        return remove_min_from_del_min_buffer(key_write_back, val);
    }
}


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
    CATreeBaseOrRouteNode * newBase = gc_alloc(ptst, gc_id);//SLCATREE_MALLOC(sizeof(CATreeBaseOrRouteNode));
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
    //SLCATREE_MALLOC(sizeof(CATreeBaseOrRouteNode));
    newRoute->isBaseNode = false;
    LL_initialize(&newRoute->baseOrRoute.route.lock);
    newRoute->baseOrRoute.route.valid = true;
    newRoute->baseOrRoute.route.left = newBaseLeft;
    newRoute->baseOrRoute.route.right = newBaseRight;
    newRoute->baseOrRoute.route.key = key;
    return newRoute;
}


static inline
void plain_slcatree_set_init(FPACATreeSet * set){
    //    drmcs_initialize(&set->globalLock);
    //LL_initialize(&set->delete_min_lock);
    gc_id = gc_add_allocator(sizeof(CATreeBaseOrRouteNode));
    critical_enter();
    set->root = allocate_base_node(new_skiplist());
    set->relaxation = 0;
    critical_exit();
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
    FPACATreeSet * set = (FPACATreeSet *)setParam;
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
    FPACATreeSet * set = (FPACATreeSet *)setParam;
    /* for(int i = 0; i < SLCATREE_FREE_LIST_SIZE; i++){ */
    /*     CATreeBaseOrRouteNode * elementToFree = (CATreeBaseOrRouteNode *)atomic_load(&set->freeList[i]); */
    /*     if(elementToFree != NULL){ */
    /*         if(elementToFree->isBaseNode){ */
    /*             LL_destroy(&elementToFree->baseOrRoute.base.lock.lock); //Free deleted base */
    /*             SLCATREE_FREE(elementToFree); */
    /*         }else{ */
    /*             LL_destroy(&elementToFree->baseOrRoute.route.lock); //Free deleted route */
    /*             SLCATREE_FREE(elementToFree);    */
    /*         } */
    /*     } */
    /* } */
    slcatree_set_free_helper(set->root);
}

static inline
bool contention_reduce_split(CATreeBaseOrRouteNode *  baseNodeContainer,
                             void ** parent,
                             FPACATreeSet * set,
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
                                                     FPACATreeSet * set){
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
                        FPACATreeSet * set) {
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
    //assert(parent->left == baseNodeContainer  || parent->right == baseNodeContainer);
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
                                     FPACATreeSet * set,
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
    if(SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE > fpahelp_info.flush_stack_pos){
      //printf("L STACK POS %d\n", fpahelp_info.flush_stack_pos);
      LL_open_delegation_queue(&newNeighbourBase->lock.lock);
      fpahelp_info.flush_stack[fpahelp_info.flush_stack_pos] = newNeighbourBaseContainer;
    }
    fpahelp_info.flush_stack_pos++;
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
    if(SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE < fpahelp_info.flush_stack_pos){
        DEBUG_PRINT(("force unlock node %p\n", baseNodeContainer));
        catree_unlock(&node->lock);
    }
    //TODO SKIP FREEING!
    gc_free(ptst, baseNodeContainer, gc_id);
    gc_free(ptst, neighbourBaseContainer, gc_id);
    gc_free(ptst, parentRoute, gc_id);
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




static inline void adaptAndUnlock(FPACATreeSet * set,
                                  CATreeBaseOrRouteNode * currentNode,
                                  CATreeBaseOrRouteNode * prevNode,
                                  bool catree_adapt){
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
    if(catree_adapt && high_contention || low_contention){
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


typedef struct {
    char pad1[128];
    volatile atomic_ulong response;
    unsigned long key;
    unsigned long value;
    char pad2[128];
} fpadelete_min_write_back_mem_type;

_Alignas(CACHE_LINE_SIZE)
__thread fpadelete_min_write_back_mem_type fpadelete_min_write_back_mem = {.response = ATOMIC_VAR_INIT(((unsigned long)-1))};


static inline
void perform_remove_min_with_lock(fpadelete_min_write_back_mem_type * mem){
    while(fpahelp_info.last_locked_node_parent != NULL && skiplist_is_empty(fpahelp_info.last_locked_node->baseOrRoute.base.root)){
        //Merge!
        fpahelp_info.last_locked_node =
            low_contention_join_force_left_child(fpahelp_info.last_locked_node,
                                                 fpahelp_info.last_locked_node_parent,
                                                 fpahelp_info.set,
                                                 &fpahelp_info.last_locked_node_parent);
    }
    int relaxation = fpahelp_info.set->relaxation;
    CATreeBaseNode * base_node = &fpahelp_info.last_locked_node->baseOrRoute.base;
    if(fpahelp_info.set->relaxation == 0){

        mem->value = skiplist_remove_min(base_node->root, &mem->key);
        atomic_store_explicit(&mem->response, 0, memory_order_release);
    }else{
        //Write back whole node(s)
        SkiplistNode * remove_head = skiplist_remove_head_nodes(base_node->root, relaxation);
        if(remove_head == NULL){
            mem->key = (unsigned long)-1;
            mem->value = (unsigned long)-1;
            atomic_store_explicit(&mem->response, 0, memory_order_release);
        }else{
            mem->key = remove_head;
            atomic_store_explicit(&mem->response, 1, memory_order_release);
        }
    }
}




static inline void delegate_perform_remove_min_with_lock(unsigned int msgSize, void * msgParam){
    fpadelete_min_write_back_mem_type * mem = *((fpadelete_min_write_back_mem_type **)msgParam);
    //drmcs_rlock(&set->globalLock);
    perform_remove_min_with_lock(mem);
}


/*
 *=================
 * Public interface
 *=================
 */
#define MAX_PUT_BUFFER_SIZE 500
#define PUT_BUFF_INCREASE_VALUE 500
#define PUT_BUFF_DECREASE_VALUE 1
#define PUT_BUFF_LOW_CONTENTION_ADATION_LIMIT -100
#define PUT_BUFF_HIGH_CONTENTION_ADATION_LIMIT 100
#define PUT_BUFF_HIGH_CONTENTION_INCREASE 2
#define PUT_BUFF_LOW_CONTENTION_DECREASE 1


#define MAX_REMOVE_MIN_RELAXATION_SIZE 1
#define REMOVE_MIN_INCREASE_VALUE 1
#define REMOVE_MIN_DECREASE_VALUE 1
#define REMOVE_MIN_LOW_CONTENTION_ADATION_LIMIT -1000
#define REMOVE_MIN_HIGH_CONTENTION_ADATION_LIMIT 1000
#define REMOVE_MIN_HIGH_CONTENTION_INCREASE 250
#define REMOVE_MIN_LOW_CONTENTION_DECREASE 1

static inline void adjust_remove_min_relaxation(){
    //Both buffers are empty reset put buffer and check if tresholds for put buffer limits are reached
    if(fpahelp_info.remove_min_contention > REMOVE_MIN_HIGH_CONTENTION_ADATION_LIMIT){
        fpahelp_info.remove_min_contention = 0;
        if(fpahelp_info.set->relaxation < MAX_REMOVE_MIN_RELAXATION_SIZE){
	  fpahelp_info.set->relaxation += REMOVE_MIN_INCREASE_VALUE;
	  //fpahelp_info.max_buffered_puts +
	  //printf("%d ", fpahelp_info.set->relaxation);
	}
    }else if (fpahelp_info.remove_min_contention < REMOVE_MIN_LOW_CONTENTION_ADATION_LIMIT){
        fpahelp_info.remove_min_contention =0;
        if(fpahelp_info.set->relaxation > 0){
            fpahelp_info.set->relaxation = fpahelp_info.set->relaxation-REMOVE_MIN_DECREASE_VALUE;
	    //printf("%d ", fpahelp_info.set->relaxation);
        }
    }
}

static inline void adjust_put_buffer(){
    //Both buffers are empty reset put buffer and check if tresholds for put buffer limits are reached
    if(fpahelp_info.put_contention > PUT_BUFF_HIGH_CONTENTION_ADATION_LIMIT){
        fpahelp_info.put_contention = 0;
        //if(fpahelp_info.max_buffered_puts < (MAX_PUT_BUFFER_SIZE-PUT_BUFF_INCREASE_VALUE)){
	fpahelp_info.max_buffered_puts = PUT_BUFF_INCREASE_VALUE;
	//fpahelp_info.max_buffered_puts +
	//printf("%d ", fpahelp_info.max_buffered_puts);
	    //}
    }else if (fpahelp_info.put_contention < PUT_BUFF_LOW_CONTENTION_ADATION_LIMIT){
        fpahelp_info.put_contention =0;
        if(fpahelp_info.max_buffered_puts > 0){
            fpahelp_info.max_buffered_puts = fpahelp_info.max_buffered_puts-PUT_BUFF_DECREASE_VALUE;
            //printf("%d ", fpahelp_info.max_buffered_puts);
        }
    }
    fpahelp_info.put_buffer.size = fpahelp_info.max_buffered_puts;

}

unsigned long fpaslqdcatree_remove_min_param(FPACATreeSet * set,
                                             unsigned long * key_write_back,
                                             bool remove_min_relax,
                                             bool put_relax,
                                             bool catree_adapt){
    unsigned long val;
    //try with buffers first
    if(remove_min_from_smallest_buffer(key_write_back, &val)){
        return val;
    }
    //Find leftmost routing node
    int retry;
    CATreeBaseOrRouteNode * current_node;
    CATreeBaseOrRouteNode * prev_node;
    CATreeRouteNode * current_route_node = NULL;
    CATreeBaseNode * base_node;
    bool contended;
    critical_enter();
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
        void * buff = LL_delegate_or_lock_keep_closed(&base_node->lock.lock, sizeof(fpadelete_min_write_back_mem_type*), &contended);
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
            *(fpadelete_min_write_back_mem_type **)buff = &fpadelete_min_write_back_mem;
            LL_close_delegate_buffer(&base_node->lock.lock,
                                     buff,
                                     delegate_perform_remove_min_with_lock);
            unsigned long val;
            unsigned long response;
            while(((unsigned long)-1) ==
                  (response = atomic_load_explicit(&fpadelete_min_write_back_mem.response,  memory_order_acquire))){
                // Spin wait
                asm("pause");
            }
            if(response == 0){
                *key_write_back = fpadelete_min_write_back_mem.key;
                val = fpadelete_min_write_back_mem.value;
            }else{
                // node response
                fpahelp_info.del_min_buffer = (SkiplistNode*)fpadelete_min_write_back_mem.key;
                remove_min_from_smallest_buffer(key_write_back, &val);
            }
            atomic_store_explicit(&fpadelete_min_write_back_mem.response, -1, memory_order_relaxed);
            critical_exit();
	    fpahelp_info.remove_min_contention+=REMOVE_MIN_HIGH_CONTENTION_INCREASE;
            if(put_relax){
                adjust_put_buffer();
            }
            return val;
        }
    } while(retry);
    if(contended){
      //printf("R STACK POS %d\n", fpahelp_info.flush_stack_pos);
      LL_open_delegation_queue(&base_node->lock.lock);
      fpahelp_info.flush_stack[fpahelp_info.flush_stack_pos] = current_node;
      fpahelp_info.flush_stack_pos++;
    }else{
      //printf("NOT CONT\n");
      fpahelp_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    }
    fpahelp_info.set = set;
    fpahelp_info.current_help_node = current_node;
    fpahelp_info.last_locked_node = current_node;
    fpahelp_info.last_locked_node_parent = prev_node; 
    if(contended){
      fpahelp_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
      int current_stack_pos = 0;
      while(current_stack_pos < SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE &&
	    current_stack_pos < fpahelp_info.flush_stack_pos){
	CATreeBaseNode * n =&fpahelp_info.flush_stack[current_stack_pos]->baseOrRoute.base;
	LL_flush_delegation_queue(&n->lock.lock);
	//printf("FR STACK POS %d\n", current_stack_pos);
	current_stack_pos++;
	if(current_stack_pos<fpahelp_info.flush_stack_pos){
	  DEBUG_PRINT(("not same as last node min unlock %p\n", current_node));
	  LL_unlock(&n->lock.lock);
	}
      }
      fpahelp_info.remove_min_contention+=REMOVE_MIN_HIGH_CONTENTION_INCREASE;
    }else{
      fpahelp_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
      fpahelp_info.remove_min_contention-=REMOVE_MIN_LOW_CONTENTION_DECREASE;
    }
      /* if(fpahelp_info.last_locked_node != fpahelp_info.current_help_node){ */
      /*   DEBUG_PRINT(("not same as last node min unlock %p\n", current_node)); */
      /*   LL_unlock(&base_node->lock.lock); */
      /* } */
    fpahelp_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
    perform_remove_min_with_lock(&fpadelete_min_write_back_mem);
    if(atomic_load_explicit(&fpadelete_min_write_back_mem.response, memory_order_relaxed) == 0){
        *key_write_back = fpadelete_min_write_back_mem.key;
        val = fpadelete_min_write_back_mem.value;
    }else{
        // node response
        fpahelp_info.del_min_buffer = (SkiplistNode*)fpadelete_min_write_back_mem.key;
        remove_min_from_smallest_buffer(key_write_back, &val);
    }
    atomic_store_explicit(&fpadelete_min_write_back_mem.response, -1, memory_order_relaxed);
    fpahelp_info.flush_stack_pos = 0;
    if(remove_min_relax){
        adjust_remove_min_relaxation();
    }
    adaptAndUnlock(set,
                   fpahelp_info.last_locked_node,
                   fpahelp_info.last_locked_node_parent,
                   catree_adapt);
    critical_exit();
    if(put_relax){
        adjust_put_buffer();
    }
    return val;
}

unsigned long fpaslqdcatree_remove_min(FPACATreeSet * set,
                                             unsigned long * key_write_back){
    return fpaslqdcatree_remove_min_param(set,
                                          key_write_back,
                                          true,
                                          true,
                                          true);
}

typedef struct {
    unsigned long key;
    unsigned long value;
} put_message;

static inline void delegate_perform_put_with_lock(unsigned int msgSize, void * msgParam){
    put_message * msg = msgParam;
    skiplist_put(fpahelp_info.last_locked_node->baseOrRoute.base.root, msg->key, msg->value);
    fpahelp_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
}

void fpaslqdcatree_put_param(FPACATreeSet * set,
                             unsigned long key,
                             unsigned long value,
                             bool catree_adapt){
    if(push(&fpahelp_info.put_buffer, key, value)){
        //printf("%d ", fpahelp_info.max_buffered_puts);
        return;
    }
    critical_enter();
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
        critical_exit();
        fpahelp_info.put_contention+=PUT_BUFF_HIGH_CONTENTION_INCREASE;
        return;
    }
    if(contended){
      //printf("P STACK POS %d\n", fpahelp_info.flush_stack_pos);
      //fflush(stdout);
      LL_open_delegation_queue(&node->lock.lock);
      fpahelp_info.flush_stack[fpahelp_info.flush_stack_pos] = currentNode;
      fpahelp_info.flush_stack_pos++;
      fpahelp_info.put_contention+=PUT_BUFF_HIGH_CONTENTION_INCREASE;
    }else{
      fpahelp_info.flush_stack_pos = SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE + 1;
      fpahelp_info.put_contention-=PUT_BUFF_LOW_CONTENTION_DECREASE;
    }
    fpahelp_info.set = set;
    fpahelp_info.current_help_node = currentNode;
    fpahelp_info.last_locked_node = currentNode;
    fpahelp_info.last_locked_node_parent = prevNode;
    skiplist_put(node->root, key, value);

    if(contended){
      fpahelp_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_FAILURE_STATS_CONTRIB;
      int current_stack_pos = 0;
      while(current_stack_pos < SKIPLIST_QDCATREE_MAX_FLUSH_STACK_SIZE &&
	    current_stack_pos < fpahelp_info.flush_stack_pos){
	//printf("PF STACK POS %d\n", current_stack_pos);
	CATreeBaseNode * n =&fpahelp_info.flush_stack[current_stack_pos]->baseOrRoute.base;
	LL_flush_delegation_queue(&n->lock.lock);
	current_stack_pos++;
	if(current_stack_pos<fpahelp_info.flush_stack_pos){
	  DEBUG_PRINT(("not same as last node min unlock %p\n", current_node));
	  LL_unlock(&n->lock.lock);
	}
      }
    }else{
      fpahelp_info.last_locked_node->baseOrRoute.base.lock.statistics += SLCATREE_LOCK_SUCCESS_STATS_CONTRIB;
    }
    /* if(fpahelp_info.last_locked_node != fpahelp_info.current_help_node){ */
    /*     DEBUG_PRINT(("not same as last node min unlock %p\n", currentNode)); */
    /*     LL_unlock(&node->lock.lock); */
    /* } */
    fpahelp_info.flush_stack_pos = 0;    
    adaptAndUnlock(set,
                   fpahelp_info.last_locked_node,
                   fpahelp_info.last_locked_node_parent,
                   catree_adapt);
    critical_exit();
    return;
}


void fpaslqdcatree_put(FPACATreeSet * set,
                       unsigned long key,
                       unsigned long value){
    fpaslqdcatree_put_param(set,
                            key,
                            value,
                            true);
}

void fpaslqdcatree_delete(FPACATreeSet * setParam){
    slcatree_set_destroy(setParam);
    SLCATREE_FREE(setParam);
}


void fpaslqdcatree_put_flush(FPASLCATreeSet * set){
    //Not defined yet
}

FPACATreeSet * fpaslqdcatree_new(){
    FPACATreeSet * set = SLCATREE_MALLOC(sizeof(FPACATreeSet));
    plain_slcatree_set_init(set);
    return set;
}

/* int main(){ */
/*     printf("THIS IS A TEST FUNCTION 2\n"); */
/*     FPACATreeSet * l =  slcatree_new(); */
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
