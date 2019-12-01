#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/private/x_bst.h"

namespace xcore
{
    namespace pointer_based
    {
        bool find(pnode& root, tree_t* tree, pnode& found)
        {
            bool ret = true;

            ASSERT(tree != NULL);
            ASSERT(value != NULL);

            pnode = NULL;
            if (root == NULL)
            {
                ret = false;
                goto done;
            }

            pnode node = root;
            while (node != NULL)
            {
                s32 compare = tree->m_compare_f(tree, key, node->key);

                if (compare < 0) {
                    node = node->left;
                } else if (compare == 0) {
                    break; /* We found our node */
                } else {
                    /* Otherwise, we want the right node, and continue iteration */
                    node = node->right;
                }
            }

            if (node == NULL) {
                ret = RB_NOT_FOUND;
                goto done;
            }

            /* Return the node we found */
            *value = node;

        done:
            return ret;
        }
    }    
}