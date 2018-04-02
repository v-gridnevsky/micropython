#include <stdlib.h>
#include <stdbool.h>
#include "dl_list.h"

DL_LIST newList(void)
{
    DL_LIST tl = malloc(sizeof(struct List));
    if ( tl != NULL )
    {
       tl->tail_pred = (NODE)&tl->head;
       tl->tail = NULL;
       tl->head = (NODE)&tl->tail;
       return tl;
    }
    return NULL;
}

bool dlListIsEmpty(DL_LIST l)
{
   return (l->head->succ == 0);
}

NODE getHead(DL_LIST l)
{
  return l->head;
}

NODE getTail(DL_LIST l)
{
  return l->tail_pred;
}


NODE addTail(DL_LIST l, NODE n)
{
    n->succ = (NODE)&l->tail;
    n->pred = l->tail_pred;
    l->tail_pred->succ = n;
    l->tail_pred = n;
    return n;
}

NODE addHead(DL_LIST l, NODE n)
{
    n->succ = l->head;
    n->pred = (NODE)&l->head;
    l->head->pred = n;
    l->head = n;
    return n;
}

NODE remHead(DL_LIST l)
{
   NODE h;
   h = l->head;
   l->head = l->head->succ;
   l->head->pred = (NODE)&l->head;
   return h;
}

NODE remTail(DL_LIST l)
{
   NODE t;
   t = l->tail_pred;
   l->tail_pred = l->tail_pred->pred;
   l->tail_pred->succ = (NODE)&l->tail;
   return t;
}

NODE insertAfter(DL_LIST l, NODE r, NODE n)
{
   n->pred = r; n->succ = r->succ;
   n->succ->pred = n; r->succ = n;
   return n;
}

NODE removeNode(DL_LIST l, NODE n)
{
   n->pred->succ = n->succ;
   n->succ->pred = n->pred;
   return n;
}

int countNodes(DL_LIST list) {
    int nodeCount = 0;
    struct IntNode *m;
    NODE l = getHead(list);
    NODE succ;
    if ( list != NULL ) {
        m = (struct IntNode *)l;
        for(;;) {
            succ = l->succ;
            m = (struct IntNode *)l;
            // Break at the end of iteration
            if (succ==0) break;
            l = succ;
            nodeCount++;
        }
    }
    return nodeCount;
}