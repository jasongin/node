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

static void EmitTraceEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  if (!args[0]->IsObject())
    return env->ThrowTypeError("traceEvent must be an object");

  Local<Object> traceEvent = args[0].As<Object>();

  // TODO: Emit trace event.
}

static void AddTraceListener(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // TODO: Add listener for category.
}

static void RemoveTraceListener(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // TODO: Remove listener for category.
}

void InitTrace(Local<Object> target,
              Local<Value> unused,
              Local<Context> context,
              void* priv) {
  Environment* env = Environment::GetCurrent(context);

  env->SetMethod(target, "emitTraceEvent", EmitTraceEvent);
  env->SetMethod(target, "addTraceListener", AddTraceListener);
  env->SetMethod(target, "removeTraceListener", RemoveTraceListener);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(trace, node::InitTrace)
