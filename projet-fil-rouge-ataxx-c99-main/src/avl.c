#include "avl.h"

#include <stdlib.h>

static int node_height(const AvlNode *node) {
    return node == NULL ? 0 : node->height;
}

static int max_int(int left, int right) {
    return left > right ? left : right;
}

static void update_height(AvlNode *node) {
    node->height = 1 + max_int(node_height(node->left), node_height(node->right));
}

static int balance_factor(const AvlNode *node) {
    return node == NULL ? 0 : node_height(node->left) - node_height(node->right);
}

static AvlNode *rotate_right(AvlNode *node) {
    AvlNode *new_root = node->left;
    node->left = new_root->right;
    new_root->right = node;
    update_height(node);
    update_height(new_root);
    return new_root;
}

static AvlNode *rotate_left(AvlNode *node) {
    AvlNode *new_root = node->right;
    node->right = new_root->left;
    new_root->left = node;
    update_height(node);
    update_height(new_root);
    return new_root;
}

static AvlNode *rebalance(AvlNode *node) {
    int balance;
    update_height(node);
    balance = balance_factor(node);

    if (balance > 1) {
        if (balance_factor(node->left) < 0) {
            node->left = rotate_left(node->left);
        }
        return rotate_right(node);
    }

    if (balance < -1) {
        if (balance_factor(node->right) > 0) {
            node->right = rotate_right(node->right);
        }
        return rotate_left(node);
    }

    return node;
}

static AvlNode *create_node(uint64_t key, int value) {
    AvlNode *node = (AvlNode *)malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }
    node->key = key;
    node->value = value;
    node->height = 1;
    node->left = NULL;
    node->right = NULL;
    return node;
}

static void destroy_node(AvlNode *node) {
    if (node == NULL) {
        return;
    }
    destroy_node(node->left);
    destroy_node(node->right);
    free(node);
}

static AvlNode *insert_node(AvlNode *node, uint64_t key, int value, bool *inserted) {
    if (node == NULL) {
        *inserted = true;
        return create_node(key, value);
    }

    if (key < node->key) {
        node->left = insert_node(node->left, key, value, inserted);
    } else if (key > node->key) {
        node->right = insert_node(node->right, key, value, inserted);
    } else {
        node->value = value;
        *inserted = false;
        return node;
    }

    return rebalance(node);
}

static bool validate_node(const AvlNode *node, uint64_t *min_key, uint64_t *max_key, int *height) {
    uint64_t left_min = 0;
    uint64_t left_max = 0;
    uint64_t right_min = 0;
    uint64_t right_max = 0;
    int left_height = 0;
    int right_height = 0;

    if (node == NULL) {
        *height = 0;
        return true;
    }

    if (!validate_node(node->left, &left_min, &left_max, &left_height)) {
        return false;
    }
    if (!validate_node(node->right, &right_min, &right_max, &right_height)) {
        return false;
    }
    if (node->left != NULL && left_max >= node->key) {
        return false;
    }
    if (node->right != NULL && right_min <= node->key) {
        return false;
    }
    if (abs(left_height - right_height) > 1) {
        return false;
    }
    if (node->height != 1 + max_int(left_height, right_height)) {
        return false;
    }

    *min_key = node->left == NULL ? node->key : left_min;
    *max_key = node->right == NULL ? node->key : right_max;
    *height = node->height;
    return true;
}

void avl_init(AvlTree *tree) {
    tree->root = NULL;
    tree->size = 0;
}

void avl_destroy(AvlTree *tree) {
    destroy_node(tree->root);
    tree->root = NULL;
    tree->size = 0;
}

bool avl_insert(AvlTree *tree, uint64_t key, int value) {
    bool inserted = false;
    tree->root = insert_node(tree->root, key, value, &inserted);
    if (tree->root == NULL) {
        return false;
    }
    if (inserted) {
        ++tree->size;
    }
    return true;
}

bool avl_find(const AvlTree *tree, uint64_t key, int *value) {
    const AvlNode *node = tree->root;
    while (node != NULL) {
        if (key < node->key) {
            node = node->left;
        } else if (key > node->key) {
            node = node->right;
        } else {
            if (value != NULL) {
                *value = node->value;
            }
            return true;
        }
    }
    return false;
}

bool avl_validate(const AvlTree *tree) {
    uint64_t min_key = 0;
    uint64_t max_key = 0;
    int height = 0;
    return validate_node(tree->root, &min_key, &max_key, &height);
}