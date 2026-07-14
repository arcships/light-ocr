#include "bundle_loader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace light_ocr::node {
namespace {

constexpr std::uint64_t kMaximumFileBytes = 256ull * 1024 * 1024;
constexpr std::uint64_t kMaximumTotalBytes = 512ull * 1024 * 1024;
constexpr std::size_t kMaximumFiles = 64;
constexpr std::size_t kMaximumDepth = 16;
constexpr std::size_t kMaximumRelativePathBytes = 4096;

[[noreturn]] void io_failure(const std::string& message, const std::string& path = {}) {
  throw BundleIoError(path.empty() ? message : message + ": " + path);
}

void account_file(LoadedBundle& bundle, std::uint64_t size, const std::string& path) {
  if (bundle.files.size() >= kMaximumFiles) {
    io_failure("bundle contains more than 64 files");
  }
  if (size > kMaximumFileBytes) io_failure("bundle file exceeds 256 MiB", path);
  if (size > kMaximumTotalBytes - bundle.total_bytes) {
    io_failure("bundle exceeds 512 MiB total size");
  }
  bundle.total_bytes += size;
}

#ifndef _WIN32

class UniqueFd final {
 public:
  explicit UniqueFd(int value = -1) noexcept : value_(value) {}
  ~UniqueFd() {
    if (value_ >= 0) ::close(value_);
  }
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : value_(other.value_) { other.value_ = -1; }
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      if (value_ >= 0) ::close(value_);
      value_ = other.value_;
      other.value_ = -1;
    }
    return *this;
  }
  int get() const noexcept { return value_; }

 private:
  int value_;
};

std::vector<std::uint8_t> read_regular_file(int parent_fd, const std::string& name,
                                            const struct stat& observed,
                                            const std::string& relative_path) {
  UniqueFd file_fd(::openat(parent_fd, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (file_fd.get() < 0) io_failure("cannot securely open bundle file", relative_path);

  struct stat opened {};
  if (::fstat(file_fd.get(), &opened) != 0 || !S_ISREG(opened.st_mode)) {
    io_failure("bundle member is not a regular file", relative_path);
  }
  if (opened.st_dev != observed.st_dev || opened.st_ino != observed.st_ino) {
    io_failure("bundle member changed while opening", relative_path);
  }
  if (opened.st_size < 0) io_failure("bundle file has a negative size", relative_path);
  const auto size = static_cast<std::uint64_t>(opened.st_size);
  if (size > kMaximumFileBytes || size > std::numeric_limits<std::size_t>::max()) {
    io_failure("bundle file exceeds its size limit", relative_path);
  }

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = ::read(file_fd.get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) io_failure("bundle file was truncated while reading", relative_path);
    offset += static_cast<std::size_t>(count);
  }
  std::uint8_t extra = 0;
  ssize_t extra_count = 0;
  do {
    extra_count = ::read(file_fd.get(), &extra, 1);
  } while (extra_count < 0 && errno == EINTR);
  if (extra_count != 0) io_failure("bundle file grew while reading", relative_path);

  struct stat after {};
  if (::fstat(file_fd.get(), &after) != 0 || after.st_dev != opened.st_dev ||
      after.st_ino != opened.st_ino || after.st_size != opened.st_size) {
    io_failure("bundle file changed while reading", relative_path);
  }
  return bytes;
}

void walk_directory(int directory_fd, const std::string& prefix, std::size_t depth,
                    LoadedBundle& bundle) {
  if (depth > kMaximumDepth) io_failure("bundle directory nesting exceeds 16 levels");
  const int duplicate_fd = ::dup(directory_fd);
  if (duplicate_fd < 0) io_failure("cannot duplicate bundle directory handle", prefix);
  DIR* raw_directory = ::fdopendir(duplicate_fd);
  if (raw_directory == nullptr) {
    ::close(duplicate_fd);
    io_failure("cannot enumerate bundle directory", prefix);
  }

  std::vector<std::string> names;
  errno = 0;
  while (const auto* entry = ::readdir(raw_directory)) {
    const std::string name(entry->d_name);
    if (name == "." || name == "..") continue;
    names.push_back(name);
    errno = 0;
  }
  const int read_error = errno;
  ::closedir(raw_directory);
  if (read_error != 0) io_failure("cannot completely enumerate bundle directory", prefix);
  std::sort(names.begin(), names.end());

  for (const auto& name : names) {
    const std::string relative = prefix.empty() ? name : prefix + "/" + name;
    if (relative.size() > kMaximumRelativePathBytes) {
      io_failure("bundle relative path exceeds 4096 bytes");
    }
    struct stat observed {};
    if (::fstatat(directory_fd, name.c_str(), &observed, AT_SYMLINK_NOFOLLOW) != 0) {
      io_failure("cannot inspect bundle member", relative);
    }
    if (S_ISLNK(observed.st_mode)) io_failure("bundle contains a symbolic link", relative);
    if (S_ISDIR(observed.st_mode)) {
      UniqueFd child(::openat(directory_fd, name.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
      if (child.get() < 0) io_failure("cannot securely open bundle directory", relative);
      struct stat opened {};
      if (::fstat(child.get(), &opened) != 0 || !S_ISDIR(opened.st_mode) ||
          opened.st_dev != observed.st_dev || opened.st_ino != observed.st_ino) {
        io_failure("bundle directory changed while opening", relative);
      }
      walk_directory(child.get(), relative, depth + 1, bundle);
      continue;
    }
    if (!S_ISREG(observed.st_mode)) {
      io_failure("bundle contains a non-regular member", relative);
    }
    if (observed.st_size < 0) io_failure("bundle file has a negative size", relative);
    account_file(bundle, static_cast<std::uint64_t>(observed.st_size), relative);
    auto storage = std::make_shared<const std::vector<std::uint8_t>>(
        read_regular_file(directory_fd, name, observed, relative));
    bundle.files.push_back(BundleFile{relative, std::move(storage)});
  }
}

#else

class UniqueHandle final {
 public:
  explicit UniqueHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
  ~UniqueHandle() {
    if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
  }
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  HANDLE get() const noexcept { return value_; }

 private:
  HANDLE value_;
};

std::vector<std::uint8_t> read_regular_file(const std::filesystem::path& path,
                                            const std::string& relative_path) {
  UniqueHandle handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (handle.get() == INVALID_HANDLE_VALUE) {
    io_failure("cannot securely open bundle file", relative_path);
  }
  BY_HANDLE_FILE_INFORMATION before {};
  if (!GetFileInformationByHandle(handle.get(), &before) ||
      (before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    io_failure("bundle member is not a regular non-reparse file", relative_path);
  }
  const std::uint64_t size =
      (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
  if (size > kMaximumFileBytes || size > std::numeric_limits<std::size_t>::max()) {
    io_failure("bundle file exceeds its size limit", relative_path);
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto remaining = bytes.size() - offset;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, 16 * 1024 * 1024));
    DWORD read = 0;
    if (!ReadFile(handle.get(), bytes.data() + offset, chunk, &read, nullptr) || read == 0) {
      io_failure("bundle file was truncated while reading", relative_path);
    }
    offset += read;
  }
  std::uint8_t extra = 0;
  DWORD extra_read = 0;
  if (!ReadFile(handle.get(), &extra, 1, &extra_read, nullptr) || extra_read != 0) {
    io_failure("bundle file grew while reading", relative_path);
  }
  BY_HANDLE_FILE_INFORMATION after {};
  if (!GetFileInformationByHandle(handle.get(), &after) ||
      before.nFileIndexHigh != after.nFileIndexHigh ||
      before.nFileIndexLow != after.nFileIndexLow || before.nFileSizeHigh != after.nFileSizeHigh ||
      before.nFileSizeLow != after.nFileSizeLow) {
    io_failure("bundle file changed while reading", relative_path);
  }
  return bytes;
}

#endif

}  // namespace

LoadedBundle load_bundle_directory_secure(const std::filesystem::path& root) {
  if (!root.is_absolute()) io_failure("bundlePath must be absolute", root.string());
  LoadedBundle bundle;
#ifndef _WIN32
  UniqueFd root_fd(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (root_fd.get() < 0) io_failure("cannot securely open bundle root", root.string());
  walk_directory(root_fd.get(), {}, 0, bundle);
#else
  const DWORD attributes = GetFileAttributesW(root.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    io_failure("bundle root must be a non-reparse directory", root.string());
  }
  std::vector<std::filesystem::path> paths;
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       iterator != end; iterator.increment(error)) {
    if (error) io_failure("cannot enumerate bundle directory", error.message());
    const DWORD member_attributes = GetFileAttributesW(iterator->path().c_str());
    if (member_attributes == INVALID_FILE_ATTRIBUTES ||
        (member_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      io_failure("bundle contains a reparse point", iterator->path().string());
    }
    if ((member_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) paths.push_back(iterator->path());
  }
  if (error) io_failure("cannot completely enumerate bundle directory", error.message());
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    auto relative = path.lexically_relative(root).generic_u8string();
    if (relative.empty() || relative.size() > kMaximumRelativePathBytes) {
      io_failure("bundle relative path is invalid", path.string());
    }
    auto bytes = read_regular_file(path, relative);
    account_file(bundle, static_cast<std::uint64_t>(bytes.size()), relative);
    auto storage = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    bundle.files.push_back(BundleFile{std::move(relative), std::move(storage)});
  }
#endif
  if (bundle.files.empty()) io_failure("bundle directory is empty", root.string());
  std::sort(bundle.files.begin(), bundle.files.end(),
            [](const BundleFile& left, const BundleFile& right) { return left.path < right.path; });
  return bundle;
}

}  // namespace light_ocr::node
