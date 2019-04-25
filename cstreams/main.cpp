
// MIT License
//
// Copyright (c) 2019 degski
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <random>
#include <sax/iostream.hpp>
#include <string>
#include <sstream>
#include <type_traits>
#include <vector>

#include <cereal/archives/binary.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>

#include "Extensions/LZ4Stream.h"

#include <compressed_streams/lz4_stream.h>

#include <lz4stream.hpp>

#ifdef _DEBUG
#    pragma comment( lib, "compressed_streams.lib" )
#else
#    pragma comment( lib, "compressed_streams.lib" )
#endif

namespace fs = std::filesystem;

void handleEptr ( std::exception_ptr eptr ) { // Passing by value is ok.
    try {
        if ( eptr )
            std::rethrow_exception ( eptr );
    }
    catch ( const std::exception & e ) {
        std::cout << "Caught exception \"" << e.what ( ) << "\"\n";
    }
}

template<typename T>
void saveToFileLZ4 ( const T & t_, fs::path && path_, std::string && file_name_, const bool append_ = false,
                     const int compression_level_ = 9 ) noexcept {
    std::ofstream compressed_ostream ( path_ / ( file_name_ + std::string ( ".lz4cereal" ) ),
                                       append_ ? std::ios::binary | std::ios::app | std::ios::out
                                               : std::ios::binary | std::ios::out );
    {
        sf::LZ4OutputStream lz4_ostream ( compressed_ostream, 0 );
        {
            cereal::BinaryOutputArchive archive ( lz4_ostream );
            archive ( t_ );
        }
        lz4_ostream.flush ( );
    }
    compressed_ostream.flush ( );
    compressed_ostream.close ( );
}

template<typename T>
void loadFromFileLZ4 ( T & t_, fs::path && path_, std::string && file_name_ ) noexcept {
    std::ifstream compressed_istream ( path_ / ( file_name_ + std::string ( ".lz4cereal" ) ), std::ios::binary );
    compressed_streams::Lz4IStream lz4_istream ( compressed_istream );
    {
        cereal::BinaryInputArchive archive ( lz4_istream );
        archive ( t_ );
    }
    compressed_istream.close ( );
}

int main ( ) {

    std::array<int, 16> a{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

    saveToFileLZ4 ( a, "y://tmp//", "cstest" );

    return EXIT_SUCCESS;
}
