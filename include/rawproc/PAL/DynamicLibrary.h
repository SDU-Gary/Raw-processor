#pragma once
#include <string>
#include <string_view>

namespace rawproc::pal {

class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    bool open(std::string_view path);
    void* symbol(std::string_view name) const;
    void close();

    bool isOpen() const { return handle_ != nullptr; }

private:
    void* handle_ = nullptr;
};

} // namespace rawproc::pal
