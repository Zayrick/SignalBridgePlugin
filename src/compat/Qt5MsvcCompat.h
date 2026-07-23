#ifndef SIGNALBRIDGE_QT5_MSVC_COMPAT_H
#define SIGNALBRIDGE_QT5_MSVC_COMPAT_H

#include <cstddef>

// Qt 5.15 uses these non-standard MSVC iterators. They were removed from
// the MSVC 19.50 STL, while raw pointers remain the fallback on other
// compilers and in newer Qt versions.
#if defined(_MSC_VER) && _MSC_VER >= 1950
namespace stdext
{
template<typename Iterator>
constexpr Iterator make_checked_array_iterator(
    Iterator iterator,
    std::size_t) noexcept
{
    return iterator;
}

template<typename Iterator>
constexpr Iterator make_unchecked_array_iterator(Iterator iterator) noexcept
{
    return iterator;
}
}
#endif

#endif // SIGNALBRIDGE_QT5_MSVC_COMPAT_H
