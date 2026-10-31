#pragma once

namespace WrathIdle
{
// Only the caller's explicitly borrowed entity types may be invalidated. Owned services and scalar state survive.
template <template <class> class TypedValue, class... Borrowed, class Untyped>
void ClearBorrowedValues(Untyped* value)
{
    if (!value)
        return;
    auto clear = [&]<class T>()
    {
        if (auto borrowed = dynamic_cast<TypedValue<T>*>(value))
        {
            value->Reset();
            borrowed->Set(T{});
        }
    };
    (clear.template operator()<Borrowed>(), ...);
}
}
