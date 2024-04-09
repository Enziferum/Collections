#pragma once
#include <memory>
#include <type_traits>
#include <stdexcept>

namespace collections::rstd {

    struct unique_function_bad_callable: std::logic_error {
        explicit unique_function_bad_callable(const std::string& message): std::logic_error(message){}
        using std::logic_error::what;
    };

    template<typename R, typename ...Args>
    class function_bridge {
    public:
        virtual ~function_bridge() noexcept = 0;
        virtual R invoke(Args&& ... args) = 0;
    };

    template<typename R, typename ...Args>
    function_bridge<R, Args...>::~function_bridge() noexcept = default;

    template<typename Func, typename R, typename ...Args>
    class specific_bridge: public function_bridge<R, Args...> {
    public:
        specific_bridge(Func&& func): m_func(std::forward<Func>(func)) {}
        virtual ~specific_bridge() noexcept override;

        R invoke(Args&& ... args) override {
            return m_func(std::forward<Args>(args)...);
        }
    private:
        Func m_func;
    };

    template<typename Func, typename R, typename ...Args>
    specific_bridge<Func, R, Args...>::~specific_bridge() noexcept = default;

    template<typename Func, typename ...Args>
    class specific_bridge<Func, void, Args...>: public function_bridge<void, Args...>{
    public:
        specific_bridge(Func&& func): m_func(std::forward<Func>(func)) {}
        virtual ~specific_bridge() noexcept override;

        void invoke(Args&& ... args) override {
            m_func(std::forward<Args>(args)...);
        }
    private:
        Func m_func;
    };

    template<typename Func, typename ...Args>
    specific_bridge<Func, void, Args...>::~specific_bridge() noexcept = default;

    template<typename Signature>
    class unique_function{};

    template<typename R, typename ...Args>
    class unique_function<R(Args...)> {
    public:
        unique_function() noexcept = default;

        template<typename Func,
                typename = std::enable_if_t<!std::is_same_v<unique_function<R(Args...)>, std::decay_t<Func>>>>
        unique_function(Func&& func) {
            static_assert(std::is_move_constructible<std::decay_t<Func>>::value,
                          "rstl::unique_function target must be move-constructible");
            if(m_bridge)
                m_bridge.reset();

            using Func_D = std::decay_t<Func>;
            m_bridge = std::make_unique<specific_bridge<Func, R, Args...>>
                (std::forward<Func>(func));
        }

        unique_function(const unique_function& other) = delete;
        unique_function& operator=(const unique_function& other) = delete;

        unique_function(unique_function&& other) noexcept: m_bridge{std::move(other.m_bridge)} {}

        unique_function& operator=(unique_function&& other) noexcept{
            if(this == &other)
                return *this;
            unique_function(std::move(other)).swap(*this);
            return *this;
        }

        ~unique_function() noexcept;

        explicit operator bool () const noexcept {
            return (m_bridge != nullptr);
        }

        void swap(unique_function& other) noexcept{
            std::swap(m_bridge, other.m_bridge);
        }

        R operator()(Args&& ... args) const {
            if(!m_bridge)
                throw unique_function_bad_callable("rstd::unique_function, create callable before invoke");
            return m_bridge -> invoke(std::forward<Args>(args)...);
        }
    private:
        std::unique_ptr<function_bridge<R, Args...>> m_bridge{nullptr};
    };

    template<typename R, typename ...Args>
    unique_function<R(Args...)>::~unique_function() noexcept = default;

    template<typename ...Args>
    class unique_function<void(Args...)> {
    public:
        unique_function() noexcept = default;

        template<typename Func,
                typename = std::enable_if_t<!std::is_same_v<unique_function<void(Args...)>, std::decay_t<Func>>>>
        unique_function(Func&& func) {
            static_assert(std::is_move_constructible<std::decay_t<Func>>::value,
                          "rstl::unique_function target must be move-constructible");

            if(m_bridge)
                m_bridge.reset();

            using func_d = std::decay_t<Func>;
            m_bridge = std::make_unique<specific_bridge<Func, void, Args...>>
                    (std::forward<Func>(func));

        }

        unique_function(const unique_function& other) = delete;
        unique_function& operator=(const unique_function& other) = delete;

        unique_function(unique_function&& other) noexcept: m_bridge{std::move(other.m_bridge)} {
             other.m_bridge = nullptr;
        }

        unique_function& operator=(unique_function&& other) noexcept {
            if(this == &other)
                return *this;
            unique_function(std::move(other)).swap(*this);
            return *this;
        }

        ~unique_function() noexcept;

        explicit operator bool () const noexcept {
            return (m_bridge != nullptr);
        }

        void swap(unique_function& other) noexcept {
            std::swap(m_bridge, other.m_bridge);
        }

        void operator()(Args&& ... args) const {
            if(!m_bridge)
                throw unique_function_bad_callable("rstd::unique_function, create callable before invoke");
            m_bridge -> invoke(std::forward<Args>(args)...);
        }
    private:
        std::unique_ptr<function_bridge<void, Args...>> m_bridge{nullptr};
    };

    template<typename ...Args>
    unique_function<void(Args...)>::~unique_function() noexcept = default;
}

namespace std {
    template<typename Signature>
    inline void swap(rstl::unique_function<Signature>& left, rstl::unique_function<Signature>& right) {
        left.swap(right);
    }
}