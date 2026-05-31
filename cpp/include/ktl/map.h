#ifndef _BIG_KTL_MAP_H
#define _BIG_KTL_MAP_H

#include <ktl/rb_tree.h>

namespace ktl {
    template <typename _Key, typename _Value, typename _Compare = ktl::less<_Key>>
    class map {
    private:
        typedef ktl::rb_tree<_Key, _Value, _Compare> tree_type;
        tree_type tree_;

    public:
        typedef typename tree_type::node_type node_type;

        class iterator {
        private:
            node_type *node_;

        public:
            explicit iterator(node_type *__node = nullptr) noexcept : node_(__node) {}

            ktl::pair<_Key, _Value> &operator*() const noexcept {
                return node_->data;
            }
            ktl::pair<_Key, _Value> *operator->() const noexcept {
                return &node_->data;
            }
            iterator &operator++() noexcept {
                node_ = tree_type::next(node_);
                return *this;
            }
            friend bool operator==(const iterator &__a, const iterator &__b) noexcept {
                return __a.node_ == __b.node_;
            }
            friend bool operator!=(const iterator &__a, const iterator &__b) noexcept {
                return __a.node_ != __b.node_;
            }
        };

        map() = default;
        map(const map &) = delete;
        map &operator=(const map &) = delete;

        bool empty() const noexcept {
            return tree_.empty();
        }
        uint32_t size() const noexcept {
            return tree_.size();
        }

        iterator begin() noexcept {
            return iterator(tree_.begin());
        }
        iterator end() noexcept {
            return iterator(nullptr);
        }

        _Value *find(const _Key &__key) noexcept {
            node_type *__node = tree_.find(__key);
            return __node == nullptr ? nullptr : &__node->data.second;
        }
        const _Value *find(const _Key &__key) const noexcept {
            node_type *__node = tree_.find(__key);
            return __node == nullptr ? nullptr : &__node->data.second;
        }

        bool insert(const _Key &__key, const _Value &__value) noexcept {
            bool inserted = false;
            return tree_.insert(__key, __value, &inserted) != nullptr && inserted;
        }

        bool insert_or_assign(const _Key &__key, const _Value &__value) noexcept {
            return tree_.insert(__key, __value) != nullptr;
        }

        bool erase(const _Key &__key) noexcept {
            return tree_.erase(__key);
        }
    };
}   // namespace ktl

#endif   // _BIG_KTL_MAP_H
