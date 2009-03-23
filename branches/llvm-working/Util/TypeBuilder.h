// -*- C++ -*-
#ifndef UTIL_TYPEBUILDER_H
#define UTIL_TYPEBUILDER_H

#ifndef __cplusplus
#error This header expects to be included only in C++ source
#endif

#include "llvm/DerivedTypes.h"

namespace llvm { class Module; }

namespace py {

/// TypeBuilder<T>::cache(module) will return an llvm::Type*
/// isomorphic to T, and may cache it in module.  You need to
/// specialize it for types LLVM doesn't natively know about.
template<typename T> class TypeBuilder {};
template<typename T> class TypeBuilder<const T> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        // LLVM doesn't have const.
        return TypeBuilder<T>::cache(m);
    }
};
template<typename T> class TypeBuilder<T*> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::PointerType::getUnqual(TypeBuilder<T>::cache(m));
    }
};
template<typename T> class TypeBuilder<T[]> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::ArrayType::get(TypeBuilder<T>::cache(m), 0);
    }
};
template<typename T, size_t N> class TypeBuilder<T[N]> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::ArrayType::get(TypeBuilder<T>::cache(m), N);
    }
};
template<> class TypeBuilder<void> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::Type::VoidTy;
    }
};
template<> class TypeBuilder<char> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::Type::Int8Ty;
    }
};
template<> class TypeBuilder<int> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::IntegerType::get(sizeof(int) * 8);
    }
};
template<> class TypeBuilder<Py_ssize_t> {
public:
    static const llvm::Type *cache(llvm::Module *m) {
        return llvm::IntegerType::get(sizeof(Py_ssize_t) * 8);
    }
};
template<typename R> class TypeBuilder<R()> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, false);
    }
};
template<typename R, typename A1> class TypeBuilder<R(A1)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        params.reserve(1);
        params.push_back(TypeBuilder<A1>::cache(m));
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, false);
    }
};
template<typename R, typename A1, typename A2> class TypeBuilder<R(A1, A2)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        params.reserve(2);
        params.push_back(TypeBuilder<A1>::cache(m));
        params.push_back(TypeBuilder<A2>::cache(m));
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, false);
    }
};
template<typename R, typename A1, typename A2, typename A3>
class TypeBuilder<R(A1, A2, A3)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        params.reserve(3);
        params.push_back(TypeBuilder<A1>::cache(m));
        params.push_back(TypeBuilder<A2>::cache(m));
        params.push_back(TypeBuilder<A3>::cache(m));
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, false);
    }
};

template<typename R> class TypeBuilder<R(...)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, true);
    }
};
template<typename R, typename A1> class TypeBuilder<R(A1, ...)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        params.reserve(1);
        params.push_back(TypeBuilder<A1>::cache(m));
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, true);
    }
};
template<typename R, typename A1, typename A2>
class TypeBuilder<R(A1, A2, ...)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        params.reserve(2);
        params.push_back(TypeBuilder<A1>::cache(m));
        params.push_back(TypeBuilder<A2>::cache(m));
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, true);
    }
};
template<typename R, typename A1, typename A2, typename A3>
class TypeBuilder<R(A1, A2, A3, ...)> {
public:
    static const llvm::FunctionType *cache(llvm::Module *m) {
        std::vector<const llvm::Type*> params;
        params.reserve(3);
        params.push_back(TypeBuilder<A1>::cache(m));
        params.push_back(TypeBuilder<A2>::cache(m));
        params.push_back(TypeBuilder<A3>::cache(m));
        return llvm::FunctionType::get(TypeBuilder<R>::cache(m), params, true);
    }
};

}  // namespace py

#endif UTIL_TYPEBUILDER_H
