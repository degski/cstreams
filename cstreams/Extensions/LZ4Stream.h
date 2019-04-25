
// MIT License
//
// Copyright (c) 2019 Alessandro Bacchini
// https://github.com/allebacco/compressed_streams (commit
// 29b77a1b3aa1d6d600dd3ecf42299152d9e2e884)
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

#pragma once

#include <cassert>

#include <array>
#include <iostream>
#include <vector>

#include <lz4frame.h>

#ifndef SFML_EXTENSIONS_BUILD
#    ifdef _DEBUG
#        pragma comment( lib, "lz4d.lib" )
#    else
#        pragma comment( lib, "lz4.lib" )
#    endif
#endif

namespace sf {

struct LZ4Dictionary {
    LZ4Dictionary ( ) noexcept              = default;
    LZ4Dictionary ( LZ4Dictionary const & ) = delete;
    LZ4Dictionary ( LZ4Dictionary && other_ ) noexcept;
    // Load from resource.
    LZ4Dictionary ( int name_ );
    ~LZ4Dictionary ( );
    [[maybe_unused]] LZ4Dictionary const & operator= ( LZ4Dictionary const & ) = delete;
    [[maybe_unused]] LZ4Dictionary const & operator                            = ( LZ4Dictionary && other_ ) noexcept;
    void loadFromResource ( int name_ );

    private:
    friend class LZ4OStreamBuf;
    friend class LZ4IStreamBuf;
    void * data      = nullptr;
    std::size_t size = 0u;
};

struct LZ4OStream : public std::ostream {
    static constexpr int DEFAULT_COMPRESSION_LEVEL = 0;
    static constexpr int BEST_SPEED                = 1;
    static constexpr int BEST_COMPRESSION          = 9;
    LZ4OStream ( std::ostream & stream_, int const compression_level = DEFAULT_COMPRESSION_LEVEL );
    LZ4OStream ( std::ostream & stream_, LZ4Dictionary const & dictionary_,
                 int const compression_level_ = DEFAULT_COMPRESSION_LEVEL );
    virtual ~LZ4OStream ( );
    void close ( );
};

struct LZ4IStream : public std::istream {
    LZ4IStream ( std::istream & stream_ );
    LZ4IStream ( std::istream & stream_, LZ4Dictionary const & dictionary_ );
    virtual ~LZ4IStream ( );
};

class LZ4OutputStream : public std::ostream {
    public:
    LZ4OutputStream ( std::ostream & sink, const int compression_level_ = 4 ) :
        std::ostream ( new LZ4OutputBuffer ( sink, compression_level_ ) ),
        buffer_ ( static_cast<LZ4OutputBuffer *> ( rdbuf ( ) ) ) {
        assert ( buffer_ );
    }

    ~LZ4OutputStream ( ) {
        close ( );
        delete buffer_;
    }

    void close ( ) { buffer_->close ( ); }

    private:
    class LZ4OutputBuffer : public std::streambuf {
        public:
        LZ4OutputBuffer ( std::ostream & sink, const int compression_level_ );
        ~LZ4OutputBuffer ( );

        LZ4OutputBuffer ( const LZ4OutputBuffer & ) = delete;
        LZ4OutputBuffer & operator= ( const LZ4OutputBuffer & ) = delete;
        void close ( );

        private:
        int_type overflow ( int_type ch ) override;
        int_type sync ( ) override;
        void compressAndWrite ( );
        void writeHeader ( );
        void writeFooter ( );

        std::ostream & sink_;
        std::array<char, 256> src_buf_;
        std::vector<char> dest_buf_;
        LZ4F_compressionContext_t ctx_;
        LZ4F_preferences_t preferences_;
        bool closed_;
    };

    LZ4OutputBuffer * buffer_;
};

class LZ4InputStream : public std::istream {
    public:
    LZ4InputStream ( std::istream & source ) :
        std::istream ( new LZ4InputBuffer ( source ) ), buffer_ ( static_cast<LZ4InputBuffer *> ( rdbuf ( ) ) ) {
        assert ( buffer_ );
    }

    ~LZ4InputStream ( ) { delete buffer_; }

    private:
    class LZ4InputBuffer : public std::streambuf {
        public:
        LZ4InputBuffer ( std::istream & source );
        ~LZ4InputBuffer ( );

        int_type underflow ( ) override;
        LZ4InputBuffer ( const LZ4InputBuffer & ) = delete;
        LZ4InputBuffer & operator= ( const LZ4InputBuffer & ) = delete;

        private:
        std::istream & source_;
        std::array<char, 64 * 1024> src_buf_;
        std::array<char, 64 * 1024> dest_buf_;
        std::size_t offset_;
        std::size_t src_buf_size_;
        LZ4F_decompressionContext_t ctx_;
    };

    LZ4InputBuffer * buffer_;
};

} // namespace sf
