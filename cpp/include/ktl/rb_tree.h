#ifndef _BIG_KTL_RB_TREE_H
#define _BIG_KTL_RB_TREE_H

#include <ktl/algorithm.h>
#include <ktl/allocator.h>
#include <ktl/pair.h>

namespace ktl {
    enum class rb_color : uint8_t {
        red,
        black,
    };

    template <typename _Key, typename _Value>
    struct rb_tree_node {
        ktl::pair<_Key, _Value> data;
        rb_tree_node *parent;
        rb_tree_node *left;
        rb_tree_node *right;
        rb_color color;

        rb_tree_node(const _Key &__key, const _Value &__value)
            : data(__key, __value), parent(nullptr), left(nullptr), right(nullptr), color(rb_color::red) {}
    };

    template <typename _Key, typename _Value, typename _Compare = ktl::less<_Key>,
        typename _Alloc = ktl::allocator<rb_tree_node<_Key, _Value>>>
    class rb_tree {
    public:
        typedef rb_tree_node<_Key, _Value> node_type;

    private:
        node_type *root_;
        uint32_t size_;
        _Compare comp_;
        _Alloc alloc_;

        bool equal_key(const _Key &__a, const _Key &__b) const {
            return !comp_(__a, __b) && !comp_(__b, __a);
        }

        static node_type *minimum(node_type *__node) noexcept {
            while (__node != nullptr && __node->left != nullptr)
                __node = __node->left;
            return __node;
        }

        static node_type *successor(node_type *__node) noexcept {
            if (__node == nullptr)
                return nullptr;
            if (__node->right != nullptr)
                return minimum(__node->right);
            node_type *parent = __node->parent;
            while (parent != nullptr && __node == parent->right) {
                __node = parent;
                parent = parent->parent;
            }
            return parent;
        }

        void rotate_left(node_type *__x) noexcept {
            node_type *__y = __x->right;
            __x->right = __y->left;
            if (__y->left != nullptr)
                __y->left->parent = __x;
            __y->parent = __x->parent;
            if (__x->parent == nullptr)
                root_ = __y;
            else if (__x == __x->parent->left)
                __x->parent->left = __y;
            else
                __x->parent->right = __y;
            __y->left = __x;
            __x->parent = __y;
        }

        void rotate_right(node_type *__x) noexcept {
            node_type *__y = __x->left;
            __x->left = __y->right;
            if (__y->right != nullptr)
                __y->right->parent = __x;
            __y->parent = __x->parent;
            if (__x->parent == nullptr)
                root_ = __y;
            else if (__x == __x->parent->right)
                __x->parent->right = __y;
            else
                __x->parent->left = __y;
            __y->right = __x;
            __x->parent = __y;
        }

        void fix_insert(node_type *__z) noexcept {
            while (__z->parent != nullptr && __z->parent->color == rb_color::red) {
                node_type *__gp = __z->parent->parent;
                if (__z->parent == __gp->left) {
                    node_type *__y = __gp->right;
                    if (__y != nullptr && __y->color == rb_color::red) {
                        __z->parent->color = rb_color::black;
                        __y->color = rb_color::black;
                        __gp->color = rb_color::red;
                        __z = __gp;
                    } else {
                        if (__z == __z->parent->right) {
                            __z = __z->parent;
                            rotate_left(__z);
                        }
                        __z->parent->color = rb_color::black;
                        __gp->color = rb_color::red;
                        rotate_right(__gp);
                    }
                } else {
                    node_type *__y = __gp->left;
                    if (__y != nullptr && __y->color == rb_color::red) {
                        __z->parent->color = rb_color::black;
                        __y->color = rb_color::black;
                        __gp->color = rb_color::red;
                        __z = __gp;
                    } else {
                        if (__z == __z->parent->left) {
                            __z = __z->parent;
                            rotate_right(__z);
                        }
                        __z->parent->color = rb_color::black;
                        __gp->color = rb_color::red;
                        rotate_left(__gp);
                    }
                }
            }
            root_->color = rb_color::black;
        }

        void transplant(node_type *__u, node_type *__v) noexcept {
            if (__u->parent == nullptr)
                root_ = __v;
            else if (__u == __u->parent->left)
                __u->parent->left = __v;
            else
                __u->parent->right = __v;
            if (__v != nullptr)
                __v->parent = __u->parent;
        }

        static rb_color color_of(node_type *__node) noexcept {
            return __node == nullptr ? rb_color::black : __node->color;
        }

        void fix_erase(node_type *__x, node_type *__parent) noexcept {
            while (__x != root_ && color_of(__x) == rb_color::black) {
                if (__parent == nullptr)
                    break;
                if (__x == __parent->left) {
                    node_type *__w = __parent->right;
                    if (color_of(__w) == rb_color::red) {
                        __w->color = rb_color::black;
                        __parent->color = rb_color::red;
                        rotate_left(__parent);
                        __w = __parent->right;
                    }
                    if (color_of(__w == nullptr ? nullptr : __w->left) == rb_color::black &&
                        color_of(__w == nullptr ? nullptr : __w->right) == rb_color::black) {
                        if (__w != nullptr)
                            __w->color = rb_color::red;
                        __x = __parent;
                        __parent = __x->parent;
                    } else {
                        if (color_of(__w == nullptr ? nullptr : __w->right) == rb_color::black) {
                            if (__w != nullptr && __w->left != nullptr)
                                __w->left->color = rb_color::black;
                            if (__w != nullptr) {
                                __w->color = rb_color::red;
                                rotate_right(__w);
                            }
                            __w = __parent->right;
                        }
                        if (__w != nullptr)
                            __w->color = __parent->color;
                        __parent->color = rb_color::black;
                        if (__w != nullptr && __w->right != nullptr)
                            __w->right->color = rb_color::black;
                        rotate_left(__parent);
                        __x = root_;
                        __parent = nullptr;
                    }
                } else {
                    node_type *__w = __parent->left;
                    if (color_of(__w) == rb_color::red) {
                        __w->color = rb_color::black;
                        __parent->color = rb_color::red;
                        rotate_right(__parent);
                        __w = __parent->left;
                    }
                    if (color_of(__w == nullptr ? nullptr : __w->right) == rb_color::black &&
                        color_of(__w == nullptr ? nullptr : __w->left) == rb_color::black) {
                        if (__w != nullptr)
                            __w->color = rb_color::red;
                        __x = __parent;
                        __parent = __x->parent;
                    } else {
                        if (color_of(__w == nullptr ? nullptr : __w->left) == rb_color::black) {
                            if (__w != nullptr && __w->right != nullptr)
                                __w->right->color = rb_color::black;
                            if (__w != nullptr) {
                                __w->color = rb_color::red;
                                rotate_left(__w);
                            }
                            __w = __parent->left;
                        }
                        if (__w != nullptr)
                            __w->color = __parent->color;
                        __parent->color = rb_color::black;
                        if (__w != nullptr && __w->left != nullptr)
                            __w->left->color = rb_color::black;
                        rotate_right(__parent);
                        __x = root_;
                        __parent = nullptr;
                    }
                }
            }
            if (__x != nullptr)
                __x->color = rb_color::black;
        }

        bool validate_node(node_type *__node, uint32_t __black_count, uint32_t *__expected) const noexcept {
            if (__node == nullptr) {
                if (*__expected == static_cast<uint32_t>(-1)) {
                    *__expected = __black_count;
                    return true;
                }
                return *__expected == __black_count;
            }
            if (__node->color == rb_color::red) {
                if (color_of(__node->left) == rb_color::red || color_of(__node->right) == rb_color::red)
                    return false;
            } else {
                ++__black_count;
            }
            if (__node->left != nullptr &&
                (__node->left->parent != __node || !comp_(__node->left->data.first, __node->data.first)))
                return false;
            if (__node->right != nullptr &&
                (__node->right->parent != __node || comp_(__node->right->data.first, __node->data.first)))
                return false;
            return validate_node(__node->left, __black_count, __expected) &&
                   validate_node(__node->right, __black_count, __expected);
        }

        void clear_node(node_type *__node) noexcept {
            if (__node == nullptr)
                return;
            clear_node(__node->left);
            clear_node(__node->right);
            alloc_.destroy(__node);
            alloc_.deallocate(__node);
        }

    public:
        rb_tree(_Compare __comp = _Compare(), _Alloc __alloc = _Alloc()) noexcept
            : root_(nullptr), size_(0), comp_(__comp), alloc_(__alloc) {}
        rb_tree(const rb_tree &) = delete;
        rb_tree &operator=(const rb_tree &) = delete;
        ~rb_tree() {
            clear();
        }

        uint32_t size() const noexcept {
            return size_;
        }
        bool empty() const noexcept {
            return size_ == 0;
        }
        node_type *begin() const noexcept {
            return minimum(root_);
        }
        static node_type *next(node_type *__node) noexcept {
            return successor(__node);
        }

        node_type *find(const _Key &__key) const noexcept {
            node_type *__cur = root_;
            while (__cur != nullptr) {
                if (equal_key(__key, __cur->data.first))
                    return __cur;
                __cur = comp_(__key, __cur->data.first) ? __cur->left : __cur->right;
            }
            return nullptr;
        }

        node_type *insert(const _Key &__key, const _Value &__value, bool *__inserted = nullptr) noexcept {
            node_type *__parent = nullptr;
            node_type *__cur = root_;
            while (__cur != nullptr) {
                __parent = __cur;
                if (equal_key(__key, __cur->data.first)) {
                    __cur->data.second = __value;
                    if (__inserted != nullptr)
                        *__inserted = false;
                    return __cur;
                }
                __cur = comp_(__key, __cur->data.first) ? __cur->left : __cur->right;
            }

            node_type *__node = alloc_.allocate();
            if (__node == nullptr) {
                if (__inserted != nullptr)
                    *__inserted = false;
                return nullptr;
            }
            if (!alloc_.construct(__node, __key, __value)) {
                alloc_.deallocate(__node);
                if (__inserted != nullptr)
                    *__inserted = false;
                return nullptr;
            }

            __node->parent = __parent;
            if (__parent == nullptr)
                root_ = __node;
            else if (comp_(__key, __parent->data.first))
                __parent->left = __node;
            else
                __parent->right = __node;

            fix_insert(__node);
            ++size_;
            if (__inserted != nullptr)
                *__inserted = true;
            return __node;
        }

        bool erase(const _Key &__key) noexcept {
            node_type *__z = find(__key);
            if (__z == nullptr)
                return false;

            node_type *__y = __z;
            node_type *__x = nullptr;
            node_type *__x_parent = nullptr;
            rb_color __y_original_color = __y->color;

            if (__z->left == nullptr) {
                __x = __z->right;
                __x_parent = __z->parent;
                transplant(__z, __z->right);
            } else if (__z->right == nullptr) {
                __x = __z->left;
                __x_parent = __z->parent;
                transplant(__z, __z->left);
            } else {
                __y = minimum(__z->right);
                __y_original_color = __y->color;
                __x = __y->right;
                if (__y->parent == __z) {
                    __x_parent = __y;
                    if (__x != nullptr)
                        __x->parent = __y;
                } else {
                    __x_parent = __y->parent;
                    transplant(__y, __y->right);
                    __y->right = __z->right;
                    __y->right->parent = __y;
                }
                transplant(__z, __y);
                __y->left = __z->left;
                __y->left->parent = __y;
                __y->color = __z->color;
            }
            if (__y_original_color == rb_color::black)
                fix_erase(__x, __x_parent);
            if (root_ != nullptr)
                root_->color = rb_color::black;
            alloc_.destroy(__z);
            alloc_.deallocate(__z);
            --size_;
            return true;
        }

        bool validate() const noexcept {
            if (root_ == nullptr)
                return true;
            if (root_->color != rb_color::black)
                return false;
            uint32_t expected = static_cast<uint32_t>(-1);
            return validate_node(root_, 0, &expected);
        }

        void clear() noexcept {
            clear_node(root_);
            root_ = nullptr;
            size_ = 0;
        }
    };
}   // namespace ktl

#endif   // _BIG_KTL_RB_TREE_H
