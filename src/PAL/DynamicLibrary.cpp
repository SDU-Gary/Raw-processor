#include "rawproc/PAL/DynamicLibrary.h"

#include <cassert>
#include <string>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace rawproc::pal {

DynamicLibrary::~DynamicLibrary() { close(); }

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept {
    handle_ = other.handle_;
    other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool DynamicLibrary::open(std::string_view path) {
    close();
#if defined(_WIN32)
    handle_ = (void*)::LoadLibraryA(std::string(path).c_str());
#else
    handle_ = ::dlopen(std::string(path).c_str(), RTLD_NOW);
#endif
    return handle_ != nullptr;
}

void* DynamicLibrary::symbol(std::string_view name) const {
    if (!handle_) return nullptr;
#if defined(_WIN32)
    return (void*)::GetProcAddress((HMODULE)handle_, std::string(name).c_str());
#else
    return ::dlsym(handle_, std::string(name).c_str());
#endif
}

void DynamicLibrary::close() {
    if (!handle_) return;
#if defined(_WIN32)
    ::FreeLibrary((HMODULE)handle_);
#else
    ::dlclose(handle_);
#endif
    handle_ = nullptr;
}

} // namespace rawproc::pal
