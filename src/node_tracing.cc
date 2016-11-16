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


static void EmitInstantEvent(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    CHECK_GE(args.Length(), 4);

    // args: name, id, category, args, timestamp?
    CHECK(args[0]->IsString());
    Utf8Value name(env->isolate(), args[0]);
fprintf(stderr, "EmitInstantEvent(%s)\n", name.out());

    if (args[1]->IsString()) {
        Utf8Value id(env->isolate(), args[1]);

    }

    if (args[2]->IsString()) {
        Utf8Value category(env->isolate(), args[2]);

        // TODO: Look up category in static category set.
    }
    else {
        CHECK(args[2]->IsArray());
        // TODO: Get categories array.

        // TODO: Look up joined categories in static category set.
    }
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

static void AddTracingListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Add listener for category.
}

static void RemoveTracingListenerCategory(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_GE(args.Length(), 1);
  CHECK(args[0]->IsString());
  Utf8Value category(env->isolate(), args[0]);

  // TODO: Remove listener for category.
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
  env->SetMethod(target, "addTracingListenerCategory", AddTracingListenerCategory);
  env->SetMethod(target, "removeTracingListenerCategory", RemoveTracingListenerCategory);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(tracing, node::InitTracing)
