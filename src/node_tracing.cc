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
    CHECK(categoryCount > 0);

    std::ostringstream os;
    CHECK(categoryArray->Get(0)->IsString());
    Utf8Value category0(env->isolate(), categoryArray->Get(0));
    os << category0.out();

    for (uint32_t i = 1; i < categoryCount; i++) {
      os << ',';

      CHECK(categoryArray->Get(i)->IsString());
      Utf8Value category(env->isolate(), categoryArray->Get(i));
      os << category.out();
    }

    auto insertion = categoryGroups.insert(os.str());
    return insertion.first->c_str();
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

  // TODO: Add listener for category.
}

static void RemoveListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Remove listener for category.
}

static void GetEnabledCategories(const FunctionCallbackInfo<Value>& args) {
    // TODO: Get category list
}

static void SetEnabledCategories(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);

    // TODO: Call env->tracing_agent()->SetCategories().
    // TODO: Call env->tracing_agent()->Start() if necessary.
    // TODO: Call env->tracing_agent()->Stop() if necessary.
}

static void Flush(const FunctionCallbackInfo<Value>& args) {
    // TODO: Flush
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
  env->SetMethod(target, "setEnabledCategories", SetEnabledCategories);
  env->SetMethod(target, "flush", Flush);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(tracing, node::InitTracing)
