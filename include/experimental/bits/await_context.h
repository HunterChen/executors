//
// await_context.h
// ~~~~~~~~~~~~~~~
// Stackless coroutine implementation.
//
// Copyright (c) 2014 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef EXECUTORS_EXPERIMENTAL_BITS_AWAIT_CONTEXT_H
#define EXECUTORS_EXPERIMENTAL_BITS_AWAIT_CONTEXT_H

#include <exception>
#include <system_error>
#include <tuple>
#include <utility>
#include <experimental/bits/executor_wrapper.h>
#include <experimental/bits/function_traits.h>
#include <experimental/bits/invoker.h>
#include <experimental/bits/tuple_utils.h>

namespace std {
namespace experimental {

template <class _Executor> template <class _OtherExecutor>
basic_await_context<_Executor>::basic_await_context(const basic_await_context<_OtherExecutor>& __c)
  : _M_executor(__c._M_executor), _M_impl(__c._M_impl), _M_error_code(__c._M_error_code)
{
}

template <class _Executor>
basic_await_context<_Executor> basic_await_context<_Executor>::operator[](error_code& __ec) const
{
  basic_await_context<_Executor> __c(*this);
  __c._M_error_code = &__ec;
  return __c;
}

template <class _T> class __awaitable {};

class __coroutine
{
public:
  __coroutine() : _M_value(0) {}
  bool _Is_complete() const { return _M_value == -1; }
  friend __coroutine& _Get_coroutine(__coroutine& __c) { return __c; }
  friend __coroutine& _Get_coroutine(__coroutine* __c) { return *__c; }
  friend exception_ptr* _Get_coroutine_exception(__coroutine&) { return 0; }
  friend exception_ptr* _Get_coroutine_exception(__coroutine*) { return 0; }
  friend void** _Get_coroutine_async_result(__coroutine&) { return 0; }
  friend void** _Get_coroutine_async_result(__coroutine*) { return 0; }

private:
  friend class __coroutine_ref;
  int _M_value;
};

class __coroutine_ref
{
public:
  __coroutine_ref(__coroutine& __c, exception_ptr* __ex, void** __r)
    : _M_value(__c._M_value), _M_modified(false), _M_ex(__ex), _M_async_result(__r)
  {
  }

  ~__coroutine_ref()
  {
    if (!_M_modified)
      _M_value = -1;
  }

  operator int() const
  {
    return _M_value;
  }

  int& operator=(int __v)
  {
    _M_modified = true;
    return _M_value = __v;
  }

  template <class _T>
  __awaitable<_T> operator&(_T& t)
  {
    *_M_async_result = &t;
    return __awaitable<_T>();
  }

  template <class _T> void operator&(__awaitable<_T>)
  {
  }

  void _Rethrow() const
  {
    if (_M_ex && *_M_ex)
      rethrow_exception(*_M_ex);
  }

private:
  __coroutine_ref& operator=(const __coroutine_ref&) = delete;
  int& _M_value;
  bool _M_modified;
  const exception_ptr* const _M_ex;
  void** const _M_async_result;
};

#define __REENTER(__c) \
  switch (::std::experimental::__coroutine_ref __coro_ref = \
      ::std::experimental::__coroutine_ref(_Get_coroutine(__c), \
        _Get_coroutine_exception(__c), _Get_coroutine_async_result(__c))) \
    case -1: if (__coro_ref) \
    { \
      goto __terminate_coroutine; \
      __terminate_coroutine: \
      __coro_ref = -1; \
      goto __bail_out_of_coroutine; \
      __bail_out_of_coroutine: \
      break; \
    } \
    else case 0:

#define __AWAIT(__n) \
  for (__coro_ref = (__n);;) \
    if (__coro_ref == 0) \
    { \
      case (__n): ; \
      __coro_ref._Rethrow(); \
      break; \
    } \
    else \
      switch (__coro_ref ? 0 : 1) \
        for (;;) \
          case -1: if (__coro_ref) \
            goto __terminate_coroutine; \
          else for (;;) \
            case 1: if (__coro_ref) \
              goto __bail_out_of_coroutine; \
            else case 0: __coro_ref&

#define reenter __REENTER
#define await __AWAIT(__LINE__)

class __await_context_impl_base
{
public:
  virtual ~__await_context_impl_base() {}
  virtual void _Resume() = 0;

  friend __coroutine& _Get_coroutine(__await_context_impl_base& __i) { return __i._M_coroutine; }
  friend __coroutine& _Get_coroutine(__await_context_impl_base* __i) { return __i->_M_coroutine; }
  friend exception_ptr* _Get_coroutine_exception(__await_context_impl_base& __i) { return &__i._M_ex; }
  friend exception_ptr* _Get_coroutine_exception(__await_context_impl_base* __i) { return &__i->_M_ex; }
  friend void** _Get_coroutine_async_result(__await_context_impl_base& __i) { return &__i._M_result_value; }
  friend void** _Get_coroutine_async_result(__await_context_impl_base* __i) { return &__i->_M_result_value; }

private:
  template <class, class...> friend struct __await_context_handler;
  template <class> friend class basic_await_context;
  __coroutine _M_coroutine;
  exception_ptr _M_ex = nullptr;
  error_code* _M_result_ec = nullptr;
  void* _M_result_value = nullptr;
};

template <class _Executor, class _Func, class... _Args>
class __await_context_impl
  : public __await_context_impl_base,
    public enable_shared_from_this<__await_context_impl<_Executor, _Func, _Args...>>
{
public:
  template <class _F, class... _A>
  __await_context_impl(const typename _Executor::work& __w, _F&& __f, _A&&... __args)
    : _M_work(__w), _M_function(forward<_F>(__f)), _M_args(forward<_A>(__args)...)
  {
  }

  virtual void _Resume()
  {
    const basic_await_context<_Executor> __ctx(_M_work, this->shared_from_this());
    _Tuple_invoke(_M_function, _M_args, __ctx);
  }

private:
  typename _Executor::work _M_work;
  _Func _M_function;
  tuple<_Args...> _M_args;
};

template <class... _Values>
struct __await_context_result
{
  typedef tuple<_Values...> _Type;

  template <class... _Args>
  static void _Apply(void* __r, _Args&&... __args)
  {
    *static_cast<_Type*>(__r) = std::make_tuple(forward<_Args>(__args)...);
  }
};

template <class _Value>
struct __await_context_result<_Value>
{
  typedef _Value _Type;

  template <class _Arg>
  static void _Apply(void* __r, _Arg&& __arg)
  {
    *static_cast<_Type*>(__r) = forward<_Arg>(__arg);
  }
};

template <>
struct __await_context_result<>
{
  typedef void _Type;

  static void _Apply(void*)
  {
  }
};

template <class _Executor, class... _Values>
struct __await_context_handler
{
  typedef __await_context_result<_Values...> _Result;

  __await_context_handler(basic_await_context<_Executor> __c)
    : _M_executor(__c._M_executor), _M_impl(__c._M_impl)
  {
  }

  template <class... _Args> void operator()(_Args&&... __args)
  {
    _Result::_Apply(_M_impl->_M_result_value, forward<_Args>(__args)...);
    _M_impl->_Resume();
  }

  _Executor _M_executor;
  shared_ptr<__await_context_impl_base> _M_impl;
};

template <class _Executor, class... _Values>
struct __await_context_handler<_Executor, error_code, _Values...>
{
  typedef __await_context_result<_Values...> _Result;

  __await_context_handler(basic_await_context<_Executor> __c)
    : _M_executor(__c._M_executor), _M_impl(__c._M_impl)
  {
  }

  template <class... _Args> void operator()(const error_code& __e, _Args&&... __args)
  {
    if (_M_impl->_M_result_ec)
      *_M_impl->_M_result_ec = __e;
    else if (__e)
      _M_impl->_M_ex = make_exception_ptr(system_error(__e));
    _Result::_Apply(_M_impl->_M_result_value, forward<_Args>(__args)...);
    _M_impl->_Resume();
  }

  _Executor _M_executor;
  shared_ptr<__await_context_impl_base> _M_impl;
};

template <class _Executor, class... _Values>
struct __await_context_handler<_Executor, exception_ptr, _Values...>
{
  typedef __await_context_result<_Values...> _Result;

  __await_context_handler(basic_await_context<_Executor> __c)
    : _M_executor(__c._M_executor), _M_impl(__c._M_impl)
  {
  }

  template <class... _Args> void operator()(const exception_ptr& __e, _Args&&... __args)
  {
    if (__e)
      _M_impl->_M_ex = make_exception_ptr(__e);
    _Result::_Apply(_M_impl->_M_result_value, forward<_Args>(__args)...);
    _M_impl->_Resume();
  }

  _Executor _M_executor;
  shared_ptr<__await_context_impl_base> _M_impl;
};

template <class _Func>
struct __await_context_call_wrapper
{
  exception_ptr* _M_exception;
  _Func _M_func;

  void operator()()
  {
    try
    {
      _M_func();
    }
    catch (...)
    {
      *_M_exception = current_exception();
    }
  }
};

template <class _Executor>
struct __await_context_executor
{
  exception_ptr* _M_exception;
  _Executor _M_executor;

  struct work
  {
    exception_ptr* _M_exception;
    typename _Executor::work _M_work;

    friend __await_context_executor make_executor(const work& __w)
    {
      return __await_context_executor{__w._M_exception, make_executor(__w._M_work)};
    }
  };

  work make_work() { return work{_M_exception, _M_executor.make_work()}; }

  template <class _F> void post(_F&& __f)
  {
    typedef typename decay<_F>::type _Func;
    _M_executor.post(__await_context_call_wrapper<_Func>{_M_exception, forward<_F>(__f)});
  }

  template <class _F> void dispatch(_F&& __f)
  {
    typedef typename decay<_F>::type _Func;
    _M_executor.dispatch(__await_context_call_wrapper<_Func>{_M_exception, forward<_F>(__f)});
  }

  template <class _Func>
  inline auto wrap(_Func&& __f)
  {
    return (wrap_with_executor)(forward<_Func>(__f), *this);
  }

  execution_context& context()
  {
    return _M_executor.context();
  }

  friend __await_context_executor make_executor(const __await_context_executor& __e)
  {
    return __e;
  }
};

template <class _Executor, class... _Values>
inline auto make_executor(const __await_context_handler<_Executor, _Values...>& __h)
{
  return __await_context_executor<_Executor>{_Get_coroutine_exception(*__h._M_impl), __h._M_executor};
}

template <class _Executor, class... _Values>
class async_result<__await_context_handler<_Executor, _Values...>>
{
public:
  typedef __await_context_handler<_Executor, _Values...> _Handler;
  typedef typename _Handler::_Result _Result;
  typedef __awaitable<typename _Result::_Type> type;

  async_result(_Handler&)
  {
  }

  async_result(const async_result&) = delete;
  async_result& operator=(const async_result&) = delete;

  type get()
  {
    return type();
  }
};

template <class _Executor, class _R, class... _Args>
struct handler_type<basic_await_context<_Executor>, _R(_Args...)>
{
  typedef __await_context_handler<_Executor, typename decay<_Args>::type...> type;
};

template <class _Executor, class _Func>
struct __await_context_launcher
{
  typename _Executor::work _M_work;
  _Func _M_func;

  template <class _F> __await_context_launcher(_F&& __f)
    : _M_work(make_executor(__f).make_work()), _M_func(forward<_F>(__f))
  {
  }

  template <class _F> __await_context_launcher(
    executor_arg_t, const _Executor& __e, _F&& __f)
      : _M_work(make_executor(__e).make_work()), _M_func(forward<_F>(__f))
  {
  }

  template <class... _Args> void operator()(_Args&&... __args)
  {
    auto __impl = make_shared<__await_context_impl<
      _Executor, _Func, typename decay<_Args>::type...>>(
        _M_work, std::move(_M_func), forward<_Args>(__args)...);
    __impl->_Resume();
  }
};

template <class _ExecutorTo, class _Func, class _ExecutorFrom>
struct uses_executor<__await_context_launcher<_ExecutorTo, _Func>, _ExecutorFrom>
  : is_convertible<_ExecutorFrom, _ExecutorTo> {};

template <class _T, class = void>
struct __is_awaitable : false_type {};

template <class _T>
struct __is_awaitable<_T,
  typename enable_if<is_convertible<__last_argument_t<__signature_t<_T>>,
    await_context>::value>::type> : true_type {};

template <class _Func, class _R, class... _Args>
struct handler_type<_Func, _R(_Args...),
  typename enable_if<__is_awaitable<typename decay<_Func>::type>::value
    && !__is_executor_wrapper<typename decay<_Func>::type>::value>::type>
{
  typedef typename decay<_Func>::type _DecayFunc;
  typedef __last_argument_t<__signature_t<_DecayFunc>> _AwaitContext;
  typedef decltype(make_executor(declval<_AwaitContext>())) _Executor;
  typedef __await_context_launcher<_Executor, _DecayFunc> type;
};

template <class _Func, class _FuncSignature, class _Continuation>
class __awaitable_chain;

template <class _Func, class _FuncResult, class... _FuncArgs, class _Continuation>
class __awaitable_chain<_Func, _FuncResult(_FuncArgs...), _Continuation>
{
public:
  template <class _F, class _C> __awaitable_chain(_F&& __f, _C&& __c)
    : _M_func(forward<_F>(__f)), _M_continuation(forward<_C>(__c))
  {
  }

  void operator()(_FuncArgs... __args)
  {
    this->_Invoke(is_same<void, _FuncResult>(), forward<_FuncArgs>(__args)...);
  }

  friend auto make_executor(const __awaitable_chain& __c)
    -> decltype(make_executor(declval<_Func>()))
  {
    return make_executor(__c._M_func);
  }

private:
  void _Invoke(true_type, _FuncArgs... __args)
  {
    auto __ctx(this->_Get_context(forward<_FuncArgs>(__args)...));
    _M_func(forward<_FuncArgs>(__args)...);
    if (_Get_coroutine(__ctx)._Is_complete())
      _M_continuation();
  }

  void _Invoke(false_type, _FuncArgs... __args)
  {
    auto __ctx(this->_Get_context(forward<_FuncArgs>(__args)...));
    auto __r = _M_func(forward<_FuncArgs>(__args)...);
    if (_Get_coroutine(__ctx)._Is_complete())
      _M_continuation(std::move(__r));
  }

  template <class _Arg, class... _Args>
  auto _Get_context(_Arg&&, _Args&& ...__args)
  {
    return this->_Get_impl(forward<_Args>(__args)...);
  }

  template <class _Executor>
  auto _Get_context(const basic_await_context<_Executor>& __ctx)
  {
    return __ctx;
  }

  template <class _Executor>
  auto _Get_context(basic_await_context<_Executor>& __ctx)
  {
    return __ctx;
  }

  template <class _Executor>
  auto _Get_context(basic_await_context<_Executor>&& __ctx)
  {
    return __ctx;
  }

  _Func _M_func;
  _Continuation _M_continuation;
};

template <class _Func>
struct continuation_traits<_Func,
  typename enable_if<__is_awaitable<typename decay<_Func>::type>::value>::type>
{
  typedef typename decay<_Func>::type _DecayFunc;
  typedef __result_t<__signature_t<_DecayFunc>> _Result;
  typedef __make_signature_t<void, _Result> signature;

  template <class _F, class _Continuation>
  static auto chain(_F&& __f, _Continuation&& __c)
  {
    return __awaitable_chain<_DecayFunc, __signature_t<_DecayFunc>,
      typename decay<_Continuation>::type>(
        forward<_F>(__f), forward<_Continuation>(__c));
  }
};

} // namespace experimental
} // namespace std

#endif