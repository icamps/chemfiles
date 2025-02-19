// Chemfiles, a modern library for chemistry file reading and writing
// Copyright (C) Guillaume Fraux and contributors -- BSD license

#include <istream>
#include <memory>
#include <string>
#include <vector>
#include <typeinfo>

#include "chemfiles/File.hpp"
#include "chemfiles/Format.hpp"
#include "chemfiles/ErrorFmt.hpp"

namespace chemfiles {
    class Frame;
}

using namespace chemfiles;

void Format::read_step(size_t /*unused*/, Frame& /*unused*/) {
    throw format_error(
        "'read_step' is not implemented for this format ({})",
        typeid(*this).name()
    );
}

void Format::read(Frame& /*unused*/) {
    throw format_error(
        "'read' is not implemented for this format ({})",
        typeid(*this).name()
    );
}

void Format::write(const Frame& /*unused*/) {
    throw format_error(
        "'write' is not implemented for this format ({})",
        typeid(*this).name()
    );
}


TextFormat::TextFormat(std::string path, File::Mode mode, File::Compression compression):
    file_(TextFile::open(std::move(path), mode, compression)) {}

void TextFormat::scan_all() {
    if (eof_found_) {
        return;
    }

    auto before = file_->tellg();
    while (!file_->eof()) {
        auto position = forward();
        if (position == std::streampos(-1)) {
            break;
        }
        if (!file_) {
            throw format_error("IO error while reading '{}'", file_->path());
        }
        steps_positions_.push_back(position);
    }

    eof_found_ = true;
    // reset failbit in the file
    file_->clear();

    if (before == std::streampos(0) && !steps_positions_.empty()) {
        file_->seekg(steps_positions_[0]);
    } else {
        file_->seekg(before);
    }
}

void TextFormat::read_step(size_t step, Frame& frame) {
    // Start by checking if we know this step, if not, look for all steps in
    // the file
    if (step >= steps_positions_.size()) {
        scan_all();
    }

    // If the step is still too big, this is an error
    if (step >= steps_positions_.size()) {
        if (steps_positions_.size() == 0) {
            throw file_error(
                "can not read file '{}' at step {}, it does not contain any step",
                file_->path(), step
            );
        } else {
            throw file_error(
                "can not read file '{}' at step {}: maximal step is {}",
                file_->path(), step, steps_positions_.size() - 1
            );
        }
    }

    file_->seekg(steps_positions_[step]);
    read_next(frame);
}

void TextFormat::read(Frame& frame) {
    auto position = file_->tellg();
    read_next(frame);
    // If no exception was thrown, we can add this step to the list
    steps_positions_.push_back(position);
}

void TextFormat::write(const Frame& frame) {
    write_next(frame);
    steps_positions_.push_back(file_->tellg());
}

size_t TextFormat::nsteps() {
    scan_all();
    return steps_positions_.size();
}

void TextFormat::read_next(Frame& /*unused*/) {
    throw format_error(
        "'read' is not implemented for this format ({})",
        typeid(*this).name()
    );
}

void TextFormat::write_next(const Frame& /*unused*/) {
    throw format_error(
        "'write' is not implemented for this format ({})",
        typeid(*this).name()
    );
}
