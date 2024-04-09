#pragma once
#include <type_traits>

#include "error.hpp"

namespace collections::concurrency {
    namespace detail {
        template<typename T>
        class ValueStorage {
            using Storage = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
        public:
            template<typename ... Args>
            void construct(Args&& ... args) {
                new (&m_storage) T(std::forward<Args>(args)...);
            }

            void moveConstruct(T&& value) {
                new (&m_storage) T(std::move(value));
            }

            void copyConstruct(const T& value) {
                new (&m_storage) T(value);
            }

            T& refUnsafe() {
                return *ptrUnsafe();
            }

            const T& constRefUnsafe() const {
                return *ptrUnsafe();
            }

            T* ptrUnsafe() {
                return reinterpret_cast<T*>(&m_storage);
            }

            const T* ptrUnsafe() const {
                return reinterpret_cast<const T*>(&m_storage);
            }

            void destroy() noexcept {
                ptrUnsafe()->~T();
            }
        private:
            Storage m_storage;
        };
    }


    template<typename T>
    class [[nodiscard]] Result {
    public:
        static_assert(!std::is_reference<T>::value,
                      "Reference types are not supported");

        using type = T;

        template <typename... Arguments>
        static Result Ok(Arguments && ... arguments) {
            Result result;
            result.m_value.construct(std::forward<Arguments>(arguments)...);
            return result;
        }

        static Result Ok(T&& value) {
            return Result(std::move(value));
        }

        static Result Fail(Error error) {
            return Result(std::move(error));
        }


        Result(Result&& other) {
            moveImpl(std::move(other));
        }

        Result& operator=(Result&& other) {
            destroyIfExists();
            moveImpl(std::move(other));
            return *this;
        }

        Result(const Result& other) {
            destroyIfExists();
            copyImpl(other);
        }

        Result& operator=(const Result& other) {
            destroyIfExists();
            moveImpl(std::move(other));
            return *this;
        }

        ~Result() noexcept {
            destroyIfExists();
        }

        T& ValueUnsafe() & {
            return m_value.refUnsafe();
        }

        const T& ValueUnsafe() const & {
            return m_value.constRefUnsafe();
        }

        T&& ValueUnsafe() && {
            return std::move(m_value.refUnsafe());
        }

        T& ValueOrThrow() & {
            throwIfError();
            return m_value.refUnsafe();
        }

        const T& ValueOrThrow() const & {
            throwIfError();
            return m_value.constRefUnsafe();
        }

        T&& ValueOrThrow() && {
            throwIfError();
            return std::move(m_value.refUnsafe());
        }

        bool hasError() const {
            return m_error.hasError();
        }

        bool isOk() const {
            return !hasError();
        }

        bool hasValue() const {
            return !hasError();
        }

        T& operator*() & {
            return m_value.refUnsafe();
        }

        const T& operator*() const & {
            return m_value.constRefUnsafe();
        }

        T&& operator*() && {
            return std::move(m_value.refUnsafe());
        }

        T* operator ->() {
            return m_value.ptrUnsafe();
        }

        const T* operator ->() const {
            return m_value.ptrUnsafe();
        }

    private:
        Result() = default;

        Result(T&& value) {
            m_value.moveConstruct(std::move(value));
        }

        Result(const T& value) {
            m_value.copyConstruct(value);
        }

        Result(Error e) {
            m_error = std::move(e);
        }

        void throwIfError() {
            m_error.throwIfError();
        }

        void moveImpl(Result&& other) {
            m_error = std::move(other.m_error);
            if(hasValue()) {
                m_value.moveConstruct(std::move(other.ValueUnsafe()));
            }
        }

        void copyImpl(const Result& other) {
            m_error = other.m_error;
            if(hasValue()) {
                m_value.copyConstruct(other.ValueUnsafe());
            }
        }

        void destroyIfExists() {
            if(isOk())
                m_value.destroy();
        }
    private:
        detail::ValueStorage<T> m_value;
        Error m_error;
    };


    template<>
    class Result<void> {
    public:
        using type = void;
    };

} // namespace collections::concurrency
