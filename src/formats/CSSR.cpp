// Chemfiles, a modern library for chemistry file reading and writing
// Copyright (C) Guillaume Fraux and contributors -- BSD license

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <istream>

#include <fmt/ostream.h>

#include "chemfiles/File.hpp"
#include "chemfiles/Format.hpp"
#include "chemfiles/Atom.hpp"
#include "chemfiles/Frame.hpp"
#include "chemfiles/Topology.hpp"
#include "chemfiles/UnitCell.hpp"
#include "chemfiles/Connectivity.hpp"

#include "chemfiles/types.hpp"
#include "chemfiles/utils.hpp"
#include "chemfiles/ErrorFmt.hpp"
#include "chemfiles/warnings.hpp"

#include "chemfiles/formats/CSSR.hpp"

using namespace chemfiles;

static bool is_digit(char c) {
    return c == '0' || c == '1' || c == '2' || c == '3' || c == '4' ||
           c == '5' || c == '6' || c == '7' || c == '8' || c == '9';
}


template<> FormatInfo chemfiles::format_information<CSSRFormat>() {
    return FormatInfo("CSSR").with_extension(".cssr").description(
        "CSSR text format"
    );
}


CSSRFormat::CSSRFormat(std::string path, File::Mode mode, File::Compression compression)
    : TextFormat(std::move(path), mode, compression)
{
    if (mode == File::APPEND) {
        throw format_error("append mode ('a') is not supported with CSSR format");
    }
}

void CSSRFormat::read_next(Frame& frame) {
    if (file_->tellg() != std::streampos(0)) {
        throw format_error("CSSR format only supports reading one frame");
    }

    // Read unit cell
    double a = 0, b = 0, c = 0;
    scan(file_->readline(), "%*38c%lf %lf %lf", &a, &b, &c);
    double alpha = 0, beta = 0, gamma = 0;
    scan(file_->readline(), "%*21c%lf %lf %lf", &alpha, &beta, &gamma);
    frame.set_cell(UnitCell(a, b, c, alpha, beta, gamma));

    size_t natoms = 0;
    int coordinate_style = -1;
    scan(file_->readline(), "%zu %d", &natoms, &coordinate_style);
    bool use_fractional = (coordinate_style == 0);

    // Title line
    file_->skipline();

    frame.resize(0);
    frame.reserve(natoms);

    std::vector<std::vector<size_t>> connectivity(natoms);
    for (auto&& line: file_->readlines(natoms)) {
        unsigned atom_id = 0;
        char name[5] = {0};
        double x = 0, y = 0, z = 0;
        unsigned bonds[8] = {0};
        double charge = 0;

        scan(line, "%u %4s %lf %lf %lf %u %u %u %u %u %u %u %u %lf",
            &atom_id, &name[0], &x, &y, &z, &bonds[0], &bonds[1], &bonds[2],
            &bonds[3], &bonds[4], &bonds[5], &bonds[6], &bonds[7], &charge
        );

        auto position = Vector3D(x, y, z);
        if (use_fractional) {
            position = frame.cell().matrix() * position;
        }

        // Atomic names can be created as <type><id>: O121 H22
        auto type = std::string(name);
        size_t type_length = 0;
        for (auto ch: type) {
            if (is_digit(ch)) {
                break;
            }
            type_length += 1;
        }
        type = type.substr(0, type_length);

        auto atom = Atom(name, type);
        atom.set_charge(charge);
        frame.add_atom(std::move(atom), position);

        for (auto bond: bonds) {
            if (bond != 0) {
                connectivity[atom_id - 1].push_back(bond - 1);
            }
        }
    }

    for (size_t i=0; i<natoms; i++) {
        for (auto j: connectivity[i]) {
            frame.add_bond(i, j);
        }
    }
}

void CSSRFormat::write_next(const Frame& frame) {
    if (file_->tellg() != std::streampos(0)) {
        throw format_error("CSSR format only supports writing one frame");
    }

    fmt::print(
        *file_, " REFERENCE STRUCTURE = 00000   A,B,C ={:8.3f}{:8.3f}{:8.3f}\n",
        frame.cell().a(), frame.cell().b(), frame.cell().c()
    );
    fmt::print(
        *file_, "   ALPHA,BETA,GAMMA ={:8.3f}{:8.3f}{:8.3f}    SPGR =  1 P1\n",
        frame.cell().alpha(), frame.cell().beta(), frame.cell().gamma()
    );

    if (frame.size() > 9999) {
        warning("CCSR writer", "too many atoms, the file might not open with other programs");
        fmt::print(*file_, "{} 0\n", frame.size());
    } else {
        fmt::print(*file_, "{:4}   0\n", frame.size());
    }

    // TODO: use the frame name/title property in the file title
    fmt::print(*file_, " file created with chemfiles\n", frame.size());

    auto connectivity = std::vector<std::vector<size_t>>(frame.size());
    for (auto& bond : frame.topology().bonds()) {
        if (bond[0] > 9999 || bond[1] > 9999) {
            warning("CCSR writer", "atomic index is too big for connectivity record, removing the bond");
            continue;
        }
        connectivity[bond[0]].push_back(bond[1]);
        connectivity[bond[1]].push_back(bond[0]);
    }

    auto& positions = frame.positions();
    auto cell_inv = frame.cell().matrix().invert();
    for (size_t i = 0; i<frame.size(); i++) {
        std::string atom_id;
        if (i <= 9999) {
            atom_id = std::to_string(i + 1);
        } else {
            atom_id = "****";
        }

        auto fractional = cell_inv * positions[i];
        fmt::print(*file_, "{:4} {:4}  {:9.5f} {:9.5f} {:9.5f}",
            atom_id, frame[i].name(), fractional[0], fractional[1], fractional[2]
        );

        auto bonds = 0;
        for (auto bond: connectivity[i]) {
            if (bonds >= 8) {
                warning("CCSR writer", "too many bonds with atom {}, only 8 are supported", i);
                break;
            }
            fmt::print(*file_, "{:4}", bond + 1);
            bonds += 1;
        }
        while (bonds < 8) {
            fmt::print(*file_, "   0");
            bonds += 1;
        }

        fmt::print(*file_, " {:7.3f}\n", frame[i].charge());
    }
}

std::streampos CSSRFormat::forward() {
    // CSSR only supports one step, so always act like there is only one
    auto position = file_->tellg();
    if (position == std::streampos(0)) {
        // advance the pointer for the next call
        file_->skipline();
        return position;
    } else {
        return std::streampos(-1);
    }
}
