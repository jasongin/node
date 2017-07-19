// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "node.h"
#include "node_buffer.h"
#include "node_internals.h"
#include "node_stat_watcher.h"

#include "napi_adapters.h"

#include "env.h"
#include "env-inl.h"
#include "req-wrap.h"
#include "req-wrap-inl.h"
#include "string_bytes.h"
#include "util.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#if defined(__MINGW32__) || defined(_MSC_VER)
# include <io.h>
#endif

#include <vector>

namespace node {
namespace {

#ifndef MIN
# define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define THROW_ERROR(msg) \
  Napi::TypeError::New(args.Env(), msg).ThrowAsJavaScriptException(), \
  Napi::Value()
#define THROW_TYPE_ERROR(msg) \
  Napi::TypeError::New(args.Env(), msg).ThrowAsJavaScriptException(), \
  Napi::Value()
#define THROW_RANGE_ERROR(msg) \
  Napi::RangeError::New(args.Env(), msg).ThrowAsJavaScriptException(), \
  Napi::Value()

#define GET_OFFSET(a) \
  ((a).IsNumber() ? (a).As<Napi::Number>().Int64Value() : -1)

class FSReqWrap: public ReqWrap<uv_fs_t> {
 public:
  enum Ownership { COPY, MOVE };

  inline static FSReqWrap* New(Napi::Env env,
                               Napi::Object req,
                               const char* syscall,
                               const char* data = nullptr,
                               enum encoding encoding = UTF8,
                               Ownership ownership = COPY);

  inline void Dispose();

  void ReleaseEarly() {
    if (data_ != inline_data()) {
      delete[] data_;
      data_ = nullptr;
    }
  }

  const char* syscall() const { return syscall_; }
  const char* data() const { return data_; }
  const enum encoding encoding_;

  size_t self_size() const override { return sizeof(*this); }

  Napi::Env napi_env() const { return napi_env_; }

 private:
  FSReqWrap(Napi::Env env,
            Napi::Object req,
            const char* syscall,
            const char* data,
            enum encoding encoding)
      : ReqWrap(
          node_api::NodeEnvironmentFromNapiEnv(env),
          node_api::V8LocalValueFromJsValue(req).As<v8::Object>(),
          AsyncWrap::PROVIDER_FSREQWRAP),
        napi_env_(env),
        encoding_(encoding),
        syscall_(syscall),
        data_(data) {
    Wrap(object(), this);
  }

  ~FSReqWrap() {
    ReleaseEarly();
    ClearWrap(object());
  }

  void* operator new(size_t size) = delete;
  void* operator new(size_t size, char* storage) { return storage; }
  char* inline_data() { return reinterpret_cast<char*>(this + 1); }

  const char* syscall_;
  const char* data_;

  Napi::Env napi_env_;

  DISALLOW_COPY_AND_ASSIGN(FSReqWrap);
};

#define ASSERT_PATH(path)                                                   \
  if (*path == nullptr)                                                     \
    return THROW_TYPE_ERROR( #path " must be a string or Buffer");

FSReqWrap* FSReqWrap::New(Napi::Env env,
                          Napi::Object req,
                          const char* syscall,
                          const char* data,
                          enum encoding encoding,
                          Ownership ownership) {
  const bool copy = (data != nullptr && ownership == COPY);
  const size_t size = copy ? 1 + strlen(data) : 0;
  FSReqWrap* that;
  char* const storage = new char[sizeof(*that) + size];
  that = new(storage) FSReqWrap(env, req, syscall, data, encoding);
  if (copy)
    that->data_ = static_cast<char*>(memcpy(that->inline_data(), data, size));
  return that;
}


void FSReqWrap::Dispose() {
  this->~FSReqWrap();
  delete[] reinterpret_cast<char*>(this);
}


void NewFSReqWrap(const v8::FunctionCallbackInfo<v8::Value>& args) {
  CHECK(args.IsConstructCall());
  ClearWrap(args.This());
}


inline bool IsInt64(double x) {
  return x == static_cast<double>(static_cast<int64_t>(x));
}

void After(uv_fs_t *req) {
  FSReqWrap* req_wrap = static_cast<FSReqWrap*>(req->data);
  CHECK_EQ(req_wrap->req(), req);
  req_wrap->ReleaseEarly();  // Free memory that's no longer used now.

  Napi::Env env = req_wrap->napi_env();

  // TODO: Change to N-API HandleScope
  v8::HandleScope handle_scope(node_api::V8IsolateFromNapiEnv(env));

  // TODO: Is this necessary?
  //Context::Scope context_scope(env->context());

  node_api::NodeEnvironment node_env(env);

  // there is always at least one argument. "error"
  int argc = 1;

  // Allocate space for two args. We may only use one depending on the case.
  // (Feel free to increase this if you need more)
  napi_value argv[2];
  Napi::Value link;
  Napi::Value error;

  if (req->result < 0) {
    // An error happened.
    argv[0] = node_api::UVException(env,
                                    static_cast<int>(req->result),
                                    req_wrap->syscall(),
                                    nullptr,
                                    req->path,
                                    req_wrap->data());
  } else {
    // error value is empty or null for non-error.
    argv[0] = env.Null();

    // All have at least two args now.
    argc = 2;

    switch (req->fs_type) {
      // These all have no data to pass.
      case UV_FS_ACCESS:
      case UV_FS_CLOSE:
      case UV_FS_RENAME:
      case UV_FS_UNLINK:
      case UV_FS_RMDIR:
      case UV_FS_MKDIR:
      case UV_FS_FTRUNCATE:
      case UV_FS_FSYNC:
      case UV_FS_FDATASYNC:
      case UV_FS_LINK:
      case UV_FS_SYMLINK:
      case UV_FS_CHMOD:
      case UV_FS_FCHMOD:
      case UV_FS_CHOWN:
      case UV_FS_FCHOWN:
        // These, however, don't.
        argc = 1;
        break;

      case UV_FS_STAT:
      case UV_FS_LSTAT:
      case UV_FS_FSTAT:
        argc = 1;
        FillStatsArray(node_env.fs_stats_field_array(),
                       static_cast<const uv_stat_t*>(req->ptr));
        break;

      case UV_FS_UTIME:
      case UV_FS_FUTIME:
        argc = 0;
        break;

      case UV_FS_OPEN:
        argv[1] = Napi::Number::New(env, static_cast<int32_t>(req->result));
        break;

      case UV_FS_WRITE:
        argv[1] = Napi::Number::New(env, static_cast<int32_t>(req->result));
        break;

      case UV_FS_MKDTEMP:
      {
        link = node_api::EncodeString(env,
                                   static_cast<const char*>(req->path),
                                   req_wrap->encoding_,
                                   &error);
        if (link.IsEmpty()) {
          // TODO(addaleax): Use `error` itself here.
          argv[0] = node_api::UVException(env,
                                UV_EINVAL,
                                req_wrap->syscall(),
                                "Invalid character encoding for filename",
                                req->path,
                                req_wrap->data());
        } else {
          argv[1] = link;
        }
        break;
      }

      case UV_FS_READLINK:
        link = node_api::EncodeString(env,
                                   static_cast<const char*>(req->ptr),
                                   req_wrap->encoding_,
                                   &error);
        if (link.IsEmpty()) {
          // TODO(addaleax): Use `error` itself here.
          argv[0] = node_api::UVException(env,
                                UV_EINVAL,
                                req_wrap->syscall(),
                                "Invalid character encoding for link",
                                req->path,
                                req_wrap->data());
        } else {
          argv[1] = link;
        }
        break;

      case UV_FS_REALPATH:
        link = node_api::EncodeString(env,
                                   static_cast<const char*>(req->ptr),
                                   req_wrap->encoding_,
                                   &error);
        if (link.IsEmpty()) {
          // TODO(addaleax): Use `error` itself here.
          argv[0] = node_api::UVException(env,
                                UV_EINVAL,
                                req_wrap->syscall(),
                                "Invalid character encoding for link",
                                req->path,
                                req_wrap->data());
        } else {
          argv[1] = link;
        }
        break;

      case UV_FS_READ:
        // Buffer interface
        argv[1] = Napi::Number::New(env, static_cast<int32_t>(req->result));
        break;

      case UV_FS_SCANDIR:
        {
          int r;
          Napi::Array names = Napi::Array::New(env, 0);
          Napi::Function fn = node_env.push_values_to_array_function();
          napi_value name_argv[NODE_PUSH_VAL_TO_ARRAY_MAX];
          size_t name_idx = 0;

          for (int i = 0; ; i++) {
            uv_dirent_t ent;

            r = uv_fs_scandir_next(req, &ent);
            if (r == UV_EOF)
              break;
            if (r != 0) {
              argv[0] = node_api::UVException(env,
                                    r,
                                    nullptr,
                                    req_wrap->syscall(),
                                    static_cast<const char*>(req->path));
              break;
            }

            Napi::Value filename = node_api::EncodeString(env,
                                    ent.name,
                                    req_wrap->encoding_,
                                    &error);
            if (filename.IsEmpty()) {
              // TODO(addaleax): Use `error` itself here.
              argv[0] = node_api::UVException(env,
                                    UV_EINVAL,
                                    req_wrap->syscall(),
                                    "Invalid character encoding for filename",
                                    req->path,
                                    req_wrap->data());
              break;
            }
            name_argv[name_idx++] = filename;

            if (name_idx >= arraysize(name_argv)) {
              fn.Call(names, name_idx, name_argv);
              name_idx = 0;
            }
          }

          if (name_idx > 0) {
            fn.Call(names, name_idx, name_argv);
          }

          argv[1] = names;
        }
        break;

      default:
        CHECK(0 && "Unhandled eio response");
    }
  }

  node_api::MakeAsyncCallback(
    req_wrap,
    node_env.oncomplete_string(),
    argc,
    argv);

  uv_fs_req_cleanup(req_wrap->req());
  req_wrap->Dispose();
}

// This struct is only used on sync fs calls.
// For async calls FSReqWrap is used.
class fs_req_wrap {
 public:
  fs_req_wrap() {}
  ~fs_req_wrap() { uv_fs_req_cleanup(&req); }
  uv_fs_t req;

 private:
  DISALLOW_COPY_AND_ASSIGN(fs_req_wrap);
};


#define ASYNC_DEST_CALL(func, request, dest, encoding, ...)                   \
  FSReqWrap* req_wrap = FSReqWrap::New(args.Env(), request.As<Napi::Object>(),\
                                       #func, dest, encoding);                \
  int err = uv_fs_ ## func(node_env.event_loop(),                             \
                           req_wrap->req(),                                   \
                           __VA_ARGS__,                                       \
                           After);                                            \
  req_wrap->Dispatched();                                                     \
  if (err < 0) {                                                              \
    uv_fs_t* uv_req = req_wrap->req();                                        \
    uv_req->result = err;                                                     \
    uv_req->path = nullptr;                                                   \
    After(uv_req);                                                            \
    req_wrap = nullptr;                                                       \
    return Napi::Value();                                                     \
  } else {                                                                    \
    return Napi::Value(args.Env(),                                            \
                       node_api::JsValueFromV8LocalValue(req_wrap->object()));\
  }

#define ASYNC_CALL(func, req, encoding, ...)                                  \
  ASYNC_DEST_CALL(func, req, nullptr, encoding, __VA_ARGS__)                  \

#define SYNC_DEST_CALL(func, path, dest, ...)                                 \
  fs_req_wrap req_wrap;                                                       \
  node_env.PrintSyncTrace();                                                  \
  int err = uv_fs_ ## func(node_env.event_loop(),                             \
                           &req_wrap.req,                                     \
                           __VA_ARGS__,                                       \
                           nullptr);                                          \
  if (err < 0) {                                                              \
    Napi::Value ex = node_api::UVException(                                   \
      args.Env(), err, #func, nullptr, path, dest);                           \
    ex.As<Napi::Error>().ThrowAsJavaScriptException();                        \
  }

#define SYNC_CALL(func, path, ...)                                            \
  SYNC_DEST_CALL(func, path, nullptr, __VA_ARGS__)                            \

#define SYNC_REQ req_wrap.req

#define SYNC_RESULT Napi::Number::New(args.Env(), err)

Napi::Value Access(const Napi::CallbackInfo& args) {
  if (args.Length() < 2)
    return THROW_TYPE_ERROR("path and mode are required");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("mode must be an integer");

  node_api::NodeEnvironment node_env(args.Env());

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  int mode = static_cast<int>(args[1].As<Napi::Number>().Int32Value());

  if (args[2].IsObject()) {
    ASYNC_CALL(access, args[2], UTF8, *path, mode);
  } else {
    SYNC_CALL(access, *path, *path, mode);
    return Napi::Value();
  }
}


Napi::Value Close(const Napi::CallbackInfo& args) {
  if (args.Length() < 1)
    return THROW_TYPE_ERROR("fd is required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");

  node_api::NodeEnvironment node_env(args.Env());
  int fd = args[0].As<Napi::Number>().Int32Value();

  if (args[1].IsObject()) {
    ASYNC_CALL(close, args[1], UTF8, fd)
  } else {
    SYNC_CALL(close, 0, fd)
    return Napi::Value();
  }
}

}  // anonymous namespace

void FillStatsArray(double* fields, const uv_stat_t* s) {
  fields[0] = static_cast<double>(s->st_dev);
  fields[1] = static_cast<double>(s->st_mode);
  fields[2] = static_cast<double>(s->st_nlink);
  fields[3] = static_cast<double>(s->st_uid);
  fields[4] = static_cast<double>(s->st_gid);
  fields[5] = static_cast<double>(s->st_rdev);
#if defined(__POSIX__)
  fields[6] = static_cast<double>(s->st_blksize);
#else
  fields[6] = static_cast<double>(-1);
#endif
  fields[7] = static_cast<double>(s->st_ino);
  fields[8] = static_cast<double>(s->st_size);
#if defined(__POSIX__)
  fields[9] = static_cast<double>(s->st_blocks);
#else
  fields[9] = static_cast<double>(-1);
#endif
  // Dates.
#define X(idx, name)                          \
  fields[idx] = static_cast<double>((s->st_##name.tv_sec * 1e3) + \
                                    (s->st_##name.tv_nsec / 1e6)); \

  X(10, atim)
  X(11, mtim)
  X(12, ctim)
  X(13, birthtim)
#undef X
}

// Used to speed up module loading.  Returns the contents of the file as
// a string or undefined when the file cannot be opened.  The speedup
// comes from not creating Error objects on failure.
static Napi::Value InternalModuleReadFile(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());
  uv_loop_t* loop = node_env.event_loop();

  CHECK(args[0].IsString());
  std::string path = args[0].As<Napi::String>();

  uv_fs_t open_req;
  const int fd = uv_fs_open(loop, &open_req, path.c_str(), O_RDONLY, 0, nullptr);
  uv_fs_req_cleanup(&open_req);

  if (fd < 0) {
    return Napi::Value();
  }

  const size_t kBlockSize = 32 << 10;
  std::vector<char> chars;
  int64_t offset = 0;
  ssize_t numchars;
  do {
    const size_t start = chars.size();
    chars.resize(start + kBlockSize);

    uv_buf_t buf;
    buf.base = &chars[start];
    buf.len = kBlockSize;

    uv_fs_t read_req;
    numchars = uv_fs_read(loop, &read_req, fd, &buf, 1, offset, nullptr);
    uv_fs_req_cleanup(&read_req);

    CHECK_GE(numchars, 0);
    offset += numchars;
  } while (static_cast<size_t>(numchars) == kBlockSize);

  uv_fs_t close_req;
  CHECK_EQ(0, uv_fs_close(loop, &close_req, fd, nullptr));
  uv_fs_req_cleanup(&close_req);

  size_t start = 0;
  if (offset >= 3 && 0 == memcmp(&chars[0], "\xEF\xBB\xBF", 3)) {
    start = 3;  // Skip UTF-8 BOM.
  }

  return Napi::String::New(args.Env(),
                           &chars[start],
                           offset - start);
}

// Used to speed up module loading.  Returns 0 if the path refers to
// a file, 1 when it's a directory or < 0 on error (usually -ENOENT.)
// The speedup comes from not creating thousands of Stat and Error objects.
static Napi::Value InternalModuleStat(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  CHECK(args[0].IsString());
  std::string path = args[0].As<Napi::String>();

  uv_fs_t req;
  int rc = uv_fs_stat(node_env.event_loop(), &req, path.c_str(), nullptr);
  if (rc == 0) {
    const uv_stat_t* const s = static_cast<const uv_stat_t*>(req.ptr);
    rc = !!(s->st_mode & S_IFDIR);
  }
  uv_fs_req_cleanup(&req);

  return Napi::Number::New(args.Env(), rc);
}

static Napi::Value Stat(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  if (args[1].IsObject()) {
    ASYNC_CALL(stat, args[1], UTF8, *path)
  } else {
    SYNC_CALL(stat, *path, *path)
    FillStatsArray(node_env.fs_stats_field_array(),
                   static_cast<const uv_stat_t*>(SYNC_REQ.ptr));
    return Napi::Value();
  }
}

static Napi::Value LStat(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  if (args[1].IsObject()) {
    ASYNC_CALL(lstat, args[1], UTF8, *path)
  } else {
    SYNC_CALL(lstat, *path, *path)
    FillStatsArray(node_env.fs_stats_field_array(),
                   static_cast<const uv_stat_t*>(SYNC_REQ.ptr));
    return Napi::Value();
  }
}

static Napi::Value FStat(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("fd is required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");

  int fd = args[0].As<Napi::Number>().Int32Value();

  if (args[1].IsObject()) {
    ASYNC_CALL(fstat, args[1], UTF8, fd)
  } else {
    SYNC_CALL(fstat, nullptr, fd)
    FillStatsArray(node_env.fs_stats_field_array(),
                   static_cast<const uv_stat_t*>(SYNC_REQ.ptr));
    return Napi::Value();
  }
}

static Napi::Value Symlink(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("target path required");
  if (len < 2)
    return THROW_TYPE_ERROR("src path required");

  BufferValue target = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(target)
  BufferValue path = node_api::BufferValue(args.Env(), args[1]);
  ASSERT_PATH(path)

  int flags = 0;

  if (args[2].IsString()) {
    std::string mode = args[2].As<Napi::String>();
    if (mode == "dir") {
      flags |= UV_FS_SYMLINK_DIR;
    } else if (mode == "junction") {
      flags |= UV_FS_SYMLINK_JUNCTION;
    } else if (mode == "file") {
      Napi::Error::New(args.Env(), "Unknown symlink type").ThrowAsJavaScriptException();
      return Napi::Value();
    }
  }

  if (args[3].IsObject()) {
    ASYNC_DEST_CALL(symlink, args[3], *path, UTF8, *target, *path, flags)
  } else {
    SYNC_DEST_CALL(symlink, *target, *path, *target, *path, flags)
    return Napi::Value();
  }
}

static Napi::Value Link(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("src path required");
  if (len < 2)
    return THROW_TYPE_ERROR("dest path required");

  BufferValue src = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(src)

  BufferValue dest = node_api::BufferValue(args.Env(), args[1]);
  ASSERT_PATH(dest)

  if (args[2].IsObject()) {
    ASYNC_DEST_CALL(link, args[2], *dest, UTF8, *src, *dest)
  } else {
    SYNC_DEST_CALL(link, *src, *dest, *src, *dest)
    return Napi::Value();
  }
}

static Napi::Value ReadLink(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  const int argc = args.Length();

  if (argc < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  const enum encoding encoding =
    node_api::ParseEncoding(args.Env(), args[1], UTF8);

  Napi::Value callback = args[2];
  if (callback.IsObject()) {
    ASYNC_CALL(readlink, callback, encoding, *path)
  } else {
    SYNC_CALL(readlink, *path, *path)
    const char* link_path = static_cast<const char*>(SYNC_REQ.ptr);

    Napi::Value error;
    Napi::String rc = node_api::EncodeString(args.Env(),
                                             link_path,
                                             encoding,
                                             &error);
    if (rc.IsEmpty()) {
      // TODO(addaleax): Use `error` itself here.
      Napi::Value ex = node_api::UVException(
          args.Env(),
          UV_EINVAL,
          "readlink",
          "Invalid character encoding for link",
          *path);
      ex.As<Napi::Error>().ThrowAsJavaScriptException();
    }
    return rc;
  }
}

static Napi::Value Rename(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("old path required");
  if (len < 2)
    return THROW_TYPE_ERROR("new path required");

  BufferValue old_path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(old_path)
  BufferValue new_path = node_api::BufferValue(args.Env(), args[1]);
  ASSERT_PATH(new_path)

  if (args[2].IsObject()) {
    ASYNC_DEST_CALL(rename, args[2], *new_path, UTF8, *old_path, *new_path)
  } else {
    SYNC_DEST_CALL(rename, *old_path, *new_path, *old_path, *new_path)
    return Napi::Value();
  }
}

static Napi::Value FTruncate(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 2)
    return THROW_TYPE_ERROR("fd and length are required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");

  int fd = args[0].As<Napi::Number>().Int32Value();

  // FIXME(bnoordhuis) It's questionable to reject non-ints here but still
  // allow implicit coercion from null or undefined to zero.  Probably best
  // handled in lib/fs.js.
  Napi::Value len_v = args[1];
  if (!len_v.IsUndefined() &&
      !len_v.IsNull() &&
      !IsInt64(len_v.As<Napi::Number>().DoubleValue())) {
    return THROW_TYPE_ERROR("Not an integer");
  }

  const int64_t len = len_v.As<Napi::Number>().Int64Value();

  if (args[2].IsObject()) {
    ASYNC_CALL(ftruncate, args[2], UTF8, fd, len)
  } else {
    SYNC_CALL(ftruncate, 0, fd, len)
    return Napi::Value();
  }
}

static Napi::Value Fdatasync(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("fd is required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");

  int fd = args[0].As<Napi::Number>().Int32Value();

  if (args[1].IsObject()) {
    ASYNC_CALL(fdatasync, args[1], UTF8, fd)
  } else {
    SYNC_CALL(fdatasync, 0, fd)
    return Napi::Value();
  }
}

static Napi::Value Fsync(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("fd is required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");

  int fd = args[0].As<Napi::Number>().Int32Value();

  if (args[1].IsObject()) {
    ASYNC_CALL(fsync, args[1], UTF8, fd)
  } else {
    SYNC_CALL(fsync, 0, fd)
    return Napi::Value();
  }
}

static Napi::Value Unlink(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  if (args[1].IsObject()) {
    ASYNC_CALL(unlink, args[1], UTF8, *path)
  } else {
    SYNC_CALL(unlink, *path, *path)
    return Napi::Value();
  }
}

static Napi::Value RMDir(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  if (args[1].IsObject()) {
    ASYNC_CALL(rmdir, args[1], UTF8, *path)
  } else {
    SYNC_CALL(rmdir, *path, *path)
    return Napi::Value();
  }
}

static Napi::Value MKDir(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 2)
    return THROW_TYPE_ERROR("path and mode are required");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("mode must be an integer");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  int mode = static_cast<int>(args[1].As<Napi::Number>().Int32Value());

  if (args[2].IsObject()) {
    ASYNC_CALL(mkdir, args[2], UTF8, *path, mode)
  } else {
    SYNC_CALL(mkdir, *path, *path, mode)
    return Napi::Value();
  }
}

static Napi::Value RealPath(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  const int argc = args.Length();

  if (argc < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  const enum encoding encoding =
      node_api::ParseEncoding(args.Env(), args[1], UTF8);

  Napi::Value callback = args[2];
  if (callback.IsObject()) {
    ASYNC_CALL(realpath, callback, encoding, *path);
  } else {
    SYNC_CALL(realpath, *path, *path);
    const char* link_path = static_cast<const char*>(SYNC_REQ.ptr);

    Napi::Value error;
    Napi::String rc = node_api::EncodeString(args.Env(),
                                             link_path,
                                             encoding,
                                             &error);
    if (rc.IsEmpty()) {
      // TODO(addaleax): Use `error` itself here.
      Napi::Value ex = node_api::UVException(
          args.Env(),
          UV_EINVAL,
          "realpath",
          "Invalid character encoding for path",
          *path);
      ex.As<Napi::Error>().ThrowAsJavaScriptException();
    }
    return rc;
  }
}

static Napi::Value ReadDir(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  const int argc = args.Length();

  if (argc < 1)
    return THROW_TYPE_ERROR("path required");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  const enum encoding encoding =
      node_api::ParseEncoding(args.Env(), args[1], UTF8);

  Napi::Value callback = args[2];
  if (callback.IsObject()) {
    ASYNC_CALL(scandir, callback, encoding, *path, 0 /*flags*/)
  } else {
    SYNC_CALL(scandir, *path, *path, 0 /*flags*/)

    CHECK_GE(SYNC_REQ.result, 0);
    int r;
    Napi::Array names = Napi::Array::New(args.Env(), 0);
    Napi::Function fn = node_env.push_values_to_array_function();
    napi_value name_v[NODE_PUSH_VAL_TO_ARRAY_MAX];
    size_t name_idx = 0;

    for (int i = 0; ; i++) {
      uv_dirent_t ent;

      r = uv_fs_scandir_next(&SYNC_REQ, &ent);
      if (r == UV_EOF)
        break;
      if (r != 0) {
        Napi::Value ex = node_api::UVException(args.Env(),
                                               r,
                                               "readdir",
                                               "",
                                               *path);
        ex.As<Napi::Error>().ThrowAsJavaScriptException();
        return Napi::Value();
      }

      Napi::Value error;
      Napi::String filename = node_api::EncodeString(args.Env(),
                                                     ent.name,
                                                     encoding,
                                                     &error);
      if (filename.IsEmpty()) {
        // TODO(addaleax): Use `error` itself here.
        Napi::Value ex = node_api::UVException(
            args.Env(),
            UV_EINVAL,
            "readdir",
            "Invalid character encoding for filename",
            *path);
        ex.As<Napi::Error>().ThrowAsJavaScriptException();
      }

      name_v[name_idx++] = filename;

      if (name_idx >= arraysize(name_v)) {
        fn.Call(names, name_idx, name_v);
        name_idx = 0;
      }
    }

    if (name_idx > 0) {
      fn.Call(names, name_idx, name_v);
    }

    return names;
  }
}

static Napi::Value Open(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("path required");
  if (len < 2)
    return THROW_TYPE_ERROR("flags required");
  if (len < 3)
    return THROW_TYPE_ERROR("mode required");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("flags must be an int");
  if (!args[2].IsNumber())
    return THROW_TYPE_ERROR("mode must be an int");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  int flags = args[1].As<Napi::Number>().Int32Value();
  int mode = static_cast<int>(args[2].As<Napi::Number>().Int32Value());

  if (args[3].IsObject()) {
    ASYNC_CALL(open, args[3], UTF8, *path, flags, mode)
  } else {
    SYNC_CALL(open, *path, *path, flags, mode)
    return SYNC_RESULT;
  }
}

// Wrapper for write(2).
//
// bytesWritten = write(fd, buffer, offset, length, position, callback)
// 0 fd        integer. file descriptor
// 1 buffer    the data to write
// 2 offset    where in the buffer to start from
// 3 length    how much to write
// 4 position  if integer, position to write at in the file.
//             if null, write from the current position
static Napi::Value WriteBuffer(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("First argument must be file descriptor");

  CHECK(args[1].IsBuffer());

  int fd = args[0].As<Napi::Number>().Int32Value();
  Napi::Buffer<char> obj = args[1].As<Napi::Buffer<char>>();
  const char* buf = obj.Data();
  size_t buffer_length = obj.Length();
  size_t off = args[2].As<Napi::Number>().Uint32Value();
  size_t len = args[3].As<Napi::Number>().Uint32Value();
  int64_t pos = GET_OFFSET(args[4]);
  Napi::Value req = args[5];

  if (off > buffer_length)
    return THROW_RANGE_ERROR("offset out of bounds");
  if (len > buffer_length)
    return THROW_RANGE_ERROR("length out of bounds");
  if (off + len < off)
    return THROW_RANGE_ERROR("off + len overflow");
  if (!Buffer::IsWithinBounds(off, len, buffer_length))
    return THROW_RANGE_ERROR("off + len > buffer.length");

  buf += off;

  uv_buf_t uvbuf = uv_buf_init(const_cast<char*>(buf), len);

  if (req.IsObject()) {
    ASYNC_CALL(write, req, UTF8, fd, &uvbuf, 1, pos)
  }

  SYNC_CALL(write, nullptr, fd, &uvbuf, 1, pos)
  return SYNC_RESULT;
}


// Wrapper for writev(2).
//
// bytesWritten = writev(fd, chunks, position, callback)
// 0 fd        integer. file descriptor
// 1 chunks    array of buffers to write
// 2 position  if integer, position to write at in the file.
//             if null, write from the current position
static Napi::Value WriteBuffers(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  CHECK(args[0].IsNumber());
  CHECK(args[1].IsArray());

  int fd = args[0].As<Napi::Number>().Int32Value();
  Napi::Array chunks = args[1].As<Napi::Array>();
  int64_t pos = GET_OFFSET(args[2]);
  Napi::Value req = args[3];

  MaybeStackBuffer<uv_buf_t> iovs(chunks.Length());

  for (uint32_t i = 0; i < iovs.length(); i++) {
    Napi::Value chunk = chunks[i];

    if (!chunk.IsBuffer())
      return THROW_TYPE_ERROR("Array elements all need to be buffers");

    Napi::Buffer<char> chunk_buffer = chunk.As<Napi::Buffer<char>>();
    iovs[i] = uv_buf_init(chunk_buffer.Data(), chunk_buffer.Length());
  }

  if (req.IsObject()) {
    ASYNC_CALL(write, req, UTF8, fd, *iovs, iovs.length(), pos)
  }

  SYNC_CALL(write, nullptr, fd, *iovs, iovs.length(), pos)
  return SYNC_RESULT;
}

#if NAPI_MIGRATION

// Wrapper for write(2).
//
// bytesWritten = write(fd, string, position, enc, callback)
// 0 fd        integer. file descriptor
// 1 string    non-buffer values are converted to strings
// 2 position  if integer, position to write at in the file.
//             if null, write from the current position
// 3 enc       encoding of string
static Napi::Value WriteString(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("First argument must be file descriptor");

  Napi::Value req;
  Napi::Value string = args[1];
  int fd = args[0].As<Napi::Number>().Int32Value();
  char* buf = nullptr;
  int64_t pos;
  size_t len;
  FSReqWrap::Ownership ownership = FSReqWrap::COPY;

  // will assign buf and len if string was external
  if (!StringBytes::GetExternalParts(string,
                                     const_cast<const char**>(&buf),
                                     &len)) {
    enum encoding enc = node_api::ParseEncoding(args.Env(), args[3], UTF8);
    len = StringBytes::StorageSize(env->isolate(), string, enc);
    buf = new char[len];
    // StorageSize may return too large a char, so correct the actual length
    // by the write size
    len = StringBytes::Write(env->isolate(), buf, len, args[1], enc);
    ownership = FSReqWrap::MOVE;
  }
  pos = GET_OFFSET(args[2]);
  req = args[4];

  uv_buf_t uvbuf = uv_buf_init(const_cast<char*>(buf), len);

  if (!req.IsObject()) {
    // SYNC_CALL returns on error.  Make sure to always free the memory.
    struct Delete {
      inline explicit Delete(char* pointer) : pointer_(pointer) {}
      inline ~Delete() { delete[] pointer_; }
      char* const pointer_;
    };
    Delete delete_on_return(ownership == FSReqWrap::MOVE ? buf : nullptr);
    SYNC_CALL(write, nullptr, fd, &uvbuf, 1, pos)
    return SYNC_RESULT;
  }

  FSReqWrap* req_wrap =
      FSReqWrap::New(env, req.As<Object>(), "write", buf, UTF8, ownership);
  int err = uv_fs_write(env->event_loop(),
                        req_wrap->req(),
                        fd,
                        &uvbuf,
                        1,
                        pos,
                        After);
  req_wrap->Dispatched();
  if (err < 0) {
    uv_fs_t* uv_req = req_wrap->req();
    uv_req->result = err;
    uv_req->path = nullptr;
    After(uv_req);
    return;
  }

  return args.GetReturnValue().Set(req_wrap->persistent());
}

#endif

/*
 * Wrapper for read(2).
 *
 * bytesRead = fs.read(fd, buffer, offset, length, position)
 *
 * 0 fd        integer. file descriptor
 * 1 buffer    instance of Buffer
 * 2 offset    integer. offset to start reading into inside buffer
 * 3 length    integer. length to read
 * 4 position  file position - null for current position
 *
 */
static Napi::Value Read(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 2)
    return THROW_TYPE_ERROR("fd and buffer are required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");
  if (!args[1].IsBuffer())
    return THROW_TYPE_ERROR("Second argument needs to be a buffer");

  int fd = args[0].As<Napi::Number>().Int32Value();

  size_t len;
  int64_t pos;

  char * buf = nullptr;

  Napi::Buffer<char> buffer_obj = args[1].As<Napi::Buffer<char>>();
  char *buffer_data = buffer_obj.Data();
  size_t buffer_length = buffer_obj.Length();

  size_t off = args[2].As<Napi::Number>().Int32Value();
  if (off >= buffer_length) {
    return THROW_ERROR("Offset is out of bounds");
  }

  len = args[3].As<Napi::Number>().Int32Value();
  if (!Buffer::IsWithinBounds(off, len, buffer_length))
    return THROW_RANGE_ERROR("Length extends beyond buffer");

  pos = GET_OFFSET(args[4]);

  buf = buffer_data + off;

  uv_buf_t uvbuf = uv_buf_init(const_cast<char*>(buf), len);

  Napi::Value req = args[5];

  if (req.IsObject()) {
    ASYNC_CALL(read, req, UTF8, fd, &uvbuf, 1, pos);
  } else {
    SYNC_CALL(read, 0, fd, &uvbuf, 1, pos)
    return SYNC_RESULT;
  }
}

/* fs.chmod(path, mode);
 * Wrapper for chmod(1) / EIO_CHMOD
 */
static Napi::Value Chmod(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 2)
    return THROW_TYPE_ERROR("path and mode are required");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("mode must be an integer");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  int mode = static_cast<int>(args[1].As<Napi::Number>().Int32Value());

  if (args[2].IsObject()) {
    ASYNC_CALL(chmod, args[2], UTF8, *path, mode);
  } else {
    SYNC_CALL(chmod, *path, *path, mode);
    return SYNC_RESULT;
  }
}


/* fs.fchmod(fd, mode);
 * Wrapper for fchmod(1) / EIO_FCHMOD
 */
static Napi::Value FChmod(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  if (args.Length() < 2)
    return THROW_TYPE_ERROR("fd and mode are required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be a file descriptor");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("mode must be an integer");

  int fd = args[0].As<Napi::Number>().Int32Value();
  int mode = static_cast<int>(args[1].As<Napi::Number>().Int32Value());

  if (args[2].IsObject()) {
    ASYNC_CALL(fchmod, args[2], UTF8, fd, mode);
  } else {
    SYNC_CALL(fchmod, 0, fd, mode);
    return SYNC_RESULT;
  }
}


/* fs.chown(path, uid, gid);
 * Wrapper for chown(1) / EIO_CHOWN
 */
static Napi::Value Chown(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("path required");
  if (len < 2)
    return THROW_TYPE_ERROR("uid required");
  if (len < 3)
    return THROW_TYPE_ERROR("gid required");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("uid must be an unsigned int");
  if (!args[2].IsNumber())
    return THROW_TYPE_ERROR("gid must be an unsigned int");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  uv_uid_t uid = static_cast<uv_uid_t>(
      args[1].As<Napi::Number>().Uint32Value());
  uv_gid_t gid = static_cast<uv_gid_t>(
      args[2].As<Napi::Number>().Uint32Value());

  if (args[3].IsObject()) {
    ASYNC_CALL(chown, args[3], UTF8, *path, uid, gid);
  } else {
    SYNC_CALL(chown, *path, *path, uid, gid);
    return SYNC_RESULT;
  }
}


/* fs.fchown(fd, uid, gid);
 * Wrapper for fchown(1) / EIO_FCHOWN
 */
static Napi::Value FChown(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("fd required");
  if (len < 2)
    return THROW_TYPE_ERROR("uid required");
  if (len < 3)
    return THROW_TYPE_ERROR("gid required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be an int");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("uid must be an unsigned int");
  if (!args[2].IsNumber())
    return THROW_TYPE_ERROR("gid must be an unsigned int");

  int fd = args[0].As<Napi::Number>().Int32Value();
  uv_uid_t uid = static_cast<uv_uid_t>(
      args[1].As<Napi::Number>().Uint32Value());
  uv_gid_t gid = static_cast<uv_gid_t>(
      args[2].As<Napi::Number>().Uint32Value());

  if (args[3].IsObject()) {
    ASYNC_CALL(fchown, args[3], UTF8, fd, uid, gid);
  } else {
    SYNC_CALL(fchown, 0, fd, uid, gid);
    return SYNC_RESULT;
  }
}


static Napi::Value UTimes(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("path required");
  if (len < 2)
    return THROW_TYPE_ERROR("atime required");
  if (len < 3)
    return THROW_TYPE_ERROR("mtime required");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("atime must be a number");
  if (!args[2].IsNumber())
    return THROW_TYPE_ERROR("mtime must be a number");

  BufferValue path = node_api::BufferValue(args.Env(), args[0]);
  ASSERT_PATH(path)

  const double atime = static_cast<double>(
      args[1].As<Napi::Number>().DoubleValue());
  const double mtime = static_cast<double>(
      args[2].As<Napi::Number>().DoubleValue());

  if (args[3].IsObject()) {
    ASYNC_CALL(utime, args[3], UTF8, *path, atime, mtime);
  } else {
    SYNC_CALL(utime, *path, *path, atime, mtime);
    return SYNC_RESULT;
  }
}

static Napi::Value FUTimes(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  int len = args.Length();
  if (len < 1)
    return THROW_TYPE_ERROR("fd required");
  if (len < 2)
    return THROW_TYPE_ERROR("atime required");
  if (len < 3)
    return THROW_TYPE_ERROR("mtime required");
  if (!args[0].IsNumber())
    return THROW_TYPE_ERROR("fd must be an int");
  if (!args[1].IsNumber())
    return THROW_TYPE_ERROR("atime must be a number");
  if (!args[2].IsNumber())
    return THROW_TYPE_ERROR("mtime must be a number");

  const int fd = args[0].As<Napi::Number>().Int32Value();
  const double atime = static_cast<double>(args[1].As<Napi::Number>().DoubleValue());
  const double mtime = static_cast<double>(args[2].As<Napi::Number>().DoubleValue());

  if (args[3].IsObject()) {
    ASYNC_CALL(futime, args[3], UTF8, fd, atime, mtime);
  } else {
    SYNC_CALL(futime, 0, fd, atime, mtime);
    return SYNC_RESULT;
  }
}

static Napi::Value Mkdtemp(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());

  CHECK_GE(args.Length(), 2);

  BufferValue tmpl = node_api::BufferValue(args.Env(), args[0]);
  if (*tmpl == nullptr)
    return THROW_TYPE_ERROR("template must be a string or Buffer");

  const enum encoding encoding =
      node_api::ParseEncoding(args.Env(), args[1], UTF8);

  if (args[2].IsObject()) {
    ASYNC_CALL(mkdtemp, args[2], encoding, *tmpl);
  } else {
    SYNC_CALL(mkdtemp, *tmpl, *tmpl);
    const char* path = static_cast<const char*>(SYNC_REQ.path);

    Napi::Value error;
    Napi::String rc =
        node_api::EncodeString(args.Env(), path, encoding, &error);
    if (rc.IsEmpty()) {
      // TODO(addaleax): Use `error` itself here.
      Napi::Value ex = node_api::UVException(
          args.Env(),
          UV_EINVAL,
          "mkdtemp",
          "Invalid character encoding for filename",
          *tmpl);
      ex.As<Napi::Error>().ThrowAsJavaScriptException();
    }
    return rc;
  }
}

static Napi::Value GetStatValues(const Napi::CallbackInfo& args) {
  node_api::NodeEnvironment node_env(args.Env());
  double* fields = node_env.fs_stats_field_array();
  if (fields == nullptr) {
    // stat fields contains twice the number of entries because `fs.StatWatcher`
    // needs room to store data for *two* `fs.Stats` instances.
    fields = new double[2 * 14];
    node_env.set_fs_stats_field_array(fields);
  }
  Napi::ArrayBuffer ab = Napi::ArrayBuffer::New(args.Env(),
                                                fields,
                                                sizeof(double) * 2 * 14);
  Napi::Float64Array fields_array =
      Napi::Float64Array::New(args.Env(), 2 * 14, ab, 0);
  return fields_array;
}

void InitFs(Napi::Env env,
            Napi::Object exports,
            Napi::Object module) {

  exports.DefineProperties({

#define MODULE_FN(name, fn) \
    Napi::PropertyDescriptor::Function(name, fn, napi_writable)

    MODULE_FN("access", Access),
    MODULE_FN("close", Close),

    MODULE_FN("open", Open),
    MODULE_FN("read", Read),
    MODULE_FN("fdatasync", Fdatasync),
    MODULE_FN("fsync", Fsync),
    MODULE_FN("rename", Rename),
    MODULE_FN("ftruncate", FTruncate),
    MODULE_FN("rmdir", RMDir),
    MODULE_FN("mkdir", MKDir),
    MODULE_FN("readdir", ReadDir),

    MODULE_FN("internalModuleReadFile", InternalModuleReadFile),
    MODULE_FN("internalModuleStat", InternalModuleStat),
    MODULE_FN("stat", Stat),
    MODULE_FN("lstat", LStat),
    MODULE_FN("fstat", FStat),
    MODULE_FN("link", Link),
    MODULE_FN("symlink", Symlink),

    MODULE_FN("readlink", ReadLink),
    MODULE_FN("unlink", Unlink),
    MODULE_FN("writeBuffer", WriteBuffer),
    MODULE_FN("writeBuffers", WriteBuffers),
#ifdef NAPI_MIGRATION
    MODULE_FN("writeString", WriteString),
#endif  // NAPI_MIGRATION
    MODULE_FN("realpath", RealPath),

    MODULE_FN("chmod", Chmod),
    MODULE_FN("fchmod", FChmod),
    // MODULE_FN("lchmod", LChmod),

    MODULE_FN("chown", Chown),
    MODULE_FN("fchown", FChown),
    // MODULE_FN("lchown", LChown),

    MODULE_FN("utimes", UTimes),
    MODULE_FN("futimes", FUTimes),

    MODULE_FN("mkdtemp", Mkdtemp),

    MODULE_FN("getStatValues", GetStatValues),
  });

#undef MODULE_FN

  // TODO: Convert following V8 code to N-API.
  // The challenge is N-API doesn't expose a way to set the internal field
  // count on a constructor instance template, as is required by AsyncWrap.
  node::Environment* node_env = node_api::NodeEnvironmentFromNapiEnv(env);
  StatWatcher::Initialize(
      node_env,
      node_api::V8LocalValueFromJsValue(exports).As<v8::Object>());

  // Create FunctionTemplate for FSReqWrap
  v8::Isolate* isolate = node_api::V8IsolateFromNapiEnv(env);
  v8::Local<v8::FunctionTemplate> fst =
      v8::FunctionTemplate::New(isolate, NewFSReqWrap);
  fst->InstanceTemplate()->SetInternalFieldCount(1);
  node_env->SetProtoMethod(fst, "getAsyncId", AsyncWrap::GetAsyncId);
  fst->SetClassName(FIXED_ONE_BYTE_STRING(isolate, "FSReqWrap"));
  node_api::V8LocalValueFromJsValue(exports).As<v8::Object>()->Set(
    FIXED_ONE_BYTE_STRING(isolate, "FSReqWrap"), fst->GetFunction());
}

NODE_API_MODULE_BUILTIN(fs, InitFs)

}  // end namespace node
