#include "load_save_png.hpp"

#include <png.h>

#include <iostream>
#include <fstream>
#include <cassert>
#include <vector>

#define LOG_ERROR(X) std::cerr << X << std::endl

using std::vector;

bool load_png(std::istream &from, unsigned int *width, unsigned int *height, vector<glm::u8vec4> *data,
              OriginLocation origin);

void
save_png(std::ostream &to, unsigned int width, unsigned int height, glm::u8vec4 const *data, OriginLocation origin);

void load_png(const std::string &filename, glm::uvec2 *size, std::vector<glm::u8vec4> *data, OriginLocation origin) {
    assert(size);
    
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open PNG image file '" + filename + "'.");
    }
    if (!load_png(file, &size->x, &size->y, data, origin)) {
        throw std::runtime_error("Failed to read PNG image from '" + filename + "'.");
    }
}

void save_png(const std::string &filename, glm::uvec2 size, glm::u8vec4 const *data, OriginLocation origin) {
    std::ofstream file(filename.c_str(), std::ios::binary);
    save_png(file, size.x, size.y, data, origin);
}


static void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto *from = reinterpret_cast< std::istream * >(png_get_io_ptr(png_ptr));
    assert(from);
    if (!from->read(reinterpret_cast< char * >(data), length)) {
        png_error(png_ptr, "Error reading.");
    }
}

static void user_write_data(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto *to = reinterpret_cast< std::ostream * >(png_get_io_ptr(png_ptr));
    assert(to);
    if (!to->write(reinterpret_cast< char * >(data), length)) {
        png_error(png_ptr, "Error writing.");
    }
}

static void user_flush_data(png_structp png_ptr) {
    auto *to = reinterpret_cast< std::ostream * >(png_get_io_ptr(png_ptr));
    assert(to);
    if (!to->flush()) {
        png_error(png_ptr, "Error flushing.");
    }
}


bool load_png(std::istream &from, unsigned int *width, unsigned int *height, vector<glm::u8vec4> *data,
              OriginLocation origin) {
    assert(data);
    uint32_t local_width, local_height;
    if (width == nullptr) width = &local_width;
    if (height == nullptr) height = &local_height;
    *width = *height = 0;
    data->clear();
    //..... load file ......
    //Load a png file, as per the libpng docs:
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) nullptr, (png_error_ptr) nullptr,
                                             (png_error_ptr) nullptr);
    
    png_set_read_fn(png, &from, user_read_data);
    
    if (!png) {
        LOG_ERROR("  cannot alloc read struct.");
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        LOG_ERROR("  cannot alloc info struct.");
        png_destroy_read_struct(&png, (png_infopp) nullptr, (png_infopp) nullptr);
        return false;
    }
    png_bytep *row_pointers = nullptr;
    if (setjmp(png_jmpbuf(png))) {
        LOG_ERROR("  png interal error.");
        png_destroy_read_struct(&png, &info, (png_infopp) nullptr);
        if (row_pointers != nullptr) delete[] row_pointers;
        data->clear();
        return false;
    }
    //not needed with custom read/write functions: png_init_io(png, NULL);
    png_read_info(png, info);
    unsigned int w = png_get_image_width(png, info);
    unsigned int h = png_get_image_height(png, info);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY ||
        png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if (!(png_get_color_type(png, info) & PNG_COLOR_MASK_ALPHA))
        png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    if (png_get_bit_depth(png, info) < 8)
        png_set_packing(png);
    if (png_get_bit_depth(png, info) == 16)
        png_set_strip_16(png);
    //Ok, should be 32-bit RGBA now.
    
    png_read_update_info(png, info);
    size_t rowbytes = png_get_rowbytes(png, info);
    //Make sure it's the format we think it is...
    assert(rowbytes == w * sizeof(uint32_t));
    
    data->resize(w * h);
    row_pointers = new png_bytep[h];
    for (unsigned int r = 0; r < h; ++r) {
        if (origin == LowerLeftOrigin) {
            row_pointers[h - 1 - r] = (png_bytep) (&(*data)[r * w]);
        } else {
            row_pointers[r] = (png_bytep) (&(*data)[r * w]);
        }
    }
    png_read_image(png, row_pointers);
    png_destroy_read_struct(&png, &info, nullptr);
    delete[] row_pointers;
    
    *width = w;
    *height = h;
    return true;
}


void
save_png(std::ostream &to, unsigned int width, unsigned int height, glm::u8vec4 const *data, OriginLocation origin) {
//After the libpng example.c
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    
    png_set_write_fn(png_ptr, &to, user_write_data, user_flush_data);
    
    if (png_ptr == nullptr) {
        LOG_ERROR("Can't create write struct.");
        return;
    }
    
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        LOG_ERROR("Can't craete info pointer");
        return;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        LOG_ERROR("Error writing png.");
        return;
    }
    
    //Not needed with custom read/write functions: png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    
    png_write_info(png_ptr, info_ptr);
    //png_set_swap_alpha(png_ptr) // might need?
    vector<png_bytep> row_pointers(height);
    for (unsigned int i = 0; i < height; ++i) {
        if (origin == UpperLeftOrigin) {
            row_pointers[i] = (png_bytep) &(data[i * width]);
        } else {
            row_pointers[i] = (png_bytep) &(data[(height - 1 - i) * width]);
        }
    }
    png_write_image(png_ptr, &(row_pointers[0]));
    
    png_write_end(png_ptr, info_ptr);
    
    png_destroy_write_struct(&png_ptr, &info_ptr);
}
