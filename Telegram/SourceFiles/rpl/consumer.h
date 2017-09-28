/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include <gsl/gsl_assert>
#include <rpl/lifetime.h>
#include <rpl/details/callable.h>

namespace rpl {
namespace details {

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
class consumer_handlers;

template <typename Value, typename Error>
class type_erased_handlers {
public:
	template <
		typename OnNext,
		typename OnError,
		typename OnDone>
	static std::shared_ptr<type_erased_handlers> create(
			OnNext &&next,
			OnError &&error,
			OnDone &&done) {
		return std::make_shared<consumer_handlers<
			Value,
			Error,
			std::decay_t<OnNext>,
			std::decay_t<OnError>,
			std::decay_t<OnDone>>>(
				std::forward<OnNext>(next),
				std::forward<OnError>(error),
				std::forward<OnDone>(done));
	}

	virtual bool put_next(Value &&value) = 0;
	virtual bool put_next_copy(const Value &value) = 0;
	virtual void put_error(Error &&error) = 0;
	virtual void put_error_copy(const Error &error) = 0;
	virtual void put_done() = 0;

	void add_lifetime(lifetime &&lifetime);

	template <typename Type, typename... Args>
	Type *make_state(Args&& ...args);

	void terminate();

protected:
	lifetime _lifetime;
	bool _terminated = false;

};

template <typename Handlers>
struct is_type_erased_handlers
	: std::false_type {
};

template <typename Value, typename Error>
struct is_type_erased_handlers<type_erased_handlers<Value, Error>>
	: std::true_type {
};

template <typename Value, typename Error, typename OnNext, typename OnError, typename OnDone>
class consumer_handlers final
	: public type_erased_handlers<Value, Error> {
public:
	template <
		typename OnNextOther,
		typename OnErrorOther,
		typename OnDoneOther>
	consumer_handlers(
		OnNextOther &&next,
		OnErrorOther &&error,
		OnDoneOther &&done)
		: _next(std::forward<OnNextOther>(next))
		, _error(std::forward<OnErrorOther>(error))
		, _done(std::forward<OnDoneOther>(done)) {
	}

	template <
		typename OnNextOther,
		typename OnErrorOther,
		typename OnDoneOther>
	static std::shared_ptr<consumer_handlers> create(
			OnNextOther &&next,
			OnErrorOther &&error,
			OnDoneOther &&done) {
		return std::make_shared<consumer_handlers>(
				std::forward<OnNextOther>(next),
				std::forward<OnErrorOther>(error),
				std::forward<OnDoneOther>(done));
	}

	bool put_next(Value &&value) final override;
	bool put_next_copy(const Value &value) final override;
	void put_error(Error &&error) final override;
	void put_error_copy(const Error &error) final override;
	void put_done() final override;

private:
	OnNext _next;
	OnError _error;
	OnDone _done;

};

template <typename Value, typename Error>
inline void type_erased_handlers<Value, Error>::add_lifetime(
		lifetime &&lifetime) {
	if (_terminated) {
		lifetime.destroy();
	} else {
		_lifetime.add(std::move(lifetime));
	}
}

template <typename Value, typename Error>
template <typename Type, typename... Args>
inline Type *type_erased_handlers<Value, Error>::make_state(
		Args&& ...args) {
	Expects(!_terminated);
	return _lifetime.make_state<Type>(std::forward<Args>(args)...);
}

template <typename Value, typename Error>
inline void type_erased_handlers<Value, Error>::terminate() {
	if (!_terminated) {
		_terminated = true;
		base::take(_lifetime).destroy();
	}
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
bool consumer_handlers<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone
>::put_next(Value &&value) {
	if (this->_terminated) {
		return false;
	}
	auto handler = this->_next;
	details::callable_invoke(std::move(handler), std::move(value));
	return true;
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
bool consumer_handlers<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone
>::put_next_copy(const Value &value) {
	if (this->_terminated) {
		return false;
	}
	auto handler = this->_next;
	details::const_ref_call_invoke(std::move(handler), value);
	return true;
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
void consumer_handlers<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone
>::put_error(Error &&error) {
	if (!this->_terminated) {
		details::callable_invoke(
			std::move(this->_error),
			std::move(error));
		this->terminate();
	}
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
void consumer_handlers<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone
>::put_error_copy(const Error &error) {
	if (!this->_terminated) {
		details::const_ref_call_invoke(
			std::move(this->_error),
			error);
		this->terminate();
	}
}

template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone>
void consumer_handlers<
	Value,
	Error,
	OnNext,
	OnError,
	OnDone
>::put_done() {
	if (!this->_terminated) {
		std::move(this->_done)();
		this->terminate();
	}
}

} // namespace details

struct no_value {
	no_value() = delete;
};

struct no_error {
	no_error() = delete;
};

struct empty_value {
};

struct empty_error {
};

template <
	typename Value = empty_value,
	typename Error = no_error,
	typename Handlers = details::type_erased_handlers<Value, Error>>
class consumer;

namespace details {

template <typename Value, typename Error, typename Handlers>
class consumer_base {
	using is_type_erased = is_type_erased_handlers<Handlers>;

public:
	template <
		typename OnNext,
		typename OnError,
		typename OnDone>
	consumer_base(
		OnNext &&next,
		OnError &&error,
		OnDone &&done);

	bool put_next(Value &&value) const;
	bool put_next_copy(const Value &value) const;
	bool put_next_forward(Value &&value) const {
		return put_next(std::move(value));
	}
	bool put_next_forward(const Value &value) const {
		return put_next_copy(value);
	}
	void put_error(Error &&error) const;
	void put_error_copy(const Error &error) const;
	void put_error_forward(Error &&error) const {
		return put_error(std::move(error));
	}
	void put_error_forward(const Error &error) const {
		return put_error_copy(error);
	}
	void put_done() const;

	void add_lifetime(lifetime &&lifetime) const;

	template <typename Type, typename... Args>
	Type *make_state(Args&& ...args) const;

	void terminate() const;

	const details::type_erased_handlers<Value, Error> *comparable() const {
		return _handlers.get();
	}

private:
	template <
		typename OtherHandlers,
		typename = std::enable_if_t<
			std::is_base_of_v<Handlers, OtherHandlers>>>
	consumer_base(const std::shared_ptr<OtherHandlers> &handlers)
	: _handlers(handlers) {
	}

	template <
		typename OtherHandlers,
		typename = std::enable_if_t<
			std::is_base_of_v<Handlers, OtherHandlers>>>
	consumer_base(std::shared_ptr<OtherHandlers> &&handlers)
	: _handlers(std::move(handlers)) {
	}

	mutable std::shared_ptr<Handlers> _handlers;

	bool handlers_put_next(Value &&value, std::true_type) const {
		return _handlers->put_next(std::move(value));
	}
	bool handlers_put_next_copy(
			const Value &value,
			std::true_type) const {
		return _handlers->put_next_copy(value);
	}
	void handlers_put_error(Error &&error, std::true_type) const {
		return std::exchange(_handlers, nullptr)->put_error(std::move(error));
	}
	void handlers_put_error_copy(
			const Error &error,
			std::true_type) const {
		return std::exchange(_handlers, nullptr)->put_error_copy(error);
	}
	void handlers_put_done(std::true_type) const {
		return std::exchange(_handlers, nullptr)->put_done();
	}

	bool handlers_put_next(Value &&value, std::false_type) const {
		return _handlers->Handlers::put_next(std::move(value));
	}
	bool handlers_put_next_copy(
			const Value &value,
			std::false_type) const {
		return _handlers->Handlers::put_next_copy(value);
	}
	void handlers_put_error(Error &&error, std::false_type) const {
		return std::exchange(_handlers, nullptr)->Handlers::put_error(std::move(error));
	}
	void handlers_put_error_copy(
			const Error &error,
			std::false_type) const {
		return std::exchange(_handlers, nullptr)->Handlers::put_error_copy(error);
	}
	void handlers_put_done(std::false_type) const {
		return std::exchange(_handlers, nullptr)->Handlers::put_done();
	}

	template <
		typename OtherValue,
		typename OtherError,
		typename OtherHandlers>
	friend class ::rpl::consumer;

};

template <typename Value, typename Error, typename Handlers>
template <typename OnNext, typename OnError, typename OnDone>
inline consumer_base<Value, Error, Handlers>::consumer_base(
	OnNext &&next,
	OnError &&error,
	OnDone &&done) : _handlers(Handlers::create(
		std::forward<OnNext>(next),
		std::forward<OnError>(error),
		std::forward<OnDone>(done))) {
}

template <typename Value, typename Error, typename Handlers>
inline bool consumer_base<Value, Error, Handlers>::put_next(
		Value &&value) const {
	if (_handlers) {
		if (handlers_put_next(std::move(value), is_type_erased())) {
			return true;
		}
		_handlers = nullptr;
	}
	return false;
}

template <typename Value, typename Error, typename Handlers>
inline bool consumer_base<Value, Error, Handlers>::put_next_copy(
		const Value &value) const {
	if (_handlers) {
		if (handlers_put_next_copy(value, is_type_erased())) {
			return true;
		}
		_handlers = nullptr;
	}
	return false;
}

template <typename Value, typename Error, typename Handlers>
inline void consumer_base<Value, Error, Handlers>::put_error(
		Error &&error) const {
	if (_handlers) {
		handlers_put_error(std::move(error), is_type_erased());
	}
}

template <typename Value, typename Error, typename Handlers>
inline void consumer_base<Value, Error, Handlers>::put_error_copy(
		const Error &error) const {
	if (_handlers) {
		handlers_put_error_copy(error, is_type_erased());
	}
}

template <typename Value, typename Error, typename Handlers>
inline void consumer_base<Value, Error, Handlers>::put_done() const {
	if (_handlers) {
		handlers_put_done(is_type_erased());
	}
}

template <typename Value, typename Error, typename Handlers>
inline void consumer_base<Value, Error, Handlers>::add_lifetime(
		lifetime &&lifetime) const {
	if (_handlers) {
		_handlers->add_lifetime(std::move(lifetime));
	} else {
		lifetime.destroy();
	}
}

template <typename Value, typename Error, typename Handlers>
template <typename Type, typename... Args>
inline Type *consumer_base<Value, Error, Handlers>::make_state(
		Args&& ...args) const {
	Expects(_handlers != nullptr);
	return _handlers->template make_state<Type>(
		std::forward<Args>(args)...);
}

template <typename Value, typename Error, typename Handlers>
inline void consumer_base<Value, Error, Handlers>::terminate() const {
	if (_handlers) {
		std::exchange(_handlers, nullptr)->terminate();
	}
}

template <typename Value, typename Error>
using consumer_base_type_erased = consumer_base<
	Value,
	Error,
	details::type_erased_handlers<Value, Error>>;

template <typename Value, typename Error, typename Handlers>
constexpr bool is_specific_handlers_v = !std::is_same_v<
	details::type_erased_handlers<Value, Error>,
	Handlers
> && std::is_base_of_v<
	details::type_erased_handlers<Value, Error>,
	Handlers
>;

} // namespace details

template <typename Value, typename Error, typename Handlers>
class consumer final
: public details::consumer_base<Value, Error, Handlers> {
	using parent_type = details::consumer_base<
		Value,
		Error,
		Handlers>;

public:
	using parent_type::parent_type;

};

template <typename Value, typename Error>
class consumer<Value, Error, details::type_erased_handlers<Value, Error>> final
: public details::consumer_base_type_erased<Value, Error> {
	using parent_type = details::consumer_base_type_erased<
		Value,
		Error>;

public:
	using parent_type::parent_type;

	template <
		typename Handlers,
		typename = std::enable_if_t<
			details::is_specific_handlers_v<Value, Error, Handlers>>>
	consumer(const details::consumer_base<Value, Error, Handlers> &other)
	: parent_type(other._handlers) {
	}

	template <
		typename Handlers,
		typename = std::enable_if_t<
			details::is_specific_handlers_v<Value, Error, Handlers>>>
	consumer(details::consumer_base<Value, Error, Handlers> &&other)
	: parent_type(std::move(other._handlers)) {
	}

	template <
		typename Handlers,
		typename = std::enable_if_t<
			details::is_specific_handlers_v<Value, Error, Handlers>>>
	consumer &operator=(
			const details::consumer_base<Value, Error, Handlers> &other) {
		this->_handlers = other._handlers;
		return *this;
	}

	template <
		typename Handlers,
		typename = std::enable_if_t<
			details::is_specific_handlers_v<Value, Error, Handlers>>>
	consumer &operator=(
			details::consumer_base<Value, Error, Handlers> &&other) {
		this->_handlers = std::move(other._handlers);
		return *this;
	}

};

template <
	typename Value,
	typename Error,
	typename Handlers1,
	typename Handlers2>
inline bool operator==(
		const consumer<Value, Error, Handlers1> &a,
		const consumer<Value, Error, Handlers2> &b) {
	return a.comparable() == b.comparable();
}

template <
	typename Value,
	typename Error,
	typename Handlers1,
	typename Handlers2>
inline bool operator<(
		const consumer<Value, Error, Handlers1> &a,
		const consumer<Value, Error, Handlers2> &b) {
	return a.comparable() < b.comparable();
}

template <
	typename Value,
	typename Error,
	typename Handlers1,
	typename Handlers2>
inline bool operator!=(
		const consumer<Value, Error, Handlers1> &a,
		const consumer<Value, Error, Handlers2> &b) {
	return !(a == b);
}

template <
	typename Value,
	typename Error,
	typename Handlers1,
	typename Handlers2>
inline bool operator>(
		const consumer<Value, Error, Handlers1> &a,
		const consumer<Value, Error, Handlers2> &b) {
	return b < a;
}

template <
	typename Value,
	typename Error,
	typename Handlers1,
	typename Handlers2>
inline bool operator<=(
		const consumer<Value, Error, Handlers1> &a,
		const consumer<Value, Error, Handlers2> &b) {
	return !(b < a);
}

template <
	typename Value,
	typename Error,
	typename Handlers1,
	typename Handlers2>
inline bool operator>=(
		const consumer<Value, Error, Handlers1> &a,
		const consumer<Value, Error, Handlers2> &b) {
	return !(a < b);
}

// GCC 7.2 can't handle not type-erased consumers.
// It eats up 4GB RAM + 16GB swap on the unittest and dies.
// Clang and Visual C++ both handle it without such problems.
template <
	typename Value,
	typename Error,
	typename OnNext,
	typename OnError,
	typename OnDone,
	typename = std::enable_if_t<
		details::is_callable_v<OnNext, Value> &&
		details::is_callable_v<OnError, Error> &&
		details::is_callable_v<OnDone>>>
#ifdef COMPILER_GCC
inline consumer<Value, Error> make_consumer(
#else // COMPILER_GCC
inline auto make_consumer(
#endif // COMPILER_GCC
		OnNext &&next,
		OnError &&error,
		OnDone &&done) {
	return consumer<Value, Error, details::consumer_handlers<
		Value,
		Error,
		std::decay_t<OnNext>,
		std::decay_t<OnError>,
		std::decay_t<OnDone>>>(
			std::forward<OnNext>(next),
			std::forward<OnError>(error),
			std::forward<OnDone>(done));
}

} // namespace rpl