#include "node.h"
#include "node_buffer.h"

#include "async-wrap.h"
#include "async-wrap-inl.h"
#include "env.h"
#include "env-inl.h"
#include "util.h"
#include "util-inl.h"
#include "tracing/agent.h"

#include "v8.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <unordered_set>
#include <functional>

namespace node {

using v8::Array;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::MaybeLocal;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

extern tracing::Agent* tracing_agent;


// The tracing APIs require category groups to be pointers to long-lived strings.
// Those strings are stored here.
static std::unordered_set<std::string> categoryGroups;

// Gets a pointer to the category-enabled flags for a tracing category group, if tracing is enabled for it.
static const uint8_t* GetCategoryGroupEnabled(const char* categoryGroup) {
    if (categoryGroup == nullptr) return nullptr;

    static TRACE_EVENT_API_ATOMIC_WORD categoryGroupAtomic;
    const uint8_t* categoryGroupEnabled;
    INTERNAL_TRACE_EVENT_GET_CATEGORY_INFO_CUSTOM_VARIABLES(categoryGroup, categoryGroupAtomic, categoryGroupEnabled);
    
    if (!(*categoryGroupEnabled &
        (kEnabledForRecording_CategoryGroupEnabledFlags | kEnabledForEventCallback_CategoryGroupEnabledFlags))) {
        return nullptr;
    }
    return categoryGroupEnabled;
}

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
  else if (categoryValue->IsArray()) {
    Local<Array> categoryArray = Local<Array>::Cast(categoryValue);
    uint32_t categoryCount = categoryArray->Length();

    std::ostringstream os;
    for (uint32_t i = 0; i < categoryCount; i++) {
      if (i > 0) {
        os << ',';
      }

      if (categoryArray->Get(i)->IsString()) {
        Utf8Value category(env->isolate(), categoryArray->Get(i));
        os << category.out();
      }
      else {
          env->ThrowTypeError("Trace event category array must contain strings.");
          return nullptr;
      }
    }

    auto insertion = categoryGroups.insert(os.str());
    return insertion.first->c_str();
  }
  else {
      env->ThrowTypeError("Trace event category must be a string or string array.");
      return nullptr;
  }
}

// Gets a list of categories from a string or string array value.
static bool GetCategoryList(
    Environment* env,
    const Local<Value>& categoryValue,
    std::vector<std::string>& categoryList) {
  if (categoryValue->IsString()) {
    Utf8Value category(env->isolate(), categoryValue);
    categoryList.reserve(1);
    categoryList.emplace_back(category.out());
  }
  else if (categoryValue->IsArray()) {
    Local<Array> categoryArray = Local<Array>::Cast(categoryValue);
    uint32_t categoryCount = categoryArray->Length();
    categoryList.reserve(categoryCount);

    for (uint32_t i = 0; i < categoryCount; i++) {
      if (categoryArray->Get(i)->IsString()) {
        Utf8Value category(env->isolate(), categoryArray->Get(i));
        categoryList.emplace_back(category.out());
      }
      else {
        env->ThrowTypeError("Trace event category array must contain strings.");
        return false;
      }
    }
  }
  else {
    env->ThrowTypeError("Trace event category must be a string or string array.");
    return false;
  }

  return true;
}


static inline bool ValidateEventName(Environment* env, const Local<Value>& arg) {
  if (!arg->IsString()) {
    env->ThrowTypeError("Trace event name must be a string.");
    return false;
  }
  return true;
}


static inline bool ValidateEventId(Environment* env, const Local<Value>& arg) {
  if (!arg->IsString() && !arg->IsUndefined()) {
    env->ThrowTypeError("Trace event id must be a string or undefined.");
    return false;
  }
  return true;
}


static int64_t GetTimestamp(Environment* env, Local<Value>& arg) {
  if (arg->IsDate()) {
    // TODO: Get timestamp value from Date arg.
    // Timestamps are ignored for now because the _WITH_TIMESTAMP tracing macro variants are
    // currently unimplemented in the v8 tracing code.
    return 0;
  }
  else if (!arg->IsUndefined()) {
    env->ThrowTypeError("Trace event timestamp must be a Date or undefined.");
    return -1;
  }
}

// Gets one or two name-value pairs from an args object.
static void TraceWithArgs(
    Environment* env,
    Local<Object>& args,
    std::function<void(int num_args, const char** arg_names, const uint8_t* arg_types, const uint64_t* arg_values)> traceFunc) {
  Local<Context> context = env->isolate()->GetCurrentContext();
  Local<Array> argNames = args->GetPropertyNames(context).ToLocalChecked();

  if (argNames->Length() == 0) {
      env->ThrowTypeError("Trace event args object must contain properties.");
      return;
  }

  Local<String> arg1Name = Local<String>::Cast(argNames->Get(context, 0).ToLocalChecked());
  Local<Value> arg1Value = args->Get(context, arg1Name).ToLocalChecked();

  int num_args = 1;
  const char* arg_names[2];
  uint8_t arg_types[2];
  uint64_t arg_values[2];

  //arg_names[0] = 

  if (argNames->Length() >= 2) {
      Local<String> arg2Name = Local<String>::Cast(argNames->Get(context, 1).ToLocalChecked());
      Local<Value> arg2Value = args->Get(context, arg2Name).ToLocalChecked();
  
      //arg_names[1] = 

  }

  traceFunc(num_args, arg_names, arg_types, arg_values);
}

static void EmitInstantEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // Args: [name, id, category, args, timestamp]
  // (The id arg is currently ignored, but included here for consistency with other event types.)
  CHECK_EQ(args.Length(), 4);

  const char* categoryGroup = GetCategoryGroup(env, args[2]);
  const uint8_t* categoryGroupEnabled = GetCategoryGroupEnabled(categoryGroup);
  if (categoryGroupEnabled == nullptr) return;

  if (!ValidateEventName(env, args[0])) return;
  Utf8Value nameValue(env->isolate(), args[0]);
  const char* name = nameValue.out();

  int64_t timestamp = GetTimestamp(env, args[4]);
  if (timestamp < 0) return;

  if (args[3]->IsUndefined()) {
    TRACE_EVENT_COPY_INSTANT0(categoryGroup, name, TRACE_EVENT_SCOPE_PROCESS);
  }
  else if (args[3]->IsObject()) {
    Local<Object> obj = Local<Object>::Cast(args[1]);
    TraceWithArgs(env, Local<Object>::Cast(args[1]), [name, categoryGroupEnabled](
        int num_args, const char** arg_names, const uint8_t* arg_types, const uint64_t* arg_values) {
      TRACE_EVENT_API_ADD_TRACE_EVENT(
        TRACE_EVENT_PHASE_INSTANT,
        categoryGroupEnabled,
        name,
        nullptr,
        0,
        0,
        num_args,
        arg_names,
        arg_types,
        arg_values,
        0);
    });
  }
  else {
    env->ThrowTypeError("Trace event args must be an object or undefined.");
  }
}

static void EmitBeginEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // Args: [name, id, category, args, timestamp]
  CHECK_EQ(args.Length(), 4);

  const char* categoryGroup = GetCategoryGroup(env, args[2]);
  const uint8_t* categoryGroupEnabled = GetCategoryGroupEnabled(categoryGroup);
  if (categoryGroupEnabled == nullptr) return;

  if (!ValidateEventName(env, args[0])) return;
  Utf8Value nameValue(env->isolate(), args[0]);
  const char* name = nameValue.out();

  if (!ValidateEventId(env, args[1])) return;
  Utf8Value idValue(env->isolate(), args[1]);
  const char* id = idValue.out();
  if (id == nullptr) {
      id = name;
  }

  int64_t timestamp = GetTimestamp(env, args[4]);
  if (timestamp < 0) return;

  if (args[3]->IsUndefined()) {
      TRACE_EVENT_COPY_ASYNC_BEGIN0(categoryGroup, name, id);
  }
  else if (args[3]->IsObject()) {
      // TODO: TRACE_EVENT_COPY_ASYNC_BEGIN1 or TRACE_EVENT_COPY_ASYNC_BEGIN2 with args
      TRACE_EVENT_COPY_ASYNC_BEGIN0(categoryGroup, name, id);
  }
  else {
      env->ThrowTypeError("Trace event args must be an object or undefined.");
  }
}

static void EmitEndEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // Args: [name, id, category, args, timestamp]
  CHECK_EQ(args.Length(), 4);

  const char* categoryGroup = GetCategoryGroup(env, args[2]);
  const uint8_t* categoryGroupEnabled = GetCategoryGroupEnabled(categoryGroup);
  if (categoryGroupEnabled == nullptr) return;

  if (!ValidateEventName(env, args[0])) return;
  Utf8Value nameValue(env->isolate(), args[0]);
  const char* name = nameValue.out();

  if (!ValidateEventId(env, args[1])) return;
  Utf8Value idValue(env->isolate(), args[1]);
  const char* id = idValue.out();
  if (id == nullptr) {
      id = name;
  }

  int64_t timestamp = GetTimestamp(env, args[4]);
  if (timestamp < 0) return;

  if (args[3]->IsUndefined()) {
      TRACE_EVENT_COPY_ASYNC_END0(categoryGroup, name, id);
  }
  else if (args[3]->IsObject()) {
      // TODO: TRACE_EVENT_COPY_ASYNC_END1 or TRACE_EVENT_COPY_ASYNC_END2 with args
      TRACE_EVENT_COPY_ASYNC_END0(categoryGroup, name, id);
  }
  else {
      env->ThrowTypeError("Trace event args must be an object or undefined.");
  }
}

static void EmitCountEvent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  // Args: [name, id, category, args, timestamp]
  // (The id arg is currently ignored, but included here for consistency with other event types.)
  CHECK_EQ(args.Length(), 4);

  const char* categoryGroup = GetCategoryGroup(env, args[2]);
  const uint8_t* categoryGroupEnabled = GetCategoryGroupEnabled(categoryGroup);
  if (categoryGroupEnabled == nullptr) return;

  if (!ValidateEventName(env, args[0])) return;
  Utf8Value nameValue(env->isolate(), args[0]);
  const char* name = nameValue.out();

  int64_t timestamp = GetTimestamp(env, args[4]);
  if (timestamp < 0) return;

  if (args[3]->IsNumber()) {
      int32_t value = args[3]->Int32Value();
      TRACE_COPY_COUNTER1(categoryGroup, name, value);
  }
  else if (args[3]->IsObject()) {
      // TODO: Get args
  }
  else {
      env->ThrowTypeError("Trace count value must be a number or object.");
      return;
  }
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

    const std::vector<std::string>& categories = tracing_agent->GetCategories();

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

    std::vector<std::string> categories;
    if (!GetCategoryList(env, args[0], categories)) {
        return;
    }

    if (!args[1]->IsNumber()) {
        env->ThrowTypeError("Trace event category enabled flag must be a number.");
        return;
    }

    Local<Integer> enabledFlags = Local<Integer>::Cast(args[1]);

    // TODO: Use categories and enabled flags to update categories to flags map.
    // TODO: Invoke onchange callback (if any categories or flags changed).

    if (categories.size() > 0) {
        tracing_agent->SetCategories(categories);
        if (!tracing_agent->IsStarted()) {
            tracing_agent->Start();
        }
    }
    else {
        tracing_agent->Stop();
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
