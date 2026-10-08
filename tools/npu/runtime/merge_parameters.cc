// Build-only IRPA assembler. Never included in a deployment package.
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "iree/base/api.h"
#include "iree/io/file_handle.h"
#include "iree/io/formats/irpa/irpa_builder.h"
#include "iree/io/formats/parser_registry.h"
#include "iree/io/parameter_index.h"

namespace {
void check(iree_status_t status) {
  if (iree_status_is_ok(status)) return;
  char text[2048] = {}; iree_host_size_t length = 0;
  iree_status_format(status,sizeof(text),text,&length);
  iree_status_ignore(status);
  throw std::runtime_error(text);
}
iree_status_t open_output(void* user, iree_io_physical_offset_t offset,
                         iree_io_physical_size_t length, iree_io_file_handle_t** file) {
  return iree_io_file_handle_create(IREE_IO_FILE_MODE_READ|IREE_IO_FILE_MODE_WRITE,
      iree_make_cstring_view(static_cast<const char*>(user)),offset+length,
      iree_allocator_system(),file);
}
}

int main(int argc,char** argv) {
  if (argc < 3) { std::cerr << "Usage: merge-aie-parameters OUTPUT INPUT...\n"; return 2; }
  iree_io_parameter_index_t *merged=nullptr,*built=nullptr;
  std::vector<std::vector<uint8_t>> archives;
  struct Entry { uint64_t size; std::vector<uint8_t> bytes; };
  std::map<std::string,Entry> seen;
  try {
    archives.reserve(argc-2);
    check(iree_io_parameter_index_create(iree_allocator_system(),&merged));
    for (int a=2;a<argc;++a) {
      std::ifstream input(argv[a],std::ios::binary);
      if (!input) throw std::runtime_error("Cannot read parameter archive");
      archives.emplace_back(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
      auto& bytes=archives.back();
      iree_io_file_handle_t* file=nullptr;
      iree_io_parameter_index_t* index=nullptr;
      auto status=iree_io_file_handle_wrap_host_allocation(IREE_IO_FILE_ACCESS_READ,
          iree_make_byte_span(bytes.data(),bytes.size()),
          iree_io_file_handle_release_callback_null(),iree_allocator_system(),&file);
      if (iree_status_is_ok(status)) status=iree_io_parameter_index_create(iree_allocator_system(),&index);
      if (iree_status_is_ok(status)) status=iree_io_parse_file_index(IREE_SV("irpa"),file,index,iree_allocator_system());
      if (iree_status_is_ok(status)) {
        for (iree_host_size_t i=0;i<iree_io_parameter_index_count(index);++i) {
          const iree_io_parameter_index_entry_t* entry=nullptr;
          status=iree_io_parameter_index_get(index,i,&entry);
          if (!iree_status_is_ok(status)) break;
          std::string key(entry->key.data,entry->key.size);
          std::vector<uint8_t> value;
          if (entry->type==IREE_IO_PARAMETER_INDEX_ENTRY_STORAGE_TYPE_FILE) {
            auto offset=entry->storage.file.offset;
            if (offset>bytes.size() || entry->length>bytes.size()-offset) {
              status=iree_make_status(IREE_STATUS_OUT_OF_RANGE,"Invalid parameter range");break;
            }
            value.assign(bytes.begin()+offset,bytes.begin()+offset+entry->length);
          } else {
            value.assign(entry->storage.splat.pattern,
                         entry->storage.splat.pattern+entry->storage.splat.pattern_length);
          }
          const auto found=seen.find(key);
          if (found!=seen.end()) {
            if (found->second.size!=entry->length || found->second.bytes!=value) {
              status=iree_make_status(IREE_STATUS_INVALID_ARGUMENT,"Conflicting parameter key");break;
            }
          } else {
            seen.emplace(key,Entry{entry->length,std::move(value)});
            status=iree_io_parameter_index_add(merged,entry);
            if (!iree_status_is_ok(status)) break;
          }
        }
      }
      iree_io_parameter_index_release(index);
      iree_io_file_handle_release(file);
      check(status);
    }
    check(iree_io_parameter_index_create(iree_allocator_system(),&built));
    iree_io_parameter_archive_file_open_callback_t callback={open_output,argv[1]};
    check(iree_io_build_parameter_archive(merged,built,callback,0,iree_allocator_system()));
    std::cout << "Shared parameters: " << seen.size() << '\n';
    iree_io_parameter_index_release(built);
    iree_io_parameter_index_release(merged);
    return 0;
  } catch (const std::exception& e) {
    iree_io_parameter_index_release(built);
    iree_io_parameter_index_release(merged);
    std::cerr << e.what() << '\n';return 1;
  }
}
