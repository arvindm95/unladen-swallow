#include "Python/ll_compile.h"

#include "Python.h"
#include "code.h"
#include "frameobject.h"

#include "Util/TypeBuilder.h"

#include "llvm/BasicBlock.h"
#include "llvm/Constant.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Intrinsics.h"
#include "llvm/Module.h"
#include "llvm/Type.h"

#include <vector>

using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantInt;
using llvm::Function;
using llvm::FunctionType;
using llvm::IntegerType;
using llvm::Module;
using llvm::PointerType;
using llvm::Type;
using llvm::Value;

namespace py {

// Copied from ceval.cc:
#define NAME_ERROR_MSG \
	"name '%.200s' is not defined"
#define GLOBAL_NAME_ERROR_MSG \
	"global name '%.200s' is not defined"
#define UNBOUNDLOCAL_ERROR_MSG \
	"local variable '%.200s' referenced before assignment"
#define UNBOUNDFREE_ERROR_MSG \
	"free variable '%.200s' referenced before assignment" \
        " in enclosing scope"

template<typename T, size_t N>
T* end(T (&array)[N])
{
    return array + N;
}

static ConstantInt *
get_signed_constant_int(const Type *type, int64_t v)
{
    return ConstantInt::get(type, static_cast<uint64_t>(v), true /* signed */);
}

template<> class TypeBuilder<PyObject> {
public:
    static const Type *cache(Module *module) {
        std::string pyobject_name("__pyobject");
        const Type *result = module->getTypeByName(pyobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with object.h.
        llvm::PATypeHolder object_ty = llvm::OpaqueType::get();
        Type *p_object_ty = PointerType::getUnqual(object_ty);
        llvm::StructType *temp_object_ty = llvm::StructType::get(
#ifdef Py_TRACE_REFS
            // _ob_next, _ob_prev
            p_object_ty, p_object_ty,
#endif
            TypeBuilder<ssize_t>::cache(module),
            p_object_ty,
            NULL);
        llvm::cast<llvm::OpaqueType>(object_ty.get())
            ->refineAbstractTypeTo(temp_object_ty);
        module->addTypeName(pyobject_name, object_ty.get());
        return object_ty.get();
    }

    enum Fields {
#ifdef Py_TRACE_REFS
        FIELD_NEXT,
        FIELD_PREV,
#endif
        FIELD_REFCNT,
        FIELD_TYPE,
    };
};
typedef TypeBuilder<PyObject> ObjectTy;

template<> class TypeBuilder<PyTupleObject> {
public:
    static const Type *cache(Module *module) {
        std::string pytupleobject_name("__pytupleobject");
        const Type *result = module->getTypeByName(pytupleobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with code.h.
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            TypeBuilder<PyObject>::cache(module),
            // From PyObject_VAR_HEAD
            TypeBuilder<ssize_t>::cache(module),
            // From PyTupleObject
            TypeBuilder<PyObject*[]>::cache(module),  // ob_item
            NULL);

        module->addTypeName(pytupleobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_SIZE,
        FIELD_ITEM,
    };
};
typedef TypeBuilder<PyTupleObject> TupleTy;

template<> class TypeBuilder<PyCodeObject> {
public:
    static const Type *cache(Module *module) {
        std::string pycodeobject_name("__pycodeobject");
        const Type *result = module->getTypeByName(pycodeobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with code.h.
        const Type *p_pyobject_type = TypeBuilder<PyObject*>::cache(module);
        const Type *int_type = TypeBuilder<int>::cache(module);
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            TypeBuilder<PyObject>::cache(module),
            // From PyCodeObject
            int_type,  // co_argcount
            int_type,  // co_nlocals
            int_type,  // co_stacksize
            int_type,  // co_flags
            p_pyobject_type,  // co_code
            p_pyobject_type,  // co_consts
            p_pyobject_type,  // co_names
            p_pyobject_type,  // co_varnames
            p_pyobject_type,  // co_freevars
            p_pyobject_type,  // co_cellvars
            //  Not bothering with defining the Inst struct.
            TypeBuilder<char*>::cache(module),  // co_tcode
            p_pyobject_type,  // co_filename
            p_pyobject_type,  // co_name
            int_type,  // co_firstlineno
            p_pyobject_type,  // co_lnotab
            TypeBuilder<char*>::cache(module),  //co_zombieframe
            p_pyobject_type,  // co_llvm_function
            NULL);

        module->addTypeName(pycodeobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT,
        FIELD_ARGCOUNT,
        FIELD_NLOCALS,
        FIELD_STACKSIZE,
        FIELD_FLAGS,
        FIELD_CODE,
        FIELD_CONSTS,
        FIELD_NAMES,
        FIELD_VARNAMES,
        FIELD_FREEVARS,
        FIELD_CELLVARS,
        FIELD_TCODE,
        FIELD_FILENAME,
        FIELD_NAME,
        FIELD_FIRSTLINENO,
        FIELD_LNOTAB,
        FIELD_ZOMBIEFRAME,
        FIELD_LLVM_FUNCTION,
    };
};
typedef TypeBuilder<PyCodeObject> CodeTy;

template<> class TypeBuilder<PyTryBlock> {
public:
    static const Type *cache(Module *module) {
        const Type *int_type = TypeBuilder<int>::cache(module);
        return llvm::StructType::get(
            // b_type, b_handler, b_level
            int_type, int_type, int_type, NULL);
    }
    enum Fields {
        FIELD_TYPE,
        FIELD_HANDLER,
        FIELD_LEVEL,
    };
};

template<> class TypeBuilder<PyFrameObject> {
public:
    static const Type *cache(Module *module) {
        std::string pyframeobject_name("__pyframeobject");
        const Type *result = module->getTypeByName(pyframeobject_name);
        if (result != NULL)
            return result;

        // Keep this in sync with frameobject.h.
        const Type *p_pyobject_type = TypeBuilder<PyObject*>::cache(module);
        const Type *int_type = TypeBuilder<int>::cache(module);
        result = llvm::StructType::get(
            // From PyObject_HEAD. In C these are directly nested
            // fields, but the layout should be the same when it's
            // represented as a nested struct.
            ObjectTy::cache(module),
            // From PyObject_VAR_HEAD
            TypeBuilder<ssize_t>::cache(module),
            // From struct _frame
            p_pyobject_type,  // f_back
            TypeBuilder<PyCodeObject*>::cache(module),  // f_code
            p_pyobject_type,  // f_builtins
            p_pyobject_type,  // f_globals
            p_pyobject_type,  // f_locals
            TypeBuilder<PyObject**>::cache(module),  // f_valuestack
            TypeBuilder<PyObject**>::cache(module),  // f_stacktop
            p_pyobject_type,  // f_trace
            p_pyobject_type,  // f_exc_type
            p_pyobject_type,  // f_exc_value
            p_pyobject_type,  // f_exc_traceback
            // f_tstate; punt on the type:
            TypeBuilder<char*>::cache(module),
            int_type,  // f_lasti
            int_type,  // f_lineno
            int_type,  // f_iblock
            // f_blockstack:
            TypeBuilder<PyTryBlock[CO_MAXBLOCKS]>::cache(module),
            // f_localsplus, flexible array.
            TypeBuilder<PyObject*[]>::cache(module),
            NULL);

        module->addTypeName(pyframeobject_name, result);
        return result;
    }

    enum Fields {
        FIELD_OBJECT_HEAD,
        FIELD_OB_SIZE,
        FIELD_BACK,
        FIELD_CODE,
        FIELD_BUILTINS,
        FIELD_GLOBALS,
        FIELD_LOCALS,
        FIELD_VALUESTACK,
        FIELD_STACKTOP,
        FIELD_TRACE,
        FIELD_EXC_TYPE,
        FIELD_EXC_VALUE,
        FIELD_EXC_TRACEBACK,
        FIELD_TSTATE,
        FIELD_LASTI,
        FIELD_LINENO,
        FIELD_IBLOCK,
        FIELD_BLOCKSTACK,
        FIELD_LOCALSPLUS,
    };
};
typedef TypeBuilder<PyFrameObject> FrameTy;

static const llvm::FunctionType *
get_function_type(Module *module)
{
    std::string function_type_name("__function_type");
    const llvm::FunctionType *result =
        llvm::cast_or_null<llvm::FunctionType>(
            module->getTypeByName(function_type_name));
    if (result != NULL)
        return result;

    result = TypeBuilder<PyObject*(PyFrameObject*)>::cache(module);
    module->addTypeName(function_type_name, result);
    return result;
}

template<typename FunctionType>
static Function *
get_global_function(Module *module, const char *name)
{
    Function *result = module->getFunction(name);
    if (result == NULL) {
        result = llvm::cast<Function>(
            module->getOrInsertFunction(
                name,
                TypeBuilder<FunctionType>::cache(module)));
        result->setLinkage(llvm::GlobalValue::ExternalLinkage);
    }
    return result;
}

static llvm::GlobalVariable *
get_py_reftotal(Module *module)
{
    const std::string py_reftotal_name("_Py_RefTotal");
    llvm::GlobalVariable *result = module->getGlobalVariable(py_reftotal_name);
    if (result != NULL)
        return result;

    // The Module keeps ownership of the new GlobalVariable, and will
    // return it the next time we call getGlobalVariable().
    return new llvm::GlobalVariable(
        IntegerType::get(sizeof(Py_ssize_t) * 8),
        false,  // Not constant.
        llvm::GlobalValue::ExternalLinkage,
        // NULL intializer makes this a declaration, to be imported from the
        // main Python executable.
        NULL,
        py_reftotal_name,
        module);
}

static Function *
get_py_negativerefcount(Module *module)
{
    const std::string py_negativerefcount_name("_Py_NegativeRefcount");
    llvm::Function *result = module->getFunction(py_negativerefcount_name);
    if (result != NULL)
        return result;

    // The Module keeps ownership of the new Function, and will return
    // it the next time we call getFunction().
    return Function::Create(
        TypeBuilder<void(const char*, int, PyObject*)>::cache(module),
        llvm::GlobalValue::ExternalLinkage,
        py_negativerefcount_name, module);
}

static Function *
get_py_dealloc(Module *module)
{
    const std::string py_dealloc_name("_Py_Dealloc");
    llvm::Function *result = module->getFunction(py_dealloc_name);
    if (result != NULL)
        return result;

    // The Module keeps ownership of the new Function, and will return
    // it the next time we call getFunction().
    return Function::Create(TypeBuilder<void(PyObject*)>::cache(module),
                            llvm::GlobalValue::ExternalLinkage,
                            py_dealloc_name, module);
}

LlvmFunctionBuilder::LlvmFunctionBuilder(
    Module *module, const std::string& name)
    : module_(module),
      function_(llvm::Function::Create(
                    get_function_type(module),
                    llvm::GlobalValue::PrivateLinkage,
                    name,
                    module))
{
    Function::arg_iterator args = function()->arg_begin();
    this->frame_ = args++;
    assert(args == function()->arg_end() &&
           "Unexpected number of arguments");
    this->frame_->setName("frame");

    builder().SetInsertPoint(BasicBlock::Create("entry", function()));

    this->stack_pointer_addr_ = builder().CreateAlloca(
        TypeBuilder<PyObject**>::cache(module),
        0, "stack_pointer_addr");
    Value *initial_stack_pointer =
        builder().CreateLoad(
            builder().CreateStructGEP(this->frame_, FrameTy::FIELD_STACKTOP),
            "initial_stack_pointer");
    builder().CreateStore(initial_stack_pointer, this->stack_pointer_addr_);

    Value *code = builder().CreateLoad(
        builder().CreateStructGEP(this->frame_, FrameTy::FIELD_CODE), "co");
    this->varnames_ = builder().CreateLoad(
        builder().CreateStructGEP(code, CodeTy::FIELD_VARNAMES),
        "varnames");
    this->names_ = builder().CreateBitCast(
        builder().CreateLoad(
            builder().CreateStructGEP(code, CodeTy::FIELD_NAMES)),
        TypeBuilder<PyTupleObject*>::cache(module),
        "names");
    this->consts_ = builder().CreateBitCast(
        builder().CreateLoad(
            builder().CreateStructGEP(code, CodeTy::FIELD_CONSTS)),
        TypeBuilder<PyTupleObject*>::cache(module),
        "consts");

    Value* fastlocals_indices[] = {
        Constant::getNullValue(Type::Int32Ty),
        ConstantInt::get(Type::Int32Ty, FrameTy::FIELD_LOCALSPLUS),
        // Get the address of frame->localsplus[0]
        Constant::getNullValue(Type::Int32Ty),
    };
    this->fastlocals_ =
        builder().CreateGEP(this->frame_,
                            fastlocals_indices, end(fastlocals_indices),
                            "fastlocals");
    Value *nlocals = builder().CreateLoad(
        builder().CreateStructGEP(code, CodeTy::FIELD_NLOCALS), "nlocals");

    this->freevars_ =
        builder().CreateGEP(this->fastlocals_, nlocals, "freevars");
}

void
LlvmFunctionBuilder::FallThroughTo(BasicBlock *next_block)
{
    if (builder().GetInsertBlock()->getTerminator() == NULL) {
        // If the block doesn't already end with a branch or
        // return, branch to the next block.
        builder().CreateBr(next_block);
    }
    builder().SetInsertPoint(next_block);
}

void
LlvmFunctionBuilder::LOAD_CONST(int index)
{
    Value *indices[] = {
        ConstantInt::get(Type::Int32Ty, 0),
        ConstantInt::get(Type::Int32Ty, TupleTy::FIELD_ITEM),
        ConstantInt::get(Type::Int32Ty, index),
    };
    Value *const_ = builder().CreateLoad(
        builder().CreateGEP(this->consts_, indices, end(indices)));
    IncRef(const_);
    Push(const_);
}

void
LlvmFunctionBuilder::LOAD_FAST(int index)
{
    BasicBlock *unbound_local =
        BasicBlock::Create("LOAD_FAST_unbound", function());
    BasicBlock *success =
        BasicBlock::Create("LOAD_FAST_success", function());

    Value *local = builder().CreateLoad(
        builder().CreateGEP(this->fastlocals_,
                            ConstantInt::get(Type::Int32Ty, index)));
    Value *is_null = builder().CreateICmpEQ(
        local, Constant::getNullValue(local->getType()));
    builder().CreateCondBr(is_null, unbound_local, success);

    builder().SetInsertPoint(unbound_local);
    Function *tuple_getitem =
        get_global_function<PyObject*(PyObject*, Py_ssize_t)>(
            this->module_, "PyTuple_GetItem");
    Value *varname = builder().CreateCall2(
        tuple_getitem, this->varnames_,
        ConstantInt::get(IntegerType::get(sizeof(Py_ssize_t) * 8),
                         index, true /* signed */));
    FormatExcCheckArg("PyExc_UnboundLocalError", UNBOUNDLOCAL_ERROR_MSG, varname);
    builder().CreateRet(Constant::getNullValue(function()->getReturnType()));

    builder().SetInsertPoint(success);
    IncRef(local);
    Push(local);
}

void
LlvmFunctionBuilder::RETURN_VALUE()
{
    Value *retval = Pop();
    builder().CreateRet(retval);
}


// Adds delta to *addr, and returns the new value.
static Value *
increment_and_get(llvm::IRBuilder<>& builder, Value *addr, int64_t delta)
{
    Value *orig = builder.CreateLoad(addr);
    Value *new_ = builder.CreateAdd(
        orig,
        get_signed_constant_int(orig->getType(), delta));
    builder.CreateStore(new_, addr);
    return new_;
}

void
LlvmFunctionBuilder::IncRef(Value *value)
{
#ifdef Py_REF_DEBUG
    // Increment the global reference count.
    Value *reftotal_addr = get_py_reftotal(this->module_);
    increment_and_get(builder(), reftotal_addr, 1);
#endif

    Value *as_pyobject = builder().CreateBitCast(
        value, TypeBuilder<PyObject*>::cache(this->module_));
    Value *refcnt_addr =
        builder().CreateStructGEP(as_pyobject, ObjectTy::FIELD_REFCNT);
    increment_and_get(builder(), refcnt_addr, 1);
}

void
LlvmFunctionBuilder::DecRef(Value *value)
{
#ifdef Py_REF_DEBUG
    // Decrement the global reference count.
    Value *reftotal_addr = get_py_reftotal(this->module_);
    increment_and_get(builder(), reftotal_addr, -1);
#endif

    Value *as_pyobject = builder().CreateBitCast(
        value, TypeBuilder<PyObject*>::cache(this->module_));
    Value *refcnt_addr =
        builder().CreateStructGEP(as_pyobject, ObjectTy::FIELD_REFCNT);
    Value *new_refcnt = increment_and_get(builder(), refcnt_addr, -1);

    // Check if we need to deallocate the object.
    BasicBlock *block_dealloc = BasicBlock::Create("dealloc", this->function_);
    BasicBlock *block_tail = BasicBlock::Create("decref_tail", this->function_);
    BasicBlock *block_ref_ne_zero = block_tail;
#ifdef Py_REF_DEBUG
    block_ref_ne_zero = BasicBlock::Create("check_refcnt", this->function_);
#endif

    Value *ne_zero = builder().CreateICmpNE(
        new_refcnt, llvm::Constant::getNullValue(new_refcnt->getType()));
    builder().CreateCondBr(ne_zero, block_ref_ne_zero, block_dealloc);

#ifdef Py_REF_DEBUG
    builder().SetInsertPoint(block_ref_ne_zero);
    Value *less_zero = builder().CreateICmpSLT(
        new_refcnt, llvm::Constant::getNullValue(new_refcnt->getType()));
    BasicBlock *block_ref_lt_zero = BasicBlock::Create("negative_refcount",
                                                 this->function_);
    builder().CreateCondBr(less_zero, block_ref_lt_zero, block_tail);

    builder().SetInsertPoint(block_ref_lt_zero);
    Value *neg_refcount = get_py_negativerefcount(this->module_);
    // TODO: Well that __FILE__ and __LINE__ are going to be useless!
    builder().CreateCall3(
        neg_refcount,
        llvm::ConstantArray::get(__FILE__),
        ConstantInt::get(IntegerType::get(sizeof(int) * 8), __LINE__),
        as_pyobject);
    builder().CreateBr(block_tail);
#endif

    builder().SetInsertPoint(block_dealloc);
    Value *dealloc = get_py_dealloc(this->module_);
    builder().CreateCall(dealloc, as_pyobject);
    builder().CreateBr(block_tail);

    builder().SetInsertPoint(block_tail);
}

void
LlvmFunctionBuilder::Push(Value *value)
{
    Value *stack_pointer = builder().CreateLoad(this->stack_pointer_addr_);
    builder().CreateStore(value, stack_pointer);
    Value *new_stack_pointer = builder().CreateGEP(
        stack_pointer, ConstantInt::get(Type::Int32Ty, 1));
    builder().CreateStore(new_stack_pointer, this->stack_pointer_addr_);
}

Value *
LlvmFunctionBuilder::Pop()
{
    Value *stack_pointer = builder().CreateLoad(this->stack_pointer_addr_);
    Value *new_stack_pointer = builder().CreateGEP(
        stack_pointer, get_signed_constant_int(Type::Int32Ty, -1));
    Value *former_top = builder().CreateLoad(new_stack_pointer);
    builder().CreateStore(new_stack_pointer, this->stack_pointer_addr_);
    return former_top;
}

void
LlvmFunctionBuilder::InsertAbort()
{
    builder().CreateCall(llvm::Intrinsic::getDeclaration(
                             this->module_, llvm::Intrinsic::trap));
}

void
LlvmFunctionBuilder::FormatExcCheckArg(const std::string &exc_name,
                                       const std::string &format_str,
                                       Value *obj)
{
    BasicBlock *skip_exc = BasicBlock::Create("end_format_exc", function());
    BasicBlock *to_string = BasicBlock::Create("to_string", function());
    BasicBlock *format_block = BasicBlock::Create("format", function());

    Value *obj_null = builder().CreateICmpEQ(
        obj, Constant::getNullValue(obj->getType()));
    builder().CreateCondBr(obj_null, skip_exc, to_string);

    builder().SetInsertPoint(to_string);
    Function *as_string = get_global_function<PyObject*(PyObject*)>(
        this->module_, "PyString_AsString");
    Value *obj_str = builder().CreateCall(as_string, obj);
    Value *obj_str_null = builder().CreateICmpEQ(
        obj_str, Constant::getNullValue(obj_str->getType()));
    builder().CreateCondBr(obj_str_null, skip_exc, format_block);

    builder().SetInsertPoint(format_block);
    Function *err_format =
        get_global_function<PyObject*(PyObject*, const char*, ...)>(
            this->module_, "PyErr_Format");
    llvm::GlobalVariable *exc = this->module_->getGlobalVariable(exc_name);
    if (exc == NULL) {
        exc = llvm::cast<llvm::GlobalVariable>(
            this->module_->getOrInsertGlobal(
                exc_name,
                TypeBuilder<PyObject*>::cache(this->module_)));
        exc->setConstant(true);
        exc->setLinkage(llvm::GlobalValue::ExternalLinkage);
    }
    llvm::GlobalVariable *format_str_var = new llvm::GlobalVariable(
        llvm::ArrayType::get(Type::Int8Ty, format_str.size() + 1),
        true,  // Constant.
        llvm::GlobalValue::InternalLinkage,
        llvm::ConstantArray::get(format_str),
        format_str, this->module_);
    builder().CreateCall3(err_format,
                          builder().CreateLoad(exc, exc_name.c_str()),
                          builder().CreateStructGEP(format_str_var, 0),
                          obj_str);
    builder().CreateBr(skip_exc);

    builder().SetInsertPoint(skip_exc);
}

}  // namespace py
