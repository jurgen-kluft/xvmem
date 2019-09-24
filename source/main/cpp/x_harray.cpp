#include "xbase/x_target.h"
#include "xbase/x_debug.h"

//#include "xvmem/x_harray.h"

namespace xcore
{
    // A bit based trie

    struct value_t
    {
        u32     m_key;
        u32     m_value;
    };

    struct node_t
    {
        enum { TRIE_RUN_POS = 5, TRIE_RUN_CNT = 5, TYPE_OF_CHILD=2, TRIE_RUN_BITS=20, TYPE_VALUE=0, TYPE_NODE=1 };

        static inline s32 max_len() { return TRIE_RUN_BITS; }

        inline s32 get_pos() const { return ((m_data >> (32-TRIE_RUN_POS)) & ((1<<TRIE_RUN_POS) - 1)); }
        inline s32 get_len() const { return ((m_data >> (32-TRIE_RUN_POS-TRIE_RUN_LEN)) & ((1<<TRIE_RUN_CNT) - 1)); }

        inline u32 get_mask() const { return ((1 << get_len()) - 1) << (32 - get_pos() - get_len()); }
        inline u32 get_run() const { return (m_data & ((1<<get_len()) - 1)) << (32 - get_pos() - get_len()); }

        // Which bit determines the child index ?
        inline s32 get_cbit() const { return (32 - (get_pos() + get_len() + 1)); }
        inline u8  get_ctype(s8 cindex) const { u32 const c = (m_data >> (32 - (TRIE_RUN_POS + TRIE_RUN_CNT + TYPE_OF_CHILD))) & ((1 << TYPE_OF_CHILD) - 1); return (c>>cindex) & 1; }

        inline void set_pos(u32 pos) {}
        inline void set_len(u32 pos) {}
        inline void set_bits(u32 bits) {}

        inline node_t* get_child_as_node(s8 cindex) const { return m_children[cindex]; }
        inline value_t* get_child_as_value(s8 cindex) const { return static_cast<value_t*>(m_children[cindex]); }

        inline void set_child_as_value(s8 cindex, value_t* cvalue) { m_children[cindex] = static_cast<node_t*>(cvalue); m_data = m_data & (0xffffffff ^ (1<<(cindex + TRIE_RUN_BITS)); }
        inline void set_child_as_node(s8 cindex, node_t* cnode) { m_children[cindex] = cnode; m_data = m_data | ((1<<cindex) << TRIE_RUN_BITS); }

        u32     m_data;
        node_t* m_children[2];
    };

    static inline bool s_is_bit_set(u64 data, s32 bit) { return (data & bit) != 0; }
    static inline u32 s_get_bits(u64 key, s32 pos, s32 len)
    {
        return (key >> (32 - (pos + len))) & ((1 << len) - 1);
    }

    struct trie_key_runner
    {
        trie_key_runner(s8 pos_begin, s8 pos_len, s8 node_maxlen)
            : m_key_pos_begin(pos_begin)
            , m_key_pos_length(pos_len)
            , m_node_run_maxlen(node_maxlen)
        {}


        bool    iterate(u64 key1, u64 key2, s8 base_pos, s8& pos, s8& len, u64& run)
        {
            u64 const mask = 0x8000000000000000L >> base_pos;
            key1 = key1 & mask;
            key2 = key2 & mask;
            u64 const diff = key1 ^ key2;
            s8 dpos = (xcountLeadingZeros(diff) - m_key_pos_begin) - base_pos;

            // If dpos turns our larget than the maximum run of a node + 1 then we
            // need to iterate again.
            if (dpos > (node_t::max_len()+1))
            {
                pos = base_pos;
                len = node_t::max_len();
                run = s_get_bits(key1, pos, len);
                return true;
            }

            // Ok so we have a run that can fit into a node
            pos = base_pos;
            len = dpos - base_pos;
            run = s_get_bits(key1, pos, len);
            return false;
        }

        static inline s8 get_child_index(u64 key, s8 cbit) { return ((key & (1<<cbit))!=0) ? 1 : 0; }

        s8      m_key_pos_begin;    // Where in the key do we start (msb(pos=0), lsb(pos=key length))
        s8      m_key_pos_length;   // From the begin pos, how many bits do we need to consider
        s8      m_node_run_maxlen;  // What is the run length a node can hold (+1 bit for branch)
    };

    static void test_key_runner()
    {
        trie_key_runner runner(0, 64, 20);

        u64 key1 = 0x000000001F000000L;
        u64 key2 = 0x000000000F000000L;

        xfsa* nodes;

        s8 bp,p,l;
        u64 b;
        bp = 0;

        node_t* parent = nullptr;
        s8 parent_cbit = 0;
        while (runner.iterate(key1, key2, bp, p, l, b))
        {
            // Create node
            node_t* n = (node_t*)nodes->allocate();
            n->set_pos(p);
            n->set_len(l);
            n->set_bits(b);
            if (parent != nullptr)
            {
                parent->set_child_as_node(runner.get_child_index(key1, parent_cbit), n);
            }

            parent_cbit = n->get_cbit();
            parent = n;
            bp += l + 1;
        }

        node_t* n = (node_t*)nodes->allocate();
        n->set_pos(p);
        n->set_len(l);
        n->set_bits(b);
        if (parent != nullptr)
        {
            parent->set_child_as_node(runner.get_child_index(key1, parent_cbit), n);
        }

    }

    // Trie data structure where every node can cover a range of bits (not only 1 bit)
    class xdtrie
    {
    public:
        

        bool    insert(u32 key, u32 value);
        bool    find(u32 key, u32& value);
        bool    remove(u32 key);

    protected:
        node_t*  m_root;
        value_t* m_value;
        xfsa*    m_nodes;
        xfsa*    m_value;
    };


    bool    xdtrie::insert(u32 key, u32 value)
    {
        // Before creating the first node (root) we wait until we have 2 elements
        if (m_root == nullptr)
        {
            if (m_value == nullptr)
            {
                m_value = (value_t*)m_values->allocate();
                m_value->m_key   = key;
                m_value->m_value = value;
                return true;
            }
            else if (m_value->m_key != key)
            {
                value_t* v = (value_t*)m_values->allocate();
                v->m_key   = key;
                v->m_value = value;
                
                node_t* n = (node_t*)m_nodes->allocate();
                
                // Determine the length of the run
                u32 const diff = v->m_key ^ m_value->m_key;
                s32 const len = xcountLeadingZeros(diff);

                s32 p = 0;
                s32 l = len;
                if (l > node_t::max_len())
                {
                    while (l > node_t::max_len())
                    {
                        // We need to create nodes to cover the identical run.
                        // Could take more than 1 node!
                        node_t* nr = (node_t*)m_nodes->allocate();
                        nr->set_pos(p);
                        nr->set_len(node_t::max_len());
                        nr->set_bits(_get_bits(key, p, node_t::max_len()));

                        l -= node_t::max_len();
                        p += node_t::max_len() + 1;
                    }
                }

                n->set_pos(p);
                n->set_len(l);
                n->set_bits(_get_bits(key, p, l));

                // Set the 2 values as children
                s32 cbit = n->get_cbit();
                s32 cidx = (_is_bit_set(v->m_key, cbit)) ? 1 : 0;
                n->set_child_as_value(cidx, v);
                n->set_child_as_value(1-cidx, m_value);

                // Set our root
                m_root = n;
                return true;
            }
            else
            {
                m_value->m_value = value;
                return true;
            }
        }
        return false;
    }

    bool    xdtrie::find(u32 key, u32& value)
    {
        node_t* n = m_root;
        while (n != nullptr)
        {
            // The run should be the same
            s32 const run = n->get_len();
            if (run > 0)
            {
                s32 const nbits = n->get_bits();
                u32 const kbits = (key & n->get_mask());
                if (kbits != n->get_run())
                    return false;
            }

            // Determine left/right child
            s32 const child = key & (1 << n->get_cbit());
            
            // Determine type-of child
            if (n->get_ctype(child) == node_t::TYPE_NODE)
            {
                // Branch is a node, traverse further
                node_t* cnode = n->get_child_as_node(child);
                n = cnode;
            }
            else
            {
                // Branch is a value, see if this is the same key-value
                value_t* cvalue = n->get_child_as_value(child);
                if (cvalue->m_key == key)
                {
                    value = cvalue->m_value;
                    return true;
                }
                return false;
            }
        }

        return false;
    }
};
