#include "node.h"
#include "node_buffer.h"

#include "async-wrap.h"
#include "async-wrap-inl.h"
#include "env.h"
#include "env-inl.h"
#include "util.h"
#include "util-inl.h"

#include "v8.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unordered_set>

namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;


// The tracing APIs require category groups to be pointers to long-lived strings.
// Those strings are stored here.
static std::unordered_set<std::string> categoryGroups;

// Gets a comma-separated category group from a string or string array value.
// All returned lists are stored (uniquely) in the static categoryGroups set.
static const char* GetCategoryGroup(Environment* env, const Local<Value>& categoryValue) {
  if (categoryValue->IsString()) {
    Utf8Value category(env->isolate(), categoryValue);

    // The returned insertion is a pair whose first item is the object that was inserted or
    // that blocked the insertion and second item is a boolean indicating whether it was inserted.
    auto insertion = categoryGroups.insert(category.out());
    return insertion.first->c_str();
  }
  else {
    CHECK(categoryValue->IsArray());
    Local<Array> categoryArray = Local<Array>::Cast(categoryValue);
    uint32_t categoryCount = categoryArray->Length();

    std::ostringstream os;
    if (categoryCount > 0) {
        CHECK(categoryArray->Get(0)->IsString());
        Utf8Value category0(env->isolate(), categoryArray->Get(0));
        os << category0.out();

        for (uint32_t i = 1; i < categoryCount; i++) {
          os << ',';

          CHECK(categoryArray->Get(i)->IsString());
          Utf8Value category(env->isolate(), categoryArray->Get(i));
          os << category.out();
        }
    }

    auto insertion = categoryGroups.insert(os.str());
    return insertion.first->c_str();
  }
}

// Gets a list of categories from a string or string array value.
static std::vector<std::string> GetCategoryList(Environment* env, const Local<Value>& categoryValue) {
    if (categoryValue->IsString()) {
        std::vector<std::string> categoryList(1);
        Utf8Value category(env->isolate(), categoryValue);
        categoryList.emplace_back(category.out());
        return categoryList;
    }
    else {
        CHECK(categoryValue->IsArray());
        Local<Array> categoryArray = Local<Array>::Cast(categoryValue);
        uint32_t categoryCount = categoryArray->Length();

        std::vector<std::string> categoryList;
        categoryList.reserve(categoryCount);

        for (uint32_t i = 0; i < categoryCount; i++) {
            CHECK(categoryArray->Get(i)->IsString());
            Utf8Value category(env->isolate(), categoryArray->Get(i));
            categoryList.emplace_back(category.out());
        }

        return categoryList;
    }
}

static void EmitInstantEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 4);

  // args: name, id, category, args, timestamp?
  CHECK(args[0]->IsString());
  Utf8Value nameValue(env->isolate(), args[0]);
  const char* name = nameValue.out();

  const char* categoryGroup = GetCategoryGroup(env, args[2]);

  if (args[3]->IsArray()) {
    // TODO: Get args
  }

  if (args[4]->IsDate()) {
    // TODO: Timestamps are ignored because the _WITH_TIMESTAMP tracing macro variants are
    // currently unimplemented in Node.
  }

fprintf(stderr, "EmitInstantEvent(%s, [%s])\n", name, categoryGroup);
  TRACE_EVENT_COPY_INSTANT0(categoryGroup, name, TRACE_EVENT_SCOPE_PROCESS);
}

static void EmitBeginEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 4);

  // args: name, id, category, args, timestamp?
  CHECK(args[0]->IsString());
  Utf8Value name(env->isolate(), args[0]);
  fprintf(stderr, "EmitBeginEvent(%s)\n", name.out());

}

static void EmitEndEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 4);

  // args: name, id, category, args, timestamp?
  CHECK(args[0]->IsString());
  Utf8Value name(env->isolate(), args[0]);
fprintf(stderr, "EmitEndEvent(%s)\n", name.out());

}

static void EmitCountEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 4);

  // args: name, id, category, args, timestamp?
  CHECK(args[0]->IsString());
  Utf8Value name(env->isolate(), args[0]);
fprintf(stderr, "EmitCountEvent(%s)\n", name.out());

}

static void AddListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Add listener category.
}

static void RemoveListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Remove listener category.
}

static void GetEnabledCategories(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);

    const std::vector<std::string>& categories = env->tracing_agent()->GetCategories();

    Local<Object> categoryMap = Object::New(env->isolate());
    for (uint32_t i = 0; i < categories.size(); i++) {
        Local<Value> category = String::NewFromUtf8(env->isolate(), categories[i].c_str());

        // TODO: CategoryGroupEnabledFlags
        Local<Value> flags = Integer::New(env->isolate(), 1);
        categoryMap->Set(category, flags);
    }
    
    args.GetReturnValue().Set(categoryMap);
}

static void EnableCategory(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);

    std::vector<std::string> categories = GetCategoryList(env, args[0]);

    CHECK(args[1]->IsNumber());
    Local<Integer> enabledFlags = Local<Integer>::Cast(args[1]);

    // TODO: Use categories and enabled flags to update categories to flags map.
    // TODO: Invoke onchange callback (if any categories or flags changed).

    if (categories.size() > 0) {
        env->tracing_agent()->SetCategories(categories);
        if (!env->tracing_agent()->IsStarted()) {
            env->tracing_agent()->Start();
        }
    }
    else {
        env->tracing_agent()->Stop();
    }
}

void InitTracing(Local<Object> target,
                 Local<Value> unused,
                 Local<Context> context,
                 void* priv) {
  Environment* env = Environment::GetCurrent(context);

  env->SetMethod(target, "emitBeginEvent", EmitBeginEvent);
  env->SetMethod(target, "emitEndEvent", EmitEndEvent);
  env->SetMethod(target, "emitInstantEvent", EmitInstantEvent);
  env->SetMethod(target, "emitCountEvent", EmitCountEvent);
  env->SetMethod(target, "addListenerCategory", AddListenerCategory);
  env->SetMethod(target, "removeListenerCategory", RemoveListenerCategory);
  env->SetMethod(target, "getEnabledCategories", GetEnabledCategories);
  env->SetMethod(target, "enableCategory", EnableCategory);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(tracing, node::InitTracing)
