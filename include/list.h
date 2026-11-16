#ifndef _LIST_H
#define _LIST_H
#include <diag.h>
typedef struct List List;
struct List
{
  List *prev, *next;
};
INLINE void
listinit(List *node)
{
  node->prev = node->next = node;
}
INLINE int
listempty(const List *head)
{
  return head->next == head;
}
INLINE void
listdel(List *node)
{
  if (listempty(node) || node->prev->next != node || node->next->prev != node)
    panic("invalid list removal");
  node->prev->next = node->next;
  node->next->prev = node->prev;
  listinit(node);
}
INLINE void
listinsert(List *before, List *after, List *node)
{
  if (node == before || node == after || !listempty(node) || node->prev != node ||
      before->next != after || after->prev != before)
    panic("invalid list insertion");
  node->prev = before;
  node->next = after;
  before->next = node;
  after->prev = node;
}
INLINE void
listhead(List *head, List *node)
{
  listinsert(head, head->next, node);
}
INLINE void
listtail(List *head, List *node)
{
  listinsert(head->prev, head, node);
}
INLINE void
listmove(List *head, List *node)
{
  if (head == node)
    panic("list head moved into itself");
  listdel(node);
  listtail(head, node);
}
#define listforeach(node, head) for ((node) = (head)->next; (node) != (head); (node) = (node)->next)
#define listforeachsafe(node, saved, head)                                                         \
  for ((node) = (head)->next, (saved) = (node)->next; (node) != (head);                            \
       (node) = (saved), (saved) = (node)->next)
#endif
