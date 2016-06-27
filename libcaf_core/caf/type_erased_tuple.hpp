/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#ifndef CAF_TYPE_ERASED_TUPLE_HPP
#define CAF_TYPE_ERASED_TUPLE_HPP

#include <tuple>
#include <cstddef>
#include <cstdint>
#include <typeinfo>

#include "caf/fwd.hpp"
#include "caf/type_nr.hpp"
#include "caf/optional.hpp"
#include "caf/type_erased_value.hpp"

#include "caf/detail/try_match.hpp"
#include "caf/detail/apply_args.hpp"
#include "caf/detail/pseudo_tuple.hpp"

namespace caf {

/// Represents a tuple of type-erased values.
class type_erased_tuple {
public:
  // -- member types -----------------------------------------------------------

  using rtti_pair = std::pair<uint16_t, const std::type_info*>;

  // -- constructors, destructors, and assignment operators --------------------

  type_erased_tuple() = default;
  type_erased_tuple(const type_erased_tuple&) = default;

  virtual ~type_erased_tuple();

  // -- pure virtual modifiers -------------------------------------------------

  /// Returns a mutable pointer to the element at position `pos`.
  virtual void* get_mutable(size_t pos) = 0;

  /// Load the content for the element at position `pos` from `source`.
  virtual void load(size_t pos, deserializer& source) = 0;

  // -- modifiers --------------------------------------------------------------

  /// Load the content for the tuple from `source`.
  void load(deserializer& source);

  // -- pure virtual observers -------------------------------------------------

  /// Returns the size of this tuple.
  virtual size_t size() const noexcept = 0;

  /// Returns a type hint for the element types.
  virtual uint32_t type_token() const noexcept = 0;

  /// Returns the type number and `std::type_info` object for
  /// the element at position `pos`.
  virtual rtti_pair type(size_t pos) const noexcept = 0;

  /// Returns the element at position `pos`.
  virtual const void* get(size_t pos) const noexcept = 0;

  /// Returns a string representation of the element at position `pos`.
  virtual std::string stringify(size_t pos) const = 0;

  /// Returns a copy of the element at position `pos`.
  virtual type_erased_value_ptr copy(size_t pos) const = 0;

  /// Saves the element at position `pos` to `sink`.
  virtual void save(size_t pos, serializer& sink) const = 0;

  // -- observers --------------------------------------------------------------

  /// Returns whether multiple references to this tuple exist.
  /// The default implementation returns false.
  virtual bool shared() const noexcept;

  ///  Returns `size() == 0`.
  bool empty() const;

  /// Returns a string representation of the tuple.
  std::string stringify() const;

  /// Saves the content of the tuple to `sink`.
  void save(serializer& sink) const;

  /// Checks whether the type of the stored value at position `pos`
  /// matches type number `n` and run-time type information `p`.
  bool matches(size_t pos, uint16_t n, const std::type_info* p) const  noexcept;

  // -- convenience functions --------------------------------------------------

  /// Returns the type number for the element at position `pos`.
  inline uint16_t type_nr(size_t pos) const noexcept {
    return type(pos).first;
  }

  /// Checks whether the type of the stored value matches `rtti`.
  inline bool matches(size_t pos, const rtti_pair& rtti) const noexcept {
    return matches(pos, rtti.first, rtti.second);
  }

  /// Convenience function for `*reinterpret_cast<const T*>(get())`.
  template <class T>
  const T& get_as(size_t pos) const {
    return *reinterpret_cast<const T*>(get(pos));
  }

  /// Convenience function for `*reinterpret_cast<T*>(get_mutable())`.
  template <class T>
  T& get_mutable_as(size_t pos) {
    return *reinterpret_cast<T*>(get_mutable(pos));
  }

  /// Returns `true` if the element at `pos` matches `T`.
  template <class T>
  bool match_element(size_t pos) const noexcept {
    CAF_ASSERT(pos < size());
    auto x = detail::meta_element_factory<T>::create();
    return detail::match_element(x, *this, pos);
  }

  /// Returns `true` if the pattern `Ts...` matches the content of this tuple.
  template <class... Ts>
  bool match_elements() const noexcept {
    if (sizeof...(Ts) != size())
      return false;
    detail::meta_elements<detail::type_list<Ts...>> xs;
    for (size_t i = 0; i < xs.arr.size(); ++i)
      if (! detail::match_element(xs.arr[i], *this, i))
        return false;
    return true;
  }

  template <class F>
  auto apply(F fun)
  -> optional<typename detail::get_callable_trait<F>::result_type> {
    using trait = typename detail::get_callable_trait<F>::type;
    detail::type_list<typename trait::result_type> result_token;
    typename trait::arg_types args_token;
    return apply(fun, result_token, args_token);
  }

private:
  template <class F, class R, class... Ts>
  optional<R> apply(F& fun, detail::type_list<R>,
                    detail::type_list<Ts...> tk) {
    if (! match_elements<Ts...>())
      return none;
    detail::pseudo_tuple<typename std::decay<Ts>::type...> xs{shared()};
    for (size_t i = 0; i < size(); ++i)
      xs[i] = const_cast<void*>(get(i)); // pseud_tuple figures out const-ness
    return detail::apply_args(fun, detail::get_indices(tk), xs);
  }

  template <class F, class... Ts>
  optional<void> apply(F& fun, detail::type_list<void>,
                       detail::type_list<Ts...> tk) {
    if (! match_elements<Ts...>())
      return none;
    detail::pseudo_tuple<typename std::decay<Ts>::type...> xs{shared()};
    for (size_t i = 0; i < size(); ++i)
      xs[i] = const_cast<void*>(get(i)); // pseud_tuple figures out const-ness
    detail::apply_args(fun, detail::get_indices(tk), xs);
    return unit;
  }
};

/// @relates type_erased_tuple
/// Dummy objects representing empty tuples.
class empty_type_erased_tuple : public type_erased_tuple {
public:
  empty_type_erased_tuple() = default;

  ~empty_type_erased_tuple();

  void* get_mutable(size_t pos) override;

  void load(size_t pos, deserializer& source) override;

  size_t size() const noexcept override;

  uint32_t type_token() const noexcept override;

  rtti_pair type(size_t pos) const noexcept override;

  const void* get(size_t pos) const noexcept override;

  std::string stringify(size_t pos) const override;

  type_erased_value_ptr copy(size_t pos) const override;

  void save(size_t pos, serializer& sink) const override;
};

/// @relates type_erased_tuple
template <class Processor>
typename std::enable_if<Processor::is_saving::value>::type
serialize(Processor& proc, type_erased_tuple& x) {
  x.save(proc);
}

/// @relates type_erased_tuple
template <class Processor>
typename std::enable_if<Processor::is_loading::value>::type
serialize(Processor& proc, type_erased_tuple& x) {
  x.load(proc);
}

/// @relates type_erased_tuple
inline std::string to_string(const type_erased_tuple& x) {
  return x.stringify();
}

template <class... Ts>
class type_erased_tuple_view : public type_erased_tuple {
public:
  // -- member types -----------------------------------------------------------
  template <size_t X>
  using num_token = std::integral_constant<size_t, X>;

  // -- constructors, destructors, and assignment operators --------------------

  type_erased_tuple_view(Ts&... xs) : xs_(xs...) {
    init();
  }

  type_erased_tuple_view(const type_erased_tuple_view& other)
      : type_erased_tuple(),
        xs_(other.xs_) {
    init();
  }

  // -- overridden modifiers ---------------------------------------------------

  void* get_mutable(size_t pos) override {
    return ptrs_[pos]->get_mutable();
  }

  void load(size_t pos, deserializer& source) override {
    ptrs_[pos]->load(source);
  }

  // -- overridden observers ---------------------------------------------------

  size_t size() const noexcept override {
    return sizeof...(Ts);
  }

  uint32_t type_token() const noexcept override {
    return make_type_token<Ts...>();
  }

  rtti_pair type(size_t pos) const noexcept override {
    return ptrs_[pos]->type();
  }

  const void* get(size_t pos) const noexcept override {
    return ptrs_[pos]->get();
  }

  std::string stringify(size_t pos) const override {
    return ptrs_[pos]->stringify();
  }

  type_erased_value_ptr copy(size_t pos) const override {
    return ptrs_[pos]->copy();
  }

  void save(size_t pos, serializer& sink) const override {
    return ptrs_[pos]->save(sink);
  }

private:
  // -- pointer "lookup table" utility -----------------------------------------

  template <size_t N>
  void init(num_token<N>, num_token<N>) {
    // end of recursion
  }

  template <size_t P, size_t N>
  void init(num_token<P>, num_token<N> last) {
    ptrs_[P] = &std::get<P>(xs_);
    init(num_token<P + 1>{}, last);
  }

  void init() {
    init(num_token<0>{}, num_token<sizeof...(Ts)>{});
  }

  // -- data members -----------------------------------------------------------

  std::tuple<type_erased_value_impl<std::reference_wrapper<Ts>>...> xs_;
  type_erased_value* ptrs_[sizeof...(Ts) == 0 ? 1 : sizeof...(Ts)];
};

template <class... Ts>
type_erased_tuple_view<Ts...> make_type_erased_tuple_view(Ts&... xs) {
  return {xs...};
}

} // namespace caf

#endif // CAF_TYPE_ERASED_TUPLE_HPP
