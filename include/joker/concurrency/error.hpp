#pragma once

#include <exception>
#include <system_error>
#include <variant>

namespace concurrency {
    class Error {
    public:
        Error(): m_error(std::monostate()){}
        Error(std::exception_ptr e): m_error(std::move(e)) {}
        Error(std::error_code e): m_error(std::move(e)) {}

        bool hasError() const {
            return hasException() || hasSystemError();
        }

        bool hasException() const {
            return std::holds_alternative<std::exception_ptr>(m_error);
        }

        bool hasSystemError() const {
            return std::holds_alternative<std::error_code>(m_error);
        }

        void throwIfError() {
            if(hasException())
                std::rethrow_exception(std::get<std::exception_ptr>(m_error));
            else if(hasError()) {
                throw std::system_error(std::get<std::error_code>(m_error));
            }
        }

    private:
        std::variant<std::monostate, std::exception_ptr, std::error_code> m_error;
    };
}