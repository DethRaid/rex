#ifndef RX_CORE_FILESYSTEM_FILE_H
#define RX_CORE_FILESYSTEM_FILE_H
#include "rx/core/string.h" // string
#include "rx/core/optional.h" // optional
#include "rx/core/concepts/no_copy.h" // no_copy
#include "rx/core/filesystem/stream.h"

namespace rx::filesystem {

struct file
  : stream
{
  constexpr file();

  file(void* _impl, const char* _file_name, const char* _mode);
  file(const char* _file_name, const char* _mode);
  file(const string& _file_name, const char* _mode);
  file(file&& other_);
  ~file();

  // Read |_size| bytes from file into |_data|.
  virtual rx_u64 read(rx_byte* _data, rx_u64 _size);

  // Write |_size| bytes from |_data| into file.
  virtual rx_u64 write(const rx_byte* _data, rx_u64 _size);

  // Seek to |where| in file.
  virtual bool seek(rx_u64 _where);

  // Flush to disk.
  virtual bool flush();

  // Query the size of the file. This works regardless of where in the file
  // the current file pointer is. If the file does not support querying it's
  // size, this returns a nullopt.
  virtual optional<rx_u64> size();

  file& operator=(file&& file_);

  bool read_line(string& line_);
  bool close();

  // Print |_fmt| with |_args| to file using |_allocator| for formatting.
  // NOTE: asserts if the file isn't a text file.
  template<typename... Ts>
  bool print(memory::allocator* _alloc, const char* _fmt, Ts&&... _args);

  // Print |_fmt| with |_args| to file using system allocator for formatting.
  // NOTE: asserts if the file isn't a text file.
  template<typename... Ts>
  bool print(const char* _fmt, Ts&&... _args);

  // Print a string into the file. This is only valid for text files.
  // NOTE: asserts if the file isn't a text file.
  bool print(string&& contents_);

  // Query if the file handle is valid, will be false if the file has been
  // closed with |close| or if the file failed to open.
  bool is_valid() const;
  operator bool() const;

private:
  friend struct process;

  void* m_impl;

  const char* m_file_name;
  const char* m_mode;
};

inline constexpr file::file()
  : m_impl{nullptr}
  , m_file_name{nullptr}
  , m_mode{nullptr}
{
}

inline file::file(const string& _file_name, const char* _mode)
  : file{_file_name.data(), _mode}
{
}

inline file::operator bool() const {
  return is_valid();
}

template<typename... Ts>
inline bool file::print(memory::allocator* _allocator, const char* _format, Ts&&... _arguments) {
  return print(string::format(_allocator, _format, utility::forward<Ts>(_arguments)...));
}

template<typename... Ts>
inline bool file::print(const char* _format, Ts&&... _arguments) {
  return print(&memory::g_system_allocator, _format, utility::forward<Ts>(_arguments)...);
}

optional<vector<rx_byte>> read_binary_file(memory::allocator* _allocator, const char* _file_name);

inline optional<vector<rx_byte>> read_binary_file(memory::allocator* _allocator, const string& _file_name) {
  return read_binary_file(_allocator, _file_name.data());
}

inline optional<vector<rx_byte>> read_binary_file(const string& _file_name) {
  return read_binary_file(&memory::g_system_allocator, _file_name);
}

inline optional<vector<rx_byte>> read_binary_file(const char* _file_name) {
  return read_binary_file(&memory::g_system_allocator, _file_name);
}

} // namespace rx::filesystem

#endif // RX_CORE_FILESYSTEM_FILE_H
