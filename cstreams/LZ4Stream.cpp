
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

#include "Extensions/LZ4Stream.h"

#include <Windows.h>

#include <cstring>

#include <algorithm>
#include <iostream>
#include <vector>

#include <lz4frame.h>

namespace sf {

LZ4Dictionary::LZ4Dictionary ( LZ4Dictionary && other_ ) noexcept {
    data        = other_.data;
    size        = other_.size;
    other_.data = nullptr;
    other_.size = 0u;
}

LZ4Dictionary::LZ4Dictionary ( int name_ ) { loadFromResource ( name_ ); }

LZ4Dictionary::~LZ4Dictionary ( ) {
    if ( nullptr != data )
        LZ4F_freeCDict ( ( LZ4F_CDict * ) data );
}

[[maybe_unused]] LZ4Dictionary const & LZ4Dictionary::operator= ( LZ4Dictionary && other_ ) noexcept {
    data        = other_.data;
    size        = other_.size;
    other_.data = nullptr;
    other_.size = 0u;
    return *this;
}

void LZ4Dictionary::loadFromResource ( int name_ ) {
    HRSRC rsrc_data = FindResource ( NULL, MAKEINTRESOURCE ( name_ ), L"FILEDATA" );
    if ( not rsrc_data )
        throw std::runtime_error ( "Failed to find resource." );
    DWORD rsrc_data_size = SizeofResource ( NULL, rsrc_data );
    if ( rsrc_data_size <= 0 )
        throw std::runtime_error ( "Size of resource is 0." );
    else
        size = ( std::size_t ) rsrc_data_size;
    HGLOBAL grsrc_data = LoadResource ( NULL, rsrc_data );
    if ( not grsrc_data )
        throw std::runtime_error ( "Failed to load resource." );
    LPVOID first_byte = LockResource ( grsrc_data );
    if ( not first_byte )
        throw std::runtime_error ( "Failed to lock resource." );
    data = LZ4F_createCDict ( first_byte, size );
    if ( not data )
        throw std::runtime_error ( "Failed to load LZ4-dictionary." );
}

static constexpr LZ4F_preferences_t DEFAULT_PREFERENCES = {
    { LZ4F_max256KB, LZ4F_blockLinked, LZ4F_noContentChecksum, LZ4F_frame, 0 /* unknown content size */, 0 /* no dictID */,
      LZ4F_noBlockChecksum },
    0,           /* compression level; 0 == default */
    0,           /* autoflush */
    0,           /* favor decompression speed */
    { 0, 0, 0 }, /* reserved, must be set to 0 */
};

class LZ4OStreamBuf final : public std::streambuf {
    public:
    LZ4OStreamBuf ( std::streambuf * buffer, int const compression_level_ ) :
        m_sink ( buffer ), m_preferences ( DEFAULT_PREFERENCES ), m_dictionary ( nullptr ) {
        std::size_t ctx_creation = LZ4F_createCompressionContext ( &m_compression_ctx, LZ4F_VERSION );
        if ( LZ4F_isError ( ctx_creation ) )
            throw std::runtime_error ( "Error during LZ4 stream creation" );
        m_preferences.compressionLevel = compression_level_;
        // Setup buffers.
        std::size_t internal_buffer_size_ = LZ4F_compressBound ( 0, &m_preferences );
        internal_buffer_size_             = std::max<std::size_t> ( internal_buffer_size_, LZ4F_HEADER_SIZE_MAX ) + 1;
        m_write_area.resize ( internal_buffer_size_ );
        m_compression_buffer.resize ( internal_buffer_size_ );
        // Setup the write are buffer. Last byte is for the overflow operation.
        setp ( &m_write_area.front ( ), &m_write_area.front ( ) + m_write_area.size ( ) - 1 );
        initialize_stream ( );
    }
    LZ4OStreamBuf ( std::streambuf * buffer, int const compression_level_, LZ4Dictionary const & dictionary_ ) :
        m_sink ( buffer ), m_preferences ( DEFAULT_PREFERENCES ), m_dictionary ( &dictionary_ ) {
        std::size_t ctx_creation = LZ4F_createCompressionContext ( &m_compression_ctx, LZ4F_VERSION );
        if ( LZ4F_isError ( ctx_creation ) )
            throw std::runtime_error ( "Error during LZ4 stream creation" );
        m_preferences.compressionLevel = compression_level_;
        // Setup buffers.
        std::size_t internal_buffer_size_ = LZ4F_compressBound ( 0, &m_preferences );
        internal_buffer_size_             = std::max<std::size_t> ( internal_buffer_size_, LZ4F_HEADER_SIZE_MAX ) + 1;
        m_write_area.resize ( internal_buffer_size_ );
        m_compression_buffer.resize ( internal_buffer_size_ );
        // Setup the write are buffer. Last byte is for the overflow operation.
        setp ( &m_write_area.front ( ), &m_write_area.front ( ) + m_write_area.size ( ) - 1 );
        initialize_stream ( );
    }

    virtual ~LZ4OStreamBuf ( ) {
        close ( );
        LZ4F_freeCompressionContext ( m_compression_ctx );
    }

    void close ( ) {
        static bool is_open = true;
        if ( is_open ) {
            compress_buffer ( );
            m_sink->pubsync ( );
            std::size_t compressed_size =
                LZ4F_compressEnd ( m_compression_ctx, m_compression_buffer.data ( ), m_compression_buffer.size ( ), nullptr );
            if ( LZ4F_isError ( compressed_size ) )
                throw std::runtime_error ( "Error during LZ4 stream finalization" );
            m_sink->sputn ( m_compression_buffer.data ( ), compressed_size );
            is_open = false;
        }
    }

    protected:
    [[nodiscard]] virtual int_type overflow ( int_type ch ) override {
        *pptr ( ) = traits_type::to_char_type ( ch );
        pbump ( 1 );
        compress_buffer ( );
        return ch;
    }

    [[nodiscard]] virtual int sync ( ) override {
        compress_buffer ( );
        return m_sink->pubsync ( );
    }

    private:
    void initialize_stream ( ) {
        std::size_t header_size = 0u;
        if ( m_dictionary )
            header_size =
                LZ4F_compressBegin_usingCDict ( m_compression_ctx, m_compression_buffer.data ( ), m_compression_buffer.size ( ),
                                                ( LZ4F_CDict const * ) m_dictionary->data, &m_preferences );
        else
            header_size = LZ4F_compressBegin ( m_compression_ctx, m_compression_buffer.data ( ), m_compression_buffer.size ( ),
                                               &m_preferences );
        if ( LZ4F_isError ( header_size ) )
            throw std::runtime_error ( "Error during LZ4 stream initialization" );
        m_sink->sputn ( m_compression_buffer.data ( ), header_size );
    }

    void finalize_stream ( ) {
        compress_buffer ( );
        std::size_t compressed_size =
            LZ4F_compressEnd ( m_compression_ctx, m_compression_buffer.data ( ), m_compression_buffer.size ( ), nullptr );
        if ( LZ4F_isError ( compressed_size ) )
            throw std::runtime_error ( "Error during LZ4 stream finalization" );
        m_sink->sputn ( m_compression_buffer.data ( ), compressed_size );
    }

    [[maybe_unused]] std::size_t compress_buffer ( ) {
        std::size_t num_bytes       = std::distance ( pbase ( ), pptr ( ) );
        std::size_t compressed_size = LZ4F_compressUpdate ( m_compression_ctx, m_compression_buffer.data ( ),
                                                            m_compression_buffer.size ( ), pbase ( ), num_bytes, nullptr );
        if ( LZ4F_isError ( compressed_size ) )
            throw std::runtime_error ( "Error during LZ4 stream writing" );
        setp ( &m_write_area.front ( ), &m_write_area.front ( ) + m_write_area.size ( ) - 1 );
        return m_sink->sputn ( m_compression_buffer.data ( ), compressed_size );
    }

    private : std::streambuf * m_sink;
    LZ4F_cctx * m_compression_ctx;
    LZ4F_preferences_t m_preferences;
    LZ4Dictionary const * m_dictionary;
    std::vector<char> m_write_area;
    std::vector<char> m_compression_buffer;
};

class LZ4IStreamBuf final : public std::streambuf {
    public:
    LZ4IStreamBuf ( std::streambuf * source_, std::size_t const internal_buffer_size_ = 4096u ) :
        m_source ( source_ ), m_context ( nullptr ), m_dictionary ( nullptr ), m_src_buffer ( internal_buffer_size_ ),
        m_read_area ( internal_buffer_size_ ), m_src_offset ( 0 ), m_src_size ( 0 ) {
        std::size_t status = LZ4F_createDecompressionContext ( &m_context, LZ4F_VERSION );
        if ( LZ4F_isError ( status ) )
            throw std::runtime_error ( "Error during LZ4 istream creation" );
        setg ( &m_read_area.front ( ), &m_read_area.front ( ), &m_read_area.front ( ) );
    }
    LZ4IStreamBuf ( std::streambuf * source_, LZ4Dictionary const & dictionary_, std::size_t const internal_buffer_size_ = 4096u ) :
        m_source ( source_ ), m_context ( nullptr ), m_dictionary ( &dictionary_ ), m_src_buffer ( internal_buffer_size_ ),
        m_read_area ( internal_buffer_size_ ), m_src_offset ( 0 ), m_src_size ( 0 ) {
        std::size_t status = LZ4F_createDecompressionContext ( &m_context, LZ4F_VERSION );
        if ( LZ4F_isError ( status ) )
            throw std::runtime_error ( "Error during LZ4 istream creation" );
        setg ( &m_read_area.front ( ), &m_read_area.front ( ), &m_read_area.front ( ) );
    }

    virtual ~LZ4IStreamBuf ( ) { LZ4F_freeDecompressionContext ( m_context ); }

    protected:
    [[nodiscard]] virtual int_type underflow ( ) override {
        while ( true ) {
            if ( m_src_offset == m_src_size ) {
                m_src_size   = m_source->sgetn ( &m_src_buffer.front ( ), m_src_buffer.size ( ) );
                m_src_offset = 0;
            }
            if ( m_src_size == 0 )
                return traits_type::eof ( );
            std::size_t src_avalable = m_src_size - m_src_offset;
            std::size_t dest_size    = m_read_area.size ( );
            std::size_t ret          = 0u;
            if ( m_dictionary )
                ret = LZ4F_decompress_usingDict ( m_context, &m_read_area.front ( ), &dest_size,
                                                  &m_src_buffer.front ( ) + m_src_offset, &src_avalable, m_dictionary->data,
                                                  m_dictionary->size, nullptr );
            else
                ret = LZ4F_decompress ( m_context, &m_read_area.front ( ), &dest_size, &m_src_buffer.front ( ) + m_src_offset,
                                        &src_avalable, nullptr );
            m_src_offset += src_avalable;
            if ( LZ4F_isError ( ret ) != 0 )
                throw std::runtime_error ( "Error during LZ4 decompression" );
            if ( dest_size > 0 ) {
                setg ( &m_read_area.front ( ), &m_read_area.front ( ), &m_read_area.front ( ) + dest_size );
                return traits_type::to_int_type ( *gptr ( ) );
            }
        }
        return traits_type::eof ( );
    }

    private:
    std::streambuf * m_source;
    LZ4F_dctx * m_context;
    LZ4Dictionary const * m_dictionary;
    std::vector<char> m_src_buffer;
    std::vector<char> m_read_area;
    std::size_t m_src_offset;
    std::size_t m_src_size;
};

LZ4OStream::LZ4OStream ( std::ostream & stream_, int const compression_level_ ) :
    std::ostream ( new LZ4OStreamBuf ( stream_.rdbuf ( ), compression_level_ ) ) {}
LZ4OStream::LZ4OStream ( std::ostream & stream_, LZ4Dictionary const & dictionary_, int const compression_level_ ) :
    std::ostream ( new LZ4OStreamBuf ( stream_.rdbuf ( ), compression_level_, dictionary_ ) ) {}
LZ4OStream::~LZ4OStream ( ) { delete rdbuf ( ); }
void LZ4OStream::close ( ) { dynamic_cast<LZ4OStreamBuf *> ( rdbuf ( ) )->close ( ); }

LZ4IStream::LZ4IStream ( std::istream & stream_ ) : std::istream ( new LZ4IStreamBuf ( stream_.rdbuf ( ) ) ) {}
LZ4IStream::LZ4IStream ( std::istream & stream_, LZ4Dictionary const & dictionary_ ) :
    std::istream ( new LZ4IStreamBuf ( stream_.rdbuf ( ), dictionary_ ) ) {}
LZ4IStream::~LZ4IStream ( ) { delete rdbuf ( ); }

LZ4OutputStream::LZ4OutputBuffer::LZ4OutputBuffer ( std::ostream & sink, const int compression_level_ ) :
    sink_ ( sink ), src_buf_ ( ), preferences_ ( LZ4F_INIT_PREFERENCES ), closed_ ( false ) {
    char * base = &src_buf_.front ( );
    setp ( base, base + src_buf_.size ( ) - 1 );
    std::size_t ret = LZ4F_createCompressionContext ( &ctx_, LZ4F_VERSION );
    if ( LZ4F_isError ( ret ) != 0 )
        throw std::runtime_error ( std::string ( "Failed to create LZ4 compression context: " ) + LZ4F_getErrorName ( ret ) );
    preferences_.compressionLevel = compression_level_;
    // TODO: No need to recalculate the dest_buf_ size on each construction
    dest_buf_.resize ( LZ4F_compressBound ( src_buf_.size ( ), &preferences_ ) );
    writeHeader ( );
}

LZ4OutputStream::LZ4OutputBuffer::~LZ4OutputBuffer ( ) { close ( ); }

LZ4OutputStream::int_type LZ4OutputStream::LZ4OutputBuffer::overflow ( int_type ch ) {
    assert ( std::less_equal<char *> ( ) ( pptr ( ), epptr ( ) ) );
    *pptr ( ) = static_cast<LZ4OutputStream::char_type> ( ch );
    pbump ( 1 );
    compressAndWrite ( );
    return ch;
}

LZ4OutputStream::int_type LZ4OutputStream::LZ4OutputBuffer::sync ( ) {
    compressAndWrite ( );
    return 0;
}

void LZ4OutputStream::LZ4OutputBuffer::compressAndWrite ( ) {
    assert ( !closed_ );
    std::ptrdiff_t orig_size = pptr ( ) - pbase ( );
    pbump ( -orig_size );
    std::size_t comp_size = LZ4F_compressUpdate ( ctx_, &dest_buf_.front ( ), dest_buf_.size ( ), pbase ( ), orig_size, nullptr );
    sink_.write ( &dest_buf_.front ( ), comp_size );
}

void LZ4OutputStream::LZ4OutputBuffer::writeHeader ( ) {
    assert ( !closed_ );
    std::size_t ret = LZ4F_compressBegin ( ctx_, &dest_buf_.front ( ), dest_buf_.size ( ), &preferences_ );
    if ( LZ4F_isError ( ret ) != 0 )
        throw std::runtime_error ( std::string ( "Failed to start LZ4 compression: " ) + LZ4F_getErrorName ( ret ) );
    sink_.write ( &dest_buf_.front ( ), ret );
}

void LZ4OutputStream::LZ4OutputBuffer::writeFooter ( ) {
    assert ( !closed_ );
    std::size_t ret = LZ4F_compressEnd ( ctx_, &dest_buf_.front ( ), dest_buf_.size ( ), nullptr );
    if ( LZ4F_isError ( ret ) != 0 )
        throw std::runtime_error ( std::string ( "Failed to end LZ4 compression: " ) + LZ4F_getErrorName ( ret ) );
    sink_.write ( &dest_buf_.front ( ), ret );
}

void LZ4OutputStream::LZ4OutputBuffer::close ( ) {
    if ( closed_ )
        return;
    sync ( );
    writeFooter ( );
    LZ4F_freeCompressionContext ( ctx_ );
    closed_ = true;
}

LZ4InputStream::LZ4InputBuffer::LZ4InputBuffer ( std::istream & source ) :
    source_ ( source ), src_buf_ ( ), dest_buf_ ( ), offset_ ( 0 ), src_buf_size_ ( 0 ), ctx_ ( nullptr ) {
    std::size_t ret = LZ4F_createDecompressionContext ( &ctx_, LZ4F_VERSION );
    if ( LZ4F_isError ( ret ) != 0 )
        throw std::runtime_error ( std::string ( "Failed to create LZ4 decompression context: " ) + LZ4F_getErrorName ( ret ) );
    setg ( &src_buf_.front ( ), &src_buf_.front ( ), &src_buf_.front ( ) );
}

LZ4InputStream::int_type LZ4InputStream::LZ4InputBuffer::underflow ( ) {
    size_t written_size = 0;
    while ( written_size == 0 ) {
        if ( offset_ == src_buf_size_ ) {
            source_.read ( &src_buf_.front ( ), src_buf_.size ( ) );
            src_buf_size_ = static_cast<size_t> ( source_.gcount ( ) );
            offset_       = 0;
        }

        if ( src_buf_size_ == 0 ) {
            return traits_type::eof ( );
        }

        size_t src_size  = src_buf_size_ - offset_;
        size_t dest_size = dest_buf_.size ( );
        size_t ret = LZ4F_decompress ( ctx_, &dest_buf_.front ( ), &dest_size, &src_buf_.front ( ) + offset_, &src_size, nullptr );
        if ( LZ4F_isError ( ret ) != 0 ) {
            throw std::runtime_error ( std::string ( "LZ4 decompression failed: " ) + LZ4F_getErrorName ( ret ) );
        }
        written_size = dest_size;
        offset_ += src_size;
    }
    setg ( &dest_buf_.front ( ), &dest_buf_.front ( ), &dest_buf_.front ( ) + written_size );
    return traits_type::to_int_type ( *gptr ( ) );
}

LZ4InputStream::LZ4InputBuffer::~LZ4InputBuffer ( ) { LZ4F_freeDecompressionContext ( ctx_ ); }

} // namespace sf
