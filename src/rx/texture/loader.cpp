#include <string.h> // memcpy

#include "rx/texture/loader.h"

#include "rx/core/filesystem/file.h"
#include "rx/core/log.h"

#include "lib/stb_image.h"

RX_LOG("texture/loader", logger);

namespace rx::texture {

bool loader::load(const string& _file_name, pixel_format _want_format) {
  auto read_data{filesystem::read_binary_file(m_allocator, _file_name)};
  if (!read_data) {
    return false;
  }

  const auto& data{*read_data};

  math::vec2<int> dimensions;

  int want_channels{0};
  switch (_want_format) {
  case pixel_format::k_r_u8:
    want_channels = 1;
    break;
  case pixel_format::k_rgb_u8:
    [[fallthrough]];
  case pixel_format::k_bgr_u8:
    want_channels = 3;
    break;
  case pixel_format::k_rgba_u8:
    [[fallthrough]];
  case pixel_format::k_bgra_u8:
    want_channels = 4;
    break;
  }

  int channels;
  rx_byte* decoded_image{stbi_load_from_memory(
    data.data(),
    static_cast<int>(data.size()),
    &dimensions.w,
    &dimensions.h,
    &channels,
    want_channels)};

  if (!decoded_image) {
    logger(log::level::k_error, "%s failed %s", _file_name, stbi_failure_reason());
    return false;
  }

  m_dimensions = dimensions.cast<rx_size>();
  m_channels = want_channels;
  m_bpp = want_channels;

  m_data.resize(m_dimensions.area() * m_bpp);
  memcpy(m_data.data(), decoded_image, m_data.size());

  stbi_image_free(decoded_image);

  // When there's an alpha channel but it encodes fully opaque, remove it.
  if (want_channels == 4) {
    bool can_remove_alpha{true};
    for (int y{0}; y < dimensions.h; y++) {
      int scan_line_offset{dimensions.w * y};
      for (int x{0}; x < dimensions.w; x++) {
        // When an alpha pixel has a value other than 255, we can't optimize.
        if (m_data[(scan_line_offset + x) * channels + 3] != 255) {
          can_remove_alpha = false;
          break;
        }
      }
    }

    if (can_remove_alpha) {
      // Copy all pixels from m_data to data removing the alpha channel.
      vector<rx_byte> data{m_allocator, dimensions.area() * 3_z};
      for (int y{0}; y < dimensions.h; y++) {
        const int scan_line_offset{dimensions.w * y};
        for (int x{0}; x < dimensions.w; x++) {
          const int pixel_offset{scan_line_offset + x};

          const int src_pixel_offset{pixel_offset * 4};
          const int dst_pixel_offset{pixel_offset * 3};

          data[dst_pixel_offset + 0] = m_data[src_pixel_offset + 0];
          data[dst_pixel_offset + 1] = m_data[src_pixel_offset + 1];
          data[dst_pixel_offset + 2] = m_data[src_pixel_offset + 2];
        }
      }
      m_channels = 3;
      m_bpp = 3;
      m_data = utility::move(data);
      logger(log::level::k_info, "%s removed alpha channel (not used)", _file_name);
    }
  }

  // When the requested pixel format is a BGR format go ahead and inplace
  // swap the pixels. We cannot use convert for this because we want this
  // to behave inplace.
  if (_want_format == pixel_format::k_bgr_u8
    || _want_format == pixel_format::k_bgra_u8)
  {
    const rx_size samples{dimensions.area() * static_cast<rx_size>(channels)};
    for (rx_size i{0}; i < samples; i += channels) {
      const auto r{m_data[0]};
      const auto b{m_data[2]};
      m_data[0] = b;
      m_data[2] = r;
    }
  }

  logger(log::level::k_verbose, "%s loaded %zux%zu @ %zu bpp", _file_name,
    m_dimensions.w, m_dimensions.h, m_bpp);

  return true;
}

} // namespace rx::texture
