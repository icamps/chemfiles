// Chemfiles, a modern library for chemistry file reading and writing
// Copyright (C) Guillaume Fraux and contributors -- BSD license

#include <cassert>
#include <cmath>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <exception>

#include <fmt/ostream.h>

#include "chemfiles/File.hpp"
#include "chemfiles/Format.hpp"
#include "chemfiles/Atom.hpp"
#include "chemfiles/Frame.hpp"
#include "chemfiles/Property.hpp"
#include "chemfiles/Topology.hpp"
#include "chemfiles/Connectivity.hpp"

#include "chemfiles/types.hpp"
#include "chemfiles/utils.hpp"
#include "chemfiles/warnings.hpp"
#include "chemfiles/ErrorFmt.hpp"
#include "chemfiles/external/optional.hpp"

#include "chemfiles/formats/SDF.hpp"

using namespace chemfiles;

template<> FormatInfo chemfiles::format_information<SDFFormat>() {
    return FormatInfo("SDF").with_extension(".sdf").description(
        "Structural Data File format"
    );
}

void SDFFormat::read_next(Frame& frame) {

    // Make this global as it may contain information we need later
    std::string counts_line;
    size_t natoms = 0;
    size_t nbonds = 0;
    try {
        std::string molecule_name = file_->readline();
        frame.set("name", molecule_name);

        file_->skipline(); // Program line - skip it
        file_->skipline(); // Comment line - skip it

        counts_line = file_->readline();
        natoms = parse<size_t>(counts_line.substr(0, 3));
        nbonds = parse<size_t>(counts_line.substr(3, 3));
    } catch (const std::exception& e) {
        throw format_error("can not read next step as SDF: {}", e.what());
    }

    std::vector<std::string> atom_lines;
    try {
        atom_lines = file_->readlines(natoms);
    } catch (const FileError& e) {
        throw format_error("can not read file: {}", e.what());
    }

    frame.reserve(natoms);
    frame.resize(0);

    for (const auto& line: atom_lines) {
        if (line.length() < 34) {
            throw format_error(
                "atom line is too small for SDF: '{}'", line
            );
        }

        auto x = parse<double>(line.substr(0, 10));
        auto y = parse<double>(line.substr(10, 10));
        auto z = parse<double>(line.substr(20, 10));
        auto name = trim(line.substr(31, 3));

        auto atom = Atom(name);

        if (line.length() >= 40) {
            long long chrg = 0;
            try {
                chrg = parse<long long>(line.substr(36, 3));
            } catch (const Error&) {
                warning("SDF reader", "charge code not numeric: {}", line.substr(36, 3));
            }
            switch(chrg) {
            case 0:
                break; // do nothing
            case 1:
                atom.set_charge(3.0);
                break;
            case 2:
                atom.set_charge(2.0);
                break;
            case 3:
                atom.set_charge(1.0);
                break;
            case 5:
                atom.set_charge(-1.0);
                break;
            case 6:
                atom.set_charge(-2.0);
                break;
            case 7:
                atom.set_charge(-3.0);
                break;
            default:
                warning("SDF reader", "unknown charge code: '{}'", chrg);
            }
        }

        frame.add_atom(std::move(atom), Vector3D(x, y, z));
    }

    std::vector<std::string> bond_lines;
    try {
        bond_lines = file_->readlines(nbonds);
    } catch (const FileError& e) {
        throw format_error("can not read file: {}", e.what());
    }

    for (const auto& line: bond_lines) {
        auto atom1 = parse<size_t>(line.substr(0, 3));
        auto atom2 = parse<size_t>(line.substr(3, 3));
        auto bondo = parse<size_t>(line.substr(6, 3));

        Bond::BondOrder bo;

        switch (bondo) {
            case 1:
                bo = Bond::SINGLE;
                break;
            case 2:
                bo = Bond::DOUBLE;
                break;
            case 3:
                bo = Bond::TRIPLE;
                break;
            case 4:
                bo = Bond::AROMATIC;
                break;
            case 8: // The 8 ispecifically means unspecified
            default:
                bo = Bond::UNKNOWN;
                break;
        }

        frame.add_bond(atom1 - 1, atom2 - 1, bo);
    }

    // Parsing the file is more or less complete now, but atom properties can
    // still be read (until 'M  END' is reached).
    // This loop breaks when the property block ends or returns on an error
    while(true) {
        try {
            auto line = file_->readline();
            if (line.empty()) {
                continue;
            } else if (line.substr(0, 4) == "$$$$") {
                // Ending block, technically wrong - but we can exit safetly
                return;
            } else if (line.substr(0, 6) == "M  END") {
                // Proper end of block
                break;
            } // TODO: Add actual ATOM property parsing here.....
        } catch (const FileError&) {
            // Premature end of file, but we can safetly end here
            warning("SDF reader", "premature end of file while reading atom property");
            return;
        }
    }

    // This portion of the file is for molecule wide properties.
    // We're done parsing, so just quit if any errors occur
    std::string property_name;
    std::string property_value;
    while(true) {
        try {
            auto line = file_->readline();
            if (line.empty()) {
                // This breaks a property group - so store now
                if (property_name.empty()) {
                    warning("SDF reader", "missing property name");
                    continue;
                }
                frame.set(property_name, Property(property_value));
            } else if (line.substr(0, 4) == "$$$$") {
                // Molecule ending block
                return;
            } else if (line.substr(0, 3) == "> <") {
                // Get the property name
                // It is formated like:
                //> <NAMEGOESHERE>
                const auto npos = line.find_last_of('>');
                property_name = line.substr(3, npos - 3);

                property_value = file_->readline();
            } else {
                // Continuation of a property value
                property_value += '\n';
                property_value += line;
            }
        } catch (const FileError&) {
            warning("SDF reader", "premature end of file while reading global property");
            return;
        }
    }

}

void SDFFormat::write_next(const Frame& frame) {
    auto& topology = frame.topology();
    auto& positions = frame.positions();
    assert(frame.size() == topology.size());

    fmt::print(*file_, "{}\n", frame.get<Property::STRING>("name").value_or("NONAME"));
    fmt::print(*file_, " chemfiles-lib\n\n");
    fmt::print(*file_, "{:>3}{:>3}  0     0  0  0  0  0  0999 V2000\n", frame.size(), topology.bonds().size());

    for (size_t i = 0; i < frame.size(); i++) {
        std::string type = topology[i].type();

        if (type.empty() || type.length() > 3) {
            type = "Xxx";
        }

        int charge_code = 0;
        double int_part;
        if (std::modf(topology[i].charge(), &int_part) == 0.0) {
            switch (static_cast<int>(int_part)) {
            case 0:
                break; // Do nothing
            case 1:
                charge_code = 3;
                break;
            case 2:
                charge_code = 2;
                break;
            case 3:
                charge_code = 1;
                break;
            case -1:
                charge_code = 5;
                break;
            case -2:
                charge_code = 6;
                break;
            case -3:
                charge_code = 7;
                break;
            default:
                warning("SDF writer", "charge code not availible for '{}'", int_part);
            }
        } else {
            warning("SDF writer", "charge not an integer: '{}'", topology[i].charge());
        }

        fmt::print(
            *file_, "{:>10.4f}{:>10.4f}{:>10.4f} {:3} 0{:3}  0  0  0  0  0  0  0  0  0  0\n",
            positions[i][0], positions[i][1], positions[i][2], type, charge_code
        );
    }

    for (const auto& bond : topology.bonds()) {

        std::string bond_order;
        auto bo = topology.bond_order(bond[0], bond[1]);

        switch(bo) {
            case Bond::SINGLE:
                bond_order = "  1";
                break;
            case Bond::DOUBLE:
                bond_order = "  2";
                break;
            case Bond::TRIPLE:
                bond_order = "  3";
                break;
            case Bond::AROMATIC:
                bond_order = "  4";
                break;
            case Bond::UNKNOWN:
            default:
                bond_order = "  8";
                break;
        }

        fmt::print(
            *file_, "{:>3}{:>3}{}  0  0  0  0\n",
            bond[0] + 1, bond[1] + 1, bond_order
        );
    }

    fmt::print(*file_, "M  END\n");

    for (auto& prop : frame.properties()) {
        if (prop.first == "name") {
            continue;
        }

        fmt::print(*file_, "> <{}>\n", prop.first);

        switch(prop.second.kind()) {
        case Property::STRING:
            fmt::print(*file_, "{}\n\n", prop.second.as_string());
            break;
        case Property::DOUBLE:
            fmt::print(*file_, "{}\n\n", prop.second.as_double());
            break;
        case Property::BOOL:
            fmt::print(*file_, "{}\n\n", prop.second.as_bool());
            break;
        case Property::VECTOR3D:
            fmt::print(*file_, "{} {} {}\n\n",
                prop.second.as_vector3d()[0],
                prop.second.as_vector3d()[1],
                prop.second.as_vector3d()[2]
            );
            break;
        }
    }

    fmt::print(*file_, "$$$$\n");
}

std::streampos SDFFormat::forward() {
    if (!*file_) {
        return std::streampos(-1);
    }

    auto position = file_->tellg();
    size_t natoms = 0;
    size_t nbonds = 0;
    try {
        // Ignore junk lines
        file_->skiplines(3);
        std::string counts_line = file_->readline();

        if (counts_line.length() < 10) {
            throw format_error("counts line must have at least 10 digits, it has {}", counts_line.length());
        }

        natoms = parse<size_t>(counts_line.substr(0,3));
        nbonds = parse<size_t>(counts_line.substr(3,3));
    } catch (const FileError&) {
        // No more line left in the file
        return std::streampos(-1);
    } catch (const Error&) {
        // We could not read an integer, so give up here
        return std::streampos(-1);
    }

    try {
        file_->skiplines(natoms + nbonds);
    } catch (const FileError&) {
        // We could not read the lines from the file
        throw format_error(
            "not enough lines in '{}' for SDF format", file_->path()
        );
    }

    // Search for ending character, updating the position in the file only
    while (!file_->eof()) {
        if (file_->readline() == "$$$$") {
            break;
        }
    }

    // We have enough data to parse an entire molecule.
    // So, even if the file does not have an ending string - return true.
    return position;
}
