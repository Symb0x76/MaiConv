#include "maiconv/core/io.hpp"

#include "AssetBundleFileFormat.h"
#include "AssetsFileFormat.h"
#include "AssetsFileReader.h"
#include "AssetsFileTable.h"
#include "AssetTypeClass.h"
#include "lodepng.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <d3d11.h>
#include <d3dcompiler.h>
#endif

namespace maiconv {
    namespace {

        constexpr uint32_t kTexture2DClassId = 28;

        std::filesystem::path make_temp_bundle_path() {
            static std::atomic<unsigned long long> counter{ 0 };
            const auto id = counter.fetch_add(1, std::memory_order_relaxed);
            return std::filesystem::temp_directory_path() / ("maiconv_unpacked_bundle_" + std::to_string(id) + ".ab");
        }

        struct TexturePayload {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t format = 0;
            std::vector<uint8_t> picture_data;
            uint64_t stream_offset = 0;
            uint32_t stream_size = 0;
            std::string stream_path;
        };

        bool is_big_endian(const AssetsFile& assets_file) {
            return assets_file.header.endianness != 0;
        }

        uint32_t resolve_class_id(const AssetsFile& assets_file, const AssetFileInfoEx& asset_info) {
            if (assets_file.header.format < 0x10) {
                return asset_info.curFileTypeOrIndex;
            }
            if (asset_info.curFileTypeOrIndex >= assets_file.typeTree.fieldCount) {
                return 0;
            }
            return static_cast<uint32_t>(assets_file.typeTree.pTypes_Unity5[asset_info.curFileTypeOrIndex].classId);
        }

        Type_0D* resolve_type_tree(AssetsFile& assets_file, const AssetFileInfoEx& asset_info) {
            if (assets_file.header.format < 0x10) {
                for (uint32_t i = 0; i < assets_file.typeTree.fieldCount; ++i) {
                    if (assets_file.typeTree.pTypes_Unity5[i].classId == static_cast<int>(asset_info.curFileTypeOrIndex)) {
                        return &assets_file.typeTree.pTypes_Unity5[i];
                    }
                }
                return nullptr;
            }
            if (asset_info.curFileTypeOrIndex >= assets_file.typeTree.fieldCount) {
                return nullptr;
            }
            return &assets_file.typeTree.pTypes_Unity5[asset_info.curFileTypeOrIndex];
        }

        bool copy_byte_array_field(AssetTypeValueField* field, std::vector<uint8_t>& output) {
            if (field == nullptr || field->IsDummy() || field->GetValue() == nullptr) {
                return false;
            }
            auto* byte_array = field->GetValue()->AsByteArray();
            if (byte_array == nullptr || byte_array->data == nullptr || byte_array->size == 0) {
                output.clear();
                return true;
            }
            output.assign(byte_array->data, byte_array->data + byte_array->size);
            return true;
        }

        bool read_texture_payload(TexturePayload& out, AssetTypeValueField* base_field) {
            if (base_field == nullptr || base_field->IsDummy()) {
                return false;
            }

            auto* width = base_field->Get("m_Width");
            auto* height = base_field->Get("m_Height");
            auto* format = base_field->Get("m_TextureFormat");
            auto* image_data = base_field->Get("image data");
            auto* stream_data = base_field->Get("m_StreamData");
            if (width == nullptr || height == nullptr || format == nullptr || image_data == nullptr || stream_data == nullptr) {
                return false;
            }
            if (width->IsDummy() || height->IsDummy() || format->IsDummy() || image_data->IsDummy() || stream_data->IsDummy()) {
                return false;
            }

            out.width = width->GetValue()->AsUInt();
            out.height = height->GetValue()->AsUInt();
            out.format = format->GetValue()->AsUInt();
            if (!copy_byte_array_field(image_data, out.picture_data)) {
                return false;
            }

            auto* stream_offset = stream_data->Get("offset");
            auto* stream_size = stream_data->Get("size");
            auto* stream_path = stream_data->Get("path");
            if (stream_offset == nullptr || stream_size == nullptr || stream_path == nullptr) {
                return false;
            }
            if (stream_offset->IsDummy() || stream_size->IsDummy() || stream_path->IsDummy()) {
                return false;
            }

            out.stream_offset = stream_offset->GetValue()->AsUInt64();
            out.stream_size = stream_size->GetValue()->AsUInt();
            const char* path_value = stream_path->GetValue()->AsString();
            out.stream_path = path_value != nullptr ? path_value : "";
            return true;
        }

        std::string normalize_entry_name(const std::string& value) {
            std::string normalized = lower(value);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return normalized;
        }

        bool read_bundle_entry_bytes(AssetBundleFile& bundle,
            IAssetsReader* bundle_reader,
            size_t entry_index,
            std::vector<uint8_t>& output) {
            IAssetsReader* entry_reader = nullptr;
            uint64_t entry_size = 0;
            if (bundle.bundleHeader6.fileVersion >= 6) {
                if (bundle.bundleInf6 == nullptr || entry_index >= bundle.bundleInf6->directoryCount) {
                    return false;
                }
                entry_size = bundle.bundleInf6->dirInf[entry_index].decompressedSize;
                entry_reader = bundle.MakeAssetsFileReader(bundle_reader, &bundle.bundleInf6->dirInf[entry_index]);
            }
            else if (bundle.bundleHeader6.fileVersion == 3) {
                if (bundle.assetsLists3 == nullptr || entry_index >= bundle.assetsLists3->count) {
                    return false;
                }
                entry_size = bundle.assetsLists3->ppEntries[entry_index]->length;
                entry_reader = bundle.MakeAssetsFileReader(bundle_reader, bundle.assetsLists3->ppEntries[entry_index]);
            }
            if (entry_reader == nullptr || entry_size == 0 || entry_size > static_cast<uint64_t>(SIZE_MAX)) {
                if (entry_reader != nullptr) {
                    Free_AssetsReader(entry_reader);
                }
                return false;
            }

            output.resize(static_cast<size_t>(entry_size));
            const size_t read_size = entry_reader->Read(0, output.size(), output.data());
            Free_AssetsReader(entry_reader);
            if (read_size != output.size()) {
                output.clear();
                return false;
            }
            return true;
        }

        bool load_stream_payload(AssetBundleFile& bundle,
            IAssetsReader* bundle_reader,
            const std::filesystem::path& bundle_path,
            TexturePayload& payload) {
            if (payload.stream_size == 0) {
                return !payload.picture_data.empty();
            }

            const auto direct_path = bundle_path.parent_path() / path_from_utf8(payload.stream_path);
            if (std::filesystem::exists(direct_path) && std::filesystem::is_regular_file(direct_path)) {
                std::ifstream in(direct_path, std::ios::binary);
                if (!in) {
                    return false;
                }
                in.seekg(static_cast<std::streamoff>(payload.stream_offset), std::ios::beg);
                payload.picture_data.resize(payload.stream_size);
                in.read(reinterpret_cast<char*>(payload.picture_data.data()), static_cast<std::streamsize>(payload.picture_data.size()));
                return static_cast<size_t>(in.gcount()) == payload.picture_data.size();
            }

            const std::string target_full = normalize_entry_name(payload.stream_path);
            const std::string target_name = normalize_entry_name(std::filesystem::path(payload.stream_path).filename().generic_string());
            const size_t entry_count = bundle.bundleHeader6.fileVersion >= 6
                ? static_cast<size_t>(bundle.bundleInf6 != nullptr ? bundle.bundleInf6->directoryCount : 0)
                : static_cast<size_t>(bundle.assetsLists3 != nullptr ? bundle.assetsLists3->count : 0);
            for (size_t i = 0; i < entry_count; ++i) {
                const char* entry_name_raw = bundle.GetEntryName(i);
                if (entry_name_raw == nullptr) {
                    continue;
                }
                const std::string entry_name = normalize_entry_name(entry_name_raw);
                const std::string entry_filename = normalize_entry_name(std::filesystem::path(entry_name_raw).filename().generic_string());
                if (entry_name != target_full && entry_filename != target_name) {
                    continue;
                }

                std::vector<uint8_t> entry_bytes;
                if (!read_bundle_entry_bytes(bundle, bundle_reader, i, entry_bytes)) {
                    continue;
                }
                if (payload.stream_offset > entry_bytes.size()) {
                    continue;
                }
                const size_t available = entry_bytes.size() - static_cast<size_t>(payload.stream_offset);
                if (available < payload.stream_size) {
                    continue;
                }

                const auto begin = entry_bytes.begin() + static_cast<std::ptrdiff_t>(payload.stream_offset);
                payload.picture_data.assign(begin, begin + static_cast<std::ptrdiff_t>(payload.stream_size));
                return true;
            }

            return false;
        }

        void expand_rgb565(uint16_t color, uint8_t* rgba) {
            const uint8_t red = static_cast<uint8_t>(((color >> 11U) & 0x1FU) * 255U / 31U);
            const uint8_t green = static_cast<uint8_t>(((color >> 5U) & 0x3FU) * 255U / 63U);
            const uint8_t blue = static_cast<uint8_t>((color & 0x1FU) * 255U / 31U);
            rgba[0] = red;
            rgba[1] = green;
            rgba[2] = blue;
            rgba[3] = 255U;
        }

        void decode_dxt_colors(const uint8_t* block,
            std::array<std::array<uint8_t, 4>, 4>& palette,
            bool& use_transparent_color) {
            const uint16_t color0 = static_cast<uint16_t>(block[0] | (static_cast<uint16_t>(block[1]) << 8U));
            const uint16_t color1 = static_cast<uint16_t>(block[2] | (static_cast<uint16_t>(block[3]) << 8U));
            expand_rgb565(color0, palette[0].data());
            expand_rgb565(color1, palette[1].data());
            use_transparent_color = color0 <= color1;
            if (!use_transparent_color) {
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    palette[2][channel] = static_cast<uint8_t>((2U * palette[0][channel] + palette[1][channel]) / 3U);
                    palette[3][channel] = static_cast<uint8_t>((palette[0][channel] + 2U * palette[1][channel]) / 3U);
                }
                palette[2][3] = 255U;
                palette[3][3] = 255U;
            }
            else {
                for (std::size_t channel = 0; channel < 3U; ++channel) {
                    palette[2][channel] = static_cast<uint8_t>((palette[0][channel] + palette[1][channel]) / 2U);
                }
                palette[2][3] = 255U;
                palette[3] = { 0U, 0U, 0U, 0U };
            }
        }

        bool decode_dxt1(const TexturePayload& payload, std::vector<uint8_t>& rgba) {
            const uint32_t blocks_x = (payload.width + 3U) / 4U;
            const uint32_t blocks_y = (payload.height + 3U) / 4U;
            const size_t expected_size = static_cast<size_t>(blocks_x) * static_cast<size_t>(blocks_y) * 8U;
            if (payload.picture_data.size() < expected_size) {
                return false;
            }

            rgba.assign(static_cast<size_t>(payload.width) * static_cast<size_t>(payload.height) * 4U, 0U);
            size_t offset = 0;
            for (uint32_t by = 0; by < blocks_y; ++by) {
                for (uint32_t bx = 0; bx < blocks_x; ++bx) {
                    const uint8_t* block = payload.picture_data.data() + offset;
                    offset += 8U;
                    std::array<std::array<uint8_t, 4>, 4> palette{};
                    bool use_transparent_color = false;
                    decode_dxt_colors(block, palette, use_transparent_color);
                    (void)use_transparent_color;
                    const uint32_t indices = static_cast<uint32_t>(block[4]) |
                        (static_cast<uint32_t>(block[5]) << 8U) |
                        (static_cast<uint32_t>(block[6]) << 16U) |
                        (static_cast<uint32_t>(block[7]) << 24U);
                    for (uint32_t y = 0; y < 4U; ++y) {
                        for (uint32_t x = 0; x < 4U; ++x) {
                            const uint32_t px = bx * 4U + x;
                            const uint32_t py = by * 4U + y;
                            if (px >= payload.width || py >= payload.height) {
                                continue;
                            }
                            const uint32_t palette_index = (indices >> (2U * (4U * y + x))) & 0x03U;
                            uint8_t* dst = rgba.data() + ((static_cast<size_t>(py) * payload.width + px) * 4U);
                            std::copy(palette[palette_index].begin(), palette[palette_index].end(), dst);
                        }
                    }
                }
            }
            return true;
        }

        bool decode_dxt5(const TexturePayload& payload, std::vector<uint8_t>& rgba) {
            const uint32_t blocks_x = (payload.width + 3U) / 4U;
            const uint32_t blocks_y = (payload.height + 3U) / 4U;
            const size_t expected_size = static_cast<size_t>(blocks_x) * static_cast<size_t>(blocks_y) * 16U;
            if (payload.picture_data.size() < expected_size) {
                return false;
            }

            rgba.assign(static_cast<size_t>(payload.width) * static_cast<size_t>(payload.height) * 4U, 0U);
            size_t offset = 0;
            for (uint32_t by = 0; by < blocks_y; ++by) {
                for (uint32_t bx = 0; bx < blocks_x; ++bx) {
                    const uint8_t* block = payload.picture_data.data() + offset;
                    offset += 16U;

                    std::array<uint8_t, 8> alpha_palette{};
                    alpha_palette[0] = block[0];
                    alpha_palette[1] = block[1];
                    if (alpha_palette[0] > alpha_palette[1]) {
                        for (size_t i = 1; i < 7; ++i) {
                            alpha_palette[i + 1] = static_cast<uint8_t>(((7U - i) * alpha_palette[0] + i * alpha_palette[1]) / 7U);
                        }
                    }
                    else {
                        for (size_t i = 1; i < 5; ++i) {
                            alpha_palette[i + 1] = static_cast<uint8_t>(((5U - i) * alpha_palette[0] + i * alpha_palette[1]) / 5U);
                        }
                        alpha_palette[6] = 0U;
                        alpha_palette[7] = 255U;
                    }

                    uint64_t alpha_indices = 0;
                    for (int i = 0; i < 6; ++i) {
                        alpha_indices |= static_cast<uint64_t>(block[2 + i]) << (8U * static_cast<unsigned>(i));
                    }

                    std::array<std::array<uint8_t, 4>, 4> palette{};
                    bool use_transparent_color = false;
                    decode_dxt_colors(block + 8U, palette, use_transparent_color);
                    (void)use_transparent_color;
                    const uint32_t color_indices = static_cast<uint32_t>(block[12]) |
                        (static_cast<uint32_t>(block[13]) << 8U) |
                        (static_cast<uint32_t>(block[14]) << 16U) |
                        (static_cast<uint32_t>(block[15]) << 24U);

                    for (uint32_t y = 0; y < 4U; ++y) {
                        for (uint32_t x = 0; x < 4U; ++x) {
                            const uint32_t px = bx * 4U + x;
                            const uint32_t py = by * 4U + y;
                            if (px >= payload.width || py >= payload.height) {
                                continue;
                            }
                            const size_t pixel_index = 4U * y + x;
                            const uint32_t palette_index = (color_indices >> (2U * pixel_index)) & 0x03U;
                            const uint32_t alpha_index = static_cast<uint32_t>((alpha_indices >> (3U * pixel_index)) & 0x07U);
                            uint8_t* dst = rgba.data() + ((static_cast<size_t>(py) * payload.width + px) * 4U);
                            std::copy(palette[palette_index].begin(), palette[palette_index].end(), dst);
                            dst[3] = alpha_palette[alpha_index];
                        }
                    }
                }
            }
            return true;
        }

#if defined(_WIN32)
        template <typename T>
        void safe_release(T*& ptr) {
            if (ptr != nullptr) {
                ptr->Release();
                ptr = nullptr;
            }
        }

        bool decode_bc7_with_d3d11(const TexturePayload& payload, std::vector<uint8_t>& rgba) {
            const uint32_t block_width = (payload.width + 3U) / 4U;
            const uint32_t block_height = (payload.height + 3U) / 4U;
            const size_t expected_size = static_cast<size_t>(block_width) * static_cast<size_t>(block_height) * 16U;
            if (payload.picture_data.size() < expected_size || payload.width == 0 || payload.height == 0) {
                return false;
            }

            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
            const D3D_FEATURE_LEVEL feature_levels[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0 };
            D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

            HRESULT hr = D3D11CreateDevice(nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                0,
                feature_levels,
                static_cast<UINT>(std::size(feature_levels)),
                D3D11_SDK_VERSION,
                &device,
                &feature_level,
                &context);
            if (FAILED(hr)) {
                hr = D3D11CreateDevice(nullptr,
                    D3D_DRIVER_TYPE_WARP,
                    nullptr,
                    0,
                    feature_levels,
                    static_cast<UINT>(std::size(feature_levels)),
                    D3D11_SDK_VERSION,
                    &device,
                    &feature_level,
                    &context);
            }
            if (FAILED(hr) || device == nullptr || context == nullptr) {
                safe_release(context);
                safe_release(device);
                return false;
            }

            const char* vertex_shader_source =
                "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };"
                "VSOut main(uint vertexId : SV_VertexID) {"
                "  float2 pos[3] = { float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0) };"
                "  float2 uv[3] = { float2(0.0, 1.0), float2(0.0, -1.0), float2(2.0, 1.0) };"
                "  VSOut outv; outv.pos = float4(pos[vertexId], 0.0, 1.0); outv.uv = uv[vertexId]; return outv; }";
            const char* pixel_shader_source =
                "Texture2D tex0 : register(t0);"
                "SamplerState samp0 : register(s0);"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {"
                "  return tex0.Sample(samp0, uv); }";

            ID3DBlob* vs_blob = nullptr;
            ID3DBlob* ps_blob = nullptr;
            ID3DBlob* error_blob = nullptr;
            hr = D3DCompile(vertex_shader_source, std::strlen(vertex_shader_source), nullptr, nullptr, nullptr,
                "main", "vs_4_0", 0, 0, &vs_blob, &error_blob);
            safe_release(error_blob);
            if (FAILED(hr) || vs_blob == nullptr) {
                safe_release(ps_blob);
                safe_release(vs_blob);
                safe_release(context);
                safe_release(device);
                return false;
            }
            hr = D3DCompile(pixel_shader_source, std::strlen(pixel_shader_source), nullptr, nullptr, nullptr,
                "main", "ps_4_0", 0, 0, &ps_blob, &error_blob);
            safe_release(error_blob);
            if (FAILED(hr) || ps_blob == nullptr) {
                safe_release(ps_blob);
                safe_release(vs_blob);
                safe_release(context);
                safe_release(device);
                return false;
            }

            ID3D11VertexShader* vertex_shader = nullptr;
            ID3D11PixelShader* pixel_shader = nullptr;
            hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader);
            if (SUCCEEDED(hr)) {
                hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader);
            }
            safe_release(ps_blob);
            safe_release(vs_blob);
            if (FAILED(hr) || vertex_shader == nullptr || pixel_shader == nullptr) {
                safe_release(pixel_shader);
                safe_release(vertex_shader);
                safe_release(context);
                safe_release(device);
                return false;
            }

            D3D11_TEXTURE2D_DESC source_desc{};
            source_desc.Width = payload.width;
            source_desc.Height = payload.height;
            source_desc.MipLevels = 1;
            source_desc.ArraySize = 1;
            source_desc.Format = DXGI_FORMAT_BC7_UNORM;
            source_desc.SampleDesc.Count = 1;
            source_desc.Usage = D3D11_USAGE_IMMUTABLE;
            source_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA source_data{};
            source_data.pSysMem = payload.picture_data.data();
            source_data.SysMemPitch = block_width * 16U;

            ID3D11Texture2D* source_texture = nullptr;
            hr = device->CreateTexture2D(&source_desc, &source_data, &source_texture);
            if (FAILED(hr) || source_texture == nullptr) {
                safe_release(pixel_shader);
                safe_release(vertex_shader);
                safe_release(context);
                safe_release(device);
                return false;
            }

            ID3D11ShaderResourceView* shader_resource_view = nullptr;
            hr = device->CreateShaderResourceView(source_texture, nullptr, &shader_resource_view);

            D3D11_TEXTURE2D_DESC render_desc{};
            render_desc.Width = payload.width;
            render_desc.Height = payload.height;
            render_desc.MipLevels = 1;
            render_desc.ArraySize = 1;
            render_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            render_desc.SampleDesc.Count = 1;
            render_desc.Usage = D3D11_USAGE_DEFAULT;
            render_desc.BindFlags = D3D11_BIND_RENDER_TARGET;

            ID3D11Texture2D* render_texture = nullptr;
            if (SUCCEEDED(hr)) {
                hr = device->CreateTexture2D(&render_desc, nullptr, &render_texture);
            }

            ID3D11RenderTargetView* render_target_view = nullptr;
            if (SUCCEEDED(hr)) {
                hr = device->CreateRenderTargetView(render_texture, nullptr, &render_target_view);
            }

            D3D11_TEXTURE2D_DESC staging_desc = render_desc;
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.BindFlags = 0;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* staging_texture = nullptr;
            if (SUCCEEDED(hr)) {
                hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
            }

            ID3D11SamplerState* sampler_state = nullptr;
            if (SUCCEEDED(hr)) {
                D3D11_SAMPLER_DESC sampler_desc{};
                sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
                hr = device->CreateSamplerState(&sampler_desc, &sampler_state);
            }

            if (FAILED(hr) || shader_resource_view == nullptr || render_texture == nullptr || render_target_view == nullptr ||
                staging_texture == nullptr || sampler_state == nullptr) {
                safe_release(sampler_state);
                safe_release(staging_texture);
                safe_release(render_target_view);
                safe_release(render_texture);
                safe_release(shader_resource_view);
                safe_release(source_texture);
                safe_release(pixel_shader);
                safe_release(vertex_shader);
                safe_release(context);
                safe_release(device);
                return false;
            }

            const FLOAT clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            context->OMSetRenderTargets(1, &render_target_view, nullptr);
            context->ClearRenderTargetView(render_target_view, clear_color);
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<FLOAT>(payload.width);
            viewport.Height = static_cast<FLOAT>(payload.height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vertex_shader, nullptr, 0);
            context->PSSetShader(pixel_shader, nullptr, 0);
            context->PSSetShaderResources(0, 1, &shader_resource_view);
            context->PSSetSamplers(0, 1, &sampler_state);
            context->Draw(3, 0);
            context->CopyResource(staging_texture, render_texture);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = context->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                safe_release(sampler_state);
                safe_release(staging_texture);
                safe_release(render_target_view);
                safe_release(render_texture);
                safe_release(shader_resource_view);
                safe_release(source_texture);
                safe_release(pixel_shader);
                safe_release(vertex_shader);
                safe_release(context);
                safe_release(device);
                return false;
            }

            rgba.resize(static_cast<size_t>(payload.width) * static_cast<size_t>(payload.height) * 4U);
            for (uint32_t y = 0; y < payload.height; ++y) {
                const uint8_t* src = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
                uint8_t* dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(payload.width) * 4U;
                std::copy(src, src + static_cast<std::ptrdiff_t>(payload.width * 4U), dst);
            }
            context->Unmap(staging_texture, 0);

            safe_release(sampler_state);
            safe_release(staging_texture);
            safe_release(render_target_view);
            safe_release(render_texture);
            safe_release(shader_resource_view);
            safe_release(source_texture);
            safe_release(pixel_shader);
            safe_release(vertex_shader);
            safe_release(context);
            safe_release(device);
            return true;
        }
#endif

        bool decode_texture_to_rgba(const TexturePayload& payload, std::vector<uint8_t>& rgba) {
            const size_t pixel_count = static_cast<size_t>(payload.width) * static_cast<size_t>(payload.height);
            switch (payload.format) {
            case 1: {
                if (payload.picture_data.size() < pixel_count) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba[i * 4U + 0U] = 255U;
                    rgba[i * 4U + 1U] = 255U;
                    rgba[i * 4U + 2U] = 255U;
                    rgba[i * 4U + 3U] = payload.picture_data[i];
                }
                return true;
            }
            case 3: {
                if (payload.picture_data.size() < pixel_count * 3U) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba[i * 4U + 0U] = payload.picture_data[i * 3U + 0U];
                    rgba[i * 4U + 1U] = payload.picture_data[i * 3U + 1U];
                    rgba[i * 4U + 2U] = payload.picture_data[i * 3U + 2U];
                    rgba[i * 4U + 3U] = 255U;
                }
                return true;
            }
            case 4: {
                if (payload.picture_data.size() < pixel_count * 4U) {
                    return false;
                }
                rgba = payload.picture_data;
                return true;
            }
            case 5: {
                if (payload.picture_data.size() < pixel_count * 4U) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba[i * 4U + 0U] = payload.picture_data[i * 4U + 1U];
                    rgba[i * 4U + 1U] = payload.picture_data[i * 4U + 2U];
                    rgba[i * 4U + 2U] = payload.picture_data[i * 4U + 3U];
                    rgba[i * 4U + 3U] = payload.picture_data[i * 4U + 0U];
                }
                return true;
            }
            case 7: {
                if (payload.picture_data.size() < pixel_count * 2U) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    expand_rgb565(static_cast<uint16_t>(payload.picture_data[i * 2U] |
                        (static_cast<uint16_t>(payload.picture_data[i * 2U + 1U]) << 8U)),
                        rgba.data() + i * 4U);
                }
                return true;
            }
            case 10:
                return decode_dxt1(payload, rgba);
            case 12:
                return decode_dxt5(payload, rgba);
            case 25:
#if defined(_WIN32)
                return decode_bc7_with_d3d11(payload, rgba);
#else
                return false;
#endif
            case 13: {
                if (payload.picture_data.size() < pixel_count * 2U) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    const uint16_t value = static_cast<uint16_t>(payload.picture_data[i * 2U] |
                        (static_cast<uint16_t>(payload.picture_data[i * 2U + 1U]) << 8U));
                    rgba[i * 4U + 0U] = static_cast<uint8_t>(((value >> 12U) & 0x0FU) * 17U);
                    rgba[i * 4U + 1U] = static_cast<uint8_t>(((value >> 8U) & 0x0FU) * 17U);
                    rgba[i * 4U + 2U] = static_cast<uint8_t>(((value >> 4U) & 0x0FU) * 17U);
                    rgba[i * 4U + 3U] = static_cast<uint8_t>((value & 0x0FU) * 17U);
                }
                return true;
            }
            case 14: {
                if (payload.picture_data.size() < pixel_count * 4U) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba[i * 4U + 0U] = payload.picture_data[i * 4U + 2U];
                    rgba[i * 4U + 1U] = payload.picture_data[i * 4U + 1U];
                    rgba[i * 4U + 2U] = payload.picture_data[i * 4U + 0U];
                    rgba[i * 4U + 3U] = payload.picture_data[i * 4U + 3U];
                }
                return true;
            }
            case 62: {
                if (payload.picture_data.size() < pixel_count * 2U) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba[i * 4U + 0U] = payload.picture_data[i * 2U + 0U];
                    rgba[i * 4U + 1U] = payload.picture_data[i * 2U + 1U];
                    rgba[i * 4U + 2U] = 0U;
                    rgba[i * 4U + 3U] = 255U;
                }
                return true;
            }
            case 63: {
                if (payload.picture_data.size() < pixel_count) {
                    return false;
                }
                rgba.resize(pixel_count * 4U);
                for (size_t i = 0; i < pixel_count; ++i) {
                    rgba[i * 4U + 0U] = payload.picture_data[i];
                    rgba[i * 4U + 1U] = payload.picture_data[i];
                    rgba[i * 4U + 2U] = payload.picture_data[i];
                    rgba[i * 4U + 3U] = 255U;
                }
                return true;
            }
            default:
                return false;
            }
        }

        void flip_rgba_rows(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height) {
            if (height <= 1 || width == 0) {
                return;
            }
            const size_t stride = static_cast<size_t>(width) * 4U;
            std::vector<uint8_t> tmp(stride);
            for (uint32_t y = 0; y < height / 2U; ++y) {
                uint8_t* top = rgba.data() + static_cast<size_t>(y) * stride;
                uint8_t* bottom = rgba.data() + static_cast<size_t>(height - 1U - y) * stride;
                std::copy(top, top + static_cast<std::ptrdiff_t>(stride), tmp.data());
                std::copy(bottom, bottom + static_cast<std::ptrdiff_t>(stride), top);
                std::copy(tmp.data(), tmp.data() + static_cast<std::ptrdiff_t>(stride), bottom);
            }
        }

    }  // namespace

    bool extract_unity_texture_bundle_to_png(const std::filesystem::path& ab_file,
        const std::filesystem::path& png_file) {
        IAssetsReader* bundle_reader = Create_AssetsReaderFromFile(ab_file.c_str(), true, RWOpenFlags_Immediately);
        if (bundle_reader == nullptr) {
            return false;
        }

        AssetBundleFile bundle;
        if (!bundle.Read(bundle_reader, nullptr, true)) {
            Free_AssetsReader(bundle_reader);
            return false;
        }

        std::filesystem::path unpacked_bundle_path;
        if (bundle.IsCompressed()) {
            unpacked_bundle_path = make_temp_bundle_path();
            IAssetsWriter* unpacked_writer = Create_AssetsWriterToFile(unpacked_bundle_path.c_str(), true, true, RWOpenFlags_Immediately);
            if (unpacked_writer == nullptr) {
                Free_AssetsReader(bundle_reader);
                return false;
            }
            if (!bundle.Unpack(bundle_reader, unpacked_writer)) {
                Free_AssetsWriter(unpacked_writer);
                Free_AssetsReader(bundle_reader);
                std::error_code ec;
                std::filesystem::remove(unpacked_bundle_path, ec);
                return false;
            }
            Free_AssetsWriter(unpacked_writer);
            Free_AssetsReader(bundle_reader);

            bundle_reader = Create_AssetsReaderFromFile(unpacked_bundle_path.c_str(), true, RWOpenFlags_Immediately);
            if (bundle_reader == nullptr) {
                std::error_code ec;
                std::filesystem::remove(unpacked_bundle_path, ec);
                return false;
            }

            bundle.Close();
            if (!bundle.Read(bundle_reader, nullptr, true)) {
                Free_AssetsReader(bundle_reader);
                std::error_code ec;
                std::filesystem::remove(unpacked_bundle_path, ec);
                return false;
            }
        }

        const size_t entry_count = bundle.bundleHeader6.fileVersion >= 6
            ? static_cast<size_t>(bundle.bundleInf6 != nullptr ? bundle.bundleInf6->directoryCount : 0)
            : static_cast<size_t>(bundle.assetsLists3 != nullptr ? bundle.assetsLists3->count : 0);
        bool ok = false;

        for (size_t entry_index = 0; entry_index < entry_count && !ok; ++entry_index) {
            if (!bundle.IsAssetsFile(bundle_reader, entry_index)) {
                continue;
            }

            IAssetsReader* assets_reader = nullptr;
            if (bundle.bundleHeader6.fileVersion >= 6) {
                assets_reader = bundle.MakeAssetsFileReader(bundle_reader, &bundle.bundleInf6->dirInf[entry_index]);
            }
            else {
                assets_reader = bundle.MakeAssetsFileReader(bundle_reader, bundle.assetsLists3->ppEntries[entry_index]);
            }
            if (assets_reader == nullptr) {
                continue;
            }

            AssetsFile assets_file(assets_reader);
            if (!assets_file.typeTree.hasTypeTree || assets_file.header.format < 0x0D) {
                Free_AssetsReader(assets_reader);
                continue;
            }

            AssetsFileTable asset_table(&assets_file);
            for (unsigned int i = 0; i < asset_table.assetFileInfoCount && !ok; ++i) {
                AssetFileInfoEx& asset_info = asset_table.pAssetFileInfo[i];
                if (resolve_class_id(assets_file, asset_info) != kTexture2DClassId) {
                    continue;
                }

                Type_0D* type_info = resolve_type_tree(assets_file, asset_info);
                if (type_info == nullptr) {
                    continue;
                }

                AssetTypeTemplateField template_field;
                if (!template_field.From0D(type_info, 0)) {
                    continue;
                }
                AssetTypeTemplateField* base_template = &template_field;
                AssetTypeInstance instance(1, &base_template, asset_info.curFileSize, assets_file.pReader, is_big_endian(assets_file), asset_info.absolutePos);
                AssetTypeValueField* base_field = instance.GetBaseField();

                TexturePayload payload;
                if (!read_texture_payload(payload, base_field)) {
                    continue;
                }
                if (payload.picture_data.empty() && !load_stream_payload(bundle, bundle_reader, ab_file, payload)) {
                    continue;
                }

                std::vector<uint8_t> rgba;
                if (!decode_texture_to_rgba(payload, rgba)) {
                    continue;
                }

                flip_rgba_rows(rgba, payload.width, payload.height);
                if (!png_file.parent_path().empty()) {
                    std::filesystem::create_directories(png_file.parent_path());
                }
                ok = lodepng_encode32_fileW(png_file.c_str(), rgba.data(), payload.width, payload.height) == 0U;
            }

            Free_AssetsReader(assets_reader);
        }

        Free_AssetsReader(bundle_reader);
        if (!unpacked_bundle_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(unpacked_bundle_path, ec);
        }
        return ok;
    }

}  // namespace maiconv