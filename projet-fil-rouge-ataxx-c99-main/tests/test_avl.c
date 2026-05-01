#include "avl.h"

#include <assert.h>

int main(void)
{
  AvlTree tree;
  int value = 0;

  avl_init(&tree);

  assert(avl_insert(&tree, 30u, 1));
  assert(avl_insert(&tree, 20u, 2));
  assert(avl_insert(&tree, 10u, 3));
  assert(avl_insert(&tree, 25u, 4));
  assert(avl_insert(&tree, 40u, 5));
  assert(avl_insert(&tree, 50u, 6));
  assert(avl_validate(&tree));

  assert(avl_find(&tree, 25u, &value));
  assert(value == 4);
  assert(!avl_find(&tree, 99u, &value));

  avl_destroy(&tree);
  return 0;
}