// Chemfiles, a modern library for chemistry file reading and writing
// Copyright (C) Guillaume Fraux and contributors -- BSD license

#ifndef CHEMFILES_FORMAT_SDF_HPP
#define CHEMFILES_FORMAT_SDF_HPP

#include <iosfwd>
#include <string>

#include "chemfiles/Format.hpp"
#include "chemfiles/File.hpp"

namespace chemfiles {
class Frame;

/// [SDF] file format reader and writer.
///
/// [SDF]: http://accelrys.com/products/collaborative-science/biovia-draw/ctfile-no-fee.html
class SDFFormat final: public TextFormat {
public:
    SDFFormat(std::string path, File::Mode mode, File::Compression compression):
        TextFormat(std::move(path), mode, compression) {}

    void read_next(Frame& frame) override;
    void write_next(const Frame& frame) override;
    std::streampos forward() override;
};

template<> FormatInfo format_information<SDFFormat>();

} // namespace chemfiles

#endif
