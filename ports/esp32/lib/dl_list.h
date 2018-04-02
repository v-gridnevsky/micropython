#include <stdbool.h>

struct List {
   struct MNode *head;
   struct MNode *tail;
   struct MNode *tail_pred;
};

struct MNode {
   struct MNode *succ;
   struct MNode *pred;
};

typedef struct MNode *NODE;
typedef struct List *DL_LIST;

/*
** DL_LIST l = newList()
** create (alloc space for) and initialize a list
*/
DL_LIST newList(void);

/*
** bool dlListIsEmpty(DL_LIST l)
** test if a list is empty
*/
bool dlListIsEmpty(DL_LIST);

/*
** NODE n = getTail(DL_LIST l)
** get the tail node of the list, without removing it
*/
NODE getTail(DL_LIST);

/*
** NODE n = getHead(DL_LIST l)
** get the head node of the list, without removing it
*/
NODE getHead(DL_LIST);

/*
** NODE rn = addTail(DL_LIST l, NODE n)
** add the node n to the tail of the list l, and return it (rn==n)
*/
NODE addTail(DL_LIST, NODE);

/*
** NODE rn = addHead(DL_LIST l, NODE n)
** add the node n to the head of the list l, and return it (rn==n)
*/
NODE addHead(DL_LIST, NODE);

/*
** NODE n = remHead(DL_LIST l)
** remove the head node of the list and return it
*/
NODE remHead(DL_LIST);

/*
** NODE n = remTail(DL_LIST l)
** remove the tail node of the list and return it
*/
NODE remTail(DL_LIST);

/*
** NODE rn = insertAfter(DL_LIST l, NODE r, NODE n)
** insert the node n after the node r, in the list l; return n (rn==n)
*/
NODE insertAfter(DL_LIST, NODE, NODE);

/*
** NODE rn = removeNode(DL_LIST l, NODE n)
** remove the node n (that must be in the list l) from the list and return it (rn==n)
*/
NODE removeNode(DL_LIST, NODE);

int countNodes(DL_LIST list);