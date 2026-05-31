#include <ktl/list.h>
#include <bits/move.h>

namespace ktl {
    namespace __detail {

        void _List_node_base::swap(_List_node_base &__x, _List_node_base &__y) noexcept {
            if (__x.next != &__x) {
                if (__y.next != &__y) {
                    // both __x and __y are not empty
                    std::swap(__x.next, __y.next);
                    std::swap(__x.prev, __y.prev);
                } else {
                    // __x is not empty, __y is empty
                    __y.next = __x.next;
                    __y.prev = __x.prev;
                    __y.next->prev = __y.prev->next = &__y;
                    __x.next = __x.prev = &__x;
                }
            } else if (__y.next != &__y) {
                // __x is empty, __y is not empty
                __x.next = __y.next;
                __x.prev = __y.prev;
                __x.next->prev = __x.prev->next = &__x;
                __y.next = __y.prev = &__y;
            }
        }

        void _List_node_base::transfer(_List_node_base *const __first, _List_node_base *const __last) noexcept {
            if (this != __last) {
                // remove [first,last) from its old position
                __last->prev->next = this;
                __first->prev->next = __last;
                this->prev->next = __first;

                // splice [first,last) into its new position
                _List_node_base *const temp = this->prev;
                this->prev = __last->prev;
                __last->prev = __first->prev;
                __first->prev = temp;
            }
        }

        void _List_node_base::reverse() noexcept {
            _List_node_base *temp = this;
            do {
                std::swap(temp->next, temp->prev);
                temp = temp->prev;
            } while (temp != this);
        }

        void _List_node_base::hook(_List_node_base *const __position) noexcept {
            this->next = __position;
            this->prev = __position->prev;
            __position->prev->next = this;
            __position->prev = this;
        }

        void _List_node_base::unhook() noexcept {
            _List_node_base *const __next_node = this->next;
            _List_node_base *const __prev_node = this->prev;
            __prev_node->next = __next_node;
            __next_node->prev = __prev_node;
            this->next = this->prev = nullptr;
        }
    }   // namespace __detail
}   // namespace ktl
