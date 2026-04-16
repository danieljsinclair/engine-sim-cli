#ifndef _VERIFICATION_H_
#define _VERIFICATION_H_

#define ASSERT_2(condition, message) \
        if (!(condition)) { \
            throw std::runtime_error(message); \
        }

#define ASSERT_3(condition, message, onErrorFunc) \
        if (!(condition)) { \
            try { \
                onErrorFunc(); \
            } catch (const std::exception& e) { \
                throw std::runtime_error(std::string(message) + " + " + e.what()); \
            } \
            throw std::runtime_error(message); \
        }

#define ASSERT_GET_MACRO(_1, _2, _3, NAME, ...) NAME
#define ASSERT(...) ASSERT_GET_MACRO(__VA_ARGS__, ASSERT_3, ASSERT_2)(__VA_ARGS__)

#endif// _VERIFICATION_H_