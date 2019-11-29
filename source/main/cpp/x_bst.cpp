#include "xbase/x_target.h"
#include "xbase/x_debug.h"
#include "xbase/x_allocator.h"

#include "xvmem/private/x_bst.h"

namespace xcore
{
    namespace bst_ptr
    {
        // This red-black tree node uses pointers to reference nodes, this
        // results in a node sizeof 8 bytes in 32-bit and 16 bytes in 64-bit.
        struct xnode
        {
        public:
            enum ESide
            {
                LEFT = 0x0,
                RIGHT = 0x1
            };
            enum EColor
            {
                BLACK = 0x0,
                RED = 0x1,
                COLOR_MASK = 0x1
            };

        private:
            static inline xnode *get_ptr(xnode *p, uptr m)
            {
                return (xnode *) ((uptr) p & ~m);
            }
            static inline xnode *set_ptr(xnode *p, xnode *n, uptr m)
            {
                return (xnode *) (((uptr) p & m) | ((uptr) n & ~m));
            }

            static inline uptr get_bit(xnode *p, uptr b)
            {
                return (uptr) p & b;
            }
            static inline xnode *set_bit(xnode *p, uptr m, uptr b)
            {
                return (xnode *) (((uptr) p & ~m) | b);
            }

        public:
            inline void clear()
            {
                left = (xnode *) RED;
                right = nullptr;
            }

            inline void set_child(xnode *node, s32 dir)
            {
                switch (dir)
                {
                case LEFT:
                    left = set_ptr(left, node, COLOR_MASK);
                    break;
                case RIGHT:
                    right = node;
                    break;
                }
            }
            inline xnode *get_child(s32 dir) const
            {
                switch (dir)
                {
                case LEFT:
                    return get_ptr(left, COLOR_MASK);
                }
                return right;
            }

            inline void set_right(xnode *node) { right = node; }
            inline xnode *get_right() const { return right; }

            inline void set_left(xnode *node)
            {
                left = set_ptr(left, node, COLOR_MASK);
            }
            inline xnode *get_left() const { return get_ptr(left, COLOR_MASK); }

            inline s32 get_side(xnode *node)
            {
                ASSERT(node == get_left() || node == get_right());
                return right == node ? RIGHT : LEFT;
            }

            inline void set_red() { left = set_bit(left, COLOR_MASK, RED); }
            inline void set_black() { left = set_bit(left, COLOR_MASK, BLACK); }
            inline void set_color(s32 colr)
            {
                ASSERT(colr == RED || colr == BLACK);
                left = set_bit(left, COLOR_MASK, colr);
            }
            inline s32 get_color() const
            {
                s32 colr = (s32) get_bit(left, COLOR_MASK);
                ASSERT(colr == RED || colr == BLACK);
                return colr;
            }
            inline bool is_red() const { return get_bit(left, COLOR_MASK) == RED; }
            inline bool is_black() const { return get_bit(left, COLOR_MASK) == BLACK; }

            XCORE_CLASS_PLACEMENT_NEW_DELETE

        private:
            xnode *left, *right;
        };

        inline s32 rb_flip_dir(s32 dir)
        {
            ASSERT(xnode::RIGHT == 1);
            ASSERT(dir == xnode::RIGHT || dir == xnode::LEFT);
            return dir ^ xnode::RIGHT;
        }

        inline bool rb_is_red(xnode *node)
        {
            return node != nullptr ? node->is_red() : xnode::BLACK;
        }

        struct rb_iterator
        {
            inline rb_iterator() : node(nullptr), top(0) {}
            inline rb_iterator(xnode *n) : node(n), top(0) {}

            enum EType
            {
                FORWARDS = 1,
                BACKWARDS = 0,
                MINIMUM = 0,
                MAXIMUM = 1,
                MAX_HEIGHT = 64
            };

            xnode *init(xnode *root, EType dir)
            {
                xnode *result = nullptr;
                if (root)
                {
                    node = root;
                    top = 0;

                    if (node != nullptr)
                    { // Save the path for later traversal
                        while (node->get_child(dir) != nullptr)
                        {
                            push(node);
                            node = node->get_child(dir);
                        }
                    }
                    result = node;
                }
                return result;
            }

            xnode *move(EType dir)
            {
                if (node->get_child(dir) != nullptr)
                { // Continue down this branch
                    push(node);
                    node = node->get_child(dir);
                    while (node->get_child(!dir) != nullptr)
                    {
                        push(node);
                        node = node->get_child(!dir);
                    }
                }
                else
                { // Move to the next branch
                    xnode *last = nullptr;
                    do
                    {
                        if (top == 0)
                        {
                            node = nullptr;
                            break;
                        }
                        last = node;
                        node = pop();
                    } while (last == node->get_child(dir));
                }
                return node;
            }

        private:
            void push(xnode *node)
            {
                ASSERT(top < MAX_HEIGHT);
                path[top++] = node;
            }
            xnode *pop()
            {
                ASSERT(top > 0);
                return path[--top];
            }

            xnode *node;
            s32 top;
            xnode *path[MAX_HEIGHT];
        };

        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        inline xnode *rb_rotate(xnode *self, s32 dir)
        {
            xnode *result = nullptr;
            if (self)
            {
                xnode *rchild;

                s32 const sdir = rb_flip_dir(dir);

                result = self->get_child(sdir);
                rchild = result->get_child(dir);

                self->set_child(rchild, sdir);
                result->set_child(self, dir);

                self->set_red();
                result->set_black();
            }
            return result;
        }

        inline xnode *rb_rotate2(xnode *self, s32 dir)
        {
            xnode *result = nullptr;
            if (self)
            {
                s32 const sdir = rb_flip_dir(dir);

                xnode *child = rb_rotate(self->get_child(sdir), sdir);
                self->set_child(child, sdir);
                result = rb_rotate(self, dir);
            }
            return result;
        }

        typedef s32 (*xnode_cmp_f)(xnode *a, xnode *b);
        typedef void (*xnode_remove_f)(xnode *a, xnode *b);

        // Test the integrity of the red-black tree
        // @return: The depth of the tree
        // @result: If any error it returns a description of the error in 'result', when
        // no error it will be NULL
        inline s32 rb_tree_test(xnode *root, xnode_cmp_f cmp_f, const char *&result)
        {
            s32 lh, rh;
            if (root == nullptr)
            {
                return 1;
            }
            else
            {
                xnode *ln = root->get_left();
                xnode *rn = root->get_right();

                // Consecutive red links
                if (rb_is_red(root))
                {
                    if (rb_is_red(ln) || rb_is_red(rn))
                    {
                        result = "Red violation";
                        return 0;
                    }
                }

                lh = rb_tree_test(ln, cmp_f, result);
                rh = rb_tree_test(rn, cmp_f, result);

                // Invalid binary search tree
                if ((ln != nullptr && cmp_f(ln, root) >= 0)
                    || (rn != nullptr && cmp_f(rn, root) <= 0))
                {
                    result = "Binary tree violation";
                    return 0;
                }

                // Black height mismatch
                if (lh != 0 && rh != 0 && lh != rh)
                {
                    result = "Black violation";
                    return 0;
                }

                // Only count black links
                if (lh != 0 && rh != 0)
                {
                    return rb_is_red(root) ? lh : lh + 1;
                }
                return 0;
            }
        }

        // Returns 1 on success, 0 otherwise.
        inline bool rb_insert_node(xnode *&root, xnode *node, xnode_cmp_f cmp_f)
        {
            bool result = false;
            if (node)
            {
                if (root == nullptr)
                {
                    root = node;
                    result = true;
                }
                else
                {
                    xnode head; // False tree root
                    head.clear();

                    s32 dir = 0;
                    s32 lastdir = 0;

                    // Set up our helpers
                    xnode *g = nullptr; // Grandparent
                    xnode *t = &head;   // Parent
                    xnode *p = nullptr; // Parent
                    xnode *q = root;    // Iterator

                    t->set_child(root, xnode::RIGHT);

                    // Search down the tree for a place to insert
                    result = false;
                    while (true)
                    {
                        if (q == nullptr)
                        { // Insert node at the first null link.
                            q = node;
                            result = true;
                            p->set_child(q, dir);
                        }
                        else if (rb_is_red(q->get_left()) && rb_is_red(q->get_right()))
                        { // Simple red violation: color flip
                            q->set_red();
                            q->get_left()->set_black();
                            q->get_right()->set_black();
                        }

                        if (rb_is_red(q) && rb_is_red(p))
                        { // Hard red violation: rotations necessary
                            s32 const dir2 =
                                (t->get_right() == g) ? xnode::RIGHT : xnode::LEFT;
                            if (q == p->get_child(lastdir))
                            {
                                xnode *r = rb_rotate(g, !lastdir);
                                t->set_child(r, dir2);
                            }
                            else
                            {
                                xnode *r = rb_rotate2(g, !lastdir);
                                t->set_child(r, dir2);
                            }
                        }

                        // Stop working if we inserted a node.
                        // This check also disallows duplicates in the tree
                        s32 const c = cmp_f(node, q);
                        if (c == 0)
                            break;

                        lastdir = dir;
                        dir = c < 0 ? xnode::LEFT : xnode::RIGHT;

                        // Move the helpers down
                        if (g != nullptr)
                        {
                            t = g;
                        }

                        g = p;
                        p = q;
                        q = q->get_child(dir);
                    }
                    // Update the root (it may be different)
                    root = head.get_child(xnode::RIGHT);
                }
                root->set_black(); // Make the root black for simplified logic
            }
            return result;
        }

        // Returns 1 if the value was removed, 0 otherwise. Optional node callback
        // can be provided to dealloc node and/or user data. Use rb_tree_node_dealloc
        // default callback to deallocate node created by rb_tree_insert(...).
        inline bool rb_remove_node(xnode *&root, xnode *find, xnode_cmp_f cmp_f, xnode_remove_f remove_f, xnode *&outnode)
        {
            if (root != nullptr)
            {
                xnode head; // False tree root
                head.clear();

                // Set up our helpers
                xnode *q = &head;
                xnode *g = nullptr;
                xnode *p = nullptr;
                xnode *f = nullptr; // Found item
                q->set_child(root, xnode::RIGHT);

                // Search and push a red node down to fix red violations as we go
                s32 dir = xnode::RIGHT;
                while (q->get_child(dir) != nullptr)
                {
                    s32 const lastdir = dir;

                    // Move the helpers down
                    g = p;
                    p = q;
                    q = q->get_child(dir);

                    s32 const c = cmp_f(find, q);

                    // Save the node with matching value and keep
                    // going; we'll do removal tasks at the end
                    if (c == 0)
                    {
                        f = q;
                    }

                    dir = (c < 0) ? xnode::LEFT : xnode::RIGHT;

                    // Push the red node down with rotations and color flips
                    if (!rb_is_red(q) && !rb_is_red(q->get_child(dir)))
                    {
                        if (rb_is_red(q->get_child(!dir)))
                        {
                            xnode *r = rb_rotate(q, dir);
                            p->set_child(r, lastdir);
                            p = r;
                        }
                        else if (!rb_is_red(q->get_child(!dir)))
                        {
                            xnode *s = p->get_child(!lastdir);
                            if (s)
                            {
                                if (!rb_is_red(s->get_child(!lastdir))
                                    && !rb_is_red(s->get_child(lastdir)))
                                { // Color flip
                                    p->set_black();
                                    s->set_red();
                                    q->set_red();
                                }
                                else
                                {
                                    s32 dir2 = g->get_child(1) == p;

                                    if (rb_is_red(s->get_child(lastdir)))
                                    {
                                        xnode *r = rb_rotate2(p, lastdir);
                                        g->set_child(r, dir2);
                                    }
                                    else if (rb_is_red(s->get_child(!lastdir)))
                                    {
                                        xnode *r = rb_rotate(p, lastdir);
                                        g->set_child(r, dir2);
                                    }

                                    // Ensure correct coloring
                                    s = g->get_child(dir2);
                                    s->set_red();
                                    q->set_red();
                                    s->get_left()->set_black();
                                    s->get_right()->set_black();
                                }
                            }
                        }
                    }
                }

                // Replace and remove the saved node
                if (f != nullptr)
                {
                    remove_f(f, q);

                    s32 const ps = p->get_child(xnode::RIGHT) == q ? xnode::RIGHT : xnode::LEFT;
                    s32 const qs = q->get_child(xnode::LEFT) == nullptr ? xnode::RIGHT : xnode::LEFT;
                    p->set_child(q->get_child(qs), ps);

                    // Give the 'removed' node back
                    q->set_left(nullptr);
                    q->set_right(nullptr);
                    outnode = q;
                }

                // Update the root (it may be different)
                root = head.get_child(xnode::RIGHT);

                // Make the root black for simplified logic
                if (root != nullptr)
                {
                    root->set_black();
                }

                return f != nullptr;
            }
            return true;
        }

        inline xnode *xtree_clear(xnode *&iterator)
        {
            //	Rotate away the left links so that
            //	we can treat this like the destruction
            //	of a linked list
            xnode *it = iterator;
            iterator = nullptr;

            while (it != nullptr)
            {
                if (it->get_child(xnode::LEFT) == nullptr)
                { // No left links, just kill the node and move on
                    iterator = (xnode *) it->get_child(xnode::RIGHT);
                    it->clear();
                    return it;
                }
                else
                { // Rotate away the left link and check again
                    iterator = (xnode *) it->get_child(xnode::LEFT);
                    it->set_child(iterator->get_child(xnode::RIGHT), xnode::LEFT);
                    iterator->set_child(it, xnode::RIGHT);
                }
                it = iterator;
                iterator = nullptr;
            }
            return nullptr;
        }
    }

    namespace bst_idx
    {
        // This red-black tree node uses pointers to reference nodes, this
        // results in a node sizeof 8 bytes in 32-bit and 16 bytes in 64-bit.
        struct xnode
        {
        public:
            enum ESide
            {
                LEFT = 0x0,
                RIGHT = 0x1
            };
            enum EColor
            {
                BLACK = 0x0,
                RED = 0x1,
                COLOR_MASK = 0x1
            };

        private:
            static inline xnode *get_ptr(xnode *p, uptr m)
            {
                return (xnode *) ((uptr) p & ~m);
            }
            static inline xnode *set_ptr(xnode *p, xnode *n, uptr m)
            {
                return (xnode *) (((uptr) p & m) | ((uptr) n & ~m));
            }

            static inline uptr get_bit(xnode *p, uptr b)
            {
                return (uptr) p & b;
            }
            static inline xnode *set_bit(xnode *p, uptr m, uptr b)
            {
                return (xnode *) (((uptr) p & ~m) | b);
            }

        public:
            inline void clear()
            {
                left = (xnode *) RED;
                right = nullptr;
            }

            inline void set_child(xnode *node, s32 dir)
            {
                switch (dir)
                {
                case LEFT:
                    left = set_ptr(left, node, COLOR_MASK);
                    break;
                case RIGHT:
                    right = node;
                    break;
                }
            }
            inline xnode *get_child(s32 dir) const
            {
                switch (dir)
                {
                case LEFT:
                    return get_ptr(left, COLOR_MASK);
                }
                return right;
            }

            inline void set_right(xnode *node) { right = node; }
            inline xnode *get_right() const { return right; }

            inline void set_left(xnode *node)
            {
                left = set_ptr(left, node, COLOR_MASK);
            }
            inline xnode *get_left() const { return get_ptr(left, COLOR_MASK); }

            inline s32 get_side(xnode *node)
            {
                ASSERT(node == get_left() || node == get_right());
                return right == node ? RIGHT : LEFT;
            }

            inline void set_red() { left = set_bit(left, COLOR_MASK, RED); }
            inline void set_black() { left = set_bit(left, COLOR_MASK, BLACK); }
            inline void set_color(s32 colr)
            {
                ASSERT(colr == RED || colr == BLACK);
                left = set_bit(left, COLOR_MASK, colr);
            }
            inline s32 get_color() const
            {
                s32 colr = (s32) get_bit(left, COLOR_MASK);
                ASSERT(colr == RED || colr == BLACK);
                return colr;
            }
            inline bool is_red() const { return get_bit(left, COLOR_MASK) == RED; }
            inline bool is_black() const { return get_bit(left, COLOR_MASK) == BLACK; }

            XCORE_CLASS_PLACEMENT_NEW_DELETE

        private:
            xnode *left, *right;
        };

        inline s32 rb_flip_dir(s32 dir)
        {
            ASSERT(xnode::RIGHT == 1);
            ASSERT(dir == xnode::RIGHT || dir == xnode::LEFT);
            return dir ^ xnode::RIGHT;
        }

        inline bool rb_is_red(xnode *node)
        {
            return node != nullptr ? node->is_red() : xnode::BLACK;
        }

        struct rb_iterator
        {
            inline rb_iterator() : node(nullptr), top(0) {}
            inline rb_iterator(xnode *n) : node(n), top(0) {}

            enum EType
            {
                FORWARDS = 1,
                BACKWARDS = 0,
                MINIMUM = 0,
                MAXIMUM = 1,
                MAX_HEIGHT = 64
            };

            xnode *init(xnode *root, EType dir)
            {
                xnode *result = nullptr;
                if (root)
                {
                    node = root;
                    top = 0;

                    if (node != nullptr)
                    { // Save the path for later traversal
                        while (node->get_child(dir) != nullptr)
                        {
                            push(node);
                            node = node->get_child(dir);
                        }
                    }
                    result = node;
                }
                return result;
            }

            xnode *move(EType dir)
            {
                if (node->get_child(dir) != nullptr)
                { // Continue down this branch
                    push(node);
                    node = node->get_child(dir);
                    while (node->get_child(!dir) != nullptr)
                    {
                        push(node);
                        node = node->get_child(!dir);
                    }
                }
                else
                { // Move to the next branch
                    xnode *last = nullptr;
                    do
                    {
                        if (top == 0)
                        {
                            node = nullptr;
                            break;
                        }
                        last = node;
                        node = pop();
                    } while (last == node->get_child(dir));
                }
                return node;
            }

        private:
            void push(xnode *node)
            {
                ASSERT(top < MAX_HEIGHT);
                path[top++] = node;
            }
            xnode *pop()
            {
                ASSERT(top > 0);
                return path[--top];
            }

            xnode *node;
            s32 top;
            xnode *path[MAX_HEIGHT];
        };

        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        inline xnode *rb_rotate(xnode *self, s32 dir)
        {
            xnode *result = nullptr;
            if (self)
            {
                xnode *rchild;

                s32 const sdir = rb_flip_dir(dir);

                result = self->get_child(sdir);
                rchild = result->get_child(dir);

                self->set_child(rchild, sdir);
                result->set_child(self, dir);

                self->set_red();
                result->set_black();
            }
            return result;
        }

        inline xnode *rb_rotate2(xnode *self, s32 dir)
        {
            xnode *result = nullptr;
            if (self)
            {
                s32 const sdir = rb_flip_dir(dir);

                xnode *child = rb_rotate(self->get_child(sdir), sdir);
                self->set_child(child, sdir);
                result = rb_rotate(self, dir);
            }
            return result;
        }

        typedef s32 (*xnode_cmp_f)(xnode *a, xnode *b);
        typedef void (*xnode_remove_f)(xnode *a, xnode *b);

        // Test the integrity of the red-black tree
        // @return: The depth of the tree
        // @result: If any error it returns a description of the error in 'result', when
        // no error it will be NULL
        inline s32 rb_tree_test(xnode *root, xnode_cmp_f cmp_f, const char *&result)
        {
            s32 lh, rh;
            if (root == nullptr)
            {
                return 1;
            }
            else
            {
                xnode *ln = root->get_left();
                xnode *rn = root->get_right();

                // Consecutive red links
                if (rb_is_red(root))
                {
                    if (rb_is_red(ln) || rb_is_red(rn))
                    {
                        result = "Red violation";
                        return 0;
                    }
                }

                lh = rb_tree_test(ln, cmp_f, result);
                rh = rb_tree_test(rn, cmp_f, result);

                // Invalid binary search tree
                if ((ln != nullptr && cmp_f(ln, root) >= 0)
                    || (rn != nullptr && cmp_f(rn, root) <= 0))
                {
                    result = "Binary tree violation";
                    return 0;
                }

                // Black height mismatch
                if (lh != 0 && rh != 0 && lh != rh)
                {
                    result = "Black violation";
                    return 0;
                }

                // Only count black links
                if (lh != 0 && rh != 0)
                {
                    return rb_is_red(root) ? lh : lh + 1;
                }
                return 0;
            }
        }

        // Returns 1 on success, 0 otherwise.
        inline bool rb_insert_node(xnode *&root, xnode *node, xnode_cmp_f cmp_f)
        {
            bool result = false;
            if (node)
            {
                if (root == nullptr)
                {
                    root = node;
                    result = true;
                }
                else
                {
                    xnode head; // False tree root
                    head.clear();

                    s32 dir = 0;
                    s32 lastdir = 0;

                    // Set up our helpers
                    xnode *g = nullptr; // Grandparent
                    xnode *t = &head;   // Parent
                    xnode *p = nullptr; // Parent
                    xnode *q = root;    // Iterator

                    t->set_child(root, xnode::RIGHT);

                    // Search down the tree for a place to insert
                    result = false;
                    while (true)
                    {
                        if (q == nullptr)
                        { // Insert node at the first null link.
                            q = node;
                            result = true;
                            p->set_child(q, dir);
                        }
                        else if (rb_is_red(q->get_left()) && rb_is_red(q->get_right()))
                        { // Simple red violation: color flip
                            q->set_red();
                            q->get_left()->set_black();
                            q->get_right()->set_black();
                        }

                        if (rb_is_red(q) && rb_is_red(p))
                        { // Hard red violation: rotations necessary
                            s32 const dir2 =
                                (t->get_right() == g) ? xnode::RIGHT : xnode::LEFT;
                            if (q == p->get_child(lastdir))
                            {
                                xnode *r = rb_rotate(g, !lastdir);
                                t->set_child(r, dir2);
                            }
                            else
                            {
                                xnode *r = rb_rotate2(g, !lastdir);
                                t->set_child(r, dir2);
                            }
                        }

                        // Stop working if we inserted a node.
                        // This check also disallows duplicates in the tree
                        s32 const c = cmp_f(node, q);
                        if (c == 0)
                            break;

                        lastdir = dir;
                        dir = c < 0 ? xnode::LEFT : xnode::RIGHT;

                        // Move the helpers down
                        if (g != nullptr)
                        {
                            t = g;
                        }

                        g = p;
                        p = q;
                        q = q->get_child(dir);
                    }
                    // Update the root (it may be different)
                    root = head.get_child(xnode::RIGHT);
                }
                root->set_black(); // Make the root black for simplified logic
            }
            return result;
        }

        // Returns 1 if the value was removed, 0 otherwise. Optional node callback
        // can be provided to dealloc node and/or user data. Use rb_tree_node_dealloc
        // default callback to deallocate node created by rb_tree_insert(...).
        inline bool rb_remove_node(xnode *&root, xnode *find, xnode_cmp_f cmp_f, xnode_remove_f remove_f, xnode *&outnode)
        {
            if (root != nullptr)
            {
                xnode head; // False tree root
                head.clear();

                // Set up our helpers
                xnode *q = &head;
                xnode *g = nullptr;
                xnode *p = nullptr;
                xnode *f = nullptr; // Found item
                q->set_child(root, xnode::RIGHT);

                // Search and push a red node down to fix red violations as we go
                s32 dir = xnode::RIGHT;
                while (q->get_child(dir) != nullptr)
                {
                    s32 const lastdir = dir;

                    // Move the helpers down
                    g = p;
                    p = q;
                    q = q->get_child(dir);

                    s32 const c = cmp_f(find, q);

                    // Save the node with matching value and keep
                    // going; we'll do removal tasks at the end
                    if (c == 0)
                    {
                        f = q;
                    }

                    dir = (c < 0) ? xnode::LEFT : xnode::RIGHT;

                    // Push the red node down with rotations and color flips
                    if (!rb_is_red(q) && !rb_is_red(q->get_child(dir)))
                    {
                        if (rb_is_red(q->get_child(!dir)))
                        {
                            xnode *r = rb_rotate(q, dir);
                            p->set_child(r, lastdir);
                            p = r;
                        }
                        else if (!rb_is_red(q->get_child(!dir)))
                        {
                            xnode *s = p->get_child(!lastdir);
                            if (s)
                            {
                                if (!rb_is_red(s->get_child(!lastdir))
                                    && !rb_is_red(s->get_child(lastdir)))
                                { // Color flip
                                    p->set_black();
                                    s->set_red();
                                    q->set_red();
                                }
                                else
                                {
                                    s32 dir2 = g->get_child(1) == p;

                                    if (rb_is_red(s->get_child(lastdir)))
                                    {
                                        xnode *r = rb_rotate2(p, lastdir);
                                        g->set_child(r, dir2);
                                    }
                                    else if (rb_is_red(s->get_child(!lastdir)))
                                    {
                                        xnode *r = rb_rotate(p, lastdir);
                                        g->set_child(r, dir2);
                                    }

                                    // Ensure correct coloring
                                    s = g->get_child(dir2);
                                    s->set_red();
                                    q->set_red();
                                    s->get_left()->set_black();
                                    s->get_right()->set_black();
                                }
                            }
                        }
                    }
                }

                // Replace and remove the saved node
                if (f != nullptr)
                {
                    remove_f(f, q);

                    s32 const ps = p->get_child(xnode::RIGHT) == q ? xnode::RIGHT : xnode::LEFT;
                    s32 const qs = q->get_child(xnode::LEFT) == nullptr ? xnode::RIGHT : xnode::LEFT;
                    p->set_child(q->get_child(qs), ps);

                    // Give the 'removed' node back
                    q->set_left(nullptr);
                    q->set_right(nullptr);
                    outnode = q;
                }

                // Update the root (it may be different)
                root = head.get_child(xnode::RIGHT);

                // Make the root black for simplified logic
                if (root != nullptr)
                {
                    root->set_black();
                }

                return f != nullptr;
            }
            return true;
        }

        inline xnode *xtree_clear(xnode *&iterator)
        {
            //	Rotate away the left links so that
            //	we can treat this like the destruction
            //	of a linked list
            xnode *it = iterator;
            iterator = nullptr;

            while (it != nullptr)
            {
                if (it->get_child(xnode::LEFT) == nullptr)
                { // No left links, just kill the node and move on
                    iterator = (xnode *) it->get_child(xnode::RIGHT);
                    it->clear();
                    return it;
                }
                else
                { // Rotate away the left link and check again
                    iterator = (xnode *) it->get_child(xnode::LEFT);
                    it->set_child(iterator->get_child(xnode::RIGHT), xnode::LEFT);
                    iterator->set_child(it, xnode::RIGHT);
                }
                it = iterator;
                iterator = nullptr;
            }
            return nullptr;
        }
    }    
}