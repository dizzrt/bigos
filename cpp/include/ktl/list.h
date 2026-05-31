#ifndef _BIG_LIST_H
#define _BIG_LIST_H

#include <ext/aligned_buffer.h>
#include <bigos/attributes.h>

namespace ktl {
    namespace __detail {
        struct _List_node_base {
            _List_node_base *next;
            _List_node_base *prev;

            static void swap(_List_node_base &__x, _List_node_base &__y) noexcept;
            void transfer(_List_node_base *const__first, _List_node_base *const __last) noexcept;
            void reverse() noexcept;
            void hook(_List_node_base *const __position) noexcept;
            void unhook() noexcept;
        };

        struct _List_node_header : public _List_node_base {
            void init() noexcept { this->next = this->prev = this; }

            // constructors
            _List_node_header() noexcept { init(); }
            _List_node_header(_List_node_header &&__x) noexcept : _List_node_base{__x.next, __x.prev} {
                if (__x.base()->next == __x.base())
                    this->next = this->prev = this;
                else {
                    this->next->prev = this->prev->next = this->base();
                    __x.init();
                }
            }

            void move_nodes(_List_node_header &&__x) {
                _List_node_base *const __xnode = __x.base();
                if (__xnode->next == __xnode)
                    init();
                else {
                    _List_node_base *const __node = this->base();
                    __node->next = __xnode->next;
                    __node->prev = __xnode->prev;
                    __node->next->prev = __node->prev->next = __node;
                    __x.init();
                }
            }

        private:
            _List_node_base *base() { return this; }
        };

    }   // namespace __detail

    template <typename _Tp>
    struct _attr_nodiscard_ _List_node : public __detail::_List_node_base {
        __gnu_cxx::__aligned_membuf<_Tp> _storage;
        _Tp *valptr() { return _storage.ptr(); }
        _Tp const *valptr() const { return _storage.ptr(); }

        _Tp &operator*() noexcept { return *_storage.ptr(); }
        _Tp *operator->() noexcept { return *_storage.ptr(); }

        _List_node() = default;
        _List_node(_Tp _val) { *_storage.ptr() = _val; }
    };

    template <typename _Tp>
    using klist_node = _List_node<_Tp>;

    template <typename _Tp>
    struct _List_iterator {
        typedef _List_iterator<_Tp> _Self;
        typedef _List_node<_Tp> _Node;

        typedef ptrdiff_t difference_type;
        typedef _Tp value_type;
        typedef _Tp *pointer;
        typedef _Tp &reference;

        __detail::_List_node_base *_node;

        _List_iterator() noexcept : _node() {}

        explicit _List_iterator(__detail::_List_node_base *__x) noexcept : _node(__x) {}

        _Self _const_cast() const noexcept { return *this; }

        reference operator*() const noexcept { return *static_cast<_Node *>(_node)->valptr(); }

        pointer operator->() const noexcept { return static_cast<_Node *>(_node)->valptr(); }

        _Self &operator++() noexcept {
            _node = _node->next;
            return *this;
        }

        _Self operator++(int) noexcept {
            _Self temp = *this;
            _node = _node->next;
            return temp;
        }

        _Self &operator--() noexcept {
            _node = _node->prev;
            return *this;
        }

        _Self operator--(int) noexcept {
            _Self temp = *this;
            _node = _node->prev;
            return temp;
        }

        friend bool operator==(const _Self &__x, const _Self &__y) noexcept { return __x._node == __y._node; }
        friend bool operator!=(const _Self &__x, const _Self &__y) noexcept { return __x._node != __y._node; }
    };

    template <typename _Tp>
    struct _List_const_iterator {
        typedef _List_const_iterator<_Tp> _Self;
        typedef const _List_node<_Tp> _Node;
        typedef _List_iterator<_Tp> iterator;

        typedef ptrdiff_t difference_type;
        typedef _Tp value_type;
        typedef const _Tp *pointer;
        typedef const _Tp &reference;

        const __detail::_List_node_base *_node;

        _List_const_iterator() noexcept : _node() {}
        _List_const_iterator(const iterator &__x) noexcept : _node(__x._node) {}
        explicit _List_const_iterator(const __detail::_List_node_base *__x) noexcept : _node(__x) {}

        iterator _M_const_cast() const noexcept { return iterator(const_cast<__detail::_List_node_base *>(_node)); }

        reference operator*() const noexcept { return *static_cast<_Node *>(_node)->valptr(); }

        pointer operator->() const noexcept { return static_cast<_Node *>(_node)->valptr(); }

        _Self &operator++() noexcept {
            _node = _node->next;
            return *this;
        }

        _Self operator++(int) noexcept {
            _Self temp = *this;
            _node = _node->next;
            return temp;
        }

        _Self &operator--() noexcept {
            _node = _node->prev;
            return *this;
        }

        _Self operator--(int) noexcept {
            _Self temp = *this;
            _node = _node->prev;
            return temp;
        }

        friend bool operator==(const _Self &__x, const _Self &__y) noexcept { return __x._node == __y._node; }
        friend bool operator!=(const _Self &__x, const _Self &__y) noexcept { return __x._node != __y._node; }
    };

    template <typename _Tp>
    class _attr_nodiscard_ _List_foundation {
    public:
        typedef _Tp value_type;
        typedef _Tp *pointer;
        typedef _Tp &reference;
        typedef const _Tp &const_reference;

        typedef _List_node<_Tp> _Node;
        typedef __detail::_List_node_base _Base;

        typedef _List_iterator<_Tp> iterator;
        typedef _List_const_iterator<_Tp> const_iterator;

    private:
        __detail::_List_node_header *_sentinel;
        __detail::_List_node_header _sentinel_node;

        // number of real node
        size_t _nr_node;

    public:
        _List_foundation() : _sentinel(&_sentinel_node), _nr_node(0) {}
        virtual ~_List_foundation() = default;

        iterator begin() noexcept _attr_pure_ { return iterator(_sentinel->next); }
        iterator end() noexcept _attr_pure_ { return iterator(_sentinel); }

        const_iterator begin() const noexcept _attr_pure_ { return const_iterator(_sentinel->next); }
        const_iterator end() const noexcept _attr_pure_ { return const_iterator(_sentinel); }

        size_t size() const noexcept _attr_pure_ { return _nr_node; }
        bool empty() const noexcept _attr_pure_ { return _sentinel->next == &_sentinel_node; }

        reference front() noexcept _attr_pure_ { return *begin(); }
        reference back() noexcept _attr_pure_ { return *(--end()); }

        const_reference front() const noexcept _attr_pure_ { return *begin(); }
        const_reference back() const noexcept _attr_pure_ { return *(--end()); }

        void insert(iterator _position, _Base &_node) noexcept {
            _node.hook(_position._node);
            _nr_node++;
        }

        void insert(iterator _position, _Base *_node) noexcept {
            _node->hook(_position._node);
            _nr_node++;
        }

        void insert(_Base &_node) noexcept {
            _node.hook(_sentinel);
            _nr_node++;
        }

        void insert(_Base *_node) noexcept {
            _node->hook(_sentinel);
            _nr_node++;
        }

        void erase(iterator &_position) noexcept {
            _position._node->unhook();
            _nr_node--;
        }

        void remove(const value_type &_value) {
            iterator first = begin();
            iterator last = end();

            while (first != last) {
                iterator next = first;
                ++next;

                if (*first == _value) {
                    first._node->unhook();
                    _nr_node--;
                }

                first = next;
            }
        }
    };

    template <typename _Tp>
    using klist = _List_foundation<_Tp>;

    // template <typename _Tp, typename _Alloc>
    // class _attr_nodiscard_ _List_base : public _List_foundation<_Tp> {};

    // template <typename _Tp, typename _Alloc = ktl::allocator<_Tp>>
    // class _attr_nodiscard_ list : protected _List_base<_Tp, _Alloc> {};

}   // namespace ktl

#endif   // _BIG_LIST_H
