#pragma once

/*
  @brief wtr/watcher/detail/adapter/linux

  The Linux `inotify` adapter.
*/

#include <watcher/platform.hpp>
#if defined(WATER_WATCHER_PLATFORM_LINUX_ANY) \
    || defined(WATER_WATCHER_PLATFORM_ANDROID_ANY)

#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <watcher/adapter/adapter.hpp>
#include <watcher/event.hpp>

namespace wtr {
namespace watcher {
namespace detail {
namespace adapter {
namespace {
/* @brief wtr/watcher/detail/adapter/linux/<a>
   Anonymous namespace for "private" things. */

/* @brief wtr/watcher/detail/adapter/linux/<a>/types
   Types:
     - string
     - dir_opt_type
     - path_map_type */
using std::string;
using dir_opt_type = std::filesystem::directory_options;
using path_map_type = std::unordered_map<int, std::string>;

/* @brief wtr/watcher/detail/adapter/linux/<a>/constants
   Constants:
     - event_max_count
     - in_init_opt
     - in_watch_opt
     - dir_opt */
inline constexpr auto event_max_count = 1;
inline constexpr auto path_map_reserve_count = 256;
inline constexpr auto in_init_opt = IN_NONBLOCK;
/* @todo
   Measure perf of IN_ALL_EVENTS */
/* @todo
   Handle move events properly.
     - Use IN_MOVED_TO
     - Use event::<something> */
inline constexpr auto in_watch_opt
    = IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_Q_OVERFLOW;
inline constexpr dir_opt_type dir_opt
    = std::filesystem::directory_options::skip_permission_denied
      & std::filesystem::directory_options::follow_directory_symlink;

/* @brief wtr/watcher/detail/adapter/linux/<a>/fns
   Functions:

     do_path_map_create
       -> optional < path_map_type >

     do_event_resource_create
       -> optional < tuple < epoll_event, epoll_event*, int > >

     do_watch_fd_create
       -> optional < int >

     do_watch_fd_release
       -> bool

     do_scan
       -> bool
*/

inline auto do_path_map_create(string const& base_path, int const watch_fd,
                               event::callback const& callback) noexcept
    -> path_map_type;
inline auto do_event_resource_create(int const watch_fd,
                                     event::callback const& callback) noexcept
    -> std::optional<std::tuple<epoll_event, epoll_event*, int>>;
inline auto do_watch_fd_create(event::callback const& callback) noexcept
    -> std::optional<int>;
inline auto do_watch_fd_release(int watch_fd,
                                event::callback const& callback) noexcept
    -> bool;
inline auto do_scan(int fd, path_map_type& path_map,
                    event::callback const& callback) noexcept -> bool;

/* @brief wtr/watcher/detail/adapter/linux/<a>/fns/do_path_map_create
   If the path given is a directory
     - find all directories above the base path given.
     - ignore nonexistent directories.
     - return a map of watch descriptors -> directories.
   If `path` is a file
     - return it as the only value in a map.
     - the watch descriptor key should always be 1. */
inline auto do_path_map_create(string const& base_path, int const watch_fd,
                               event::callback const& callback) noexcept
    -> path_map_type
{
  using rdir_iterator = std::filesystem::recursive_directory_iterator;

  auto dir_ec = std::error_code{};
  path_map_type path_map;
  path_map.reserve(path_map_reserve_count);

  auto do_mark = [&](auto& dir) {
    int wd = inotify_add_watch(watch_fd, dir.c_str(), in_watch_opt);
    return wd < 0
        ? [&](){
            callback({"e/sys/inotify_add_watch",
                     event::what::other, event::kind::watcher});
            return false; }()
        : [&](){
            path_map[wd] = dir;
            return true;  }();
  };

  if (!do_mark(base_path))
    return path_map_type{};
  else if (std::filesystem::is_directory(base_path, dir_ec))
    /* @todo @note
       Should we bail from within this loop if `do_mark` fails? */
    for (auto const& dir : rdir_iterator(base_path, dir_opt, dir_ec))
      if (!dir_ec)
        if (std::filesystem::is_directory(dir, dir_ec))
          if (!dir_ec) do_mark(dir.path());
  return path_map;
};

/* @brief wtr/watcher/detail/adapter/linux/<a>/fns/do_event_resource_create
   Return and initializes epoll events and file descriptors,
   which are the resources needed for an epoll_wait loop.
   Or return nothing. */
inline auto do_event_resource_create(int const watch_fd,
                                     event::callback const& callback) noexcept
    -> std::optional<std::tuple<epoll_event, epoll_event*, int>>
{
  struct epoll_event event_conf
  {
    .events = EPOLLIN, .data { .fd = watch_fd }
  };
  struct epoll_event event_list[event_max_count];
  int event_fd
#if defined(WATER_WATCHER_PLATFORM_LINUX_ANY)
      = epoll_create1(EPOLL_CLOEXEC);
#elif defined(WATER_WATCHER_PLATFORM_ANDROID_ANY)
      = epoll_create(0);
#endif

  if (epoll_ctl(event_fd, EPOLL_CTL_ADD, watch_fd, &event_conf) < 0) {
    callback({"e/sys/epoll_create", event::what::other, event::kind::watcher});
    return std::nullopt;
  } else
    return std::make_tuple(event_conf, event_list, event_fd);
};

/* @brief wtr/watcher/detail/adapter/linux/<a>/fns/do_watch_fd_create
   Return an (optional) file descriptor
   which may access the inotify api. */
inline auto do_watch_fd_create(event::callback const& callback) noexcept
    -> std::optional<int>
{
  int watch_fd
#if defined(WATER_WATCHER_PLATFORM_LINUX_ANY)
      = inotify_init1(in_init_opt);
#elif defined(WATER_WATCHER_PLATFORM_ANDROID_ANY)
      = inotify_init();
#endif

  if (watch_fd < 0) {
    callback({"e/sys/inotify_init", event::what::other, event::kind::watcher});
    return std::nullopt;
  } else
    return watch_fd;
};

/* @brief wtr/watcher/detail/adapter/linux/<a>/fns/do_watch_fd_release
   Close the file descriptor `fd_watch`,
   Invoke `callback` on errors. */
inline auto do_watch_fd_release(int watch_fd,
                                event::callback const& callback) noexcept
    -> bool
{
  if (close(watch_fd) < 0) {
    callback({"e/sys/close", event::what::other, event::kind::watcher});
    return false;
  } else
    return true;
}

/* @brief wtr/watcher/detail/adapter/linux/<a>/fns/do_scan
   Reads through available (inotify) filesystem events.
   Discerns their path and type.
   Calls the callback.
   Returns false on eventful errors.

   @todo
   Return new directories when they appear,
   Consider running and returning `find_dirs` from here.
   Remove destroyed watches. */
inline auto do_scan(int watch_fd, path_map_type& path_map,
                    event::callback const& callback) noexcept -> bool
{
  /* 4096 is a typical page size. */
  static constexpr auto buf_len = 4096;
  alignas(struct inotify_event) char buf[buf_len];

  enum class event_recv_status { eventful, eventless, error };

  auto const lift_event_recv = [](int fd, char* buf) {
    /* Read some events. */
    ssize_t len = read(fd, buf, buf_len);

    /* EAGAIN means no events were found.
       We return `eventless` in that case. */
    if (len < 0 && errno != EAGAIN)
      return std::make_pair(event_recv_status::error, len);
    else if (len <= 0)
      return std::make_pair(event_recv_status::eventless, len);
    else
      return std::make_pair(event_recv_status::eventful, len);
  };

  /* Loop while events can be read from the inotify file descriptor. */
  while (true) {
    /* Read events */
    auto [status, len] = lift_event_recv(watch_fd, buf);
    /* Handle the errored, eventless and eventful reads */
    switch (status) {
      case event_recv_status::eventful:
        /* Loop over all events in the buffer. */
        const struct inotify_event* event_recv;
        for (char* ptr = buf; ptr < buf + len;
             ptr += sizeof(struct inotify_event) + event_recv->len)
        {
          event_recv = (const struct inotify_event*)ptr;

          /* @todo
             Consider using std::filesystem here. */
          auto const path_kind = event_recv->mask & IN_ISDIR
                                     ? event::kind::dir
                                     : event::kind::file;
          int path_wd = event_recv->wd;
          auto event_base_path = path_map.find(path_wd)->second;
          auto event_path = string(event_base_path + "/" + event_recv->name);

          if (event_recv->mask & IN_Q_OVERFLOW) {
            callback(
                {"e/self/overflow", event::what::other, event::kind::watcher});
          } else if (event_recv->mask & IN_CREATE) {
            callback({event_path, event::what::create, path_kind});
            if (path_kind == event::kind::dir) {
              int new_watch_fd = inotify_add_watch(watch_fd, event_path.c_str(),
                                                   in_watch_opt);
              path_map[new_watch_fd] = event_path;
            }
          } else if (event_recv->mask & IN_DELETE) {
            callback({event_path, event::what::destroy, path_kind});
            /* @todo rm watch, rm path map entry */
          } else if (event_recv->mask & IN_MOVE) {
            callback({event_path, event::what::rename, path_kind});
          } else if (event_recv->mask & IN_MODIFY) {
            callback({event_path, event::what::modify, path_kind});
          } else {
            callback({event_path, event::what::other, path_kind});
          }
        }
        /* We don't want to return here. We run until `eventless`. */
        break;
      case event_recv_status::error:
        callback({"e/sys/read", event::what::other, event::kind::watcher});
        return false;
        break;
      case event_recv_status::eventless: return true; break;
    }
  }
}

} /* namespace */

/* @brief wtr/watcher/detail/adapter/linux/fns/watch
   Monitors `base_path` for changes.
   Invokes `callback` with an `event` when they happen.

   @param base_path
   The path to watch for filesystem events.

   @param callback
   A callback to perform when the files
   being watched change. */
inline bool watch(auto const& path, event::callback const& callback,
                  auto const& is_living) noexcept
{
  auto const do_error = [&callback](string const& msg) -> bool {
    callback({msg, event::what::other, event::kind::watcher});
    return false;
  };

  /* Gather resources:
       - watch fd -- for inotify
       - path map -- for event to path lookup
       - event list -- for epoll
       - event fd -- for epoll */

  auto watch_fd_optional = do_watch_fd_create(callback);

  if (watch_fd_optional.has_value()) {
    auto watch_fd = watch_fd_optional.value();

    auto&& path_map = do_path_map_create(path, watch_fd, callback);

    auto&& event_tuple = do_event_resource_create(watch_fd, callback);

    if (event_tuple.has_value()) {
      /* event_conf is 0th */
      auto event_list = std::get<1>(event_tuple.value());
      auto event_fd = std::get<2>(event_tuple.value());

      /* Do work until dead:
          - Await filesystem events
          - Invoke `callback` on errors and events */

      while (is_living(path) && !path_map.empty()) {
        int event_count
            = epoll_wait(event_fd, event_list, event_max_count, delay_ms);
        if (event_count < 0)
          return do_watch_fd_release(watch_fd, callback)
                 && do_error("e/sys/epoll_wait");
        else if (event_count > 0)
          for (int n = 0; n < event_count; ++n)
            if (event_list[n].data.fd == watch_fd)
              if (is_living(path))
                if (!do_scan(watch_fd, path_map, callback))
                  return do_watch_fd_release(watch_fd, callback)
                         && do_error("e/self/scan");
      }
      return do_watch_fd_release(watch_fd, callback);

    } else
      return do_watch_fd_release(watch_fd, callback);

  } else
    return false;
}

inline bool watch(char const* path, event::callback const& callback,
                  auto const& is_living) noexcept
{
  return watch(std::string(path), callback, is_living);
}

} /* namespace adapter */
} /* namespace detail */
} /* namespace watcher */
} /* namespace wtr */

#endif /* if defined(WATER_WATCHER_PLATFORM_LINUX_ANY) */
