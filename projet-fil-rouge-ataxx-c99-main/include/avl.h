#ifndef AVL_H
#define AVL_H

#include "common.h"

typedef struct AvlNode {
    uint64_t key;
    int value;
    int height;
    struct AvlNode *left;
    struct AvlNode *right;
} AvlNode;

typedef struct AvlTree {
    AvlNode *root;
    size_t size;
} AvlTree;

void avl_init(AvlTree *tree);
void avl_destroy(AvlTree *tree);
bool avl_insert(AvlTree *tree, uint64_t key, int value);
bool avl_find(const AvlTree *tree, uint64_t key, int *value);
bool avl_validate(const AvlTree *tree);

#endif