#pragma once

#include <iterator>
template <typename T>
class reverse_itr {
    T& iterable_;
public:
    explicit reverse_itr(T& iterable) : iterable_{iterable} {}
    auto begin() const { return std::rbegin(iterable_); }
    auto end() const { return std::rend(iterable_); }
};
