#pragma once

#include <filesystem>

namespace maiconv {
// Creates a STORE-method zip archive of a folder's contents and removes the
// folder afterwards. The archive is written at <folder>.zip. Returns false
// (leaving the folder in place) on any failure.
bool zip_folder_and_remove(const std::filesystem::path &folder);

} // namespace maiconv
